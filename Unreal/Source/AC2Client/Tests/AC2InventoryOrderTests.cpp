#include "AC2Network.h"
#include "AC2GameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2InventoryOrderTest,"AC2.Network.InventoryOrderingAndLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2InventoryOrderTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    FAC2CharacterProfile Profile;Profile.Id=TEXT("17");Mode->Characters.Characters.Add(Profile);Mode->Characters.ActiveId=Profile.Id;
    {
        FAC2Network Net(Mode);Net.PlayerId=Profile.Id;Net.Phase=TEXT("world");
        auto Event=[](const TCHAR* Type,const FString& Id){auto J=MakeShared<FJsonObject>();
            J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);J->SetNumberField(TEXT("instance"),0);return J;};
        auto Cache=[&](const FString& Id){auto Obj=MakeUnique<FAC2Network::FObject>();Obj->Entity=0x470007ed;
            Obj->CachedVisual=MakeShared<FJsonObject>();Obj->CachedVisual->SetNumberField(TEXT("did"),0x1f0002ae);
            Net.Objects.Add(Id,MoveTemp(Obj));};
        auto Order=[&](const TArray<FString>& Ids){TestEqual(TEXT("Bag count"),Net.BagSlots.Num(),Ids.Num());
            for(int32 I=0;I<Ids.Num();++I){TestTrue(TEXT("Server bag identity"),Net.BagSlots.Contains(Ids[I]));
                TestEqual(TEXT("Server bag ordinal"),Net.BagSlots.FindRef(Ids[I]),I);}};
        Cache(TEXT("a"));Cache(TEXT("b"));Cache(TEXT("loot"));
        auto Bag=Event(TEXT("inventory"),Profile.Id);Bag->SetArrayField(TEXT("items"),
            {MakeShared<FJsonValueString>(TEXT("a")),MakeShared<FJsonValueString>(TEXT("b"))});
        Bag->SetArrayField(TEXT("equipment"),{});Net.Handle(Bag);
        TestEqual(TEXT("Snapshot adopts cached metadata"),Mode->Characters.Active()->Items.Num(),2);
        Net.LootContainer=TEXT("corpse");Net.LootItems={TEXT("loot")};Net.TakeLoot(0);
        auto Contain=Event(TEXT("contain"),TEXT("loot"));Contain->SetNumberField(TEXT("position_stamp"),1);Net.Handle(Contain);
        TestTrue(TEXT("Contain before pickup keeps metadata"),Net.Objects.Contains(TEXT("loot")));
        auto Acquired=Event(TEXT("inventory_acquired"),TEXT("loot"));Acquired->SetStringField(TEXT("owner"),Profile.Id);
        Acquired->SetStringField(TEXT("from"),TEXT("corpse"));Acquired->SetStringField(TEXT("source_item"),TEXT("loot"));
        Acquired->SetNumberField(TEXT("slot"),1);Acquired->SetNumberField(TEXT("status"),0);Net.Handle(Acquired);Net.Handle(Acquired);
        Order({TEXT("a"),TEXT("loot"),TEXT("b")});
        TestTrue(TEXT("Pickup changes ownership without CreateObject"),Net.OwnsInventoryItem(TEXT("loot")));
        TestTrue(TEXT("Picked-up item leaves corpse"),Net.LootItems.IsEmpty());
        TestEqual(TEXT("No duplicate inventory rows"),Mode->Characters.Active()->Items.Num(),3);
        auto Equip=Event(TEXT("inventory_result"),TEXT("a"));Equip->SetNumberField(TEXT("equipped"),64);
        Equip->SetNumberField(TEXT("status"),0);Net.Handle(Equip);Net.Handle(Equip);
        Order({TEXT("loot"),TEXT("b")});
        Equip->SetNumberField(TEXT("equipped"),0);Equip->SetNumberField(TEXT("slot"),1);Net.Handle(Equip);Net.Handle(Equip);
        Order({TEXT("loot"),TEXT("a"),TEXT("b")});
        auto Move=Event(TEXT("inventory_result"),TEXT("loot"));Move->SetNumberField(TEXT("slot"),2);
        Move->SetNumberField(TEXT("status"),0);Net.Handle(Move);Net.Handle(Move);
        Order({TEXT("a"),TEXT("b"),TEXT("loot")});
        Acquired->SetStringField(TEXT("id"),TEXT("late"));Acquired->SetStringField(TEXT("source_item"),TEXT("late"));
        Acquired->SetNumberField(TEXT("slot"),1);Net.Handle(Acquired);
        TestTrue(TEXT("Move ack before metadata retains ownership"),Net.OwnsInventoryItem(TEXT("late")));
        Cache(TEXT("late"));Net.SyncInventoryItems();Order({TEXT("a"),TEXT("late"),TEXT("b"),TEXT("loot")});
        TestEqual(TEXT("Late metadata becomes usable inventory"),Mode->Characters.Active()->Items.Num(),4);
        Acquired->SetStringField(TEXT("id"),TEXT("failed"));Acquired->SetNumberField(TEXT("status"),54);Net.Handle(Acquired);
        TestFalse(TEXT("Failed transfer grants nothing"),Net.OwnsInventoryItem(TEXT("failed")));
        Net.Handle(Event(TEXT("remove"),TEXT("b")));Order({TEXT("a"),TEXT("late"),TEXT("loot")});
        Net.LootItems={TEXT("taken"),TEXT("remaining")};Net.PendingPickupSounds.Add(TEXT("taken"));Net.PendingPickupSounds.Add(TEXT("remaining"));
        auto Bulk=Event(TEXT("inventory_bulk"),TEXT("corpse"));Bulk->SetStringField(TEXT("owner"),Profile.Id);
        Bulk->SetBoolField(TEXT("transmute"),false);Bulk->SetNumberField(TEXT("status"),0);
        Bulk->SetArrayField(TEXT("items"),{MakeShared<FJsonValueString>(TEXT("taken"))});
        Bulk->SetArrayField(TEXT("remaining"),{MakeShared<FJsonValueString>(TEXT("remaining"))});Net.Handle(Bulk);Net.Handle(Bulk);
        TestTrue(TEXT("Bulk reply keeps rejected items in corpse"),Net.LootItems==TArray<FString>{TEXT("remaining")});
        TestTrue(TEXT("Bulk reply settles pending transfers"),Net.PendingPickupSounds.IsEmpty());
        Order({TEXT("a"),TEXT("late"),TEXT("loot")});
        const int32 Count=Net.Objects.Num();
        for(int32 I=0;I<2100;++I){const FString Id=FString::FromInt(10000+I);
            Net.Handle(Event(TEXT("position"),Id));Net.Handle(Event(TEXT("visual"),Id));}
        TestEqual(TEXT("Orphan updates cannot exhaust object budget"),Net.Objects.Num(),Count);
        Net.Handle(Event(TEXT("object"),TEXT("new_enemy")));
        TestTrue(TEXT("A real create still succeeds after stale traffic"),Net.Objects.Contains(TEXT("new_enemy")));
        Net.Handle(Event(TEXT("remove"),TEXT("new_enemy")));Net.Handle(Event(TEXT("position"),TEXT("new_enemy")));
        TestFalse(TEXT("Deleted enemy is not resurrected by a late position"),Net.Objects.Contains(TEXT("new_enemy")));
        auto Respawn=Event(TEXT("object"),TEXT("new_enemy"));Respawn->SetNumberField(TEXT("instance"),2);Net.Handle(Respawn);
        auto Health=Event(TEXT("quality"),TEXT("new_enemy"));Health->SetNumberField(TEXT("table"),4);
        Health->SetNumberField(TEXT("key"),0x102);Health->SetNumberField(TEXT("value"),0);Net.Handle(Health);
        TestEqual(TEXT("Old death update cannot kill the replacement"),Net.Objects[TEXT("new_enemy")]->Health,-1.f);
        Health->SetNumberField(TEXT("instance"),2);Health->SetNumberField(TEXT("value"),50);Net.Handle(Health);
        TestEqual(TEXT("Current instance health is applied"),Net.Objects[TEXT("new_enemy")]->Health,50.f);
        Net.Handle(Event(TEXT("object"),Profile.Id));
        auto ModeEvent=Event(TEXT("mode"),Profile.Id);ModeEvent->SetNumberField(TEXT("mode"),0x41000010);Net.Handle(ModeEvent);
        TestTrue(TEXT("Native missile mode updates combat toggle"),Net.Combat);
        ModeEvent->SetNumberField(TEXT("mode"),AC2Motion::Peace);Net.Handle(ModeEvent);
        TestFalse(TEXT("Native peace mode updates combat toggle"),Net.Combat);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
