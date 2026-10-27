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
        Floor = 0;
        StageFloors.Reset();
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
            for (auto& Component : StageComponents)
                if (auto* Mesh = Cast<UProceduralMeshComponent>(Component.Get()))
                    for (int32 S = 0; S < Mesh->GetNumSections(); ++S)
                    {
                        const auto* Sec = Mesh->GetProcMeshSection(S);
                        if (!Sec || !Sec->bSectionVisible) continue;
                        for (int32 I = 0; I + 2 < Sec->ProcIndexBuffer.Num(); I += 3)
                        {
                            FVector P[3];
                            for (int32 J = 0; J < 3; ++J)
                                P[J] = Mesh->GetRelativeTransform().TransformPosition(Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[I + J]].Position);
                            const FVector N = FVector::CrossProduct(P[1] - P[0], P[2] - P[0]);
                            if (FMath::Abs(N.Z) <= .7 * N.Size() || FMath::Abs(N.Z) < SMALL_NUMBER) continue;
                            const double Z = FVector::DotProduct(N, P[0]) / N.Z;
                            const FVector At(0, 0, Z);
                            if (FMath::ClosestPointOnTriangleToPoint(At, P[0], P[1], P[2]).Equals(At, .01))
                                StageFloors.Add(Z);
                        }
                    }
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
    if (!Ready) return false;
    if (!References.Contains(Physics))
    {
        FReference Ref;
        Ref.Body = Model->ModelBounds();
        Ref.Head = Model->ModelBounds(0x8100001b); // Native MeshType::Head.
        const FBox Feet = Model->ModelBounds(0x81000001);
        if (!Ref.Body.IsValid) return Ready = false;
        Ref.Feet = Feet.IsValid ? Feet.Min.Z : Ref.Body.Min.Z;
        if (!Ref.Head.IsValid)
        {
            // Drudge has a single body mesh rather than a separate head part.
            Ref.Head = Ref.Body;
            Ref.Head.Min.Z = FMath::Lerp(Ref.Body.Min.Z, Ref.Body.Max.Z, .72);
        }
        References.Add(Physics, Ref);
    }
    Reference = References[Physics];
    // A short downward trace around the authored avatar origin hits the top of
    // the stand, excluding its lower floors and the Empyrean sky dome overhead.
    const double Reach = Reference.Body.GetSize().Z;
    double Support = -BIG_NUMBER;
    for (float Z : StageFloors)
        if (Z >= -Reach && Z <= Reach) Support = FMath::Max(Support, double(Z));
    Floor = Support > -BIG_NUMBER ? Support : 0;
    HeadBounds = Model->ModelBounds(0x8100001b);
    if (!HeadBounds.IsValid) HeadBounds = Reference.Head;
    for (uint32 Type : {0x81000003u, 0x81000004u})
    { const FBox Part = Model->ModelBounds(Type); if (Part.IsValid) HeadBounds += Part; }
    Body->SetRelativeScale3D(FVector(Build, Build, Height));
    Body->SetRelativeLocation(FVector(0, 0, Floor - Reference.Feet * Height));
    // Preserve the idle phase and framing while changing hair, face or clothes.
    Model->Animate(Time, 0);
    SetCamera(HeadCamera, Yaw);
    return true;
}
void AAC2Portrait::SetCamera(bool Head, float Rotation)
{
    HeadCamera = Head;
    Yaw = Rotation;
    Body->SetRelativeRotation(FRotator(0, Yaw, 0));
    if (!Reference.Body.IsValid) return;
    const float Height = Reference.Body.GetSize().Z * Body->GetRelativeScale3D().Z;
    FBox Focus = Head ? HeadBounds + Reference.Head : Reference.Body;
    // Reserve idle-motion margins. The face target stays fixed; larger native
    // crests can widen the head view without shifting the target or the feet.
    Focus = Focus.ExpandBy(Head ? FVector(4, 4, 8) : FVector(3, 3, 8));
    const FVector LocalTarget = Head ? Reference.Head.GetCenter() : FVector(0, 0, Focus.GetCenter().Z);
    // A rotation-invariant envelope avoids zooming in/out as the rotation slider turns.
    float Radius = 0;
    for (int32 C = 0; C < 4; ++C)
        Radius = FMath::Max(Radius, float(FVector2D((C & 1 ? Focus.Max.X : Focus.Min.X) - LocalTarget.X,
                                                   (C & 2 ? Focus.Max.Y : Focus.Min.Y) - LocalTarget.Y).Size()));
    const FVector Target = Body->GetRelativeTransform().TransformPosition(LocalTarget);
    Radius *= Body->GetRelativeScale3D().X;
    const float Low = Body->GetRelativeTransform().TransformPosition(Focus.Min).Z;
    const float High = Body->GetRelativeTransform().TransformPosition(Focus.Max).Z;
    const float Fill = Head ? .8f : .7f;
    const float Aspect = float(Image->SizeY) / Image->SizeX;
    const float TanH = FMath::Tan(FMath::DegreesToRadians(35.f * .5f));
    const float Vertical = FMath::Max(Target.Z - Low, High - Target.Z);
    // Fit the rotation envelope in the usable part of the native viewport,
    // leaving the arch and title clear. A circle avoids a box's fictitious corners.
    float Distance = FMath::Max(FMath::Sqrt(FMath::Square(Radius) + FMath::Square(Radius / (Fill * TanH))),
                               Radius + Vertical / (Fill * TanH * Aspect));
    // The native stages are enclosed sets. Widen the lens rather than pulling
    // the camera into a foreground tree when fitting a broad body or long tail.
    Distance = FMath::Max(Radius + 1, FMath::Min(Distance, Height * (Head ? 1.f : 1.75f)));
    const float FitTan = FMath::Max3(TanH,
        Radius / (Fill * FMath::Sqrt(FMath::Square(Distance) - FMath::Square(Radius))),
        Vertical / (Fill * (Distance - Radius) * Aspect));
    Capture->FOVAngle = FMath::RadiansToDegrees(2 * FMath::Atan(FitTan));
    Capture->SetRelativeLocation(Target + FVector(0, Distance, 0));
    Capture->SetRelativeRotation(FRotator(0, -90, 0));
    KeyLight->SetRelativeLocation(Target + FVector(80, Distance + 100, Height * .4f));
    KeyLight->SetIntensity(50000 * FMath::Square((Distance + 100) / 200));
}
bool AAC2Portrait::AuditFraming(FString& Error) const
{
    if (!Ready || !Model) { Error = TEXT("Portrait unavailable"); return false; }
    FBox Feet = Model->ModelBounds(0x81000001);
    if (!Feet.IsValid) Feet = Model->ModelBounds();
    const float FootZ = Body->GetRelativeTransform().TransformPosition(Feet.Min).Z;
    if (FMath::Abs(FootZ - Floor) > 5)
    { Error = FString::Printf(TEXT("Feet are %g cm from pedestal"), FootZ - Floor); return false; }
    FBox Visible = HeadCamera ? Model->ModelBounds(0x8100001b) : Model->ModelBounds();
    if (HeadCamera)
    {
        for (uint32 Type : {0x81000003u, 0x81000004u})
        { const FBox Part = Model->ModelBounds(Type); if (Part.IsValid) Visible += Part; }
    }
    if (!Visible.IsValid) Visible = Reference.Head;
    const FTransform View = Capture->GetRelativeTransform().Inverse();
    const float TanH = FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * .5f));
    const float TanV = TanH * Image->SizeY / Image->SizeX;
    for (int32 C = 0; C < 8; ++C)
    {
        FVector P(C & 1 ? Visible.Max.X : Visible.Min.X, C & 2 ? Visible.Max.Y : Visible.Min.Y, C & 4 ? Visible.Max.Z : Visible.Min.Z);
        P = View.TransformPosition(Body->GetRelativeTransform().TransformPosition(P));
        const FVector2D UV(.5 + P.Y / (2 * P.X * TanH), .5 - P.Z / (2 * P.X * TanV));
        if (P.X <= 0 || UV.X < .07 || UV.X > .93 || UV.Y < .1 || UV.Y > .93)
        { Error = FString::Printf(TEXT("%s clipped at %s"), HeadCamera ? TEXT("Head") : TEXT("Body"), *UV.ToString()); return false; }
    }
    return true;
}
void AAC2Portrait::Rotate(float Degrees)
{
    SetCamera(HeadCamera, Yaw + Degrees);
}
void AAC2Portrait::Tick(float Delta)
{
    Super::Tick(Delta);
    Time += Delta;
    if (!Arms.IsEmpty())
    {
        ArmTime = FMath::Min(1.5f, ArmTime + Delta);
        for (int32 I = 0; I < Arms.Num(); ++I)
            if (Arms[I].IsValid())
                Arms[I]->SetRelativeRotation(FQuat::Slerp(ArmStart[I], ArmEnd[I], ArmTime / 1.5f));
        if (ArmTime >= 1.5f && ArmCurrent != ArmTarget)
        {
            const int32 N = Arms.Num();
            const bool Left = (ArmTarget - ArmCurrent + N) % N <= N / 2;
            const int32 Next = (ArmCurrent + (Left ? 1 : N - 1)) % N;
            for (int32 I = 0; I < N; ++I)
            {
                const float Angle = (I == ArmCurrent || I == Next ? 45.f : 270.f / (N - 2)) * (Left ? 1 : -1);
                ArmStart[I] = Arms[I]->GetRelativeRotation().Quaternion();
                ArmEnd[I] = FQuat(FVector::UpVector, FMath::DegreesToRadians(Angle)) * ArmStart[I];
            }
            ArmCurrent = Next;
            ArmTime = 0;
        }
    }
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
        const auto *At = Scene.Locations.Find(A.HoldingLocation);
        if (!At)
            return false;
        auto *Root = NewObject<USceneComponent>(this);
        AddInstanceComponent(Root);
        Root->SetupAttachment(Stage);
        Root->SetRelativeTransform(*At);
        Root->RegisterComponent();
        auto Library = MakeUnique<FAC2AssetLibrary>();
        if (!Library->Open(Directory, Error) ||
            !Library->DrawModel(A.Physics, this, Root, A.Resolve(A.Defaults())))
            return false;
        const FBox Box = Library->ModelBounds().TransformBy(*At);
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
        const FBox B = Arms.IsValidIndex(I) ? LineupBounds[I].TransformBy(FigureFrames[I] * Arms[I]->GetRelativeTransform()) : LineupBounds[I];
        FBox2D Box(ForceInit);
        for (int32 C = 0; C < 8; ++C)
        {
            FVector P = View.TransformPosition(
                FVector(C & 1 ? B.Max.X : B.Min.X, C & 2 ? B.Max.Y : B.Min.Y, C & 4 ? B.Max.Z : B.Min.Z));
            if (P.X <= 1)
                continue;
            Box += FVector2D(.5 + P.Y / (2 * P.X * Tangent), .5 - P.Z / (2 * P.X * Tangent * Image->SizeY / Image->SizeX));
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

FVector2D AAC2Portrait::AvatarPoint(int32 Index) const
{
    if (!LineupBounds.IsValidIndex(Index)) return FVector2D(-1,-1);
    FBox B = Arms.IsValidIndex(Index) ? LineupBounds[Index].TransformBy(FigureFrames[Index] * Arms[Index]->GetRelativeTransform()) : LineupBounds[Index];
    const FVector P = Capture->GetRelativeTransform().InverseTransformPosition(B.GetCenter());
    const float Tan = FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * .5f));
    return P.X <= 1 ? FVector2D(-1,-1) : FVector2D(.5 + P.Y/(2*P.X*Tan), .5-P.Z/(2*P.X*Tan*Image->SizeY/Image->SizeX));
}
void AAC2Portrait::SelectCharacter(int32 Index)
{
    if (LineupBounds.IsValidIndex(Index)) ArmTarget = Index;
}
bool AAC2Portrait::ShowSelection(const FString &Directory, const TArray<FAC2SelectionFigure> &Figures, int32 Selected, int32 Slots)
{
    Image = NewObject<UTextureRenderTarget2D>(this);
    Image->ClearColor = FLinearColor::Black;
    Image->InitAutoFormat(706,768);
    Capture->TextureTarget = Image;
    Capture->ShowOnlyActors.Add(this);
    Backdrop = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    FAC2UISceneInfo Scene;
    if (!Backdrop->Open(Directory, Error) || !Backdrop->DrawUIScene(0x21000001,this,Stage,&Scene)) return false;
    FTransform Holding;
    if (!Backdrop->HoldingLocation(0x1f000068,1,Holding)) return false;
    Slots=FMath::Max(3,FMath::Max(Slots,Figures.Num()));
    ArmCurrent = ArmTarget = FMath::Clamp(Selected,0,Slots-1);
    for (int32 I=0; I<Slots; ++I)
    {
        auto *Arm = NewObject<USceneComponent>(this);
        AddInstanceComponent(Arm); Arm->SetupAttachment(Stage); Arm->RegisterComponent();
        const int32 Relative = (I-ArmCurrent+Slots)%Slots;
        const float Angle = Relative ? -45.f-(Relative-1)*270.f/(Slots-2) : 0;
        Arm->SetRelativeRotation(FRotator(0,Angle,0));
        Arms.Add(Arm); ArmStart.Add(Arm->GetRelativeRotation().Quaternion()); ArmEnd.Add(ArmStart.Last());
        auto ArmModel = MakeUnique<FAC2AssetLibrary>();
        if (!ArmModel->Open(Directory,Error) || !ArmModel->DrawModel(0x1f000068,this,Arm)) return false;
        LineupModels.Add(MoveTemp(ArmModel));
        if (!Figures.IsValidIndex(I)) continue;
        const auto &Figure=Figures[I];
        auto *Root=NewObject<USceneComponent>(this);
        AddInstanceComponent(Root); Root->SetupAttachment(Arm);
        FTransform At=Holding;
        At.MultiplyScale3D(FVector(Figure.Build,Figure.Build,Figure.Height));
        Root->SetRelativeTransform(At); Root->RegisterComponent(); FigureFrames.Add(At);
        auto Library=MakeUnique<FAC2AssetLibrary>();
        if (!Library->Open(Directory,Error) || !Library->DrawModel(Figure.Physics,this,Root,Figure.Appearance)) return false;
        const FBox Box = Library->ModelBounds();
        if (!Box.IsValid) return false;
        LineupBounds.Add(Box); LineupModels.Add(MoveTemp(Library));
    }
    Capture->SetRelativeLocation(Scene.Camera.GetLocation());
    Capture->SetRelativeRotation((Scene.Camera.GetRotation()*FQuat(FVector::UpVector,PI/2)).Rotator());
    Capture->FOVAngle=FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(32.5f))*706/768));
    KeyLight->SetRelativeLocation(Scene.Camera.GetLocation()+FVector(0,100,100));
    KeyLight->SetIntensity(300000); KeyLight->SetAttenuationRadius(7000);
    return Ready=true;
}
