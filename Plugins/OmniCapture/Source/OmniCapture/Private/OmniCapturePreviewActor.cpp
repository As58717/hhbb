#include "OmniCapturePreviewActor.h"

#include "Components/StaticMeshComponent.h"
#include "OmniCaptureEquirectConverter.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/Texture2DResource.h"

namespace
{
    static UStaticMesh* LoadPreviewPlane()
    {
        static TWeakObjectPtr<UStaticMesh> CachedMesh;
        if (!CachedMesh.IsValid())
        {
            CachedMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
        }
        return CachedMesh.Get();
    }

    static UMaterialInterface* LoadPreviewMaterial()
    {
        static TWeakObjectPtr<UMaterialInterface> CachedMaterial;
        if (!CachedMaterial.IsValid())
        {
            CachedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultSpriteMaterial.DefaultSpriteMaterial"));
        }
        return CachedMaterial.Get();
    }
}

AOmniCapturePreviewActor::AOmniCapturePreviewActor()
{
    PrimaryActorTick.bCanEverTick = false;
    ScreenComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewScreen"));
    SetRootComponent(ScreenComponent);
    ScreenComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ScreenComponent->bHiddenInGame = true;
}

void AOmniCapturePreviewActor::Initialize(float InScale)
{
    PreviewScale = FMath::Max(0.1f, InScale);

    if (UStaticMesh* PlaneMesh = LoadPreviewPlane())
    {
        ScreenComponent->SetStaticMesh(PlaneMesh);
    }

    ScreenComponent->SetRelativeScale3D(FVector(PreviewScale, PreviewScale, PreviewScale));
    ScreenComponent->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    EnsureMaterial();
}

void AOmniCapturePreviewActor::BeginPlay()
{
    Super::BeginPlay();
    EnsureMaterial();
}

void AOmniCapturePreviewActor::EnsureMaterial()
{
    if (!ScreenComponent)
    {
        return;
    }

    if (!DynamicMaterial)
    {
        if (UMaterialInterface* BaseMaterial = LoadPreviewMaterial())
        {
            DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
            ScreenComponent->SetMaterial(0, DynamicMaterial);
        }
    }
}

void AOmniCapturePreviewActor::SetPreviewEnabled(bool bEnabled)
{
    if (ScreenComponent)
    {
        ScreenComponent->SetVisibility(bEnabled);
        ScreenComponent->bHiddenInGame = !bEnabled;
    }
}

void AOmniCapturePreviewActor::ResizePreviewTexture(const FIntPoint& Size)
{
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return;
    }

    if (PreviewTexture && PreviewTexture->GetSizeX() == Size.X && PreviewTexture->GetSizeY() == Size.Y)
    {
        return;
    }

    PreviewTexture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
    PreviewTexture->MipGenSettings = TMGS_NoMipmaps;
    PreviewTexture->CompressionSettings = TC_HDR;
    PreviewTexture->SRGB = true;
    PreviewTexture->UpdateResourceImmediate();

    ApplyTexture(PreviewTexture);
}

void AOmniCapturePreviewActor::ApplyTexture(UTexture2D* Texture)
{
    EnsureMaterial();
    if (DynamicMaterial && Texture)
    {
        DynamicMaterial->SetTextureParameterValue(TextureParameterName, Texture);
    }
}

void AOmniCapturePreviewActor::UpdatePreviewTexture(const FOmniCaptureEquirectResult& Result)
{
    const FIntPoint Size = Result.Size;
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return;
    }

    ResizePreviewTexture(Size);
    if (!PreviewTexture)
    {
        return;
    }

    FTexture2DMipMap& Mip = PreviewTexture->GetPlatformData()->Mips[0];
    void* TextureMemory = Mip.BulkData.Lock(LOCK_READ_WRITE);

    if (Result.PreviewPixels.Num() != Size.X * Size.Y)
    {
        Mip.BulkData.Unlock();
        return;
    }

    FMemory::Memcpy(TextureMemory, Result.PreviewPixels.GetData(), Result.PreviewPixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    PreviewTexture->UpdateResource();
}
