#include "AC2Assets.h"
#include "AC2NativeMaterial.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
void FinishBirdShaders()
{
#if WITH_EDITOR
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2BirdWingTest,"AC2.Render.SceneryBirdWings",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2BirdWingTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Bird rendering requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    struct FState {
        TSharedPtr<FAC2AssetLibrary> Assets;
        AActor* Model=nullptr;
        USceneCaptureComponent2D* Capture=nullptr;
        UTextureRenderTarget2D* Image=nullptr;
        FBox Bounds=FBox(ForceInit);
        int32 TopPixels=0;
    };
    auto State=MakeShared<FState>();
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/BirdWings");
    IFileManager::Get().MakeDirectory(*Output,true);
    for(bool Preview:{false,true})
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,State,Preview](){
            State->Assets=MakeShared<FAC2AssetLibrary>();FString Error;
            if(!TestTrue(TEXT("Open scenery bird DATs"),State->Assets->Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error)))return true;
            State->Model=World->SpawnActor<AActor>();
            auto* Root=NewObject<USceneComponent>(State->Model);State->Model->SetRootComponent(Root);Root->RegisterComponent();
            FAC2PreviewLighting Lighting;Lighting.Ambient=FLinearColor(.8f,.8f,.8f);
            if(Preview)State->Assets->SetPreviewLighting(Lighting,FTransform::Identity);
            // Native small flying bird: five-bone setup 04000003, wing material 31000005.
            TestTrue(TEXT("Draw native scenery bird"),State->Assets->DrawModel(0x1f000003,State->Model,Root));
            if(!Preview)UpdateAC2WorldLighting(World,Lighting,[](const FVector&,FAC2PreviewLighting&){return false;});
            TestTrue(TEXT("Bird has its native animation"),State->Assets->AnimationCount()>0);
            State->Bounds=State->Assets->ModelBounds();
            auto* Camera=World->SpawnActor<AActor>();
            State->Capture=NewObject<USceneCaptureComponent2D>(Camera);Camera->SetRootComponent(State->Capture);
            State->Capture->RegisterComponent();State->Capture->bCaptureEveryFrame=false;State->Capture->bCaptureOnMovement=false;
            State->Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            State->Capture->ShowOnlyActorComponents(State->Model);
            State->Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
            State->Capture->ProjectionType=ECameraProjectionMode::Orthographic;
            State->Capture->OrthoWidth=State->Bounds.GetSize().GetMax()*1.3f;
            State->Capture->ShowFlags.SetMotionBlur(false);State->Capture->ShowFlags.SetEyeAdaptation(false);
            State->Capture->ShowFlags.SetTonemapper(false);State->Capture->ShowFlags.SetFog(false);
            State->Image=NewObject<UTextureRenderTarget2D>(Camera);State->Image->ClearColor=FLinearColor::Black;
            State->Image->InitCustomFormat(512,512,PF_B8G8R8A8,false);State->Image->UpdateResourceImmediate(true);
            State->Capture->TextureTarget=State->Image;
            return true;
        }));
        for(int32 Frame=0;Frame<3;++Frame)for(int32 Side=0;Side<2;++Side)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,World,Frame,Side](){
                if(!State->Capture)return true;
                if(Side==0)State->Assets->Animate(Frame*.13f,0);
                const FVector Center=State->Assets->ModelBounds().GetCenter();
                const FVector Eye=Center+FVector(0,0,Side?-200:200);
                State->Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
                FinishBirdShaders();
                World->SendAllEndOfFrameUpdates();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){if(State->Capture)State->Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            // The first capture warms shader/scene resources. Read the settled
            // frame, just as the tent interior render regression does.
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){
                FinishBirdShaders();if(State->Capture)State->Capture->CaptureScene();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Preview,Frame,Side](){
                if(!State->Image)return true;
                TArray<FColor> Pixels;
                TestTrue(TEXT("Read bird view"),State->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
                int32 Visible=0;for(auto& P:Pixels){P.A=255;if(P.R+P.G+P.B>30)++Visible;}
                const FString Name=FString::Printf(TEXT("%s-%d-%s"),Preview?TEXT("preview"):TEXT("world"),Frame,Side?TEXT("below"):TEXT("above"));
                AddInfo(FString::Printf(TEXT("%s: %d visible pixels"),*Name,Visible));
                TestTrue(TEXT("Bird is visible"),Visible>500);
                if(!Side)State->TopPixels=Visible;
                else TestTrue(TEXT("Wings retain their silhouette from underneath throughout the flap"),Visible>State->TopPixels*.65);
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(512,512,Pixels,PNG);
                TestTrue(TEXT("Save bird view"),FFileHelper::SaveArrayToFile(PNG,*(Output/(Name+TEXT(".png")))));
                return true;
            }));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){
            State->Assets.Reset();if(State->Model)State->Model->Destroy();
            if(State->Capture)State->Capture->GetOwner()->Destroy();State->Capture=nullptr;State->Image=nullptr;return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
