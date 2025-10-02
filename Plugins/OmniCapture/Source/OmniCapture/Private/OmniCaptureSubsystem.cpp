#include "OmniCaptureSubsystem.h"

#include "OmniCaptureAudioRecorder.h"
#include "OmniCaptureDirectorActor.h"
#include "OmniCaptureEquirectConverter.h"
#include "OmniCaptureNVENCEncoder.h"
#include "OmniCapturePNGWriter.h"
#include "OmniCaptureRigActor.h"
#include "OmniCaptureRingBuffer.h"
#include "OmniCapturePreviewActor.h"
#include "OmniCaptureMuxer.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "RenderingThread.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "RHI.h"
#include "PixelFormat.h"
#include "Math/UnrealMathUtility.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureSubsystem, Log, All);

namespace OmniCapture
{
    static constexpr TCHAR RigActorName[] = TEXT("OmniCaptureRig");
    static constexpr TCHAR DirectorActorName[] = TEXT("OmniCaptureDirector");
    static const FString WarningLowDisk = TEXT("Storage space is low for OmniCapture output");
    static const FString WarningFrameDrop = TEXT("Frame drops detected - rendering slower than encode path");
    static const FString WarningLowFps = TEXT("Capture frame rate is below the configured target");
}

void UOmniCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("OmniCapture subsystem initialized"));
}

void UOmniCaptureSubsystem::Deinitialize()
{
    EndCapture(false);
    Super::Deinitialize();
}

void UOmniCaptureSubsystem::BeginCapture(const FOmniCaptureSettings& InSettings)
{
    if (bIsCapturing)
    {
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("Capture already running"));
        return;
    }

    if (InSettings.Resolution <= 0)
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Invalid capture resolution"));
        return;
    }

    OriginalSettings = InSettings;
    ActiveSettings = InSettings;
    ActiveSettings.OutputDirectory = BuildOutputDirectory();

    BaseOutputDirectory = ActiveSettings.OutputDirectory;
    BaseOutputFileName = ActiveSettings.OutputFileName.IsEmpty() ? TEXT("OmniCapture") : ActiveSettings.OutputFileName;
    CurrentSegmentIndex = 0;
    CapturedFrameMetadata.Empty();
    CompletedSegments.Empty();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    LastFinalizedOutput.Empty();
    LastStillImagePath.Empty();
    OutputMuxer.Reset();

    ActiveWarnings.Empty();
    LatestRingBufferStats = FOmniCaptureRingBufferStats();
    AudioStats = FOmniAudioSyncStats();
    ResetDynamicWarnings();

    bIsPaused = false;
    bDroppedFrames = false;
    DroppedFrameCount = 0;
    CurrentCaptureFPS = 0.0;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;
    LastRuntimeWarningCheckTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = LastRuntimeWarningCheckTime;

    const bool bEnvironmentOk = ValidateEnvironment();
    if (!ApplyFallbacks())
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Capture aborted due to environment validation failure."));
        return;
    }
    if (!bEnvironmentOk && ActiveWarnings.Num() > 0)
    {
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("Capture environment warnings: %s"), *FString::Join(ActiveWarnings, TEXT("; ")));
    }

    ConfigureActiveSegment();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Invalid world context for capture"));
        return;
    }

    CreateRig();
    if (!RigActor.IsValid())
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Failed to create capture rig"));
        return;
    }

    CreateTickActor();
    if (!TickActor.IsValid())
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Failed to create tick actor"));
        DestroyRig();
        return;
    }

    SpawnPreviewActor();

    InitializeOutputWriters();

    OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    if (OutputMuxer)
    {
        OutputMuxer->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
    }

    RingBuffer = MakeUnique<FOmniCaptureRingBuffer>();
    RingBuffer->Initialize(ActiveSettings, [this](TUniquePtr<FOmniCaptureFrame>&& Frame)
    {
        if (!Frame.IsValid())
        {
            return;
        }

        if (OutputMuxer)
        {
            OutputMuxer->PushFrame(*Frame);
            AudioStats = OutputMuxer->GetAudioStats();
            if (AudioRecorder)
            {
                AudioStats.PendingPackets += AudioRecorder->GetPendingPacketCount();
            }
        }

        switch (ActiveSettings.OutputFormat)
        {
        case EOmniOutputFormat::PNGSequence:
            if (PNGWriter)
            {
                const FString FileName = BuildFrameFileName(Frame->Metadata.FrameIndex, TEXT(".png"));
                PNGWriter->EnqueueFrame(MoveTemp(Frame), FileName);
            }
            break;
        case EOmniOutputFormat::NVENCHardware:
            if (NVENCEncoder)
            {
                NVENCEncoder->EnqueueFrame(*Frame);
            }
            break;
        default:
            break;
        }

        if (RingBuffer.IsValid())
        {
            LatestRingBufferStats = RingBuffer->GetStats();
            if (LatestRingBufferStats.DroppedFrames > DroppedFrameCount)
            {
                DroppedFrameCount = LatestRingBufferStats.DroppedFrames;
                HandleDroppedFrame();
            }
        }
    });

    InitializeAudioRecording();

    bIsCapturing = true;
    bDroppedFrames = false;
    DroppedFrameCount = 0;
    FrameCounter = 0;
    CaptureStartTime = FPlatformTime::Seconds();
    CurrentSegmentStartTime = CaptureStartTime;
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
    LastRuntimeWarningCheckTime = CurrentSegmentStartTime;
    PreviewFrameInterval = (ActiveSettings.bEnablePreviewWindow && ActiveSettings.PreviewFrameRate > 0.f) ? (1.0 / FMath::Max(1.0f, ActiveSettings.PreviewFrameRate)) : 0.0;
    LastPreviewUpdateTime = CaptureStartTime;
    State = EOmniCaptureState::Recording;

    UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("Begin capture %s %dx%d (%s, %s, %s) -> %s"),
        ActiveSettings.Mode == EOmniCaptureMode::Stereo ? TEXT("Stereo") : TEXT("Mono"),
        ActiveSettings.Resolution,
        ActiveSettings.Resolution,
        ActiveSettings.OutputFormat == EOmniOutputFormat::PNGSequence ? TEXT("PNG") : TEXT("NVENC"),
        ActiveSettings.Gamma == EOmniCaptureGamma::Linear ? TEXT("Linear") : TEXT("sRGB"),
        ActiveSettings.Codec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H.264"),
        *ActiveSettings.OutputDirectory);
}

