#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "AC2CharacterUI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SkillEquipmentTest,"AC2.Data.SkillEquipmentFeedback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SkillEquipmentTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    FString Error;TestTrue(TEXT("DAT equipment catalogue"),Mode->CharacterCatalog.Open(Mode->DatDirectory,Error));
    // Starter entity IDs and their visuals vary across retail builds.
    auto Starter=[&](const TCHAR* Name,uint32 Implement){
        for(const auto& Pair:Mode->CharacterCatalog.Items)
            if(Pair.Value.Name==Name)
            {
                TestTrue(Name,Pair.Value.Implement.IsSet() && Pair.Value.Implement.GetValue()==Implement);
                return Pair.Key;
            }
        AddError(FString::Printf(TEXT("Starter missing from native character matrix: %s"),Name));
        return 0u;
    };
    const uint32 Bow=Starter(TEXT("Sapling Bow"),4);
    const uint32 Stave=Starter(TEXT("Apprentice Staff"),0x40000);
    Starter(TEXT("Rusty Sword"),2);Starter(TEXT("Chuck"),0x100);
    Starter(TEXT("Javelin"),0x8000);Starter(TEXT("Drum"),8);
    uint32 Boots=0;
    for(const auto& Pair:Mode->CharacterCatalog.Items)
        if(Pair.Value.Preferred==16 && !Pair.Value.Implement.IsSet()){Boots=Pair.Key;break;}
    TestNotEqual(TEXT("Native starter footwear"),Boots,0u);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Equipment feedback tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Net.Skills={{0x60,true,TEXT("0")},{0xcf,true,TEXT("0")},{0x59,true,TEXT("0")}};
    {
        const auto UI=SNew(SAC2GameUI).Mode(Mode);UI->TestingShortcuts=true;
        const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};Tick();
        UI->ShortcutSet=0;UI->ShortcutRows=1;UI->ShortcutInstantScroll=true;
        for(auto& Binding:UI->Shortcuts)Binding={};
        UI->Shortcuts[0]=FAC2Shortcut::ForSkill(0x60);UI->Shortcuts[1]=FAC2Shortcut::ForSkill(0xcf);
        UI->Shortcuts[2]=FAC2Shortcut::ForSkill(0x59);Tick();
        auto Hint=[&](int32 Slot){return UI->Hints.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));};
        auto Shaded=[&](int32 Slot){const auto R=UI->Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
            return UI->Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Id==FAC2UILayout::Token(TEXT("bottom_fill")) &&
                N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right && N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom && !N.Clip.IsEmpty() &&
                N.Media.ContainsByPredicate([](const auto& M){return M.Image==0x410016eau;});});};
        auto Equipment=[&](const FString& Id,uint32 Slot){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("inventory"));
            J->SetStringField(TEXT("id"),Player.Id);J->SetNumberField(TEXT("gold"),0);J->SetArrayField(TEXT("items"),{});
            TArray<TSharedPtr<FJsonValue>> Worn;if(!Id.IsEmpty()){auto W=MakeShared<FJsonObject>();W->SetStringField(TEXT("id"),Id);W->SetNumberField(TEXT("slot"),Slot);Worn.Add(MakeShared<FJsonValueObject>(W));}
            J->SetArrayField(TEXT("equipment"),Worn);Net.Handle(J);Tick();};
        auto Create=[&](const FString& Id,uint32 Definition){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("object"));
            J->SetStringField(TEXT("id"),Id);J->SetStringField(TEXT("container"),Player.Id);J->SetNumberField(TEXT("entity"),Definition);J->SetNumberField(TEXT("quantity"),1);Net.Handle(J);Tick();};
        TestFalse(TEXT("Login does not guess empty hands before inventory"),Shaded(0));
        TestTrue(TEXT("Pending equipment is explained"),Hint(0).Contains(TEXT("data pending")));
        Equipment(TEXT("100"),0x40);
        TestFalse(TEXT("Snapshot before worn object create is not treated as empty"),Net.HeldImplement(0x40).IsSet());
        TestFalse(TEXT("Pending worn weapon does not produce false shading"),Shaded(0));
        Create(TEXT("100"),Bow);
        TestTrue(TEXT("Bow shades magic, not missile"),Shaded(0) && !Shaded(1));
        TestTrue(TEXT("Unmet requirement names the stave"),Hint(0).Contains(TEXT("Stave (not equipped)")));
        TestTrue(TEXT("Bow is confirmed equipped in missile tooltip"),Hint(1).Contains(TEXT("Bow (equipped)")));
        UI->InspectShortcut(0);Tick();
        TestTrue(TEXT("Skill examination includes the current equipment requirement"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text.Contains(TEXT("Stave (not equipped)"));}));
        Equipment(TEXT("101"),0x40);Create(TEXT("101"),Stave);
        TestTrue(TEXT("Changing to a stave reverses the shading live"),!Shaded(0) && Shaded(1));
        TestTrue(TEXT("Examination refreshes after a gear change"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text.Contains(TEXT("Stave (equipped)"));}));
        UI->ShowSkills=true;UI->JournalAction(TEXT("MagicButton"));Tick();
        TestTrue(TEXT("Skill book tooltip shares the live equipment requirement"),UI->Hints.FindRef(TEXT("LearnedSkill96")).Contains(TEXT("Stave (equipped)")));
        UI->ShowSkills=false;
        Equipment(TEXT("101"),0x80);
        TestTrue(TEXT("A stave in the wrong hand fails both requirements"),Shaded(0) && Hint(0).Contains(TEXT("Empty hand (not equipped)")));
        Equipment(TEXT("102"),0x40);Create(TEXT("102"),Boots);
        TestFalse(TEXT("No implement quality remains unknown, not a guessed weapon category"),Net.HeldImplement(0x40).IsSet());
        TestTrue(TEXT("Unknown metadata explains pending status without a false equipment shade"),!Shaded(0) && Hint(0).Contains(TEXT("data pending")));
        Equipment(FString(),0);
        TestTrue(TEXT("Unequipping shades weapon skills but satisfies empty off-hand"),Shaded(0) && Shaded(1) && Hint(0).Contains(TEXT("Empty hand (equipped)")));
        TestTrue(TEXT("Incompatible equipment does not erase or unlearn bindings"),UI->Shortcuts[0].Skill==0x60 && UI->ShortcutAllowed(0x60));
        TestTrue(TEXT("Unknown equipment cannot satisfy a specific requirement"),!FAC2SkillData::Holds(4,{}).IsSet());
        TestTrue(TEXT("Unrestricted requirement does not need equipment metadata"),FAC2SkillData::Holds(MAX_uint32,{}).GetValue());
        TestTrue(TEXT("Mixed mask accepts allowed alternative"),FAC2SkillData::Holds(0x11,1u).GetValue() && FAC2SkillData::Holds(0x11,0x10u).GetValue());
        TestFalse(TEXT("Mixed mask rejects other implements"),FAC2SkillData::Holds(0x11,4u).GetValue());
        TestEqual(TEXT("Alternative labels are complete"),FAC2SkillData::ImplementName(0x4100),FString(TEXT("Hammer or Turret construction kit")));
        TestFalse(TEXT("Passive mastery has no fabricated implement requirements"),UI->SkillData.Get(4).HasImplements);
        // Real bridge events drive one timer per skill, independent of pages,
        // duplicate shortcuts, click attempts and equipment presentation.
        Equipment(TEXT("101"),0x40);Create(TEXT("101"),Stave);
        UI->Shortcuts[3]=UI->Shortcuts[10]=FAC2Shortcut::ForSkill(0x60);
        auto ClockAt=[&](double Now){Net.Clock.Valid=true;Net.Clock.Base=Now;Net.Clock.Received=FPlatformTime::Seconds();Tick();};
        auto Reset=MakeShared<FJsonObject>();Reset->SetStringField(TEXT("event"),TEXT("skills"));Reset->SetBoolField(TEXT("replace"),false);
        auto S=MakeShared<FJsonObject>();S->SetNumberField(TEXT("id"),0x60);S->SetBoolField(TEXT("trained"),true);
        S->SetStringField(TEXT("xp"),TEXT("0"));S->SetNumberField(TEXT("reset_started"),1000.);
        Reset->SetArrayField(TEXT("items"),{MakeShared<FJsonValueObject>(S)});Net.Handle(Reset);ClockAt(1000.1);
        auto Height=[&](int32 Slot){const auto R=UI->Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
            for(const auto& N:UI->Native.Nodes)if(N.Id==FAC2UILayout::Token(TEXT("bottom_fill")) &&
                N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right && N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom)
                return N.Clip.Height();return 0;};
        TestEqual(TEXT("Mind Lash reset uses native skill field 34"),UI->SkillData.Get(0x60).ResetSeconds,5.f);
        const int32 StartHeight=Height(0);
        TestTrue(TEXT("Accepted cast shades the skill and duplicate binding"),StartHeight>24 && Height(3)==StartHeight);
        TestTrue(TEXT("Tooltip exposes remaining server reset"),Hint(0).Contains(TEXT("Ready in")));
        auto Finished=MakeShared<FJsonObject>();Finished->SetStringField(TEXT("event"),TEXT("skill_finished"));Net.Handle(Finished);
        Net.CastSkill(0x60);ClockAt(1002.5);
        TestTrue(TEXT("Failed/repeated casts and finish notices do not restart the countdown"),Height(0)>10 && Height(0)<StartHeight);
        UI->ShortcutSet=1;Tick();TestTrue(TEXT("Paging retains the same skill countdown"),Height(0)>10 && Height(0)<StartHeight);
        UI->ShortcutSet=0;Equipment(TEXT("100"),0x40);
        TestTrue(TEXT("Wrong weapon takes precedence over partial cooldown shade"),Height(0)>24);
        ClockAt(1006);Equipment(TEXT("101"),0x40);
        TestFalse(TEXT("Expired timer stays expired after re-equipping"),Shaded(0));
        Net.SharedSkillReset={1006,1};ClockAt(1006.5);
        TestTrue(TEXT("Shared recovery shades another equipped skill"),Height(2)>10 && Height(2)<24);
        // Native pending notices identify the actual sent request, not the
        // maneuver. Rejected repeats must not unlock an earlier queued cast.
        auto Notice=[&](const TCHAR* Event,uint32 Request,uint32 Skill,bool Enabled=false){auto J=MakeShared<FJsonObject>();
            J->SetStringField(TEXT("event"),Event);J->SetNumberField(TEXT("request"),Request);J->SetNumberField(TEXT("skill"),Skill);
            J->SetBoolField(TEXT("enabled"),Enabled);Net.Handle(J);Tick();};
        // Each state selects a child whose bitmap is authored in its own
        // stateless media. The child's bitmap IDs moved between these DATs.
        const bool IcefireArt=Bow==0x4700051bu;
        const uint32 PendingArt=0x41001710u;
        const uint32 ToggledArt=IcefireArt?0x410044d6u:0x41007f72u;
        const uint32 ActiveArt=IcefireArt?0x410044d7u:0x41005b1bu;
        auto Overlay=[&](int32 Slot,uint32 Image){const auto R=UI->Buttons.FindRef(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));
            return UI->Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Clip.Min.X>=R.Left && N.Clip.Max.X<=R.Right &&
                N.Clip.Min.Y>=R.Top && N.Clip.Max.Y<=R.Bottom && !N.Clip.IsEmpty() &&
                N.Media.ContainsByPredicate([&](const auto& M){return M.Image==Image;});});};
        Notice(TEXT("skill_requested"),17,0x60);Notice(TEXT("skill_requested"),18,0x60);
        TestTrue(TEXT("Native pending art marks every copy of the requested skill"),Overlay(0,PendingArt) && Overlay(3,PendingArt));
        TestFalse(TEXT("Other skills never inherit the request marker"),Overlay(2,PendingArt));
        Notice(TEXT("skill_requested"),17,0x59);
        TestFalse(TEXT("Duplicate counter cannot be reassigned to another skill"),Net.SkillPending(0x59));
        Notice(TEXT("skill_finished"),18,0);
        TestTrue(TEXT("Rejected repeat does not clear earlier request"),Net.SkillPending(0x60) && Overlay(0,PendingArt));
        UI->ShortcutSet=1;Tick();TestTrue(TEXT("Paging preserves requested state"),Overlay(0,PendingArt));
        UI->ShortcutSet=0;Tick();UI->ShortcutLocked=false;UI->DragShortcut=0;UI->DragBinding=UI->Shortcuts[0];
        UI->ItemPressPoint={0,0};UI->Mouse={60,60};UI->UpdateShortcutDrag(true);
        TestFalse(TEXT("Even Shift/unlocked shortcuts cannot drag a requested skill"),UI->ShortcutDragStarted);
        TestTrue(TEXT("Blocked drag releases cleanly"),UI->FinishShortcutDrag());
        TestEqual(TEXT("Blocked drag cannot remove the binding"),UI->Shortcuts[0].Skill,0x60u);
        Notice(TEXT("skill_toggled"),0,0x60,true);
        TestTrue(TEXT("Pending marker takes priority without forgetting toggle"),Overlay(0,PendingArt) && Net.ToggledSkills.Contains(0x60));
        Notice(TEXT("skill_finished"),99,0);TestTrue(TEXT("Unknown completion cannot unlock an attack"),Net.SkillPending(0x60));
        Notice(TEXT("skill_finished"),17,0);
        TestTrue(TEXT("Completion restores native toggled artwork"),!Overlay(0,PendingArt) && Overlay(0,ToggledArt));
        TestTrue(TEXT("Request completion does not alter shared recovery"),Height(2)>0);
        Notice(TEXT("skill_toggled"),0,0x60,false);TestFalse(TEXT("Untoggle removes native marker"),Overlay(0,ToggledArt));
        UI->DragShortcut=0;UI->DragBinding=UI->Shortcuts[0];UI->ItemPressPoint={0,0};UI->Mouse={60,60};UI->UpdateShortcutDrag(true);
        TestTrue(TEXT("Finishing restores shortcut dragging"),UI->ShortcutDragStarted);
        Notice(TEXT("skill_requested"),19,0x60);UI->FinishShortcutDrag();
        TestEqual(TEXT("Request arriving during drag prevents removal at drop"),UI->Shortcuts[0].Skill,0x60u);
        Notice(TEXT("skill_finished"),19,0);
        // StoryManager selects active artwork only after the matching action
        // starts. Its identity survives unrelated layers, but not replacement.
        Notice(TEXT("skill_requested"),30,0x60);Net.StartSkillStory(30,0x410000b9,101);Tick();
        TestTrue(TEXT("Playing story selects native active art on duplicate bindings"),Net.SkillActive(0x60) && !Net.SkillPending(0x60) && Overlay(0,ActiveArt) && Overlay(3,ActiveArt));
        UI->ShortcutSet=1;Tick();TestTrue(TEXT("Active artwork follows paging"),Overlay(0,ActiveArt));
        UI->ShortcutSet=0;Tick();UI->DragShortcut=0;UI->DragBinding=UI->Shortcuts[0];UI->ItemPressPoint={0,0};UI->Mouse={60,60};UI->UpdateShortcutDrag(true);
        TestFalse(TEXT("Active skill retains drag protection"),UI->ShortcutDragStarted);UI->FinishShortcutDrag();
        Notice(TEXT("skill_requested"),31,0x60);Notice(TEXT("skill_finished"),31,0);
        TestTrue(TEXT("Rejected repeat cannot end the playing story"),Net.SkillActive(0x60) && Overlay(0,ActiveArt));
        Net.UpdateSkillStory(101);Tick();TestTrue(TEXT("Unchanged action instance stays active"),Net.SkillActive(0x60));
        Notice(TEXT("skill_toggled"),0,0x60,true);Net.UpdateSkillStory(102);Tick();
        TestTrue(TEXT("Same-behavior replacement ends the old instance and restores toggle"),!Net.SkillBusy(0x60) && Overlay(0,ToggledArt));
        Notice(TEXT("skill_toggled"),0,0x60,false);
        Notice(TEXT("skill_requested"),32,0x60);Net.StartSkillStory(32,0x410000b9,103);Net.UpdateSkillStory(0);Tick();
        TestFalse(TEXT("Completion or stop releases active state"),Net.SkillBusy(0x60));
        Notice(TEXT("skill_requested"),33,0x60);Net.StartSkillStory(33,0xdeadbeef,0);Tick();
        TestFalse(TEXT("Failed animation cannot strand a requested marker"),Net.SkillBusy(0x60));
        Net.StartSkillStory(99,0x410000b9,104);Tick();TestFalse(TEXT("Unknown story cannot activate a shortcut"),Net.SkillActive(0x60));
        Notice(TEXT("skill_requested"),0,0x60);Net.StartSkillStory(0,0x410000b9,105);Tick();
        TestTrue(TEXT("Counter wrap to zero remains usable"),Net.SkillActive(0x60));
        Notice(TEXT("skill_finished"),0,0);TestFalse(TEXT("Server cancellation clears the matching active request"),Net.SkillBusy(0x60));
        auto Remove=MakeShared<FJsonObject>();Remove->SetStringField(TEXT("event"),TEXT("skill_remove"));Remove->SetNumberField(TEXT("id"),0x60);Net.Handle(Remove);Tick();
        TestFalse(TEXT("Removing a skill removes its timer"),Shaded(0));
        Notice(TEXT("skill_requested"),20,0x59);Notice(TEXT("skill_toggled"),0,0x59,true);Net.StartSkillStory(20,0x410000b6,106);
        Net.ClearWorld();Tick();TestFalse(TEXT("Disconnect removes stale equipment shade"),Shaded(0));
        TestEqual(TEXT("Disconnect clears shared recovery"),Net.SharedSkillReset.Started,-1.);
        TestTrue(TEXT("Disconnect clears request, active and toggle state"),Net.PendingSkills.IsEmpty() && Net.ToggledSkills.IsEmpty() && !Net.ActiveSkillRequest.IsSet());
    }
    Mode->Network.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2VerticalMeterTest,"AC2.Data.NativeShortcutMeter",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2VerticalMeterTest::RunTest(const FString&)
{
    FAC2NativeUI UI;FString Error;
    if(!TestTrue(TEXT("Native UI"),UI.Open(AC2::ResolveDatDirectory(TEXT("")),Error)))return false;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    UI.Visibility.Add(T(TEXT("TimeLeftMeter")),true);
    for(float Value:{0.f,.5f,1.f}){
        UI.Begin({1600,900});UI.Meters.Add(T(TEXT("TimeLeftMeter")),Value);
        UI.Template(0x22000001,TEXT("ShortcutTemplate"),FIntRect(100,100,132,132),FIntRect(100,108,132,132));
        const auto* Fill=UI.Find(TEXT("bottom_fill"));
        if(Value==1){TestNull(TEXT("Completed native shade has no visible fill"),Fill);continue;}
        if(!TestNotNull(TEXT("Incomplete shade renders DAT fill"),Fill))continue;
        TestEqual(TEXT("Native fill moves downward within its clipped row"),Fill->Rect.Min.Y,100+FMath::RoundToInt(32*Value));
        TestEqual(TEXT("Shade respects partial hotbar row clip"),Fill->Clip.Min.Y,FMath::Max(108,100+FMath::RoundToInt(32*Value)));
        TestEqual(TEXT("Moving fill cannot bleed into next row"),Fill->Clip.Max.Y,132);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CooldownMathTest,"AC2.Data.NativeSkillCooldown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2CooldownMathTest::RunTest(const FString&)
{
    const FAC2SkillCooldown Skill{100.,5.},Shared{103.,1.};
    const auto Reset=FAC2SkillCooldown::Longest(Skill,Shared,103.5);
    TestEqual(TEXT("Longer skill cooldown wins"),Reset.Remaining(103.5),1.5);
    TestEqual(TEXT("Native fill progresses from covered to clear"),Reset.Position(103.5),.7f);
    TestEqual(TEXT("Exact deadline expires"),Reset.Position(105.),1.f);
    TestEqual(TEXT("Delayed delivery uses timestamp, not receipt time"),Reset.Remaining(104.5),.5);
    TestEqual(TEXT("Shared reset uses its own denominator"),FAC2SkillCooldown::Longest({100.,2.},Shared,103.5).Position(103.5),.5f);
    TestEqual(TEXT("Small clock lag cannot overflow the fill"),Skill.Position(99.),0.f);
    TestEqual(TEXT("Absent timestamp never fabricates a cooldown"),FAC2SkillCooldown{-1.,5.}.Remaining(0.),0.);
    TestEqual(TEXT("Invalid duration cannot shade a skill forever"),FAC2SkillCooldown{100.,INFINITY}.Remaining(101.),0.);
    return true;
}
#endif
