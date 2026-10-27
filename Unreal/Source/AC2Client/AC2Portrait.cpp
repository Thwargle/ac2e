#include "AC2Portrait.h"
#include "AC2CharacterData.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProceduralMeshComponent.h"

AAC2Portrait::AAC2Portrait()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PortraitStage"));
    Stage = CreateDefaultSubobject<USceneComponent>(TEXT("NativeUIScene"));
    Stage->SetupAttachment(RootComponent);
    Body = CreateDefaultSubobject<USceneComponent>(TEXT("Avatar"));
    Body->SetupAttachment(RootComponent);
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortraitCamera"));
    Capture->SetupAttachment(RootComponent);
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->FOVAngle = 35;
    Capture->bCaptureEveryFrame = true;
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
    Capture->PostProcessSettings.AutoExposureBias = 9;
    KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PortraitLight"));
    auto *Light = KeyLight.Get();
    Light->SetupAttachment(RootComponent);
    Light->SetRelativeLocation(FVector(100, 300, 200));
    Light->SetIntensity(60000);
    Light->SetAttenuationRadius(6000);
    Light->SetCastShadows(false);
    SetActorEnableCollision(false);
}
bool AAC2Portrait::Show(const FString &Directory, uint32 Physics, float Height, float Build,
                        const FAC2Appearance &Appearance, uint32 Scene)
{
    Model.Reset();
    Capture->ShowOnlyActors.Reset();
    Capture->ShowOnlyActors.Add(this);
    for (auto &C : Generated)
        if (C.IsValid())
            C->DestroyComponent();
    Generated.Reset();
    if (!Image)
    {
        Image = NewObject<UTextureRenderTarget2D>(this);
        Image->ClearColor = FLinearColor(.018f, .026f, .03f, 1);
        Image->InitAutoFormat(640, 674);
        Capture->TextureTarget = Image;
        Capture->ShowOnlyActors.Add(this);
    }
    if (SceneId != Scene)
    {
        for (auto &C : StageComponents)
            if (C.IsValid())
                C->DestroyComponent();
        StageComponents.Reset();
        Backdrop.Reset();
        SceneId = Scene;
        if (Scene)
        {
            TInlineComponentArray<UActorComponent *> BeforeStage(this);
            Backdrop = MakeUnique<FAC2AssetLibrary>();
            FString Error;
            if (!Backdrop->Open(Directory, Error) || !Backdrop->DrawUIScene(Scene, this, Stage))
                UE_LOG(LogTemp, Warning, TEXT("AC2 creator stage %08X could not be drawn"), Scene);
            TInlineComponentArray<UActorComponent *> AfterStage(this);
            for (auto *C : AfterStage)
                if (!BeforeStage.Contains(C))
                    StageComponents.Add(C);
        }
    }
    Body->SetRelativeTransform(FTransform::Identity);
    TInlineComponentArray<UActorComponent *> Before(this);
    Model = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    Ready = Model->Open(Directory, Error) && Model->DrawModel(Physics, this, Body, Appearance);
    TInlineComponentArray<UActorComponent *> After(this);
    for (auto *C : After)
        if (!Before.Contains(C))
            Generated.Add(C);
    FBox Box(ForceInit);
    TInlineComponentArray<UProceduralMeshComponent *> Meshes(this);
    for (auto *M : Meshes)
    {
        if (M->GetAttachParent() != Body)
            continue;
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        for (int32 S = 0; S < M->GetNumSections(); ++S)
            if (auto *Section = M->GetProcMeshSection(S))
                for (const auto &V : Section->ProcVertexBuffer)
                    Box += M->GetRelativeTransform().TransformPosition(V.Position);
    }
    if (!Ready || !Box.IsValid)
        return Ready = false;
    // Hair, boots and the current idle pose must not reframe the camera or move the floor.
    // Cache an unscaled reference per native race/sex entity; height/build change the body only.
    if (!ReferenceBounds.Contains(Physics))
        ReferenceBounds.Add(Physics, Box);
    const FBox &Reference = ReferenceBounds[Physics];
    Body->SetRelativeScale3D(FVector(Build, Build, Height));
    Body->SetRelativeLocation(FVector(0, 0, -Reference.Min.Z * Height));
    ModelHeight = Reference.GetSize().Z;
    ModelWidth = FMath::Max(Reference.GetSize().X, Reference.GetSize().Y);
    // Keep the animation at the same time when rebuilding for a slider change.
    Model->Animate(Time, 0);
    SetCamera(HeadCamera, Yaw);
    return true;
}
void AAC2Portrait::SetCamera(bool Head, float Rotation)
{
    HeadCamera = Head;
    Yaw = Rotation;
    Body->SetRelativeRotation(FRotator(0, Yaw, 0));
    const float Distance = Head ? ModelHeight * .5f : FMath::Max(ModelHeight * 1.75f, ModelWidth * 2.3f);
    Capture->SetRelativeLocation(FVector(0, Distance, ModelHeight * (Head ? .88f : .52f)));
    Capture->SetRelativeRotation(FRotator(0, -90, 0));
    KeyLight->SetRelativeLocation(FVector(80, Distance + 100, ModelHeight * .9f));
    KeyLight->SetIntensity(50000 * FMath::Square((Distance + 100) / 200));
}
void AAC2Portrait::Rotate(float Degrees)
{
    SetCamera(HeadCamera, Yaw + Degrees);
}
void AAC2Portrait::Tick(float Delta)
{
    Super::Tick(Delta);
    Time += Delta;
    for (auto &Library : LineupModels)
        Library->Animate(Time, 0);
    if (Ready && Model)
    {
        const FVector V = FVector::ZeroVector;
        Model->Animate(Time, 0, nullptr, &V);
    }
}
void AAC2Portrait::EndPlay(const EEndPlayReason::Type Reason)
{
    Model.Reset();
    Backdrop.Reset();
    LineupModels.Reset();
    Super::EndPlay(Reason);
}

