#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"
#include "OmniCaptureRigActor.h"

struct FOmniCaptureEquirectResult
{
    TUniquePtr<FImagePixelData> PixelData;
    TArray<FColor> PreviewPixels;
    FIntPoint Size = FIntPoint::ZeroValue;
    bool bIsLinear = false;
    bool bUsedCPUFallback = false;
    TRefCountPtr<IPooledRenderTarget> OutputTarget;
    FTexture2DRHIRef Texture;
    FGPUFenceRHIRef ReadyFence;
    TArray<TRefCountPtr<IPooledRenderTarget>> EncoderPlanes;
};

class OMNICAPTURE_API FOmniCaptureEquirectConverter
{
public:
    static FOmniCaptureEquirectResult ConvertToEquirectangular(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye);
};

