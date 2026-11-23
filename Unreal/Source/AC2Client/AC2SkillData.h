#pragma once
#include "AC2DatArchive.h"
// Common SkillData fields in the native index. Eligibility remains server-owned.
struct FAC2SkillInfo {
    FString Name,Description; uint32 Icon=0,Race=0,Weapons=0,Level=0,Credits=0,Cap=0,Curve=0,Kind=0;
    uint32 LeftWeapons=0;
    float ResetSeconds=0;
    bool Valid=false,HasImplements=false,Harmful=false;
    TMap<uint32,uint32> Required,Forbidden;
};
struct FAC2SkillMissile {
    uint32 Entity=0,Visual=0,Holding=0;
    float Speed=0,SourceHeight=0,TargetHeight=0;
    bool Clone=false,Thrown=false;
};
struct FAC2SkillNode {
    uint32 Id=0,Kind=0,Icon=0; FIntPoint Cell=FIntPoint::ZeroValue;
    FString Name,Description; TArray<uint32> Parents,Dependencies;
    bool Passive=false,Untrainable=false;
};
struct FAC2SkillPanel {
    FString Name,Description; uint32 Icon=0;
    TArray<FAC2SkillNode> Nodes; bool Valid=false;
};
// A snapshot of authoritative character state, used only for UI eligibility.
struct FAC2SkillContext {
    uint32 Race=0,Level=0,Untraining=0;
    int32 Credits=0;
    uint64 AvailableXp=0;
    bool Dead=false;
    TMap<uint32,uint64> Known;
};
struct FAC2SkillActions {
    // Empty means the action is available; otherwise explain the disabled control.
    FString Train,Raise,Untrain,Cancel;
};
class FAC2SkillData
{
public:
    bool Open(const FString& Directory);
    const FAC2SkillInfo& Get(uint32 Id);
    TOptional<FAC2SkillMissile> Missile(uint32 Id,uint32 AttackHook);
    TArray<uint32> All(uint32 Race);
    int32 Rank(uint32 Id,uint64 Xp);
    uint64 NextCost(uint32 Id,uint64 Xp);
    bool Supported(uint32 Id,uint32 Race);
    FAC2SkillActions Actions(uint32 Id,const FAC2SkillContext& Context,bool Untrainable=true);
    const FAC2SkillPanel& Panel(uint32 Id);
    static uint32 RootPanel(uint32 Race,int32 Tab);
    // Empty hand is enum bit 1; unset means equipment data has not arrived.
    static TOptional<bool> Holds(uint32 Required,TOptional<uint32> Implement);
    static FString ImplementName(uint32 Mask);
    int32 PanelCount() const { return Panels.Num(); }
private:
    FAC2DatArchive Portal,Local;
    TMap<uint32,uint32> Records;
    TMap<uint32,FAC2SkillInfo> Cache;
    TMap<uint32,TArray<FAC2SkillMissile>> MissileCache;
    TMap<uint32,TArray<uint64>> Curves;
    TMap<uint32,uint32> Panels;
    TMap<uint32,FAC2SkillPanel> PanelCache;
    const TArray<uint64>& Curve(uint32 Id);
};