bool AAC2Portrait::ShowLineup(const FString &Directory, const FAC2CharacterCatalog &Catalog)
{
    if (!LineupModels.IsEmpty())
        return Ready;
    Image = NewObject<UTextureRenderTarget2D>(this);
    Image->ClearColor = FLinearColor::Black;
    Image->InitAutoFormat(1024, 619);
    Capture->TextureTarget = Image;
    Capture->ShowOnlyActors.Add(this);
    Backdrop = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    FAC2UISceneInfo Scene;
    if (!Backdrop->Open(Directory, Error) || !Backdrop->DrawUIScene(0x2100000b, this, Stage, &Scene))
        return false;
    for (const auto &A : Catalog.Avatars)
    {
        const auto *At = Scene.Locations.Find(A.Location);
        if (!At)
            return false;
        auto *Root = NewObject<USceneComponent>(this);
        AddInstanceComponent(Root);
        Root->SetupAttachment(Stage);
        Root->SetRelativeTransform(*At);
        Root->RegisterComponent();
        auto Library = MakeUnique<FAC2AssetLibrary>();
        TInlineComponentArray<UActorComponent *> Before(this);
        if (!Library->Open(Directory, Error) ||
            !Library->DrawModel(A.Physics, this, Root, A.Resolve(A.Defaults())))
            return false;
        FBox Box(ForceInit);
        TInlineComponentArray<UProceduralMeshComponent *> Meshes(this);
        for (auto *M : Meshes)
            if (!Before.Contains(M))
                for (int32 S = 0; S < M->GetNumSections(); ++S)
                    if (auto *Section = M->GetProcMeshSection(S))
                        for (const auto &V : Section->ProcVertexBuffer)
                            Box += (*At).TransformPosition(
                                M->GetRelativeTransform().TransformPosition(V.Position));
        LineupBounds.Add(Box);
        LineupModels.Add(MoveTemp(Library));
    }
    // SceneTool uses +Y forward and a vertical 65-degree field; UE uses +X and horizontal FOV.
    Capture->SetRelativeLocation(Scene.Camera.GetLocation());
    Capture->SetRelativeRotation((Scene.Camera.GetRotation() * FQuat(FVector::UpVector, PI / 2)).Rotator());
    Capture->FOVAngle =
        FMath::RadiansToDegrees(2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(65.f / 2)) * 1024 / 619));
    KeyLight->SetRelativeLocation(Scene.Camera.GetLocation() + FVector(0, 100, 100));
    KeyLight->SetIntensity(300000);
    KeyLight->SetAttenuationRadius(7000);
    return Ready = LineupModels.Num() == Catalog.Avatars.Num();
}
int32 AAC2Portrait::PickAvatar(FVector2D Point) const
{
    const FTransform View = Capture->GetRelativeTransform().Inverse();
    float Tangent = FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * .5f));
    int32 Best = INDEX_NONE;
    double Distance = BIG_NUMBER;
    for (int32 I = 0; I < LineupBounds.Num(); ++I)
    {
        const auto &B = LineupBounds[I];
        FBox2D Box(ForceInit);
        for (int32 C = 0; C < 8; ++C)
        {
            FVector P = View.TransformPosition(
                FVector(C & 1 ? B.Max.X : B.Min.X, C & 2 ? B.Max.Y : B.Min.Y, C & 4 ? B.Max.Z : B.Min.Z));
            if (P.X <= 1)
                continue;
            Box += FVector2D(.5 + P.Y / (2 * P.X * Tangent), .5 - P.Z / (2 * P.X * Tangent * 619 / 1024));
        }
        double D = View.TransformPosition(B.GetCenter()).X;
        if (Box.bIsValid && Box.IsInside(Point) && D < Distance)
        {
            Best = I;
            Distance = D;
        }
    }
    return Best;
}
