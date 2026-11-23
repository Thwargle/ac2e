#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Placement.h"
#include "AC2WorldGrid.h"
#include "AC2WorldStream.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TumerokPassageTest,"AC2.Data.TumerokPassageWalking",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TumerokPassageTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    Mode->LoginStarted=true;Mode->CharacterUIStarted=true;
    auto* PC=World->SpawnActor<APlayerController>();PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    Mode->Stream=MakeUnique<FAC2WorldStream>();Mode->Stream->Radius=0;
    const auto Cleanup=[&](){Mode->Stream.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    FString Error;
    if(!TestTrue(TEXT("Open native passage"),Mode->Stream->Open(World,Mode->DatDirectory,Error)))
    {AddError(Error);Cleanup();return false;}
    const FVector Origin=AC2::BlockOrigin(0x03fd);
    Mode->Stream->SetInteriorCell(0x03fd012a);
    const double Deadline=FPlatformTime::Seconds()+90;
    while(!Mode->Stream->AllReady() && FPlatformTime::Seconds()<Deadline)
    {Mode->Stream->Tick(Origin);World->SendAllEndOfFrameUpdates();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.01f);}
    if(!TestTrue(TEXT("Passage collision ready"),Mode->Stream->AllReady())){Cleanup();return false;}
    FAC2CharacterCatalog Catalog;
    if(!TestTrue(TEXT("Avatar catalog"),Catalog.Open(Mode->DatDirectory,Error))){AddError(Error);Cleanup();return false;}
    auto* M=Player->GetCharacterMovement();
    const FVector Feet=Origin+FVector(-284.00177,345.83524,-74.06845)*100;
    int32 Variants=0,RayMisses=0;
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4)
    for(bool Largest:{false,true})
    {
        const float Height=Largest?Avatar.Heights.Last():Avatar.Heights[0];
        const float Build=Largest?Avatar.Builds.Last():Avatar.Builds[0];
        if(!TestTrue(TEXT("Tumerok model"),Player->ApplyAvatar(Avatar.Physics,Height,Build,Avatar.Resolve(Avatar.Defaults()))))continue;
        ++Variants;
        const float Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
        FVector Start;
        if(!TestTrue(TEXT("Reported feet are standable"),AC2::StandingPosition(World,Feet,Radius,Half,Start,Player)))continue;
        Player->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
        Player->SetHeld(false);Player->SetRunning(true);M->StopMovementImmediately();
        // Follow the corridor bend, then return across the same doorway. These
        // are movement inputs, not teleports or an A* approximation of stepping.
        const TArray<FVector> Goals={
            Origin+FVector(-280.564,342.132,-75)*100,
            Origin+FVector(-280.564,337.5,-73.5)*100,
            Origin+FVector(-280.564,342.132,-75)*100,Start};
        FVector LastSafe=Start;bool RouteOK=true;
        for(int32 Leg=0;Leg<Goals.Num() && RouteOK;++Leg)
        {
            for(int32 Frame=0;Frame<300 && FVector::Dist2D(Player->GetActorLocation(),Goals[Leg])>25;++Frame)
            {
                const FVector Direction=(Goals[Leg]-Player->GetActorLocation()).GetSafeNormal2D();
                Player->SetActorRotation(Direction.Rotation());PC->SetControlRotation(Direction.Rotation());
                Player->Tick(1.f/60);Player->AddMovementInput(Direction);
                M->TickComponent(1.f/60,LEVELTICK_All,nullptr);
                uint32 Cell=0x03fd012a;
                if(Mode->Stream->InteriorCellAt(Player->GetActorLocation()-FVector(0,0,Half),Cell))Mode->Stream->SetInteriorCell(Cell);
                double Ground;
                if(M->IsMovingOnGround() && !Mode->Stream->GroundAt(Player->GetActorLocation(),Ground))++RayMisses;
                if(!Mode->HasGroundSupport(Player,Ground))
                {
                    AddError(FString::Printf(TEXT("Ground safety rejects a walking step, sex %X size %d leg %d at %s"),
                        Avatar.Sex,int(Largest),Leg,*((Player->GetActorLocation()-Origin)/100).ToString()));
                    Player->SetActorLocation(LastSafe);M->StopMovementImmediately();RouteOK=false;break;
                }
                LastSafe=Player->GetActorLocation();
            }
            if(!TestTrue(FString::Printf(TEXT("Passage leg %d, sex %X size %d"),Leg,Avatar.Sex,int(Largest)),
                FVector::Dist2D(Player->GetActorLocation(),Goals[Leg])<=25))RouteOK=false;
        }
        // Keep the safety guard: a stale walking contact cannot authorize a
        // teleport over empty space, and flying/falling must not reuse it.
        double Ground;
        Player->SetActorLocation(Origin+FVector(500000,500000,50000),false,nullptr,ETeleportType::TeleportPhysics);
        TestFalse(TEXT("Teleport does not reuse the previous floor"),Mode->HasGroundSupport(Player,Ground));
        M->SetMovementMode(MOVE_Falling);
        TestFalse(TEXT("Unsupported falling position is rejected"),Mode->HasGroundSupport(Player,Ground));
    }
    TestEqual(TEXT("Both sexes and size extremes walked"),Variants,4);
    TestTrue(TEXT("Regression exercises actual native floor ray misses"),RayMisses>0);
    AddInfo(FString::Printf(TEXT("Walked both directions for %d avatars; %d supported steps missed by the old point ray."),Variants,RayMisses));
    // Native animated bridge 47000050 at the reported 254.4,40731.8 location.
    // Its unweighted physics part belongs to the skinned group frame, not to
    // the second decorative bone. Exercise the real walking capsule uphill.
    Mode->Stream->SetInteriorCell(0x03fd0120);
    Mode->Stream->Tick(Origin);
    const FVector RampPoint=Origin+FVector(-225.6,251.8,-71.539)*100;
    FHitResult RampHit;FCollisionQueryParams Q;Q.AddIgnoredActor(Player);
    TestTrue(TEXT("Native bridge supports reported position"),World->LineTraceSingleByChannel(RampHit,
        RampPoint+FVector(0,0,80),RampPoint-FVector(0,0,80),ECC_Pawn,Q));
    TestTrue(TEXT("Collision agrees with native bridge surface"),FMath::Abs(RampHit.ImpactPoint.Z-RampPoint.Z)<8);
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4)
    for(bool Largest:{false,true}) {
        Player->ApplyAvatar(Avatar.Physics,Largest?Avatar.Heights.Last():Avatar.Heights[0],
            Largest?Avatar.Builds.Last():Avatar.Builds[0],Avatar.Resolve(Avatar.Defaults()));
        const float Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FVector Start;
        // The foot of the native ramp is buried in the cave floor at -72.5 m.
        // Start on that floor, not below it at the mesh's lowest vertex.
        if(!TestTrue(TEXT("Lower ramp is standable"),AC2::StandingPosition(World,
            Origin+FVector(-221.5,254.1,-72.5)*100,Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half,Start,Player)))continue;
        Player->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
        Player->SetHeld(false);Player->SetRunning(true);M->StopMovementImmediately();M->SetMovementMode(MOVE_Walking);
        const TArray<FVector> Goals={Origin+FVector(-225.53,251.4,-71.46)*100,
            Origin+FVector(-228.56,249.38,-69.92)*100,Origin+FVector(-232.3,247.35,-67.6)*100,Start};
        for(int32 Leg=0;Leg<Goals.Num();++Leg) {
            for(int32 Frame=0;Frame<300 && FVector::Dist2D(Player->GetActorLocation(),Goals[Leg])>30;++Frame) {
                const FVector Direction=(Goals[Leg]-Player->GetActorLocation()).GetSafeNormal2D();
                Player->SetActorRotation(Direction.Rotation());PC->SetControlRotation(Direction.Rotation());
                Player->Tick(1.f/60);Player->AddMovementInput(Direction);M->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            }
            TestTrue(FString::Printf(TEXT("Bridge leg %d sex %X size %d at %s"),Leg,Avatar.Sex,int(Largest),
                *((Player->GetActorLocation()-Origin)/100).ToString()),FVector::Dist2D(Player->GetActorLocation(),Goals[Leg])<=30);
            if(Leg==2)TestTrue(TEXT("Capsule climbed native bridge"),Player->GetActorLocation().Z-Start.Z>450);
        }
    }
    Cleanup();
    return true;
}
#endif
