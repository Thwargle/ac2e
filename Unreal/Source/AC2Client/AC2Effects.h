#pragma once
#include "CoreMinimal.h"
#include "AC2Particle.h"
#include "AC2Assets.h"
class UWorld;
class UProceduralMeshComponent;
class AActor;
// Native PSDesc systems, streamed scenery sources, and local precipitation.
// Caps are shared across emitters so corrupt records or dense towns cannot allocate without bound.
class FAC2Effects
{
  public:
    ~FAC2Effects();
    void Open(UWorld *World, const FString &Directory);
    bool NativeBurst(uint32 Id, const FVector &Position);
    bool NativeBurst(const FAC2ParticleSource& Source) { return Start(Source,false); }
    bool MoveSource(uint64 Key,const FTransform& Frame);
    void StopSource(uint64 Key);
    void SyncSources(const TArray<FAC2ParticleSource> &Sources);
    void Update(const FVector &Camera, const FVector &Right, const FVector &Up, float Delta, float Rain,
                const FVector &Wind, bool Indoors);
    void Burst(const FVector &Position, const FLinearColor &Color);
    AActor *RenderActor() const
    {
        return Actor;
    }
    int32 Count() const
    {
        return Particles.Num();
    }
    int32 SystemCount() const
    {
        return Systems.Num();
    }
    int32 NativeCount() const;
    int32 SourceCount() const;
    int32 RainCount() const;
    int32 ActiveEmitters(uint32 Descriptor) const;

  private:
    struct FPreset
    {
        FAC2ParticlePreset Data;
        int32 Section = 0;
    };
    struct FEmitterState
    {
        float NextBirth = 0;
        FVector LastDischarge = FVector::ZeroVector;
        uint32 Born = 0;
    };
    struct FSystem
    {
        uint32 Descriptor = 0;
        FAC2ParticleSource Source;
        TArray<FEmitterState> Emitters;
        FRandomStream Random;
        float Age = 0, Stop = 0;
        bool Persistent = false;
    };
    struct FParticle
    {
        FVector P = FVector::ZeroVector, V = FVector::ZeroVector, Origin = FVector::ZeroVector;
        float Life = 0, MaxLife = 0, Width = 3, Height = 3, Scale = 1, Angle = 0, Spin = 0, Streak = 0;
        FLinearColor Color = FLinearColor::White;
        uint64 System = 0;
        int32 Emitter = 0, Section = 0, Point = 0;
        const FAC2ParticleKey *Controller = nullptr;
        bool Rain = false, Splash = false, Impact = false;
    };
    TArray<FParticle> Particles;
    TUniquePtr<FAC2AssetLibrary> Assets;
    FAC2DatArchive Portal;
    TMap<uint32, FPreset> Presets;
    TMap<uint64, FSystem> Systems;
    int32 Sections = 1;
    uint64 PreviewSerial = 0xffffffff00000000ULL;
    AActor *Actor = nullptr;
    UProceduralMeshComponent *Mesh = nullptr;
    UWorld *World = nullptr;
    FRandomStream Random{42061};
    float Emit = 0;
    bool Start(const FAC2ParticleSource &Source, bool Persistent);
    void Step(float Dt, const FVector &Wind);
    void Discharge(uint64 Key, FSystem &System, int32 Emitter, float BirthTime);
};
