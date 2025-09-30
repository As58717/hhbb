#include "OmniCaptureRigActor.h"

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"

namespace
{
    constexpr int32 FaceCount = 6;
}

AOmniCaptureRigActor::AOmniCaptureRigActor()
{
    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;

    RigRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RigRoot"));
    SetRootComponent(RigRoot);

    LeftEyeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftEyeRoot"));
    LeftEyeRoot->SetupAttachment(RigRoot);

    RightEyeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RightEyeRoot"));
    RightEyeRoot->SetupAttachment(RigRoot);
}

void AOmniCaptureRigActor::Configure(const FOmniCaptureSettings& InSettings)
{
    CachedSettings = InSettings;

    const float IPDHalf = CachedSettings.Mode == EOmniCaptureMode::Stereo
        ? CachedSettings.InterPupillaryDistanceCm * 0.5f
        : 0.0f;

    for (USceneCaptureComponent2D* Capture : LeftEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }

    for (USceneCaptureComponent2D* Capture : RightEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }

    for (UTextureRenderTarget2D* RenderTarget : RenderTargets)
    {
        if (RenderTarget)
        {
            RenderTarget->ConditionalBeginDestroy();
        }
    }

    LeftEyeCaptures.Empty();
    RightEyeCaptures.Empty();
    RenderTargets.Empty();

    BuildEyeRig(EOmniCaptureEye::Left, -IPDHalf);

    if (CachedSettings.Mode == EOmniCaptureMode::Stereo)
    {
        BuildEyeRig(EOmniCaptureEye::Right, IPDHalf);
    }
}

void AOmniCaptureRigActor::Capture(FOmniEyeCapture& OutLeftEye, FOmniEyeCapture& OutRightEye) const
{
    CaptureEye(EOmniCaptureEye::Left, OutLeftEye);

    if (CachedSettings.Mode == EOmniCaptureMode::Stereo && RightEyeCaptures.Num() > 0)
    {
        CaptureEye(EOmniCaptureEye::Right, OutRightEye);
    }
    else
    {
        OutRightEye = OutLeftEye;
    }
}

void AOmniCaptureRigActor::BuildEyeRig(EOmniCaptureEye Eye, float IPDHalfCm)
{
    USceneComponent* EyeRoot = Eye == EOmniCaptureEye::Left ? LeftEyeRoot : RightEyeRoot;

    if (!EyeRoot)
    {
        return;
    }

    EyeRoot->SetRelativeLocation(FVector(0.0f, IPDHalfCm, 0.0f));

    TArray<USceneCaptureComponent2D*>& TargetArray = Eye == EOmniCaptureEye::Left ? LeftEyeCaptures : RightEyeCaptures;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        FString ComponentName = FString::Printf(TEXT("%s_CaptureFace_%d"), Eye == EOmniCaptureEye::Left ? TEXT("Left") : TEXT("Right"), FaceIndex);
        USceneCaptureComponent2D* CaptureComponent = NewObject<USceneCaptureComponent2D>(this, *ComponentName);
        CaptureComponent->SetupAttachment(EyeRoot);
        CaptureComponent->RegisterComponent();
        ConfigureCaptureComponent(CaptureComponent);

        FRotator FaceRotation;
        GetOrientationForFace(FaceIndex, FaceRotation);
        CaptureComponent->SetRelativeRotation(FaceRotation);

        TargetArray.Add(CaptureComponent);
    }
}

void AOmniCaptureRigActor::ConfigureCaptureComponent(USceneCaptureComponent2D* CaptureComponent) const
{
    if (!CaptureComponent)
    {
        return;
    }

    CaptureComponent->FOVAngle = 90.0f;
    CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this);
    check(RenderTarget);

    const EPixelFormat PixelFormat = PF_FloatRGBA;
    RenderTarget->InitCustomFormat(CachedSettings.Resolution, CachedSettings.Resolution, PixelFormat, false);
    RenderTarget->TargetGamma = CachedSettings.Gamma == EOmniCaptureGamma::Linear ? 1.0f : 2.2f;
    RenderTarget->bAutoGenerateMips = false;
    RenderTarget->ClearColor = FLinearColor::Black;
    RenderTarget->Filter = TF_Bilinear;

    CaptureComponent->TextureTarget = RenderTarget;
    RenderTargets.Add(RenderTarget);
}

void AOmniCaptureRigActor::CaptureEye(EOmniCaptureEye Eye, FOmniEyeCapture& OutCapture) const
{
    const TArray<USceneCaptureComponent2D*>& CaptureComponents = Eye == EOmniCaptureEye::Left ? LeftEyeCaptures : RightEyeCaptures;

    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        OutCapture.Faces[FaceIndex].RenderTarget = nullptr;
    }

    for (int32 FaceIndex = 0; FaceIndex < CaptureComponents.Num(); ++FaceIndex)
    {
        if (USceneCaptureComponent2D* CaptureComponent = CaptureComponents[FaceIndex])
        {
            CaptureComponent->CaptureScene();

            UTextureRenderTarget2D* RenderTarget = Cast<UTextureRenderTarget2D>(CaptureComponent->TextureTarget);
            OutCapture.Faces[FaceIndex].RenderTarget = RenderTarget;
        }
    }
}

void AOmniCaptureRigActor::GetOrientationForFace(int32 FaceIndex, FRotator& OutRotation)
{
    switch (FaceIndex)
    {
    case 0: // +X
        OutRotation = FRotator(0.0f, 90.0f, 0.0f);
        break;
    case 1: // -X
        OutRotation = FRotator(0.0f, -90.0f, 0.0f);
        break;
    case 2: // +Y
        OutRotation = FRotator(-90.0f, 0.0f, 0.0f);
        break;
    case 3: // -Y
        OutRotation = FRotator(90.0f, 0.0f, 0.0f);
        break;
    case 4: // +Z
        OutRotation = FRotator(0.0f, 0.0f, 0.0f);
        break;
    case 5: // -Z
        OutRotation = FRotator(0.0f, 180.0f, 0.0f);
        break;
    default:
        OutRotation = FRotator::ZeroRotator;
        break;
    }
}

