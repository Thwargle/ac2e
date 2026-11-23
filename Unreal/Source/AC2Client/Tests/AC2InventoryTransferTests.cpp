#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "AC2WorldGrid.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2InventoryTransferTest,"AC2.Network.InventoryGiveAndDrop",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2InventoryTransferTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=FGuid::NewGuid().ToString();Player.Name=TEXT("Transfer tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=TEXT("2305843009213695062");Net.Cell=0x12340001;
    const FString Item=TEXT("9223372036854776099"),Recipient=TEXT("9223372036854777737");
    auto Event=[](const TCHAR* Type,const FString& Id){auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);return J;};
    auto Bag=Event(TEXT("inventory"),Net.PlayerId);Bag->SetArrayField(TEXT("items"),{MakeShared<FJsonValueString>(Item)});
    Bag->SetArrayField(TEXT("equipment"),{});Net.Handle(Bag);
    auto Create=Event(TEXT("object"),Item);Create->SetStringField(TEXT("container"),Net.PlayerId);
    Create->SetNumberField(TEXT("entity"),0x47001745);Create->SetNumberField(TEXT("quantity"),3);Net.Handle(Create);
    auto NPC=MakeUnique<FAC2Network::FObject>();NPC->Entity=1191194117; // installed Shelter Sentry
    NPC->Actor=World->SpawnActor<AActor>();NPC->Placed=true;NPC->Cell=Net.Cell;Net.Objects.Add(Recipient,MoveTemp(NPC));
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture bridge IPC"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))
    {
        const auto UI=SNew(SAC2GameUI).Mode(Mode);UI->ShowInventory=true;
        UI->Native.RootOffsets.Reset();UI->Native.RootSizes.Reset();
        const auto G=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
        UI->Tick(G,FPlatformTime::Seconds(),.016f);
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};Read();
        auto Check=[&](const TCHAR* Action){TSharedPtr<FJsonObject> J;
            if(TestTrue(TEXT("One complete inventory request"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Read()),J))){
                TestEqual(TEXT("Correct action"),J->GetStringField(TEXT("command")),FString(Action));
                TestEqual(TEXT("Preserve full item identity"),J->GetStringField(TEXT("id")),Item);
                TestEqual(TEXT("Whole stack quantity"),J->GetIntegerField(TEXT("quantity")),3);
                TestEqual(TEXT("Current bag ordinal"),J->GetIntegerField(TEXT("from")),0);
                if(FString(Action)==TEXT("give_item"))TestEqual(TEXT("Actual hovered recipient"),J->GetStringField(TEXT("target")),Recipient);
            }};
        const auto* Cell=UI->Buttons.Find(TEXT("Cell0"));
        if(TestNotNull(TEXT("Inventory drag target exists"),Cell)){
            const FVector2D Start((Cell->Left+Cell->Right)*.5,(Cell->Top+Cell->Bottom)*.5),End(800,150);
            TestFalse(TEXT("Recipient is outside all windows"),UI->OwnsPointer(End));
            auto Drag=[&](FVector2D To){const auto A=G.LocalToAbsolute(Start*UI->Scale),B=G.LocalToAbsolute(To*UI->Scale);
                UI->OnMouseButtonDown(G,FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
                UI->OnMouseMove(G,FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
                UI->OnMouseButtonUp(G,FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
            UI->WorldNameRects.Add(Recipient,FSlateRect(780,130,820,170));
            UI->HoverObject=TEXT("stale-hover");Drag(End);Check(TEXT("give_item"));
            TestTrue(TEXT("Sending a gift does not remove inventory"),Net.OwnsInventoryItem(Item));
            Net.Objects[Recipient]->Dead=true;Drag(End);
            TestTrue(TEXT("Dead recipient never falls back to a ground drop"),Read().IsEmpty());
            Net.Objects[Recipient]->Dead=false;Net.Objects[Recipient]->Entity=0x47001745;Drag(End);
            TestTrue(TEXT("Scenery/item recipient never becomes ground drop"),Read().IsEmpty());
            Net.Objects.Remove(Recipient);Drag(End);
            TestTrue(TEXT("Stale nameplate cannot give or accidentally drop"),Read().IsEmpty());
            Drag(FVector2D(-20,150));TestTrue(TEXT("Release outside viewport cancels"),Read().IsEmpty());
        }
        Net.DropItem(0);Check(TEXT("drop_item"));
        TestTrue(TEXT("Drop waits for authority"),Net.OwnsInventoryItem(Item));
        Mode->Characters.Active()->Items[0].Equipped=true;Net.DropItem(0);Net.GiveItem(0,Recipient);
        TestTrue(TEXT("Worn item cannot be transferred directly"),Read().IsEmpty());Mode->Characters.Active()->Items[0].Equipped=false;
        auto Ack=Event(TEXT("inventory_acquired"),Item);Ack->SetStringField(TEXT("source_item"),Item);
        Ack->SetStringField(TEXT("from"),Net.PlayerId);Ack->SetStringField(TEXT("owner"),Recipient);
        Ack->SetNumberField(TEXT("status"),54);Net.Handle(Ack);
        TestTrue(TEXT("Rejected gift preserves inventory"),Net.OwnsInventoryItem(Item));
        Ack->SetNumberField(TEXT("status"),0);Ack->SetStringField(TEXT("id"),TEXT("9223372036854777740"));Net.Handle(Ack);
        TestTrue(TEXT("Split transfer preserves original stack"),Net.OwnsInventoryItem(Item));
        Ack->SetStringField(TEXT("id"),Item);Net.Handle(Ack);Net.Handle(Ack);Net.Handle(Event(TEXT("inventory_remove"),Item));
        TestFalse(TEXT("Confirmed whole gift releases ownership once"),Net.OwnsInventoryItem(Item));
        TestTrue(TEXT("Confirmed transfer leaves no phantom inventory row"),Mode->Characters.Active()->Items.IsEmpty());
        TestTrue(TEXT("Transfer never echoes another request"),Read().IsEmpty());
        // Native drops may consist only of Position after the cached bag
        // CreateObject. Exercise both orders of physics and inventory notices.
        auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),0x1f0002ae);
        Create->SetObjectField(TEXT("visual"),Visual);Create->SetNumberField(TEXT("entity"),0x470007ed);
        Create->SetNumberField(TEXT("instance"),0);Create->SetNumberField(TEXT("position_stamp"),10);
        Net.Handle(Bag);Net.Handle(Create);
        TestFalse(TEXT("Bag visual is cached without spawning it"),Net.Objects[Item]->Actor.IsValid());
        Net.Handle(Event(TEXT("inventory_remove"),Item));
        TestFalse(TEXT("Inventory removal alone never invents a world placement"),Net.Objects[Item]->Actor.IsValid());
        auto Position=Event(TEXT("position"),Item);Position->SetNumberField(TEXT("instance"),0);
        Position->SetNumberField(TEXT("position_stamp"),11);Position->SetNumberField(TEXT("cell"),Net.Cell);
        Position->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(3)});
        auto CheckGround=[&](){const auto& Obj=*Net.Objects[Item];
            TestTrue(TEXT("Dropped item has world placement and no parent"),Obj.Placed && Obj.Parent.IsEmpty());
            if(TestTrue(TEXT("Drop renders the actual cached DAT model"),Obj.Actor.IsValid() && Obj.Model.IsValid())){
                TestTrue(TEXT("Dropped model contains geometry"),Obj.Actor->GetComponentsBoundingBox(true).IsValid!=0);
                TestTrue(TEXT("Drop uses server world coordinates"),Obj.Actor->GetActorLocation().Equals(AC2::BlockOrigin(Net.Cell>>16)+FVector(100,200,300)));
            }
            TestFalse(TEXT("World item is no longer owned"),Net.OwnsInventoryItem(Item));
        };
        Net.Handle(Position);CheckGround();
        auto Contain=Event(TEXT("contain"),Item);Contain->SetNumberField(TEXT("instance"),0);
        Contain->SetNumberField(TEXT("position_stamp"),12);Net.Handle(Contain);Net.Handle(Bag);
        TestFalse(TEXT("Picking up removes the world model"),Net.Objects[Item]->Actor.IsValid());
        Net.Handle(Position);Position->SetNumberField(TEXT("position_stamp"),12);Net.Handle(Position);
        TestFalse(TEXT("Older and duplicate positions cannot resurrect a picked-up item"),Net.Objects[Item]->Actor.IsValid());
        Position->SetNumberField(TEXT("instance"),1);Position->SetNumberField(TEXT("position_stamp"),13);Net.Handle(Position);
        TestTrue(TEXT("Different instance cannot release inventory"),Net.OwnsInventoryItem(Item));
        Position->SetNumberField(TEXT("instance"),0);Net.Handle(Position);CheckGround();
        const auto GroundActor=Net.Objects[Item]->Actor;
        Net.Handle(Position);Net.Handle(Event(TEXT("inventory_remove"),Item));
        TestTrue(TEXT("Duplicate position and late bag removal preserve the world actor"),Net.Objects[Item]->Actor==GroundActor);
        // A complete world create can also arrive before its bag removal.
        Net.Handle(Bag);Create->SetStringField(TEXT("container"),TEXT("0"));
        Create->SetObjectField(TEXT("placement"),Position);Create->SetNumberField(TEXT("position_stamp"),14);
        Net.Handle(Create);CheckGround();
        // Recreating the item in a container must clear the old world pose.
        Create->RemoveField(TEXT("placement"));Create->SetStringField(TEXT("container"),Net.PlayerId);
        Net.Handle(Create);TestFalse(TEXT("Container create cannot leave an old ground mesh"),Net.Objects[Item]->Actor.IsValid());
        TestTrue(TEXT("Container create restores inventory ownership"),Net.OwnsInventoryItem(Item));
        Contain->SetNumberField(TEXT("position_stamp"),0xffff);Net.Objects[Item]->PositionStamp=0xfffe;Net.Handle(Contain);
        Position->SetNumberField(TEXT("position_stamp"),0);Net.Handle(Position);CheckGround();
        TestTrue(TEXT("World lifecycle does not send speculative requests"),Read().IsEmpty());
        FPlatformProcess::ClosePipe(Commands,Net.Write);Net.Write=nullptr;
    }
    Mode->Network.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
