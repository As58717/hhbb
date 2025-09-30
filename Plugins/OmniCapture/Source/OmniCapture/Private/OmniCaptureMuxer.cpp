#include "OmniCaptureMuxer.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString FOmniCaptureMuxer::ResolveFFmpegBinary(const FOmniCaptureSettings& Settings)
{
    if (!Settings.PreferredFFmpegPath.IsEmpty())
    {
        return Settings.PreferredFFmpegPath;
    }

    FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("OMNICAPTURE_FFMPEG"));
    if (!EnvPath.IsEmpty())
    {
        return EnvPath;
    }

    return TEXT("ffmpeg");
}

bool FOmniCaptureMuxer::IsFFmpegAvailable(const FOmniCaptureSettings& Settings, FString* OutResolvedPath)
{
    const FString Resolved = ResolveFFmpegBinary(Settings);
    if (OutResolvedPath)
    {
        *OutResolvedPath = Resolved;
    }

    if (Resolved.IsEmpty())
    {
        return false;
    }

    if (Resolved.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase))
    {
        return true;
    }

    return FPaths::FileExists(Resolved);
}

void FOmniCaptureMuxer::Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory)
{
    OutputDirectory = InOutputDirectory.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("OmniCaptures")) : InOutputDirectory;
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    BaseFileName = Settings.OutputFileName.IsEmpty() ? TEXT("OmniCapture") : Settings.OutputFileName;

    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    CachedFFmpegPath = ResolveFFmpegBinary(Settings);
}

void FOmniCaptureMuxer::BeginRealtimeSession(const FOmniCaptureSettings& Settings)
{
    AudioStats = FOmniAudioSyncStats();
    LastVideoTimestamp = 0.0;
    LastAudioTimestamp = 0.0;
    DriftWarningThresholdMs = Settings.bForceConstantFrameRate ? 20.0 : 35.0;
    bRealtimeSessionActive = true;
}

void FOmniCaptureMuxer::EndRealtimeSession()
{
    bRealtimeSessionActive = false;
    AudioStats = FOmniAudioSyncStats();
    LastVideoTimestamp = 0.0;
    LastAudioTimestamp = 0.0;
}

void FOmniCaptureMuxer::PushFrame(const FOmniCaptureFrame& Frame)
{
    if (!bRealtimeSessionActive)
    {
        return;
    }

    LastVideoTimestamp = Frame.Metadata.Timecode;
    AudioStats.LatestVideoTimestamp = LastVideoTimestamp;

    int32 PacketCount = 0;
    double LatestAudioTime = LastAudioTimestamp;

    for (const FOmniAudioPacket& Packet : Frame.AudioPackets)
    {
        const double Duration = (Packet.SampleRate > 0 && Packet.NumChannels > 0)
            ? static_cast<double>(Packet.PCM16.Num()) / (static_cast<double>(Packet.SampleRate) * FMath::Max(Packet.NumChannels, 1))
            : 0.0;
        LatestAudioTime = FMath::Max(LatestAudioTime, Packet.Timestamp + Duration);
        ++PacketCount;
    }

    LastAudioTimestamp = LatestAudioTime;
    AudioStats.LatestAudioTimestamp = LastAudioTimestamp;
    AudioStats.PendingPackets = PacketCount;
    AudioStats.DriftMilliseconds = (LastAudioTimestamp - LastVideoTimestamp) * 1000.0;
    AudioStats.MaxObservedDriftMilliseconds = FMath::Max(AudioStats.MaxObservedDriftMilliseconds, FMath::Abs(AudioStats.DriftMilliseconds));
    AudioStats.bInError = FMath::Abs(AudioStats.DriftMilliseconds) > DriftWarningThresholdMs;
}

bool FOmniCaptureMuxer::FinalizeCapture(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath)
{
    FString ManifestPath;
    if (!WriteManifest(Settings, Frames, AudioPath, VideoPath, ManifestPath))
    {
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("OmniCapture manifest written to %s"), *ManifestPath);

    TryInvokeFFmpeg(Settings, Frames, AudioPath, VideoPath);

    return true;
}

