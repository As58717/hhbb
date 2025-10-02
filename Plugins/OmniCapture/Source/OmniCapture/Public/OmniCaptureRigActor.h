#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OmniCaptureTypes.h"
#include "OmniCaptureRigActor.generated.h"

class USceneComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UENUM()
enum class EOmniCaptureEye : uint8
{
    Left,
    Right
};

struct FOmniCaptureFaceResources
{
    UTextureRenderTarget2D* RenderTarget = nullptr;
};

struct FOmniEyeCapture
{
    FOmniCaptureFaceResources Faces[6];
};

UCLASS(NotBlueprintable)
class OMNICAPTURE_API AOmniCaptureRigActor final : public AActor
{
    GENERATED_BODY()

public:
    AOmniCaptureRigActor();

    void Configure(const FOmniCaptureSettings& InSettings);
    void Capture(FOmniEyeCapture& OutLeftEye, FOmniEyeCapture& OutRightEye) const;

    FORCEINLINE const FTransform& GetRigTransform() const { return RigRoot->GetComponentTransform(); }

private:
    void BuildEyeRig(EOmniCaptureEye Eye, float IPDHalfCm);
    void ConfigureCaptureComponent(USceneCaptureComponent2D* CaptureComponent) const;
    void CaptureEye(EOmniCaptureEye Eye, FOmniEyeCapture& OutCapture) const;

    static void GetOrientationForFace(int32 FaceIndex, FRotator& OutRotation);

private:
    UPROPERTY()
    USceneComponent* RigRoot;

    UPROPERTY()
    USceneComponent* LeftEyeRoot;

    UPROPERTY()
    USceneComponent* RightEyeRoot;

    UPROPERTY()
    TArray<USceneCaptureComponent2D*> LeftEyeCaptures;

    UPROPERTY()
    TArray<USceneCaptureComponent2D*> RightEyeCaptures;

    UPROPERTY(Transient)
    TArray<UTextureRenderTarget2D*> RenderTargets;

    FOmniCaptureSettings CachedSettings;
};

