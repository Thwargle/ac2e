#include "AC2Animation.h"

float FAC2AnimatorInput::Slider(uint32 Id) const
{
    switch (Id)
    {
    case 0x40000001:
        return Forward;
    case 0x40000002:
        return Side;
    case 0x40000003:
        return Vertical;
    case 0x40000004:
        return Turn;
    default:
        return 0;
    }
}

TSharedPtr<FAC2Animator> FAC2Animator::Load(FAC2DatArchive &Portal, uint32 Map, uint32 Motion)
{
    return Load(
        [&](uint32 Id, TArray<uint8> &Bytes) {
            FString Error;
            return Portal.Read(Id, Bytes, Error);
        },
        Map, Motion);
}
TSharedPtr<FAC2Animator> FAC2Animator::Load(const FRead &Read, uint32 Map, uint32 Motion)
{
    uint32 Id = Map;
    TArray<uint8> Bytes;
    if ((Map >> 24) == 3)
    {
        if (!Read(Map, Bytes))
            return nullptr;
        FAC2AssetReader R(MoveTemp(Bytes));
        if (R.Word() != Map)
            return nullptr;
        const uint32 Count = R.Word() & 65535;
        Id = 0;
        for (uint32 I = 0; I < Count && R.Good; ++I)
        {
            const uint32 Key = R.Word(), Value = R.Word();
            if (Key == Motion)
                Id = Value;
        }
        if (!R.End())
            return nullptr;
    }
    TMap<uint32, TSharedPtr<FNode>> Cache;
    TSet<uint32> Visiting;
    bool Good = true;
    int32 Budget = 512;
    TFunction<TSharedPtr<FNode>(uint32, int32)> Node = [&](uint32 Key, int32 Depth) -> TSharedPtr<FNode> {
        if (!Key)
            return nullptr; // Null lattice corners are authored and contribute no pose.
        if (Visiting.Contains(Key) || Depth > 12 || --Budget < 0)
        {
            Good = false;
            return nullptr;
        }
        if (auto *Cached = Cache.Find(Key))
            return *Cached;
        TArray<uint8> Data;
        if ((Key >> 24) != 5 || !Read(Key, Data))
        {
            Good = false;
            return nullptr;
        }
        FAC2AssetReader R(MoveTemp(Data));
        auto N = MakeShared<FNode>();
        N->Id = R.Word();
        N->Kind = R.Word();
        if (N->Id != Key)
        {
            Good = false;
            return nullptr;
        }
        Visiting.Add(Key);
        if (N->Kind == 0x40000001)
        {
            N->Clip = MakeShared<FAC2Animation>();
            Good &= N->Clip->Decode(Key, R.Data);
        }
        else if (N->Kind == 0x40000003)
        {
            N->Children.Add(Node(R.Word(), Depth + 1));
            Good &= R.End();
        }
        else if (N->Kind == 0x40000008)
        {
            const uint32 Default = R.Word();
            N->SwitchKind = R.Word();
            const uint32 Count = R.Word() & 65535;
            if (Count > 4096 || N->SwitchKind > 2)
                R.Good = false;
            N->Children.Add(Node(Default, Depth + 1));
            for (uint32 I = 0; I < Count && R.Good; ++I)
            {
                N->Keys.Add(R.Word());
                N->Children.Add(Node(R.Word(), Depth + 1));
            }
            Good &= R.End();
        }
        else if (N->Kind == 0x40000002)
        {
            const int32 Count = R.Count(8, 8);
            int64 Lattice = 1;
            for (int32 I = 0; I < Count && R.Good; ++I)
            {
                FAxis Axis;
                Axis.Id = R.Word();
                const int32 Ticks = R.Count(4, 64);
                if (!Ticks)
                    R.Good = false;
                for (int32 J = 0; J < Ticks && R.Good; ++J)
                {
                    float Tick = R.Float();
                    if (J && Tick <= Axis.Ticks.Last())
                        R.Good = false;
                    Axis.Ticks.Add(Tick);
                }
                Lattice *= Ticks;
                if (Lattice > 4096)
                    R.Good = false;
                N->Axes.Add(MoveTemp(Axis));
            }
            const int32 Children = R.Count(4, 4096);
            if (Children != Lattice)
                R.Good = false;
            for (int32 I = 0; I < Children && R.Good; ++I)
                N->Children.Add(Node(R.Word(), Depth + 1));
            Good &= R.End();
        }
        else
            Good = false;
        Visiting.Remove(Key);
        Cache.Add(Key, N);
        return N;
    };
    TSharedPtr<FAC2Animator> Graph = MakeShared<FAC2Animator>();
    Graph->Root = Node(Id, 0);
    return Good && Graph->Root ? Graph : nullptr;
}
void FAC2Animator::Gather(const FNode *N, const FAC2AnimatorInput &Input, float Weight,
                          TArray<FWeighted> &Out)
{
    if (!N || Weight < .0002f)
        return;
    if (N->Clip)
    {
        Out.Add({N, Weight});
        return;
    }
    if (N->Kind == 0x40000003)
    {
        Gather(N->Children[0].Get(), Input, Weight, Out);
        return;
    }
    if (N->Kind == 0x40000008)
    {
        uint32 Key = N->SwitchKind == 0 ? Input.Mode : N->SwitchKind == 1 ? Input.Element : Input.Terrain;
        int32 Index = N->Keys.IndexOfByKey(Key);
        Gather(N->Children[Index + 1].Get(), Input, Weight, Out);
        return;
    }
    if (N->Kind != 0x40000002)
        return;
    struct FCorner
    {
        int32 Index;
        float Weight;
    };
    TArray<FCorner> Corners{{0, Weight}};
    int32 Stride = 1;
    for (const auto &Axis : N->Axes)
    {
        float Value = FMath::Clamp(Input.Slider(Axis.Id), Axis.Ticks[0], Axis.Ticks.Last());
        int32 Low = 0;
        while (Low + 1 < Axis.Ticks.Num() && Axis.Ticks[Low + 1] <= Value)
            ++Low;
        int32 High = FMath::Min(Low + 1, Axis.Ticks.Num() - 1);
        float Alpha = High == Low ? 0 : (Value - Axis.Ticks[Low]) / (Axis.Ticks[High] - Axis.Ticks[Low]);
        TArray<FCorner> Next;
        for (const auto &C : Corners)
        {
            if (Alpha < 1)
                Next.Add({C.Index + Low * Stride, C.Weight * (1 - Alpha)});
            if (Alpha > 0)
                Next.Add({C.Index + High * Stride, C.Weight * Alpha});
        }
        Corners = MoveTemp(Next);
        Stride *= Axis.Ticks.Num(); // First slider varies fastest in native lattice order.
    }
    for (const auto &C : Corners)
        Gather(N->Children[C.Index].Get(), Input, C.Weight, Out);
}
float FAC2Animator::Period(const FAC2AnimatorInput &Input) const
{
    TArray<FWeighted> Clips;
    Gather(Root.Get(), Input, 1, Clips);
    float Period = 0, Weight = 0;
    for (const auto &C : Clips)
    {
        Period += C.Weight * C.Node->Clip->Period;
        Weight += C.Weight;
    }
    // Null lattice corners provide no animation, not a zero-duration clip.
    return Weight>SMALL_NUMBER ? Period/Weight : 0;
}
TArray<uint32> FAC2Animator::ActiveClips(const FAC2AnimatorInput &Input) const
{
    TArray<FWeighted> Clips;
    Gather(Root.Get(), Input, 1, Clips);
    TArray<uint32> Ids;
    for (const auto &C : Clips)
        Ids.AddUnique(C.Node->Id);
    return Ids;
}
TArray<FTransform> FAC2Animator::Sample(float Phase, const FAC2AnimatorInput &Input,
                                        const TArray<FTransform> &Rest) const
{
    TArray<FWeighted> Clips;
    Gather(Root.Get(), Input, 1, Clips);
    TArray<FTransform> Pose = Rest;
    TArray<float> Weights;
    Weights.Init(0, Rest.Num());
    Phase = FMath::Frac(FMath::Max(Phase, 0.f));
    for (const auto &C : Clips)
    {
        const auto P = C.Node->Clip->Sample(Phase * C.Node->Clip->Period, Rest);
        for (int32 I = 0; I < Pose.Num(); ++I)
        {
            const float W = C.Weight * C.Node->Clip->BoneWeight(I, Phase);
            if (W <= SMALL_NUMBER)
                continue;
            if (Weights[I] == 0)
                Pose[I] = P[I];
            else
            {
                const float Alpha = W / (Weights[I] + W);
                Pose[I].SetTranslation(FMath::Lerp(Pose[I].GetTranslation(), P[I].GetTranslation(), Alpha));
                Pose[I].SetScale3D(FMath::Lerp(Pose[I].GetScale3D(), P[I].GetScale3D(), Alpha));
                Pose[I].SetRotation(
                    FQuat::Slerp(Pose[I].GetRotation(), P[I].GetRotation(), Alpha).GetNormalized());
            }
            Weights[I] += W;
        }
    }
    // Authored partial bone weights leave the underlying locomotion/rest pose visible.
    for(int32 I=0;I<Pose.Num();++I)
        if(Weights[I]>0 && Weights[I]<1)
        {FTransform Mixed;Mixed.Blend(Rest[I],Pose[I],Weights[I]);Pose[I]=Mixed;}
    return Pose;
}