bool FOmniCaptureMuxer::WriteManifest(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath, FString& OutManifestPath) const
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("fileBase"), BaseFileName);
    Root->SetStringField(TEXT("directory"), OutputDirectory);
    Root->SetStringField(TEXT("outputFormat"), Settings.OutputFormat == EOmniOutputFormat::PNGSequence ? TEXT("PNGSequence") : TEXT("NVENC"));
    Root->SetStringField(TEXT("mode"), Settings.Mode == EOmniCaptureMode::Stereo ? TEXT("Stereo") : TEXT("Mono"));
    Root->SetStringField(TEXT("gamma"), Settings.Gamma == EOmniCaptureGamma::Linear ? TEXT("Linear") : TEXT("sRGB"));
    Root->SetNumberField(TEXT("resolution"), Settings.Resolution);
    Root->SetNumberField(TEXT("frameCount"), Frames.Num());
    Root->SetNumberField(TEXT("frameRate"), CalculateFrameRate(Frames));
    Root->SetStringField(TEXT("stereoLayout"), Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? TEXT("TopBottom") : TEXT("SideBySide"));
    switch (Settings.ColorSpace)
    {
    case EOmniCaptureColorSpace::BT2020:
        Root->SetStringField(TEXT("colorSpace"), TEXT("BT.2020"));
        break;
    case EOmniCaptureColorSpace::HDR10:
        Root->SetStringField(TEXT("colorSpace"), TEXT("HDR10"));
        break;
    default:
        Root->SetStringField(TEXT("colorSpace"), TEXT("BT.709"));
        break;
    }
    Root->SetStringField(TEXT("audio"), AudioPath);
    const FString FinalVideo = OutputDirectory / (BaseFileName + TEXT(".mp4"));
    Root->SetStringField(TEXT("videoFile"), FinalVideo);
    if (!VideoPath.IsEmpty())
    {
        Root->SetStringField(TEXT("nvencBitstream"), VideoPath);
    }
    Root->SetBoolField(TEXT("zeroCopy"), Settings.bZeroCopy);
    Root->SetStringField(TEXT("codec"), Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H264"));
    switch (Settings.NVENCColorFormat)
    {
    case EOmniCaptureColorFormat::NV12:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("NV12"));
        break;
    case EOmniCaptureColorFormat::P010:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("P010"));
        break;
    case EOmniCaptureColorFormat::BGRA:
        Root->SetStringField(TEXT("nvencColorFormat"), TEXT("BGRA"));
        break;
    }

    TArray<TSharedPtr<FJsonValue>> FrameArray;
    FrameArray.Reserve(Frames.Num());
    for (const FOmniCaptureFrameMetadata& Metadata : Frames)
    {
        TSharedRef<FJsonObject> FrameObject = MakeShared<FJsonObject>();
        FrameObject->SetNumberField(TEXT("index"), Metadata.FrameIndex);
        FrameObject->SetNumberField(TEXT("timecode"), Metadata.Timecode);
        FrameObject->SetBoolField(TEXT("keyFrame"), Metadata.bKeyFrame);
        FrameArray.Add(MakeShared<FJsonValueObject>(FrameObject));
    }
    Root->SetArrayField(TEXT("frames"), FrameArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        return false;
    }

    OutManifestPath = OutputDirectory / (BaseFileName + TEXT("_Manifest.json"));
    return FFileHelper::SaveStringToFile(OutputString, *OutManifestPath);
}

