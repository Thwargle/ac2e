#include "AC2Particle.h"

FAC2Wave FAC2Wave::Read(FAC2AssetReader &R)
{
    FAC2Wave W;
    W.Kind = R.Word();
    W.Base = R.Float();
    W.BaseSpeed = R.Float();
    W.Amplitude = R.Float();
    W.AmplitudeSpeed = R.Float();
    W.Phase = R.Float();
    W.PhaseSpeed = R.Float();
    W.Frequency = R.Float();
    W.FrequencySpeed = R.Float();
    W.Roughness = R.Float();
    W.RoughnessSpeed = R.Float();
    return W;
}
float FAC2Wave::Value(float T) const
{
    const float B = Base + BaseSpeed * T, A = Amplitude + AmplitudeSpeed * T;
    const float P = Phase * UE_PI + PhaseSpeed * T, F = Frequency + FrequencySpeed * T;
    const float S = FMath::Sin(F * T + P);
    float V = B;
    switch (Kind)
    {
    case 2:
        V += F * T + P;
        break;
    case 4:
        V += A * S;
        break;
    case 5:
        V += A * (S < 0 ? -1 : 1);
        break;
    case 6:
        V += A * FMath::Abs(S);
        break;
    case 3:
    case 7:
    case 8:
        // Deterministic noise, not Turbine's global dice / Perlin permutation.
        V += A * FMath::PerlinNoise1D(T * F + P);
        break;
    case 9:
        if (A >= 1 && F >= 1)
            V += int32(FMath::Fmod(FMath::Max(0.f, T) * int32(FMath::Min(F, 10000.f)), int32(FMath::Min(A, 10000.f)))) * P;
        break;
    }
    return FMath::IsFinite(V) ? V : B;
}
FAC2WaveVector FAC2WaveVector::Read(FAC2AssetReader &R)
{
    FAC2WaveVector V;
    V.X = FAC2Wave::Read(R);
    V.Y = FAC2Wave::Read(R);
    V.Z = FAC2Wave::Read(R);
    return V;
}
bool FAC2ParticlePreset::Decode(uint32 Id, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id)
        return false;
    Maximum = R.Word();
    ScaleType = R.Word();
    Step = R.Float();
    FastForward = R.Float();
    PhysicsDuration = R.Float();
    FadeStart = R.Float();
    FadeStop = R.Float();
    WorldSpace = R.Word() != 0;
    ForceDraw = R.Word() != 0;
    int32 NE = R.Count(1084, 64);
    int32 PointBudget = 262144;
    for (int32 I = 0; I < NE && R.Good; ++I)
    {
        FAC2ParticleEmitter E;
        E.Maximum = R.Word();
        E.Origin = FAC2WaveVector::Read(R);
        E.Shape = R.Word();
        E.ParticleShape = R.Word();
        E.Scale = FAC2Wave::Read(R);
        E.ParticleScale = FAC2Wave::Read(R);
        E.Exploding = R.Word() != 0;
        E.Inclusive = R.Word() != 0;
        E.Active = R.Word() != 0;
        E.BirthInterval = R.Float();
        E.Life = FAC2Wave::Read(R);
        E.Speed = FAC2Wave::Read(R);
        E.Streak = FAC2Wave::Read(R);
        E.Rotation = FAC2WaveVector::Read(R);
        E.WorldRotation = FAC2WaveVector::Read(R);
        E.Spin = FAC2WaveVector::Read(R);
        E.Direction = FAC2WaveVector::Read(R);
        E.MinSpread = FAC2Wave::Read(R);
        E.MaxSpread = FAC2Wave::Read(R);
        E.Limit = R.Word();
        E.Blast = R.Word();
        E.Start = R.Float();
        E.Duration = R.Float();
        E.EmissionDistance = R.Float();
        E.FadeIn = R.Float();
        E.FadeOut = R.Float();
        E.Snap = FAC2Wave::Read(R);
        for (bool &C : E.Constrain)
            C = R.Word() != 0;
        int32 NK = R.Count(180, 256);
        for (int32 K = 0; K < NK && R.Good; ++K)
        {
            FAC2ParticleKey Key;
            Key.Time = R.Float();
            Key.Flags = R.Word();
            Key.X = R.Float();
            Key.Y = R.Float();
            float Red = R.Float(), Green = R.Float(), Blue = R.Float(), Alpha = R.Float();
            Key.Color = FLinearColor(Red, Green, Blue, Alpha);
            Key.Mass = R.Float();
            Key.Controller = R.Word();
            Key.Forces = R.Word();
            Key.Motion = FAC2WaveVector::Read(R);
            int32 NP = R.Count(12, 65536);
            PointBudget -= NP;
            if (PointBudget < 0)
                return false;
            for (int32 P = 0; P < NP && R.Good; ++P)
                Key.Points.Add(R.Vector());
            R.Align();
            if (Key.Time < 0)
                R.Good = false;
            E.Keys.Add(MoveTemp(Key));
        }
        // 7160 PSDesc 390006AD contains colour keys out of chronological order.
        // Keep equal-time ordering, but make interpolation safe for this authored record.
        E.Keys.StableSort([](const auto &A, const auto &B) { return A.Time < B.Time; });
        Emitters.Add(MoveTemp(E));
    }
    Material = R.Word();
    return R.End() && NE > 0 && (Material >> 24) == 0x31;
}
void FAC2ParticleEmitter::Sample(float Age, float ScaleAtBirth, float &Width, float &Height, FLinearColor &Tint,
                                 float &Mass) const
{
    Width = Height = 1;
    Tint = FLinearColor::White;
    Mass = 1;
    // Channels have independent keys; a colour-only key must not reset scale or mass.
    for (int32 Channel = 0; Channel < 3; ++Channel)
    {
        const uint32 Interpolate = 1u << Channel, Set = 8u << Channel;
        const FAC2ParticleKey *A = nullptr, *B = nullptr;
        for (const auto &K : Keys)
        {
            if (!(K.Flags & (Interpolate | Set)))
                continue;
            if (K.Time <= Age)
                A = &K;
            else
            {
                if (K.Flags & Interpolate)
                    B = &K;
                break;
            }
        }
        if (!A)
            continue;
        const float T = B ? FMath::Clamp((Age - A->Time) / FMath::Max(.0001f, B->Time - A->Time), 0.f, 1.f) : 0;
        if (Channel == 0)
        {
            Width = B ? FMath::Lerp(A->X, B->X, T) : A->X;
            Height = B ? FMath::Lerp(A->Y, B->Y, T) : A->Y;
        }
        if (Channel == 1)
            Tint = B ? FMath::Lerp(A->Color, B->Color, T) : A->Color;
        if (Channel == 2)
            Mass = B ? FMath::Lerp(A->Mass, B->Mass, T) : A->Mass;
    }
    Width = FMath::Clamp(Width * ScaleAtBirth * 50, 0.f, 20000.f);
    Height = FMath::Clamp(Height * ScaleAtBirth * 50, 0.f, 20000.f);
}
const FAC2ParticleKey *FAC2ParticleEmitter::ControllerAt(float Age, uint32 &Forces) const
{
    const FAC2ParticleKey *Result = nullptr;
    Forces = 0;
    for (const auto &K : Keys)
    {
        if (K.Time > Age)
            break;
        if (K.Flags & 128)
            Result = &K;
        if (K.Flags & 256)
            Forces = K.Forces;
    }
    return Result;
}
void FAC2ParticlePreset::Sample(float Age, float &Width, float &Height, FLinearColor &Tint) const
{
    float Mass;
    if (!Emitters.IsEmpty())
        Emitters[0].Sample(Age, Emitters[0].ParticleScale.Value(0), Width, Height, Tint, Mass);
}
