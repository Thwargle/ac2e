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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ConcurrentWindowsTest,"AC2.Data.ConcurrentWindows",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ConcurrentWindowsTest::RunTest(const FString&)
{
    const FConfigFile SavedConfig=*GConfig->FindConfigFile(GGameUserSettingsIni);
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=TEXT("101");Player.Name=TEXT("Window tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture commands without a server connection"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);Cast<AAC2HUD>(PC->MyHUD)->GameUI=UI;
        UI->Native.RootOffsets.Reset();UI->Native.RootSizes.Reset();
        const auto G=FGeometry::MakeRoot(FVector2D(1600,1000),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};
        UI->ShowFps=false;UI->FpsPosition=FVector2D(25,42);
        UI->Submit(TEXT("/FPS"));
        for(int32 Frame=0;Frame<20;++Frame)Tick();
        TestTrue(TEXT("FPS command shows a live frame measurement"),UI->ShowFps && UI->MeasuredFps>0);
        const auto Start=UI->FpsBounds();const FVector2D Grab(Start.Left+3,Start.Top+3),End(800,700);
        auto Pointer=[&](FVector2D P,FVector2D Previous){
            return FPointerEvent(0,P*UI->Scale,Previous*UI->Scale,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        };
        UI->OnMouseButtonDown(G,Pointer(Grab,Grab));
        UI->OnMouseMove(G,Pointer(End,Grab));
        UI->OnMouseButtonUp(G,Pointer(End,End));
        TestTrue(TEXT("FPS meter moves by its grab point"),UI->FpsPosition.Equals(End-FVector2D(3,3),1.));
        // Shop and Result windows also save root IDs that have no named config key.
        const uint32 Detached=0x0e036a6c;
        GConfig->SetVector2D(TEXT("AC2.Windows"),TEXT("0E036A6C"),FVector2D(731,201),GGameUserSettingsIni);
        GConfig->SetVector2D(TEXT("AC2.WindowSizes"),TEXT("0E036A6C"),FVector2D(414,515),GGameUserSettingsIni);
        const auto Relog=SNew(SAC2GameUI).Mode(Mode);
        TestEqual(TEXT("Detached panel position survives login layout"),Relog->Native.RootOffsets.FindRef(Detached),FIntPoint(731,201));
        TestEqual(TEXT("Detached panel dimensions survive relog"),Relog->Native.RootSizes.FindRef(Detached),FIntPoint(414,515));
        TestTrue(TEXT("FPS visibility and dragged position survive HUD recreation"),Relog->ShowFps && Relog->FpsPosition.Equals(UI->FpsPosition,1.));
        auto Capture=[&](const FString& File){
#if WITH_EDITOR
            if(!FParse::Param(FCommandLine::Get(),TEXT("AC2WindowRender")))return;
            UI->SlatePrepass();FWidgetRenderer Renderer(true);
            TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
            Target->ClearColor=FLinearColor::Transparent;Target->InitCustomFormat(1600,1000,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);
            Renderer.DrawWidget(Target.Get(),UI,FVector2D(1600,1000),0.f);FlushRenderingCommands();
            FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);TArray<FColor> Pixels;
            if(TestTrue(TEXT("Render independent windows"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))){
                if(UI->ShowFps){
                    const FSlateRect R=UI->FpsBounds();int32 Bright=0,Dark=0;
                    for(int32 Y=R.Top+1;Y<R.Bottom-1;++Y)
                        for(int32 X=R.Left+1;X<R.Right-1;++X){
                            const auto& Pixel=Pixels[Y*1600+X];
                            if(Pixel.A>80 && Pixel.R<45 && Pixel.G<45 && Pixel.B<45)++Dark;
                            if(Pixel.R>140 && Pixel.G>140 && Pixel.B>140)++Bright;
                        }
                    TestTrue(TEXT("FPS digits contrast with their dark background"),Bright>6 && Dark>100);
                }
                for(auto& P:Pixels)P.A=255;TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1600,1000,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*File);
            }
#endif
        };
        auto Json=[&](const TCHAR* Source){
            TSharedPtr<FJsonObject> Event;
            const auto Reader=TJsonReaderFactory<>::Create(FString(Source));
            if(TestTrue(TEXT("Status test message parses"),FJsonSerializer::Deserialize(Reader,Event)))Net.Handle(Event);
        };
        Json(TEXT(R"({"event":"clock","server_time":1000.0})"));
        Json(TEXT(R"({"event":"status_effects","replace":true,"effects":[{"eid":42,"flags":2,"order":1,"cast":990,"duration":20,"icon":1090524006,"name":"Haste","description":"Combat speed enhanced."}]})"));
        Json(TEXT(R"({"event":"link_status","silence_seconds":12.0,"sent_bps":48.0,"received_bps":128.0,"retransmit_percent":1.5})"));
        Json(TEXT(R"({"event":"pvp_status","server":0,"region":1})"));
        Json(TEXT(R"({"event":"quality","table":3,"key":286,"value":90.0})"));
        Json(TEXT(R"({"event":"quality","table":4,"key":263,"value":100})"));
        Json(TEXT(R"({"event":"quality","table":4,"key":264,"value":100})"));
        Json(TEXT(R"({"event":"quality","table":4,"key":302,"value":100,"level_xp_floor":"2978018150324","level_xp_span":"258511070776"})"));
        Json(TEXT(R"({"event":"quality","table":12,"key":300,"value":"3107273685712"})"));
        Tick();
        TestEqual(TEXT("Native XP meter retains 64-bit level progress"),UI->Native.Meters.FindRef(FAC2UILayout::Token(TEXT("ExperienceMeter"))),.5f);
        TestEqual(TEXT("Native XP display receives progress and per-level requirement"),UI->Native.Text.FindRef(FAC2UILayout::Token(TEXT("ExperienceText"))),FString(TEXT("129255535388 / 258511070776")));
        Json(TEXT(R"({"event":"quality","table":12,"key":2059534337,"value":"3107273685702"})"));
        Json(TEXT(R"({"event":"quality","table":12,"key":2059534338,"value":"3107273685742"})"));
        Tick();
        TestEqual(TEXT("AC2E authoritative XP boundaries take precedence over DAT"),UI->Native.Meters.FindRef(FAC2UILayout::Token(TEXT("ExperienceMeter"))),.25f);
        TestEqual(TEXT("Link quality uses time since last incoming data"),UI->Native.Find(TEXT("NetStatusField"))->State,FAC2UILayout::Token(TEXT("bad")));
        TestEqual(TEXT("Vitae penalty uses reduced native state"),UI->Native.Find(TEXT("VitaeStatusField"))->State,FAC2UILayout::Token(TEXT("notfull")));
        TestEqual(TEXT("Region conflict status uses native peace art"),UI->Native.Find(TEXT("PKStatusField"))->State,FAC2UILayout::Token(TEXT("peace")));
        TestTrue(TEXT("Timed effect has native icon, active countdown meter and clickable entry"),
            UI->Buttons.Contains(TEXT("status:effect:42")) && UI->Native.Find(TEXT("EffectTimeMeter")) &&
            UI->Icons.ContainsByPredicate([](const auto& I){return I.Image==1090524006 && I.Rect.Width()>0;}));
        const auto EffectBounds=UI->Buttons.FindRef(TEXT("status:effect:42"));
        const FVector2D EffectPoint((EffectBounds.Left+EffectBounds.Right)*.5,(EffectBounds.Top+EffectBounds.Bottom)*.5);
        UI->OnMouseButtonDown(G,Pointer(EffectPoint,EffectPoint));
        UI->OnMouseButtonUp(G,Pointer(EffectPoint,EffectPoint));
        TestFalse(TEXT("Empty space reserved for future buffs accepts world clicks"),UI->OwnsPointer(FVector2D(350,210)));
        TestTrue(TEXT("Effect examination includes its definition"),UI->ShowStats && UI->StatusDescription(UI->InspectedObject).Contains(TEXT("Combat speed enhanced.")));
        UI->ShowStats=false;UI->InspectedObject.Reset();
        Capture(FPaths::ProjectSavedDir()/TEXT("Screenshots/VitalsStatus.png"));
        Json(TEXT(R"({"event":"status_effects","effects":[{"eid":21,"flags":0,"order":0,"name":"Untimed status","duration":-1}]})"));
        Tick();
        TestTrue(TEXT("A shorter timed effect appears before an untimed status"),
            UI->Buttons.FindRef(TEXT("status:effect:42")).Left<UI->Buttons.FindRef(TEXT("status:effect:21")).Left);
        Json(TEXT(R"({"event":"status_effects","remove":21})"));
        Json(TEXT(R"({"event":"status_effects","remove":42})"));Tick();
        TestFalse(TEXT("Withdrawn effect loses its hitbox"),UI->Buttons.Contains(TEXT("status:effect:42")));
        Capture(FPaths::ProjectSavedDir()/TEXT("Screenshots/FPS.png"));
        UI->Submit(TEXT("/fps"));TestFalse(TEXT("FPS command hides the meter"),UI->ShowFps);
        auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
        auto Place=[&](uint32 Id,FIntPoint Origin){const auto* N=UI->Native.Find(Id);if(!N)return;
            UI->Native.RootOffsets.Add(Id,UI->Native.DisplayedRootOffsets.FindRef(Id)+Origin-N->Rect.Min);UI->Rebuild();};
        Tick();UI->ToggleInventory();UI->Action(TEXT("Skills"));UI->Action(TEXT("Quests"));UI->SocialAction(TEXT("Social"));UI->ToggleStats();UI->Action(TEXT("Options"));Tick();
        UI->RaiseWindow(T(TEXT("ToolbarField")));Tick();
        UI->RaiseWindow(T(TEXT("SocialField")));Tick();
        TestFalse(TEXT("Loot dropdown has an authored hit target"),UI->FellowshipLootAnchor.IsEmpty());
        UI->Mouse=FVector2D(UI->FellowshipLootAnchor.Min+UI->FellowshipLootAnchor.Size()/2);
        TestTrue(TEXT("Loot dropdown opens with a mouse click"),UI->OptionChoiceMouseDown(EKeys::LeftMouseButton));
        TestEqual(TEXT("Native loot popup offers all five modes"),UI->OptionChoiceItems.Num(),5);
        if(UI->OptionChoiceItems.Num()==5){
            const auto Row=UI->OptionChoiceItems[1];UI->Mouse=FVector2D(Row.Min+Row.Size()/2);
            UI->OptionChoiceMouseDown(EKeys::LeftMouseButton);UI->OptionChoiceMouseUp(EKeys::LeftMouseButton);
            TestEqual(TEXT("Shared loot selection survives closing popup"),UI->FellowshipLootChoice,1);
            const auto* XpToggle=UI->Native.Find(TEXT("NoFellowshipXPToggleButton"));
            if(TestNotNull(TEXT("XP sharing has a click target"),XpToggle)){
                const FVector2D Point(XpToggle->Clip.Min+XpToggle->Clip.Size()/2);
                UI->OnMouseButtonDown(G,Pointer(Point,Point));
                UI->OnMouseButtonUp(G,Pointer(Point,Point));
                TestTrue(TEXT("Clicking the XP control enables sharing"),UI->FellowshipShareXpChoice);
            }
            UI->SocialAction(TEXT("FellowshipCreateButton"));
            const FString Create=FPlatformProcess::ReadPipe(Commands);
            TestTrue(TEXT("Creation transmits selected loot mode"),Create.Contains(TEXT("\"looting\":1")));
            TestTrue(TEXT("Creation transmits XP sharing independently"),Create.Contains(TEXT("\"shared_xp\":true")));
            Json(TEXT(R"({"event":"fellowship","flags":1,"looting":0,"leader":"1","name":"Quest Party","members":[{"id":"1","level":1}]})"));
            TestTrue(TEXT("The server confirms shared XP"),Mode->Network->FellowshipSharedXp);
            Json(TEXT(R"({"event":"fellowship_clear"})"));
            TestFalse(TEXT("Leaving clears the last confirmed XP mode"),Mode->Network->FellowshipSharedXp);
        }
        TestTrue(TEXT("Toolbar clicks cannot cover the shortcut graphics"),
            UI->WindowStack.Find(T(TEXT("ToolbarField")))<UI->WindowStack.Find(T(TEXT("ShortcutField"))));
        for(const TCHAR* Name:{TEXT("InventoryField"),TEXT("SkillField"),TEXT("QuestField"),TEXT("SocialField"),TEXT("ExaminationField"),TEXT("OptionField")})
            TestNotNull(FString(TEXT("Concurrent panel: "))+Name,UI->Native.Find(Name));
        UI->Action(TEXT("Quests"));Tick();TestTrue(TEXT("Closing quests preserves inventory and skills"),UI->ShowInventory&&UI->ShowSkills&&!UI->ShowQuests);
        UI->CloseOptions();UI->ShowSocial=false;UI->ShowStats=false;Tick();
        Place(T(TEXT("InventoryField")),FIntPoint(20,50));Place(T(TEXT("SkillField")),FIntPoint(180,90));
        UI->RaiseWindow(T(TEXT("SkillField")));Tick();
        const FVector2D Overlap(230,180);
        TestEqual(TEXT("Front window owns overlap"),UI->WindowAt(Overlap),T(TEXT("SkillField")));
        UI->RaiseWindow(T(TEXT("InventoryField")));Tick();
        TestEqual(TEXT("Raising inventory changes hit order"),UI->WindowAt(Overlap),T(TEXT("InventoryField")));
        // Automatic item inspection must not cover the slot just clicked, even
        // when saved Examine and Inventory positions overlap.
        UI->SelectedNetworkItem=TEXT("9001");UI->ShowStats=true;Tick();
        TestTrue(TEXT("Automatic item inspection keeps inventory in front"),
            UI->WindowStack.Find(T(TEXT("ExaminationField")))<UI->WindowStack.Find(T(TEXT("InventoryField"))));
        UI->RaiseWindow(T(TEXT("ExaminationField")));Tick();
        TestTrue(TEXT("Explicitly raising Examine still works"),
            UI->WindowStack.Find(T(TEXT("ExaminationField")))>UI->WindowStack.Find(T(TEXT("InventoryField"))));
        UI->ShowStats=false;UI->SelectedNetworkItem.Reset();Tick();
        const auto Before=UI->Native.Bounds(TEXT("InventoryField"));
        UI->Mouse=FVector2D(Before.Max)-FVector2D(2,2);
        TestTrue(TEXT("Inventory corner starts resizing"),UI->BeginWindowResize());
        UI->ResizeWindowTo(UI->Mouse+FVector2D(130,110));UI->ResizeWindow=0;Tick(); // Do not persist test geometry.
        TestEqual(TEXT("Inventory expands by dragged amount"),UI->Native.Bounds(TEXT("InventoryField")).Size(),Before.Size()+FIntPoint(130,110));
        TestEqual(TEXT("Resizing the bottom right keeps the top left stationary"),UI->Native.Bounds(TEXT("InventoryField")).Min,Before.Min);
        TestTrue(TEXT("Inventory list follows resized panel"),UI->Native.Bounds(TEXT("ContainerListBox")).Max.X>Before.Max.X-40);
        UI->ShowSkills=false;Tick();UI->ToggleInventory();UI->ToggleInventory();Tick();
        TestEqual(TEXT("Resized dimensions survive reopening"),UI->Native.Bounds(TEXT("InventoryField")).Size(),Before.Size()+FIntPoint(130,110));
        const uint32 Quest=T(TEXT("QuestField"));
        UI->ShowQuests=true;UI->Native.RootSizes.Add(Quest,FIntPoint(400,750));Tick();
        const auto* JournalBody=UI->Native.Find(0x85413ff5u);
        const auto* JournalTrim=UI->Native.Find(0xb21c3f68u);
        const auto QuestRect=UI->Native.Bounds(TEXT("QuestField"));
        TestTrue(TEXT("Quest content expands with its window"),JournalBody && JournalBody->Rect.Height()>650 && JournalBody->Rect.Width()>360);
        if(JournalTrim)TestTrue(TEXT("Quest bottom border keeps its authored height"),JournalTrim->Rect.Height()<40 && FMath::Abs(JournalTrim->Rect.Max.Y-QuestRect.Max.Y)<5);
        UI->ShowSkills=true;UI->SkillTab=0;
        UI->Native.RootSizes.Add(T(TEXT("SkillField")),FIntPoint(420,750));Tick();
        const auto* Modify=UI->Native.Find(TEXT("ModifyField"));
        TestTrue(TEXT("Tall skill tree clears the action footer"),Modify && UI->SkillClip.Max.Y<=Modify->Rect.Min.Y+1);
        const auto* SkillPage=UI->Native.Find(0xaf472f02u);
        // The authored page is 333 px tall. Its footer ends at local y=317 (exclusive).
        const int32 FooterMargin=Modify && SkillPage ? SkillPage->Rect.Max.Y-Modify->Rect.Max.Y : INDEX_NONE;
        TestTrue(FString::Printf(TEXT("Skill action footer retains its native 16 px lower margin (actual %d)"),FooterMargin),FMath::Abs(FooterMargin-16)<=2);
        Place(T(TEXT("SkillField")),FIntPoint(540,60));Tick();
        Capture(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2-resized-journal-skills.png"));
        UI->Native.RootSizes.Remove(T(TEXT("SkillField")));
        UI->ShowSkills=false;UI->ShowQuests=false;Tick();
        // Load metadata exported from the selected DAT by the bridge test.
        FString Fixture;FParse::Value(FCommandLine::Get(),TEXT("AC2CraftFixture="),Fixture);
        FString Text;TSharedPtr<FJsonObject> Registry;
        if(TestTrue(TEXT("Read selected-DAT recipe fixture"),!Fixture.IsEmpty()&&FFileHelper::LoadFileToString(Text,*Fixture)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Registry))){
            Registry->SetBoolField(TEXT("replace"),true);Net.Handle(Registry);
            const auto Recipe=Registry->GetArrayField(TEXT("catalog"))[0]->AsObject();const uint32 Id=Recipe->GetNumberField(TEXT("id"));
            UI->ShowSkills=true;UI->SkillTab=4;UI->CraftProfessions=true;Tick();
            UI->Native.RootSizes.Add(T(TEXT("SkillField")),FIntPoint(859,900));Tick();
            const auto CraftFrame=UI->Native.Bounds(TEXT("SkillField"));
            const auto* CraftPage=UI->Native.Find(TEXT("CraftPage"));
            const auto* CraftFooter=UI->Native.Find(0xb6645e9b);
            TestTrue(TEXT("Craft content fills resized width and height"),CraftPage&&CraftPage->Rect.Width()>800&&CraftPage->Rect.Height()>800);
            // Icefire's frame draws its own bottom border; Legions has this
            // extra footer child. Both must follow the resized content area.
            if(CraftFooter)TestTrue(TEXT("Skills footer follows the bottom border"),FMath::Abs(CraftFooter->Rect.Max.Y-CraftFrame.Max.Y)<35);
            TestNull(TEXT("Profession browser has no overlapping selected-profession footer"),UI->Native.Find(0xe44235ff));
            Capture(Fixture+TEXT(".browser.png"));
            TestTrue(TEXT("Craft book chooses profession from current DAT"),Net.CraftSkills.Contains(UI->CraftSkill));
            UI->CraftSkill=Recipe->GetNumberField(TEXT("skill"));UI->CraftProfessions=false;Tick();
            TestTrue(TEXT("Granted recipe has a clickable row"),UI->Buttons.Contains(*FString::Printf(TEXT("CraftRecipe%u"),Id)));
            const uint32 RecipeArt=UI->RecipeIcon(Id);
            TestTrue(TEXT("Recipe row draws the current DAT recipe or profession icon"),RecipeArt && UI->Icons.ContainsByPredicate([&](const auto& Icon){
                return Icon.Window==T(TEXT("SkillField")) && Icon.Image==RecipeArt;
            }));
            Net.OpenCraft(Id);UI->RaiseWindow(0x0e036a6c);Tick();
            TestTrue(TEXT("Recipe and inventory coexist"),UI->Native.Find(0x0e036a6c)&&UI->Native.Find(TEXT("InventoryField")));
            const auto Original=UI->Native.Find(0x0e036a6c)->Rect;Place(0x0e036a6c,FIntPoint(700,100));
            UI->Mouse=FVector2D(UI->Native.Find(0x0e036a6c)->Rect.Max)-FVector2D(2,2);
            TestTrue(TEXT("Recipe corner starts resizing"),UI->BeginWindowResize());
            UI->ResizeWindowTo(UI->Mouse+FVector2D(170,80));UI->ResizeWindow=0;Tick();
            TestEqual(TEXT("Recipe width increases"),UI->Native.Find(0x0e036a6c)->Rect.Width(),Original.Width()+170);
            const auto* Close=UI->Native.Find(0xacae3cde);const auto* Title=UI->Native.Find(0x5cabf830);
            TestTrue(TEXT("Craft close remains native size"),Close&&Close->Rect.Size()==FIntPoint(17,17));
            TestTrue(TEXT("Craft close is at the right end of the caption"),Close&&UI->Native.Find(0x0e036a6c)->Rect.Max.X-Close->Rect.Max.X<40);
            TestTrue(TEXT("Craft title leaves header control space"),Title&&Close&&Title->Clip.Max.X<Close->Rect.Min.X);
            const auto* LevelLabel=UI->Native.Find(0xf594c22e);const auto* LevelUp=UI->Native.Find(0x82155597);const auto* LevelNumber=UI->Native.Find(0xbff888f4);
            TestTrue(TEXT("Recipe level caption leaves the numeric recess clear"),LevelLabel&&LevelUp&&LevelLabel->Rect.Max.X<=LevelUp->Rect.Min.X-40);
            TestTrue(TEXT("Recipe level label and value align vertically"),LevelLabel&&LevelNumber&&LevelLabel->Rect.Min.Y==LevelNumber->Rect.Min.Y&&LevelLabel->Rect.Max.Y==LevelNumber->Rect.Max.Y);
            auto Inputs=Net.CraftInputs();
            if(TestTrue(TEXT("Recipe has required materials"),Inputs.Num()>0)){
                const auto& First=Inputs[0];FAC2InventoryItem Item;Item.NetworkId=TEXT("9001");Item.Definition=First.Choices[0];Item.Quantity=First.Quantity;Item.Cell=0;
                Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Item.Definition);
                const uint32 MaterialArt=Mode->CharacterCatalog.Items.FindRef(Item.Definition).Icon;
                auto HasMaterialArt=[&](float Opacity){return MaterialArt && UI->Icons.ContainsByPredicate([&](const auto& Icon){
                    return Icon.Window==0x0e036a6c && Icon.Image==MaterialArt && FMath::IsNearlyEqual(Icon.Tint.A,Opacity);
                });};
                if(First.Choices.Num()==1)TestTrue(TEXT("Single material slot previews its item icon"),HasMaterialArt(.55f));
                Mode->Characters.Active()->Items.Add(Item);FPlatformProcess::ReadPipe(Commands);
                Net.SetCraftIngredient(First.Ordinal,Item.NetworkId);
                TestTrue(TEXT("Manual ingredient selection sends validation"),FPlatformProcess::ReadPipe(Commands).Contains(TEXT("craft_validate")));
                TestFalse(TEXT("Selection waits for server approval"),Net.CanCraft());
                auto Reply=MakeShared<FJsonObject>();Reply->SetStringField(TEXT("event"),TEXT("craft_ingredient"));Reply->SetNumberField(TEXT("recipe"),Id);
                Reply->SetNumberField(TEXT("spinner"),Net.CraftSpinner);Reply->SetNumberField(TEXT("ordinal"),First.Ordinal);Reply->SetStringField(TEXT("item"),TEXT("stale"));Reply->SetNumberField(TEXT("error"),0);
                Net.Handle(Reply);TestFalse(TEXT("Stale validation cannot approve a replacement"),Net.CraftAccepted[First.Ordinal-1]);
                Reply->SetStringField(TEXT("item"),Item.NetworkId);Net.Handle(Reply);TestTrue(TEXT("Matching reply approves selection"),Net.CraftAccepted[First.Ordinal-1]);
                Tick();TestTrue(TEXT("Assigned material displays its item icon at full opacity"),HasMaterialArt(1.f));
            }
            // Complete the native ingredient transaction through the same UI actions.
            Net.Gold=1000000;UI->ShowSkills=false;Tick();
            for(int32 I=0;I<Inputs.Num();++I){const auto& Input=Inputs[I];if(Input.Choices.IsEmpty())continue;
                const FString Instance=FString::Printf(TEXT("%d"),9001+I);
                if(I){FAC2InventoryItem Extra;Extra.NetworkId=Instance;Extra.Definition=Input.Choices[0];Extra.Quantity=Input.Quantity;Extra.Cell=I;
                    Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Extra.Definition);Mode->Characters.Active()->Items.Add(Extra);}
                Tick();FPlatformProcess::ReadPipe(Commands);
                const auto* From=UI->Buttons.Find(*FString::Printf(TEXT("Cell%d"),I));
                const auto* To=UI->Buttons.Find(*FString::Printf(TEXT("CraftIngredient%u"),Input.Ordinal));
                if(TestTrue(TEXT("Visible backpack and ingredient drop targets"),From&&To)){
                    const FVector2D A=G.LocalToAbsolute(FVector2D((From->Left+From->Right)*.5,(From->Top+From->Bottom)*.5)*UI->Scale);
                    const FVector2D B=G.LocalToAbsolute(FVector2D((To->Left+To->Right)*.5,(To->Top+To->Bottom)*.5)*UI->Scale);
                    UI->OnMouseButtonDown(G,FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
                    UI->OnMouseMove(G,FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
                    UI->OnMouseButtonUp(G,FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
                    TestTrue(TEXT("Dragging a backpack item requests native validation"),FPlatformProcess::ReadPipe(Commands).Contains(TEXT("craft_validate")));
                }
                auto Reply=MakeShared<FJsonObject>();Reply->SetStringField(TEXT("event"),TEXT("craft_ingredient"));Reply->SetNumberField(TEXT("recipe"),Id);
                Reply->SetNumberField(TEXT("spinner"),Net.CraftSpinner);Reply->SetNumberField(TEXT("ordinal"),Input.Ordinal);Reply->SetStringField(TEXT("item"),Instance);Reply->SetNumberField(TEXT("error"),0);Net.Handle(Reply);
            }
            TestTrue(TEXT("All approved materials enable crafting"),Net.CanCraft());
            UI->CraftAction(TEXT("CraftExecute"));TestTrue(TEXT("Craft button sends the native transaction"),Net.CraftPending&&FPlatformProcess::ReadPipe(Commands).Contains(TEXT("craft_execute")));
            UI->CraftAction(TEXT("CraftExecute"));TestTrue(TEXT("Pending craft prevents duplicate requests"),FPlatformProcess::ReadPipe(Commands).IsEmpty());
            auto Completed=MakeShared<FJsonObject>();Completed->SetStringField(TEXT("event"),TEXT("craft_complete"));Completed->SetNumberField(TEXT("recipe"),Id);Completed->SetNumberField(TEXT("notify"),1);Completed->SetNumberField(TEXT("error"),0);Net.Handle(Completed);
            TestTrue(TEXT("Completion releases pending state and clears spent selections"),!Net.CraftPending&&!Net.CanCraft()&&Net.CraftIngredients[0].IsEmpty());
            Tick();
#if WITH_EDITOR
            if(FParse::Param(FCommandLine::Get(),TEXT("AC2WindowRender"))){
                UI->ShowSkills=false;UI->ShowStats=true;Tick();Place(T(TEXT("ExaminationField")),FIntPoint(1170,80));

                UI->SlatePrepass();FWidgetRenderer Renderer(true);
                TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
                Target->ClearColor=FLinearColor::Transparent;Target->InitCustomFormat(1600,1000,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);
                Renderer.DrawWidget(Target.Get(),UI,FVector2D(1600,1000),0.f);FlushRenderingCommands();
                FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
                TArray<FColor> Pixels;if(TestTrue(TEXT("Render independent windows"),Target.IsValid()&&Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))){
                    for(auto& P:Pixels)P.A=255;TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1600,1000,Pixels,PNG);
                    FFileHelper::SaveArrayToFile(PNG,*(Fixture+TEXT(".png")));
                }
            }
#endif
        }
    }
    FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    *GConfig->FindConfigFile(GGameUserSettingsIni)=SavedConfig;GConfig->Flush(false,GGameUserSettingsIni);
    return true;
}
#endif
