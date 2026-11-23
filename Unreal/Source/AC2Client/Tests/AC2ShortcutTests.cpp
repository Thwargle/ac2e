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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ItemShortcutsTest,"AC2.Data.ItemShortcuts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ItemShortcutsTest::RunTest(const FString&)
{
    const FString Potion=TEXT("18446744073709551610"),Boots=TEXT("9295429630892703745");
    TestEqual(TEXT("Legacy skill binding migrates"),FAC2Shortcut::Decode(TEXT("96")).Skill,96u);
    TestEqual(TEXT("Full unsigned instance ID survives encoding"),FAC2Shortcut::Decode(FAC2Shortcut::ForItem(Potion).Encode()).Item,Potion);
    for(const TCHAR* Bad:{TEXT("i:18446744073709551616"),TEXT("i:-1"),TEXT("i:0"),TEXT("i:12bad"),TEXT("s:4294967296"),TEXT("s:"),TEXT("NaN"),TEXT("1.5")})
        TestTrue(TEXT("Malformed binding is empty"),FAC2Shortcut::Decode(Bad).IsEmpty());

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
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Shortcut tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Net.Skills.Add({96,true,TEXT("0")});
    const FString Section=TEXT("AC2.Shortcuts.")+Player.Id;
    GConfig->SetString(*Section,TEXT("Slots"),TEXT("96,0,176"),GGameUserSettingsIni);
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);
        UI->ShortcutInstantScroll=true;
        UI->Native.RootOffsets.Reset();UI->Native.RootSizes.Reset();
        const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Event=[](const TCHAR* Type,const FString& Id){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);return J;};
        auto Snapshot=[&](TArray<FString> Items){auto J=Event(TEXT("inventory"),Player.Id);TArray<TSharedPtr<FJsonValue>> List;
            for(const auto& Id:Items)List.Add(MakeShared<FJsonValueString>(Id));J->SetArrayField(TEXT("items"),List);
            J->SetArrayField(TEXT("equipment"),{});J->SetNumberField(TEXT("gold"),0);Net.Handle(J);};
        auto Create=[&](const FString& Id,uint32 Definition,int32 Quantity){auto J=Event(TEXT("object"),Id);
            J->SetStringField(TEXT("container"),Player.Id);J->SetNumberField(TEXT("entity"),Definition);J->SetNumberField(TEXT("quantity"),Quantity);
            Net.Handle(J);};
        auto Center=[&](FName Key){const auto* R=UI->Buttons.Find(Key);TestNotNull(TEXT("Native hit target exists"),R);
            return R?G.LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*UI->Scale):FVector2D::ZeroVector;};
        auto Down=[&](FVector2D P,FKey Button=EKeys::LeftMouseButton){UI->OnMouseButtonDown(G,FPointerEvent(0,P,P,{Button},Button,0,FModifierKeysState()));};
        auto Up=[&](FVector2D P){UI->OnMouseButtonUp(G,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto Drag=[&](FName From,FName To,bool Shift=true){const auto A=Center(From),B=Center(To);
            const FModifierKeysState Mods(Shift,false,false,false,false,false,false,false,false);
            UI->OnMouseButtonDown(G,FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,Mods));
            UI->OnMouseMove(G,FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,Mods));Up(B);};

        Tick();TestEqual(TEXT("Existing skill pages retained"),UI->Shortcuts[0].Skill,96u);
        UI->Shortcuts[9]=FAC2Shortcut::ForItem(Potion);Tick();
        TestEqual(TEXT("Login without inventory does not erase item binding"),UI->Shortcuts[9].Item,Potion);
        Snapshot({Potion,Boots});Tick();
        TestEqual(TEXT("Snapshot preceding object creates retains reference"),UI->Shortcuts[9].Item,Potion);
        UI->ActivateShortcut(9);TestTrue(TEXT("Pending item cannot send a guessed command"),Read().IsEmpty());
        Create(Potion,0x47001745,3);Create(Boots,0x47000fdc,1);UI->ShowInventory=true;Tick();
        TestEqual(TEXT("Object creation resolves identity"),UI->ShortcutItemIndex(Potion),0);
        TestTrue(TEXT("Shortcut uses actual native icon"),UI->ShortcutLayers(UI->Shortcuts[9]).Num()>0 && UI->ShortcutLayers(UI->Shortcuts[9])[0].Image!=0);
        TestTrue(TEXT("Quantity appears in item shortcut tooltip"),UI->Hints.FindRef(TEXT("ShortcutSlot9")).Contains(TEXT("(3)")));
        // Test with an explicit lock so saved user preferences cannot weaken it.
        UI->ShortcutLocked=true;
        Drag(TEXT("Cell0"),TEXT("ShortcutSlot4"),false);
        TestEqual(TEXT("Inventory drag assigns item shortcut"),UI->Shortcuts[4].Item,Potion);
        TestTrue(TEXT("Assigning shortcut sends no move/drop/use command"),Read().IsEmpty());
        TestEqual(TEXT("Assigning leaves actual inventory intact"),Mode->Characters.Active()->Items.Num(),2);
        Drag(TEXT("ShortcutSlot4"),TEXT("ShortcutSlot0"),false);
        TestTrue(TEXT("Locked shortcut cannot replace another without Shift"),UI->Shortcuts[4].Item==Potion && UI->Shortcuts[0].Skill==96);
        TestTrue(TEXT("Blocked drag never activates or drops an item"),Read().IsEmpty());
        {
            const auto Start=Center(TEXT("ShortcutSlot4"));const FVector2D Outside(800,180);Down(Start);
            UI->OnMouseMove(G,FPointerEvent(0,Outside,Start,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
            TestFalse(TEXT("Locked drag does not lift its icon"),UI->ShortcutDragStarted);
            Up(Start);
            TestTrue(TEXT("Returning a blocked drag to its source does not activate"),Read().IsEmpty() && UI->Shortcuts[4].Item==Potion);
            Down(Start);UI->OnMouseMove(G,FPointerEvent(0,Outside,Start,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));Up(Outside);
            TestTrue(TEXT("Locked world drag preserves binding and inventory"),UI->Shortcuts[4].Item==Potion && Net.OwnsInventoryItem(Potion) && Read().IsEmpty());
        }
        Drag(TEXT("ShortcutSlot4"),TEXT("ShortcutSlot0"));
        TestTrue(TEXT("Moving onto occupied slot replaces instead of swaps"),UI->Shortcuts[4].IsEmpty() && UI->Shortcuts[0].Item==Potion);
        TestTrue(TEXT("Dragging item reference sends no inventory command"),Read().IsEmpty());
        UI->ShortcutLocked=false;Drag(TEXT("ShortcutSlot0"),TEXT("ShortcutSlot4"),false);
        TestTrue(TEXT("Unlocked shortcut moves without Shift"),UI->Shortcuts[0].IsEmpty() && UI->Shortcuts[4].Item==Potion);
        Drag(TEXT("ShortcutSlot4"),TEXT("ShortcutSlot0"),false);UI->ShortcutLocked=true;

        UI->ShowInventory=false;UI->ShowSkills=true;UI->JournalAction(TEXT("MagicButton"));UI->ShortcutSet=4;UI->ShortcutRows=3;Tick();
        auto DoubleSkill=[&](){const auto P=Center(TEXT("LearnedSkill96"));
            UI->OnMouseButtonDoubleClick(G,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));Up(P);};
        DoubleSkill();
        TestEqual(TEXT("Skill double-click fills current page first empty slot while locked"),UI->Shortcuts[40].Skill,96u);
        TestTrue(TEXT("Adding skill does not cast it"),Read().IsEmpty());
        UI->Shortcuts[42]=FAC2Shortcut::ForSkill(176);DoubleSkill();
        TestTrue(TEXT("Quick-add finds the first hole without replacing occupied slots"),UI->Shortcuts[41].Skill==96 && UI->Shortcuts[42].Skill==176);
        for(int32 I=40;I<50;++I)UI->Shortcuts[I]=FAC2Shortcut::ForSkill(176);
        DoubleSkill();
        TestTrue(TEXT("Full page does not overwrite or spill into expanded rows"),UI->Shortcuts[49].Skill==176 && UI->Shortcuts[50].IsEmpty() && Read().IsEmpty());
        for(int32 I=40;I<50;++I)UI->Shortcuts[I]={};
        Net.UntrainingSkill=96;DoubleSkill();
        TestTrue(TEXT("Untraining skill cannot be quick-added"),UI->Shortcuts[40].IsEmpty());Net.UntrainingSkill=0;
        Net.Skills[0].Trained=false;DoubleSkill();
        TestTrue(TEXT("Unlearned skill cannot be quick-added"),UI->Shortcuts[40].IsEmpty());Net.Skills[0].Trained=true;
        UI->ShowSkills=false;UI->ShowInventory=true;UI->ShortcutSet=0;UI->ShortcutRows=1;Tick();

        // Pointer operations follow the visible list rows, while keyboard row
        // shortcuts and the page label follow the current top row like retail.
        {
            TArray<FAC2Shortcut> SavedBindings;SavedBindings.Append(UI->Shortcuts,100);
            for(auto& Binding:UI->Shortcuts)Binding={};
            UI->Shortcuts[0]=FAC2Shortcut::ForItem(Potion);UI->Shortcuts[10]=FAC2Shortcut::ForItem(Boots);
            UI->ShortcutRows=2;UI->ShortcutInstantScroll=true;UI->Rebuild();
            UI->ShortcutInstantScroll=false;UI->ShortcutTime=100;UI->SelectShortcutSet(1);
            UI->ShortcutTime=100.125;UI->Rebuild();
            const auto List=UI->Native.Bounds(TEXT("ShortcutListBox"));
            const auto* Top=UI->Buttons.Find(TEXT("ShortcutSlot0"));const auto* Bottom=UI->Buttons.Find(TEXT("ShortcutSlot20"));
            TestTrue(TEXT("Partial rows have clipped hit targets"),Top && Bottom && Top->Top==List.Min.Y && Top->Bottom-Top->Top==16 && Bottom->Bottom==List.Max.Y && Bottom->Bottom-Bottom->Top==16);
            TestEqual(TEXT("Partial bottom row maps to its absolute binding"),UI->ShortcutHitIndex(TEXT("ShortcutSlot20")),20);
            TestEqual(TEXT("Page label follows the currently visible top row"),UI->Native.Find(TEXT("SetNumberDisplay"))->Text,FString(TEXT("1")));
            UI->ActivateShortcut(0);auto Command=Read();
            TestTrue(TEXT("Keyboard first row uses the outgoing visible potion"),Command.Contains(Potion) && Command.Contains(TEXT("\"command\":\"use\"")));
            UI->ActivateShortcut(10);Command=Read();
            TestTrue(TEXT("Keyboard second row uses the visible boots"),Command.Contains(Boots) && Command.Contains(TEXT("\"command\":\"equip\"")));
            Down(Center(TEXT("ShortcutSlot10")));Up(Center(TEXT("ShortcutSlot10")));Command=Read();
            TestTrue(TEXT("Pointer click follows the displayed incoming icon"),Command.Contains(Boots) && Command.Contains(TEXT("\"command\":\"equip\"")));
            Down(Center(TEXT("ShortcutSlot10")),EKeys::RightMouseButton);Read();
            TestEqual(TEXT("Right-click inspects the displayed binding"),Net.ExaminationId,Boots);UI->ShowStats=false;
            Drag(TEXT("Cell0"),TEXT("ShortcutSlot20"),false);
            TestTrue(TEXT("Drop onto partial row assigns that visible binding"),UI->Shortcuts[20].Item==Potion && UI->Shortcuts[30].IsEmpty() && Read().IsEmpty());
            const auto Held=Center(TEXT("ShortcutSlot0"));Down(Held);
            UI->ShortcutTime=100.25;UI->Rebuild();Up(Held);
            TestTrue(TEXT("Page moving a different icon under a held pointer does not activate it"),Read().IsEmpty());
            TestEqual(TEXT("Settled page removes outgoing hit targets"),UI->ShortcutHitIndex(TEXT("ShortcutSlot0")),10);
            TestFalse(TEXT("Settled page removes extra partial row"),UI->Buttons.Contains(TEXT("ShortcutSlot20")));
            UI->ShortcutTime=101;UI->SelectShortcutSet(9);UI->ShortcutTime=101.25;UI->Rebuild();
            TestTrue(TEXT("Animated final page disables trailing padding"),UI->ShortcutTopRow==9 && !UI->Buttons.Contains(TEXT("ShortcutSlot10")));
            UI->SelectShortcutSet(0);UI->ShortcutTime=101.375;UI->Rebuild();
            TestTrue(TEXT("Page wrap renders intermediate native rows"),UI->ShortcutTopRow>0 && UI->ShortcutTopRow<9);
            const int32 Before=UI->ShortcutScroll.View(UI->ShortcutTime),VisibleRow=UI->ShortcutTopRow;
            UI->HandleShortcutAction(0x4100003f);
            TestTrue(TEXT("Relative keyboard paging uses the visible row without jumping"),UI->ShortcutSet==VisibleRow+1 && UI->ShortcutScroll.View(UI->ShortcutTime)==Before);
            UI->SelectShortcutSet(0);
            UI->ShortcutOwner.Reset();UI->LoadShortcuts();UI->Rebuild();
            TestTrue(TEXT("Profile reload settles at the saved destination"),UI->ShortcutTopRow==0 && !UI->ShortcutScroll.Moving(UI->ShortcutTime));
            for(int32 I=0;I<100;++I)UI->Shortcuts[I]=SavedBindings[I];
            UI->ShortcutInstantScroll=true;UI->ShortcutSet=0;UI->ShortcutRows=1;UI->Rebuild();UI->SaveShortcuts();
        }

        // Reorder the actual inventory array as an object refresh does. Never
        // use the array index captured when the shortcut was assigned.
        Swap(Mode->Characters.Active()->Items[0],Mode->Characters.Active()->Items[1]);Tick();
        UI->ActivateShortcut(0);auto Used=Read();
        TestTrue(TEXT("Activation resolves the full item identity after reorder"),Used.Contains(TEXT("\"command\":\"use\"")) && Used.Contains(Potion));
        TestFalse(TEXT("Activation did not use the other array entry"),Used.Contains(Boots));
        Create(Potion,0x47001745,2);Tick();
        TestTrue(TEXT("Stack update refreshes tooltip"),UI->Hints.FindRef(TEXT("ShortcutSlot0")).Contains(TEXT("(2)")));
        // The scrolling case examined another item. Advance the request clock
        // past the inspection throttle without depending on test execution time.
        Net.ExaminationStarted=FPlatformTime::Seconds()-.126;
        Down(Center(TEXT("ShortcutSlot0")),EKeys::RightMouseButton);
        TestTrue(TEXT("Right-click requests item examination"),Read().Contains(TEXT("\"command\":\"examine\"")) && Net.ExaminationId==Potion);
        TestFalse(TEXT("Examination did not start a camera gesture"),UI->CameraRight);
        UI->ShowStats=false;UI->Shortcuts[1]=FAC2Shortcut::ForSkill(96);Tick();
        Down(Center(TEXT("ShortcutSlot1")),EKeys::RightMouseButton);
        TestEqual(TEXT("Right-click skill opens skill inspection"),UI->ExaminedSkill,96u);
        TestTrue(TEXT("Skill examination does not cast"),Read().IsEmpty());UI->ShowStats=false;

        UI->Shortcuts[3]=FAC2Shortcut::ForItem(Boots);UI->ActivateShortcut(3);Used=Read();
        TestTrue(TEXT("Equipment shortcut uses normal equip path"),Used.Contains(TEXT("\"command\":\"equip\"")) && Used.Contains(Boots));
        auto Equipped=Event(TEXT("inventory_result"),Boots);Equipped->SetNumberField(TEXT("status"),0);
        Equipped->SetNumberField(TEXT("equipped"),16);Net.Handle(Equipped);Tick();
        TestEqual(TEXT("Equipping preserves binding"),UI->Shortcuts[3].Item,Boots);
        UI->ActivateShortcut(3);TestTrue(TEXT("Equipped shortcut follows normal unequip path"),Read().Contains(TEXT("\"command\":\"unequip\"")));

        // Expanded rows use the same instance identity, inspection and activation
        // paths as the first row; shrinking must never discard hidden slots.
        UI->ShortcutRows=5;UI->Shortcuts[29]=FAC2Shortcut::ForItem(Potion);UI->Shortcuts[49]=FAC2Shortcut::ForSkill(96);Tick();
        TestTrue(TEXT("Fifth row has native hit targets"),UI->Buttons.Contains(TEXT("ShortcutSlot49")));
        Down(Center(TEXT("ShortcutSlot29")));Tick();
        const uint32 ShortcutButton=FAC2UILayout::Token(TEXT("ShortcutButton")),Pressed=FAC2UILayout::Token(TEXT("normal_pressed"));
        const int32 PressedSlots=UI->Native.Nodes.FilterByPredicate([&](const auto& N){return N.Id==ShortcutButton && N.State==Pressed;}).Num();
        TestEqual(TEXT("Press feedback affects exactly one of the fifty template instances"),PressedSlots,1);
        Up(Center(TEXT("ShortcutSlot29")));Used=Read();
        TestTrue(TEXT("Third-row item uses the correct inventory instance"),Used.Contains(TEXT("\"command\":\"use\"")) && Used.Contains(Potion));
        Down(Center(TEXT("ShortcutSlot29")),EKeys::RightMouseButton);Read();
        TestEqual(TEXT("Third-row item opens its own examination"),Net.ExaminationId,Potion);UI->ShowStats=false;
        UI->ShortcutRows=1;Tick();UI->ActivateShortcut(29);
        TestTrue(TEXT("Hidden row cannot activate but retains its binding"),Read().IsEmpty() && UI->Shortcuts[29].Item==Potion);
        UI->ShortcutRows=5;UI->ShortcutSet=9;Tick();UI->ActivateShortcut(10);
        TestTrue(TEXT("Page ten padding neither binds nor activates"),!UI->Buttons.Contains(TEXT("ShortcutSlot10")) && Read().IsEmpty());
        UI->SaveShortcuts();UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestTrue(TEXT("Row count survives reload with all hidden bindings"),UI->ShortcutRows==5 && UI->Shortcuts[49].Skill==96);
        FAC2CharacterProfile Other;Other.Id=FGuid::NewGuid().ToString();Other.Name=TEXT("Other shortcut tester");
        Mode->Characters.Characters.Add(Other);Mode->Characters.ActiveId=Other.Id;UI->LoadShortcuts();
        TestEqual(TEXT("Another character starts with its own row count"),UI->ShortcutRows,1);
        Mode->Characters.ActiveId=Player.Id;UI->LoadShortcuts();
        TestEqual(TEXT("Returning character restores its row count"),UI->ShortcutRows,5);
        UI->ShortcutRows=1;UI->ShortcutSet=0;Tick();

        // Binding removal must not become a world drop, even for an item.
        Tick();const auto A=Center(TEXT("ShortcutSlot9"));const FVector2D WorldPoint(800,180);Down(A);
        UI->OnMouseMove(G,FPointerEvent(0,WorldPoint,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(true,false,false,false,false,false,false,false,false)));Up(WorldPoint);
        TestTrue(TEXT("World drag removes only the shortcut"),UI->Shortcuts[9].IsEmpty() && Net.OwnsInventoryItem(Potion));
        TestTrue(TEXT("Removing shortcut sends no world drop"),Read().IsEmpty());
        Down(Center(TEXT("ShortcutSlot0")));UI->OnMouseCaptureLost(FCaptureLostEvent());Up(Center(TEXT("ShortcutSlot8")));
        TestTrue(TEXT("Interrupted drag preserves bindings"),UI->Shortcuts[0].Item==Potion && UI->Shortcuts[8].IsEmpty());

        UI->Shortcuts[99]=FAC2Shortcut::ForItem(Potion);UI->ShortcutSet=9;UI->SaveShortcuts();
        UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestTrue(TEXT("Typed bindings and page survive save/reload"),UI->Shortcuts[99].Item==Potion && UI->Shortcuts[1].Skill==96 && UI->ShortcutSet==9);
        Net.Handle(Event(TEXT("inventory_remove"),Potion));Tick();
        TestTrue(TEXT("Consumption clears all copies on every page"),UI->Shortcuts[0].IsEmpty() && UI->Shortcuts[99].IsEmpty());
        UI->ActivateShortcut(9);TestTrue(TEXT("Consumed shortcut cannot activate another potion"),Read().IsEmpty());
        UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestTrue(TEXT("Removed references stay removed after reload"),UI->Shortcuts[99].IsEmpty());

        Net.ClearWorld();Tick();TestEqual(TEXT("Disconnect does not erase equipped shortcut"),UI->Shortcuts[3].Item,Boots);
        Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Snapshot({});Tick();
        TestTrue(TEXT("New authoritative snapshot retires items lost between sessions"),UI->Shortcuts[3].IsEmpty());
    }
    GConfig->EmptySection(*Section,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
    FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