void UOmniCaptureSubsystem::EndCapture(bool bFinalize)
{
    if (!bIsCapturing)
    {
        return;
    }

    UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("End capture (Finalize=%d)"), bFinalize ? 1 : 0);

    bIsCapturing = false;
    bIsPaused = false;
    State = EOmniCaptureState::Finalizing;

    DestroyTickActor();
    DestroyPreviewActor();
    DestroyRig();

    ShutdownAudioRecording();

    if (RingBuffer)
    {
        RingBuffer->Flush();
        RingBuffer.Reset();
    }

    ShutdownOutputWriters(bFinalize);
    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }
    FinalizeOutputs(bFinalize);

    State = EOmniCaptureState::Idle;
    LatestRingBufferStats = FOmniCaptureRingBufferStats();
    AudioStats = FOmniAudioSyncStats();
}

void UOmniCaptureSubsystem::PauseCapture()
{
    if (!bIsCapturing || bIsPaused)
    {
        return;
    }

    bIsPaused = true;
    State = EOmniCaptureState::Paused;

    if (RingBuffer)
    {
        RingBuffer->Flush();
    }

    if (AudioRecorder)
    {
        AudioRecorder->SetPaused(true);
    }

    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }
}

void UOmniCaptureSubsystem::ResumeCapture()
{
    if (!bIsCapturing || !bIsPaused)
    {
        return;
    }

    bIsPaused = false;
    State = bDroppedFrames ? EOmniCaptureState::DroppedFrames : EOmniCaptureState::Recording;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;

    if (AudioRecorder)
    {
        AudioRecorder->SetPaused(false);
    }

    if (OutputMuxer)
    {
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
    }
}

bool UOmniCaptureSubsystem::CapturePanoramaStill(const FOmniCaptureSettings& InSettings, FString& OutFilePath)
{
    OutFilePath.Empty();

    if (bIsCapturing)
    {
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("Cannot capture still image while recording is active."));
        return false;
    }

    if (InSettings.Resolution <= 0)
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Invalid resolution supplied for still capture."));
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("No valid world available for still capture."));
        return false;
    }

    LastStillImagePath.Empty();

    FOmniCaptureSettings StillSettings = InSettings;
    StillSettings.OutputFormat = EOmniOutputFormat::PNGSequence;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= RF_Transient;

    AOmniCaptureRigActor* TempRig = World->SpawnActor<AOmniCaptureRigActor>(AOmniCaptureRigActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (!TempRig)
    {
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("Failed to spawn capture rig for still capture."));
        return false;
    }

    TempRig->Configure(StillSettings);

    FOmniEyeCapture LeftEye;
    FOmniEyeCapture RightEye;
    TempRig->Capture(LeftEye, RightEye);

    FlushRenderingCommands();

    FOmniCaptureEquirectResult Result = FOmniCaptureEquirectConverter::ConvertToEquirectangular(StillSettings, LeftEye, RightEye);

    World->DestroyActor(TempRig);

    if (!Result.PixelData.IsValid())
    {
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("Still capture did not generate pixel data. Check cubemap rig configuration."));
        return false;
    }

    FString OutputDirectory = StillSettings.OutputDirectory;
    if (OutputDirectory.IsEmpty())
    {
        OutputDirectory = FPaths::ProjectSavedDir() / TEXT("OmniCaptures");
    }
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);

    const FString BaseName = StillSettings.OutputFileName.IsEmpty() ? TEXT("OmniCaptureStill") : StillSettings.OutputFileName;
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString FileName = FString::Printf(TEXT("%s_%s.png"), *BaseName, *Timestamp);
    OutFilePath = OutputDirectory / FileName;

    FOmniCapturePNGWriter Writer;
    FOmniCaptureSettings WriterSettings = StillSettings;
    WriterSettings.OutputDirectory = OutputDirectory;
    WriterSettings.OutputFileName = BaseName;
    Writer.Initialize(WriterSettings, OutputDirectory);

    TUniquePtr<FOmniCaptureFrame> Frame = MakeUnique<FOmniCaptureFrame>();
    Frame->Metadata.FrameIndex = 0;
    Frame->Metadata.Timecode = 0.0;
    Frame->Metadata.bKeyFrame = true;
    Frame->PixelData = MoveTemp(Result.PixelData);
    Frame->bLinearColor = Result.bIsLinear;
    Frame->bUsedCPUFallback = Result.bUsedCPUFallback;

    Writer.EnqueueFrame(MoveTemp(Frame), FileName);
    Writer.Flush();

    LastStillImagePath = OutFilePath;
    LastFinalizedOutput = OutFilePath;

    UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("Panoramic still saved to %s"), *OutFilePath);

    return true;
}

