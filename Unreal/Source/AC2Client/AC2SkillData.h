#pragma once
#include "AC2DatArchive.h"
// Common SkillData fields in the native index. Eligibility remains server-owned.
struct FAC2SkillInfo {
    FString Name,Description; uint32 Icon=0,Race=0,Weapons=0,Level=0,Credits=0,Cap=0,Curve=0,Kind=0;
    bool Valid=false;
    TMap<uint32,uint32> Required,Forbidden;
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
class FAC2SkillData
{
public:
    bool Open(const FString& Directory);
    const FAC2SkillInfo& Get(uint32 Id);
    TArray<uint32> All(uint32 Race);
    int32 Rank(uint32 Id,uint64 Xp);
    uint64 NextCost(uint32 Id,uint64 Xp);
    const FAC2SkillPanel& Panel(uint32 Id);
    static uint32 RootPanel(uint32 Race,int32 Tab);
    int32 PanelCount() const { return Panels.Num(); }
private:
    FAC2DatArchive Portal,Local;
    TMap<uint32,uint32> Records;
    TMap<uint32,FAC2SkillInfo> Cache;
    TMap<uint32,TArray<uint64>> Curves;
    TMap<uint32,uint32> Panels;
    TMap<uint32,FAC2SkillPanel> PanelCache;
    const TArray<uint64>& Curve(uint32 Id);
};