bool FOmniCaptureMuxer::TryInvokeFFmpeg(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath) const
{
    if (Frames.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No frames captured; skipping FFmpeg mux."));
        return false;
    }

    if (CachedFFmpegPath.IsEmpty())
    {
        CachedFFmpegPath = BuildFFmpegBinaryPath();
    }

    const FString Binary = CachedFFmpegPath;
    if (Binary.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg not configured. Skipping automatic muxing."));
        return false;
    }

    if (!Binary.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase) && !FPaths::FileExists(Binary))
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg binary %s was not found on disk."), *Binary);
        return false;
    }

    const double FrameRate = CalculateFrameRate(Frames);
    const double EffectiveFrameRate = FrameRate <= 0.0 ? 30.0 : FrameRate;

    FString ColorSpaceArg = TEXT("bt709");
    FString ColorPrimariesArg = TEXT("bt709");
    FString ColorTransferArg = TEXT("bt709");
    FString PixelFormatArg = TEXT("yuv420p");

    switch (Settings.ColorSpace)
    {
    case EOmniCaptureColorSpace::BT2020:
        ColorSpaceArg = TEXT("bt2020nc");
        ColorPrimariesArg = TEXT("bt2020");
        ColorTransferArg = TEXT("bt2020-10");
        PixelFormatArg = TEXT("yuv420p10le");
        break;
    case EOmniCaptureColorSpace::HDR10:
        ColorSpaceArg = TEXT("bt2020nc");
        ColorPrimariesArg = TEXT("bt2020");
        ColorTransferArg = TEXT("smpte2084");
        PixelFormatArg = TEXT("yuv420p10le");
        break;
    default:
        break;
    }

    FString OutputFile = OutputDirectory / (BaseFileName + TEXT(".mp4"));
    FString CommandLine;

    if (Settings.OutputFormat == EOmniOutputFormat::PNGSequence)
    {
        FString Pattern = OutputDirectory / FString::Printf(TEXT("%s_%%06d.png"), *BaseFileName);
        CommandLine = FString::Printf(TEXT("-y -framerate %.3f -i \"%s\""), EffectiveFrameRate, *Pattern);
    }
    else if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        const FString BitstreamPath = !VideoPath.IsEmpty() ? VideoPath : (OutputDirectory / (BaseFileName + TEXT(".h264")));
        if (!FPaths::FileExists(BitstreamPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("NVENC bitstream %s not found; skipping FFmpeg mux."), *BitstreamPath);
            return false;
        }

        CommandLine = FString::Printf(TEXT("-y -framerate %.3f -i \"%s\""), EffectiveFrameRate, *BitstreamPath);
    }
    else
    {
        return false;
    }

    if (!AudioPath.IsEmpty() && FPaths::FileExists(AudioPath))
    {
        CommandLine += FString::Printf(TEXT(" -i \"%s\" -c:a aac -b:a 192k"), *AudioPath);
    }
    else
    {
        CommandLine += TEXT(" -an");
        if (!AudioPath.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("Audio file %s was not found; muxed output will be silent."), *AudioPath);
        }
    }

    const TCHAR* StereoMode = Settings.Mode == EOmniCaptureMode::Stereo ? TEXT("top-bottom") : TEXT("mono");
    if (Settings.Mode == EOmniCaptureMode::Stereo)
    {
        StereoMode = Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? TEXT("top-bottom") : TEXT("left-right");
    }

    if (Settings.OutputFormat == EOmniOutputFormat::PNGSequence)
    {
        const TCHAR* CodecName = Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("libx265") : TEXT("libx264");
        CommandLine += FString::Printf(TEXT(" -c:v %s -pix_fmt %s"), CodecName, *PixelFormatArg);
    }
    else if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        CommandLine += TEXT(" -c:v copy");
    }

    CommandLine += FString::Printf(TEXT(" -metadata:s:v:0 spherical_video=1 -metadata:s:v:0 projection=equirectangular -metadata:s:v:0 stereo_mode=%s"), StereoMode);
    CommandLine += FString::Printf(TEXT(" -colorspace %s -color_primaries %s -color_trc %s"), *ColorSpaceArg, *ColorPrimariesArg, *ColorTransferArg);

    if (Settings.bForceConstantFrameRate)
    {
        CommandLine += TEXT(" -vsync cfr");
    }

    if (Settings.bEnableFastStart)
    {
        CommandLine += TEXT(" -movflags +faststart");
    }

    CommandLine += FString::Printf(TEXT(" -shortest \"%s\""), *OutputFile);

    UE_LOG(LogTemp, Log, TEXT("Invoking FFmpeg: %s %s"), *Binary, *CommandLine);

    FProcHandle ProcHandle = FPlatformProcess::CreateProc(*Binary, *CommandLine, true, true, true, nullptr, 0, *OutputDirectory, nullptr);
    if (!ProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to launch FFmpeg process."));
        return false;
    }

    FPlatformProcess::WaitForProc(ProcHandle);
    int32 ReturnCode = 0;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
    if (ReturnCode != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpeg returned non-zero exit code %d"), ReturnCode);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("FFmpeg muxing complete: %s"), *OutputFile);
    return true;
}

FString FOmniCaptureMuxer::BuildFFmpegBinaryPath() const
{
    return ResolveFFmpegBinary(FOmniCaptureSettings());
}

double FOmniCaptureMuxer::CalculateFrameRate(const TArray<FOmniCaptureFrameMetadata>& Frames) const
{
    if (Frames.Num() < 2)
    {
        return 30.0;
    }

    double Duration = Frames.Last().Timecode - Frames[0].Timecode;
    if (Duration <= 0.0)
    {
        return 30.0;
    }

    return static_cast<double>(Frames.Num() - 1) / Duration;
}
