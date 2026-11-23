#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Network.h"

// Isolated rendered fixture: accepted self-heal, rejected repeat, duplicated
// shortcuts, another page, then expiry. The test proxy delays real UDP replies
// so the requested marker can also be captured. No clock/events are fabricated.
bool SAC2GameUI::SmokeCooldownInput(int32 Step,FString& Error)
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto* Net=Mode->Network.Get();auto* P=Mode->Characters.Active();
    if(!Net || !P)return Fail(TEXT("Cooldown fixture has no connected character"));
    if(Step==0){
        ClosePanels();TestingShortcuts=true;ShortcutSet=0;ShortcutRows=1;ShortcutInstantScroll=true;
        for(auto& Binding:Shortcuts)Binding={};
        Shortcuts[0]=Shortcuts[1]=Shortcuts[10]=FAC2Shortcut::ForSkill(0x55);
        const int32 Weapon=P->Items.IndexOfByPredicate([](const auto& I){return I.Definition==0x470007ee;});
        if(Weapon==INDEX_NONE)return Fail(TEXT("Cooldown fixture needs its starter sword"));
        if(!P->Items[Weapon].Equipped)Net->UseItem(Weapon);
        Net->SetCombat(false); // The accepted support cast must choose its native stance.
        Rebuild();return true;
    }
    auto RequestedOverlay=[&](int32 Slot){const auto R=Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
        return Native.Nodes.ContainsByPredicate([&](const auto& N){return !N.Clip.IsEmpty() && N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right &&
            N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom && N.Media.ContainsByPredicate([](const auto& M){return M.Image==0x41001710;});});};
    auto ActiveOverlay=[&](int32 Slot){const auto R=Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
        return Native.Nodes.ContainsByPredicate([&](const auto& N){return !N.Clip.IsEmpty() && N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right &&
            N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom && N.Media.ContainsByPredicate([](const auto& M){return M.Image==0x41005b1b;});});};
    if(Step==2 || Step==8){
        Rebuild();const uint32 Skill=Step==2?0x55:0x59;
        if(!Net->SkillPending(Skill) || !RequestedOverlay(0) || !RequestedOverlay(1))return Fail(TEXT("Actual sent request did not mark both shortcuts pending"));
        SelectShortcutSet(1);Rebuild();
        if(!RequestedOverlay(0))return Fail(TEXT("Pending marker was lost when changing pages"));
        return true;
    }
    if(Step==7){
        SelectShortcutSet(0);Shortcuts[0]=Shortcuts[1]=Shortcuts[10]=FAC2Shortcut::ForSkill(0x59);
        Rebuild();Net->CastSkill(0x59);return true; // Sword deliberately fails Flame Bolt's stave requirement.
    }
    if(Step==9){
        Rebuild();
        if(Net->SkillPending(0x59) || RequestedOverlay(0))return Fail(TEXT("Server rejection did not clear its request marker"));
        return true;
    }
    if(Step>=3)--Step;
    bool Blocked=false;SkillEquipmentHint(0x55,Blocked);
    if(Blocked)return Fail(TEXT("Healing skill is blocked by equipment"));
    if(Step==1){
        if(Net->Combat)return Fail(TEXT("Fixture must begin the support cast in peace mode"));
        if(P->Health>=Net->MaxHealth)return Fail(TEXT("Fixture must be injured for an accepted heal"));
        Net->ClearTarget();Net->CastSkill(0x55);return true;
    }
    const auto* Skill=Net->Skills.FindByPredicate([](const auto& S){return S.Id==0x55;});
    const auto Now=Net->ServerTime();
    if(!Skill || Skill->ResetStarted<0 || !Now.IsSet())return Fail(TEXT("Server did not deliver native skill reset timestamp"));
    const auto Reset=Net->SkillCooldown(0x55,SkillData.Get(0x55).ResetSeconds,Now.GetValue());
    auto Height=[&](int32 Slot){const auto R=Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
        for(const auto& N:Native.Nodes)if(N.Id==FAC2UILayout::Token(TEXT("bottom_fill")) &&
            N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right && N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom)
            return N.Clip.Height();return 0;};
    Rebuild();
    if(Step==2){
        if(!Net->Combat)return Fail(TEXT("Accepted support cast did not enter its native combat stance"));
        SelectShortcutSet(0);Rebuild();
        if(Net->SkillPending(0x55) || RequestedOverlay(0) || !Net->SkillActive(0x55) || !ActiveOverlay(0) || !ActiveOverlay(1))return Fail(TEXT("Playing skill story did not replace both pending markers with native active art"));
        SelectShortcutSet(1);Rebuild();
        if(!ActiveOverlay(0))return Fail(TEXT("Active marker was lost on another page"));
        SelectShortcutSet(0);Rebuild();
        if(Reset.Remaining(Now.GetValue())<5 || Height(0)<23 || Height(0)!=Height(1))return Fail(TEXT("Accepted cast did not shade both native shortcut meters"));
        SmokeCooldownStamp=Skill->ResetStarted;Net->CastSkill(0x55);
    }
    if(Step==3){
        if(Net->SkillBusy(0x55) || ActiveOverlay(0))return Fail(TEXT("Finished animation left its shortcut active or locked"));
        if(Skill->ResetStarted!=SmokeCooldownStamp || Height(0)<=0 || Height(0)>=23)return Fail(TEXT("Rejected repeat restarted the timer, or fill did not move"));
    }
    if(Step==4){
        SelectShortcutSet(1);Rebuild();
        if(Height(0)<=0 || Height(0)>=23 || Skill->ResetStarted!=SmokeCooldownStamp)return Fail(TEXT("Page change lost the running cooldown"));
    }
    if(Step==5){
        if(Reset.Remaining(Now.GetValue())!=0 || Height(0)!=0 || Hints.FindRef(TEXT("ShortcutSlot0")).Contains(TEXT("Ready in")))
            return Fail(TEXT("Expired timer still shades the shortcut"));
    }
    return true;
}
