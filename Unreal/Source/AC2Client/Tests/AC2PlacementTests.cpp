#include "AC2Placement.h"
#include "AC2CellConnectivity.h"
#include "AC2WorldStream.h"
#include "AC2WorldGrid.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PlacementTest, "AC2.Data.CapsuleLandingClearance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2PlacementTest::RunTest(const FString &Parameters)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    UWorld *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Box = [&](FVector Center, FVector Extent) {
        auto *Actor = World->SpawnActor<AActor>();
        auto *C = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(C);
        Actor->AddInstanceComponent(C);
        C->SetBoxExtent(Extent);
        C->SetCollisionProfileName(TEXT("BlockAll"));
        C->RegisterComponent();
        Actor->SetActorLocation(Center);
        return Actor;
    };
    Box(FVector(0, 0, -10), FVector(2000, 2000, 10));
    FVector Landing;
    TestTrue(TEXT("find flat floor"), AC2::StandingPosition(World, FVector::ZeroVector, 34, 103, Landing));
    TestTrue(TEXT("capsule half height plus floor gap, not the old embedded Z+100"), Landing.Z >= 105.9);
    TestTrue(TEXT("landing clears actual capsule"), AC2::CapsuleClear(World, Landing, 34, 103));
    auto *Tree = Box(FVector(0, 0, 200), FVector(40, 40, 200));
    TestFalse(TEXT("tree at destination rejects landing"),
              AC2::StandingPosition(World, FVector::ZeroVector, 34, 103, Landing));
    TestFalse(TEXT("full capsule catches tree beside floor point"),
              AC2::StandingPosition(World, FVector(65, 0, 0), 34, 103, Landing));
    TestTrue(TEXT("nearby clear fallback remains possible"),
             AC2::StandingPosition(World, FVector(120, 0, 0), 34, 103, Landing));
    Tree->Destroy();
    auto *Ceiling = Box(FVector(600, 0, 240), FVector(200, 200, 10));
    TestTrue(TEXT("230cm room admits a 206cm capsule"),
             AC2::StandingPosition(World, FVector(600, 0, 0), 34, 103, Landing));
    Ceiling->SetActorLocation(FVector(600, 0, 210));
    TestFalse(TEXT("200cm room rejects a 206cm capsule"),
              AC2::StandingPosition(World, FVector(600, 0, 0), 34, 103, Landing));
    TestFalse(TEXT("floor search cannot fall to an unrelated storey"),
              AC2::StandingPosition(World, FVector(1200, 0, 400), 34, 103, Landing));
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CellConnectivityTest, "AC2.Data.InteriorConnectivity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CellConnectivityTest::RunTest(const FString&)
{
    FAC2CellConnectivity Graph;
    Graph.Connect(0x02990109,0x0299011b);
    Graph.Connect(0x0297013e,0x0297013f);
    Graph.Connect(0x56670100,0x56670101);
    Graph.Outside.Add(0x56670101);
    TestTrue(TEXT("Connected room retained"),Graph.Reachable(0x02990109).Contains(0x0299011b));
    TestFalse(TEXT("Overlapping unrelated dungeon excluded"),Graph.Reachable(0x02990109).Contains(0x0297013e));
    TestFalse(TEXT("Sealed dungeon excludes landscape"),Graph.ReachesOutside(0x02990109));
    TestTrue(TEXT("Exterior-connected building remains accessible"),Graph.Reachable(0).Contains(0x56670100));
    TestTrue(TEXT("Inside a building can see the outside"),Graph.ReachesOutside(0x56670100));
    return true;
}

// Exercise real DAT rooms and cooked collision at the reported human start.
class FNativeDungeonLandingCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* World = nullptr;
    TUniquePtr<FAC2WorldStream> Stream;
    double Started = FPlatformTime::Seconds();
    FVector Feet = AC2::BlockOrigin(0x0299) + FVector(5906.0577,24019.9,-4460.6205);
