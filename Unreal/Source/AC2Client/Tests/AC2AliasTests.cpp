#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "AC2MapData.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AliasesTest,"AC2.Data.Aliases",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AliasesTest::RunTest(const FString&)
{
    const auto Parsed=FAC2AliasRequest::Parse(TEXT(" /ALIAS ADD ;Ready \"/say Ready, Dereth!\" "));
    TestTrue(TEXT("Native add syntax preserves replacement case and normalizes name"),Parsed.Recognized && Parsed.Error.IsEmpty() &&
        Parsed.Name==TEXT(";ready") && Parsed.Text==TEXT("/say Ready, Dereth!"));
    TestEqual(TEXT("Abbreviated add"),FAC2AliasRequest::Parse(TEXT("/alias ;go /me goes" )).Text,FString(TEXT("/me goes")));
    TestEqual(TEXT("Clearlist is the retail clear synonym"),FAC2AliasRequest::Parse(TEXT("/alias clearlist")).Command,EAC2AliasCommand::Clear);
    TestEqual(TEXT("Native help topic"),FAC2AliasRequest::Parse(TEXT(" /HELP ALIAS ")).Command,EAC2AliasCommand::Help);
    TestFalse(TEXT("Unrelated commands are not intercepted"),FAC2AliasRequest::Parse(TEXT("/aliases list")).Recognized);
    for(const TCHAR* Bad:{TEXT("/alias"),TEXT("/alias add"),TEXT("/alias ;"),TEXT("/alias go Hi"),TEXT("/alias ;go"),
            TEXT("/alias ;go \"\""),TEXT("/alias ;go \"unfinished"),TEXT("/alias ;go a\nb"),TEXT("/alias list extra"),
            TEXT("/alias clear ;go"),TEXT("/alias remove"),TEXT("/alias remove ;a ;b"),TEXT("/alias shortcut ;a"),
            TEXT("/alias shortcut ;a 0"),TEXT("/alias shortcut ;a 11"),TEXT("/alias shortcut ;a 1x"),TEXT("/alias shortcut ;a 1 extra"),
            TEXT("/alias ;TARGET fake"),TEXT("/alias remove ;LOC")}){
        const auto R=FAC2AliasRequest::Parse(Bad);
        TestTrue(TEXT("Malformed or protected mutation is rejected"),R.Recognized && !R.Error.IsEmpty());
    }
    FAC2Aliases Aliases;
    TestTrue(TEXT("Case-insensitive definitions"),Aliases.Set(TEXT(";Greet"),TEXT("Hello, ;target! Meet me at ;loc.")) && Aliases.Contains(TEXT(";GREET")));
    TestFalse(TEXT("Strict aliases cannot be overridden"),Aliases.Set(TEXT(";Loc"),TEXT("Elsewhere")));
    Aliases.Set(TEXT(";t"),TEXT("/tell"));Aliases.Set(TEXT(";wave"),TEXT("/me waves to ;target"));
    Aliases.Set(TEXT(";go"),TEXT(";wave"));Aliases.Set(TEXT(";loop"),TEXT(";LOOP"));
    Aliases.Set(TEXT(";a"),TEXT(";b"));Aliases.Set(TEXT(";b"),TEXT(";A"));
    FString Expanded,Error;
    auto Expand=[&](const FString& Input,const FString& Target=TEXT("Ari"),const FString& Location=TEXT("33.3N, 8.7W")){
        TestTrue(TEXT("Bounded alias expansion succeeds"),Aliases.Expand(Input,Target,Location,Expanded,Error));return Expanded;
    };
    TestEqual(TEXT("Native embedded tokens stop at punctuation"),Expand(TEXT("/say [;GREET] ;greet2 ;greet_tail ;unknown ;")),
        FString(TEXT("/say [Hello, Ari! Meet me at 33.3N, 8.7W.] ;greet2 Hello, Ari! Meet me at 33.3N, 8.7W._tail ;unknown ;")));
    TestEqual(TEXT("A command prefix alias keeps its arguments"),Expand(TEXT(";T Ari, Hello!")),FString(TEXT("/tell Ari, Hello!")));
    TestEqual(TEXT("Nested aliases resolve every occurrence"),Expand(TEXT(";go ;target ;TARGET")),FString(TEXT("/me waves to Ari Ari Ari")));
    TestEqual(TEXT("Missing target stays literal"),Expand(TEXT(";target"),TEXT("")),FString(TEXT(";target")));
    TestEqual(TEXT("Cycle terminates at the unresolved reference"),Expand(TEXT(";loop ;a")),FString(TEXT(";LOOP ;A")));
    TestEqual(TEXT("Definitions retain dynamic references"),Expand(TEXT("/alias ;later ;wave ;target")),FString(TEXT("/alias ;later ;wave ;target")));
    TestEqual(TEXT("Alias shortcuts retain their name"),Expand(TEXT("/alias shortcut ;wave 2")),FString(TEXT("/alias shortcut ;wave 2")));
    // Unknown strings and punctuation are not a shell or multi-command separator.
    TestEqual(TEXT("A bare semicolon is literal"),Expand(TEXT("/say Hello; /recall")),FString(TEXT("/say Hello; /recall")));
    Aliases.Set(TEXT(";large"),FString::ChrN(512,'x'));
    TestFalse(TEXT("Oversize expansion is rejected atomically"),Aliases.Expand(TEXT(";large;large;large;large;large"),{},{},Expanded,Error));
    TestTrue(TEXT("No partial command is returned"),Expanded.IsEmpty() && !Error.IsEmpty());
    Aliases.Set(TEXT(";e0"),TEXT("x"));
    for(int32 I=1;I<=10;++I)Aliases.Set(FString::Printf(TEXT(";e%d"),I),FString::Printf(TEXT(";e%d;e%d"),I-1,I-1));
    TestFalse(TEXT("Exponential alias work is bounded"),Aliases.Expand(TEXT(";e10"),{},{},Expanded,Error));
    TestFalse(TEXT("Control characters cannot inject another command"),Aliases.Expand(TEXT("/say x\n/recall"),{},{},Expanded,Error));
    const FString Unicode=TEXT("/say é Ω said:\"yes\"; [%], = | ;target");
    Aliases.Set(TEXT(";unicode"),Unicode);
    FAC2Aliases Reload;Reload.Decode(Aliases.Encode());
    TestEqual(TEXT("Saved aliases retain Unicode, quotes and INI delimiters"),Reload.Values.FindRef(TEXT(";unicode")),Unicode);
    TestEqual(TEXT("Canonical persistence roundtrip"),Reload.Encode(),Aliases.Encode());
    Reload.Decode(TEXT("garbage,t:O2xvYw==|t:ZmFrZQ==,t:O2JhZA==|t:YQo=,t:O3g=|t:SGk="));
    TestTrue(TEXT("Invalid and protected persisted entries are ignored"),Reload.Values.Num()==1 && Reload.Values.FindRef(TEXT(";x"))==TEXT("Hi"));

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    auto* Pawn=World->SpawnActor<APawn>();auto* Root=NewObject<USceneComponent>(Pawn);Pawn->SetRootComponent(Root);Root->RegisterComponent();
    Pawn->SetActorLocation(FVector(1861550,2719540,9390));PC->Possess(Pawn);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Alias tester");
    FAC2CharacterProfile Other;Other.Id=FGuid::NewGuid().ToString();Other.Name=TEXT("Other alias tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.Characters.Add(Other);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    auto Target=MakeUnique<FAC2Network::FObject>();Target->Name=TEXT("Green Wasp");Net.Objects.Add(TEXT("wasp"),MoveTemp(Target));Net.TargetId=TEXT("wasp");
    void* Commands=nullptr;
    ON_SCOPE_EXIT{
        for(const auto& Id:{Player.Id,Other.Id})for(const TCHAR* Prefix:{TEXT("AC2.Aliases."),TEXT("AC2.Shortcuts.")})
            GConfig->EmptySection(*(Prefix+Id),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    if(!TestTrue(TEXT("Capture actual bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))return false;
    const auto UI=SNew(SAC2GameUI).Mode(Mode);UI->ShortcutInstantScroll=true;
    const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
    auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};
    auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
    Tick();Read();UI->SelectShortcutSet(4);
    UI->Submit(TEXT("/help alias"));
    TestTrue(TEXT("Help displays the full native command reference locally"),Read().IsEmpty() &&
        UI->Log.Lines.ContainsByPredicate([](const auto& L){return L.Text==TEXT("/alias clearlist : Same as clear.");}));
    UI->Submit(TEXT("/alias ;WAVE /me greets ;target at ;loc"));
    TestEqual(TEXT("Definition is stored before resolving built-ins"),UI->Aliases.Values.FindRef(TEXT(";wave")),FString(TEXT("/me greets ;target at ;loc")));
    UI->Submit(TEXT("/alias shortcut ;WAVE 10"));
    TestTrue(TEXT("Native alias shortcut stores the name on the visible page"),UI->Shortcuts[49].Text==TEXT(";wave") && Read().IsEmpty());
    UI->ActivateShortcut(9);const FString First=Read();
    UI->Submit(TEXT("/me greets Green Wasp at ")+FAC2MapData::Coordinates(FAC2MapData::Landscape(Pawn->GetActorLocation())));
    TestEqual(TEXT("Alias activation uses the ordinary emote path and current coordinates"),First,Read());
    TestTrue(TEXT("An actual emote was emitted"),First.Contains(TEXT("Green Wasp")) && First.Contains(TEXT("emote")));
    Net.Objects[TEXT("wasp")]->Name=TEXT("Red Soldier Wasp");Pawn->SetActorLocation(Pawn->GetActorLocation()+FVector(20400,20400,0));
    UI->ActivateShortcut(9);const FString Updated=Read();
    TestTrue(TEXT("Built-in values update on every activation"),Updated.Contains(TEXT("Red Soldier Wasp")) && Updated!=First);
    TestEqual(TEXT("History remembers alias input"),UI->Log.History.Last(),FString(TEXT(";wave")));
    UI->Submit(TEXT("/alias ;wave /say ")+Unicode.Mid(5));UI->ActivateShortcut(9);
    const FString Rebound=Read();UI->Submit(Unicode);
    TestEqual(TEXT("Changing alias updates an existing shortcut"),Rebound,Read());
    UI->AliasOwner.Reset();UI->LoadAliases();UI->ShortcutOwner.Reset();UI->LoadShortcuts();
    TestTrue(TEXT("Aliases and their shortcuts reload independently"),UI->Aliases.Values.FindRef(TEXT(";wave"))==Unicode && UI->Shortcuts[49].Text==TEXT(";wave"));
    UI->Submit(TEXT("/alias shortcut ;missing 10"));UI->Submit(TEXT("/alias shortcut ;wave 11"));
    TestTrue(TEXT("Invalid assignments preserve existing binding"),UI->Shortcuts[49].Text==TEXT(";wave") && Read().IsEmpty());
    UI->Submit(TEXT("/alias list"));
    TestTrue(TEXT("List includes protected descriptions and saved definitions"),UI->Log.Lines.ContainsByPredicate([](const auto& L){return L.Text==TEXT(";target = The name of your current selection");}) &&
        UI->Log.Lines.Last().Text==TEXT(";wave = ")+Unicode && Read().IsEmpty());
    UI->Submit(TEXT("/alias ;next /say New value"));UI->Submit(TEXT("/alias ;wave ;next"));
    UI->ActivateShortcut(9);TestTrue(TEXT("Chained alias executes once"),Read().Contains(TEXT("New value")));
    UI->Submit(TEXT("/alias ;t /tell"));UI->Submit(TEXT(";t Ari, Hello!"));const auto Tell=Read();UI->Submit(TEXT("/tell Ari, Hello!"));
    TestTrue(TEXT("Prefix alias keeps tell destination and message"),!Tell.IsEmpty() && Tell==Read());
    UI->Submit(TEXT("/alias ;wipe /clear"));UI->Submit(TEXT(";wipe"));
    TestTrue(TEXT("Expanded local commands remain local"),UI->Log.Lines.IsEmpty() && Read().IsEmpty());
    UI->Submit(TEXT("/alias shortcut ;target 2"));
    TestEqual(TEXT("Protected aliases can be placed on hotbar"),UI->Shortcuts[41].Text,FString(TEXT(";target")));
    Net.TargetId=Player.Id;UI->ActivateShortcut(1);TestTrue(TEXT("Self target resolves to character name"),Read().Contains(Player.Name));
    UI->Submit(TEXT("/alias remove ;NEXT"));
    TestFalse(TEXT("Removal ignores case"),UI->Aliases.Contains(TEXT(";next")));
    Mode->Characters.ActiveId=Other.Id;UI->LoadShortcuts();UI->LoadAliases();
    TestTrue(TEXT("Aliases are isolated per character"),UI->Aliases.Values.IsEmpty() && UI->Shortcuts[49].IsEmpty());
    Mode->Characters.ActiveId=Player.Id;UI->LoadAliases();UI->LoadShortcuts();
    TestTrue(TEXT("Returning character retains aliases"),UI->Aliases.Contains(TEXT(";wave")) && !UI->Aliases.Contains(TEXT(";next")));
    UI->Submit(TEXT("/alias clearlist"));UI->AliasOwner.Reset();UI->LoadAliases();
    TestTrue(TEXT("Clear persists while preserving built-ins and shortcut references"),UI->Aliases.Values.IsEmpty() && UI->Aliases.Contains(TEXT(";target")) && UI->Shortcuts[49].Text==TEXT(";wave") && Read().IsEmpty());
    UI->ActivateShortcut(9);TestTrue(TEXT("A removed alias remains literal, never executes its old value"),Read().Contains(TEXT(";wave")));
    Mode->FrontEnd=true;UI->Submit(TEXT("/alias ;bad /say bad"));
    TestTrue(TEXT("Character selection cannot mutate aliases"),UI->Aliases.Values.IsEmpty() && Read().IsEmpty());
    return true;
}
#endif
