#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

class IImageWriteQueue;
class FImageWriteTask;

class OMNICAPTURE_API FOmniCapturePNGWriter
{
public:
    FOmniCapturePNGWriter();
    ~FOmniCapturePNGWriter();

    void Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory);
    void EnqueueFrame(TUniquePtr<FOmniCaptureFrame>&& Frame, const FString& FrameFileName);
    void Flush();
    const TArray<FOmniCaptureFrameMetadata>& GetCapturedFrames() const { return CapturedMetadata; }
    TArray<FOmniCaptureFrameMetadata> ConsumeCapturedFrames();

private:
    IImageWriteQueue* ImageWriteQueue = nullptr;
    FString OutputDirectory;
    FString SequenceBaseName;

    TArray<FOmniCaptureFrameMetadata> CapturedMetadata;
    FCriticalSection MetadataCS;
};

