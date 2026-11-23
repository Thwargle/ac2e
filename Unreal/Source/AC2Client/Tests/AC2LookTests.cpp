#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LookLifecycleTest,"AC2.Data.PlayerLookLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LookLifecycleTest::RunTest(const FString&)
{
    const FVector Feet(100,200,-300);
    for(double Scale:{.8,1.,1.2})
    {
        TestTrue(TEXT("Creature head interest follows scaled height"),AC2Look::TargetPoint(Feet,200*Scale,true).Equals(Feet+FVector(0,0,150*Scale)));
        TestTrue(TEXT("Object head interest uses native midpoint"),AC2Look::TargetPoint(Feet,200*Scale,false).Equals(Feet+FVector(0,0,100*Scale)));
    }
    const FTransform Body=FTransform::Identity;
    TestTrue(TEXT("camera pitch is orbital displacement in metres"),AC2Look::Camera(Body,FVector(0,4,3)).Equals(FVector(3,0,0)));
    const FTransform Scaled(FQuat::Identity,FVector::ZeroVector,FVector(2,1,3));
    TestTrue(TEXT("camera sliders compensate independent body scale"),AC2Look::Camera(Scaled,FVector(-1,1,3)).Equals(FVector(1,0,UE_PI/8),.00001));
    const double SideFactor=UE_HALF_PI*.63661975f-1.5707964f;
    TestTrue(TEXT("positive camera side transition"),AC2Look::Camera(Body,FVector(-2,0,1)).Equals(FVector(SideFactor,0,UE_HALF_PI*SideFactor),.00001));
    TestTrue(TEXT("negative camera side flips hemisphere"),AC2Look::Camera(Body,FVector(2,0,1)).Equals(FVector(-1,0,UE_HALF_PI),.00001));
    TestTrue(TEXT("zero first-person orbit is neutral"),AC2Look::Camera(Body,FVector::ZeroVector).IsNearlyZero());
    AC2Look::FPose Pose;
    const FVector Aim=AC2Look::At(Body,FVector::ZeroVector,FVector(-3,4,5));
    TestTrue(TEXT("target pitch is height divided by horizontal range"),FMath::IsNearlyEqual(Aim.X,1.,.00001));
    TestTrue(TEXT("quarter-weight target yaw and inverse-weight pitch"),Pose.Aim(TEXT("a"),Aim,.125).Equals(FVector(4,0,Aim.Z*.25),.00001));
    TestTrue(TEXT("half-second acquisition reaches raw target"),Pose.Aim(TEXT("a"),Aim,.375).Equals(Aim,.00001));
    TestTrue(TEXT("release retains target contribution at quarter second"),Pose.Direction(FVector::ZeroVector,.25,true).Equals(Aim*.5,.00001));
    TestTrue(TEXT("held target release ends after half a second"),Pose.Direction(FVector::ZeroVector,.25,true).IsNearlyZero());
    TestTrue(TEXT("local axes have independent durations"),Pose.Direction(FVector(.1,0,1),.1,true).Equals(FVector(.1,0,.5)));
    TestTrue(TEXT("unchanged destination reaches endpoint"),Pose.Direction(FVector(.1,0,1),.1,true).Equals(FVector(.1,0,1)));
    Pose={};
    const FVector Remote(4.5,0,-.6);
    TestTrue(TEXT("remote slider begins half-second ramp"),Pose.Direction(Remote,.2,false).Equals(Remote*.4,.00001));
    TestTrue(TEXT("replicated endpoint retained across frames"),Pose.Direction(Remote,.3,false).Equals(Remote,.00001));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    void* Commands=nullptr;
    ON_SCOPE_EXIT{FPlatformProcess::ClosePipe(Commands,nullptr);Mode->Network.Reset();PC->UnPossess();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    if(!TestTrue(TEXT("capture head reports"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))return false;
    Net.Phase=TEXT("world");Net.PlayerId=TEXT("42");Net.TargetId=TEXT("9313444029402382339");Net.Combat=true;
    auto O=MakeUnique<FAC2Network::FObject>();O->Instance=7;O->BodyHeight=200;
    auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();O->Actor=Actor;
    Net.Objects.Add(Net.TargetId,MoveTemp(O));
    const FVector Eye=Player->GetActorLocation()+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Ahead=Player->Body->GetComponentTransform().GetUnitAxis(EAxis::Y);
    Player->Camera->SetWorldRotation(Ahead.Rotation());
    Actor->SetActorLocation(Eye+Ahead*500-FVector(0,0,150));Net.PlayerLook(.1f);
    TestEqual(TEXT("selection begins independent head tracking"),Net.PlayerLookTarget,Net.TargetId);
    auto Sent=FPlatformProcess::ReadPipe(Commands);
    TestTrue(TEXT("full IID is sent as native look target"),Sent.Contains(TEXT("look_target"))&&Sent.Contains(Net.TargetId));
    Actor->SetActorLocation(Eye+Ahead*1500-FVector(0,0,150));Net.PlayerLook(2.f);
    TestFalse(TEXT("exact native disinterest boundary keeps looking"),Net.PlayerLookTarget.IsEmpty());
    TestTrue(TEXT("no repeated target message"),FPlatformProcess::ReadPipe(Commands).IsEmpty());
    Net.PlayerLook(.01f);Sent=FPlatformProcess::ReadPipe(Commands);
    TestTrue(TEXT("distant head target cleared without deselection or peace"),
        Net.PlayerLookTarget.IsEmpty()&&!Net.TargetId.IsEmpty()&&Net.Combat&&Sent.Contains(TEXT("look_target"))&&!Sent.Contains(TEXT("combat")));
    Net.PlayerLook(1);TestTrue(TEXT("same selection cannot immediately reacquire discarded head target"),FPlatformProcess::ReadPipe(Commands).IsEmpty());
    Net.Time=2;Net.ReportPlayerLook(FVector(.2,0,.3));Sent=FPlatformProcess::ReadPipe(Commands);
    TestTrue(TEXT("camera pose is sent after head target expires"),Sent.Contains(TEXT("look_direction")));
    Net.Time=3;Net.ReportPlayerLook(FVector(.2,0,.3));TestTrue(TEXT("unchanged pose emits nothing"),FPlatformProcess::ReadPipe(Commands).IsEmpty());
    Net.ClearWorld();TestTrue(TEXT("session clear resets all head state"),Net.PlayerLookTarget.IsEmpty()&&Net.LookSelection.IsEmpty()&&Net.LookReporter.Sent==0);
    return true;
}
#endif
