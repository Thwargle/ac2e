#include "AC2Effects.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"

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
void FAC2Effects::Open(UWorld *W, const FString &Directory)
{
    World = W;
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
        Preset = &Presets.Add(Source.Descriptor, {MoveTemp(Data), Section});
        UE_LOG(LogTemp, Display, TEXT("AC2 native PSDesc %08X: %d emitters, material=%08X"),
               Source.Descriptor, Preset->Data.Emitters.Num(), Preset->Data.Material);
    }
    FSystem S;
    S.Source = Source;
    S.Descriptor = Source.Descriptor;
    S.Persistent = Persistent;
    S.Random.Initialize(uint32(Source.Key) ^ uint32(Source.Key >> 32) ^ Source.Descriptor);
    S.Emitters.SetNum(Preset->Data.Emitters.Num());
    for (int32 I = 0; I < S.Emitters.Num(); ++I)
    {
        const auto &E = Preset->Data.Emitters[I];
        S.Emitters[I].NextBirth = FMath::Max(0.f, E.Start) + FMath::Max(.001f, E.BirthInterval);
        S.Emitters[I].LastDischarge = Source.Frame.GetLocation();
        S.Stop = FMath::Max(S.Stop, E.Start + (E.Duration > 0 ? E.Duration : 1.f));
    }
    S.Stop = FMath::Clamp(S.Stop, .1f, 15.f);
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
            P.V=Frame.TransformVector(S->Source.Frame.InverseTransformVector(P.V));
        }
    S->Source.Frame=Frame;return true;
}
void FAC2Effects::StopSource(uint64 Key)
{
    Systems.Remove(Key);Particles.RemoveAll([Key](const FParticle& P){return P.System==Key;});
}
void FAC2Effects::SyncSources(const TArray<FAC2ParticleSource> &Sources)
{
    TSet<uint64> Wanted;
    for (const auto &S : Sources)
        Wanted.Add(S.Key);
    for (auto It = Systems.CreateIterator(); It; ++It)
        if (It.Value().Persistent && !Wanted.Contains(It.Key()))
        {
            const uint64 Key = It.Key();
            Particles.RemoveAllSwap([&](const auto &P) { return P.System == Key; });
            It.RemoveCurrent();
        }
    for (const auto &S : Sources)
    {
        auto *Existing = Systems.Find(S.Key);
        if (Existing && Existing->Descriptor != S.Descriptor)
        {
            Particles.RemoveAllSwap([&](const auto &P) { return P.System == S.Key; });
            Systems.Remove(S.Key);
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
    if (E.Shape == 2)
    {
        float A = R.FRand() * 2 * UE_PI;
        Shape = FVector(FMath::Cos(A), FMath::Sin(A), 0);
    }
    if (E.Shape == 3)
        Shape = R.VRand();
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
    const float Spread = FMath::DegreesToRadians(R.FRandRange(E.MinSpread.Value(T), E.MaxSpread.Value(T)));
    FVector Side = FVector::CrossProduct(Direction, FMath::Abs(Direction.Z) < .9 ? FVector::UpVector
                                                                                 : FVector::XAxisVector)
                       .GetSafeNormal();
    Direction =
        FQuat(Direction, R.FRand() * 2 * UE_PI).RotateVector(FQuat(Side, Spread).RotateVector(Direction));
    P.P = S.Source.Frame.TransformPosition((Shape + E.Origin.Value(T)) * 100);
    P.Origin = P.P;
    P.V = S.Source.Frame.TransformVectorNoScale(Direction) * FMath::Clamp(E.Speed.Value(T), -1000.f, 1000.f) *
          100;
    P.Scale = E.ParticleScale.Value(T) * Preset.Data.RenderScale(S.Source.Frame.GetScale3D().GetAbsMax(),S.Source.ParticleScale);
    P.Angle = E.Rotation.Z.Value(T);
    P.Spin = E.Spin.Z.Value(T);
    P.Streak = E.Streak.Value(T);
    P.Point = int32(S.Emitters[Index].Born);
    Particles.Add(P);
}
void FAC2Effects::Step(float Dt, const FVector &Wind)
{
    for (auto &Pair : Systems)
    {
        auto &S = Pair.Value;
        S.Age += Dt;
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
        if (!S.Persistent && S.Age > S.Stop)
            continue;
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
        const auto &E = Presets[S->Descriptor].Data.Emitters[P.Emitter];
        const float Age = P.MaxLife - P.Life;
        float Mass;
        E.Sample(Age, P.Scale, P.Width, P.Height, P.Color, Mass);
        uint32 Forces;
        const auto *K = E.ControllerAt(Age, Forces);
        if (K != P.Controller)
        {
            P.Controller = K;
            P.Origin = P.P;
        }
        const float Relative = K ? FMath::Max(0.f, Age - K->Time) : Age;
        if (K && K->Controller == 2)
            P.P = P.Origin + S->Source.Frame.TransformVector(K->Motion.Value(Relative) * 100);
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
        }
        P.Angle += P.Spin * Dt;
    }
    for (auto It = Systems.CreateIterator(); It; ++It)
        if (!It.Value().Persistent && It.Value().Age > It.Value().Stop)
        {
            bool Alive = false;
            for (const auto &P : Particles)
                Alive |= P.System == It.Key();
            if (!Alive)
                It.RemoveCurrent();
        }
}
void FAC2Effects::Update(const FVector &Camera, const FVector &Right, const FVector &Up, float Dt, float Rain,
                         const FVector &Wind, bool Indoors)
{
    if (!Mesh)
        return;
    UpdateDecals();
    Dt = FMath::Clamp(Dt, 0.f, .15f);
    float Remaining = Dt;
    while (Remaining > SMALL_NUMBER)
    {
        float Quantum = FMath::Min(Remaining, 1.f / 60);
        Step(Quantum, Wind);
        Remaining -= Quantum;
    }
    if (Indoors)
        Particles.RemoveAllSwap([](const auto &P) { return P.Rain; });
    else
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
            Particles.Add(P);
        }
    }
    Actor->SetActorLocation(Camera);
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
            FVector Side = Right, Vertical = Up;
            bool Triangle = false;
            if (P.System)
            {
                const auto &S = Systems[P.System];
                const auto &D = Presets[S.Descriptor].Data;
                const auto &E = D.Emitters[P.Emitter];
                Triangle = E.ParticleShape == 3;
                const float Distance = FVector::Distance(Camera, S.Source.Frame.GetLocation()) / 100;
                if (D.FadeStop > D.FadeStart)
                    Color.A *=
                        1 - FMath::Clamp((Distance - D.FadeStart) / (D.FadeStop - D.FadeStart), 0.f, 1.f);
                float A = FMath::DegreesToRadians(P.Angle);
                Side = Right * FMath::Cos(A) + Up * FMath::Sin(A);
                Vertical = Up * FMath::Cos(A) - Right * FMath::Sin(A);
                if (E.Constrain[0] || E.Constrain[1] || E.Constrain[2])
                {
                    FVector Axis = E.Constrain[2]   ? FVector::UpVector
                                   : E.Constrain[1] ? FVector::YAxisVector
                                                    : FVector::XAxisVector;
                    Vertical = S.Source.Frame.TransformVectorNoScale(Axis);
                    Side = FVector::CrossProduct(Vertical, (Camera - P.P).GetSafeNormal()).GetSafeNormal();
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
            if (P.Streak > 0)
                Vertical += P.V * FMath::Clamp(P.Streak, 0.f, 1.f) * .5f;
            if (Color.A <= .001f)
                continue;
            int32 I = V.Num();
            FVector Center = P.P - Camera;
            V.Append({Center - Side - Vertical, Center + Side - Vertical, Center - Side + Vertical});
            UV.Append({FVector2D(0, 1), FVector2D(1, 1), FVector2D(0, 0)});
            Indices.Append({I, I + 2, I + 1});
            if (!Triangle)
            {
                V.Add(Center + Side + Vertical);
                UV.Add(FVector2D(1, 0));
                Indices.Append({I + 1, I + 2, I + 3});
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
