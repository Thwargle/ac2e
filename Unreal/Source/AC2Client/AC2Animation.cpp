#include "AC2Animation.h"
#include <type_traits>

namespace
{
FTransform ReadFrame(FAC2AssetReader &R)
{
    const FVector P = R.Vector() * 100, S = R.Vector();
    return FTransform(R.Quaternion(), P, S);
}
template <typename T> T SampleKeys(const TArray<TPair<float, T>> &K, float Time, const T &Default)
{
    if (K.IsEmpty())
        return Default;
    if (Time <= K[0].Key)
        return K[0].Value;
    for (int32 I = 1; I < K.Num(); ++I)
        if (Time < K[I].Key)
        {
            float A = (Time - K[I - 1].Key) / FMath::Max(SMALL_NUMBER, K[I].Key - K[I - 1].Key);
            if constexpr (std::is_same_v<T, FQuat>)
                return FQuat::Slerp(K[I - 1].Value, K[I].Value, A).GetNormalized();
            else
                return FMath::Lerp(K[I - 1].Value, K[I].Value, A);
        }
    return K.Last().Value;
}
} // namespace
bool FAC2Setup::Decode(uint32 Id, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id)
        return false;
    R.Word();
    int32 N = R.Count(4, 4096);
    for (int32 I = 0; I < N; ++I)
        Meshes.Add(R.Word());
    N = R.Count(8, 4096);
    for (int32 I = 0; I < N; ++I)
    {
        Parents.Add(R.Word());
        Names.Add(R.Word());
    }
    N = R.Count(8, 4096);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        uint32 Kind = R.Word();
        int32 K = R.Count(40, 4096);
        TArray<FTransform> P;
        for (int32 J = 0; J < K; ++J)
            P.Add(ReadFrame(R));
        if (Kind == 0)
            Rest = P;
        if (Kind == 1)
            Bind = P;
    }
    if (Rest.IsEmpty())
        Rest.Init(FTransform::Identity, Parents.Num());
    if (Bind.IsEmpty())
        Bind = Rest;
    for (int32 L = 0; L < 2; ++L)
    {
        N = R.Count(48, 4096);
        for (int32 I = 0; I < N && R.Good; ++I)
        {
            uint32 Key = R.Word();
            FSocket S;
            S.Bone = R.Word();
            S.Frame = ReadFrame(R);
            Sockets.Add(Key, S);
        }
    }
    return R.Good && Rest.Num() == Parents.Num() && Bind.Num() == Parents.Num();
}
TArray<FMatrix> FAC2Setup::World(const TArray<FTransform> &Pose, bool IncludeScale) const
{
    TArray<FMatrix> Out;
    for (int32 I = 0; I < Parents.Num(); ++I)
    {
        FMatrix M = Pose.IsValidIndex(I) ? (IncludeScale ? Pose[I].ToMatrixWithScale() : Pose[I].ToMatrixNoScale()) : FMatrix::Identity;
        if (Parents[I] < uint32(I))
            M = M * Out[Parents[I]];
        Out.Add(M);
    }
    return Out;
}
bool FAC2Animation::Decode(uint32 Id, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id || R.Word() != 0x40000001)
        return false;
    Period = R.Float();
    int32 N = R.Count(16, 256);
    if (Period < 0 || Period > 86400)
        return false;
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        FTrack T;
        for (int32 Kind = 0; Kind < 2; ++Kind)
        {
            int32 K = R.Count(16, 16384);
            auto &Values = Kind ? T.Offset : T.Scale;
            for (int32 J = 0; J < K; ++J)
            {
                FVector V = R.Vector();
                float Time = R.Float();
                if (Kind)
                    V *= 100;
                if (Time < 0 || (Values.Num() && Time < Values.Last().Key))
                    R.Good = false;
                Values.Add({Time, V});
            }
        }
        int32 K = R.Count(20, 16384);
        for (int32 J = 0; J < K; ++J)
        {
            FQuat Q = R.Quaternion();
            float Time = R.Float();
            if (Time < 0 || (T.Rotation.Num() && Time < T.Rotation.Last().Key))
                R.Good = false;
            T.Rotation.Add({Time, Q});
        }
        K = R.Count(8, 16384);
        for (int32 J = 0; J < K; ++J)
        {
            const float Weight = R.Float(), Time = R.Float();
            if (Weight < 0 || Time < 0 || (T.Weight.Num() && Time < T.Weight.Last().Key))
                R.Good = false;
            T.Weight.Add({Time, Weight});
        }
        Tracks.Add(MoveTemp(T));
    }
    // AnimAsset::StreamPackInternal appends AnimInfo: native FX triggers,
    // weenie hooks, impulse triggers and hold-loop data (7160 006B4FA0).
    if(R.Left()>=4 && R.Word())
    {
        N=R.Count(28,4096);
        for(int32 I=0;I<N && R.Good;++I)
        {
            FEffect E;E.Id=R.Word();E.Phase=R.Float();E.Stop=R.Word()!=0;
            E.Probability=R.Float();E.Intensity=R.Float();E.Variation=R.Float();E.Play=R.Word()!=0;
            if(!FMath::IsFinite(E.Phase)||!FMath::IsFinite(E.Probability)||!FMath::IsFinite(E.Intensity)||!FMath::IsFinite(E.Variation))R.Good=false;
            Effects.Add(E);
        }
        N=R.Count(20,4096);R.Take(int64(N)*20); // Server-owned attack hooks never apply damage locally.
        N=R.Count(12,4096);
        for(int32 I=0;I<N && R.Good;++I){if(!R.Word())R.Take(12);R.Take(8);}
        R.Take(12); // hold flag, loop start/end
    }
    return R.End() && !Tracks.IsEmpty();
}
TArray<FTransform> FAC2Animation::Sample(float Time, const TArray<FTransform> &Rest) const
{
    Time = Period > 0 ? FMath::Fmod(FMath::Max(Time, 0.f), Period) : 0;
    TArray<FTransform> Out = Rest;
    for (int32 I = 0; I < Out.Num() && I < Tracks.Num(); ++I)
    {
        const auto &T = Tracks[I];
        Out[I] = FTransform(SampleKeys(T.Rotation, Time, Rest[I].GetRotation()),
                            SampleKeys(T.Offset, Time, Rest[I].GetTranslation()),
                            SampleKeys(T.Scale, Time, Rest[I].GetScale3D()));
    }
    return Out;
}
float FAC2Animation::BoneWeight(int32 Bone, float Phase) const
{
    return Tracks.IsValidIndex(Bone) ? SampleKeys(Tracks[Bone].Weight, FMath::Frac(Phase) * Period, 1.f)
                                     : 0.f;
}
TArray<FAC2Animation::FEffect> FAC2Animator::EffectsBetween(float From,float To,const FAC2AnimatorInput& Input) const
{
    TArray<FWeighted> Leaves;Gather(Root.Get(),Input,1,Leaves);
    const FWeighted* Best=nullptr;
    for(const auto& L:Leaves)if(L.Node->Clip && (!Best || L.Weight>Best->Weight))Best=&L;
    TArray<FAC2Animation::FEffect> Out;
    if(Best && To>From)for(const auto& E:Best->Node->Clip->Effects)
        if((E.Phase>From || (From==0 && E.Phase==0)) && E.Phase<=To)Out.Add(E);
    return Out;
}
bool FAC2FXScript::Decode(uint32 Id,TConstArrayView<uint8> Bytes)
{
    // FxScript::StreamPack 7160 00628970: Trigger list, ImpulseTrigger list, period.
    Effects.Reset();Period=0;FAC2AssetReader R(Bytes);
    if(Id>>24!=0x57 || R.Word()!=Id)return false;
    int32 N=R.Count(28,4096);
    for(int32 I=0;I<N && R.Good;++I)
    {
        FAC2Animation::FEffect E;E.Id=R.Word();E.Phase=R.Float();E.Stop=R.Word()!=0;
        E.Probability=R.Float();E.Intensity=R.Float();E.Variation=R.Float();E.Play=R.Word()!=0;
        if(!FMath::IsFinite(E.Phase)||E.Phase<0||E.Phase>1||!FMath::IsFinite(E.Probability)||!FMath::IsFinite(E.Intensity)||!FMath::IsFinite(E.Variation))R.Good=false;
        Effects.Add(E);
    }
    N=R.Count(12,4096);
    for(int32 I=0;I<N && R.Good;++I){if(!R.Word())R.Take(12);R.Take(8);}
    Period=R.Float();return R.End() && FMath::IsFinite(Period) && Period>0 && Period<=300;
}
TSharedPtr<FAC2Animation> FAC2Animation::Load(FAC2DatArchive &Portal, uint32 Map, uint32 Motion)
{
    TArray<uint8> B;
    FString Error;
    uint32 Id = Map;
    if ((Map >> 24) == 3)
    {
        if (!Portal.Read(Map, B, Error))
            return nullptr;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Map)
            return nullptr;
        uint32 N = R.Word() & 65535;
        Id = 0;
        for (uint32 I = 0; I < N && R.Good; ++I)
        {
            uint32 Key = R.Word(), Value = R.Word();
            if (Key == Motion)
                Id = Value;
        }
    }
    TSet<uint32> Seen;
    for (int32 Depth = 0; Id && Depth < 8 && !Seen.Contains(Id); ++Depth)
    {
        Seen.Add(Id);
        if (!Portal.Read(Id, B, Error) || B.Num() < 8)
            return nullptr;
        if (AC2::U32(B.GetData() + 4) == 0x40000001)
        {
            TSharedPtr<FAC2Animation> A = MakeShared<FAC2Animation>();
            return A->Decode(Id, B) ? A : nullptr;
        }
        if (AC2::U32(B.GetData() + 4) != 0x40000008)
            return nullptr;
        uint32 Child = 0;
        for (int32 I = 8; I + 4 <= B.Num(); I += 4)
        {
            uint32 V = AC2::U32(B.GetData() + I);
            if ((V >> 24) == 5)
            {
                Child = V;
                break;
            }
        }
        Id = Child;
    }
    return nullptr;
}