bool UOmniCaptureSubsystem::CanPause() const
{
    return bIsCapturing && !bIsPaused;
}

bool UOmniCaptureSubsystem::CanResume() const
{
    return bIsCapturing && bIsPaused;
}

FString UOmniCaptureSubsystem::GetStatusString() const
{
    if (!bIsCapturing)
    {
        FString Status;
        if (State == EOmniCaptureState::Finalizing)
        {
            Status = TEXT("Finalizing");
        }
        else
        {
            Status = TEXT("Idle");
        }

        if (!LastStillImagePath.IsEmpty())
        {
            Status += TEXT(" | Last Still: ") + LastStillImagePath;
        }

        if (ActiveWarnings.Num() > 0)
        {
            Status += TEXT(" | Warnings: ");
            Status += FString::Join(ActiveWarnings, TEXT("; "));
        }

        return Status;
    }

    FString Status;
    switch (State)
    {
    case EOmniCaptureState::Recording:
        Status = bDroppedFrames ? TEXT("Recording (Dropped Frames)") : TEXT("Recording");
        break;
    case EOmniCaptureState::Paused:
        Status = TEXT("Paused");
        break;
    case EOmniCaptureState::DroppedFrames:
        Status = TEXT("Recording (Dropped Frames)");
        break;
    case EOmniCaptureState::Finalizing:
        Status = TEXT("Finalizing");
        break;
    default:
        Status = TEXT("Idle");
        break;
    }

    Status += FString::Printf(TEXT(" | Frames:%d Pending:%d Dropped:%d Blocked:%d"), FrameCounter, LatestRingBufferStats.PendingFrames, LatestRingBufferStats.DroppedFrames, LatestRingBufferStats.BlockedPushes);
    Status += FString::Printf(TEXT(" | FPS:%.2f"), CurrentCaptureFPS);
    Status += FString::Printf(TEXT(" | Segment:%d"), CurrentSegmentIndex);

    Status += FString::Printf(TEXT(" | Audio Drift:%.2fms (Max %.2fms) Pending:%d"), AudioStats.DriftMilliseconds, AudioStats.MaxObservedDriftMilliseconds, AudioStats.PendingPackets);
    if (AudioStats.bInError)
    {
        Status += TEXT(" | AudioSyncError");
    }
    if (AudioRecorder)
    {
        Status += TEXT(" | ") + AudioRecorder->GetDebugStatus();
    }

    if (ActiveWarnings.Num() > 0)
    {
        Status += TEXT(" | Warnings: ");
        Status += FString::Join(ActiveWarnings, TEXT("; "));
    }

    return Status;
}

FOmniAudioSyncStats UOmniCaptureSubsystem::GetAudioSyncStats() const
{
    return AudioStats;
}

void UOmniCaptureSubsystem::CreateRig()
{
    DestroyRig();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = OmniCapture::RigActorName;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCaptureRigActor* NewRig = World->SpawnActor<AOmniCaptureRigActor>(SpawnParams))
    {
        NewRig->Configure(ActiveSettings);
        RigActor = NewRig;
    }
}

void UOmniCaptureSubsystem::DestroyRig()
{
    if (AActor* Rig = RigActor.Get())
    {
        Rig->Destroy();
    }
    RigActor.Reset();
}

void UOmniCaptureSubsystem::CreateTickActor()
{
    DestroyTickActor();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = OmniCapture::DirectorActorName;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCaptureDirectorActor* Director = World->SpawnActor<AOmniCaptureDirectorActor>(SpawnParams))
    {
        Director->Initialize(this);
        TickActor = Director;
    }
}

void UOmniCaptureSubsystem::DestroyTickActor()
{
    if (AActor* Director = TickActor.Get())
    {
        Director->Destroy();
    }
    TickActor.Reset();
}

