#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "AC2SkillData.h"
#include "AC2WorldGrid.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AutoTargetTest,"AC2.Data.AutoTarget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AutoTargetTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    void* Commands=nullptr;
    ON_SCOPE_EXIT{
        FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();Mode->Stream.Reset();PC->UnPossess();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    if(!TestTrue(TEXT("Capture outgoing selection"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))return false;
    Mode->Stream=MakeUnique<FAC2WorldStream>();Mode->Stream->Radius=0;FString Error;
    if(!TestTrue(TEXT("Open real target visibility"),Mode->Stream->Open(World,Mode->DatDirectory,Error)))return false;
    const FVector Focus=AC2::BlockOrigin(0x77b8)+FVector(8000,8000,200000);Player->SetActorLocation(Focus);
    const double Deadline=FPlatformTime::Seconds()+20;
    while(!Mode->CellVisible(0x77b80001) && FPlatformTime::Seconds()<Deadline){Mode->Stream->Tick(Focus);FPlatformProcess::Sleep(.001f);}
    if(!TestTrue(TEXT("Target scene is visible"),Mode->CellVisible(0x77b80001)))return false;
    Net.PlayerId=TEXT("101");Net.Phase=TEXT("world");Net.Cell=0x77b80001;
    const FString Enemy=TEXT("9313444029402382339"),Other=TEXT("9313444029402382340");
    for(const auto& Id:{Net.PlayerId,Enemy,Other}){
        auto O=MakeUnique<FAC2Network::FObject>();O->Instance=7;O->Cell=0x77b80001;O->Placed=true;O->Name=Id;
        auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);Root->RegisterComponent();Actor->SetActorLocation(Focus+FVector(200,0,0));O->Actor=Actor;
        Net.Objects.Add(Id,MoveTemp(O));
    }
    auto Story=[&](uint32 Skill,const FString& Attacker,const FString& Owner,int32 Stamp=7,const FString& Target=TEXT("101")){
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("story_target"));J->SetStringField(TEXT("id"),Target);
        J->SetStringField(TEXT("owner"),Owner);J->SetStringField(TEXT("attacker"),Attacker);
        J->SetNumberField(TEXT("instance"),Stamp);J->SetNumberField(TEXT("skill"),Skill);Net.Handle(J);
    };
    auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
    Story(0x59,Enemy,Enemy);
    TestEqual(TEXT("Flame Bolt selects its attacker without needing damage text"),Net.TargetId,Enemy);
    const auto Sent=Read();TestTrue(TEXT("Exact 64-bit attacker is sent through normal target request"),Sent.Contains(Enemy) && Sent.Contains(TEXT("target")));
    TestFalse(TEXT("Auto selection does not attack or switch stance"),Net.Combat || Sent.Contains(TEXT("attack")) || Sent.Contains(TEXT("combat")));
    Story(6,Other,Other);TestTrue(TEXT("Another attacker cannot steal the current selection"),Net.TargetId==Enemy && Read().IsEmpty());
    TestEqual(TEXT("Last attacker still updates with an existing target"),Net.LastAttackerId,Other);
    Net.Objects[Enemy]->Health=0;Story(6,Other,Other);
    TestTrue(TEXT("Selected corpse remains selected for looting"),Net.TargetId==Enemy && Read().IsEmpty());
    Net.TargetId.Reset();Net.AutoTarget=false;Story(6,Enemy,Enemy);
    TestTrue(TEXT("Disabled auto target remembers attacker without selecting"),Net.TargetId.IsEmpty() && Net.LastAttackerId==Enemy && Read().IsEmpty());
    Net.AutoTarget=true;Story(0x55,Other,Other);Story(0x139,Other,Other);Story(0xffff,Other,Other);
    TestTrue(TEXT("Healing, recall and unknown skills do not target or replace last attacker"),Net.TargetId.IsEmpty() && Net.LastAttackerId==Enemy && Read().IsEmpty());
    Story(6,Other,Other,6);Story(6,Other,TEXT("missing"));Story(6,Other,Other,7,Enemy);Story(6,Net.PlayerId,Net.PlayerId);
    TestTrue(TEXT("Stale, removed, third-party and self stories cannot select"),Net.TargetId.IsEmpty() && Read().IsEmpty());
    Net.Dead=true;Story(6,Other,Other);Net.Dead=false;
    TestTrue(TEXT("Dead player does not auto select"),Net.TargetId.IsEmpty() && Read().IsEmpty());
    Net.Objects[Other]->RadarSelectable=false;Story(6,Other,Other);
    TestTrue(TEXT("Explicitly unselectable actor stays unselected"),Net.TargetId.IsEmpty() && Read().IsEmpty());
    Net.Objects[Other]->RadarSelectable=true;Net.Objects[Other]->Actor->SetActorHiddenInGame(true);Story(6,Other,Other);
    TestTrue(TEXT("Hidden actor stays unselected"),Net.TargetId.IsEmpty() && Read().IsEmpty());
    Net.Objects[Other]->Actor->SetActorHiddenInGame(false);
    // Victim-owned stories carry the victim's instance, not the attacker's.
    Net.Objects[Other]->Instance=12;Story(6,Other,Net.PlayerId);
    TestTrue(TEXT("Basic melee miss uses victim stamp and selects attacker"),Net.TargetId==Other && Read().Contains(Other));
    TestTrue(TEXT("Native melee and Flame Bolt flags are harmful"),Net.CombatSkillData->Get(6).Harmful && Net.CombatSkillData->Get(0x59).Harmful);
    TestFalse(TEXT("Native Ease Suffering flag is helpful"),Net.CombatSkillData->Get(0x55).Harmful);
    // These are the native monster fixtures validated by RetailRadar. Entity
    // numbers are reassigned between the stock and Icefire script catalogs.
    const uint32 Monster=Net.SelectionLook(0x47000531).Category==AC2Radar::Players?0x47000676:0x4700053f;
    Net.Objects[Enemy]->Entity=Net.Objects[Other]->Entity=Monster;
    Net.Objects[Enemy]->Health=Net.Objects[Other]->Health=-1;
    TestEqual(TEXT("Replacement eligibility uses installed monster inheritance"),Net.SelectionLook(Monster).Category,uint32(AC2Radar::Monsters));
    auto Quality=[&](const FString& Id,uint32 Table,uint32 Key,int32 Value,int32 Stamp){
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("quality"));J->SetStringField(TEXT("id"),Id);
        J->SetNumberField(TEXT("table"),Table);J->SetNumberField(TEXT("key"),Key);J->SetNumberField(TEXT("value"),Value);
        J->SetNumberField(TEXT("instance"),Stamp);Net.Handle(J);
    };
    auto Remove=[&](const FString& Id,int32 Stamp){
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("remove"));J->SetStringField(TEXT("id"),Id);
        J->SetNumberField(TEXT("instance"),Stamp);Net.Handle(J);
    };
    Net.TargetId=Other;Net.Combat=true;
    Quality(Other,4,0x102,0,12);
    TestTrue(TEXT("Death notice alone keeps corpse selected without outgoing commands"),Net.TargetId==Other && Read().IsEmpty());
    Quality(Other,1,0x270,0,11);Remove(Other,11);
    TestTrue(TEXT("Stale selectability and deletion cannot clear or retarget"),Net.TargetId==Other && Net.Objects.Contains(Other) && Read().IsEmpty());
    Quality(Other,1,0x270,0,12);
    const auto ReplacementCommands=Read();
    TestTrue(TEXT("Unselectable corpse replaced by a monster with health not yet published"),Net.TargetId==Enemy && Net.Combat);
    TestTrue(TEXT("Unselectable notice clears, stops old target, then selects replacement"),
        ReplacementCommands.Find(TEXT("\"id\":\"0\""))<ReplacementCommands.Find(TEXT("stop_target")) &&
        ReplacementCommands.Find(TEXT("stop_target"))<ReplacementCommands.Find(Enemy));
    Net.SelectTarget(Other);TestTrue(TEXT("Mouse or programmatic picking cannot select unselectable actors"),Net.TargetId==Enemy && Read().IsEmpty());
    Quality(Other,1,0x270,1,12);Net.Objects[Other]->Health=60;
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(100,0,0));
    TestEqual(TEXT("Nearest candidate wins"),Net.ReplacementTarget(TEXT("missing")),Other);
    Net.Objects[Other]->Summoner=Net.PlayerId;
    TestEqual(TEXT("Summoned companions are excluded from PvE replacement"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Summoner.Reset();Net.Objects[Other]->Entity=0;
    TestEqual(TEXT("Scenery or unknown classes never replace foes"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Entity=Monster;Net.Objects[Other]->Actor->SetActorHiddenInGame(true);
    TestEqual(TEXT("Hidden foes are excluded"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Actor->SetActorHiddenInGame(false);Net.Objects[Other]->Health=0;
    TestEqual(TEXT("Dead foes are excluded"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Health=60;
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(100,0,100));
    TestEqual(TEXT("Native vertical penalty beats Euclidean nearest"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(200,0,0));
    TestEqual(TEXT("Equal distance chooses the lower full unsigned IID"),Net.ReplacementTarget(TEXT("missing")),Enemy);
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(7400,0,0));
    TestEqual(TEXT("Outdoor replacement includes 74 metres"),Net.ReplacementTarget(Enemy),Other);
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(7600,0,0));
    TestTrue(TEXT("Outdoor replacement excludes beyond radar's 75 metres"),Net.ReplacementTarget(Enemy).IsEmpty());
    Net.Objects[Other]->Actor->SetActorLocation(Focus+FVector(200,0,0));
    Net.TargetId=Other;Net.AutoTarget=false;Quality(Other,1,0x270,0,12);
    const auto DisabledCommands=Read();
    TestTrue(TEXT("Disabled auto target clears and stops an unselectable target without replacing"),
        Net.TargetId.IsEmpty() && DisabledCommands.Contains(TEXT("stop_target")) && !DisabledCommands.Contains(Enemy) && Net.Combat);
    Quality(Other,1,0x270,1,12);Net.AutoTarget=true;Net.TargetId=Other;
    Remove(Other,12);const auto RemovedCommands=Read();
    TestTrue(TEXT("Deletion selects the remaining foe without changing stance"),Net.TargetId==Enemy && Net.Combat && !Net.Objects.Contains(Other));
    TestTrue(TEXT("Deletion stops the old target before requesting replacement"),
        RemovedCommands.Contains(TEXT("stop_target")) && RemovedCommands.Find(TEXT("stop_target"))<RemovedCommands.Find(Enemy));
    Remove(Enemy,7);const auto EmptyCommands=Read();
    TestTrue(TEXT("No remaining foe clears selection and preserves stance"),Net.TargetId.IsEmpty() && Net.Combat && EmptyCommands.Contains(TEXT("stop_target")));
    Net.ClearWorld();TestTrue(TEXT("Leaving clears last attacker and selection"),Net.LastAttackerId.IsEmpty() && Net.TargetId.IsEmpty() && Net.AutoTarget);
    return true;
}
#endif
