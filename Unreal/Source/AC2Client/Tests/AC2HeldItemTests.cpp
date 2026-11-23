#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2HeldItem.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2HeldRunningTest,"AC2.Data.HeldWeaponsFollowRunning",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2HeldRunningTest::RunTest(const FString&) {
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    auto* FloorActor=World->SpawnActor<AActor>();auto* Floor=NewObject<UBoxComponent>(FloorActor);
    FloorActor->SetRootComponent(Floor);Floor->SetBoxExtent(FVector(20000,20000,10));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Floor->SetCollisionResponseToAllChannels(ECR_Block);Floor->RegisterComponent();
    FloorActor->SetActorLocation(FVector(0,0,-10));
    World->BeginPlay();Mode->LoginStarted=true;Mode->CharacterUIStarted=true;
    Mode->Network=MakeUnique<FAC2Network>(Mode);Mode->Network->PlayerId=TEXT("runner");
    auto Item=MakeUnique<FAC2Network::FObject>();auto* Weapon=World->SpawnActor<AAC2Portrait>();
    Item->Actor=Weapon;Item->Parent=TEXT("runner");Item->Holding=4;
    Mode->Network->Objects.Add(TEXT("javelin"),MoveTemp(Item));
    FAC2CharacterCatalog Catalog;FString Error;
    if(TestTrue(TEXT("Native avatar catalog"),Catalog.Open(Mode->DatDirectory,Error)))
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4)
    for(bool Tall:{false,true}) {
        TestTrue(TEXT("Running avatar"),Player->ApplyAvatar(Avatar.Physics,Tall?Avatar.Heights.Last():Avatar.Heights[0],
            Tall?Avatar.Builds.Last():Avatar.Builds[0],Avatar.Resolve(Avatar.Defaults())));
        Player->SetActorLocation(FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));
        Player->SetHeld(false);Player->SetRunning(true);Player->SetNativeMode(0x41000010);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        double MaxGripError=0;
        for(int32 Frame=0;Frame<120;++Frame) {
            Player->AddMovementInput(FVector::ForwardVector);
            // Exercise engine scheduling: manually calling the attachment after
            // the pawn would hide the original one-frame lag.
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);
            FTransform Socket;TestTrue(TEXT("Running socket"),Player->HoldingTransform(4,Socket));
            MaxGripError=FMath::Max(MaxGripError,FVector::Distance(Weapon->GetActorLocation(),Socket.GetLocation()));
        }
        TestTrue(FString::Printf(TEXT("Player really ran: %s, begun %d, held %d, ticks %d"),*Player->GetActorLocation().ToString(),int(Player->HasActorBegunPlay()),int(Player->IsHeld()),int(Player->PrimaryActorTick.IsTickFunctionRegistered())),Player->GetActorLocation().X>500);
        TestTrue(FString::Printf(TEXT("Same-frame hand attachment sex %X tall %d, error %.3f cm"),Avatar.Sex,int(Tall),MaxGripError),MaxGripError<.05);
        TestTrue(TEXT("Weapon size is independent of avatar height"),Weapon->GetActorScale3D().Equals(FVector::OneVector,.001));
    }
    Mode->Network.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2HeldItemTest,"AC2.Data.TumerokHeldWeapons",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2HeldItemTest::RunTest(const FString&) {
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2CharacterCatalog Catalog;FString Error;
    if(!TestTrue(TEXT("Native avatar catalog"),Catalog.Open(Directory,Error)))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4) {
        auto* Actor=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Model;
        TestTrue(TEXT("Native Tumerok body"),Model.Open(Directory,Error)&&Model.DrawModel(Avatar.Physics,Actor,Actor->Body,Avatar.Resolve(Avatar.Defaults())));
        float Time=0;Model.Animate(Time,0);
        for(bool Tall:{false,true}) {
            const double Width=Tall?Avatar.Builds[0]:Avatar.Builds.Last();
            Actor->Body->SetRelativeScale3D(FVector(Width,Width,Tall?Avatar.Heights.Last():Avatar.Heights[0]));
            for(uint32 Visual:{0x1f0002aeu,0x1f000059u,0x1f000427u}) {
                auto* Weapon=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Held;
                TestTrue(TEXT("Recovered javelin/drum renders"),Held.Open(Directory,Error)&&Held.DrawModel(Visual,Weapon,Weapon->Body));
                const bool Javelin=Visual==0x1f0002ae;
                const uint32 Mode=Javelin?0x41000010:0x41000011, Behavior=Javelin?0x410000e2:0x410000b0;
                Model.StopBehavior();Model.SetAnimationState(AC2Motion::Ground,Mode);
                TestTrue(TEXT("Native weapon action resolves"),Model.PlayBehavior(Behavior,Mode));
                if(Javelin)TestTrue(TEXT("Native throw duration"),FMath::IsNearlyEqual(Model.BehaviorDuration(Behavior),1.41667f,.001f));
                FTransform First;bool Moved=false,Released=false;
                for(int32 Frame=0;Frame<45;++Frame) {
                    Time+=1.f/30;Model.Animate(Time,0);FTransform Socket;
                    if(Javelin) {
                        const bool Hidden=Model.ReleasedHeldItem(4);Released|=Hidden;
                        if(Frame<19)TestFalse(TEXT("Held through windup"),Hidden);
                        if(Frame==23)TestTrue(TEXT("Held javelin hidden after release"),Hidden);
                    }
                    TestFalse(TEXT("Other holding points stay visible"),Model.ReleasedHeldItem(3));
                    TestTrue(TEXT("Native animated holding point exists"),Model.AnimatedHoldingLocation(Javelin?4:3,Socket));
                    Socket=Socket*Actor->Body->GetComponentTransform();
                    const FTransform HeldFrame=AC2HeldItemTransform(Socket,FVector(1.2));
                    TestTrue(TEXT("Weapon keeps independent size"),HeldFrame.GetScale3D().Equals(FVector(1.2),.0001));
                    TestTrue(TEXT("Grip follows scaled character"),HeldFrame.GetLocation().Equals(Socket.GetLocation(),.0001));
                    Weapon->SetActorTransform(HeldFrame);
                    if(Frame==0)First=Socket;else Moved|=!First.GetLocation().Equals(Socket.GetLocation(),1);
                }
                TestTrue(TEXT("Throw/drum action moves its holding point"),Moved);
                if(Javelin)TestTrue(TEXT("Native release phase reached"),Released);
                TestFalse(TEXT("Held visual returns after recovery"),Model.ReleasedHeldItem(4));
                if(Javelin) {
                    Model.PlayBehavior(Behavior,Mode);
                    for(int32 I=0;I<24;++I){Time+=1.f/30;Model.Animate(Time,0);}
                    TestTrue(TEXT("Second throw releases"),Model.ReleasedHeldItem(4));
                    Model.StopBehavior();TestFalse(TEXT("Cancelled throw restores the held item"),Model.ReleasedHeldItem(4));
                }
                Weapon->Destroy();
            }
        }
        Actor->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2HeldRenderTest,"AC2.Render.TumerokJavelin",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2HeldRenderTest::RunTest(const FString&) {
    if(GUsingNullRHI){AddError(TEXT("Weapon render test requires a graphics device"));return false;}
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2CharacterCatalog Catalog;FString Error;
    if(!Catalog.Open(Directory,Error)){AddError(Error);return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/TumerokJavelin");IFileManager::Get().MakeDirectory(*Output,true);
    struct FState {TWeakObjectPtr<AAC2Portrait> Actor,Weapon;TUniquePtr<FAC2AssetLibrary> Model,Held;float Time=0;};
    auto State=MakeShared<FState>();
    auto FinishShaders=[](){
#if WITH_EDITOR
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    };
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4) {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,State,Avatar,Directory,FinishShaders](){
            auto* A=World->SpawnActor<AAC2Portrait>();auto* W=World->SpawnActor<AAC2Portrait>();State->Actor=A;State->Weapon=W;
            A->Image=NewObject<UTextureRenderTarget2D>(A);A->Image->ClearColor=FLinearColor(.08,.1,.12,1);A->Image->InitCustomFormat(640,674,PF_FloatRGBA,true);
            A->Capture->TextureTarget=A->Image;A->Capture->ShowOnlyActors.Add(A);A->Capture->ShowOnlyActors.Add(W);
            A->Capture->bCaptureEveryFrame=false;A->Capture->bCaptureOnMovement=false;
            FAC2PreviewLighting Light;Light.Ambient=FLinearColor(.6,.6,.6);
            State->Model=MakeUnique<FAC2AssetLibrary>();State->Held=MakeUnique<FAC2AssetLibrary>();
            State->Model->SetPreviewLighting(Light,FTransform::Identity);State->Held->SetPreviewLighting(Light,FTransform::Identity);
            FString E;TestTrue(TEXT("Render avatar"),State->Model->Open(Directory,E)&&State->Model->DrawModel(Avatar.Physics,A,A->Body,Avatar.Resolve(Avatar.Defaults())));
            TestTrue(TEXT("Render javelin"),State->Held->Open(Directory,E)&&State->Held->DrawModel(0x1f0002ae,W,W->Body));
            A->Body->SetRelativeScale3D(FVector(Avatar.Builds[0],Avatar.Builds[0],Avatar.Heights.Last()));
            const FVector Focus(0,0,125),Camera(350,420,180);A->Capture->SetWorldLocationAndRotation(Camera,(Focus-Camera).Rotation());A->Capture->FOVAngle=40;
            State->Time=0;State->Model->SetAnimationState(AC2Motion::Ground,0x41000010);State->Model->Animate(0,0);
            FinishShaders();
            return true;
        }));
        for(int32 Pose=0;Pose<6;++Pose) {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,Pose,World](){
                if(Pose==1)State->Model->PlayBehavior(0x410000e2,0x41000010);
                if(Pose>=4){State->Model->StopBehavior();State->Model->SetAnimationState(AC2Motion::Ground,Pose==4?0x41000010:AC2Motion::Peace);}
                FVector Velocity(0,550,0);
                for(int32 I=0;I<(Pose==0?15:Pose==1?10:Pose==2?11:22);++I){State->Time+=1.f/30;State->Model->Animate(State->Time,Pose>=4?550:0,Pose>=4?&Velocity:nullptr);}
                FTransform Socket;State->Model->AnimatedHoldingLocation(4,Socket);
                State->Weapon->SetActorTransform(AC2HeldItemTransform(Socket*State->Actor->Body->GetComponentTransform(),FVector::OneVector));
                State->Weapon->SetActorHiddenInGame(State->Model->ReleasedHeldItem(4));
                World->SendAllEndOfFrameUpdates();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Actor->Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Actor->Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Avatar,Pose](){
                TArray<FColor> Pixels;TestTrue(TEXT("Read weapon pose"),State->Actor->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
                for(auto& P:Pixels)P.A=255;TArray<uint8> PNG;FImageUtils::CompressImageArray(640,674,Pixels,PNG);
                TestTrue(TEXT("Save weapon pose"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("%u-%d.png"),Avatar.Sex,Pose))));return true;
            }));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Model.Reset();State->Held.Reset();State->Actor->Destroy();State->Weapon->Destroy();return true;}));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));return true;
}
#endif
