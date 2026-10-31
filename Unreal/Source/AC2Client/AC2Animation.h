#pragma once
#include "AC2AssetReader.h"

struct FAC2Setup
{
    struct FSocket
    {
        uint32 Bone = 0;
        FTransform Frame;
    };
    TArray<uint32> Meshes, Parents, Names;
    TArray<FTransform> Rest, Bind;
    TMap<uint32, FSocket> Sockets;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
    TArray<FMatrix> World(const TArray<FTransform> &Pose, bool IncludeScale = true) const;
};
struct FAC2Animation
{
    struct FEffect {uint32 Id=0; float Phase=0, Probability=1, Intensity=1, Variation=0; bool Stop=false,Play=true;};
    TArray<FEffect> Effects;
    TArray<float> JumpTriggers;
    struct FTrack
    {
        TArray<TPair<float, FVector>> Scale, Offset;
        TArray<TPair<float, FQuat>> Rotation;
        TArray<TPair<float, float>> Weight;
    };
    float Period = 0;
    TArray<FTrack> Tracks;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
    TArray<FTransform> Sample(float Time, const TArray<FTransform> &Rest) const;
    float BoneWeight(int32 Bone, float Phase) const;
    static TSharedPtr<FAC2Animation> Load(FAC2DatArchive &Portal, uint32 Map, uint32 Motion);
};

struct FAC2FXScript
{
    float Period=0;
    TArray<FAC2Animation::FEffect> Effects;
    bool Decode(uint32 Id,TConstArrayView<uint8> Bytes);
};

// Native locomotion sliders use metres/second in model axes (+Y forward).
struct FAC2AnimatorInput
{
    float Forward = 0, Side = 0, Vertical = 0, Turn = 0;
    uint32 Mode = 0, Element = 0, Terrain = 0;
    float Slider(uint32 Id) const;
};

struct FAC2AnimationTriggerState
{
    double Cycle = -1;
    TArray<FAC2Animation::FEffect> Fired;
};

// Bounded DBAnimator tree: AnimAsset, AnimCube, AMHuman and AMSwitchME.
// Assets share a normalized cycle phase so blended gaits remain in step.
class FAC2Animator
{
  public:
    using FRead = TFunction<bool(uint32, TArray<uint8> &)>;
    static TSharedPtr<FAC2Animator> Load(FAC2DatArchive &Portal, uint32 Map, uint32 Motion);
    static TSharedPtr<FAC2Animator> Load(const FRead &Read, uint32 Map, uint32 Motion);
    float Period(const FAC2AnimatorInput &Input) const;
    float JumpDelay(const FAC2AnimatorInput &Input) const;
    TArray<FTransform> Sample(float Phase, const FAC2AnimatorInput &Input,
                              const TArray<FTransform> &Rest) const;
    TArray<uint32> ActiveClips(const FAC2AnimatorInput &Input) const;
    // Looping tracks pass unwrapped cycle times; pose sampling still uses the fractional phase.
    TArray<FAC2Animation::FEffect> EffectsBetween(double From,double To,const FAC2AnimatorInput& Input,
        bool Looping=false,FAC2AnimationTriggerState* State=nullptr) const;

  private:
    struct FAxis
    {
        uint32 Id = 0;
        TArray<float> Ticks;
    };
    struct FNode
    {
        uint32 Id = 0, Kind = 0, SwitchKind = 0;
        TSharedPtr<FAC2Animation> Clip;
        TArray<FAxis> Axes;
        TArray<TSharedPtr<FNode>> Children;
        TArray<uint32> Keys;
    };
    struct FWeighted
    {
        const FNode *Node;
        float Weight;
    };
    TSharedPtr<FNode> Root;
    static void Gather(const FNode *Node, const FAC2AnimatorInput &Input, float Weight,
                       TArray<FWeighted> &Out);
};
