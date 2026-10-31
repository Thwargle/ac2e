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
#include "ProceduralMeshComponent.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
void FinishTentShaders() {
#if WITH_EDITOR
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MaterialCullingTest,"AC2.Data.NativeMaterialCulling",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2MaterialCullingTest::RunTest(const FString&)
{
    for(bool Preview:{false,true})
    {
        FAC2AssetLibrary Assets;FString Error;
        if(!TestTrue(TEXT("Open native materials"),Assets.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error)))return false;
        if(Preview)Assets.SetPreviewLighting(FAC2PreviewLighting(),FTransform::Identity);
        // Both tent sizes override boolean property 4003 / field kind 5.
        for(uint32 Id:{0x31000845u,0x31000846u,0x31000847u,0x31000848u})
        {
            const auto* Mat=Assets.Material(Id);
            TestTrue(FString::Printf(TEXT("%s tent %08X honors native no-cull modifier"),Preview?TEXT("Preview"):TEXT("World"),Id),Mat && Mat->IsTwoSided());
        }
        const auto* Ordinary=Assets.Material(0x2b000002);
        TestTrue(TEXT("Unmodified template retains one-sided culling"),Ordinary && !Ordinary->IsTwoSided());
        // 2B000005 serializes cull=2, but property 4003 defaults to true.
        // Retail applies that property after the serialized layer is loaded.
        const auto* DefaultTwoSided=Assets.Material(0x2b000005);
        TestTrue(TEXT("Default property overrides serialized template culling"),DefaultTwoSided && DefaultTwoSided->IsTwoSided());
        for(uint32 Id:{0x31000006u,0x31000007u,0x31000181u})
        {
            const auto* Mat=Assets.Material(Id);
            TestTrue(FString::Printf(TEXT("Foliage %08X honors template default no-cull property"),Id),Mat && Mat->IsTwoSided());
        }
        const auto* Wings=Assets.Material(0x31000005);
        TestTrue(TEXT("Single-sheet scenery bird wings render both faces"),Wings && Wings->IsTwoSided());
        const auto* Body=Assets.Material(0x31000004);
        TestTrue(TEXT("Scenery bird body keeps native one-sided culling"),Body && !Body->IsTwoSided());
    }
    return true;
}
// The actual scenery at 36.4N, 13.3W: entity 47003669, visual 1F00192D.
// Inside views catch one-sided fabric even when the exterior looks correct.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TentCullingTest,"AC2.Render.NativeTentInterior",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TentCullingTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Tent rendering requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    struct FState {TSharedPtr<FAC2AssetLibrary> Assets; AActor* Model=nullptr; USceneCaptureComponent2D* Capture=nullptr; UTextureRenderTarget2D* Image=nullptr; FBox Bounds=FBox(ForceInit);};
    auto State=MakeShared<FState>();
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/TentInterior");
    IFileManager::Get().MakeDirectory(*Output,true);
    for(bool Preview:{false,true})
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,State,Preview](){
            State->Assets=MakeShared<FAC2AssetLibrary>();FString Error;
            State->Capture=nullptr;State->Image=nullptr;State->Model=nullptr;
            if(!TestTrue(TEXT("Open tent DAT resources"),State->Assets->Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error))){AddError(Error);return true;}
            State->Model=World->SpawnActor<AActor>();
            auto* Root=NewObject<USceneComponent>(State->Model);State->Model->SetRootComponent(Root);Root->RegisterComponent();
            FAC2PreviewLighting Lighting;Lighting.Ambient=FLinearColor(.7,.7,.7);
            if(Preview)State->Assets->SetPreviewLighting(Lighting,FTransform::Identity);
            TestTrue(TEXT("Load exact native tent"),State->Assets->DrawModel(0x1f00192d,State->Model,Root));
            if(!Preview)UpdateAC2WorldLighting(World,Lighting,[](const FVector&,FAC2PreviewLighting&){return false;});
            State->Bounds=State->Assets->ModelBounds();
            AddInfo(FString::Printf(TEXT("Tent bounds: %s"),*State->Bounds.ToString()));
            auto* Camera=World->SpawnActor<AActor>();
            State->Capture=NewObject<USceneCaptureComponent2D>(Camera);Camera->SetRootComponent(State->Capture);
            State->Capture->RegisterComponent();State->Capture->bCaptureEveryFrame=false;State->Capture->bCaptureOnMovement=false;
            State->Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            State->Capture->ShowOnlyActorComponents(State->Model);
            State->Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
            State->Capture->FOVAngle=80;
            State->Capture->ShowFlags.SetMotionBlur(false);State->Capture->ShowFlags.SetEyeAdaptation(false);
            State->Capture->ShowFlags.SetTonemapper(false);State->Capture->ShowFlags.SetFog(false);
            State->Image=NewObject<UTextureRenderTarget2D>(Camera);State->Image->ClearColor=FLinearColor::Black;
            State->Image->InitCustomFormat(640,480,PF_B8G8R8A8,false);State->Image->UpdateResourceImmediate(true);
            State->Capture->TextureTarget=State->Image;
            return true;
        }));
        for(int32 View=0;View<3;++View)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,World,View](){
                if(!State->Capture)return true;
                const FVector Center=State->Bounds.GetCenter(),Extent=State->Bounds.GetExtent();
                FVector Eye=Center,Target=Center+FVector(0,Extent.Y,0);
                if(View==0){Eye=Center+FVector(0,-Extent.Y*3,0);Target=Center;}
                if(View==2)Target=Center+FVector(0,0,Extent.Z);
                State->Capture->SetWorldLocationAndRotation(Eye,(Target-Eye).Rotation());
                FinishTentShaders();
                World->SendAllEndOfFrameUpdates();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){if(State->Capture)State->Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){
                FinishTentShaders();
                if(State->Capture)State->Capture->CaptureScene();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Preview,View](){
                if(!State->Image)return true;
                TArray<FColor> Pixels;
                TestTrue(TEXT("Read tent render"),State->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
                int32 Visible=0;for(auto& P:Pixels){P.A=255;if(P.R+P.G+P.B>30)++Visible;}
                const FString Name=FString::Printf(TEXT("%s-%s"),Preview?TEXT("preview"):TEXT("world"),View==0?TEXT("outside"):View==1?TEXT("inside-wall"):TEXT("inside-roof"));
                TestTrue(FString::Printf(TEXT("%s contains textured fabric (%d pixels)"),*Name,Visible),Visible>(View==0?10000:120000));
                TArray<uint8> PNG;FImageUtils::CompressImageArray(640,480,Pixels,PNG);
                TestTrue(TEXT("Save tent view"),FFileHelper::SaveArrayToFile(PNG,*(Output/(Name+TEXT(".png")))));
                return true;
            }));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Assets.Reset();if(State->Model)State->Model->Destroy();if(State->Capture)State->Capture->GetOwner()->Destroy();return true;}));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
