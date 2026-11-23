#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2EffectRenderTest,"AC2.Render.NativeEffectLighting",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2EffectRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Effect rendering requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    struct FState {AAC2Portrait* Actor=nullptr;TUniquePtr<FAC2AssetLibrary> Model;TArray<FColor> Unlit;float Time=0;};
    auto State=MakeShared<FState>();State->Actor=World->SpawnActor<AAC2Portrait>();
    auto* A=State->Actor;
    A->Image=NewObject<UTextureRenderTarget2D>(A);A->Image->InitCustomFormat(640,640,PF_FloatRGBA,true);
    A->Capture->TextureTarget=A->Image;A->Capture->ShowOnlyActors.Add(A);
    A->Capture->bCaptureEveryFrame=false;A->Capture->bCaptureOnMovement=false;
    const FVector Focus(0,0,100),Camera(440,440,280);
    A->Capture->SetWorldLocationAndRotation(Camera,(Focus-Camera).Rotation());A->Capture->FOVAngle=45;
    auto* Floor=World->SpawnActor<AActor>();auto* Mesh=NewObject<UStaticMeshComponent>(Floor);
    Floor->SetRootComponent(Mesh);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
    Mesh->SetWorldScale3D(FVector(12,12,1));Mesh->RegisterComponent();A->Capture->ShowOnlyActors.Add(Floor);
    FString Error;const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2CharacterCatalog Catalog;
    State->Model=MakeUnique<FAC2AssetLibrary>();
    const bool Open=Catalog.Open(Directory,Error)&&State->Model->Open(Directory,Error);
    const auto* Avatar=Catalog.Avatar(4,0x1000);
    if(!TestTrue(TEXT("Render Tumerok spell recipient"),Open&&Avatar&&State->Model->DrawModel(Avatar->Physics,A,A->Body,Avatar->Resolve(Avatar->Defaults()))))
    {State->Model.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;}
    State->Model->Animate(0,0);
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/NativeEffects");IFileManager::Get().MakeDirectory(*Output,true);
    auto FinishShaders=[](){
#if WITH_EDITOR
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    };
    for(int32 Frame=0;Frame<6;++Frame)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,World,Frame,FinishShaders]() {
            if(Frame==1)TestTrue(TEXT("Native light-only effect"),State->Model->PlayEffect(0x410005fb));
            if(Frame==2)TestTrue(TEXT("Native ongoing vigor-damage effect"),State->Model->PlayEffect(0x41000031));
            if(Frame>=2)for(int32 I=0;I<6;++I){State->Time+=1.f/30;State->Model->Animate(State->Time,0);}
            State->Model->UpdateMaterials(State->Time);
            FinishShaders();
            World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Actor->Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Actor->Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Frame](){
            TArray<FColor> Pixels;
            TestTrue(TEXT("Read effect render"),State->Actor->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
            if(Frame==0)State->Unlit=Pixels;
            if(Frame==1)
            {
                int32 Brighter=0;
                for(int32 I=0;I<Pixels.Num();++I)if(State->Unlit.IsValidIndex(I)&&
                    int32(Pixels[I].R)+Pixels[I].G+Pixels[I].B>int32(State->Unlit[I].R)+State->Unlit[I].G+State->Unlit[I].B+30)++Brighter;
                TestTrue(TEXT("Authored FX light illuminates surrounding geometry"),Brighter>10000);
            }
            for(auto& P:Pixels)P.A=255;TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,640,Pixels,PNG);
            TestTrue(TEXT("Save native effect frame"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("frame-%d.png"),Frame))));return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State,World](){
        State->Model.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
    }));
    return true;
}
#endif
