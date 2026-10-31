#pragma once
#include "AC2AssetReader.h"

// Waveform::StreamPack: type, then five value/velocity pairs.
struct FAC2Wave
{
    uint32 Kind = 1;
    float Base = 0, BaseSpeed = 0, Amplitude = 0, AmplitudeSpeed = 0;
    float Phase = 0, PhaseSpeed = 0, Frequency = 0, FrequencySpeed = 0, Roughness = 0, RoughnessSpeed = 0;
    static FAC2Wave Read(FAC2AssetReader &R);
    float Value(float Time) const;
};
struct FAC2WaveVector
{
    FAC2Wave X, Y, Z;
    static FAC2WaveVector Read(FAC2AssetReader &R);
    FVector Value(float T) const
    {
        return FVector(X.Value(T), Y.Value(T), Z.Value(T));
    }
};
// DecalFX::StreamPack (006A9930): packed RGBA, material, world offset,
// lifespan, then three Waveforms. This is independent of particle emitters.
struct FAC2Decal
{
    uint32 Material=0;
    FColor Color=FColor::White;
    FVector Origin=FVector::ZeroVector;
    float Lifetime=0;
    FAC2Wave Size,Rotation,Radius;
    static FAC2Decal Read(FAC2AssetReader& R);
    static float Opacity(float Age,float Life);
};
struct FAC2ParticleKey
{
    float Time = 0, X = 1, Y = 1, Mass = 1;
    FLinearColor Color = FLinearColor::White;
    uint32 Flags = 0, Controller = 0, Forces = 0;
    FAC2WaveVector Motion;
    TArray<FVector> Points;
};
struct FAC2ParticleEmitter
{
    uint32 Maximum = 0, Shape = 0, ParticleShape = 4, Limit = 0, Blast = 0;
    bool Exploding = false, Inclusive = false, Active = false, Constrain[3] = {};
    float BirthInterval = 0, Start = 0, Duration = 0, EmissionDistance = 0, FadeIn = 0, FadeOut = 0;
    FAC2Wave Scale, ParticleScale, Life, Speed, Streak, MinSpread, MaxSpread, Snap;
    FAC2WaveVector Origin, Rotation, WorldRotation, Spin, Direction;
    TArray<FAC2ParticleKey> Keys;
    void Sample(float Age, float ScaleAtBirth, float &Width, float &Height, FLinearColor &Tint, float &Mass) const;
    const FAC2ParticleKey *ControllerAt(float Age, uint32 &Forces) const;
};
struct FAC2ParticlePreset
{
    uint32 Material = 0, Maximum = 0, ScaleType = 0;
    float Step = .02f, FastForward = 0, PhysicsDuration = 0, FadeStart = 0, FadeStop = 0;
    bool WorldSpace = false, ForceDraw = false;
    TArray<FAC2ParticleEmitter> Emitters;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
    float RenderScale(float ObjectScale,float VisualParticleScale) const;
    void Sample(float Age, float &Width, float &Height, FLinearColor &Tint) const;
};

// World-space centimetres, resolved while the block's DAT library is on its worker.
struct FAC2ParticleSource
{
    uint64 Key = 0;
    uint32 Descriptor = 0;
    FTransform Frame;
    float ParticleScale = 1;
    uint32 Cell = 0; // Owning interior, or zero for outdoor scenery.
};
