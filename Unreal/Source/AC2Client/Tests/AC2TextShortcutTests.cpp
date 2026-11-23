#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "AC2CharacterUI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TextShortcutsTest,"AC2.Data.TextShortcuts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TextShortcutsTest::RunTest(const FString&)
{
    const FString Message=TEXT("/tell Ari, Ready for Dereth? é Ω \"yes\"; [%]");
    const auto Binding=FAC2Shortcut::ForText(Message);
    TestFalse(TEXT("Text is a distinct occupied binding"),Binding.IsEmpty());
    TestTrue(TEXT("Unicode and punctuation survive delimiter-safe storage"),FAC2Shortcut::Decode(Binding.Encode())==Binding);
    TestFalse(TEXT("Encoded payload contains no list delimiter"),Binding.Encode().Contains(TEXT(",")));
    for(const TCHAR* Bad:{TEXT("t:%%%%"),TEXT("t:"),TEXT("t:AA=="),TEXT("t:/w=="),TEXT("t:YQ"),TEXT("t:YQo="),TEXT("t:IA==")})
        TestTrue(TEXT("Malformed/nonprintable text cannot become a saved command"),FAC2Shortcut::Decode(Bad).IsEmpty());
    TestFalse(TEXT("512-character text is supported"),FAC2Shortcut::ForText(FString::ChrN(512,'A')).IsEmpty());
    TestTrue(TEXT("Oversized text rejected"),FAC2Shortcut::ForText(FString::ChrN(513,'A')).IsEmpty());
    const auto Parsed=FAC2ShortcutRequest::Parse(TEXT(" /SHORTCUT text 10 \"/me waves to everyone\" "));
    TestTrue(TEXT("Native quoted syntax and one-based slot"),Parsed.Recognized && Parsed.Slot==9 && Parsed.Text==TEXT("/me waves to everyone") && Parsed.Error.IsEmpty());
    TestEqual(TEXT("Native abbreviated syntax"),FAC2ShortcutRequest::Parse(TEXT("/shortcut 1 Hello, Dereth!")).Slot,0);
    TestFalse(TEXT("Similarly named commands are not intercepted"),FAC2ShortcutRequest::Parse(TEXT("/shortcuts text 1 Hi")).Recognized);
    for(const TCHAR* Command:{TEXT(" /SHORTCUT NUMBERS ON "),TEXT("/shortcut numbers off")}){
        const auto R=FAC2ShortcutRequest::Parse(Command);
        TestTrue(TEXT("Native numbers command is distinct from a text binding"),R.Recognized && R.Error.IsEmpty() && R.ShowNumbers.IsSet() && R.Slot==INDEX_NONE && R.Text.IsEmpty());
    }
    TestTrue(TEXT("On and off parse as different states"),FAC2ShortcutRequest::Parse(TEXT("/shortcut numbers on")).ShowNumbers.GetValue() &&
        !FAC2ShortcutRequest::Parse(TEXT("/shortcut numbers off")).ShowNumbers.GetValue());
    for(const TCHAR* Bad:{TEXT("/shortcut"),TEXT("/shortcut text"),TEXT("/shortcut text 0 Hi"),TEXT("/shortcut text 11 Hi"),
            TEXT("/shortcut text -1 Hi"),TEXT("/shortcut text 1x Hi"),TEXT("/shortcut text 1"),TEXT("/shortcut text 1 \"\""),
            TEXT("/shortcut text 1 \"unclosed"),TEXT("/shortcut text 1 hello\n/recall"),TEXT("/shortcut numbers"),
            TEXT("/shortcut numbers maybe"),TEXT("/shortcut numbers off extra"),TEXT("/shortcut numbers on\n/say Surprise")}){
        const auto R=FAC2ShortcutRequest::Parse(Bad);
        TestTrue(TEXT("Invalid input is handled without a binding"),R.Recognized && R.Slot==INDEX_NONE && !R.Error.IsEmpty());
    }

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
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Text shortcut tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    const FString Section=TEXT("AC2.Shortcuts.")+Player.Id;
    FString OriginalNumbers;
    const bool HadNumbers=GConfig->GetString(TEXT("AC2.Options"),TEXT("Shortcut numbers"),OriginalNumbers,GGameUserSettingsIni);
    ON_SCOPE_EXIT{
        if(HadNumbers)GConfig->SetString(TEXT("AC2.Options"),TEXT("Shortcut numbers"),*OriginalNumbers,GGameUserSettingsIni);
        else GConfig->RemoveKey(TEXT("AC2.Options"),TEXT("Shortcut numbers"),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    };
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture real client bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);
        UI->ShortcutInstantScroll=true;UI->ShortcutLocked=true;
        const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Center=[&](FName Key){const auto* R=UI->Buttons.Find(Key);TestNotNull(TEXT("Native hit target"),R);
            return R?G.LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*UI->Scale):FVector2D::ZeroVector;};
        auto Up=[&](FVector2D P){UI->OnMouseButtonUp(G,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto Down=[&](FVector2D P,FKey Key=EKeys::LeftMouseButton){UI->OnMouseButtonDown(G,FPointerEvent(0,P,P,{Key},Key,0,FModifierKeysState()));};
        auto Drag=[&](FName From,FName To,bool Shift){const auto A=Center(From),B=Center(To);Down(A);
            UI->OnMouseMove(G,FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(Shift,false,false,false,false,false,false,false,false)));Up(B);};
        Tick();Read();UI->SelectShortcutSet(4);UI->ShortcutRows=2;Tick();
        const auto T=[](const TCHAR* Name){return FAC2UILayout::Token(Name);};
        auto NumberLabels=[&](){TArray<FString> Labels;
            for(const auto& N:UI->Native.Nodes)if(N.Id==T(TEXT("NumberDisplay")) && !N.Text.IsEmpty()){
                Labels.Add(N.Text);
                TestTrue(TEXT("Native corner numbers use the small outlined single-line template"),N.Rect.Width()==10 && N.Rect.Height()==10 && N.Attr(TEXT("one_line")) && N.Attr(TEXT("font_outline")));
            }
            return Labels;
        };
        UI->Shortcuts[40]=FAC2Shortcut::ForSkill(96);UI->Shortcuts[41]=FAC2Shortcut::ForText(TEXT("/me hello"));
        UI->Submit(TEXT("/shortcut numbers on"));
        const auto Labels=NumberLabels();
        TestTrue(TEXT("Empty slots in both rows show native corner numbers"),Labels.Num()==18 && Labels[0]==TEXT("3") && Labels[7]==TEXT("0") && Labels[8]==TEXT("1") && Labels.Last()==TEXT("0"));
        TestEqual(TEXT("Visible page number is shown"),UI->Native.Find(TEXT("SetNumberDisplay"))->Text,FString(TEXT("5")));
        UI->Submit(TEXT("/shortcut numbers off"));
        TestTrue(TEXT("Off hides dynamic numbers without changing bindings or hit targets"),NumberLabels().IsEmpty() &&
            UI->Native.Find(TEXT("SetNumberDisplay"))->Text.IsEmpty() && UI->Shortcuts[40].Skill==96 && UI->Shortcuts[41].Text==TEXT("/me hello") && UI->Buttons.Contains(TEXT("ShortcutSlot19")));
        const auto* Frame=UI->Native.Find(TEXT("ShortcutField"));
        TestTrue(TEXT("The fixed numbered DAT frame remains intact"),Frame && Frame->Media.ContainsByPredicate([](const auto& Art){return Art.Image==0x41008d29;}));
        UI->Submit(TEXT("/shortcut numbers maybe"));
        TestFalse(TEXT("Invalid command leaves preference unchanged"),UI->ShowShortcutNumbers);
        float SavedNumbers=1;
        TestTrue(TEXT("Command persists the same option as the checkbox"),GConfig->GetFloat(TEXT("AC2.Options"),TEXT("Shortcut numbers"),SavedNumbers,GGameUserSettingsIni) && SavedNumbers==0);
        UI->OptionValues.Reset();UI->RestoreOptions();
        TestFalse(TEXT("Saved number preference reloads"),UI->ShowShortcutNumbers);
        // Immediate changes during a smooth scroll must not snap its position.
        UI->ShortcutInstantScroll=false;UI->ShortcutTime=100;UI->SelectShortcutSet(5);UI->ShortcutTime=100.1;UI->Rebuild();
        const int32 Before=UI->ShortcutScroll.View(UI->ShortcutTime);
        UI->Submit(TEXT("/shortcut numbers on"));
        TestTrue(TEXT("Command preserves the moving view and labels its visible page"),UI->ShortcutScroll.Moving(100.1) && UI->ShortcutScroll.View(100.1)==Before &&
            UI->Native.Find(TEXT("SetNumberDisplay"))->Text==FString::FromInt(UI->ShortcutTopRow+1));
        UI->ShortcutInstantScroll=true;UI->SelectShortcutSet(9);
        TestEqual(TEXT("Padding beyond binding 100 gets no misleading labels"),NumberLabels().Num(),10);
        TestFalse(TEXT("Number toggle does not enable padding hit targets"),UI->Buttons.Contains(TEXT("ShortcutSlot10")));
        UI->SelectShortcutSet(4);UI->Shortcuts[40]={};UI->Shortcuts[41]={};UI->Rebuild();
        TestTrue(TEXT("All number commands remain local"),Read().IsEmpty());
        UI->Editing=true;UI->Submit(TEXT("/shortcut text 10 /say Hello, Dereth!"));UI->Editing=false;
        TestEqual(TEXT("Creation works from focused chat on the current page"),UI->Shortcuts[49].Text,FString(TEXT("/say Hello, Dereth!")));
        TestTrue(TEXT("Assignment never sends its payload"),Read().IsEmpty());
        TestTrue(TEXT("Tooltip exposes full text without fictitious examination"),UI->Hints.FindRef(TEXT("ShortcutSlot9")).Contains(TEXT("Hello, Dereth!")) && !UI->Hints.FindRef(TEXT("ShortcutSlot9")).Contains(TEXT("examine")));
        const auto Layers=UI->ShortcutLayers(UI->Shortcuts[49]);
        TestTrue(TEXT("Native AliasText supplies the icon"),Layers.Num()==1 && Layers[0].Image==0x4100159a);
        const auto Label=UI->Native.Nodes.FindByPredicate([](const auto& N){return N.Id==FAC2UILayout::Token(TEXT("AliasText")) && N.Text==TEXT("/say Hello, Dereth!");});
        TestTrue(TEXT("Native text remains one line, outlined and inside the slot"),Label && Label->Attr(TEXT("one_line")) && Label->Attr(TEXT("font_outline")) && Label->Clip.Width()<=32 && Label->Clip.Height()<=32);
        UI->ActivateShortcut(9);const FString KeyCommand=Read();
        UI->Submit(TEXT("/say Hello, Dereth!"));
        TestEqual(TEXT("Shortcut uses the exact same network command as chat Enter"),KeyCommand,Read());
        TestTrue(TEXT("Chat activation produced a send"),KeyCommand.Contains(TEXT("Hello, Dereth!")));
        const auto P=Center(TEXT("ShortcutSlot9"));Down(P);Up(P);
        TestEqual(TEXT("Pointer activation submits once"),Read(),KeyCommand);
        Down(P,EKeys::RightMouseButton);
        TestTrue(TEXT("Text right-click neither sends nor examines"),Read().IsEmpty() && !UI->ShowStats);

        UI->Editing=true;UI->ActivateShortcut(9);UI->Editing=false;
        TestTrue(TEXT("Typing in chat blocks hotbar activation"),Read().IsEmpty());
        Drag(TEXT("ShortcutSlot9"),TEXT("ShortcutSlot8"),false);
        TestTrue(TEXT("Locked text binding cannot move or activate on drag"),UI->Shortcuts[49].Text==TEXT("/say Hello, Dereth!") && UI->Shortcuts[48].IsEmpty() && Read().IsEmpty());
        Drag(TEXT("ShortcutSlot9"),TEXT("ShortcutSlot8"),true);
        TestTrue(TEXT("Shift drag moves the text binding without submitting"),UI->Shortcuts[49].IsEmpty() && UI->Shortcuts[48].Text==TEXT("/say Hello, Dereth!") && Read().IsEmpty());
        UI->Shortcuts[99]=Binding;UI->SaveShortcuts();UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestTrue(TEXT("All pages persist text alongside existing bindings"),UI->ShortcutSet==4 && UI->Shortcuts[99]==Binding && UI->Shortcuts[0].Skill==0xb0);
        UI->Submit(TEXT("/shortcut text 9 \"unterminated"));
        TestEqual(TEXT("Invalid replacement preserves existing binding"),UI->Shortcuts[48].Text,FString(TEXT("/say Hello, Dereth!")));
        TestTrue(TEXT("Invalid creation is not sent as public chat"),Read().IsEmpty());
        UI->SelectShortcutSet(9);UI->ActivateShortcut(9);
        TestTrue(TEXT("Page ten activates the stored text"),Read().Contains(TEXT("Ari")));
        UI->ActivateShortcut(10);TestTrue(TEXT("Padding past binding 100 cannot activate"),Read().IsEmpty());
        UI->SelectShortcutSet(4);Tick();
        const auto Start=Center(TEXT("ShortcutSlot8"));const FVector2D Outside(800,180);Down(Start);
        UI->OnMouseMove(G,FPointerEvent(0,Outside,Start,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(true,false,false,false,false,false,false,false,false)));Up(Outside);
        TestTrue(TEXT("Dragging into the world removes only the shortcut"),UI->Shortcuts[48].IsEmpty() && Read().IsEmpty());
        UI->Submit(TEXT("/shortcut text 1 /clear"));UI->ActivateShortcut(0);
        TestTrue(TEXT("Local commands stay local"),UI->Log.Lines.IsEmpty() && Read().IsEmpty());
        UI->Submit(TEXT("/shortcut text 2 /shortcut text 3 /say Reassigned"));UI->ActivateShortcut(1);
        TestTrue(TEXT("A command may assign another shortcut without recursively activating it"),UI->Shortcuts[42].Text==TEXT("/say Reassigned") && Read().IsEmpty());
        UI->ShortcutInstantScroll=false;UI->ShortcutTime=100;UI->SelectShortcutSet(5);
        UI->ShortcutTime=100.1;UI->Rebuild();const int32 VisiblePage=UI->ShortcutTopRow;
        UI->Submit(TEXT("/shortcut text 4 /say Visible row"));
        TestEqual(TEXT("Creation during scrolling follows visible row"),UI->Shortcuts[VisiblePage*10+3].Text,FString(TEXT("/say Visible row")));
        FAC2CharacterProfile Other;Other.Id=FGuid::NewGuid().ToString();Other.Name=TEXT("Other tester");
        Mode->Characters.Characters.Add(Other);Mode->Characters.ActiveId=Other.Id;UI->LoadShortcuts();
        TestTrue(TEXT("Text bindings do not leak between characters"),UI->Shortcuts[99].IsEmpty());
        Mode->Characters.ActiveId=Player.Id;UI->LoadShortcuts();
        TestTrue(TEXT("Returning character retains its text"),UI->Shortcuts[99]==Binding);
    }
    GConfig->EmptySection(*Section,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
    FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