void UOmniCaptureSubsystem::SpawnPreviewActor()
{
    DestroyPreviewActor();

    if (!ActiveSettings.bEnablePreviewWindow)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCapturePreviewActor* Preview = World->SpawnActor<AOmniCapturePreviewActor>(SpawnParameters))
    {
        Preview->Initialize(ActiveSettings.PreviewScreenScale);
        Preview->SetPreviewEnabled(true);
        if (RigActor.IsValid())
        {
            Preview->AttachToActor(RigActor.Get(), FAttachmentTransformRules::KeepWorldTransform);
            Preview->SetActorLocation(RigActor->GetActorLocation() + FVector(ActiveSettings.Resolution * 0.1f, 0.0f, 0.0f));
        }
        PreviewActor = Preview;
    }
}

void UOmniCaptureSubsystem::DestroyPreviewActor()
{
    if (AActor* Preview = PreviewActor.Get())
    {
        Preview->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Preview->Destroy();
    }
    PreviewActor.Reset();
}

void UOmniCaptureSubsystem::InitializeOutputWriters()
{
    RecordedVideoPath.Reset();

    switch (ActiveSettings.OutputFormat)
    {
    case EOmniOutputFormat::PNGSequence:
        PNGWriter = MakeUnique<FOmniCapturePNGWriter>();
        PNGWriter->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        break;
    case EOmniOutputFormat::NVENCHardware:
        NVENCEncoder = MakeUnique<FOmniCaptureNVENCEncoder>();
        NVENCEncoder->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        if (NVENCEncoder->IsInitialized())
        {
            RecordedVideoPath = NVENCEncoder->GetOutputFilePath();
        }
        break;
    default:
        break;
    }
}

void UOmniCaptureSubsystem::ShutdownOutputWriters(bool bFinalizeOutputs)
{
    if (PNGWriter)
    {
        PNGWriter->Flush();
        PNGWriter.Reset();
    }

    if (NVENCEncoder)
    {
        if (bFinalizeOutputs)
        {
            NVENCEncoder->Finalize();
        }
        NVENCEncoder.Reset();
    }
}

void UOmniCaptureSubsystem::FinalizeOutputs(bool bFinalizeOutputs)
{
    if (!bFinalizeOutputs)
    {
        CapturedFrameMetadata.Empty();
        CompletedSegments.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        LastFinalizedOutput.Empty();
        LastStillImagePath.Empty();
        OutputMuxer.Reset();
        return;
    }

    if (CapturedFrameMetadata.Num() > 0)
    {
        CompleteActiveSegment(true);
    }

    if (CompletedSegments.Num() == 0)
    {
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("FinalizeOutputs called with no captured frames"));
        OutputMuxer.Reset();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        LastFinalizedOutput.Empty();
        LastStillImagePath.Empty();
        return;
    }

    if (!OutputMuxer)
    {
        OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    }

    LastFinalizedOutput.Empty();

    for (const FOmniCaptureSegmentRecord& Segment : CompletedSegments)
    {
        if (!OutputMuxer)
        {
            break;
        }

        FOmniCaptureSettings SegmentSettings = ActiveSettings;
        SegmentSettings.OutputDirectory = Segment.Directory;
        SegmentSettings.OutputFileName = Segment.BaseFileName;

        OutputMuxer->Initialize(SegmentSettings, Segment.Directory);
        OutputMuxer->BeginRealtimeSession(SegmentSettings);

        const bool bSuccess = OutputMuxer->FinalizeCapture(SegmentSettings, Segment.Frames, Segment.AudioPath, Segment.VideoPath);
        if (!bSuccess)
        {
            UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("Output muxing failed for segment %d. Check OmniCapture manifest for details."), Segment.SegmentIndex);
        }
        OutputMuxer->EndRealtimeSession();

        const FString FinalVideoPath = Segment.Directory / (Segment.BaseFileName + TEXT(".mp4"));
        LastFinalizedOutput = FinalVideoPath;

        if (SegmentSettings.bOpenPreviewOnFinalize && !FinalVideoPath.IsEmpty())
        {
            FPlatformProcess::LaunchFileInDefaultExternalApplication(*FinalVideoPath);
        }
    }

    CompletedSegments.Empty();
    CapturedFrameMetadata.Reset();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    OutputMuxer.Reset();
}

bool UOmniCaptureSubsystem::ValidateEnvironment()
{
    bool bResult = true;

    const FString GpuBrand = FPlatformMisc::GetPrimaryGPUBrand();
    if (!GpuBrand.IsEmpty())
    {
        ActiveWarnings.Add(FString::Printf(TEXT("GPU: %s"), *GpuBrand));
    }

#if PLATFORM_WINDOWS
    if (GDynamicRHI)
    {
        const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();
        if (InterfaceType != ERHIInterfaceType::D3D11 && InterfaceType != ERHIInterfaceType::D3D12)
        {
            ActiveWarnings.Add(TEXT("OmniCapture requires D3D11 or D3D12 for GPU capture. Current RHI is unsupported."));
            bResult = false;
        }
    }
    else
    {
        ActiveWarnings.Add(TEXT("Unable to resolve active RHI interface. Zero-copy NVENC will be disabled."));
        bResult = false;
    }
#else
    ActiveWarnings.Add(TEXT("OmniCapture NVENC pipeline is Windows-only; PNG sequence mode is recommended."));
    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        bResult = false;
    }
