#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OmniCapturePreviewActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FOmniCaptureEquirectResult;

UCLASS()
class OMNICAPTURE_API AOmniCapturePreviewActor : public AActor
{
    GENERATED_BODY()

public:
    AOmniCapturePreviewActor();

    void Initialize(float InScale);
    void UpdatePreviewTexture(const FOmniCaptureEquirectResult& Result);
    void SetPreviewEnabled(bool bEnabled);

protected:
    virtual void BeginPlay() override;

private:
    void EnsureMaterial();
    void ApplyTexture(UTexture2D* Texture);
    void ResizePreviewTexture(const FIntPoint& Size);

private:
    UPROPERTY(Transient)
    UStaticMeshComponent* ScreenComponent;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* DynamicMaterial = nullptr;

    UPROPERTY(Transient)
    UTexture2D* PreviewTexture = nullptr;

    FName TextureParameterName = TEXT("SpriteTexture");
    float PreviewScale = 1.0f;
};
