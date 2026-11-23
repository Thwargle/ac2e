#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2OptionChoiceTest,"AC2.Data.OptionChoice",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2OptionChoiceTest::RunTest(const FString&)
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
        auto Event=[&](FVector2D P,FKey Key=EKeys::LeftMouseButton){
            const auto Screen=UI->GetCachedGeometry().LocalToAbsolute(P*UI->Scale);
            return FPointerEvent(0,Screen,Screen,{},Key,0,FModifierKeysState());
        };
        auto Click=[&](FIntRect R){const auto E=Event(FVector2D(R.Min+R.Size()/2));
            UI->OnMouseButtonDown(UI->GetCachedGeometry(),E);UI->OnMouseButtonUp(UI->GetCachedGeometry(),E);};
        UI->Shortcuts[99]=FAC2Shortcut::ForSkill(96);UI->ShortcutSet=7;
        for(const auto Size:{FVector2D(1024,768),FVector2D(1600,900),FVector2D(2560,1440)}){
            UI->Tick(FGeometry::MakeRoot(Size,FSlateLayoutTransform()),FPlatformTime::Seconds(),.016f);
            UI->ShowOptions=true;UI->ShortcutRows=1;UI->RefreshShortcutHeightOption(true);
            UI->OptionAction(TEXT("OptionsTab2"));UI->ScrollOptions(MAX_int32/2);
            TestFalse(TEXT("Native dropdown has a hit target"),UI->OptionChoiceAnchor.IsEmpty());
            Click(UI->OptionChoiceAnchor);
            TestEqual(TEXT("Mouse down opens popup, release keeps it open"),UI->OpenOptionChoice,33);
            if(!TestEqual(TEXT("All five native choices appear"),UI->OptionChoiceItems.Num(),5))break;
            TestEqual(TEXT("Native popup dimensions include list margins"),UI->OptionChoicePopup.Size(),FIntPoint(85,100));
            TestTrue(TEXT("Popup stays visible"),UI->OptionChoicePopup.Min.Y>=0 && UI->OptionChoicePopup.Max.Y<=Size.Y);
            TestTrue(TEXT("Popup paints above its parent panel"),UI->OptionDialogNodesStart<UI->Native.Nodes.Num());
            for(int32 I=UI->OptionDialogNodesStart;I<UI->Native.Nodes.Num();++I)
                TestNotEqual(TEXT("A fully visible menu has no draggable scrollbar thumb"),UI->Native.Nodes[I].Id,FAC2UILayout::Token(TEXT("widget")));
            Click(FIntRect(UI->OptionChoicePopup.Max-FIntPoint(8,12),UI->OptionChoicePopup.Max-FIntPoint(2,6)));
            TestEqual(TEXT("Clicking the disabled scrollbar leaves the menu open"),UI->OpenOptionChoice,33);
            for(int32 I=0;I<5;++I)TestFalse(TEXT("Native choice label is localized"),UI->ShortcutHeightLabel(I+1).IsEmpty());
            Click(UI->OptionChoiceItems[4]);
            TestTrue(TEXT("Selection edits only draft"),UI->OpenOptionChoice==INDEX_NONE && UI->OptionValues[33]==5 && UI->AppliedOptions[33]==1 && UI->ShortcutRows==1);
            UI->OptionAction(TEXT("OptionsApply"));
            TestTrue(TEXT("Apply changes height without changing page or hidden bindings"),UI->ShortcutRows==5 && UI->AppliedOptions[33]==5 && UI->ShortcutSet==7 && UI->Shortcuts[99].Skill==96);
            Click(UI->OptionChoiceAnchor);Click(UI->OptionChoiceItems[1]);
            UI->OptionAction(TEXT("OptionsRecall"));UI->AnswerOptionConfirmation(true);
            TestTrue(TEXT("Recall restores applied height"),UI->OptionValues[33]==5 && UI->ShortcutRows==5);
            Click(UI->OptionChoiceAnchor);
            UI->OnKeyDown(UI->GetCachedGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
            TestTrue(TEXT("Escape closes only popup"),UI->ShowOptions && UI->OpenOptionChoice==INDEX_NONE && UI->ShortcutRows==5);
            Click(UI->OptionChoiceAnchor);Click(FIntRect(800,100,810,110));
            TestTrue(TEXT("Outside click dismisses without starting camera gesture"),UI->OpenOptionChoice==INDEX_NONE && !UI->CameraLeft && !UI->CameraRight);
            UI->OptionValues[33]=4;UI->OptionValues[0]=95;UI->ShortcutRows=3;UI->RefreshShortcutHeightOption(true);
            TestTrue(TEXT("Grip notice updates height draft and baseline without discarding other edits"),UI->OptionValues[33]==3 && UI->AppliedOptions[33]==3 && UI->OptionValues[0]==95);
            UI->OptionAction(TEXT("OptionsDefault"));UI->AnswerOptionConfirmation(true);
            TestTrue(TEXT("Confirmed native default applies one row"),UI->ShortcutRows==1 && UI->OptionValues[33]==1);
            Click(UI->OptionChoiceAnchor);UI->ScrollOptions(-UI->OptionsMaxScroll);
            TestEqual(TEXT("Scrolling parent closes popup"),UI->OpenOptionChoice,INDEX_NONE);
            TestEqual(TEXT("Native menu artwork resolves"),UI->Native.MissingImages,0);
        }
        UI->CloseOptionChoice();UI->OptionValues=UI->AppliedOptions;
    }
    GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