#endif

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        const FOmniNVENCCapabilities Caps = FOmniCaptureNVENCEncoder::QueryCapabilities();
        if (!Caps.AdapterName.IsEmpty())
        {
            ActiveWarnings.Add(FString::Printf(TEXT("Adapter: %s"), *Caps.AdapterName));
        }
        if (!Caps.DriverVersion.IsEmpty())
        {
            ActiveWarnings.Add(FString::Printf(TEXT("Driver: %s"), *Caps.DriverVersion));
        }

        if (!Caps.bHardwareAvailable)
        {
            ActiveWarnings.Add(TEXT("NVENC hardware encoder unavailable"));
            bResult = false;
        }
        if (ActiveSettings.Codec == EOmniCaptureCodec::HEVC && !Caps.bSupportsHEVC)
        {
            ActiveWarnings.Add(TEXT("HEVC codec unsupported by detected NVENC hardware"));
            bResult = false;
        }
        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010 && !Caps.bSupports10Bit)
        {
            ActiveWarnings.Add(TEXT("P010 / Main10 NVENC path unavailable on this GPU"));
            bResult = false;
        }
        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12 && !Caps.bSupportsNV12)
        {
            ActiveWarnings.Add(TEXT("NV12 NVENC path unavailable on this GPU"));
            bResult = false;
        }

        const EPixelFormat PixelFormat =
            (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12) ? PF_NV12 :
            (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010) ? PF_P010 :
            PF_B8G8R8A8;

        if (!GPixelFormats[PixelFormat].Supported)
        {
            ActiveWarnings.Add(TEXT("Requested NVENC pixel format is not supported by the active RHI"));
            bResult = false;
        }

        if (ActiveSettings.bZeroCopy)
        {
#if PLATFORM_WINDOWS
            if (!GDynamicRHI || (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D11 && GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D12))
            {
                ActiveWarnings.Add(TEXT("Zero-copy NVENC requires D3D11 or D3D12; zero-copy will be disabled."));
                bResult = false;
            }
#else
            ActiveWarnings.Add(TEXT("Zero-copy NVENC is only available on Windows/D3D; zero-copy will be disabled."));
            bResult = false;
#endif
        }
    }

    FString ResolvedFFmpeg;
    if (!FOmniCaptureMuxer::IsFFmpegAvailable(ActiveSettings, &ResolvedFFmpeg))
    {
        ActiveWarnings.Add(TEXT("FFmpeg not detected - automatic muxing disabled"));
    }
    else if (!ResolvedFFmpeg.IsEmpty() && !ResolvedFFmpeg.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase))
    {
        ActiveWarnings.Add(FString::Printf(TEXT("FFmpeg: %s"), *ResolvedFFmpeg));
    }

    uint64 FreeBytes = 0;
    uint64 TotalBytes = 0;
    if (IFileManager::Get().GetDiskFreeSpace(*ActiveSettings.OutputDirectory, FreeBytes, TotalBytes))
    {
        const uint64 MinFreeBytes = static_cast<uint64>(FMath::Max(0, ActiveSettings.MinimumFreeDiskSpaceGB)) * 1024ull * 1024ull * 1024ull;
        if (MinFreeBytes > 0 && FreeBytes < MinFreeBytes)
        {
            AddWarningUnique(OmniCapture::WarningLowDisk);
        }
    }
    else
    {
        AddWarningUnique(TEXT("Unable to query disk space for capture output"));
    }

    return bResult;
}

bool UOmniCaptureSubsystem::ApplyFallbacks()
{
    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware && !FOmniCaptureNVENCEncoder::IsNVENCAvailable())
    {
        if (ActiveSettings.bAllowNVENCFallback)
        {
            ActiveWarnings.Add(TEXT("Falling back to PNG sequence because NVENC is unavailable"));
            ActiveSettings.OutputFormat = EOmniOutputFormat::PNGSequence;
            return true;
        }

        ActiveWarnings.Add(TEXT("NVENC required but unavailable"));
        return false;
    }

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
#if !PLATFORM_WINDOWS
        ActiveWarnings.Add(TEXT("NVENC output is not supported on this platform; switching to PNG sequence."));
        ActiveSettings.OutputFormat = EOmniOutputFormat::PNGSequence;
        return true;
