#pragma once
#include "AC2Animation.h"

// Legions MotionInterpDesc (0x32) and BehaviorTable (0x44), read without modifying the DAT.
struct FAC2MotionValues
{
    float Lift=0, Drag=0, Terminal=50;
    bool Gravity=true;
    float Sink=0, Hover=0, Jump=6.28f, Forward=5.5f, Backward=2, Strafe=3;
    float StillTurn=180, RunTurn=180, Vertical=3, ForwardBrake=15, BackwardBrake=15, StrafeBrake=15;
    float TurnBrake=180, ForwardAccel=15, BackwardAccel=15, StrafeAccel=15, TurnAccel=180, VerticalAccel=10;
};
struct FAC2MotionData
{
    struct FTerrain {FAC2MotionValues Default;TMap<uint32,FAC2MotionValues> Modifiers;};
    struct FList { FAC2MotionValues Default; TMap<uint32,FTerrain> Terrain;TMap<uint32,FAC2MotionValues> Global; };
    TMap<uint64,uint32> States;
    TArray<FList> Lists;
    float SeaFloor=.1f, Water=.75f;
    bool Decode(uint32 Id,TConstArrayView<uint8> Bytes);
    FAC2MotionValues Values(uint32 Element,uint32 Mode=0x40000001,uint32 Terrain=0x80000001,uint32 Global=0,uint32 Modifier=0) const;
};
struct FAC2BehaviorData
{
    struct FCombo { uint32 Element,Mode,Behavior,Key,Layer; };
    TArray<FCombo> Combos;
    struct FTransition {uint32 FromElement,FromMode,ToElement,ToMode,Behavior,Effect,StopEffect;};
    TArray<FTransition> Transitions;
    TMap<uint32,float> Ramps;
    uint32 InitialMode=0;
    bool Decode(uint32 Id,TConstArrayView<uint8> Bytes);
    uint32 Key(uint32 Element,uint32 Mode,uint32 Behavior) const;
};
namespace AC2Motion
{
constexpr uint32 Ground=0x40000001, Air=0x40000002, Water=0x40000003, SeaFloor=0x40000004;
constexpr uint32 Peace=0x40000001, Swim=0x40000003, Melee=0x4100000a, Dead=0x41000005;
constexpr uint32 Monster=0x41000012;
constexpr uint32 Move=0x40000001, Jump=0x4000000a, Land=0x4000000b, Fall=0x40000009;
// EnumMapper 23000027: Melee_Auto1, death, Damage_Hit1, Magic_LS_Recall1.
constexpr uint32 Attack=0x41000089, Death=0x4100001f, Hit=0x4100007c, Recall=0x410000d8;
constexpr uint32 MeleeNormal=0x41000077, Unarmed=0x4100000f;
}
