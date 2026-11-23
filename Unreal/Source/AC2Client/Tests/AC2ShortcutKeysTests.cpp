#include "AC2ShortcutKeys.h"
#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShortcutKeysTest,"AC2.Data.ShortcutKeys",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShortcutKeysTest::RunTest(const FString&)
{
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FString Error;TArray<FAC2ShortcutKey> Keys;
    if(!TestTrue(TEXT("Default keymaps resolve through installed enum maps"),FAC2ShortcutKeys::Load(Directory,Keys,Error))){AddError(Error);return false;}
    TestEqual(TEXT("Only the thirty-two native hotbar bindings are loaded"),Keys.Num(),32);
    const FKey Digits[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine,EKeys::Zero};
    auto Mapping=[&](FKey Key,bool Shift,bool Ctrl){const auto* Found=Keys.FindByPredicate([&](const auto& K){return K.Chord==FInputChord(Key,Shift,Ctrl,false,false);});return Found?Found->Action:0u;};
    for(int32 I=0;I<10;++I){
        TestEqual(TEXT("Plain number activates first row"),Mapping(Digits[I],false,false),0x4100002bu+I);
        TestEqual(TEXT("Ctrl number selects a page"),Mapping(Digits[I],false,true),0x41000035u+I);
        TestEqual(TEXT("Shift number activates second row"),Mapping(Digits[I],true,false),0x4100005fu+I);
    }
    TestEqual(TEXT("Minus advances"),Mapping(EKeys::Hyphen,false,false),0x4100003fu);
    TestEqual(TEXT("Shift minus reverses"),Mapping(EKeys::Hyphen,true,false),0x41000040u);
    FAC2DatArchive Portal;TArray<uint8> Bytes;TArray<FAC2ShortcutKey> Decoded;
    if(TestTrue(TEXT("Native keymap fixture opens"),Portal.Open(Directory/TEXT("portal.dat"),1,Error) && Portal.Read(0x1d000000,Bytes,Error))){
        for(const int32 Length:{0,4,20,Bytes.Num()-1}){
            Decoded=Keys;
            TestFalse(TEXT("Truncated keymap is rejected"),FAC2ShortcutKeys::Decode(MakeArrayView(Bytes.GetData(),Length),0x1d000000,false,false,Decoded));
            TestTrue(TEXT("Partial bindings cannot escape decoder"),Decoded.IsEmpty());
        }
        TestFalse(TEXT("Wrong resource identity is rejected"),FAC2ShortcutKeys::Decode(Bytes,0x1d000004,false,false,Decoded));
        Bytes.Add(0);TestFalse(TEXT("Trailing bytes are rejected"),FAC2ShortcutKeys::Decode(Bytes,0x1d000000,false,false,Decoded));
    }

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;Mode->DatDirectory=Directory;
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();
    FAC2KeyBindings Defaults;
    Defaults.Initialize(Keys);
    PC->ApplyGameplayKeys(Defaults);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Keyboard tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Net.Skills.Add({96,true,TEXT("0")});
    const FString Section=TEXT("AC2.Shortcuts.")+Player.Id;void* Commands=nullptr;
    if(TestTrue(TEXT("Capture keyboard commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);Cast<AAC2HUD>(PC->MyHUD)->GameUI=UI;
        UI->ShortcutInstantScroll=true;
        UI->Tick(FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform()),FPlatformTime::Seconds(),.016f);
        UI->LoadShortcuts();for(auto& Binding:UI->Shortcuts)Binding=FAC2Shortcut::ForSkill(96);
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Event=[&](FKey Key,EInputEvent Type){PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Type,Type==IE_Released?0.f:1.f));};
        auto Pump=[&](){PC->PlayerInput->ProcessInputStack({PC->InputComponent},.016f,false);};
        auto Press=[&](FKey Key,TArray<FKey> Modifiers=TArray<FKey>{},bool Repeat=false){
            for(const auto& M:Modifiers)Event(M,IE_Pressed);Event(Key,IE_Pressed);Pump();
            if(Repeat){Event(Key,IE_Repeat);Pump();}
            Event(Key,IE_Released);for(const auto& M:Modifiers)Event(M,IE_Released);Pump();
        };
        auto OneCast=[&](const FString& Data){TArray<FString> Lines;Data.ParseIntoArrayLines(Lines);return Lines.Num()==1 && Lines[0].Contains(TEXT("\"command\":\"skill\""));};
        for(int32 I=0;I<10;++I){Press(Digits[I],{EKeys::LeftControl});TestEqual(TEXT("Controller selects every page"),UI->ShortcutSet,I);}
        TestTrue(TEXT("Page keys never cast the occupied slot"),Read().IsEmpty());
        Press(EKeys::Hyphen);TestEqual(TEXT("Next wraps page ten to one"),UI->ShortcutSet,0);
        Press(EKeys::Hyphen,{EKeys::RightShift});TestEqual(TEXT("Previous wraps one to ten"),UI->ShortcutSet,9);
        Press(EKeys::Two,{EKeys::RightControl});TestEqual(TEXT("Right control uses the same retail meta mode"),UI->ShortcutSet,1);
        Press(EKeys::Zero,{},true);TestTrue(TEXT("First-row key casts exactly once despite repeat"),OneCast(Read()));
        Press(EKeys::Zero,{EKeys::LeftShift});TestTrue(TEXT("Second row cannot fire while hidden"),Read().IsEmpty());
        UI->ShortcutRows=2;
        Press(EKeys::Zero,{EKeys::LeftShift});TestTrue(TEXT("Visible second-row key casts exactly once"),OneCast(Read()));
        Press(EKeys::One,{EKeys::RightShift});TestTrue(TEXT("Right shift casts the second row"),OneCast(Read()));
        Press(EKeys::Zero,{EKeys::LeftControl});Press(EKeys::Zero,{EKeys::LeftShift});
        TestTrue(TEXT("Second-row end padding cannot cast a first-row fallback"),Read().IsEmpty());
        Press(EKeys::One,{EKeys::LeftControl,EKeys::LeftShift});Press(EKeys::One,{EKeys::LeftAlt});
        TestTrue(TEXT("Unmapped modifiers neither cast nor switch pages"),Read().IsEmpty() && UI->ShortcutSet==9);
        UI->Editing=true;
        Press(EKeys::One);Press(EKeys::One,{EKeys::LeftControl});Press(EKeys::Hyphen);
        TestTrue(TEXT("Chat protects activation and page selection"),Read().IsEmpty() && UI->ShortcutSet==9);UI->Editing=false;
        UI->Frontend=true;Press(EKeys::One,{EKeys::LeftControl});Press(EKeys::One);
        TestTrue(TEXT("Frontend cannot use gameplay shortcuts"),Read().IsEmpty() && UI->ShortcutSet==9);UI->Frontend=false;
        Mode->FrontEnd=true;Press(EKeys::Hyphen);Press(EKeys::One);Mode->FrontEnd=false;
        TestTrue(TEXT("Character selection cannot use gameplay shortcuts"),Read().IsEmpty() && UI->ShortcutSet==9);
        UI->RestoreOptions();UI->ShowOptions=true;UI->AskOptionConfirmation(SAC2GameUI::EOptionConfirmation::Default);
        Press(EKeys::Hyphen);Press(EKeys::One);Press(EKeys::One,{EKeys::LeftControl});
        TestTrue(TEXT("Options confirmation blocks controller shortcut activation and paging"),Read().IsEmpty() && UI->ShortcutSet==9);
        UI->AnswerOptionConfirmation(false);UI->ShowOptions=false;
        Press(EKeys::Three,{EKeys::LeftControl});UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestEqual(TEXT("Keyboard page selection persists with the character"),UI->ShortcutSet,2);
        TestFalse(TEXT("Unrelated native actions are not intercepted"),UI->HandleShortcutAction(0x41000013));
        FAC2KeyBindings Changed=Defaults;
        const auto Find=[&](FName Id){return Changed.Rows.IndexOfByPredicate([&](const auto& R){return R.Id==Id;});};
        const int32 CastIndex=Find(TEXT("Shortcut4100002b")),Forward=Find(TEXT("Forward")),Alt=Find(TEXT("ForwardAlt"));
        const FInputChord NewCast(EKeys::F8,false,false,true,false);
        TestEqual(TEXT("All gameplay and retail shortcuts are exposed"),Changed.Rows.Num(),29+32);
        TestFalse(TEXT("An occupied movement key is rejected"),Changed.Assign(CastIndex,EKeys::W,Error));
        TestFalse(TEXT("Held axes reject modifier chords"),Changed.Assign(Forward,NewCast,Error));
        TestTrue(TEXT("An alternate may be unbound"),Changed.Assign(Alt,{},Error));
        TestTrue(TEXT("Movement can be rebound"),Changed.Assign(Forward,EKeys::K,Error));
        TestTrue(TEXT("The native hotbar action may use a new chord"),Changed.Assign(CastIndex,NewCast,Error));
        PC->ApplyGameplayKeys(Changed);
        const auto& Axes=PC->PlayerInput->GetKeysForAxis(TEXT("AC2Forward"));
        TestTrue(TEXT("Rebound movement replaces its old keys"),Axes.ContainsByPredicate([](const auto& A){return A.Key==EKeys::K;}) &&
            !Axes.ContainsByPredicate([](const auto& A){return A.Key==EKeys::W || A.Key==EKeys::Up;}));
        for(auto& Binding:UI->Shortcuts) Binding=FAC2Shortcut::ForSkill(96);
        Press(EKeys::One); Press(EKeys::F8);
        TestTrue(TEXT("The previous hotbar binding and incomplete chord do nothing"),Read().IsEmpty());
        Press(EKeys::F8,{EKeys::LeftAlt});
        TestTrue(TEXT("The remapped chord casts once through controller input"),OneCast(Read()));

        const FString Ini=FPaths::ConvertRelativePathToFull(FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(),TEXT("KeyBindings"),TEXT(".ini")));
        FFileHelper::SaveStringToFile(TEXT("[SectionsToSave]\nbCanSaveAllSections=true\n"),*Ini);
        GConfig->LoadFile(Ini);
        Changed.Save(Ini);
        GConfig->UnloadFile(Ini);
        GConfig->LoadFile(Ini);
        FAC2KeyBindings Reload=Defaults;
        Reload.Load(Ini);
        TestTrue(TEXT("Bindings survive disk reload, including unbound and modifiers"),Reload.Rows[CastIndex].Chord==NewCast &&
            Reload.Rows[Forward].Chord.Key==EKeys::K && !Reload.Rows[Alt].Chord.Key.IsValid());
        Reload.Reset(); Reload.Save(Ini); Changed.Load(Ini);
        TestTrue(TEXT("Restoring defaults removes overrides"),Changed.Rows[CastIndex].Chord.Key==EKeys::One &&
            Changed.Rows[Forward].Chord.Key==EKeys::W && Changed.Rows[Alt].Chord.Key==EKeys::Up);
        GConfig->UnloadFile(Ini);
        IFileManager::Get().Delete(*Ini);
        Cast<AAC2HUD>(PC->MyHUD)->GameUI.Reset();
    }
    GConfig->EmptySection(*Section,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
    FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
