#include "AC2Placement.h"
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
#endif