#endif

        const FOmniNVENCCapabilities Caps = FOmniCaptureNVENCEncoder::QueryCapabilities();

        if (ActiveSettings.Codec == EOmniCaptureCodec::HEVC && !Caps.bSupportsHEVC)
        {
            ActiveWarnings.Add(TEXT("HEVC unsupported - falling back to H.264"));
            ActiveSettings.Codec = EOmniCaptureCodec::H264;
        }

        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010 && !Caps.bSupports10Bit)
        {
            ActiveWarnings.Add(TEXT("P010 unsupported - switching to NV12"));
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::NV12;
        }

        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12 && !Caps.bSupportsNV12)
        {
            ActiveWarnings.Add(TEXT("NV12 unsupported - switching to BGRA"));
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::BGRA;
        }

        if (!FOmniCaptureNVENCEncoder::SupportsColorFormat(ActiveSettings.NVENCColorFormat))
        {
            ActiveWarnings.Add(TEXT("Requested NVENC color format unavailable - switching to BGRA"));
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::BGRA;
        }

        if (ActiveSettings.bZeroCopy)
        {
#if PLATFORM_WINDOWS
            if (!GDynamicRHI || (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D11 && GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D12))
            {
                ActiveWarnings.Add(TEXT("Zero-copy not supported on this RHI - disabling zero-copy"));
                ActiveSettings.bZeroCopy = false;
            }
#else
            ActiveWarnings.Add(TEXT("Zero-copy NVENC disabled on this platform"));
            ActiveSettings.bZeroCopy = false;
#endif
        }
    }

    return true;
}

void UOmniCaptureSubsystem::InitializeAudioRecording()
{
    if (!ActiveSettings.bRecordAudio)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    AudioRecorder = MakeUnique<FOmniCaptureAudioRecorder>();
    if (AudioRecorder->Initialize(World, ActiveSettings))
    {
        AudioRecorder->Start();
    }
    else
    {
        AudioRecorder.Reset();
    }
}

void UOmniCaptureSubsystem::ShutdownAudioRecording()
{
    if (!AudioRecorder)
    {
        return;
    }

    AudioRecorder->Stop(ActiveSettings.OutputDirectory, ActiveSettings.OutputFileName);
    RecordedAudioPath = AudioRecorder->GetOutputFilePath();
    if (!RecordedAudioPath.IsEmpty())
    {
        UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("Audio recording saved to %s"), *RecordedAudioPath);
    }
    AudioRecorder.Reset();
}

void UOmniCaptureSubsystem::TickCapture(float DeltaTime)
{
    if (!bIsCapturing)
    {
        return;
    }

    if (!bIsPaused)
    {
        RotateSegmentIfNeeded();
        CaptureFrame();
    }

    UpdateRuntimeWarnings();
}

void UOmniCaptureSubsystem::CaptureFrame()
{
    if (!RigActor.IsValid() || !RingBuffer)
    {
        HandleDroppedFrame();
        return;
    }

    FOmniEyeCapture LeftEye;
    FOmniEyeCapture RightEye;
    RigActor->Capture(LeftEye, RightEye);

    FlushRenderingCommands();

    FOmniCaptureEquirectResult ConversionResult = FOmniCaptureEquirectConverter::ConvertToEquirectangular(ActiveSettings, LeftEye, RightEye);
    const bool bRequiresGPU = ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware;
    if (!ConversionResult.PixelData.IsValid())
    {
        HandleDroppedFrame();
        return;
    }

    if (bRequiresGPU && !ConversionResult.Texture.IsValid())
    {
        HandleDroppedFrame();
        return;
    }

    TUniquePtr<FOmniCaptureFrame> Frame = MakeUnique<FOmniCaptureFrame>();
    Frame->Metadata.FrameIndex = FrameCounter++;
    Frame->Metadata.Timecode = FPlatformTime::Seconds() - CaptureStartTime;
    Frame->Metadata.bKeyFrame = (Frame->Metadata.FrameIndex % ActiveSettings.Quality.GOPLength) == 0;

    ++FramesSinceLastFpsSample;
    const double NowSeconds = FPlatformTime::Seconds();
    if (LastFpsSampleTime <= 0.0)
    {
        LastFpsSampleTime = NowSeconds;
    }
    const double SampleElapsed = NowSeconds - LastFpsSampleTime;
    if (SampleElapsed >= 1.0)
    {
        const double SafeElapsed = FMath::Max(SampleElapsed, KINDA_SMALL_NUMBER);
        CurrentCaptureFPS = static_cast<double>(FramesSinceLastFpsSample) / SafeElapsed;
        FramesSinceLastFpsSample = 0;
        LastFpsSampleTime = NowSeconds;
    }

    Frame->PixelData = MoveTemp(ConversionResult.PixelData);
    Frame->GPUSource = ConversionResult.OutputTarget;
    Frame->Texture = ConversionResult.Texture;
    Frame->ReadyFence = ConversionResult.ReadyFence;
    Frame->bLinearColor = ConversionResult.bIsLinear;
    Frame->bUsedCPUFallback = ConversionResult.bUsedCPUFallback;
    Frame->EncoderTextures.Reset();
    for (const TRefCountPtr<IPooledRenderTarget>& Plane : ConversionResult.EncoderPlanes)
    {
        if (!Plane.IsValid())
        {
            continue;
        }

        if (FRHITexture* PlaneTexture = Plane->GetRenderTargetItem().ShaderResourceTexture)
        {
            if (FTexture2DRHIRef TextureRef = PlaneTexture->GetTexture2D())
            {
                Frame->EncoderTextures.Add(TextureRef);
            }
        }
    }
    if (Frame->EncoderTextures.Num() == 0 && Frame->Texture.IsValid())
    {
        Frame->EncoderTextures.Add(Frame->Texture);
    }

    if (AudioRecorder)
    {
        AudioRecorder->GatherAudio(Frame->Metadata.Timecode, Frame->AudioPackets);
    }

    CapturedFrameMetadata.Add(Frame->Metadata);

    RingBuffer->Enqueue(MoveTemp(Frame));

    if (RingBuffer)
    {
        LatestRingBufferStats = RingBuffer->GetStats();
    }

    if (PreviewActor.IsValid())
    {
        const double Now = FPlatformTime::Seconds();
        if (PreviewFrameInterval <= 0.0 || (Now - LastPreviewUpdateTime) >= PreviewFrameInterval)
        {
            PreviewActor->UpdatePreviewTexture(ConversionResult);
            LastPreviewUpdateTime = Now;
        }
    }
}

