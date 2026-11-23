#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShortcutSyncTest,"AC2.Data.ShortcutSync",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShortcutSyncTest::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Shortcut tester");
    FAC2CharacterProfile Other;Other.Id=FGuid::NewGuid().ToString();Other.Name=TEXT("Other shortcut tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.Characters.Add(Other);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Net.ShortcutScope=TEXT("sync-test");
    const FString Legacy=TEXT("AC2.Shortcuts.")+Player.Id;
    const FString Saved=FAC2Shortcut::ForText(TEXT("/say My existing shortcut")).Encode();
    GConfig->SetString(*Legacy,TEXT("SlotsV2"),*Saved,GGameUserSettingsIni);
    void* Commands=nullptr;
    ON_SCOPE_EXIT{
        for(const auto& Id:{Player.Id,Other.Id})for(const auto* Prefix:{TEXT("AC2.Shortcuts."),TEXT("AC2.Shortcuts.sync-test."),TEXT("AC2.Shortcuts.other-server.")})
            GConfig->EmptySection(*(Prefix+Id),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    if(!TestTrue(TEXT("Capture native bridge IPC"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))return false;
    auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
    auto Profile=[&](TArray<TSharedPtr<FJsonValue>> Items){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("shortcuts"));J->SetArrayField(TEXT("items"),Items);Net.Handle(J);};
    auto Skill=[](double Value){auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("kind"),TEXT("skill"));B->SetNumberField(TEXT("value"),Value);return MakeShared<FJsonValueObject>(B);};
    auto UI=SNew(SAC2GameUI).Mode(Mode);UI->LoadShortcuts();
    TestTrue(TEXT("Existing local bar remains usable while profile is pending"),UI->Shortcuts[0].Text==TEXT("/say My existing shortcut") && Read().IsEmpty());
    Profile({Skill(176)});UI->LoadShortcuts();const auto Imported=Read();
    TestTrue(TEXT("Local bar imports once via native updates"),Imported.Contains(TEXT("shortcut")) && Imported.Contains(TEXT("My existing shortcut")) && Net.ServerShortcuts[0]==UI->Shortcuts[0]);
    FString Backup;GConfig->GetString(*Legacy,TEXT("SlotsV2"),Backup,GGameUserSettingsIni);
    TestEqual(TEXT("Legacy data retained as backup"),Backup,Saved);
    UI->Shortcuts[7]=FAC2Shortcut::ForText(TEXT("/say café, 世界"));
    UI->Shortcuts[98]=FAC2Shortcut::ForRecipe(0x6f000001);UI->Shortcuts[99]=FAC2Shortcut::ForItem(TEXT("9308940437337407489"));
    UI->SaveShortcuts();const auto Changes=Read();
    TestTrue(TEXT("Only changed slots uploaded with exact item identity"),Changes.Contains(TEXT("9308940437337407489")) && Changes.Contains(TEXT("recipe")) && Changes.Contains(TEXT("café, 世界")) && !Changes.Contains(TEXT("My existing shortcut")));
    UI->ShortcutSet=8;UI->ShortcutRows=3;UI->SaveShortcuts();
    TestTrue(TEXT("Paging and resizing do not resend bindings"),Read().IsEmpty());
    const uint64 Revision=Net.ShortcutRevision;Profile({Skill(-1)});
    TestTrue(TEXT("Malformed profile cannot partially replace bindings"),Net.ShortcutRevision==Revision && Net.ServerShortcuts[99].Item==TEXT("9308940437337407489"));
    for(int32 I=0;I<32;++I)UI->Shortcuts[I]=FAC2Shortcut::ForText(FString::ChrN(512,TEXT('x')));
    UI->SaveShortcuts();
    TestTrue(TEXT("Oversized edits are rejected before any network write"),Read().IsEmpty() && UI->Shortcuts[0]==Net.ServerShortcuts[0] && Mode->Notice.Contains(TEXT("16 KiB")));
    Net.ClearWorld();Net.Phase=TEXT("entering");Profile({});UI->LoadShortcuts();
    TestTrue(TEXT("Incoming bar waits until world entry"),!UI->Shortcuts[0].IsEmpty() && Read().IsEmpty());
    Net.PlayerId=Player.Id;Net.Phase=TEXT("world");UI->LoadShortcuts();
    bool Empty=true;for(const auto& B:UI->Shortcuts)Empty&=B.IsEmpty();
    TestTrue(TEXT("Reconnect restores intentionally empty server bar without importing again"),Empty && Read().IsEmpty());
    UI->Shortcuts[5]=FAC2Shortcut::ForSkill(96);UI->SaveShortcuts();Read();UI->Shortcuts[5]={};UI->SaveShortcuts();
    TestTrue(TEXT("Clearing final slot uploads an explicit empty binding"),Read().Contains(TEXT("empty")) && Net.ServerShortcuts[5].IsEmpty());
    auto Enum=MakeShared<FJsonObject>();Enum->SetStringField(TEXT("kind"),TEXT("enum"));Enum->SetNumberField(TEXT("value"),0);
    Profile({MakeShared<FJsonValueObject>(Enum)});UI->LoadShortcuts();UI->SaveShortcuts();
    TestTrue(TEXT("Native enum shortcut survives without being erased or resent"),UI->Shortcuts[0].IsEnumeration && Read().IsEmpty() && FAC2Shortcut::Decode(UI->Shortcuts[0].Encode())==UI->Shortcuts[0]);
    Mode->Characters.ActiveId=Other.Id;Net.ClearWorld();Net.Phase=TEXT("world");Net.PlayerId=Other.Id;
    Profile({Skill(0x166)});UI->LoadShortcuts();
    TestTrue(TEXT("New character uses its server profile"),UI->Shortcuts[0].Skill==0x166 && UI->Shortcuts[5].IsEmpty() && Read().IsEmpty());
    Mode->Characters.ActiveId=Player.Id;Net.ClearWorld();Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    Profile({Skill(0x60)});UI=SNew(SAC2GameUI).Mode(Mode);UI->LoadShortcuts();
    TestTrue(TEXT("Fresh UI uses server profile after one-time migration"),UI->Shortcuts[0].Skill==0x60 && Read().IsEmpty());
    const FString Owner=UI->ShortcutOwner;Net.ShortcutScope=TEXT("other-server");UI->LoadShortcuts();
    TestTrue(TEXT("Local cache is scoped by connection"),UI->ShortcutOwner!=Owner);
    Net.Phase=TEXT("leaving");auto Cancel=MakeShared<FJsonObject>();
    Cancel->SetStringField(TEXT("event"),TEXT("logout_cancelled"));Cancel->SetStringField(TEXT("message"),TEXT("Preference processing was not confirmed."));
    Net.Handle(Cancel);TestTrue(TEXT("An unconfirmed logout keeps the world session usable"),Net.IsWorld() && Net.PlayerId==Player.Id && Mode->Notice==TEXT("Preference processing was not confirmed."));
    return true;
}
#endif
