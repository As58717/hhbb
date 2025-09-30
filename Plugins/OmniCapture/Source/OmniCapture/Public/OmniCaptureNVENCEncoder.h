#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

#if WITH_OMNI_NVENC
#include "AVEncoder.h"
#endif

#if WITH_OMNI_NVENC
namespace AVEncoder
{
    struct FVideoEncoderCapability;
}
#endif

struct FOmniNVENCCapabilities
{
    bool bHardwareAvailable = false;
    bool bSupportsNV12 = false;
    bool bSupportsP010 = false;
    bool bSupportsHEVC = false;
    bool bSupports10Bit = false;
    FString AdapterName;
    FString DriverVersion;
};

class OMNICAPTURE_API FOmniCaptureNVENCEncoder
{
public:
    FOmniCaptureNVENCEncoder();
    ~FOmniCaptureNVENCEncoder();

    void Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory);
    void EnqueueFrame(const FOmniCaptureFrame& Frame);
    void Finalize();
    static bool IsNVENCAvailable();
    static FOmniNVENCCapabilities QueryCapabilities();
    static bool SupportsColorFormat(EOmniCaptureColorFormat Format);

    bool IsInitialized() const { return bInitialized; }
    FString GetOutputFilePath() const { return OutputFilePath; }

private:
    FString OutputFilePath;
    bool bInitialized = false;
    EOmniCaptureColorFormat ColorFormat = EOmniCaptureColorFormat::NV12;
    bool bZeroCopyRequested = true;
    EOmniCaptureCodec RequestedCodec = EOmniCaptureCodec::HEVC;

#if WITH_OMNI_NVENC
    TSharedPtr<AVEncoder::FVideoEncoder> VideoEncoder;
    TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput;
    AVEncoder::FVideoEncoder::FLayerConfig LayerConfig;
    AVEncoder::FVideoEncoder::FCodecConfig CodecConfig;
    FCriticalSection EncoderCS;
    TArray<uint8> AnnexBBuffer;
    TUniquePtr<IFileHandle> BitstreamFile;
#endif
};