void UOmniCaptureSubsystem::FlushRingBuffer()
{
    if (RingBuffer)
    {
        RingBuffer->Flush();
    }
}

void UOmniCaptureSubsystem::HandleDroppedFrame()
{
    bDroppedFrames = true;
    State = EOmniCaptureState::DroppedFrames;
    ++DroppedFrameCount;
    AddWarningUnique(OmniCapture::WarningFrameDrop);
    UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("OmniCapture frame dropped"));
}

void UOmniCaptureSubsystem::ConfigureActiveSegment()
{
    const FString SegmentSuffix = (CurrentSegmentIndex == 0)
        ? FString()
        : FString::Printf(TEXT("_seg%02d"), CurrentSegmentIndex);

    FString SegmentDirectory = BaseOutputDirectory;
    if (ActiveSettings.bCreateSegmentSubfolders)
    {
        SegmentDirectory = BaseOutputDirectory / FString::Printf(TEXT("Segment_%02d"), CurrentSegmentIndex);
    }

    ActiveSettings.OutputDirectory = SegmentDirectory;
    ActiveSettings.OutputFileName = BaseOutputFileName + SegmentSuffix;

    IFileManager::Get().MakeDirectory(*ActiveSettings.OutputDirectory, true);

    CapturedFrameMetadata.Empty();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();

    CurrentSegmentStartTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
}

void UOmniCaptureSubsystem::RotateSegmentIfNeeded()
{
    if (!bIsCapturing)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    bool bShouldRotate = false;

    if (ActiveSettings.SegmentDurationSeconds > 0.0f)
    {
        const double SegmentElapsed = Now - CurrentSegmentStartTime;
        if (SegmentElapsed >= ActiveSettings.SegmentDurationSeconds)
        {
            bShouldRotate = true;
        }
    }

    if (!bShouldRotate && ActiveSettings.SegmentSizeLimitMB > 0)
    {
        if ((Now - LastSegmentSizeCheckTime) >= 1.0)
        {
            LastSegmentSizeCheckTime = Now;
            const int64 SegmentBytes = CalculateActiveSegmentSizeBytes();
            const int64 LimitBytes = static_cast<int64>(ActiveSettings.SegmentSizeLimitMB) * 1024 * 1024;
            if (LimitBytes > 0 && SegmentBytes >= LimitBytes)
            {
                bShouldRotate = true;
            }
        }
    }

    if (!bShouldRotate || CapturedFrameMetadata.Num() == 0)
    {
        return;
    }

    UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("Rotating capture segment -> %d"), CurrentSegmentIndex + 1);

    if (RingBuffer)
    {
        RingBuffer->Flush();
    }

    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }

    ShutdownAudioRecording();
    ShutdownOutputWriters(true);
    CompleteActiveSegment(true);

    ++CurrentSegmentIndex;
    ConfigureActiveSegment();

    InitializeOutputWriters();

    if (!OutputMuxer)
    {
        OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    }

    if (OutputMuxer)
    {
        OutputMuxer->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
        AudioStats = FOmniAudioSyncStats();
    }

    InitializeAudioRecording();

    CurrentSegmentStartTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;
}

void UOmniCaptureSubsystem::CompleteActiveSegment(bool bStoreResults)
{
    if (!bStoreResults)
    {
        CapturedFrameMetadata.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        return;
    }

    if (CapturedFrameMetadata.Num() == 0)
    {
        CapturedFrameMetadata.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        return;
    }

    FOmniCaptureSegmentRecord SegmentRecord;
    SegmentRecord.SegmentIndex = CurrentSegmentIndex;
    SegmentRecord.Directory = ActiveSettings.OutputDirectory;
    SegmentRecord.BaseFileName = ActiveSettings.OutputFileName;
    SegmentRecord.AudioPath = RecordedAudioPath;
    SegmentRecord.VideoPath = RecordedVideoPath;
    SegmentRecord.Frames = MoveTemp(CapturedFrameMetadata);

    CompletedSegments.Add(MoveTemp(SegmentRecord));

    CapturedFrameMetadata.Reset();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
}