public:
    explicit FNativeDungeonLandingCommand(FAutomationTestBase* In) : Test(In) {}
    ~FNativeDungeonLandingCommand()
    {
        Stream.Reset();
        if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
    }
    bool Update() override
    {
        if (!World)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
            World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Stream = MakeUnique<FAC2WorldStream>(); FString Error;
            if (!Test->TestTrue(TEXT("Open starter dungeon DATs"),Stream->Open(World,AC2::ResolveDatDirectory(TEXT("")),Error))) return true;
            Stream->SetInteriorCell(0x02990109);
        }
        Stream->Tick(AC2::BlockOrigin(0x0299)+FVector(8000,8000,Feet.Z));
        if (!Stream->AllReady())
        {
            if (FPlatformTime::Seconds()-Started<60) return false;
            Test->AddError(TEXT("Starter dungeon collision did not finish loading")); return true;
        }
        Test->TestTrue(TEXT("Requested starter room visible"),Stream->CellVisible(0x02990109));
        Test->TestFalse(TEXT("Overlapping room 0297013E hidden"),Stream->CellVisible(0x0297013e));
        Test->TestFalse(TEXT("Sealed starter dungeon hides exterior terrain"),Stream->ExteriorVisible());
        uint32 Cell=0;
        Test->TestTrue(TEXT("Starter feet resolve to their native cell"),Stream->InteriorCellAt(Feet,Cell)&&Cell==0x02990109);
        for (FVector2D Size : {FVector2D(34,103),FVector2D(55,165)})
        {
            FVector Center = FVector::ZeroVector;
            Test->TestTrue(TEXT("Native arrival has a walkable clear landing"),Stream->NetworkLanding(Feet,0x02990109,Size.X,Size.Y,Center,nullptr));
            Test->TestTrue(TEXT("Full arrival capsule clear"),AC2::CapsuleClear(World,Center,Size.X,Size.Y));
            Test->TestTrue(TEXT("Landing stays near the server position"),FVector::Dist2D(Center,Feet)<401);
        }
        FVector AirCenter = FVector::ZeroVector;
        const FVector AirFeet=Feet+FVector(0,0,200);
        Test->TestTrue(TEXT("Saved mid-jump arrival remains usable"),Stream->NetworkLanding(AirFeet,0x02990109,34,103,AirCenter,nullptr));
        Test->TestTrue(TEXT("Clear airborne position is retained for gravity"),(AirCenter-FVector(0,0,103)).Equals(AirFeet,.01));
        auto* Wall = World->SpawnActor<AActor>();
        auto* Shape = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Shape); Wall->AddInstanceComponent(Shape);
        Shape->SetBoxExtent(FVector(60,60,250)); Shape->SetCollisionProfileName(TEXT("BlockAll")); Shape->RegisterComponent();
        Wall->SetActorLocation(Feet+FVector(0,0,150));
        FVector Center = FVector::ZeroVector;
        Test->TestFalse(TEXT("Blocked arrival reproduces embedded capsule"),AC2::CapsuleClear(World,Feet+FVector(0,0,103),34,103));
        Test->TestTrue(TEXT("Blocked network arrival finds nearby floor"),Stream->NetworkLanding(Feet,0x02990109,34,103,Center,nullptr));
        Test->TestTrue(TEXT("Fallback clears wall and floor"),AC2::CapsuleClear(World,Center,34,103));
        Wall->Destroy();
        Stream->SetInteriorCell(0x0297013e);
        Test->TestFalse(TEXT("Changing dungeon hides previous rooms"),Stream->CellVisible(0x02990109));
        Test->TestTrue(TEXT("Changing dungeon activates its own rooms"),Stream->CellVisible(0x0297013e));
        Stream->SetInteriorCell(0);
        Test->TestTrue(TEXT("Leaving dungeon restores exterior"),Stream->ExteriorVisible());
        Test->TestFalse(TEXT("Sealed dungeon is not an outdoor room"),Stream->CellVisible(0x02990109));
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeDungeonLandingTest,"AC2.Data.NativeDungeonLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2NativeDungeonLandingTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FNativeDungeonLandingCommand(this));
    return true;
}
#endif
