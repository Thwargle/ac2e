#include "AC2Network.h"
#include "AC2GameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeEquipmentTest,"AC2.Network.NativeEquipmentLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2NativeEquipmentTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));FString Error;
    TestTrue(TEXT("Load equipment catalogue"),Mode->CharacterCatalog.Open(Mode->DatDirectory,Error));
    const FString PlayerId=TEXT("2305843009213693994"),ItemId=TEXT("9223372036854775850");
    FAC2CharacterProfile Profile;Profile.Id=PlayerId;Mode->Characters.Characters.Add(Profile);Mode->Characters.ActiveId=PlayerId;
    {
        FAC2Network Net(Mode);Net.PlayerId=PlayerId;Net.Phase=TEXT("world");
        auto Event=[&](const TCHAR* Type,const FString& Id){auto J=MakeShared<FJsonObject>();
            J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);J->SetNumberField(TEXT("instance"),0);return J;};
        Net.Handle(Event(TEXT("object"),PlayerId));
        auto Inventory=Event(TEXT("inventory"),PlayerId);
        Inventory->SetArrayField(TEXT("items"),{MakeShared<FJsonValueString>(ItemId)});
        Inventory->SetArrayField(TEXT("equipment"),{});Net.Handle(Inventory);
        auto Create=Event(TEXT("object"),ItemId);Create->SetStringField(TEXT("container"),PlayerId);
        Create->SetNumberField(TEXT("entity"),0x470007ed);auto Visual=MakeShared<FJsonObject>();
        Visual->SetNumberField(TEXT("did"),0x1f0002ae);Create->SetObjectField(TEXT("visual"),Visual);Net.Handle(Create);
        TestEqual(TEXT("Bag item is retained"),Mode->Characters.Active()->Items.Num(),1);
        TestFalse(TEXT("Bag item has no world mesh"),Net.Objects[ItemId]->Actor.IsValid());
        auto Parent=Event(TEXT("parent"),ItemId);Parent->SetStringField(TEXT("parent"),PlayerId);
        Parent->SetNumberField(TEXT("parent_instance"),0);Parent->SetNumberField(TEXT("position_stamp"),1);
        Parent->SetNumberField(TEXT("holding"),4);Net.Handle(Parent);
        TestTrue(TEXT("Native Parent builds the cached bag visual"),Net.Objects[ItemId]->Actor.IsValid());
        TestEqual(TEXT("Native holding point survives"),Net.Objects[ItemId]->Holding,4u);
        auto Equip=Event(TEXT("inventory_result"),ItemId);Equip->SetNumberField(TEXT("equipped"),64);Equip->SetNumberField(TEXT("status"),0);Net.Handle(Equip);
        TestEqual(TEXT("Successful equip updates inventory"),Mode->Characters.Active()->Items[0].Equipped,64u);
        auto Leave=Event(TEXT("world_leave"),ItemId);Leave->SetNumberField(TEXT("position_stamp"),2);Net.Handle(Leave);
        TestTrue(TEXT("LeaveWorld preserves inventory ownership"),Net.OwnsInventoryItem(ItemId));
        TestEqual(TEXT("LeaveWorld preserves item metadata"),Mode->Characters.Active()->Items.Num(),1);
        TestFalse(TEXT("LeaveWorld removes the world mesh"),Net.Objects[ItemId]->Actor.IsValid());
        Net.Handle(Parent);TestTrue(TEXT("Stale Parent cannot restore a sheathed weapon"),Net.Objects[ItemId]->Parent.IsEmpty());
        Equip->SetNumberField(TEXT("equipped"),0);Equip->SetNumberField(TEXT("slot"),0);Net.Handle(Equip);
        TestEqual(TEXT("Unequipped item returns to the bag"),Mode->Characters.Active()->Items[0].Equipped,0u);
        Parent->SetNumberField(TEXT("position_stamp"),3);Net.Handle(Parent);
        TestTrue(TEXT("Reequip restores the model without another CreateObject"),Net.Objects[ItemId]->Actor.IsValid());
        auto Detach=Event(TEXT("deparent"),ItemId);Detach->SetStringField(TEXT("parent"),PlayerId);
        Detach->SetNumberField(TEXT("parent_instance"),0);Detach->SetNumberField(TEXT("position_stamp"),4);Net.Handle(Detach);
        TestTrue(TEXT("DeParent detaches the child"),Net.Objects[ItemId]->Parent.IsEmpty());
        TestFalse(TEXT("Detached item waits for its world placement"),Net.Objects[ItemId]->Actor.IsValid());
        auto Contain=Event(TEXT("contain"),ItemId);Contain->SetNumberField(TEXT("position_stamp"),4);Net.Handle(Contain);
        TestTrue(TEXT("Contain after DeParent preserves the bag"),Net.OwnsInventoryItem(ItemId));
        TestEqual(TEXT("Contain keeps inventory metadata"),Mode->Characters.Active()->Items.Num(),1);
        Parent->SetNumberField(TEXT("position_stamp"),5);Net.Handle(Parent);
        TestTrue(TEXT("Reequip after Contain rebuilds the cached mesh"),Net.Objects[ItemId]->Actor.IsValid());
        Net.Handle(Event(TEXT("inventory_remove"),ItemId));
        TestTrue(TEXT("Actual inventory removal clears the bag"),Mode->Characters.Active()->Items.IsEmpty());
        TestFalse(TEXT("Actual inventory removal clears ownership"),Net.OwnsInventoryItem(ItemId));
        Net.Handle(Event(TEXT("remove"),ItemId));TestFalse(TEXT("Destruction clears the cached object"),Net.Objects.Contains(ItemId));
        // The same DID is a glyph in Legions and an unrelated item in Icefire.
        // Resolve the active archive's target count, never infer it from the DID.
        uint32 Glyph=0;
        for(uint32 Did:{0x47001582u,0x47001aecu,0x47001aedu}){
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Did);
            if(const auto* D=Mode->CharacterCatalog.Items.Find(Did);D && D->UseTargets==1)Glyph=Did;
        }
        if(TestTrue(TEXT("Active DAT provides a targeted training glyph"),Glyph!=0)){
            FAC2InventoryItem Item;Item.Definition=Glyph;Item.NetworkId=ItemId;Mode->Characters.Active()->Items.Add(Item);
            void* Commands=nullptr;
            if(TestTrue(TEXT("Capture targeted use without connecting"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
                Net.TargetId.Reset();Net.UseItem(0);
                TestTrue(TEXT("Glyph cannot send a targetless use"),FPlatformProcess::ReadPipe(Commands).IsEmpty());
                Net.TargetId=TEXT("9223372036854775851");Net.UseItem(0);
                const FString Sent=FPlatformProcess::ReadPipe(Commands);
                TestTrue(TEXT("Glyph request includes the selected portal"),Sent.Contains(TEXT("\"target\":\"9223372036854775851\"")));
                TestTrue(TEXT("Glyph request names the owned glyph"),Sent.Contains(TEXT("\"item\":\"9223372036854775850\"")));
                TestFalse(TEXT("Targeted use is not an equip request"),Sent.Contains(TEXT("equip")));
                FPlatformProcess::ClosePipe(Commands,Net.Write);Net.Write=nullptr;
            }
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
