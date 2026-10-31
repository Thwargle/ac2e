#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeExit.h"

void SAC2GameUI::LoadShortcuts()
{
    const auto* P=Mode->Characters.Active();if(!P)return;
    const FString Owner=TEXT("AC2.Shortcuts.")+P->Id;
    if(ShortcutOwner==Owner)return;
    ShortcutOwner=Owner;ShortcutSet=0;FMemory::Memzero(Shortcuts);
    FString Saved;
    if(GConfig->GetString(*Owner,TEXT("Slots"),Saved,GGameUserSettingsIni)){
        TArray<FString> Values;Saved.ParseIntoArray(Values,TEXT(","),false);
        for(int32 I=0;I<FMath::Min(100,Values.Num());++I)Shortcuts[I]=FCString::Strtoui64(*Values[I],nullptr,10);
        GConfig->GetInt(*Owner,TEXT("Set"),ShortcutSet,GGameUserSettingsIni);ShortcutSet=FMath::Clamp(ShortcutSet,0,9);
    }else{
        const uint32 Human[]={0xb0,0x60,0xcf},Lugian[]={0x166,0x111,0x1a0},Tumerok[]={0x2cc,0x2a0,0x316};
        for(int32 I=0;I<3;++I)Shortcuts[I]=(P->Race==2?Lugian:P->Race==4?Tumerok:Human)[I];
    }
}

bool SAC2GameUI::SmokeShortcuts(FString& Error)
{
    LoadShortcuts();
    const FString OriginalOwner=ShortcutOwner;const int32 OriginalSet=ShortcutSet;
    uint32 Original[100];FMemory::Memcpy(Original,Shortcuts,sizeof(Shortcuts));
    TestingShortcuts=true;
    ON_SCOPE_EXIT{FMemory::Memcpy(Shortcuts,Original,sizeof(Shortcuts));ShortcutSet=OriginalSet;ShortcutOwner=OriginalOwner;
        TestingShortcuts=false;Rebuild();};
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto Position=[&](FName Key,FVector2D& P){const auto* R=Buttons.Find(Key);if(!R)return false;
        P=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);return true;};
    auto Click=[&](FName Key){FVector2D P;if(!Position(Key,P))return false;
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
    // Rebuild normally loads the active character. Keep that load idempotent
    // while using an otherwise empty page, and restore the original on exit.
    ShortcutOwner=OriginalOwner;ShortcutSet=1;FMemory::Memzero(Shortcuts+10,10*sizeof(uint32));Rebuild();
    auto Drag=[&](FName From,FName To){FVector2D A,B;if(!Position(From,A)||!Position(To,B))return false;
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
    if(!Drag(TEXT("LearnedSkill96"),TEXT("ShortcutSlot9")) || Shortcuts[19]!=96)return Fail(TEXT("Learned active skill did not drag onto an empty hotbar slot"));
    if(!Drag(TEXT("ShortcutSlot9"),TEXT("ShortcutSlot7")) || Shortcuts[17]!=96 || Shortcuts[19])return Fail(TEXT("Hotbar slot drag did not move the binding"));
    if(!Click(TEXT("ShortcutUp")) || ShortcutSet!=0)return Fail(TEXT("Hotbar up arrow did not select the previous set"));
    if(!Click(TEXT("ShortcutDown")) || ShortcutSet!=1 || Shortcuts[17]!=96)return Fail(TEXT("Hotbar down arrow lost its bindings"));
    const auto* Number=Native.Find(TEXT("SetNumberDisplay"));
    if(!Number || Number->Text!=TEXT("2"))return Fail(TEXT("Hotbar page number did not update"));
    return true;
}
void SAC2GameUI::SaveShortcuts()
{
    if(ShortcutOwner.IsEmpty() || TestingShortcuts)return;
    FString Saved;for(int32 I=0;I<100;++I){if(I)Saved+=TEXT(",");Saved+=FString::FromInt(Shortcuts[I]);}
    GConfig->SetString(*ShortcutOwner,TEXT("Slots"),*Saved,GGameUserSettingsIni);
    GConfig->SetInt(*ShortcutOwner,TEXT("Set"),ShortcutSet,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
}
bool SAC2GameUI::ShortcutAllowed(uint32 Skill) const
{
    const auto* Net=Mode->Network.Get();
    return Skill && Net && Net->UntrainingSkill!=Skill && Net->Skills.ContainsByPredicate([&](const auto& S){return S.Id==Skill&&S.Trained;});
}
void SAC2GameUI::ActivateShortcut(int32 Slot)
{
    if(Editing || Slot<0 || Slot>=10 || !Mode->InWorld())return;
    LoadShortcuts();const uint32 Skill=Shortcuts[ShortcutSet*10+Slot];if(!Skill)return;
    if(!Mode->Network){if(Slot==0)Mode->Attack();else if(Slot==1)Mode->CastBurst(Mode->PreviewEffect);return;}
    if(!ShortcutAllowed(Skill))return;
    if(Skill==0xb0 || Skill==0x166 || Skill==0x2cc)Mode->Network->Attack();
    else if(Skill==0x343)Mode->Network->Recall();else Mode->Network->CastSkill(Skill);
}
void SAC2GameUI::BuildShortcuts()
{
    LoadShortcuts();const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    for(const auto Pair:{TPair<uint32,FName>(0x52b27974,TEXT("ShortcutUp")),TPair<uint32,FName>(0xe6d21fa6,TEXT("ShortcutDown"))})
        if(const auto* N=Native.Find(Pair.Key))Buttons.Add(Pair.Value,FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));
    for(auto& N:Native.Nodes)if(N.Id==T(TEXT("SetNumberDisplay")))N.Text=FString::FromInt(ShortcutSet+1);
    const FIntRect Area=Native.Bounds(TEXT("ShortcutListBox"));
    for(int32 I=0;I<10 && Area.Width()>0;++I){
        const FIntRect Rect(Area.Min+FIntPoint(I*32,0),Area.Min+FIntPoint((I+1)*32,32));
        const FName Key(*FString::Printf(TEXT("ShortcutSlot%d"),I));
        Buttons.Add(Key,FSlateRect(Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y));
        const uint32 Skill=Shortcuts[ShortcutSet*10+I];if(!Skill)continue;
        Native.Text.Add(T(TEXT("NumberDisplay")),FString::FromInt((I+1)%10));
        Native.States.Add(T(TEXT("ShortcutTemplate")),T(Mode->Network&&!ShortcutAllowed(Skill)?TEXT("ghosted"):TEXT("stateless")));
        const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ShortcutTemplate"),Rect,Area);
        for(int32 J=First;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Id==T(TEXT("ShortcutIcon"))){
            FAC2UIArt Art;Art.Image=SkillData.Get(Skill).Icon;Native.Nodes[J].Media.Add(Art);}
        Hints.Add(Key,SkillData.Get(Skill).Name+TEXT("\nDrag to move; drag into the world to remove."));
    }
}
