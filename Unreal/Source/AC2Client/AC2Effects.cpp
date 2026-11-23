#include "AC2Effects.h"
#include "AC2GameMode.h"
#include "AC2WorldStream.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Components/PointLightComponent.h"
#include "AC2NativeMaterial.h"
#endif

namespace
{
    // PSManager owns the scene-wide list; model effects own the live instances.
    TArray<TWeakPtr<FAC2ParticleRepulsor>> ParticleRepulsors;
}

TSharedPtr<FAC2ParticleRepulsor> FAC2ParticleRepulsor::Register(USceneComponent* Root, const FAC2RepulsorFX& Descriptor)
{
    if (!Descriptor.Enabled || !IsValid(Root) || !Root->GetWorld()) return nullptr;
    ParticleRepulsors.RemoveAllSwap([](const auto& Entry) { return !Entry.IsValid(); });
    auto Field = MakeShared<FAC2ParticleRepulsor>();
    Field->Data = Descriptor;
    Field->Owner = Root;
    Field->Created = Root->GetWorld()->GetTimeSeconds();
    Field->Update(0);
    ParticleRepulsors.Add(Field);
    return Field;
}

bool FAC2ParticleRepulsor::Update(float WorldSpeedCm, const FVector* FixedCenter)
{
    auto* Root = Owner.Get();
    if (!Root || !Root->GetWorld() || (Data.Lifetime >= .0002f &&
        Root->GetWorld()->GetTimeSeconds() - Created >= Data.Lifetime)) return false;
    Center = FixedCenter ? *FixedCenter : Root->GetComponentLocation();
    Speed = WorldSpeedCm * .01f;
    SceneFrame = GFrameCounter;
    return true;
}

FVector FAC2ParticleRepulsor::ForceAt(const FVector& Point) const
{
    const FVector Direction = Point - Center;
    const double Distance = Direction.Size();
    const double RangeCm = Data.Range * 100.;
    if (Distance < .02 || Distance >= RangeCm) return FVector::ZeroVector;
    const double Strength = (1. - Distance / RangeCm) * Data.Acceleration * 100.;
    return Direction * ((Data.Attractor ? -Strength : Strength) / Distance);
}

bool FAC2Effects::GroundDecal(const FAC2Decal& D,const FTransform& Frame,AActor* Owner)
{
    if(!World || !Actor || !Assets || !D.Material || !FMath::IsFinite(D.Lifetime) || D.Lifetime<=0 || Decals.Num()>=96)return false;
    const float Now=World->GetTimeSeconds(),Size=D.Size.Value(Now)*100,Radius=D.Radius.Value(Now)*100;
    const float Angle=D.Rotation.Value(Now);
    if(!FMath::IsFinite(Size) || Size<=0 || Size>2000 || !FMath::IsFinite(Radius) || Radius<0 || Radius>2000 || !FMath::IsFinite(Angle))return false;
    FGroundDecal Decal;Decal.Born=Now;Decal.Life=FMath::Min(D.Lifetime,120.f);Decal.Color=D.Color;
    FVector Center=Frame.GetLocation()+D.Origin*100;
    const float Theta=Random.FRand()*2*PI,Offset=Random.FRand()*Radius;
    Center+=FVector(FMath::Cos(Theta)*Offset,FMath::Sin(Theta)*Offset,0);
    const FQuat Rotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(Angle))*Frame.GetRotation();
    TArray<bool> Valid;TArray<int32> Indices;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2GroundDecal),true,Owner);Query.AddIgnoredActor(Actor);
    // Project a bounded grid onto collision geometry. Invalid samples and steep
    // discontinuities are omitted, so a decal never bridges a ledge or floats.
    for(int32 Y=0;Y<=4;++Y)for(int32 X=0;X<=4;++X)
    {
        const FVector P=Center+Rotation.RotateVector(FVector((X/4.f-.5f)*Size,(Y/4.f-.5f)*Size,0));
        FHitResult Hit;const bool Found=World->LineTraceSingleByObjectType(Hit,P+FVector(0,0,Size*.5f+30),P-FVector(0,0,Size+220),FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Hit.ImpactNormal.Z>.25;
        Valid.Add(Found);Decal.Vertices.Add(Found?Hit.ImpactPoint+Hit.ImpactNormal*.15:P);
        Decal.Normals.Add(Found?Hit.ImpactNormal:FVector::UpVector);Decal.UV.Add(FVector2D(X/4.f,Y/4.f));
    }
    auto Triangle=[&](int32 A,int32 B,int32 C){
        if(Valid[A] && Valid[B] && Valid[C] && FMath::Abs(Decal.Vertices[A].Z-Decal.Vertices[B].Z)<Size*.6f && FMath::Abs(Decal.Vertices[A].Z-Decal.Vertices[C].Z)<Size*.6f)
            Indices.Append({A,B,C});
    };
    for(int32 Y=0;Y<4;++Y)for(int32 X=0;X<4;++X){const int32 A=Y*5+X;Triangle(A,A+5,A+1);Triangle(A+1,A+5,A+6);}
    if(Indices.IsEmpty())return false;
    auto* Component=NewObject<UProceduralMeshComponent>(Actor);Actor->AddInstanceComponent(Component);
    Component->SetAbsolute(true,true,true);Component->SetWorldTransform(FTransform::Identity);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCastShadow(false);Component->RegisterComponent();
    TArray<FColor> Colors;Colors.Init(FColor(D.Color.R,D.Color.G,D.Color.B,0),Decal.Vertices.Num());
    Component->CreateMeshSection(0,Decal.Vertices,Indices,Decal.Normals,Decal.UV,Colors,TArray<FProcMeshTangent>(),false);
    Component->SetMaterial(0,Assets->ParticleMaterial(D.Material));Decal.Mesh=Component;Decals.Add(MoveTemp(Decal));return true;
}
void FAC2Effects::UpdateDecals()
{
    for(int32 I=Decals.Num()-1;I>=0;--I)
    {
        auto& D=Decals[I];const float Age=World->GetTimeSeconds()-D.Born;
        if(!D.Mesh.IsValid() || Age>=D.Life)
        {if(D.Mesh.IsValid())D.Mesh->DestroyComponent();Decals.RemoveAtSwap(I);continue;}
        FColor Color=D.Color;Color.A=uint8(FMath::RoundToInt(D.Color.A*FAC2Decal::Opacity(Age,D.Life)));
        TArray<FColor> Colors;Colors.Init(Color,D.Vertices.Num());
        D.Mesh->UpdateMeshSection(0,D.Vertices,D.Normals,D.UV,Colors,TArray<FProcMeshTangent>());
    }
}

