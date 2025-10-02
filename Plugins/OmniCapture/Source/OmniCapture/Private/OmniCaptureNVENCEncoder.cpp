#include "OmniCaptureNVENCEncoder.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureTypes.h"
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathUtility.h"
#include "PixelFormat.h"
#include "RHI.h"

#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
#include "VideoEncoderFactory.h"
#include "VideoEncoderInput.h"
#include "VideoEncoder.h"
#include "VideoEncoderCommon.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#endif

namespace
{
#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    AVEncoder::EVideoFormat ToVideoFormat(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::NV12:
            return AVEncoder::EVideoFormat::NV12;
        case EOmniCaptureColorFormat::P010:
            return AVEncoder::EVideoFormat::P010;
        case EOmniCaptureColorFormat::BGRA:
        default:
            return AVEncoder::EVideoFormat::BGRA8;
        }
    }
#endif
}

FOmniCaptureNVENCEncoder::FOmniCaptureNVENCEncoder()
{
}

bool FOmniCaptureNVENCEncoder::IsNVENCAvailable()
{
#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

FOmniNVENCCapabilities FOmniCaptureNVENCEncoder::QueryCapabilities()
{
    FOmniNVENCCapabilities Caps;

#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    Caps.bHardwareAvailable = IsNVENCAvailable();
    Caps.bSupportsNV12 = SupportsColorFormat(EOmniCaptureColorFormat::NV12);
    Caps.bSupportsP010 = SupportsColorFormat(EOmniCaptureColorFormat::P010);
    Caps.bSupportsHEVC = Caps.bHardwareAvailable;
    Caps.bSupports10Bit = Caps.bSupportsP010;
#else
    Caps.bHardwareAvailable = false;
#endif

    Caps.AdapterName = FPlatformMisc::GetPrimaryGPUBrand();
#if PLATFORM_WINDOWS
    const FGPUDriverInfo DriverInfo = FPlatformMisc::GetGPUDriverInfo(FGPUDriverInfo::EDeviceType::Primary);
    Caps.DriverVersion = DriverInfo.DriverVersion;
#endif

    return Caps;
}

bool FOmniCaptureNVENCEncoder::SupportsColorFormat(EOmniCaptureColorFormat Format)
{
#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    switch (Format)
    {
    case EOmniCaptureColorFormat::NV12:
        return GPixelFormats[PF_NV12].Supported != 0;
    case EOmniCaptureColorFormat::P010:
#if defined(PF_P010)
        return GPixelFormats[PF_P010].Supported != 0;
#else
        return false;
#endif
    case EOmniCaptureColorFormat::BGRA:
        return GPixelFormats[PF_B8G8R8A8].Supported != 0;
    default:
        return false;
    }
#else
    return Format == EOmniCaptureColorFormat::BGRA;
#endif
}

FOmniCaptureNVENCEncoder::~FOmniCaptureNVENCEncoder()
{
    Finalize();
}

void FOmniCaptureNVENCEncoder::Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory)
{
    FString Directory = OutputDirectory.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("OmniCaptures")) : OutputDirectory;
    Directory = FPaths::ConvertRelativePathToFull(Directory);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*Directory);

    RequestedCodec = Settings.Codec;
    const bool bUseHEVC = RequestedCodec == EOmniCaptureCodec::HEVC;
    OutputFilePath = Directory / (Settings.OutputFileName + (bUseHEVC ? TEXT(".h265") : TEXT(".h264")));
    ColorFormat = Settings.NVENCColorFormat;
    bZeroCopyRequested = Settings.bZeroCopy;

