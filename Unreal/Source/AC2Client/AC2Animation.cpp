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
    // CharacterSetup::UnPack places its collision primitives after the two
    // socket lists. Coordinates and dimensions here are Unreal centimetres.
    if(R.Left()) {
        N=R.Count(20,4096);
        for(int32 I=0;I<N && R.Good;++I) {
            FCylinder C;C.Low=R.Vector()*100;C.Radius=R.Float()*100;C.Height=R.Float()*100;
            if(C.Radius<0 || C.Height<0)R.Good=false;
            Cylinders.Add(C);
        }
        N=R.Count(16,4096);
        for(int32 I=0;I<N && R.Good;++I) {
            FSphere S;S.Center=R.Vector()*100;S.Radius=R.Float()*100;
            if(S.Radius<0)R.Good=false;
            Spheres.Add(S);
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
        N=R.Count(20,4096);
        for(int32 I=0;I<N && R.Good;++I)
        {
            const uint32 Type=R.Word();R.Take(12);const float Phase=R.Float();
            if(!FMath::IsFinite(Phase)||Phase<0||Phase>1)R.Good=false;
            // StoryManager::HandleAnimHook increments its counter for this
            // hook. Presentation only: damage is still entirely server-owned.
            if(Type==0x40000002)StoryTriggers.Add(Phase);
        }
        StoryTriggers.Sort();
        N=R.Count(12,4096);
        for(int32 I=0;I<N && R.Good;++I)
        {
            const bool Jump=R.Word()!=0;
            if(!Jump)R.Take(12);
            R.Word();const float Phase=R.Float();
            if(!FMath::IsFinite(Phase)||Phase<0||Phase>1)R.Good=false;
            if(Jump)JumpTriggers.Add(Phase);
        }
        const bool HasHoldLoop=R.Word()!=0;
        const float Start=R.Float(), End=R.Float();
        if(HasHoldLoop)HoldLoop={Start,End};
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
TArray<FAC2Animation::FEffect> FAC2Animator::EffectsBetween(double From,double To,const FAC2AnimatorInput& Input,
    bool Looping,FAC2AnimationTriggerState* State) const
{
    TArray<FAC2Animation::FEffect> Out;
    if(!FMath::IsFinite(From)||!FMath::IsFinite(To)||From<0||To<=From)return Out;
    const FNode* Node=Root.Get();
    while(Node && (Node->Kind==0x40000003 || Node->Kind==0x40000008))
    {
        int32 Child=0;
        if(Node->Kind==0x40000008)
        {
            const uint32 Key=Node->SwitchKind==0?Input.Mode:Node->SwitchKind==1?Input.Element:Input.Terrain;
            Child=Node->Keys.IndexOfByKey(Key)+1;
        }
        Node=Node->Children.IsValidIndex(Child)?Node->Children[Child].Get():nullptr;
    }
    if(!Node)return Out;
    TArray<FAC2Animation::FEffect> Events;
    if(Node->Clip)Events=Node->Clip->Effects;
    else
    {
        // AnimCube::GetTriggers merges by effect name, summing weighted time,
        // probability and intensity. Choosing the dominant gait loses quieter
        // footsteps and snaps their timing when walk/run blend weights cross.
        TArray<FWeighted> Leaves;Gather(Node,Input,1,Leaves);
        TMap<uint32,FAC2Animation::FEffect> Merged;
        for(const auto& Leaf:Leaves)
        {
          if(!Leaf.Node->Clip)continue;
          for(const auto& E:Leaf.Node->Clip->Effects)
          {
            auto* Total=Merged.Find(E.Id);
            if(!Total){FAC2Animation::FEffect Empty;Empty.Id=E.Id;Empty.Probability=Empty.Intensity=0;Total=&Merged.Add(E.Id,Empty);}
            Total->Phase+=E.Phase*Leaf.Weight;
            Total->Probability+=E.Probability*Leaf.Weight;
            Total->Intensity+=E.Intensity*Leaf.Weight;
            Total->Variation+=E.Variation*Leaf.Weight;
          }
        }
        Merged.GenerateValueArray(Events);
    }
    Events.StableSort([](const auto& A,const auto& B){return A.Phase<B.Phase || (A.Phase==B.Phase && A.Id<B.Id);});
    // Catch the tail and head of a wrapped cycle, including phase-zero cues.
    // Bound catch-up after an invalid/very long time jump rather than flooding FX.
    const double First=Looping?FMath::Max(FMath::FloorToDouble(From),FMath::FloorToDouble(To)-16):0;
    const double Last=Looping?FMath::FloorToDouble(To):0;
    for(int32 Offset=0;Offset<=16 && First+Offset<=Last;++Offset)
    {
        const double Cycle=First+Offset;
        if(State && State->Cycle!=Cycle){State->Cycle=Cycle;State->Fired.Reset();}
        for(const auto& E:Events)
        {
            const double At=Cycle+E.Phase;
            if(!((At>From || (From==0 && At==0)) && At<=To))continue;
            // Cube event times vary as speed changes; crossing the same cue a
            // second time in one stride must not play a second footstep.
            if(State && State->Fired.ContainsByPredicate([&](const auto& Known){return Known.Id==E.Id &&
                (!Node->Clip || (Known.Phase==E.Phase && Known.Probability==E.Probability));}))continue;
            Out.Add(E);
            if(State)State->Fired.Add(E);
        }
    }
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
        // FxScript::GetTriggers accepts authored pre-roll times. Icefire's
        // 5700000C starts its first effect at -0.01, due on the first update.
        if(!FMath::IsFinite(E.Phase)||E.Phase>1||!FMath::IsFinite(E.Probability)||!FMath::IsFinite(E.Intensity)||!FMath::IsFinite(E.Variation))R.Good=false;
        Effects.Add(E);
    }
    N=R.Count(12,4096);
    for(int32 I=0;I<N && R.Good;++I){if(!R.Word())R.Take(12);R.Take(8);}
    Period=R.Float();return R.End() && FMath::IsFinite(Period) && Period>0 && Period<=300;
}
TArray<FAC2Animation::FEffect> FAC2FXScript::EffectsBetween(float From,float To) const
{
    TArray<FAC2Animation::FEffect> Due;
    if(!FMath::IsFinite(From)||!FMath::IsFinite(To)||From<0||To<=From)return Due;
    for(const auto& E:Effects)
        if((E.Phase>From || (From==0 && E.Phase<=0)) && E.Phase<=To)Due.Add(E);
    return Due;
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