FAC2Effects::~FAC2Effects()
{
    if (IsValid(Actor))
        Actor->Destroy();
}
void FAC2Effects::Open(UWorld *W, const FString &Directory, bool InWorldScene)
{
    World = W;
    WorldScene = InWorldScene;
    FString Error;
    Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error);
    Assets = MakeUnique<FAC2AssetLibrary>();
    Assets->Open(Directory, Error);
    Actor = W->SpawnActor<AActor>();
    Mesh = NewObject<UProceduralMeshComponent>(Actor);
    Actor->SetRootComponent(Mesh);
    Actor->AddInstanceComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->RegisterComponent();
    Mesh->SetMaterial(
        0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Particle.M_AC2Particle")));
}
bool FAC2Effects::Start(const FAC2ParticleSource &Source, bool Persistent)
{
    if (!Mesh || !Assets || Systems.Num() >= 64 || Systems.Contains(Source.Key))
        return false;
    auto *Preset = Presets.Find(Source.Descriptor);
    if (!Preset)
    {
        int32 Section = 0;
        if (Presets.Num() >= 64)
        {
            for (auto It = Presets.CreateIterator(); It; ++It)
            {
                bool Used = false;
                for (const auto &S : Systems)
                    Used |= S.Value.Descriptor == It.Key();
                if (!Used)
                {
                    Section = It.Value().Section;
                    It.RemoveCurrent();
                    break;
                }
            }
            if (!Section)
                return false;
        }
        TArray<uint8> B;
        FString Error;
        FAC2ParticlePreset Data;
        if (!Portal.Read(Source.Descriptor, B, Error) || !Data.Decode(Source.Descriptor, B))
            return false;
        if (!Section)
            Section = Sections++;
        else
        {
            Mesh->ClearMeshSection(Section);
            Mesh->SetMaterial(Section, nullptr);
            TArray<UMaterialInterface *> Retained;
            for (const auto &Other : Presets)
                Retained.Add(Mesh->GetMaterial(Other.Value.Section));
            Assets->RetainRuntimeMaterials(Retained);
        }
        Mesh->SetMaterial(Section, Assets->ParticleMaterial(Data.Material));
        const bool ParticleLighting=Assets->ParticleMaterialUsesLighting(Data.Material);
        Preset = &Presets.Add(Source.Descriptor, {MoveTemp(Data), Section, ParticleLighting});
        UE_LOG(LogTemp, Display, TEXT("AC2 native PSDesc %08X: %d emitters, material=%08X"),
               Source.Descriptor, Preset->Data.Emitters.Num(), Preset->Data.Material);
    }
    FSystem S;
    S.Source = Source;
    S.Descriptor = Source.Descriptor;
    S.Persistent = Persistent;
    S.Looping = Preset->Data.ApproximateLifetime()<0;
    S.Random.Initialize(uint32(Source.Key) ^ uint32(Source.Key >> 32) ^ Source.Descriptor);
    S.Emitters.SetNum(Preset->Data.Emitters.Num());
    for (int32 I = 0; I < S.Emitters.Num(); ++I)
    {
        const auto &E = Preset->Data.Emitters[I];
        S.Emitters[I].NextBirth = FMath::Max(0.f, E.Start) + FMath::Max(.001f, E.BirthInterval);
        S.Emitters[I].LastDischarge = Source.Frame.GetLocation();
    }
    Systems.Add(Source.Key, MoveTemp(S));
    return true;
}
bool FAC2Effects::NativeBurst(uint32 Id, const FVector &Position)
{
    FAC2ParticleSource S;
    S.Key = ++PreviewSerial;
    S.Descriptor = Id;
    S.Frame = FTransform(Position);
    return Start(S, false);
}
bool FAC2Effects::MoveSource(uint64 Key,const FTransform& Frame)
{
    auto S=Systems.Find(Key);if(!S)return false;
    if(!Presets[S->Descriptor].Data.WorldSpace && !S->Source.Frame.Equals(Frame))
        for(auto& P:Particles)if(P.System==Key)
        {
            P.P=Frame.TransformPosition(S->Source.Frame.InverseTransformPosition(P.P));
            P.Origin=Frame.TransformPosition(S->Source.Frame.InverseTransformPosition(P.Origin));
            if (P.Streak > 0)
                P.StreakOrigin=Frame.TransformPosition(S->Source.Frame.InverseTransformPosition(P.StreakOrigin));
            P.V=Frame.TransformVector(S->Source.Frame.InverseTransformVector(P.V));
        }
    S->Source.Frame=Frame;return true;
}
void FAC2Effects::StopSource(uint64 Key)
{
    auto* S=Systems.Find(Key);if(!S)return;
    const auto& Preset=Presets[S->Descriptor];
    const bool Shared=S->Source.Shared && !Preset.Data.WorldSpace && !Preset.ParticleLighting;
    if(!Shared && Particles.ContainsByPredicate([Key](const auto& P){return P.System==Key;}))
    {
        // ParticleSystem::Release hands unshared systems to PSManager. Freeze
        // the final frame, stop emission, and let existing particles finish.
        // Give the autonomous tail its own key so immediate recasts/re-entry
        // cannot move it or collide with the previous owner's identity.
        FSystem Tail=MoveTemp(*S);Systems.Remove(Key);
        uint64 Autonomous;
        do {Autonomous=++PreviewSerial;} while(Systems.Contains(Autonomous));
        Tail.Source.Key=Autonomous;Tail.Persistent=false;Tail.Looping=false;Tail.Released=true;
        for(auto& P:Particles)if(P.System==Key)P.System=Autonomous;
        Systems.Add(Autonomous,MoveTemp(Tail));
    }
    else {Systems.Remove(Key);Particles.RemoveAll([Key](const auto& P){return P.System==Key;});}
}
bool FAC2Effects::LoopingSource(uint64 Key) const
{
    const auto* S=Systems.Find(Key);return S && S->Looping;
}
void FAC2Effects::SyncSources(const TArray<FAC2ParticleSource> &Sources)
{
    TSet<uint64> Wanted;
    for (const auto &S : Sources)
        Wanted.Add(S.Key);
    TArray<uint64> Retired;
    for(const auto& Pair:Systems)if(Pair.Value.Persistent && !Wanted.Contains(Pair.Key))Retired.Add(Pair.Key);
    for(uint64 Key:Retired)StopSource(Key);
    for (auto It = SceneryRepulsors.CreateIterator(); It; ++It)
        if (!Wanted.Contains(It.Key())) It.RemoveCurrent();
    for (const auto &S : Sources)
    {
        if (S.Repulsor.Enabled)
        {
            if (WorldScene && !SceneryRepulsors.Contains(S.Key))
            {
                auto Field = FAC2ParticleRepulsor::Register(Mesh, S.Repulsor);
                if (Field) Field->Center = S.Frame.GetLocation();
                SceneryRepulsors.Add(S.Key, MoveTemp(Field));
            }
            continue;
        }
        auto *Existing = Systems.Find(S.Key);
        if (Existing && Existing->Descriptor != S.Descriptor)
        {
            StopSource(S.Key);
            Existing = nullptr;
        }
        if (!Existing)
            Start(S, true);
        else
        {
            MoveSource(S.Key,S.Frame);
            Existing->Source = S;
        }
    }
}
void FAC2Effects::Burst(const FVector &Position, const FLinearColor &Color)
{
    for (int32 I = 0; I < 36 && Particles.Num() < 4096; ++I)
    {
        FParticle P;
        P.P = Position;
        P.V = Random.VRand() * Random.FRandRange(100, 350);
        P.Life = P.MaxLife = Random.FRandRange(.3f, .9f);
        P.Color = Color;
        Particles.Add(P);
    }
}
void FAC2Effects::Discharge(uint64 Key, FSystem &S, int32 Index, float Birth)
{
    const auto &Preset = Presets[S.Descriptor];
    const auto &E = Preset.Data.Emitters[Index];
    const float T = Birth - E.Start;
    FParticle P;
    P.System = Key;
    P.Emitter = Index;
    P.Section = Preset.Section;
    P.Life = P.MaxLife = FMath::Clamp(E.Life.Value(T), .001f, 60.f);
    auto &R = S.Random;
    FVector Shape = FVector::ZeroVector;
    const float Radius = FMath::Clamp(E.Scale.Value(T), -500.f, 500.f) * .5f;
    if (E.Shape == 1)
    {
        Shape = FVector(R.FRandRange(-1, 1), R.FRandRange(-1, 1), 0);
        if (!E.Inclusive)
        {
            if (R.RandRange(0, 1))
                Shape.X = R.RandRange(0, 1) ? 1 : -1;
            else
                Shape.Y = R.RandRange(0, 1) ? 1 : -1;
        }
    }
    if (E.Shape == 2 || E.Shape == 3)
        Shape = FVector(R.FRandRange(-1, 1), R.FRandRange(-1, 1), E.Shape == 3 ? R.FRandRange(-1, 1) : 0).GetSafeNormal();
    if ((E.Shape == 2 || E.Shape == 3) && E.Inclusive)
        Shape *= R.FRand();
    Shape *= Radius;
    FVector Direction = E.Direction.Value(T).GetSafeNormal();
    if (Direction.IsNearlyZero())
        Direction = FVector::YAxisVector;
    if (E.Shape == 4)
        Shape = Direction * R.FRandRange(0, Radius * 2);
    if (E.Exploding)
        Direction = Shape.IsNearlyZero() ? R.VRand() : Shape.GetSafeNormal();
    const float MinSpread = E.MinSpread.Value(T), MaxSpread = E.MaxSpread.Value(T);
    if (MaxSpread - MinSpread > .0002f)
    {
        const float Spread = FMath::DegreesToRadians(R.FRandRange(MinSpread, MaxSpread));
        const FVector Side = FVector::CrossProduct(Direction, FMath::Abs(Direction.Z) < .9 ? FVector::UpVector
                                                                                         : FVector::XAxisVector).GetSafeNormal();
        Direction = FQuat(Direction, R.FRand() * 2 * UE_PI).RotateVector(FQuat(Side, Spread).RotateVector(Direction));
    }
    P.P = S.Source.Frame.TransformPosition((Shape + E.Origin.Value(T)) * 100);
    P.Origin = P.P;
    P.StreakOrigin = P.P;
    P.V = S.Source.Frame.TransformVectorNoScale(Direction) * FMath::Clamp(E.Speed.Value(T), -1000.f, 1000.f) *
          100;
    P.Scale = E.ParticleScale.Value(T) * Preset.Data.RenderScale(S.Source.Frame.GetScale3D().GetAbsMax(),S.Source.ParticleScale);
    auto Configured = [](const FAC2Wave& Wave) { return Wave.Kind != 1 || Wave.Base != 0; };
    P.WorldRotation = Configured(E.WorldRotation.X) || Configured(E.WorldRotation.Y) || Configured(E.WorldRotation.Z);
    P.Rotation = (P.WorldRotation ? E.WorldRotation : E.Rotation).Value(T);
    P.Spin = E.Spin.Value(T);
    P.Streak = E.Streak.Value(T);
    P.Point = int32(S.Emitters[Index].Born);
    Particles.Add(P);
}
void FAC2Effects::Step(float Dt, const FVector &Wind)
{
    TArray<TSharedPtr<FAC2ParticleRepulsor>, TInlineAllocator<8>> ActiveRepulsors;
    bool GatheredRepulsors = false;
    for (auto &Pair : Systems)
    {
        auto &S = Pair.Value;
        S.Age += Dt;
        if(S.Released)continue;
        const auto &D = Presets[S.Descriptor].Data;
        int32 SystemAlive = 0;
        TArray<int32, TInlineAllocator<64>> Alive;
        Alive.Init(0, D.Emitters.Num());
        for (const auto &P : Particles)
            if (P.System == Pair.Key)
            {
                ++SystemAlive;
                ++Alive[P.Emitter];
            }
        for (int32 I = 0; I < D.Emitters.Num(); ++I)
        {
            const auto &E = D.Emitters[I];
            auto &State = S.Emitters[I];
            if (!E.Active || !E.Blast || S.Age < E.Start ||
                (E.Duration > 0 && S.Age > E.Start + E.Duration))
                continue;
            // ParticleEmitter::EmitDistanceParticles (7160 00598970) is
            // independent of the timed birth rate. Native missile trails have
            // a zero birth interval and emit each 0.25/0.333m travelled.
            const float Spacing=E.EmissionDistance*100;
            if(FMath::IsFinite(Spacing) && Spacing>SMALL_NUMBER)
            {
                const FTransform EndFrame=S.Source.Frame;
                const FVector Delta=EndFrame.GetLocation()-State.LastDischarge;
                const float Distance=Delta.Size();
                const int32 Steps=FMath::FloorToInt(FMath::Min(Distance/Spacing,128.f));
                const FVector Direction=Delta.GetSafeNormal();
                for(int32 Step=0;Step<Steps;++Step)
                {
                    State.LastDischarge+=Direction*Spacing;
                    int32 N=FMath::Min3(4096-Particles.Num(),int32(FMath::Min(D.Maximum,1024u))-SystemAlive,int32(FMath::Min(E.Maximum,1024u))-Alive[I]);
                    N=FMath::Min(N,int32(FMath::Min(E.Blast,128u)));
                    if(E.Limit)N=FMath::Min(N,int32(E.Limit>State.Born?E.Limit-State.Born:0));
                    if(N<=0){State.LastDischarge=EndFrame.GetLocation();break;}
                    S.Source.Frame.SetLocation(State.LastDischarge);
                    for(int32 J=0;J<N;++J){Discharge(Pair.Key,S,I,S.Age);++State.Born;}
                    Alive[I]+=N;SystemAlive+=N;
                }
                // Drop excessive travel (teleports), instead of replaying it on later frames.
                if(Distance>Spacing*128)State.LastDischarge=EndFrame.GetLocation();
                S.Source.Frame=EndFrame;
            }
            if(E.BirthInterval<=0)continue;
            const float Interval = FMath::Max(.001f, E.BirthInterval);
            int32 Budget = 128;
            while (State.NextBirth <= S.Age && Budget-- > 0)
            {
                int32 Room = FMath::Min(4096 - Particles.Num(),
                                        FMath::Min(int32(FMath::Min(D.Maximum, 1024u)) - SystemAlive,
                                                   int32(FMath::Min(E.Maximum, 1024u)) - Alive[I]));
                int32 N = FMath::Min(Room, int32(FMath::Min(E.Blast, 128u)));
                if (E.Limit)
                    N = FMath::Min(N, int32(E.Limit > State.Born ? E.Limit - State.Born : 0));
                if (N <= 0)
                {
                    State.NextBirth = S.Age + Interval;
                    break;
                }
                for (int32 J = 0; J < N; ++J)
                {
                    Discharge(Pair.Key, S, I, State.NextBirth);
                    ++State.Born;
                }
                Alive[I] += N;
                SystemAlive += N;
                State.NextBirth += Interval;
            }
            if (Budget <= 0)
                State.NextBirth = S.Age + Interval;
        }
    }
    for (int32 I = Particles.Num() - 1; I >= 0; --I)
    {
        auto &P = Particles[I];
        P.Life -= Dt;
        if (P.Life <= 0)
        {
            if (P.Rain && P.Impact) // A short surface splash reuses the streak's slot.
            {
                P.Rain = false;
                P.Splash = true;
                P.Life = P.MaxLife = .18f;
                P.V = FVector::ZeroVector;
                P.Width = 4;
                P.Height = 4;
            }
            else
            {
                Particles.RemoveAtSwap(I);
                continue;
            }
        }
        if (!P.System)
        {
            P.P += P.V * Dt;
            if (!P.Rain && !P.Splash)
                P.V.Z -= 250 * Dt;
            continue;
        }
        auto *S = Systems.Find(P.System);
        if (!S)
        {
            Particles.RemoveAtSwap(I);
            continue;
        }
        const auto& Preset = Presets[S->Descriptor].Data;
        const auto &E = Preset.Emitters[P.Emitter];
        const float Age = P.MaxLife - P.Life;
        float Mass;
        E.Sample(Age, P.Scale, P.Width, P.Height, P.Color, Mass);
        uint32 Forces;
        const auto *K = E.ControllerAt(Age, Forces);
        if (K != P.Controller)
        {
            P.Controller = K;
            P.Origin = P.P;
            P.RepulseRemainder = 0;
        }
        const float Relative = K ? FMath::Max(0.f, Age - K->Time) : Age;
        if (K && K->Controller == 2)
        {
            P.P = P.Origin + S->Source.Frame.TransformVector(K->Motion.Value(Relative) * 100);
            if (P.Streak > 0)
                P.StreakOrigin = P.Origin + S->Source.Frame.TransformVector(K->Motion.Value(FMath::Max(0.f, Relative - P.Streak)) * 100);
        }
        else if (K && K->Controller == 3)
        {
            if (WorldScene && !GatheredRepulsors)
            {
                for (const auto& Entry : ParticleRepulsors)
                {
                    auto Field = Entry.Pin();
                    if (Field && Field->Owner.IsValid() && Field->Owner->GetWorld() == World &&
                        GFrameCounter - Field->SceneFrame < 2 &&
                        (Field->Data.MinimumVelocity <= .0002f || Field->Speed >= Field->Data.MinimumVelocity) &&
                        (Field->Data.Lifetime < .0002f || World->GetTimeSeconds() - Field->Created < Field->Data.Lifetime))
                        ActiveRepulsors.Add(MoveTemp(Field));
                }
                GatheredRepulsors = true;
            }
            // ParticleEmitter::DoRepulse integrates and damps velocity once per
            // authored physics step. The spring displaces position independently.
            const double Quantum = Preset.Step;
            P.RepulseRemainder = FMath::Min(P.RepulseRemainder + Dt, .25);
            while (Quantum >= .0002 && P.RepulseRemainder >= Quantum)
            {
                P.RepulseRemainder -= Quantum;
                FVector FieldForce = FVector::ZeroVector;
                for (const auto& Field : ActiveRepulsors) FieldForce += Field->ForceAt(P.P);
                for (int32 Axis = 0; Axis < 3; ++Axis) if (E.Constrain[Axis]) FieldForce[Axis] = 0;
                if (Forces & 1) FieldForce.Z -= 980 * Mass;
                if (Forces & 2) FieldForce += Wind * (100 * Mass);
                P.V += FieldForce;
                P.P += (P.V + (P.Origin - P.P) * E.Snap.Value(Age)) * Quantum;
                P.V *= .95;
            }
        }
        else if (K && (K->Controller == 4 || K->Controller == 5) && !K->Points.IsEmpty())
        {
            if (K->Controller == 4)
                P.P = P.Origin + S->Source.Frame.TransformVector(K->Points[P.Point % K->Points.Num()] * 100);
            else
            {
                FVector Goal =
                    P.Origin + S->Source.Frame.TransformVector(K->Points[P.Point % K->Points.Num()] * 100);
                float Distance = P.V.Size() * Dt;
                if (FVector::DistSquared(P.P, Goal) <= Distance * Distance)
                {
                    P.P = Goal;
                    ++P.Point;
                }
                else
                    P.P += (Goal - P.P).GetSafeNormal() * Distance;
            }
            P.StreakOrigin = P.P;
        }
        else
        {
            FVector Acceleration = FVector::ZeroVector;
            if (Forces & 1)
                Acceleration.Z -= 980;
            if (Forces & 2)
                Acceleration += Wind * 100;
            P.P += P.V * Dt + Acceleration * Mass * (.5f * Dt * Dt);
            P.V += Acceleration * Mass * Dt;
            if (P.Streak > 0)
            {
                const float Lag = FMath::Min(P.Streak, Relative);
                P.StreakOrigin = P.P - P.V * Lag + Acceleration * Mass * (.5f * Lag * Lag);
            }
        }
    }
    for (auto It = Systems.CreateIterator(); It; ++It)
        if (!It.Value().Persistent)
        {
            // GetReadyToDie checks actual emissions, not the estimated lifetime.
            // Capacity pressure and distance-driven emission can delay a finite
            // count well beyond Limit / Blast * BirthInterval.
            const auto& S=It.Value();const auto& D=Presets[S.Descriptor].Data;
            bool Emitting=false;
            for(int32 I=0;I<D.Emitters.Num();++I)
            {
                const auto& E=D.Emitters[I];
                Emitting|=!S.Released && E.Active && E.Blast &&
                    (E.Duration<=0 || S.Age<=E.Start+E.Duration) &&
                    (!E.Limit || S.Emitters[I].Born<E.Limit);
            }
            if(Emitting)continue;
            bool Alive = false;
            for (const auto &P : Particles)
                Alive |= P.System == It.Key();
            if (!Alive)
                It.RemoveCurrent();
        }
}
// FillVerts::GetStreakedVertexPositions selects the leading edge by screen octant.
// Each end retains its own depth, including when the trail approaches the camera.
static bool AC2StreakQuad(const FVector& Head, const FVector& Tail, const FVector& Right, const FVector& Up,
                          float Width, float Height, FVector (&Corners)[4])
{
    FVector2D Direction((Head - Tail).Dot(Right), (Head - Tail).Dot(Up));
    if (!Direction.Normalize()) return false;
    const FVector X = Right * Width, Z = Up * Height;
    FVector HeadOffset, TailOffset, Across;
    const double Axis = FMath::Abs(Direction.X);
    if (Axis < .3826834)
    {
        HeadOffset = Direction.Y > 0 ? Z : -Z;
        TailOffset = -HeadOffset;
        Across = X;
    }
    else if (Axis > .9238795)
    {
        HeadOffset = Direction.X > 0 ? X : -X;
        TailOffset = -HeadOffset;
        Across = Z;
    }
    else
    {
        HeadOffset = TailOffset = FVector::ZeroVector;
        Across = Direction.X * Direction.Y > 0 ? X - Z : X + Z;
    }
    // Match the quad's existing BL, BR, TL, TR storage and corresponding UVs.
    Corners[0] = Head + HeadOffset - Across;
    Corners[1] = Tail + TailOffset - Across;
    Corners[2] = Head + HeadOffset + Across;
    Corners[3] = Tail + TailOffset + Across;
    return true;
}
void FAC2Effects::Update(const FVector &Camera, const FVector &Right, const FVector &Up, float Dt, float Rain,
                         const FVector &Wind,bool Sheltered,const FAC2PreviewLighting* PreviewLighting,
                         const FAC2WorldStream* Terrain)
{
    if (!Mesh)
        return;
    UpdateDecals();
    for (auto& Pair : SceneryRepulsors)
        if (Pair.Value && !Pair.Value->Update(0, &Pair.Value->Center))
            Pair.Value.Reset(); // A finite startup field stays spent while this source is streamed.
    Dt = FMath::Clamp(Dt, 0.f, .15f);
    if(Sheltered)
    {
        Emit=0;
        Particles.RemoveAllSwap([](const auto& P){return P.Rain || P.Splash;});
    }
    float Remaining = Dt;
    while (Remaining > SMALL_NUMBER)
    {
        float Quantum = FMath::Min(Remaining, 1.f / 60);
        Step(Quantum, Wind);
        Remaining -= Quantum;
    }
    if (!Sheltered && Rain>.4f)
    {
        Emit += FMath::Clamp(Rain, 0.f, 1.f) * 500 * Dt;
        int32 N = FMath::Min(int32(Emit), 32);
        Emit -= N;
        for (int32 I = 0; I < N && Particles.Num() < 4096; ++I)
        {
            FParticle P;
            P.P = Camera + FVector(Random.FRandRange(-1200, 1200), Random.FRandRange(-1200, 1200),
                                   Random.FRandRange(500, 1300));
            P.V = Wind * 100 + FVector(0, 0, -1500);
            P.Rain = true;
            P.Width = 1.2f;
            P.Height = 32;
            P.Color = FLinearColor(.5f, .65f, .8f, .4f);
            P.Life = 1.6f;
            FHitResult Hit;
            if (World->LineTraceSingleByObjectType(Hit, P.P, P.P + P.V * P.Life,
                                                   FCollisionObjectQueryParams(ECC_WorldStatic)))
            {
                P.Life *= Hit.Time;
                P.Impact = true;
            }
            P.MaxLife = P.Life;
            // Stock rain stops at the first surface. Water is visual mesh
            // geometry, so the terrain trace alone lets drops cross it.
            double Height=0;
            if(Terrain && Terrain->WaterAt(P.P+P.V*P.Life,Height))
            {
                const double T=(Height-P.P.Z)/P.V.Z;
                double ContactHeight=0;
                if(T<0)continue;
                if(T<P.Life && Terrain->WaterAt(P.P+P.V*T,ContactHeight) && FMath::IsNearlyEqual(Height,ContactHeight))
                {
                    P.MaxLife=P.Life=float(T);
                    P.Impact=true;
                }
            }
            Particles.Add(P);
        }
    }
    Actor->SetActorLocation(Camera);
    // Resolve a room's light list once per source, not once per particle. A
    // local emitter's particles are already transformed into world space.
    TMap<uint64,FAC2PreviewLighting> Lighting;
    const auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    for(const auto& Entry:Systems)if(Presets[Entry.Value.Descriptor].ParticleLighting)
    {
        if(PreviewLighting)Lighting.Add(Entry.Key,*PreviewLighting);
        else if(Mode)Lighting.Add(Entry.Key,Mode->ParticleLightingAt(Entry.Value.Source.Frame.GetLocation()));
    }
    for (int32 Section = 0; Section < Sections; ++Section)
    {
        TArray<const FParticle *> Sorted;
        for (const auto &P : Particles)
            if (P.Section == Section)
                Sorted.Add(&P);
        Sorted.Sort([&](const FParticle &A, const FParticle &B) {
            return FVector::DistSquared(A.P, Camera) > FVector::DistSquared(B.P, Camera);
        });
        TArray<FVector> V, Normals;
        TArray<FVector2D> UV;
        TArray<int32> Indices;
        TArray<FColor> Colors;
        for (const auto *Pointer : Sorted)
        {
            const auto &P = *Pointer;
            auto Color = P.Color;
            if(const auto* Scene=Lighting.Find(P.System))Color=AC2LitParticleColor(Color,P.P,*Scene);
            FVector Side = Right, Vertical = Up;
            bool Triangle = false;
            if (P.System)
            {
                const auto &S = Systems[P.System];
                const auto &D = Presets[S.Descriptor].Data;
                const auto &E = D.Emitters[P.Emitter];
                if (E.ParticleShape != 3 && E.ParticleShape != 4) continue;
                Triangle = E.ParticleShape == 3;
                const float Distance = FVector::Distance(Camera, S.Source.Frame.GetLocation()) / 100;
                if (D.FadeStop > D.FadeStart)
                    Color.A *=
                        1 - FMath::Clamp((Distance - D.FadeStart) / (D.FadeStop - D.FadeStart), 0.f, 1.f);
                if (!E.Keys.IsEmpty()) Color.A *= E.Opacity(S.Age, D.FastForward);
                const FVector Angles = P.Rotation + P.Spin * (P.MaxLife - P.Life);
                if (P.WorldRotation || !Angles.IsNearlyZero())
                {
                    // Matrix4::RotateAngles applied to the quad's native X (right) and Z (up) axes.
                    double SX, CX, SY, CY, SZ, CZ;
                    FMath::SinCos(&SX, &CX, -Angles.X);
                    FMath::SinCos(&SY, &CY, -Angles.Y);
                    FMath::SinCos(&SZ, &CZ, -Angles.Z);
                    Side = FVector(CZ * CY, SY, -SZ * CY);
                    Vertical = FVector(SZ * CX + CZ * SY * SX, -CY * SX, CZ * CX - SY * SZ * SX);
                    if (!P.WorldRotation)
                    {
                        const FVector Forward = FVector::CrossProduct(Right, Up);
                        Side = Right * Side.X + Forward * Side.Y + Up * Side.Z;
                        Vertical = Right * Vertical.X + Forward * Vertical.Y + Up * Vertical.Z;
                    }
                }
            }
            else
                Color.A *= FMath::Min(1.f, P.Life / .2f);
            if (P.Rain)
                Vertical = -P.V.GetSafeNormal();
            if (P.Splash)
            {
                Side = FVector::XAxisVector;
                Vertical = FVector::YAxisVector;
            }
            Side *= P.Width;
            Vertical *= P.Height;
            if (Color.A <= .001f)
                continue;
            int32 I = V.Num();
            const FVector Center = P.P - Camera;
            FVector Corners[4] = { Center-Side-Vertical, Center+Side-Vertical, Center-Side+Vertical, Center+Side+Vertical };
            const bool Streaked = P.Streak > 0 && AC2StreakQuad(Center, P.StreakOrigin-Camera, Right, Up, P.Width, P.Height, Corners);
            const bool Flip = Streaked && FVector::CrossProduct(Corners[2]-Corners[0],Corners[1]-Corners[0]).Dot(FVector::CrossProduct(Up,Right)) < 0;
            V.Append({Corners[0], Corners[1], Corners[2]});
            UV.Append({FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0)});
            Indices.Append({I, I + (Flip ? 1 : 2), I + (Flip ? 2 : 1)});
            if (!Triangle)
            {
                V.Add(Corners[3]);
                UV.Add(FVector2D(1, 0));
                Indices.Append({I + 1, I + (Flip ? 3 : 2), I + (Flip ? 2 : 3)});
            }
            for (int32 J = 0; J < (Triangle ? 3 : 4); ++J)
            {
                Normals.Add(FVector::UpVector);
                Colors.Add(Color.ToFColor(false));
            }
        }
        if (V.IsEmpty())
            Mesh->ClearMeshSection(Section);
        else
            Mesh->CreateMeshSection(Section, V, Indices, Normals, UV, Colors, TArray<FProcMeshTangent>(),
                                    false);
    }
    Assets->UpdateMaterials(World->GetTimeSeconds());
}
int32 FAC2Effects::NativeCount() const
{
    int32 N = 0;
    for (const auto &P : Particles)
        N += P.System != 0;
    return N;
}
int32 FAC2Effects::SourceCount() const
{
    int32 N = 0;
    for (const auto &S : Systems)
        N += S.Value.Persistent;
    return N;
}