#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    const int32 OutputWidth = Settings.Resolution * 2;
    const int32 OutputHeight = Settings.Mode == EOmniCaptureMode::Stereo ? Settings.Resolution * 2 : Settings.Resolution;

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("AVEncoder")))
    {
        FModuleManager::Get().LoadModule(TEXT("AVEncoder"));
    }

    AVEncoder::FVideoEncoderInput::FCreateParameters CreateParameters;
    CreateParameters.Width = OutputWidth;
    CreateParameters.Height = OutputHeight;
    CreateParameters.Format = ToVideoFormat(ColorFormat);
    CreateParameters.MaxBufferDimensions = FIntPoint(OutputWidth, OutputHeight);
    CreateParameters.DebugName = TEXT("OmniCaptureNVENC");
    CreateParameters.bAutoCopy = !bZeroCopyRequested;

    EncoderInput = AVEncoder::FVideoEncoderInput::CreateForRHI(CreateParameters);
    if (!EncoderInput.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create NVENC encoder input."));
        return;
    }

    LayerConfig = AVEncoder::FVideoEncoder::FLayerConfig();
    LayerConfig.Width = OutputWidth;
    LayerConfig.Height = OutputHeight;
    LayerConfig.MaxFramerate = 120;
    LayerConfig.TargetBitrate = Settings.Quality.TargetBitrateKbps * 1000;
    LayerConfig.MaxBitrate = FMath::Max<int32>(LayerConfig.TargetBitrate, Settings.Quality.MaxBitrateKbps * 1000);
    LayerConfig.MinQp = 0;
    LayerConfig.MaxQp = 51;

    CodecConfig = AVEncoder::FVideoEncoder::FCodecConfig();
    CodecConfig.bLowLatency = Settings.Quality.bLowLatency;
    CodecConfig.GOPLength = Settings.Quality.GOPLength;
    CodecConfig.MaxNumBFrames = Settings.Quality.BFrames;
    CodecConfig.bEnableFrameReordering = Settings.Quality.BFrames > 0;

    AVEncoder::FVideoEncoder::FInit EncoderInit;
    EncoderInit.Codec = bUseHEVC ? AVEncoder::ECodec::HEVC : AVEncoder::ECodec::H264;
    EncoderInit.CodecConfig = CodecConfig;
    EncoderInit.Layers.Add(LayerConfig);

    auto OnEncodedPacket = AVEncoder::FVideoEncoder::FOnEncodedPacket::CreateLambda([this](const AVEncoder::FVideoEncoder::FEncodedPacket& Packet)
    {
        FScopeLock Lock(&EncoderCS);
        if (!BitstreamFile)
        {
            return;
        }

        AnnexBBuffer.Reset();
        Packet.ToAnnexB(AnnexBBuffer);
        if (AnnexBBuffer.Num() > 0)
        {
            BitstreamFile->Write(AnnexBBuffer.GetData(), AnnexBBuffer.Num());
        }
    });

    VideoEncoder = AVEncoder::FVideoEncoderFactory::Create(*EncoderInput, EncoderInit, MoveTemp(OnEncodedPacket));
    if (!VideoEncoder.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create NVENC video encoder."));
        EncoderInput.Reset();
        return;
    }

    BitstreamFile.Reset(PlatformFile.OpenWrite(*OutputFilePath, /*bAppend=*/false));
    if (!BitstreamFile)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unable to open NVENC bitstream output file."));
    }

    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("NVENC encoder ready (%dx%d, %s, ZeroCopy=%s)."), OutputWidth, OutputHeight, bUseHEVC ? TEXT("HEVC") : TEXT("H.264"), bZeroCopyRequested ? TEXT("Yes") : TEXT("No"));
#else
    UE_LOG(LogTemp, Warning, TEXT("NVENC is only available on Windows with AVEncoder support."));
#endif
}

void FOmniCaptureNVENCEncoder::EnqueueFrame(const FOmniCaptureFrame& Frame)
{
#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    if (!bInitialized || !VideoEncoder.IsValid() || !EncoderInput.IsValid())
    {
        return;
    }

    if (Frame.ReadyFence.IsValid())
    {
        RHIWaitGPUFence(Frame.ReadyFence);
    }

    if (Frame.bUsedCPUFallback)
    {
        UE_LOG(LogTemp, Warning, TEXT("Skipping NVENC submission because frame used CPU equirect fallback."));
        return;
    }

    if (!Frame.Texture.IsValid())
    {
        return;
    }

    TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame;
    if (Frame.EncoderTextures.Num() > 0)
    {
        InputFrame = EncoderInput->CreateEncoderInputFrame();
        if (InputFrame.IsValid())
        {
            for (int32 PlaneIndex = 0; PlaneIndex < Frame.EncoderTextures.Num(); ++PlaneIndex)
            {
                if (Frame.EncoderTextures[PlaneIndex].IsValid())
                {
                    InputFrame->SetTexture(PlaneIndex, Frame.EncoderTextures[PlaneIndex]);
                }
            }
        }
    }

    if (!InputFrame.IsValid())
    {
        InputFrame = EncoderInput->CreateEncoderInputFrameFromRHITexture(Frame.Texture);
    }

    if (!InputFrame.IsValid())
    {
        return;
    }

    InputFrame->SetTimestampUs(static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0));
    InputFrame->SetFrameIndex(Frame.Metadata.FrameIndex);
    InputFrame->SetKeyFrame(Frame.Metadata.bKeyFrame);

    VideoEncoder->Encode(InputFrame);
#else
    (void)Frame;
#endif
}

void FOmniCaptureNVENCEncoder::Finalize()
{
#if WITH_OMNI_NVENC && PLATFORM_WINDOWS
    if (!bInitialized)
    {
        return;
    }

    VideoEncoder.Reset();
    EncoderInput.Reset();

    if (BitstreamFile)
    {
        BitstreamFile->Flush();
        BitstreamFile.Reset();
    }

    bInitialized = false;
    UE_LOG(LogTemp, Log, TEXT("NVENC finalize complete -> %s"), *OutputFilePath);
#endif
}
