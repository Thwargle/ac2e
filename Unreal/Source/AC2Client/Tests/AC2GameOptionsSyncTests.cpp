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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2GameOptionsSyncTest,"AC2.Data.GameOptionsSync",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2GameOptionsSyncTest::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile A;A.Id=FGuid::NewGuid().ToString();A.Name=TEXT("Options tester");
    FAC2CharacterProfile B;B.Id=FGuid::NewGuid().ToString();B.Name=TEXT("Other options tester");
    Mode->Characters.Characters.Add(A);Mode->Characters.Characters.Add(B);Mode->Characters.ActiveId=A.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("entering");Net.PlayerId=A.Id;Net.ShortcutScope=TEXT("options-test");
    TOptional<FConfigSection> Original;
    if(const auto* Section=GConfig->GetSection(TEXT("AC2.Options"),false,GGameUserSettingsIni))Original=*Section;
    void* Commands=nullptr;
    ON_SCOPE_EXIT{
        GConfig->EmptySection(TEXT("AC2.Options"),GGameUserSettingsIni);
        if(Original.IsSet())for(const auto& Entry:Original.GetValue())
            GConfig->AddToSection(TEXT("AC2.Options"),Entry.Key,Entry.Value.GetSavedValue(),GGameUserSettingsIni);
        for(const auto& Id:{A.Id,B.Id})GConfig->EmptySection(*(TEXT("AC2.Shortcuts.options-test.")+Id),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    if(!TestTrue(TEXT("Capture options bridge IPC"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))return false;
    auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
    auto Profile=[&](uint32 Fields,uint32 Bits=0,uint32 Radar=0,int32 Height=1,float Range=-1.f){
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("game_options"));J->SetNumberField(TEXT("fields"),Fields);
        J->SetNumberField(TEXT("bits"),Bits);J->SetNumberField(TEXT("radar"),Radar);J->SetNumberField(TEXT("height"),Height);J->SetNumberField(TEXT("range"),Range);Net.Handle(J);
    };
    auto UI=SNew(SAC2GameUI).Mode(Mode);UI->RestoreOptions();const auto Defaults=UI->AppliedOptions;
    Profile(FAC2GameOptions::All,0x80014024,0x80000021,4,-1);UI->LoadShortcuts();
    TestTrue(TEXT("Entering profile waits for a ready owned character"),!UI->LoadedGameOptionsSession.IsValid() && Read().IsEmpty());
    Net.Phase=TEXT("world");UI->LoadShortcuts();
    TestTrue(TEXT("Native auto-target preference restored"),Net.AutoTarget && UI->AppliedOptions[34]==1);
    TestTrue(TEXT("Native flags and height restore without an upload"),UI->DamageNumbers && !UI->ThirdPartyDamage && !UI->ShowShortcutNumbers && !UI->ShortcutLocked && !UI->ShortcutInstantScroll && UI->ShortcutRows==4 && UI->MapUI.GetRadarMask()==0x21 && Read().IsEmpty());
    TestEqual(TEXT("Native unset range uses the slider default"),UI->AppliedOptions[21],1.f);
    UI->OptionValues[20]=1; // Deliberately unapplied.
    UI->SetRadarCoordinates(true);
    TestTrue(TEXT("Immediate radar action preserves draft and unrelated native bits"),Read().Contains(TEXT("game_options")) && UI->OptionValues[20]==1 && UI->AppliedOptions[20]==0 && Net.ServerGameOptions.Bits==0x80014034 && Net.ServerGameOptions.Radar==0x80000021 && Net.ServerGameOptions.Range==-1);
    Profile(0x20,0,0x40000002);UI->LoadShortcuts();
    TestTrue(TEXT("Partial profile preserves other fields and pending edits"),UI->OptionValues[20]==1 && UI->AppliedOptions[20]==0 && UI->ShortcutRows==4 && Net.ServerGameOptions.Range==-1 && UI->MapUI.GetRadarMask()==2 && Read().IsEmpty());
    UI->OptionValues[33]=2;UI->ApplyOptions(2);
    TestTrue(TEXT("Apply commits the draft through the server path"),UI->ThirdPartyDamage && Net.ServerGameOptions.Bits==0x8001403c && Net.ServerGameOptions.Range==-1 && Read().Contains(TEXT("game_options")));
    TestTrue(TEXT("Height choice applies and uploads the native profile field"),UI->ShortcutRows==2 && Net.ServerGameOptions.Height==2);
    UI->ShortcutRows=5;UI->RefreshShortcutHeightOption(true);UI->SaveShortcuts();
    TestTrue(TEXT("Hotbar grip persists height without normalizing native values"),Net.ServerGameOptions.Height==5 && Net.ServerGameOptions.Range==-1 && Net.ServerGameOptions.Radar==0x40000002 && Read().Contains(TEXT("game_options")));
    UI->ShortcutSet=7;UI->SaveShortcuts();
    TestTrue(TEXT("Paging and unchanged options do not resend"),Read().IsEmpty());
    UI->SetShortcutNumbers(true);
    TestTrue(TEXT("Shortcut command persists the native number flag"),(Net.ServerGameOptions.Bits & 1)!=0 && Read().Contains(TEXT("game_options")));
    const uint64 Revision=Net.GameOptionsRevision;
    Profile(FAC2GameOptions::All,0,0,0);Profile(8,0,0,1,-.25);Profile(0xffffffff);
    TestTrue(TEXT("Invalid profile cannot partly overwrite preferences"),Net.GameOptionsRevision==Revision && Net.ServerGameOptions.Height==5 && Net.ServerGameOptions.Range==-1);
    Profile(8,0,0,1,2.5f);UI->LoadShortcuts();UI->SetShortcutNumbers(false);
    TestTrue(TEXT("Range beyond slider domain survives an unrelated edit"),Net.ServerGameOptions.Range==2.5f && Read().Contains(TEXT("game_options")));
    UI->OptionValues[21]=.25f;UI->ApplyOptions(2);
    TestTrue(TEXT("Changing range uploads the float value"),Net.ServerGameOptions.Range==.25f && Read().Contains(TEXT("game_options")));
    Mode->Characters.ActiveId=B.Id;Net.ClearWorld();Net.Phase=TEXT("world");Net.PlayerId=B.Id;Profile(0);UI->LoadShortcuts();
    for(int32 I:{19,20,21,23,24,25,26,27,28,29,30,31,32,34})
        TestEqual(TEXT("Unsaved character starts from local defaults, not previous character"),UI->AppliedOptions[I],Defaults[I]);
    TestTrue(TEXT("Absent profile is not uploaded on login"),Read().IsEmpty() && UI->ShortcutRows==1);
    UI->SetShortcutNumbers(!UI->ShowShortcutNumbers);
    TestTrue(TEXT("First explicit edit initializes all four native fields"),Read().Contains(TEXT("game_options")) && Net.ServerGameOptions.Fields==FAC2GameOptions::All && Net.ServerGameOptions.Range==Defaults[21]);
    Mode->Characters.ActiveId=A.Id;Net.ClearWorld();Net.Phase=TEXT("world");Net.PlayerId=A.Id;
    Profile(FAC2GameOptions::All,0x8001403c,0x40000002,5,.25f);UI->LoadShortcuts();
    TestTrue(TEXT("Reconnect restores character-specific options"),UI->ThirdPartyDamage && !UI->ShowShortcutNumbers && UI->ShortcutRows==5 && UI->AppliedOptions[21]==.25f && Read().IsEmpty());
    UI->ShowOptions=true;UI->OptionAction(TEXT("OptionsTab2"));
    TestFalse(TEXT("Native auto-target label resolves"),UI->Native.Localized(TEXT("ID_Option_AutoTargetText")).IsEmpty());
    TestFalse(TEXT("Native auto-target help resolves"),UI->Native.Localized(TEXT("ID_Option_AutoTargetText_Help")).IsEmpty());
    UI->OptionAction(TEXT("OptionPlus34"));
    TestTrue(TEXT("Auto-target draft does not change combat selection behavior"),Net.AutoTarget && UI->OptionValues[34]==0 && Read().IsEmpty());
    UI->ApplyOptions(2);
    TestTrue(TEXT("Auto-target applies and saves only its native bit"),!Net.AutoTarget && Net.ServerGameOptions.Bits==0x8001401c && Read().Contains(TEXT("game_options")));
    UI->OptionAction(TEXT("OptionPlus34"));UI->OptionAction(TEXT("OptionsRecall"));UI->AnswerOptionConfirmation(true);
    TestTrue(TEXT("Revert keeps applied auto-target off"),!Net.AutoTarget && UI->OptionValues[34]==0 && Read().IsEmpty());
    Profile(0x80,0x8001403c);
    TestTrue(TEXT("Incoming profile is effective before HUD refresh"),Net.AutoTarget);
    UI->LoadShortcuts();TestEqual(TEXT("HUD follows authoritative auto-target update"),UI->AppliedOptions[34],1.f);
    return true;
}
#endif
