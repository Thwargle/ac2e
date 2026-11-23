#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2OptionsTest,"AC2.Data.Options",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2OptionsTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    World->SpawnActor<AAC2PlayerController>();
    {
        const auto UI=SNew(SAC2GameUI).Mode(Mode).Frontend(true);UI->TestingOptions=UI->TestingShortcuts=true;
        const auto Original=UI->AppliedOptions;
        const auto T=[](const TCHAR* Name){return FAC2UILayout::Token(Name);};
        auto Act=[&](const TCHAR* Name){UI->Action(Name);};
        auto Event=[&](FVector2D P,FKey Key=EKeys::LeftMouseButton,float Wheel=0.f){
            const auto Screen=UI->GetCachedGeometry().LocalToAbsolute(P*UI->Scale);
            return FPointerEvent(0,Screen,Screen,{},Key,Wheel,FModifierKeysState());
        };
        auto Click=[&](const TCHAR* Name){
            const auto* R=UI->Buttons.Find(Name);if(!TestNotNull(Name,R))return;
            const auto E=Event(FVector2D((R->Left+R->Right)*.5f,(R->Top+R->Bottom)*.5f));
            UI->OnMouseButtonDown(UI->GetCachedGeometry(),E);UI->OnMouseButtonUp(UI->GetCachedGeometry(),E);
        };
        for(const auto Size:{FVector2D(1024,768),FVector2D(1600,900),FVector2D(2560,1440)}){
            UI->Tick(FGeometry::MakeRoot(Size,FSlateLayoutTransform()),FPlatformTime::Seconds(),.016f);
            Act(TEXT("OptionsTab1"));
            TestEqual(TEXT("Selected tab artwork updates in the same rebuild"),UI->Native.Find(TEXT("ClientTab"))->State,T(TEXT("open")));
            TestEqual(TEXT("Unselected tab artwork is closed"),UI->Native.Find(TEXT("CommandTab"))->State,T(TEXT("closed")));
            TestEqual(TEXT("Rows use the native template height"),UI->OptionRowHeight,33);
            TestEqual(TEXT("Content determines scrolling instead of a fixed row cap"),UI->OptionsMaxScroll,16*33-UI->OptionsList.Height());
            TestFalse(TEXT("Unchanged tab cannot apply"),UI->Buttons.Contains(TEXT("ApplyButton")));
            TestFalse(TEXT("Unchanged tab cannot recall"),UI->Buttons.Contains(TEXT("RecallButton")));
            const int32 Top=UI->OptionsThumb.Min.Y;
            const auto Wheel=Event(FVector2D(UI->OptionsList.Min+FIntPoint(20,20)),EKeys::Invalid,-1);
            UI->OnMouseWheel(UI->GetCachedGeometry(),Wheel);
            TestEqual(TEXT("Wheel advances one native row"),UI->OptionsScroll,33);
            TestTrue(TEXT("Wheel updates scrollbar thumb"),UI->OptionsThumb.Min.Y>Top);
            UI->ScrollOptions(MAX_int32/2);
            TestEqual(TEXT("All content is reachable"),UI->OptionsScroll,UI->OptionsMaxScroll);
            TestEqual(TEXT("Thumb reaches bottom"),UI->OptionsThumb.Max.Y,UI->OptionsBar.Max.Y);
            TestTrue(TEXT("Last setting can be clicked"),UI->Buttons.Contains(TEXT("OptionPlus18")));
            for(const auto& B:UI->Buttons)if(B.Key.ToString().StartsWith(TEXT("OptionPlus")) || B.Key.ToString().StartsWith(TEXT("OptionMinus")))
                TestTrue(TEXT("Partially visible controls hit only inside the list"),B.Value.Top>=UI->OptionsList.Min.Y && B.Value.Bottom<=UI->OptionsList.Max.Y);
            const auto Down=Event(FVector2D(UI->OptionsThumb.Min+UI->OptionsThumb.Size()/2));
            UI->OnMouseButtonDown(UI->GetCachedGeometry(),Down);
            TestTrue(TEXT("Thumb drag starts through real input handler"),UI->DraggingOptionsScroll);
            const auto Move=Event(FVector2D(UI->OptionsBar.Min)-FVector2D(0,100));
            UI->OnMouseMove(UI->GetCachedGeometry(),Move);UI->OnMouseButtonUp(UI->GetCachedGeometry(),Move);
            TestEqual(TEXT("Thumb drag clamps at top"),UI->OptionsScroll,0);
            TestFalse(TEXT("Mouse release ends drag"),UI->DraggingOptionsScroll);
            Act(TEXT("OptionsScrollDown"));TestEqual(TEXT("Arrow advances one row"),UI->OptionsScroll,33);
            Act(TEXT("OptionsTab3"));TestEqual(TEXT("Short page has no excess scroll"),UI->OptionsMaxScroll,0);
            TestEqual(TEXT("Tab change resets scroll"),UI->OptionsScroll,0);
            Act(TEXT("OptionsDefault"));
            const auto Dialog=UI->Native.Bounds(TEXT("ConfirmationField"));
            TestEqual(TEXT("Native confirmation size"),Dialog.Size(),FIntPoint(340,150));
            TestTrue(TEXT("Native confirmation is centered"),FVector2D::Distance(FVector2D(Dialog.Min+Dialog.Size()/2),Size*.5)<2);
            const auto* Question=UI->Native.Find(TEXT("QuestionText"));
            TestTrue(TEXT("Confirmation has native localized question"),Question && !Question->Text.IsEmpty());
            TestEqual(TEXT("Only modal buttons remain clickable"),UI->Buttons.Num(),2);
            Click(TEXT("NoButton"));TestFalse(TEXT("Native No button dismisses"),UI->OptionConfirmationOpen());
        }
        UI->AppliedOptions[0]=90;UI->AppliedOptions[10]=1;UI->AppliedOptions[12]=14;
        UI->OptionValues=UI->AppliedOptions;UI->RestoreOptions();
        Act(TEXT("OptionsTab1"));Act(TEXT("OptionPlus0"));
        TestTrue(TEXT("Edit enables Apply and Recall"),UI->Buttons.Contains(TEXT("ApplyButton")) && UI->Buttons.Contains(TEXT("RecallButton")));
        UI->ShowOptions=false;UI->ToggleOptions();
        TestEqual(TEXT("Opening after another panel covers options preserves its draft"),UI->OptionValues[0],95.f);
        Act(TEXT("OptionsTab2"));Act(TEXT("OptionPlus10"));Act(TEXT("OptionsApply"));
        TestEqual(TEXT("Current tab applied"),UI->AppliedOptions[10],0.f);
        TestEqual(TEXT("Other tab not accidentally applied"),UI->AppliedOptions[0],90.f);
        TestEqual(TEXT("Other tab draft survives"),UI->OptionValues[0],95.f);
        Act(TEXT("OptionsTab1"));Act(TEXT("OptionsRecall"));
        TestTrue(TEXT("Recall requests confirmation"),UI->OptionConfirmationOpen());
        const int32 Scroll=UI->OptionsScroll;const auto Before=UI->OptionValues;
        UI->OnMouseWheel(UI->GetCachedGeometry(),Event(FVector2D(UI->OptionsList.Min),EKeys::Invalid,-3));
        Act(TEXT("OptionPlus0"));Act(TEXT("Inventory"));
        TestTrue(TEXT("Modal blocks edits and other panel actions"),UI->OptionValues==Before && !UI->ShowInventory && UI->OptionsScroll==Scroll);
        UI->OnKeyDown(UI->GetCachedGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
        TestEqual(TEXT("Cancel recall retains draft"),UI->OptionValues[0],95.f);
        Act(TEXT("OptionsRecall"));Click(TEXT("YesButton"));
        TestEqual(TEXT("Confirmed recall restores current page"),UI->OptionValues[0],90.f);
        TestEqual(TEXT("Recall leaves other page applied value"),UI->AppliedOptions[10],0.f);
        Act(TEXT("OptionsTab3"));Act(TEXT("OptionPlus12"));Act(TEXT("OptionsApply"));
        TestTrue(TEXT("Chat Apply confirms before changing font"),UI->OptionConfirmationOpen() && UI->AppliedOptions[12]==14.f);
        Click(TEXT("YesButton"));TestEqual(TEXT("Chat Apply updates font after confirmation"),UI->AppliedOptions[12],16.f);
        Act(TEXT("OptionsTab1"));Act(TEXT("OptionPlus0"));
        Act(TEXT("OptionsTab2"));Act(TEXT("OptionsDefault"));Click(TEXT("YesButton"));
        TestEqual(TEXT("Default immediately applies current page"),UI->AppliedOptions[10],1.f);
        TestEqual(TEXT("Default preserves other page draft"),UI->OptionValues[0],95.f);
        UI->ToggleOptions();TestTrue(TEXT("Closing with changes confirms"),UI->OptionConfirmationOpen());
        Click(TEXT("NoButton"));TestFalse(TEXT("Close No hides options"),UI->ShowOptions);
        TestEqual(TEXT("Close No discards all drafts"),UI->OptionValues[0],90.f);
        UI->ToggleOptions();Act(TEXT("OptionsTab1"));Act(TEXT("OptionPlus0"));
        Act(TEXT("OptionsTab2"));Act(TEXT("OptionPlus10"));UI->ToggleOptions();Click(TEXT("YesButton"));
        TestTrue(TEXT("Close Yes applies every changed page"),!UI->ShowOptions && UI->AppliedOptions[0]==95.f && UI->AppliedOptions[10]==0.f);
        TestEqual(TEXT("All dialog and option artwork resolves"),UI->Native.MissingImages,0);
        UI->ShowOptions=true;Act(TEXT("OptionsTab2"));
        for(int32 I=23;I<=29;++I)UI->AppliedOptions[I]=UI->OptionValues[I]=(I==28?0.f:1.f);
        UI->RestoreOptions();UI->Rebuild();
        TestEqual(TEXT("Retail radar defaults exclude items"),UI->MapUI.GetRadarMask(),uint32(AC2Radar::DefaultMask));
        UI->ScrollOptions(MAX_int32/2);
        TestTrue(TEXT("Last radar option remains reachable"),UI->Buttons.Contains(TEXT("OptionPlus29")));
        TestTrue(TEXT("Radar options use native labels"),UI->Native.Nodes.ContainsByPredicate([&](const auto& N){
            return N.Text==UI->Native.Localized(TEXT("ID_Option_RadarShowCoordinates")) && !N.Text.IsEmpty();}));
        Act(TEXT("OptionPlus24"));
        TestTrue(TEXT("Draft filter leaves live radar unchanged"),(UI->MapUI.GetRadarMask()&AC2Radar::NPCs)!=0);
        Act(TEXT("OptionsApply"));
        TestFalse(TEXT("Apply updates live radar filter"),(UI->MapUI.GetRadarMask()&AC2Radar::NPCs)!=0);
        Act(TEXT("OptionPlus28"));Act(TEXT("OptionsApply"));
        TestTrue(TEXT("Items can be explicitly enabled"),(UI->MapUI.GetRadarMask()&AC2Radar::Items)!=0);
        Act(TEXT("OptionPlus29"));Act(TEXT("OptionsApply"));
        TestFalse(TEXT("Options can collapse coordinates"),UI->MapUI.ShowCoordinates());
        Act(TEXT("OptionsTab1"));Act(TEXT("OptionPlus0"));const float Draft=UI->OptionValues[0];
        UI->MapUI.Activate(T(TEXT("CoordBoxButton")),false);
        TestTrue(TEXT("Radar coordinate button synchronizes draft and applied options"),
            UI->MapUI.ShowCoordinates() && UI->AppliedOptions[29]==1 && UI->OptionValues[29]==1);
        TestEqual(TEXT("Radar button preserves unrelated draft"),UI->OptionValues[0],Draft);
        const auto Mask=UI->MapUI.GetRadarMask();UI->RestoreOptions();
        TestEqual(TEXT("Restoring options retains applied filter"),UI->MapUI.GetRadarMask(),Mask);
        Act(TEXT("OptionsTab2"));Act(TEXT("OptionsDefault"));Click(TEXT("YesButton"));
        TestEqual(TEXT("Native Defaults restores radar mask"),UI->MapUI.GetRadarMask(),uint32(AC2Radar::DefaultMask));
        TestTrue(TEXT("Retail defaults lock existing shortcuts"),UI->ShortcutLocked);
        UI->ScrollOptions(MAX_int32/2);
        TestTrue(TEXT("Native lock option is reachable and localized"),UI->Buttons.Contains(TEXT("OptionPlus30")) &&
            UI->Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Text==UI->Native.Localized(TEXT("ID_Option_ShortcutLocked")) && !N.Text.IsEmpty();}));
        Act(TEXT("OptionPlus30"));TestTrue(TEXT("Draft unlock does not apply prematurely"),UI->ShortcutLocked);
        Act(TEXT("OptionsRecall"));Click(TEXT("YesButton"));
        TestTrue(TEXT("Revert restores lock draft"),UI->OptionValues[30]==1 && UI->ShortcutLocked);
        Act(TEXT("OptionPlus30"));Act(TEXT("OptionsApply"));
        TestFalse(TEXT("Apply unlocks existing shortcuts"),UI->ShortcutLocked);
        UI->ShortcutLocked=true;UI->RestoreOptions();
        TestFalse(TEXT("Runtime restore retains applied unlock"),UI->ShortcutLocked);
        Act(TEXT("OptionsDefault"));Click(TEXT("YesButton"));
        TestTrue(TEXT("Defaults relocks shortcuts"),UI->ShortcutLocked);
        TestTrue(TEXT("Retail defaults use instant shortcut scrolling"),UI->ShortcutInstantScroll);
        UI->ScrollOptions(MAX_int32/2);
        TestTrue(TEXT("Native instant-scroll checkbox is reachable and localized"),UI->Buttons.Contains(TEXT("OptionPlus31")) &&
            UI->Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Text==UI->Native.Localized(TEXT("ID_Option_ShortcutInstaScroll")) && !N.Text.IsEmpty();}));
        Act(TEXT("OptionPlus31"));TestTrue(TEXT("Scrolling draft leaves behavior unchanged"),UI->ShortcutInstantScroll);
        Act(TEXT("OptionsApply"));TestFalse(TEXT("Apply enables smooth paging"),UI->ShortcutInstantScroll);
        UI->ShortcutTime=100;UI->SelectShortcutSet((UI->ShortcutSet+1)%10);
        TestTrue(TEXT("Smooth paging starts a native-duration transition"),UI->ShortcutScroll.Moving(100));
        Act(TEXT("OptionsDefault"));Click(TEXT("YesButton"));
        TestTrue(TEXT("Defaults settles the transition immediately"),UI->ShortcutInstantScroll && !UI->ShortcutScroll.Moving(100) && UI->ShortcutTopRow==UI->ShortcutSet);
        UI->ScrollOptions(MAX_int32/2);
        TestTrue(TEXT("Retail Defaults enables shortcut number labels"),UI->ShowShortcutNumbers);
        TestTrue(TEXT("Number option is reachable with its native label and help"),UI->Buttons.Contains(TEXT("OptionPlus32")) &&
            UI->Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Text==UI->Native.Localized(TEXT("ID_Option_ShortcutNumbersText")) && !N.Text.IsEmpty();}) &&
            !UI->Hints.FindRef(TEXT("OptionPlus32")).IsEmpty());
        Click(TEXT("OptionPlus32"));TestTrue(TEXT("Checkbox draft does not hide numbers before Apply"),UI->ShowShortcutNumbers);
        Act(TEXT("OptionsRecall"));Click(TEXT("YesButton"));
        TestTrue(TEXT("Revert restores the number checkbox"),UI->OptionValues[32]==1 && UI->ShowShortcutNumbers);
        Click(TEXT("OptionPlus32"));Click(TEXT("ApplyButton"));
        TestFalse(TEXT("Native checkbox Apply hides number labels"),UI->ShowShortcutNumbers);
        UI->ShowShortcutNumbers=true;UI->RestoreOptions();
        TestFalse(TEXT("Runtime restore uses the applied number preference"),UI->ShowShortcutNumbers);
        Act(TEXT("OptionsTab1"));Act(TEXT("OptionPlus0"));const float OtherDraft=UI->OptionValues[0];
        UI->SetShortcutNumbers(true);
        TestTrue(TEXT("Immediate command updates applied and draft without applying other settings"),
            UI->ShowShortcutNumbers && UI->OptionValues[32]==1 && UI->AppliedOptions[32]==1 && UI->OptionValues[0]==OtherDraft && UI->OptionsChanged(1));
        Act(TEXT("OptionsTab2"));UI->SetShortcutNumbers(false);
        Act(TEXT("OptionsDefault"));Click(TEXT("YesButton"));
        TestTrue(TEXT("Native Defaults restores shortcut numbers"),UI->ShowShortcutNumbers && UI->OptionValues[32]==1);
        UI->AppliedOptions=UI->OptionValues=Original;UI->RestoreOptions();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
