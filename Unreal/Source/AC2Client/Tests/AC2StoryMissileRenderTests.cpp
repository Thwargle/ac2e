#include "AC2Assets.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2StoryMissileRenderTest,"AC2.Render.StoryMissileFlight",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2StoryMissileRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Projectile rendering requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Light=World->SpawnActor<ADirectionalLight>();Light->SetActorRotation(FRotator(-40,70,0));
    Light->GetLightComponent()->SetIntensity(3.f);
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    struct FState {TUniquePtr<FAC2Network> Net;AAC2Portrait* View=nullptr;double FirstX=0;};
    auto State=MakeShared<FState>();State->Net=MakeUnique<FAC2Network>(Mode);auto& Net=*State->Net;
    auto Add=[&](const TCHAR* Id,const FVector& Location,uint32 Visual){
        auto O=MakeUnique<FAC2Network::FObject>();O->Placed=true;O->Instance=1;O->Cell=0x77b80001;O->Goal=Location;
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("did"),Visual);Net.ApplyVisual(*O,J,false);
        Net.Objects.Add(Id,MoveTemp(O));
    };
    Add(TEXT("shooter"),FVector::ZeroVector,0x1f000025);Add(TEXT("target"),FVector(2000,0,0),0x1f000031);
    auto* Shooter=Net.Objects[TEXT("shooter")]->Model.Get();
    if(!TestNotNull(TEXT("Native Tumerok shooter"),Shooter)){
        State->Net.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;
    }
    TestTrue(TEXT("Native throw starts"),Shooter->PlayBehavior(0x410000e2,0x41000010));
    const auto Hooks=Shooter->StoryHookDelays(0x410000e2);
    TestTrue(TEXT("Native throw exposes a release hook within its animation"),Hooks.Num()>0&&Hooks[0]>.1f&&Hooks[0]<Shooter->BehaviorDuration(0x410000e2));
    auto Story=MakeShared<FJsonObject>();Story->SetStringField(TEXT("event"),TEXT("story_missiles"));
    Story->SetStringField(TEXT("id"),TEXT("shooter"));Story->SetStringField(TEXT("attacker"),TEXT("shooter"));
    Story->SetNumberField(TEXT("instance"),1);Story->SetNumberField(TEXT("skill"),0x316);Story->SetNumberField(TEXT("behavior"),0x410000e2);
    auto Hook=MakeShared<FJsonObject>();Hook->SetNumberField(TEXT("kind"),3);Hook->SetNumberField(TEXT("attack_hook"),0);
    Hook->SetNumberField(TEXT("anim_hook"),1);Hook->SetStringField(TEXT("target"),TEXT("target"));
    Story->SetArrayField(TEXT("hooks"),{MakeShared<FJsonValueObject>(Hook)});Net.Handle(Story);
    TestTrue(TEXT("Native story queues its release"),Net.StoryMissiles.Num()==1);
    if(Hooks.Num()){
        Net.Tick(Hooks[0]-.01f);TestEqual(TEXT("No missile before native release"),Net.NextLocalMissile,uint64(0));
        Net.Tick(.02f);
    }
    State->View=World->SpawnActor<AAC2Portrait>();auto* View=State->View;
    View->Image=NewObject<UTextureRenderTarget2D>(View);View->Image->InitCustomFormat(960,320,PF_FloatRGBA,true);
    View->Capture->TextureTarget=View->Image;View->Capture->bCaptureEveryFrame=false;View->Capture->bCaptureOnMovement=false;
    View->Capture->ProjectionType=ECameraProjectionMode::Orthographic;View->Capture->OrthoWidth=2400;
    const FVector Focus(1000,0,100),Camera(1000,-3000,100);
    View->Capture->SetWorldLocationAndRotation(Camera,(Focus-Camera).Rotation());
    if(auto O=Net.Objects.Find(TEXT("story:1"));TestTrue(TEXT("Release creates a separate world projectile"),O&&(*O)->Actor.IsValid()))
        View->Capture->ShowOnlyActors.Add((*O)->Actor.Get());
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/StoryMissiles");IFileManager::Get().MakeDirectory(*Output,true);
    auto FinishShaders=[](){
#if WITH_EDITOR
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    };
    for(int32 Frame=0;Frame<3;++Frame){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,World,Frame,FinishShaders](){
            State->Net->Tick(Frame?.15f:.1f);FinishShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->View->Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Frame,Output](){
            TArray<FColor> Pixels;TestTrue(TEXT("Read projectile flight render"),State->View->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
            int32 Count=0,Lit=0;double Sum=0;
            // SceneColor HDR alpha is inverse opacity. The show-only capture
            // contains only the in-flight missile, not its shooter or target.
            for(int32 I=0;I<Pixels.Num();++I)if(Pixels[I].A<128){++Count;Sum+=I%960;if(Pixels[I].R+Pixels[I].G+Pixels[I].B>60)++Lit;}
            TestTrue(TEXT("Projectile occupies visible pixels during flight"),Count>20);
            TestTrue(TEXT("Projectile material responds to scene lighting"),Lit>20);
            const double X=Count?Sum/Count:0;if(Frame==0)State->FirstX=X;
            else TestTrue(TEXT("Rendered projectile travels across the fixed camera"),FMath::Abs(X-State->FirstX)>60*Frame);
            AddInfo(FString::Printf(TEXT("Frame %d: %d projectile pixels, centre %.1f"),Frame,Count,X));
            for(auto& P:Pixels)P.A=255;TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(960,320,Pixels,PNG);
            TestTrue(TEXT("Save projectile flight frame"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("frame-%d.png"),Frame))));return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,World](){
        State->Net.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
    }));return true;
}
#endif
