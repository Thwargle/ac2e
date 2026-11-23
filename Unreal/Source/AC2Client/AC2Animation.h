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
    struct FCylinder { FVector Low; float Radius=0, Height=0; };
    struct FSphere { FVector Center; float Radius=0; };
    TArray<FCylinder> Cylinders;
    TArray<FSphere> Spheres;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
    TArray<FMatrix> World(const TArray<FTransform> &Pose, bool IncludeScale = true) const;
};
struct FAC2HoldLoop
{
    float Start=0, End=0;
    bool Active() const { return End>Start && End<=1 && Start>=0; }
    // Retail wraps only after passing the end key. The end key itself can fire FX.
    double Repeats(double Phase) const
    {
        return Active() && Phase>End ? FMath::CeilToDouble((Phase-End)/(double(End)-Start)) : 0;
    }
    double PhaseAt(double Phase) const
    {
        if(!Active() || Phase<=End)return Phase;
        const double Remainder=FMath::Fmod(Phase-End,double(End)-Start);
        return Remainder>0 ? Start+Remainder : End;
    }
};
struct FAC2Animation
{
    struct FEffect {uint32 Id=0; float Phase=0, Probability=1, Intensity=1, Variation=0; bool Stop=false,Play=true; uint64 SourceAction=0;};
    TArray<FEffect> Effects;
    TArray<float> JumpTriggers;
    TArray<float> StoryTriggers;
    FAC2HoldLoop HoldLoop;
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
    TArray<FAC2Animation::FEffect> EffectsBetween(float From, float To) const;
};

// Slider state belongs to the animated object, not to the shared DAT graph.
// Body/attachment graphs using the same slider see the same choice and ramp.
struct FAC2AnimatorState
{
    struct FRandomChoice
    {
        int32 From=0, To=0;
        double Elapsed=0, Updated=-1;
        float Ramp=0;
        bool Initialized=false, Active=false;
    };
    FRandomStream Random{1337};
    TMap<uint32,FRandomChoice> Choices;
    struct FTilt
    {
        FVector2D Value=FVector2D::ZeroVector,From=FVector2D::ZeroVector,Target=FVector2D::ZeroVector;
        double Started=0,Updated=-1;
    };
    TMap<uint32,FTilt> Tilts;
};

// Native locomotion sliders use metres/second in model axes (+Y forward).
struct FAC2AnimatorInput
{
    float Forward = 0, Side = 0, Vertical = 0, Turn = 0;
    FVector Look=FVector::ZeroVector, GroundNormal=FVector::UpVector;
    bool Grounded=false;
    double Time=0;
    FAC2AnimatorState* State=nullptr;
    TConstArrayView<uint32> BoneNames;
    uint32 Mode = 0, Element = 0, Terrain = 0;
    float Slider(uint32 Id) const;
};

struct FAC2AnimationTriggerState
{
    double Cycle = -1;
    TArray<FAC2Animation::FEffect> Fired;
};

// Bounded DBAnimator tree, including random and additive procedural controllers.
// Assets share a normalized cycle phase so blended gaits remain in step.
class FAC2Animator
{
  public:
    using FRead = TFunction<bool(uint32, TArray<uint8> &)>;
    static TSharedPtr<FAC2Animator> Load(FAC2DatArchive &Portal, uint32 Map, uint32 Motion);
    static TSharedPtr<FAC2Animator> Load(const FRead &Read, uint32 Map, uint32 Motion);
    float Period(const FAC2AnimatorInput &Input) const;
    float JumpDelay(const FAC2AnimatorInput &Input) const;
    TArray<float> StoryTriggers(const FAC2AnimatorInput &Input) const;
    FAC2HoldLoop HoldLoop(const FAC2AnimatorInput &Input) const;
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
        uint32 InterpolationSlider=0, RandomSlider=0;
        float Frequency=0, Ramp=0, Variation=0;
        struct FBoneAngle {uint32 Bone;float X,Y;};
        TArray<FBoneAngle> BoneAngles;
        FVector2D AngleMin=FVector2D::ZeroVector,AngleMax=FVector2D::ZeroVector;
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