int64 UOmniCaptureSubsystem::CalculateActiveSegmentSizeBytes() const
{
    int64 TotalBytes = 0;
    IFileManager& FileManager = IFileManager::Get();

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        if (!RecordedVideoPath.IsEmpty())
        {
            const int64 BitstreamSize = FileManager.FileSize(*RecordedVideoPath);
            if (BitstreamSize > 0)
            {
                TotalBytes += BitstreamSize;
            }
        }
    }
    else
    {
        class FSegmentStatVisitor final : public IPlatformFile::FDirectoryStatVisitor
        {
        public:
            FSegmentStatVisitor(const FString& InPrefix, int64& InTotal)
                : Prefix(InPrefix)
                , Total(InTotal)
            {
            }

            virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
            {
                if (StatData.bIsDirectory)
                {
                    return true;
                }

                const FString FileName = FPaths::GetCleanFilename(FilenameOrDirectory);
                if (Prefix.IsEmpty() || FileName.StartsWith(Prefix))
                {
                    Total += StatData.FileSize;
                }
                return true;
            }

        private:
            FString Prefix;
            int64& Total;
        };

        FSegmentStatVisitor Visitor(ActiveSettings.OutputFileName, TotalBytes);
        FileManager.IterateDirectoryStat(*ActiveSettings.OutputDirectory, Visitor);
    }

    if (!RecordedAudioPath.IsEmpty())
    {
        const int64 AudioSize = FileManager.FileSize(*RecordedAudioPath);
        if (AudioSize > 0)
        {
            TotalBytes += AudioSize;
        }
    }

    return TotalBytes;
}

void UOmniCaptureSubsystem::UpdateRuntimeWarnings()
{
    const double Now = FPlatformTime::Seconds();
    if ((Now - LastRuntimeWarningCheckTime) < 1.0)
    {
        return;
    }

    LastRuntimeWarningCheckTime = Now;

    if (ActiveSettings.MinimumFreeDiskSpaceGB > 0)
    {
        uint64 FreeBytes = 0;
        uint64 TotalBytes = 0;
        const uint64 ThresholdBytes = static_cast<uint64>(ActiveSettings.MinimumFreeDiskSpaceGB) * 1024ull * 1024ull * 1024ull;
        if (ThresholdBytes > 0 && IFileManager::Get().GetDiskFreeSpace(*ActiveSettings.OutputDirectory, FreeBytes, TotalBytes))
        {
            if (FreeBytes < ThresholdBytes)
            {
                AddWarningUnique(OmniCapture::WarningLowDisk);
            }
            else
            {
                RemoveWarning(OmniCapture::WarningLowDisk);
            }
        }
    }

    if (ActiveSettings.TargetFrameRate > 0.0f)
    {
        const double ThresholdFps = ActiveSettings.TargetFrameRate * FMath::Clamp(static_cast<double>(ActiveSettings.LowFrameRateWarningRatio), 0.1, 1.0);
        if (!bIsPaused && CurrentCaptureFPS > 0.0 && CurrentCaptureFPS < ThresholdFps)
        {
            AddWarningUnique(OmniCapture::WarningLowFps);
        }
        else
        {
            RemoveWarning(OmniCapture::WarningLowFps);
            if (!bDroppedFrames)
            {
                RemoveWarning(OmniCapture::WarningFrameDrop);
            }
        }
    }
}

void UOmniCaptureSubsystem::AddWarningUnique(const FString& Warning)
{
    if (!Warning.IsEmpty())
    {
        ActiveWarnings.AddUnique(Warning);
    }
}

void UOmniCaptureSubsystem::RemoveWarning(const FString& Warning)
{
    if (!Warning.IsEmpty())
    {
        ActiveWarnings.Remove(Warning);
    }
}

void UOmniCaptureSubsystem::ResetDynamicWarnings()
{
    RemoveWarning(OmniCapture::WarningLowDisk);
    RemoveWarning(OmniCapture::WarningFrameDrop);
    RemoveWarning(OmniCapture::WarningLowFps);
}

FString UOmniCaptureSubsystem::BuildOutputDirectory() const
{
    if (!ActiveSettings.OutputDirectory.IsEmpty())
    {
        return FPaths::ConvertRelativePathToFull(ActiveSettings.OutputDirectory);
    }

    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OmniCaptures"));
}

FString UOmniCaptureSubsystem::BuildFrameFileName(int32 FrameIndex, const FString& Extension) const
{
    return FString::Printf(TEXT("%s_%06d%s"), *ActiveSettings.OutputFileName, FrameIndex, *Extension);
}

