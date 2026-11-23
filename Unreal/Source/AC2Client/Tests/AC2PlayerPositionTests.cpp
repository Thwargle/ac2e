#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2WorldGrid.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PlayerPositionTest,"AC2.Network.PlayerPositionStamps",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2PlayerPositionTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    auto* PC=World->SpawnActor<APlayerController>();
    auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    {
        FAC2Network Net(Mode);Net.PlayerId=TEXT("42");Net.Phase=TEXT("entering");
        auto Warning=MakeShared<FJsonObject>();Warning->SetStringField(TEXT("event"),TEXT("error"));
        Warning->SetStringField(TEXT("message"),TEXT("Profile decoder warning"));Net.Handle(Warning);
        TestEqual(TEXT("Early profile warning does not cancel entry"),Net.Phase,FString(TEXT("entering")));
        auto Pose=MakeShared<FJsonObject>();Pose->SetNumberField(TEXT("cell"),0x03fd0113);
        Pose->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(1),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(3)});
        Pose->SetNumberField(TEXT("heading"),45);
        auto Create=MakeShared<FJsonObject>();Create->SetStringField(TEXT("event"),TEXT("object"));
        Create->SetStringField(TEXT("id"),Net.PlayerId);Create->SetNumberField(TEXT("instance"),0);
        Create->SetObjectField(TEXT("placement"),Pose);Net.Handle(Create);
        TestTrue(TEXT("First player pose requests one arrival"),Net.HasPlayerPose && Net.NeedsPlacement);
        Net.NeedsPlacement=false;Net.Phase=TEXT("world");
        Player->SetActorLocation(FVector(20,30,40));Player->SetActorRotation(FRotator(0,123,0));
        Player->GetCharacterMovement()->Velocity=FVector(25,50,0);
        auto Update=MakeShared<FJsonObject>(*Pose);Update->SetStringField(TEXT("event"),TEXT("position"));
        Update->SetStringField(TEXT("id"),Net.PlayerId);Update->SetNumberField(TEXT("instance"),0);
        Update->SetNumberField(TEXT("teleport"),0);Update->SetNumberField(TEXT("force"),0);
        for(int32 I=1;I<=800;++I){Update->SetNumberField(TEXT("position_stamp"),I);Net.Handle(Update);}
        TestFalse(TEXT("800 self echoes never restart portal space"),Net.NeedsPlacement);
        TestTrue(TEXT("Echoes preserve the locally driven body"),Player->GetActorLocation().Equals(FVector(20,30,40)));
        TestTrue(TEXT("Echoes preserve movement momentum"),Player->GetVelocity().Equals(FVector(25,50,0)));
        Update->SetNumberField(TEXT("position_stamp"),801);Update->SetNumberField(TEXT("force"),1);Net.Handle(Update);
        const FVector Corrected=AC2::BlockOrigin(0x03fd)+FVector(100,200,300+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        TestTrue(TEXT("Fresh force stamp corrects the body"),Player->GetActorLocation().Equals(Corrected));
        TestEqual(TEXT("Force correction keeps facing"),Player->GetActorRotation().Yaw,123.);
        TestFalse(TEXT("Force correction does not enter portal space"),Net.NeedsPlacement);
        Update->SetNumberField(TEXT("position_stamp"),802);Update->SetNumberField(TEXT("teleport"),1);Net.Handle(Update);
        TestTrue(TEXT("Fresh teleport starts an arrival"),Net.NeedsPlacement);
        Net.NeedsPlacement=false;
        Update->SetNumberField(TEXT("position_stamp"),803);Net.Handle(Update);
        TestFalse(TEXT("Repeated teleport counter does not reopen arrival"),Net.NeedsPlacement);
        Update->SetNumberField(TEXT("position_stamp"),800);Update->SetNumberField(TEXT("teleport"),2);Net.Handle(Update);
        TestFalse(TEXT("Out of order position cannot teleport"),Net.NeedsPlacement);
        Update->SetNumberField(TEXT("position_stamp"),804);Update->SetNumberField(TEXT("instance"),1);Net.Handle(Update);
        TestFalse(TEXT("Instance zero is checked against stale instances"),Net.NeedsPlacement);
        auto& Obj=*Net.Objects[Net.PlayerId];Obj.PositionStamp=Obj.TeleportStamp=65535;
        Update->SetNumberField(TEXT("instance"),0);Update->SetNumberField(TEXT("position_stamp"),0);
        Update->SetNumberField(TEXT("teleport"),0);Net.Handle(Update);
        TestTrue(TEXT("Native counter wrap still teleports"),Net.NeedsPlacement);
        Net.NeedsPlacement=false;
        Update->SetNumberField(TEXT("position_stamp"),1);Update->SetNumberField(TEXT("teleport"),65535);Net.Handle(Update);
        TestFalse(TEXT("Old pre-wrap teleport does not reopen arrival"),Net.NeedsPlacement);

        Create->SetStringField(TEXT("id"),TEXT("43"));Net.Handle(Create);
        auto& Peer=*Net.Objects[TEXT("43")];
        auto* PeerActor=World->SpawnActor<AAC2Explorer>();Peer.Actor=PeerActor;
        const FVector Start=Peer.Goal;
        PeerActor->SetActorLocation(Start);
        Update->SetStringField(TEXT("id"),TEXT("43"));
        Update->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(3),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(3)});
        Update->SetNumberField(TEXT("teleport"),0);Net.Handle(Update);
        TestTrue(TEXT("Ordinary observer updates interpolate from the current body"),Peer.PresentationMotion.Active && Peer.PresentationMotion.From.Equals(Start));
        Update->SetNumberField(TEXT("position_stamp"),2);Update->SetNumberField(TEXT("teleport"),1);Net.Handle(Update);
        TestTrue(TEXT("A short remote teleport snaps directly to the destination"),
            !Peer.PresentationMotion.Active && PeerActor->GetActorLocation().Equals(Start+FVector(200,0,0)) && PeerActor->GetActorQuat().Equals(Peer.Rotation));
        TestFalse(TEXT("An observed teleport leaves the local camera in world space"),Net.NeedsPlacement);
        Update->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(4),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(3)});
        Update->SetNumberField(TEXT("position_stamp"),3);Net.Handle(Update);
        TestTrue(TEXT("Ordinary observer motion resumes after the teleport"),Peer.PresentationMotion.Active &&
            Peer.PresentationMotion.From.Equals(Start+FVector(200,0,0)) && Peer.PresentationMotion.To.Equals(Start+FVector(300,0,0)));
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
