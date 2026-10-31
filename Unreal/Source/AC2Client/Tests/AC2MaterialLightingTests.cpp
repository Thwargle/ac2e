#include "AC2Assets.h"
#include "AC2NativeMaterial.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
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
void FinishMaterialShaders() {
#if WITH_EDITOR
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MaterialFinishDataTest,"AC2.Data.NativeMaterialFinish",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2MaterialFinishDataTest::RunTest(const FString&)
{
    const TArray<FColor> Samples={FColor::Black,FColor::White};
    const FLinearColor Midpoint=AC2SampleNativeRGB(Samples,2,1,.5,.5);
    TestTrue(TEXT("Native filtering averages encoded black and white to 50 percent RGB"),Midpoint.Equals(FLinearColor(.5f,.5f,.5f,1)));
    TestEqual(TEXT("Half tint stores quarter RGB without sRGB brightening"),(Midpoint*.5f).ToFColor(false).R,uint8(64));
    TestTrue(TEXT("Negative terrain coordinates wrap in the same color space"),AC2SampleNativeRGB(Samples,2,1,-.5,.5).Equals(Midpoint));
    // Values independently read from portal.dat: modifier 30000045 and 30000087.
    for(bool Preview:{false,true}) {
        FAC2AssetLibrary Assets;FString Error;
        if(!TestTrue(TEXT("Open material DATs"),Assets.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error)))return false;
        if(Preview)Assets.SetPreviewLighting({},FTransform::Identity);
        auto* Matte=Cast<UMaterialInstanceDynamic>(Assets.Material(0x2b000000));
        auto* Colored=Cast<UMaterialInstanceDynamic>(Assets.Material(0x31000042));
        auto* Animated=Cast<UMaterialInstanceDynamic>(Assets.Material(0x3100005d));
        if(!TestNotNull(TEXT("Matte template"),Matte) || !TestNotNull(TEXT("Colored finish"),Colored) ||
           !TestNotNull(TEXT("Animated finish"),Animated))return false;
        TestEqual(TEXT("Template power zero disables highlights despite the enable flag"),Matte->K2_GetScalarParameterValue(TEXT("AC2NativeSpecularPower")),0.f);
        TestEqual(TEXT("No additional UE PBR shine"),Colored->K2_GetScalarParameterValue(TEXT("AC2Specular")),0.f);
        TestEqual(TEXT("Explicit highlight power"),Colored->K2_GetScalarParameterValue(TEXT("AC2NativeSpecularPower")),20.f);
        TestTrue(TEXT("Highlight color is retained in native RGB"),Colored->K2_GetVectorParameterValue(TEXT("AC2NativeSpecularColor")).Equals(
            FLinearColor(.372549f,.309804f,.290196f,1),.00001f));
        const float First=Animated->K2_GetScalarParameterValue(TEXT("AC2NativeSpecularPower"));
        Assets.UpdateMaterials(1.f);
        const float Last=Animated->K2_GetScalarParameterValue(TEXT("AC2NativeSpecularPower"));
        TestTrue(TEXT("Animated power waveform is evaluated at creation"),FMath::IsNearlyEqual(First,7.351141f,.0001f));
        TestTrue(TEXT("Animated power continues without UV modifiers"),FMath::IsNearlyEqual(Last,8.993384f,.0001f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MaterialFinishRenderTest,"AC2.Render.NativeMaterialFinish",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2MaterialFinishRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Material finish rendering requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    auto* Model=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UProceduralMeshComponent>(Model);Model->SetRootComponent(Mesh);Mesh->RegisterComponent();
    Mesh->CreateMeshSection(0,{{0,-32,-32},{0,32,-32},{0,32,32},{0,-32,32}},
        {0,1,2,0,2,3},{{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},{{0,0},{1,0},{1,1},{0,1}},
        {FColor::White,FColor::White,FColor::White,FColor::White},{},false);
    auto* Camera=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(Camera);Camera->SetRootComponent(Capture);Capture->RegisterComponent();
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->SetWorldLocationAndRotation(FVector(-200,0,0),FRotator::ZeroRotator);
    Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=64;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Capture->ShowOnlyActorComponents(Model);
    Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
    Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAntiAliasing(false);
    auto* Image=NewObject<UTextureRenderTarget2D>(Camera);Image->ClearColor=FLinearColor::Black;
    Image->InitCustomFormat(64,64,PF_FloatRGBA,true);Image->TargetGamma=1;
    Image->UpdateResourceImmediate(true);Capture->TextureTarget=Image;
    auto* Texture=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);
    Texture->SRGB=true;Texture->NeverStream=true;
    auto& Data=Texture->GetPlatformData()->Mips[0].BulkData;
    *static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE))=FColor(128,128,128,255);Data.Unlock();Texture->UpdateResource();
    // Keep the synthetic texture alive through the latent frames.
    Texture->AddToRoot();
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/MaterialFinish");
    IFileManager::Get().MakeDirectory(*Output,true);
    for(bool Preview:{false,true})for(int32 Case=0;Case<7;++Case) {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mesh,Texture,Preview,Case,World](){
            const TCHAR* Path=Preview?TEXT("/Game/AC2/Materials/M_AC2UIOpaque.M_AC2UIOpaque"):TEXT("/Game/AC2/Materials/M_AC2Opaque.M_AC2Opaque");
            auto* Parent=LoadObject<UMaterialInterface>(nullptr,Path);
            if(!TestNotNull(TEXT("Load shader template"),Parent))return true;
            auto* M=UMaterialInstanceDynamic::Create(Parent,Mesh);Mesh->SetMaterial(0,M);
            M->SetTextureParameterValue(TEXT("Albedo"),Texture);M->SetTextureParameterValue(TEXT("AC2Stage0Texture"),Texture);
            M->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
            M->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
            M->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(4,2,0,0)); // texture * diffuse
            M->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,2,0,0));
            M->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.8,.4,.2));
            M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(1,1,0,0));
            M->SetVectorParameterValue(TEXT("AC2UIAmbient"),FLinearColor(.1,.1,.1));
            M->SetVectorParameterValue(TEXT("AC2UILight0"),FLinearColor(-1,0,0,0));
            M->SetVectorParameterValue(TEXT("AC2UIColor0"),FLinearColor(.4,.4,.4));
            M->SetVectorParameterValue(TEXT("AC2NativeSpecularColor"),FLinearColor(.2,.4,.6));
            M->SetScalarParameterValue(TEXT("AC2NativeSpecularEnabled"),1);
            M->SetScalarParameterValue(TEXT("AC2NativeSpecularPower"),Case==0?0:Case==3?40:10);
            if(Case==2 || Case==3)M->SetVectorParameterValue(TEXT("AC2UILight0"),FLinearColor(-.6,.8,0,0));
            if(Case==4)M->SetVectorParameterValue(TEXT("AC2UILight0"),FLinearColor(1,0,0,0));
            if(Case>=5)M->SetVectorParameterValue(TEXT("AC2UILight0"),FLinearColor(-200,0,0,2000));
            if(Case==6)M->SetVectorParameterValue(TEXT("AC2UIIgnore"),FLinearColor(0,1,0,0));
            FinishMaterialShaders();
            World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){FinishMaterialShaders();Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.25f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Preview,Case,Output](){
            TArray<FLinearColor> Linear;
            FReadSurfaceDataFlags ReadFlags(RCM_MinMax);ReadFlags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read material pixels"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Linear,ReadFlags)))return true;
            // Analytic D3D lit / texture cascade oracle. Highlight is added after
            // texturing (not tinted by albedo), with no ACES or PBR response.
            float NdotL=Case==4 || Case==6?0:Case>=2?.6f:1.f;
            float Highlight=Case==0 || Case==4 || Case==6?0:Case==1?1:FMath::Pow(.8f,Case==3?20.f:5.f);
            if(Case==5) {
                // Four plane corners have equal distance; point COLOR1 uses
                // white light and squared attenuation, independently of diffuse.
                const float D2=200.f*200+2*32.f*32,Attenuation=1.f/(1+10*D2/(2000.f*2000));
                NdotL=200.f/FMath::Sqrt(D2)*Attenuation;
                Highlight=Attenuation*Attenuation/.4f;
            }
            const FLinearColor Expected=FLinearColor(.8,.4,.2)*(.4f*NdotL*128.f/255.f)+
                FLinearColor(.1,.1,.1)*(128.f/255.f)+FLinearColor(.2,.4,.6)*(.4f*Highlight);
            const FColor P=Linear[32*64+32].ToFColorSRGB();
            TestTrue(FString::Printf(TEXT("%s case %d matches retail RGB; actual %s expected %s"),Preview?TEXT("preview"):TEXT("world"),Case,*P.ToString(),*Expected.ToString()),
                FMath::Abs(P.R-Expected.R*255)<4 && FMath::Abs(P.G-Expected.G*255)<4 && FMath::Abs(P.B-Expected.B*255)<4);
            TArray<FColor> Pixels;
            for(const auto& V:Linear){FColor Png=V.ToFColorSRGB();Png.A=255;Pixels.Add(Png);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(64,64,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("%s-%d.png"),Preview?TEXT("preview"):TEXT("world"),Case)));
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Texture](){Texture->RemoveFromRoot();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