int32 FAC2Effects::RainCount() const
{
    int32 Count = 0;
    for (const auto &P : Particles)
        Count += P.Rain;
    return Count;
}
int32 FAC2Effects::ActiveEmitters(uint32 Descriptor) const
{
    TSet<int32> Indices;
    for (const auto &P : Particles)
        if (const auto *S = Systems.Find(P.System))
            if (S->Descriptor == Descriptor)
                Indices.Add(P.Emitter);
    return Indices.Num();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ParticleRepulsorTest,"AC2.Data.ParticleRepulsors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ParticleRepulsorTest::RunTest(const FString&)
{
    // enable, 8 m/s, 2 m radius, 2 second lifetime, attractor flag, 1 m/s minimum
    const uint32 Packed[] = {1, 0x41000000, 0x40000000, 0x40000000, 0, 0x3f800000};
    FAC2AssetReader Reader(MakeArrayView(reinterpret_cast<const uint8*>(Packed), sizeof(Packed)));
    auto D = FAC2RepulsorFX::Read(Reader);
    TestTrue(TEXT("Native field payload decodes fully"), Reader.End() && D.Enabled && !D.Attractor &&
        D.Acceleration == 8 && D.Range == 2 && D.Lifetime == 2 && D.MinimumVelocity == 1);
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    {
        auto* Actor = World->SpawnActor<AActor>();
        auto* Root = NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        auto Field = FAC2ParticleRepulsor::Register(Root, D);
        FAC2Effects FX;
        FX.Open(World, AC2::ResolveDatDirectory(TEXT("")));
        FAC2ParticlePreset Data;
        Data.Step = .0625f;
        Data.Maximum = 4;
        auto& E = Data.Emitters.AddDefaulted_GetRef();
        E.Maximum = 4;
        E.Snap.Base = .5f;
        auto& Key = E.Keys.AddDefaulted_GetRef();
        Key.Flags = 128; Key.Controller = 3;
        FX.Presets.Add(1, {Data, 0});
        FAC2ParticleSource Source; Source.Key = 1; Source.Descriptor = 1;
        FX.NativeBurst(Source);
        FAC2Effects::FParticle P;
        P.System = 1; P.Life = P.MaxLife = 10; P.P = FVector(100,0,0);
        auto& Particle = FX.Particles.Add_GetRef(P);
        auto Reset = [&] { Particle = P; };
        FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Stationary owner is below minimum speed"), Particle.P.Equals(P.P));
        Root->SetWorldLocation(FVector(1,0,0));
        Field->Update(100);
        TestTrue(TEXT("Field center follows the owner root"), Field->Center.Equals(Root->GetComponentLocation()));
        Root->SetWorldLocation(FVector::ZeroVector); Field->Update(100);
        FX.Step(Data.Step * .5f, FVector::ZeroVector);
        TestTrue(TEXT("Substep remainder is retained"), Particle.P.Equals(P.P));
        FX.Step(Data.Step * .5f, FVector::ZeroVector);
        TestTrue(TEXT("Separate owner repels with linear radial falloff and authored damping"),
            Particle.P.Equals(FVector(125,0,0)) && Particle.V.Equals(FVector(380,0,0)));
        FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Snap restores toward initial position independently of velocity"),
            Particle.P.Equals(FVector(166.71875,0,0)) && Particle.V.Equals(FVector(646,0,0)));
        TestTrue(TEXT("At center and at radius no force is applied"),
            Field->ForceAt(FVector::ZeroVector).IsZero() && Field->ForceAt(FVector(200,0,0)).IsZero());
        Field->Data.Attractor = true;
        Reset(); FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Attractor reverses the field"), Particle.P.Equals(FVector(75,0,0)));
        FX.Presets[1].Data.Emitters[0].Constrain[0] = true;
        Reset(); FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Constrained axis receives no field acceleration"), Particle.P.Equals(P.P));
        FX.Presets[1].Data.Emitters[0].Constrain[0] = false;
        Field->Created -= D.Lifetime;
        Reset(); FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Expired field is inactive before owner cleanup"), Particle.P.Equals(P.P) && !Field->Update(100));
        Field.Reset();
        FAC2ParticleSource Fixed; Fixed.Key = 7; Fixed.Repulsor = D;
        Fixed.Repulsor.Lifetime = 0; Fixed.Repulsor.MinimumVelocity = 0;
        Fixed.Frame = FTransform(FVector(150,0,0));
        FX.SyncSources({Fixed});
        Reset(); FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Stationary scenery field uses placement center and indefinite life"), Particle.P.Equals(FVector(62.5,0,0)));
        FX.SyncSources({});
        Reset(); FX.Step(Data.Step, FVector::ZeroVector);
        TestTrue(TEXT("Unstreaming the field releases it"), Particle.P.Equals(P.P));
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ParticleLightingTest,"AC2.Data.ParticleLighting",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ParticleLightingTest::RunTest(const FString&)
{
    FAC2PreviewLighting Scene;Scene.Ambient=FLinearColor(.1f,.2f,.3f);
    FAC2PreviewLight L;L.Position=FVector(100,0,0);L.Range=100;L.Intensity=.55f;
    L.Diffuse=FLinearColor(1,.5f,.25f);Scene.Lights.Add(L);
    const auto Color=AC2LitParticleColor(FLinearColor(.8f,.4f,.2f,.5f),FVector::ZeroVector,Scene);
    TestTrue(TEXT("Native attenuation and RGB multiply without a surface normal"),Color.Equals(FLinearColor(.12f,.09f,.0625f,.5f),.0001f));
    Scene.Lights[0].Directional=true;Scene.Lights[0].Direction=FVector::DownVector;
    TestTrue(TEXT("Directional light has no point attenuation"),
        AC2LitParticleColor(FLinearColor::White,FVector(500,0,0),Scene).Equals(FLinearColor(.65f,.475f,.4375f,1),.0001f));
    Scene.Lights.Reset();Scene.Ambient=FLinearColor::Black;
    L.Directional=true;L.Diffuse=FLinearColor::White;L.Intensity=.1f;
    for(int32 I=0;I<6;++I)Scene.Lights.Add(L);
    TestTrue(TEXT("Native five-light limit"),AC2LitParticleColor(FLinearColor::White,FVector::ZeroVector,Scene).Equals(FLinearColor(.5f,.5f,.5f,1),.0001f));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    {
        FAC2Effects FX;FX.Open(World,AC2::ResolveDatDirectory(TEXT("")));
        FAC2ParticlePreset Data;Data.Maximum=16;Data.Emitters.AddDefaulted();
        FX.Presets.Add(1,{Data,0,true});
        FAC2ParticleSource Source;Source.Key=1;Source.Descriptor=1;
        TestTrue(TEXT("Particle lighting source starts"),FX.NativeBurst(Source));
        FAC2Effects::FParticle P;P.System=1;P.Color=FLinearColor(.8f,.4f,.2f,.5f);P.Life=P.MaxLife=10;
        FX.Particles.Add(P);
        auto Render=[&](){FX.Update(FVector(0,-100,0),FVector::XAxisVector,FVector::UpVector,0,0,FVector::ZeroVector,true,&Scene);};
        Render();auto* Section=FX.Mesh->GetProcMeshSection(0);
        if(TestNotNull(TEXT("Lit particle vertices reach the renderer"),Section) && !Section->ProcVertexBuffer.IsEmpty())
            TestEqual(TEXT("Vertex RGB carries native light; alpha remains unchanged"),Section->ProcVertexBuffer[0].Color,FLinearColor(.4f,.2f,.1f,.5f).ToFColor(false));
        FX.Presets[1].ParticleLighting=false;Render();Section=FX.Mesh->GetProcMeshSection(0);
        if(TestNotNull(TEXT("Unlit particle vertices"),Section) && !Section->ProcVertexBuffer.IsEmpty())
            TestEqual(TEXT("Unlit authored color is preserved"),Section->ProcVertexBuffer[0].Color,P.Color.ToFColor(false));
        auto& E = FX.Presets[1].Data.Emitters[0];
        E.Keys.AddDefaulted();
        E.FadeIn = 2;
        FX.Systems[1].Age = 1;
        FX.Particles[0].Spin.Z = UE_HALF_PI;
        FX.Particles[0].Life -= 1;
        E.Constrain[0] = true;
        Render(); Section=FX.Mesh->GetProcMeshSection(0);
        if (TestTrue(TEXT("Oriented particle submitted"), Section && Section->ProcVertexBuffer.Num() == 4))
        {
            const auto& V = Section->ProcVertexBuffer;
            TestTrue(TEXT("One second of Z spin turns right edge upwards by a quarter turn"),
                (V[1].Position - V[0].Position).Equals(FVector(0, 0, 2 * P.Width), .001));
            TestEqual(TEXT("Emitter opacity multiplies keyed alpha"), V[0].Color.A, FLinearColor(0,0,0,.25f).ToFColor(false).A);
        }
        FX.Particles[0].Spin = FVector::ZeroVector;
        FX.Particles[0].Rotation.Y = UE_HALF_PI;
        FX.Particles[0].WorldRotation = true;
        FX.Update(FVector(0,-100,0),FVector::YAxisVector,FVector::UpVector,0,0,FVector::ZeroVector,true,&Scene);
        Section=FX.Mesh->GetProcMeshSection(0);
        if (TestTrue(TEXT("World-oriented particle submitted"), Section && Section->ProcVertexBuffer.Num() == 4))
            TestTrue(TEXT("World rotation uses its authored Y angle independently of the camera axes"),
                (Section->ProcVertexBuffer[1].Position - Section->ProcVertexBuffer[0].Position).Equals(FVector(0,-2*P.Width,0),.001));
        auto& Trail = FX.Particles[0];
        Trail.WorldRotation = false;
        Trail.Rotation = FVector::ZeroVector;
        Trail.Streak = .5f;
        Trail.V = FVector(100, 60, 0);
        FX.Step(.25f, FVector::ZeroVector);
        TestTrue(TEXT("Physics controller evaluates the lagging endpoint at streak time"),
            Trail.StreakOrigin.Equals(Trail.P - Trail.V * .5f,.001));
        Render(); Section=FX.Mesh->GetProcMeshSection(0);
        if (TestTrue(TEXT("Streak quad reaches renderer"), Section && Section->ProcVertexBuffer.Num() == 4))
        {
            const auto& V = Section->ProcVertexBuffer;
            TestTrue(TEXT("Streak tail uses its own depth"), FMath::IsNearlyEqual(V[0].Position.Y - V[1].Position.Y,30.0,.001));
            TestTrue(TEXT("Streak extends behind the moving head"),
                FMath::IsNearlyEqual(V[0].Position.X - V[1].Position.X, 50.0 + 2 * Trail.Width,.001));
        }
        auto* Light=NewObject<UPointLightComponent>(FX.RenderActor());Light->RegisterComponent();
        Light->SetIntensity(2);Light->SetAttenuationRadius(500);Light->SetLightColor(FLinearColor(.2f,.4f,.6f));
        RegisterAC2EffectLight(Light);Scene.Lights.Reset();AppendAC2EffectLights(World,FVector::ZeroVector,Scene);
        if(TestEqual(TEXT("Live FX light joins particle lighting"),Scene.Lights.Num(),1))
            TestEqual(TEXT("Native intensity is restored from UE mapping"),Scene.Lights[0].Intensity,.5f);
        Light->DestroyComponent();Scene.Lights.Reset();AppendAC2EffectLights(World,FVector::ZeroVector,Scene);
        TestEqual(TEXT("Expired FX light cannot remain in particle lighting"),Scene.Lights.Num(),0);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ParticleLifecycleTest,"AC2.Data.ParticleLifetimes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ParticleLifecycleTest::RunTest(const FString&)
{
    FAC2ParticlePreset Finite;Finite.Maximum=16;
    FAC2ParticleEmitter E;E.Maximum=16;E.Active=true;E.Blast=1;E.Limit=3;
    E.BirthInterval=1;E.Start=2;E.Life.Base=.5f;E.ParticleScale.Base=1;
    Finite.Emitters.Add(E);
    TestEqual(TEXT("Native count, delay and particle tail lifetime"),Finite.ApproximateLifetime(),5.5f);
    auto Loop=Finite;Loop.Emitters[0].Limit=0;Loop.Emitters[0].Start=0;
    TestEqual(TEXT("Unlimited emitter is a loop"),Loop.ApproximateLifetime(),-1.f);
    auto Long=Loop;Long.Emitters[0].Duration=30;
    TestEqual(TEXT("Authored duration exceeds old fifteen-second cutoff"),Long.ApproximateLifetime(),30.5f);
    auto Physics=Finite;Physics.PhysicsDuration=1;
    TestEqual(TEXT("Native physics-controlled lifetime"),Physics.ApproximateLifetime(),-1.f);
    auto Wave=Finite;Wave.Emitters[0].Life.Kind=2;Wave.Emitters[0].Life.Amplitude=-2;
    Wave.FastForward=1;
    TestEqual(TEXT("Native lifespan amplitude and fast-forward estimate"),Wave.ApproximateLifetime(),6.5f);
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    {
        FAC2Effects FX;FX.Open(World,AC2::ResolveDatDirectory(TEXT("")));
        FX.Presets.Add(1,{Finite,0});FX.Presets.Add(2,{Loop,0});FX.Presets.Add(3,{Long,0});
        for(uint32 I=1;I<=3;++I)
        {FAC2ParticleSource S;S.Key=I;S.Descriptor=I;S.Shared=true;TestTrue(TEXT("Start lifecycle fixture"),FX.NativeBurst(S));}
        TestTrue(TEXT("Loop classification reaches the running source"),FX.LoopingSource(2));
        TestFalse(TEXT("Finite source is not a loop"),FX.LoopingSource(1));
        for(int32 I=0;I<41;++I)FX.Step(.1f,FVector::ZeroVector);
        TestEqual(TEXT("Delayed count-limited emitter continues past one second"),FX.Systems[1].Emitters[0].Born,2u);
        for(int32 I=41;I<200;++I)FX.Step(.1f,FVector::ZeroVector);
        TestFalse(TEXT("Finished count-limited system releases its slot"),FX.Systems.Contains(1));
        TestTrue(TEXT("Long system emits after fifteen seconds"),FX.Systems.Contains(3) && FX.Systems[3].Emitters[0].Born>=19);
        for(int32 I=200;I<330;++I)FX.Step(.1f,FVector::ZeroVector);
        TestFalse(TEXT("Finite long system eventually releases its slot"),FX.Systems.Contains(3));
        TestTrue(TEXT("Native loop keeps emitting"),FX.Systems.Contains(2) && FX.Systems[2].Emitters[0].Born>=32);
        FX.SyncSources({});
        TestTrue(TEXT("Scenery reconciliation cannot erase a dynamic looping effect"),FX.Systems.Contains(2));
        FX.StopSource(2);
        TestEqual(TEXT("Explicit stop releases loop and particles"),FX.SystemCount()+FX.Count(),0);
        auto Limited=Finite;Limited.Maximum=1;Limited.Emitters[0].Maximum=1;
        Limited.Emitters[0].Start=0;Limited.Emitters[0].BirthInterval=.1f;Limited.Emitters[0].Life.Base=2;
        FX.Presets.Add(4,{Limited,0});FAC2ParticleSource S;S.Key=4;S.Descriptor=4;FX.NativeBurst(S);
        for(int32 I=0;I<30;++I)FX.Step(.1f,FVector::ZeroVector);
        TestTrue(TEXT("Particle cap does not truncate a count-limited emitter"),FX.Systems.Contains(4));
        if(FX.Systems.Contains(4))TestEqual(TEXT("Emission resumes when capacity becomes available"),FX.Systems[4].Emitters[0].Born,2u);
        for(int32 I=0;I<40;++I)FX.Step(.1f,FVector::ZeroVector);
        TestFalse(TEXT("Capacity-limited finite source finishes after its final tail"),FX.Systems.Contains(4));
        for(bool WorldSpace:{false,true})
        {
            auto Tail=Loop;Tail.WorldSpace=WorldSpace;Tail.Emitters[0].BirthInterval=.1f;Tail.Emitters[0].Life.Base=1;
            FX.Presets.Add(5,{Tail,0});S.Key=5;S.Descriptor=5;S.Shared=WorldSpace;
            TestTrue(TEXT("Start unshared or world-space trail"),FX.NativeBurst(S));
            FX.Step(.2f,FVector::ZeroVector);const int32 Alive=FX.Count();
            TestTrue(TEXT("Trail emitted before release"),Alive>0);FX.StopSource(5);
            TestEqual(TEXT("Release keeps emitted particles"),FX.Count(),Alive);
            TestFalse(TEXT("Owner cannot move a released trail"),FX.MoveSource(5,FTransform(FVector(100,0,0))));
            TestFalse(TEXT("Release relinquishes looping ownership"),FX.LoopingSource(5));
            TestTrue(TEXT("Owner key can immediately start a new cast"),FX.NativeBurst(S));
            FX.StopSource(5); // New cast has not emitted; only old tail remains.
            for(int32 I=0;I<12;++I)FX.Step(.1f,FVector::ZeroVector);
            TestEqual(TEXT("Autonomous tail ends without further births"),FX.Count()+FX.SystemCount(),0);
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
