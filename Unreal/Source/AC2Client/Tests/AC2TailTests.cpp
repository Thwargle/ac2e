#include "AC2Assets.h"
#include "AC2CharacterData.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TailSeamTest,"AC2.Data.TumerokTailSeam",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TailSeamTest::RunTest(const FString&)
{
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2CharacterCatalog Catalog;FString Error;
    if(!TestTrue(TEXT("Native avatar catalog"),Catalog.Open(Directory,Error)))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4)
    for(bool Dressed:{false,true})
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Model;
        if(TestTrue(TEXT("Tumerok loads"),Model.Open(Directory,Error) &&
            Model.DrawModel(Avatar.Physics,Actor,Actor->Body,Avatar.Resolve(Avatar.Defaults(),Dressed?0x700:0))))
        {
            float Time=0;double Worst=0;int32 Minimum=MAX_int32;
            for(bool Tall:{false,true})for(bool Wide:{false,true})
            {
                const float Width=Wide?Avatar.Builds.Last():Avatar.Builds[0];
                Actor->Body->SetRelativeScale3D(FVector(Width,Width,Tall?Avatar.Heights.Last():Avatar.Heights[0]));
                for(int32 Scenario=0;Scenario<6;++Scenario)
                {
                    const uint32 Mode=Scenario==2||Scenario==3?AC2Motion::Melee:AC2Motion::Peace;
                    Model.StopBehavior();Model.SetAnimationState(AC2Motion::Ground,Mode);
                    if(Scenario>=3)TestTrue(TEXT("Tail follows native action"),Model.PlayBehavior(
                        Scenario==3?AC2Motion::Attack:Scenario==4?AC2Motion::Pickup:AC2Motion::Putdown,Mode));
                    for(int32 Frame=0;Frame<60;++Frame)
                    {
                        Time+=1.f/30;Model.Animate(Time,Scenario==1?250:0);
                        int32 Matches=0;double Gap=0;Model.AttachmentSeamGap(0x81000024,Matches,Gap);
                        Minimum=FMath::Min(Minimum,Matches);Worst=FMath::Max(Worst,Gap);
                    }
                }
            }
            AddInfo(FString::Printf(TEXT("Tumerok sex %u dressed %d: %d seam points, worst gap %.5f cm"),Avatar.Sex,Dressed,Minimum,Worst));
            TestTrue(TEXT("Authored tail/body seam found"),Minimum>=6);
            TestTrue(TEXT("Rendered tail seam stays closed at both scale extremes"),Worst<.05);
        }
        Actor->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TailRenderTest,"AC2.Render.TumerokTailSeam",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TailRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Tail render test requires a graphics device"));return false;}
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2CharacterCatalog Catalog;FString Error;
    if(!TestTrue(TEXT("Native avatar catalog"),Catalog.Open(Directory,Error)))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/TumerokTail");
    IFileManager::Get().MakeDirectory(*Output,true);
    struct FState{TWeakObjectPtr<AAC2Portrait> Portrait;TUniquePtr<FAC2AssetLibrary> Model;};auto State=MakeShared<FState>();
    auto FinishShaders=[](){
#if WITH_EDITOR
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    };
    for(const auto& Avatar:Catalog.Avatars)if(Avatar.Race==4)for(bool Tall:{false,true})
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,State,Avatar,Tall,Directory,FinishShaders](){
            auto* Portrait=World->SpawnActor<AAC2Portrait>();State->Portrait=Portrait;
            const float Height=Tall?Avatar.Heights.Last():Avatar.Heights[0],Width=Tall?Avatar.Builds[0]:Avatar.Builds.Last();
            Portrait->Image=NewObject<UTextureRenderTarget2D>(Portrait);
            Portrait->Image->ClearColor=FLinearColor(.08f,.1f,.12f,1);
            Portrait->Image->InitCustomFormat(640,674,PF_FloatRGBA,true);
            Portrait->Capture->TextureTarget=Portrait->Image;Portrait->Capture->ShowOnlyActors.Add(Portrait);
            State->Model=MakeUnique<FAC2AssetLibrary>();
            FAC2PreviewLighting Lighting;Lighting.Ambient=FLinearColor(.4f,.4f,.4f);
            FAC2PreviewLight Key;Key.Directional=true;Key.Direction=FVector(0,1,-.3).GetSafeNormal();Key.Intensity=.6f;
            Lighting.Lights.Add(Key);State->Model->SetPreviewLighting(Lighting,FTransform::Identity);
            FString LoadError;
            TestTrue(TEXT("Render scaled Tumerok"),State->Model->Open(Directory,LoadError) &&
                State->Model->DrawModel(Avatar.Physics,Portrait,Portrait->Body,Avatar.Resolve(Avatar.Defaults())));
            Portrait->Body->SetRelativeScale3D(FVector(Width,Width,Height));
            Portrait->Capture->bCaptureEveryFrame=false;Portrait->Capture->bCaptureOnMovement=false;
            State->Model->Animate(.15f,0);
            const FTransform Frame=Portrait->Body->GetComponentTransform();
            const FVector Focus=Frame.TransformPosition(FVector(0,-15,90));
            const FVector Camera=Frame.TransformPosition(FVector(0,-250,105));
            Portrait->Capture->SetWorldLocationAndRotation(Camera,(Focus-Camera).Rotation());
            Portrait->Capture->FOVAngle=35;
            FinishShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Portrait->Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,FinishShaders](){FinishShaders();State->Portrait->Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Avatar,Tall](){
            auto* Portrait=State->Portrait.Get();TArray<FColor> Pixels;
            TestTrue(TEXT("Read tail rendering"),Portrait->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
            int32 Visible=0;for(auto& Pixel:Pixels){Pixel.A=255;Visible+=Pixel.R+Pixel.G+Pixel.B>30;}
            TestTrue(TEXT("Tumerok is visible"),Visible>1000);
            TArray<uint8> PNG;FImageUtils::CompressImageArray(Portrait->Image->SizeX,Portrait->Image->SizeY,Pixels,PNG);
            TestTrue(TEXT("Save tail seam view"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("%u-%s.png"),Avatar.Sex,Tall?TEXT("tall-thin"):TEXT("short-wide")))));
            State->Model.Reset();Portrait->Destroy();return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
