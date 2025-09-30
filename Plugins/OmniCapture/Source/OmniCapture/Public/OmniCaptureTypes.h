#pragma once

#include "CoreMinimal.h"
#include "ImageWriteTypes.h"
#include "RHIResources.h"
#include "OmniCaptureTypes.generated.h"

UENUM(BlueprintType)
enum class EOmniCaptureMode : uint8
{
    Mono,
    Stereo
};

UENUM(BlueprintType)
enum class EOmniCaptureStereoLayout : uint8
{
    TopBottom,
    SideBySide
};

UENUM(BlueprintType)
enum class EOmniOutputFormat : uint8
{
    PNGSequence,
    NVENCHardware
};

UENUM(BlueprintType)
enum class EOmniCaptureGamma : uint8
{
    SRGB,
    Linear
};

UENUM(BlueprintType)
enum class EOmniCaptureColorSpace : uint8
{
    BT709,
    BT2020,
    HDR10
};

UENUM(BlueprintType)
enum class EOmniCaptureCodec : uint8
{
    H264,
    HEVC
};

UENUM(BlueprintType)
enum class EOmniCaptureColorFormat : uint8
{
    NV12,
    P010,
    BGRA
};

UENUM(BlueprintType)
enum class EOmniCaptureRateControlMode : uint8
{
    ConstantBitrate,
    VariableBitrate,
    Lossless
};

UENUM(BlueprintType)
enum class EOmniCaptureState : uint8
{
    Idle,
    Recording,
    Paused,
    DroppedFrames,
    Finalizing
};

UENUM(BlueprintType)
enum class EOmniCaptureRingBufferPolicy : uint8
{
    DropOldest,
    BlockProducer
};

USTRUCT(BlueprintType)
struct FOmniCaptureQuality
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    int32 TargetBitrateKbps = 60000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    int32 MaxBitrateKbps = 80000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    int32 GOPLength = 60;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    int32 BFrames = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    bool bLowLatency = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
    EOmniCaptureRateControlMode RateControlMode = EOmniCaptureRateControlMode::ConstantBitrate;
};

USTRUCT(BlueprintType)
struct FOmniCaptureSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EOmniCaptureMode Mode = EOmniCaptureMode::Mono;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EOmniCaptureStereoLayout StereoLayout = EOmniCaptureStereoLayout::TopBottom;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 1024, UIMin = 1024))
    int32 Resolution = 4096;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, UIMin = 0.0))
    float TargetFrameRate = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EOmniCaptureGamma Gamma = EOmniCaptureGamma::SRGB;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bEnablePreviewWindow = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.1, UIMin = 0.1))
    float PreviewScreenScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 1.0, UIMin = 5.0, ClampMax = 240.0))
    float PreviewFrameRate = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bRecordAudio = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    float AudioGain = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    TSoftObjectPtr<class USoundSubmix> SubmixToRecord;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    float InterPupillaryDistanceCm = 6.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, UIMin = 0.0))
    float SegmentDurationSeconds = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0))
    int32 SegmentSizeLimitMB = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bCreateSegmentSubfolders = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    EOmniOutputFormat OutputFormat = EOmniOutputFormat::PNGSequence;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    FString OutputDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    FString OutputFileName = TEXT("OmniCapture");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    EOmniCaptureColorSpace ColorSpace = EOmniCaptureColorSpace::BT709;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    bool bEnableFastStart = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    bool bForceConstantFrameRate = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    bool bAllowNVENCFallback = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = 0))
    int32 MinimumFreeDiskSpaceGB = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = 0.1, ClampMax = 1.0))
    float LowFrameRateWarningRatio = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    FString PreferredFFmpegPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, ClampMax = 1.0))
    float SeamBlend = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, ClampMax = 1.0))
    float PolarDampening = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    FOmniCaptureQuality Quality;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    EOmniCaptureCodec Codec = EOmniCaptureCodec::HEVC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    EOmniCaptureColorFormat NVENCColorFormat = EOmniCaptureColorFormat::NV12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    bool bZeroCopy = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = 0, UIMin = 0))
    int32 RingBufferCapacity = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC")
    EOmniCaptureRingBufferPolicy RingBufferPolicy = EOmniCaptureRingBufferPolicy::DropOldest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
    bool bOpenPreviewOnFinalize = false;
};

USTRUCT()
struct FOmniCaptureFrameMetadata
{
    GENERATED_BODY()

    UPROPERTY()
    int32 FrameIndex = 0;

    UPROPERTY()
    double Timecode = 0.0;

    UPROPERTY()
    bool bKeyFrame = false;
};

struct FOmniCaptureFrame
{
    FOmniCaptureFrameMetadata Metadata;
    TUniquePtr<FImagePixelData> PixelData;
    TRefCountPtr<IPooledRenderTarget> GPUSource;
    FTexture2DRHIRef Texture;
    FGPUFenceRHIRef ReadyFence;
    bool bLinearColor = false;
    bool bUsedCPUFallback = false;
    TArray<struct FOmniAudioPacket> AudioPackets;
    TArray<FTexture2DRHIRef> EncoderTextures;
};

USTRUCT()
struct FOmniAudioPacket
{
    GENERATED_BODY()

    UPROPERTY()
    double Timestamp = 0.0;

    UPROPERTY()
    int32 SampleRate = 48000;

    UPROPERTY()
    int32 NumChannels = 2;

    UPROPERTY()
    TArray<int16> PCM16;
};

USTRUCT(BlueprintType)
struct FOmniCaptureRingBufferStats
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 PendingFrames = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 DroppedFrames = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    int32 BlockedPushes = 0;
};

USTRUCT(BlueprintType)
struct FOmniAudioSyncStats
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    double LatestVideoTimestamp = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    double LatestAudioTimestamp = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    double DriftMilliseconds = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    double MaxObservedDriftMilliseconds = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    int32 PendingPackets = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
    bool bInError = false;
};

