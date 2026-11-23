#include "AC2Sky.h"
#include "AC2WaterReflection.h"
#include "AC2AssetReader.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "AC2Fog.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

FVector FAC2SkyKey::LightDirection() const
{
    // Vector::FromSpherical uses heading clockwise from +Y and elevation from the horizon.
    // Keep elevations beyond 90 degrees: the native sun sweeps over the zenith.
    const double H = FMath::DegreesToRadians(Heading), P = FMath::DegreesToRadians(Pitch);
    return FVector(-FMath::Sin(H) * FMath::Cos(P), -FMath::Cos(H) * FMath::Cos(P), -FMath::Sin(P));
}

float AC2::SkyTimeRatio(float Before, float After, float Time)
{
    if (Before == After)
        return 0;
    if (Before < After)
        return Time >= Before && Time <= After ? (Time - Before) / (After - Before) : 0.5f;
    const float Span = After - Before + 1;
    return Span > 0 ? FMath::Fmod(Time - Before + 1, 1) / Span : 0;
}
float AC2::SkySweep(float Begin, float End, float Ratio, bool West)
{
    if (!West && End < Begin)
        Begin -= 360;
    else if (West && Begin < End)
        End -= 360;
    return FMath::Fmod(FMath::Lerp(Begin, End, Ratio) + 360, 360);
}
namespace
{
FLinearColor Packed(FAC2AssetReader &R)
{
    // CDayDesc interpolates HSB in native packed RGB, before display decoding.
    return FColor(R.Word()).ReinterpretAsLinear();
}
FLinearColor InterpolateSkyColor(const FLinearColor& Before, const FLinearColor& After, float Ratio)
{
    const FLinearColor A = Before.LinearRGBToHSV();
    const FLinearColor B = After.LinearRGBToHSV();
    float HueDelta = B.R - A.R;
    // CDayDesc::InterpAngleClosest takes the wrapped path even at exactly half a turn.
    if (HueDelta >= 180.f)
        HueDelta -= 360.f;
    else if (HueDelta <= -180.f)
        HueDelta += 360.f;
    return FLinearColor(A.R + HueDelta * Ratio, FMath::Lerp(A.G,B.G,Ratio), FMath::Lerp(A.B,B.B,Ratio),
                        FMath::Lerp(Before.A,After.A,Ratio)).HSVToLinearRGB();
}
void Keys(const FAC2SkyDay &Day, float T, const FAC2SkyKey *&Before, const FAC2SkyKey *&After)
{
    Before = After = nullptr;
    if (Day.Keys.IsEmpty())
        return;
    Before = &Day.Keys.Last();
    After = &Day.Keys[0];
    for (const auto &K : Day.Keys)
    {
        if (T < K.Time)
        {
            After = &K;
            break;
        }
        Before = &K;
    }
}
} // namespace
bool FAC2SkyDay::Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FAC2AssetReader R(Bytes);
    R.Word();
    Id = R.Word();
    if (Id != ExpectedId)
        return false;
    const int32 N = R.Count(64, 4096);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        FAC2SkyKey K;
        K.Time = R.Float();
        K.FogMin = R.Float();
        K.FogMax = R.Float();
        K.FogSky = R.Float();
        K.FogColor = Packed(R);
        K.Direct = R.Float();
        K.Heading = R.Float();
        K.Pitch = R.Float();
        K.DirectColor = Packed(R);
        K.Ambient = R.Float();
        K.AmbientColor = Packed(R);
        R.Word();
        const int32 NR = R.Count(20, 4096);
        for (int32 J = 0; J < NR; ++J)
        {
            FAC2SkyReplacement V;
            V.Name = R.Word();
            V.Graphic = R.Word();
            V.Pitch = R.Float();
            V.Heading = R.Float();
            V.Color = Packed(R);
            K.Replacements.Add(V);
        }
        // Native days include a terminal key at exactly 1.0 (midnight).
        K.Rain = R.Float();
        K.WindX = R.Float();
        K.WindY = R.Float();
        if (K.Time < 0 || K.Time > 1 || (Keys.Num() && K.Time < Keys.Last().Time))
            R.Good = false;
        Keys.Add(MoveTemp(K));
    }
    const int32 NO = R.Count(68, 4096);
    for (int32 I = 0; I < NO && R.Good; ++I)
    {
        FAC2SkyObject O;
        O.Name = R.Word();
        O.Graphic = R.Word();
        O.Begin = R.Float();
        O.End = R.Float();
        O.BeginPitch = R.Float();
        O.EndPitch = R.Float();
        O.BeginHeading = R.Float();
        O.EndHeading = R.Float();
        R.Float();
        O.After = R.Word() != 0;
        R.Word();
        O.Water = R.Word();
        R.Take(8);
        O.WestToEast = R.Word() != 0;
        R.Float();
        const int32 NF = R.Count(12, 4096);
        R.Take(int64(NF) * 12);
        Objects.Add(O);
    }
    return R.End() && !Keys.IsEmpty();
}
FAC2SkyKey FAC2SkyDay::Sample(float T) const
{
    const FAC2SkyKey *B, *A;
    ::Keys(*this, T, B, A);
    if (!B || !A)
        return {};
    const float R = AC2::SkyTimeRatio(B->Time, A->Time, T);
    FAC2SkyKey K;
    K.Direct = FMath::Lerp(B->Direct, A->Direct, R);
    K.Ambient = FMath::Lerp(B->Ambient, A->Ambient, R);
    K.Heading = FMath::Lerp(B->Heading, B->Heading + FMath::FindDeltaAngleDegrees(B->Heading, A->Heading), R);
    K.Pitch = FMath::Lerp(B->Pitch, B->Pitch + FMath::FindDeltaAngleDegrees(B->Pitch, A->Pitch), R);
    K.DirectColor = InterpolateSkyColor(B->DirectColor, A->DirectColor, R);
    K.AmbientColor = InterpolateSkyColor(B->AmbientColor, A->AmbientColor, R);
    K.FogColor = InterpolateSkyColor(B->FogColor, A->FogColor, R);
    K.FogMin = FMath::Lerp(B->FogMin, A->FogMin, R);
    K.FogMax = FMath::Lerp(B->FogMax, A->FogMax, R);
    K.FogSky = FMath::Lerp(B->FogSky, A->FogSky, R);
    K.Rain = FMath::Lerp(B->Rain, A->Rain, R);
    K.WindX = FMath::Lerp(B->WindX, A->WindX, R);
    K.WindY = FMath::Lerp(B->WindY, A->WindY, R);
    return K;
}
EAC2CelestialBody FAC2SkyDay::CelestialBody(uint32 ObjectName) const
{
    // SkyObject::obj_name is a day-local replacement key, not a global enum.
    // Stock names its sun/moon 6/5; Icefire uses 5/7 and inserts other sky
    // objects. Both archives retain these celestial graphics in every day.
    // Classify the base object so authored replacement graphics retain its role.
    const auto* Object=Objects.FindByPredicate([&](const auto& O){return O.Name==ObjectName;});
    if(!Object || Object->Water)return EAC2CelestialBody::None;
    if(Object->Graphic==0x04000023)return EAC2CelestialBody::Sun;
    if(Object->Graphic==0x040000b6)return EAC2CelestialBody::Moon;
    // Unrecognized sky content keeps the native light-key direction.
    return EAC2CelestialBody::None;
}
TArray<FAC2SkyReplacement> FAC2SkyDay::Positions(float T,bool InWater) const
{
    TArray<FAC2SkyReplacement> Result;
    const FAC2SkyKey *B, *A;
    ::Keys(*this, T, B, A);
    for (const auto &O : Objects)
    {
        if (O.Water && O.Water!=uint32(InWater))
            continue;
        FAC2SkyReplacement P;
        P.Name = O.Name;
        P.After = O.After;
        const bool Up =
            O.Begin == O.End || (O.Begin <= T && T < O.End) || (O.End < O.Begin && (T < O.End || T >= O.Begin));
        if (Up)
        {
            P.Graphic = O.Graphic;
            const float R = AC2::SkyTimeRatio(O.Begin, O.End, T);
            P.Heading = AC2::SkySweep(O.BeginHeading, O.EndHeading, R, O.WestToEast);
            P.Pitch = AC2::SkySweep(O.BeginPitch, O.EndPitch, R, O.WestToEast);
        }
        const auto *BR = B ? B->Replacements.FindByPredicate([&](const auto &V) { return V.Name == O.Name; }) : nullptr;
        const auto *AR = A ? A->Replacements.FindByPredicate([&](const auto &V) { return V.Name == O.Name; }) : nullptr;
        if (BR)
        {
            if (BR->Graphic)
                P.Graphic = BR->Graphic;
            P.Heading += BR->Heading;
            P.Pitch += BR->Pitch;
            P.Color = BR->Color;
            if (AR)
                P.Color = InterpolateSkyColor(BR->Color, AR->Color, AC2::SkyTimeRatio(B->Time, A->Time, T));
        }
        if (P.Graphic)
            Result.Add(P);
    }
    return Result;
}
FAC2SkyRenderer::~FAC2SkyRenderer()
{
    ReleaseAC2WaterReflection(World);
    for (auto &Pair : Instances)
        if (IsValid(Pair.Value.Actor))
            Pair.Value.Actor->Destroy();
    // Leaving the world destroys its environment as well as its visible sky layers.
    // Otherwise every character selection/reentry leaves another active set of lights.
    if (IsValid(Sun))
        Sun->Destroy();
    if (IsValid(Ambient))
        Ambient->Destroy();
    if (IsValid(SkyFogLayer))
        SkyFogLayer->Destroy();
    ReleaseAC2Fog(World);
}
bool FAC2SkyRenderer::Open(UWorld *InWorld, const FString &Directory, FString &Error)
{
    World = InWorld;
    FAC2DatArchive Portal;
    TArray<uint8> B;
    if (!Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) || !Portal.Read(0x0f000000, B, Error))
        return false;
    FAC2AssetReader R(MoveTemp(B));
    if (R.Word() != 0x0f000000)
        return false;
    R.Word();
    R.String();
    R.Take(28);
    uint32 SkyId = 0, FogId = 0;
    for (int32 I = 0; I < 11 && R.Good; ++I)
    {
        const int32 N = R.Count(8, 4096);
        for (int32 J = 0; J < N; ++J)
        {
            const uint32 Id = R.Word();
            if (I == 6 && J == 0) SkyId = Id;
            if (I == 8 && J == 0) FogId = Id;
        }
        R.Take(int64(N) * 4);
        R.Align();
    }
    if (!R.End() || !SkyId || !Portal.Read(SkyId, B, Error))
    {
        Error = TEXT("Cannot resolve native region sky");
        return false;
    }
    FAC2AssetReader S(MoveTemp(B));
    S.Word();
    if (S.Word() != SkyId)
        return false;
    S.Take(32);
    const uint32 TimeId = S.Word();
    const int32 ND = S.Count(8, 4096);
    uint32 DayId = 0;
    float Best = -1;
    for (int32 I = 0; I < ND; ++I)
    {
        const uint32 Id = S.Word();
        const float Weight = S.Float();
        TArray<uint8> Bytes;
        FAC2SkyDay Candidate;
        if (Portal.Read(Id, Bytes, Error) && Candidate.Decode(Id, Bytes))
        {
            Days.Add(MoveTemp(Candidate));
            Weights.Add(Weight);
        }
        if (Weight > Best)
        {
            Best = Weight;
            DayId = Id;
        }
    }
    if (!S.End() || !Portal.Read(DayId, B, Error) || !Day.Decode(DayId, B))
    {
        Error = TEXT("Cannot decode native sky day");
        return false;
    }
    if (!Portal.Read(TimeId, B, Error) || !Calendar.Decode(TimeId, B))
    {
        Error = TEXT("Cannot decode native game calendar");
        return false;
    }
    if(FogId){
        FString FogError;
        if(!RegionFog.Load(Portal,FogId,FogError))
            UE_LOG(LogTemp,Warning,TEXT("AC2 region fog: %s"),*FogError);
    }
    const auto Date = Calendar.At(CurrentTime);
    DayFraction = Date.Fraction;
    DayNumber = Date.Seed;
    SetWeather(FAC2GameTime::Weather(Date.Seed, Weights));
    Assets = MakeUnique<FAC2AssetLibrary>();
    if (!Assets->Open(Directory, Error))
        return false;
    WeatherIndex = Days.IndexOfByPredicate([&](const FAC2SkyDay &D) { return D.Id == Day.Id; });
    // LScapeSky composites a uniform source-alpha quad between two sky
    // groups. Place its equivalent behind the world but ahead of DrawSky's
    // 100 km sky meshes, retaining ordinary scene occlusion.
    auto* Sphere = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* SkyAlpha = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2SkyAlpha.M_AC2SkyAlpha"));
    if (!Sphere || !SkyAlpha)
    {
        Error = TEXT("Missing sky fog mesh or material");
        return false;
    }
    SkyFogLayer = World->SpawnActor<AActor>();
    SkyFogLayer->SetActorEnableCollision(false);
    auto* Shell = NewObject<UStaticMeshComponent>(SkyFogLayer, TEXT("SkyFogShell"));
    SkyFogLayer->SetRootComponent(Shell);
    Shell->SetMobility(EComponentMobility::Movable);
    Shell->SetStaticMesh(Sphere);
    Shell->SetCollisionProfileName(TEXT("NoCollision"));
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetCollisionResponseToAllChannels(ECR_Ignore);
    Shell->SetGenerateOverlapEvents(false);
    Shell->SetCanEverAffectNavigation(false);
    Shell->SetCastShadow(false);
    Shell->SetReceivesDecals(false);
    Shell->SetTranslucentSortPriority(-11000);
    SkyFogMaterial = UMaterialInstanceDynamic::Create(SkyAlpha,SkyFogLayer);
    Shell->SetMaterial(0,SkyFogMaterial);
    Shell->RegisterComponent();
    SkyFogLayer->SetActorScale3D(FVector(7000000.0 / Sphere->GetBounds().SphereRadius));
    SkyFogLayer->SetActorHiddenInGame(true);
    Sun = World->SpawnActor<ADirectionalLight>();
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetCastShadows(true);
    CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->SetDynamicShadowDistanceMovableLight(20000);
    CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->DynamicShadowCascades=4;
    Sun->GetLightComponent()->ShadowBias=.3f;
    // VSM supplies the full world-space silhouette. Screen-space contact rays
    // disappear at screen edges and were the only visible shadows under SM5.
    Sun->GetLightComponent()->ContactShadowLength=0;
    Ambient = World->SpawnActor<ASkyLight>();
    auto *AmbientLight = Ambient->GetLightComponent();
    AmbientLight->SetMobility(EComponentMobility::Movable);
    // Native ambient RGB is independent of the translucent sky artwork. Use a
    // neutral upper hemisphere tinted by that key, with only dim ground bounce
    // below. An unoccluded full sphere illuminates foliage from under the terrain.
    AmbientLight->SourceType = SLS_SpecifiedCubemap;
    AmbientLight->bLowerHemisphereIsBlack = true;
    AmbientLight->LowerHemisphereColor = FLinearColor(.08f, .08f, .08f);
    auto *NeutralCube =
        LoadObject<UTextureCube>(nullptr, TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
    if (!NeutralCube)
    {
        Error = TEXT("Missing neutral ambient cubemap");
        return false;
    }
    AmbientLight->SetCubemap(NeutralCube);
    UE_LOG(LogTemp, Display, TEXT("AC2 native sky: region=%08X sky=%08X day=%08X keys=%d objects=%d day_seconds=%.0f"),
           0x0f000000, SkyId, Day.Id, Day.Keys.Num(), Day.Objects.Num(), Calendar.DayLength);
    return true;
}
void FAC2SkyRenderer::SetHour(float Hour)
{
    if (Calendar.DayLength <= 0 || !FMath::IsFinite(Hour)) return;
    CurrentTime = Calendar.AtHour(CurrentTime, Hour);
    DayFraction = Calendar.At(CurrentTime).Fraction;
}
int32 FAC2SkyRenderer::ObjectCount() const
{
    int32 Count = 0;
    for (const auto &Pair : Instances)
        Count += Pair.Value.Rendered && !Pair.Value.Actor->IsHidden() ? 1 : 0;
    return Count;
}
void FAC2SkyRenderer::Update(const FVector &Viewer, float DeltaTime, TOptional<double> ServerTime)
{
    if (!World || !Assets)
        return;
    CurrentViewer=Viewer;
    if (ServerTime.IsSet() && FMath::IsFinite(ServerTime.GetValue()))
        CurrentTime = ServerTime.GetValue();
    else
        CurrentTime += double(DeltaTime) * TimeScale;
    const auto Date = Calendar.At(CurrentTime);
    if (Date.Seed != DayNumber && !Days.IsEmpty())
    {
        DayNumber = Date.Seed;
        SetWeather(FAC2GameTime::Weather(Date.Seed, Weights));
    }
    DayFraction = Date.Fraction;
    for (auto &Pair : Instances)
        Pair.Value.Actor->SetActorHiddenInGame(true);
    const auto Atmosphere = Lighting();
    const float FogAlpha = FMath::Clamp(Atmosphere.FogSky,0.f,1.f);
    SkyFogLayer->SetActorLocation(Viewer);
    SkyFogLayer->SetActorHiddenInGame(FogAlpha <= 0);
    if (FogAlpha > 0)
    {
        auto Tint = FLinearColor::FromSRGBColor(Atmosphere.FogColor.ToFColor(false));
        Tint.A = FogAlpha;
        SkyFogMaterial->SetVectorParameterValue(TEXT("SkyTint"),Tint);
    }
    int32 DrawOrder = 0;
    TOptional<FVector> VisibleSun,VisibleMoon;
    for (const auto &P : Day.Positions(DayFraction,IsSubmerged()))
    {
        const auto Body=Day.CelestialBody(P.Name);
        // Cache each authored variant so cycling days doesn't accumulate MIDs.
        auto &I = Instances.FindOrAdd((uint64(P.Name) << 32) | P.Graphic);
        if (!I.Actor)
        {
            I.Actor = World->SpawnActor<AActor>();
            auto *Root = NewObject<USceneComponent>(I.Actor);
            I.Actor->SetRootComponent(Root);
            I.Actor->AddInstanceComponent(Root);
            Root->RegisterComponent();
            I.Rendered = Assets->DrawSky(P.Graphic, I.Actor, Root);
            if(I.Rendered && Body!=EAC2CelestialBody::None)
                I.CelestialCenter=I.Actor->GetComponentsBoundingBox(true).GetCenter();
            if (!I.Rendered)
            {
                UE_LOG(LogTemp, Warning, TEXT("AC2 sky graphic %08X could not render"), P.Graphic);
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("AC2 native sky graphic %08X loaded"), P.Graphic);
            }
        }
        I.Actor->SetActorHiddenInGame(!P.After && FMath::IsNearlyEqual(FogAlpha,1.f,.0002f));
        I.Actor->SetActorLocation(Viewer);
        I.Actor->SetActorRotation(FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(P.Pitch)) *
                                  FQuat(FVector::UpVector, -FMath::DegreesToRadians(P.Heading)));
        if(Body!=EAC2CelestialBody::None && P.Color.A>KINDA_SMALL_NUMBER && !I.CelestialCenter.IsNearlyZero())
        {
            const FVector ToOrb=I.Actor->GetActorQuat().RotateVector(I.CelestialCenter).GetSafeNormal();
            if(ToOrb.Z>0){if(Body==EAC2CelestialBody::Sun)VisibleSun=ToOrb;else VisibleMoon=ToOrb;}
        }
        TInlineComponentArray<UMeshComponent *> Components(I.Actor);
        // Native LScapeSky draws in object-list order; all mesh bounds follow the
        // same viewer so distance sorting alone is ambiguous for the alpha layers.
        for (auto *C : Components)
        {
            C->SetTranslucentSortPriority((P.After ? -10000 : -16000) + DrawOrder);
            for (int32 J = 0; J < C->GetNumMaterials(); ++J)
                if (auto *M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(J)))
                    M->SetVectorParameterValue(TEXT("SkyTint"), FLinearColor::FromSRGBColor(P.Color.ToFColor(false)));
        }
        ++DrawOrder;
    }
    Assets->UpdateMaterials(World->GetTimeSeconds());
    if(!IsSubmerged())UpdateAC2WaterReflection(World);
    // Retail's light keys and sky meshes have separately authored paths. Keep
    // its colour/intensity keys, but align cast shadows with the visible orb.
    // Ease back to the keyed direction at the horizon to avoid a sunrise snap.
    CelestialDirection.Reset();
    const auto Orb=VisibleSun.IsSet()?VisibleSun:VisibleMoon;
    if(Orb.IsSet())
    {
        const float Horizon=FMath::Clamp(float(Orb.GetValue().Z)/.15f,0.f,1.f);
        const float Blend=Horizon*Horizon*(3-2*Horizon);
        CelestialDirection=FMath::Lerp(Day.Sample(DayFraction).LightDirection(),-Orb.GetValue(),Blend).GetSafeNormal();
    }
    const auto K = Lighting();
    const auto FogKey=InteriorFogOverride.IsSet()?InteriorFogOverride.GetValue():K;
    UpdateAC2Fog(World,{FogKey.FogMin,FogKey.FogMax,FogKey.FogColor,FogKey.FogType==1});
    Sun->SetActorRotation(K.LightDirection().Rotation());
    Sun->GetLightComponent()->SetIntensity(FMath::Max(0.0f, K.Direct) * 4);
    Sun->GetLightComponent()->SetLightColor(FLinearColor::FromSRGBColor(K.DirectColor.ToFColor(false)));
    // Per-room/native ambient is supplied by world materials. A global sky fill
    // would double-light outdoor surfaces and wash out underground rooms.
    Ambient->GetLightComponent()->SetIntensity(0);
    Ambient->GetLightComponent()->SetLightColor(FLinearColor::FromSRGBColor(K.AmbientColor.ToFColor(false)));
}

void FAC2SkyRenderer::SetWeather(int32 Index)
{
    if (Days.IsEmpty())
        return;
    WeatherIndex = ((Index % Days.Num()) + Days.Num()) % Days.Num();
    Day = Days[WeatherIndex];
}
