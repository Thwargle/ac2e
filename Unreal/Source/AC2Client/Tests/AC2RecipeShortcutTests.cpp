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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RecipeShortcutsTest,"AC2.Data.RecipeShortcuts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RecipeShortcutsTest::RunTest(const FString&)
{
    constexpr uint32 Health=0x6f001f17;
    TestEqual(TEXT("Recipe identity round trips separately from a skill"),FAC2Shortcut::Decode(FAC2Shortcut::ForRecipe(Health).Encode()).Recipe,Health);
    for(const TCHAR* Bad:{TEXT("r:0"),TEXT("r:96"),TEXT("r:4294967296"),TEXT("r:1862278935bad")})
        TestTrue(TEXT("Invalid recipe identity rejected"),FAC2Shortcut::Decode(Bad).IsEmpty());
    FString Text;TSharedPtr<FJsonObject> Catalog;
    if(!TestTrue(TEXT("Read real recovered crafting catalog"),FFileHelper::LoadFileToString(Text,*(FPaths::ProjectDir()/TEXT("../AC2E/emulator/content/professions-v1.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Catalog)))return false;
    TSharedPtr<FJsonObject> Potion,Variable;uint32 VariableId=0;
    for(const auto& V:Catalog->GetArrayField(TEXT("recipes"))){const auto R=V->AsObject();
        if(uint32(R->GetNumberField(TEXT("id")))==Health)Potion=R;
        if(!Variable && R->GetNumberField(TEXT("maximum"))>R->GetNumberField(TEXT("minimum"))){Variable=R;VariableId=R->GetNumberField(TEXT("id"));}
    }
    if(!TestTrue(TEXT("Fixed and adjustable recipes present"),Potion.IsValid() && Variable.IsValid()))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Recipe shortcut tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    const FString Section=TEXT("AC2.Shortcuts.")+Player.Id;
    GConfig->SetString(*Section,TEXT("SlotsV2"),*FAC2Shortcut::ForRecipe(Health).Encode(),GGameUserSettingsIni);
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);UI->Native.RootOffsets.Reset();UI->Native.RootSizes.Reset();
        UI->ShortcutInstantScroll=true;
        const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(G,FPlatformTime::Seconds(),.016f);};
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Registry=[&](bool Granted){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("craft_registry"));J->SetBoolField(TEXT("replace"),true);
            TArray<TSharedPtr<FJsonValue>> Ids,Recipes;if(Granted){
                Ids={MakeShared<FJsonValueNumber>(Health),MakeShared<FJsonValueNumber>(VariableId)};
                Recipes={MakeShared<FJsonValueObject>(Potion),MakeShared<FJsonValueObject>(Variable)};
            }
            J->SetArrayField(TEXT("recipes"),Ids);J->SetArrayField(TEXT("catalog"),Recipes);
            J->SetArrayField(TEXT("professions"),Catalog->GetArrayField(TEXT("skills")));Net.Handle(J);Tick();
        };
        auto Center=[&](FName Key){const auto* R=UI->Buttons.Find(Key);TestNotNull(TEXT("Native hit target exists"),R);
            return R?G.LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*UI->Scale):FVector2D::ZeroVector;};
        auto Down=[&](FVector2D P,FKey Button=EKeys::LeftMouseButton){UI->OnMouseButtonDown(G,FPointerEvent(0,P,P,{Button},Button,0,FModifierKeysState()));};
        auto Up=[&](FVector2D P){UI->OnMouseButtonUp(G,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
        auto Drag=[&](FName From,FName To){const auto A=Center(From),B=Center(To);Down(A);
            UI->OnMouseMove(G,FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(true,false,false,false,false,false,false,false,false)));Up(B);};
        const FName RecipeRow(*FString::Printf(TEXT("CraftRecipe%u"),Health));
        Tick();UI->ActivateShortcut(0);
        TestEqual(TEXT("Pending registry preserves saved binding"),UI->Shortcuts[0].Recipe,Health);
        TestTrue(TEXT("Pending recipe sends no commands"),Read().IsEmpty());TestEqual(TEXT("Pending recipe does not open"),Net.OpenCraftRecipe,0u);
        Registry(true);UI->ShowSkills=true;UI->JournalAction(TEXT("CraftTab"));Tick();
        TestTrue(TEXT("Craft tab opens the profession browser"),UI->CraftProfessions && UI->CraftPage==0);
        UI->CraftAction(*FString::Printf(TEXT("CraftProfession%u"),uint32(Potion->GetNumberField(TEXT("skill")))));Tick();
        // Keep the recipe and hotbar unobstructed in both native layouts.
        const uint32 SkillWindow=FAC2UILayout::Token(TEXT("SkillField"));
        UI->Native.RootOffsets.FindOrAdd(SkillWindow)+=FIntPoint(650,50)-UI->Native.Bounds(TEXT("SkillField")).Min;Tick();
        Drag(RecipeRow,TEXT("ShortcutSlot4"));
        TestEqual(TEXT("Recipe row drag creates shortcut"),UI->Shortcuts[4].Recipe,Health);
        TestEqual(TEXT("Drag does not open recipe"),Net.OpenCraftRecipe,0u);
        TestTrue(TEXT("Drag does not validate or craft"),Read().IsEmpty());
        TestEqual(TEXT("Native Mucor potion recipe inherits its profession icon"),uint32(Potion->GetNumberField(TEXT("icon"))),0u);
        const uint32 ProfessionIcon=Net.CraftSkills.FindRef(uint32(Potion->GetNumberField(TEXT("skill"))))->GetNumberField(TEXT("icon"));
        const auto FallbackLayers=UI->ShortcutLayers(UI->Shortcuts[4]);
        TestTrue(TEXT("Recipe without an icon uses its profession artwork"),FallbackLayers.Num()==1 && FallbackLayers[0].Image==ProfessionIcon);
        Potion->SetNumberField(TEXT("icon"),0x41004a20u);
        const auto ExplicitLayers=UI->ShortcutLayers(UI->Shortcuts[4]);
        TestTrue(TEXT("Explicit recipe artwork takes priority"),ExplicitLayers.Num()==1 && ExplicitLayers[0].Image==0x41004a20u);
        Potion->SetNumberField(TEXT("icon"),0);
        Down(Center(RecipeRow),EKeys::RightMouseButton);Tick();
        TestEqual(TEXT("Recipe row right-click examines"),UI->ExaminedRecipe,Health);
        auto Lines=UI->RecipeExamination(Health);
        TestTrue(TEXT("Examination includes actual ingredient quantity"),Lines.Contains(TEXT("Ingredient: 100 x ")+UI->CraftItemName(uint32(Potion->GetArrayField(TEXT("ingredients"))[1]->AsObject()->GetArrayField(TEXT("choices"))[0]->AsNumber()))));
        TestTrue(TEXT("Examination includes output"),Lines.Contains(TEXT("Possible result: ")+UI->CraftItemName(uint32(Potion->GetArrayField(TEXT("outputs"))[0]->AsObject()->GetNumberField(TEXT("item"))))));
        TestTrue(TEXT("Inspection sends no crafting or object examination request"),Read().IsEmpty());
        UI->ExaminedRecipe=0;UI->ShowStats=false;Down(Center(TEXT("ShortcutSlot4")),EKeys::RightMouseButton);
        TestEqual(TEXT("Hotbar right-click examines recipe"),UI->ExaminedRecipe,Health);TestFalse(TEXT("Recipe inspection does not capture camera"),UI->CameraRight);
        UI->Shortcuts[1]=FAC2Shortcut::ForSkill(96);UI->InspectShortcut(1);
        TestEqual(TEXT("Skill examination clears recipe"),UI->ExaminedRecipe,0u);UI->ShowStats=false;
        UI->ShowSkills=false;Tick();UI->ActivateShortcut(4);
        TestEqual(TEXT("Shortcut opens recipe with craft book closed"),Net.OpenCraftRecipe,Health);
        TestTrue(TEXT("Opening without ingredients cannot craft"),!Net.CanCraft() && Read().IsEmpty());
        auto& Bag=Mode->Characters.Active()->Items;
        Bag={{0x47001744,0,0,TEXT("90001"),1},
             {0x470027a7,0,1,TEXT("90002"),40},
             {0x470027a7,0,2,TEXT("90003"),60}};
        Net.PopulateCraft();
        const FString Validation=Read();
        TestTrue(TEXT("Split material stacks validate through the normal craft request"),
            Validation.Contains(TEXT("\"ordinal\":2")) && Validation.Contains(TEXT("\"item\":\"90002\"")));
        for(int32 Slot=0;Slot<2;++Slot){
            const FString Reply=FString::Printf(TEXT(R"({"event":"craft_ingredient","recipe":%u,"ordinal":%d,"item":"%s","spinner":%u,"error":0})"),
                Health,Slot+1,*Bag[Slot].NetworkId,Net.CraftSpinner);
            TSharedPtr<FJsonObject> Accepted;
            if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Reply),Accepted))Net.Handle(Accepted);
        }
        TestTrue(TEXT("Server-approved split materials enable Craft"),Net.CanCraft());
        Bag[2].Quantity=59;
        TestFalse(TEXT("Removing one unit disables Craft despite earlier validation"),Net.CanCraft());
        Bag[2].Quantity=60;
        Net.ExecuteCraft();
        TestTrue(TEXT("Execution sends the selected stack and waits for completion"),
            Net.CraftPending && Read().Contains(TEXT("\"item\":\"90002\"")));
        Bag.Reset();

        Net.CloseCraft();UI->Shortcuts[5]=FAC2Shortcut::ForRecipe(VariableId);UI->ActivateShortcut(5);
        Net.CraftLevel(1);const uint32 Chosen=Net.CraftSpinner;UI->ActivateShortcut(5);
        TestEqual(TEXT("Reusing shortcut preserves selected recipe level"),Net.CraftSpinner,Chosen);
        UI->InspectShortcut(5);Lines=UI->RecipeExamination(VariableId);
        TestTrue(TEXT("Recipe examination tracks chosen level"),Lines.ContainsByPredicate([&](const FString& L){return L.Contains(FString::Printf(TEXT("(showing %u)"),Chosen));}));
        Net.CraftPending=true;UI->ActivateShortcut(4);
        TestTrue(TEXT("Different recipe cannot erase pending craft"),Net.CraftPending && Net.OpenCraftRecipe==VariableId && Net.CraftSpinner==Chosen);
        Net.CraftPending=false;Net.CloseCraft();Net.Dead=true;UI->ActivateShortcut(4);
        TestEqual(TEXT("Dead character cannot open crafting"),Net.OpenCraftRecipe,0u);Net.Dead=false;
        UI->ShowStats=false;UI->Shortcuts[2]=FAC2Shortcut::ForSkill(96);Tick();
        Drag(TEXT("ShortcutSlot4"),TEXT("ShortcutSlot2"));
        TestTrue(TEXT("Recipe moves and replaces skill binding"),UI->Shortcuts[2].Recipe==Health && UI->Shortcuts[4].IsEmpty());
        Down(Center(TEXT("ShortcutSlot2")));UI->OnMouseCaptureLost(FCaptureLostEvent());Up(Center(TEXT("ShortcutSlot8")));
        TestTrue(TEXT("Cancelled recipe drag preserves source"),UI->Shortcuts[2].Recipe==Health && UI->Shortcuts[8].IsEmpty());
        UI->Shortcuts[99]=FAC2Shortcut::ForRecipe(Health);UI->ShortcutSet=9;UI->SaveShortcuts();UI->ShortcutOwner.Reset();UI->LoadShortcuts();
        TestTrue(TEXT("Recipe and skill bindings coexist after reload"),UI->Shortcuts[99].Recipe==Health && UI->Shortcuts[1].Skill==96 && UI->ShortcutSet==9);
        Registry(false);UI->ActivateShortcut(9);
        TestTrue(TEXT("Revoked recipe is retained but cannot open"),UI->Shortcuts[99].Recipe==Health && Net.OpenCraftRecipe==0 && !UI->RecipeShortcutAllowed(Health));
        TestTrue(TEXT("Revoked recipe sends no command"),Read().IsEmpty());
        Tick();const auto A=Center(TEXT("ShortcutSlot9"));const FVector2D WorldPoint(800,180);Down(A);
        UI->OnMouseMove(G,FPointerEvent(0,WorldPoint,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(true,false,false,false,false,false,false,false,false)));Up(WorldPoint);
        TestTrue(TEXT("Unavailable recipe shortcut can still be removed"),UI->Shortcuts[99].IsEmpty());
        Registry(true);UI->SelectShortcutSet(0);UI->ActivateShortcut(2);TestEqual(TEXT("Restored registry re-enables retained recipe"),Net.OpenCraftRecipe,Health);
        UI->InspectRecipe(Health);UI->InspectTarget(Player.Id);TestEqual(TEXT("Object examination replaces recipe inspection"),UI->ExaminedRecipe,0u);Read();
        Net.ClearWorld();Tick();UI->ActivateShortcut(2);
        TestTrue(TEXT("Disconnect preserves reference without exposing old character's recipe"),UI->Shortcuts[2].Recipe==Health && !UI->RecipeShortcutAllowed(Health) && Net.OpenCraftRecipe==0);
    }
    GConfig->EmptySection(*Section,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
    FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
