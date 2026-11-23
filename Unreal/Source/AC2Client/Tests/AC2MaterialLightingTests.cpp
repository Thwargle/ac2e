#include "AC2Assets.h"
#include "AC2Appearance.h"
#include "AC2NativeMaterial.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
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
namespace { void FinishMaterialShaders(); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2FoliageContactRenderTest,"AC2.Rendering.FoliageContacts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2FoliageContactRenderTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()){AddError(TEXT("Foliage test requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    auto* Actor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();Mesh->BoundsScale=3;
    TArray<FVector> Vertices,Normals;TArray<FVector2D> UV,Weights;TArray<int32> Indices;
    for(int32 I=0;I<=8;++I){
        Vertices.Add(FVector(-5,0,I*10));Vertices.Add(FVector(5,0,I*10));
        Normals.Add(FVector(0,-1,0));Normals.Add(FVector(0,-1,0));
        UV.Add(FVector2D(0,I/8.));UV.Add(FVector2D(1,I/8.));
        Weights.Add(FVector2D(I/8.,80));Weights.Add(FVector2D(I/8.,80));
        if(I<8)Indices.Append({I*2,I*2+1,I*2+3,I*2,I*2+3,I*2+2});
    }
    Mesh->CreateMeshSection(0,Vertices,Indices,Normals,UV,UV,UV,Weights,{}, {},false);
    auto* Material=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2Grass.M_AC2Grass")),Mesh);
    if(!TestNotNull(TEXT("Grass template"),Material))return false;
    Mesh->SetMaterial(0,Material);RegisterAC2FoliageMaterial(Material,Mesh,true);
    Material->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
    Material->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
    Material->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,0,0,0));
    Material->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(3,0,0,0));
    Material->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
    auto* Camera=World->SpawnActor<AActor>();auto* Capture=NewObject<USceneCaptureComponent2D>(Camera);
    Camera->SetRootComponent(Capture);Capture->RegisterComponent();
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->SetWorldLocationAndRotation(FVector(0,-300,40),FRotator(0,90,0));
    Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=180;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Capture->ShowOnlyActorComponents(Actor);
    Capture->CaptureSource=SCS_SceneColorHDR;Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAntiAliasing(false);
    auto* Image=NewObject<UTextureRenderTarget2D>(Camera);Image->ClearColor=FLinearColor::Black;
    Image->InitCustomFormat(128,128,PF_FloatRGBA,true);Image->UpdateResourceImmediate(true);Capture->TextureTarget=Image;
    auto Baseline=MakeShared<FVector2D>();
    // Prime the capture after shader compilation/resource registration. The
    // first offscreen view can precede the render-thread scene proxy upload.
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture](){
        FinishMaterialShaders();World->SendAllEndOfFrameUpdates();Capture->CaptureScene();return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.5f));
    for(int32 Step=0;Step<6;++Step){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Step](){
            TArray<FAC2FoliageContact> Contacts;
            if(Step==1)Contacts.Add({1,FVector(-35,0,0),35,180});
            if(Step==2)Contacts.Add({2,FVector(35,0,0),35,180}); // remote body, opposite side
            if(Step==3)Contacts.Add({3,FVector(-35,0,600),35,180}); // different floor
            if(Step==5){
                Contacts.Add({4,FVector(-80,0,0),35,180});UpdateAC2FoliageInteraction(World,Contacts,1);
                Contacts[0].Feet.X=80;UpdateAC2FoliageInteraction(World,Contacts,.016f);
            }else UpdateAC2FoliageInteraction(World,Contacts,1);
            FinishMaterialShaders();
            World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Baseline,Step](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read bent foliage pixels"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            auto Center=[&](int32 Start,int32 End){double Total=0;int32 N=0;
                for(int32 Y=Start;Y<End;++Y)for(int32 X=0;X<128;++X)if(Pixels[Y*128+X].R>.5f){Total+=X;++N;}
                return N?Total/N:-1000.;};
            const FVector2D Current(Center(88,92),Center(39,57));
            if(Step==0){*Baseline=Current;TestTrue(TEXT("Baseline roots and tips rendered"),Current.X>0 && Current.Y>0);}
            TestTrue(FString::Printf(TEXT("Step %d leaves roots anchored: %.2f vs %.2f"),Step,Current.X,Baseline->X),FMath::Abs(Current.X-Baseline->X)<1.5);
            if(Step==1 || Step==2 || Step==5)
                TestTrue(FString::Printf(TEXT("Body %d bends rendered tips: %.2f vs %.2f"),Step,Current.Y,Baseline->Y),Current.Y>0 && FMath::Abs(Current.Y-Baseline->Y)>4);
            else TestTrue(TEXT("No contact or another floor leaves tips at rest"),FMath::Abs(Current.Y-Baseline->Y)<1);
            TArray<FColor> Colors;for(auto C:Pixels){auto P=C.ToFColorSRGB();P.A=255;Colors.Add(P);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(128,128,Colors,PNG);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/FoliageContacts");IFileManager::Get().MakeDirectory(*Dir,true);
            FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("step-%d.png"),Step)));return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaterBlendRenderTest,"AC2.Rendering.WaterBlend",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2WaterBlendRenderTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()){AddError(TEXT("Water test requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    auto* Actor=World->SpawnActor<AActor>();auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);
    Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();
    Mesh->CreateMeshSection(0,{{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}},
        {0,2,1,0,3,2},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},{{0,0},{1,0},{1,1},{0,1}},
        {},{},{},{},{},false);
    auto* M=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2Water.M_AC2Water")),Mesh);
    if(!TestNotNull(TEXT("Water template"),M))return false;
    Mesh->SetMaterial(0,M);
    M->SetScalarParameterValue(TEXT("AC2ShoreFoamEnabled"),1);
    M->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
    M->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
    M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
    M->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.2f,.3f,.5f,.8f));
    M->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,0,0,0));
    M->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,0,0,0));
    auto* Reflection=NewObject<UTextureRenderTarget2D>(Actor);
    Reflection->ClearColor=FLinearColor(.25f,.5f,1);
    Reflection->InitCustomFormat(16,16,PF_FloatRGBA,true);Reflection->UpdateResourceImmediate(true);Reflection->AddToRoot();
    M->SetTextureParameterValue(TEXT("AC2WaterReflection"),Reflection);
    // Split the reflection into two colors so a flipped V or missing signed
    // bump cannot pass the same checks as a uniform cubemap or sky color.
    auto MakeProbeTexture=[](int32 Size,auto ColorAt){
        auto* T=UTexture2D::CreateTransient(Size,Size,PF_B8G8R8A8);
        T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;
        auto& Data=T->GetPlatformData()->Mips[0].BulkData;
        auto* P=static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE));
        for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X)*P++=ColorAt(Y);
        Data.Unlock();T->UpdateResource();T->AddToRoot();return T;
    };
    auto* SplitReflection=MakeProbeTexture(64,[](int32 Y){return Y<32?FColor::Red:FColor::Blue;});
    auto* NoDistortion=MakeProbeTexture(1,[](int32){return FColor(128,128,0);});
    auto* SignedDistortion=MakeProbeTexture(1,[](int32){return FColor(128,0,0);});
    TArray<UTexture2D*> Masks;
    for(uint8 Alpha:{uint8(0),uint8(128),uint8(255)}){
        auto* Texture=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);Texture->SRGB=true;Texture->NeverStream=true;
        auto& Data=Texture->GetPlatformData()->Mips[0].BulkData;
        *static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE))=FColor(255,255,255,Alpha);Data.Unlock();Texture->UpdateResource();
        Texture->AddToRoot();Masks.Add(Texture);
    }
    M->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),Masks[0]);
    // A visible two-color riverbed catches transmission bugs that a black
    // clear color cannot: both colors must survive the water/shore passes.
    TArray<UProceduralMeshComponent*> Bed;
    for(int32 Side=0;Side<2;++Side){
        auto* Ground=NewObject<UProceduralMeshComponent>(Actor);Ground->RegisterComponent();
        const double Y0=Side==0?-200:0,Y1=Side==0?0:200;
        Ground->CreateMeshSection(0,{{-200,Y0,-40},{200,Y0,-40},{200,Y1,-40},{-200,Y1,-40}},
            {0,1,2,0,2,3,2,1,0,3,2,0},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
            {{0,0},{1,0},{1,1},{0,1}},{},{},false);
        auto* G=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2Opaque.M_AC2Opaque")),Ground);
        Ground->SetMaterial(0,G);G->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
        G->SetVectorParameterValue(TEXT("Tint"),Side==0?FLinearColor(.7,.1,.1):FLinearColor(.1,.1,.7));
        G->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
        G->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
        G->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,0,0,0));
        G->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,0,0,0));
        Ground->SetVisibility(false);Bed.Add(Ground);
    }
    auto* Camera=World->SpawnActor<AActor>();auto* Capture=NewObject<USceneCaptureComponent2D>(Camera);
    Camera->SetRootComponent(Capture);Capture->RegisterComponent();Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(-90,0,0));
    Capture->CaptureSource=SCS_SceneColorHDR;Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAntiAliasing(false);
    auto* Image=NewObject<UTextureRenderTarget2D>(Camera);Image->ClearColor=FLinearColor::Black;
    Image->InitCustomFormat(64,64,PF_FloatRGBA,true);Image->UpdateResourceImmediate(true);Capture->TextureTarget=Image;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture](){
        FinishMaterialShaders();World->SendAllEndOfFrameUpdates();Capture->CaptureScene();return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.5f));
    auto BedBaseline=MakeShared<TArray<FColor>>();
    for(int32 Step=0;Step<19;++Step){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([M,Mesh,Bed,World,Step,Masks,Capture,SplitReflection,NoDistortion,SignedDistortion](){
            M->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),Step>=3?1:0);
            M->SetScalarParameterValue(TEXT("AC2WaterReflectionHeight"),Step==3?100.f:0.f);
            if(Step==6 || Step==7 || Step==18){
                const double Pass=Step!=7 ? 1. : 0.;
                Mesh->CreateMeshSection(0,{{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}},
                    {0,2,1,0,3,2},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
                    {{0,0},{1,0},{1,1},{0,1}}, {},{{0,Pass},{0,Pass},{0,Pass},{0,Pass}}, {},{},{},false);
            }
            if(Step==6){
                M->SetTextureParameterValue(TEXT("AC2ShoreFoam"),Masks[2]);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),Masks[2]);
                M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(1,1,0,0));
                M->SetVectorParameterValue(TEXT("AC2UIAmbient"),FLinearColor(.1,.2,.3));
            }
            M->SetTextureParameterValue(TEXT("AC2ShoreMask"),Masks[Step==4?1:FMath::Min(Step,2)]);
            if(Step>=7){
                for(auto* Ground:Bed)Ground->SetVisibility(true);
                Mesh->SetVisibility(Step!=7);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),Masks[0]);
                M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
                M->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),Step==11?1:0);
                M->SetTextureParameterValue(TEXT("AC2ShoreMask"),Masks[Step==9?1:Step==10?0:2]);
            }
            if(Step>=12){
                for(auto* Ground:Bed)Ground->SetVisibility(false);
                M->SetScalarParameterValue(TEXT("AC2WaterBelow"),Step==12?0:1);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),Masks[2]);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoam"),Masks[2]);
                M->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),1);
                Capture->SetWorldLocationAndRotation(FVector(0,0,Step==14?200:-200),FRotator(Step==14?-90:90,0,0));
            }
            if(Step>=16){
                Capture->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(-90,0,0));
                M->SetScalarParameterValue(TEXT("AC2WaterBelow"),0);
                // Isolate reflection projection and foam coverage for these probes.
                M->SetVectorParameterValue(TEXT("Tint"),FLinearColor::Black);
                M->SetTextureParameterValue(TEXT("AC2WaterReflection"),SplitReflection);
                M->SetTextureParameterValue(TEXT("AC2WaterBumpMap"),Step==16?NoDistortion:SignedDistortion);
            }
            if(Step==18){
                M->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),0);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoam"),Masks[1]);
                M->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),Masks[1]);
            }
            FinishMaterialShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.4f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.4f));
        // Prime newly visible opaque proxies before using their scene-color
        // copy as the transmission baseline.
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Step,BedBaseline](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read water pixels"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            if(Step==18){
                const auto Actual=Pixels[32*64+32].ToFColorSRGB();
                TestTrue(FString::Printf(TEXT("Half-covered foam blends in display RGB: %s"),*Actual.ToString()),
                    FMath::Abs(Actual.R-129)<4 && FMath::Abs(Actual.G-129)<4 && FMath::Abs(Actual.B-129)<4);
                return true;
            }
            if(Step>=16){
                const auto Top=Pixels[22*64+32].ToFColorSRGB(),Bottom=Pixels[42*64+32].ToFColorSRGB();
                const auto Center=Pixels[32*64+32].ToFColorSRGB();
                TestTrue(TEXT("Projected reflection reverses the vertical axis"),Top.B>120 && Top.R<5 && Bottom.R>120 && Bottom.B<5);
                TestTrue(TEXT("Signed ripple displaces the projected reflection sample"),
                    Step==16 ? Center.R>120 && Center.B<5 : Center.B>120 && Center.R<5);
                return true;
            }
            if(Step>=12){
                const auto Actual=Pixels[32*64+32].ToFColorSRGB();
                const auto Blended=(FLinearColor::FromSRGBColor(FColor(51,77,128))*.8f).ToFColorSRGB();
                const FVector Expected=(Step==12 || Step==14)?FVector::ZeroVector:FVector(Blended.R,Blended.G,Blended.B);
                TestTrue(FString::Printf(TEXT("Water side %d uses the underside material independently of the reflection pass: %s expected %s"),Step,*Actual.ToString(),*Expected.ToString()),
                    FMath::Abs(Actual.R-Expected.X)<4 && FMath::Abs(Actual.G-Expected.Y)<4 && FMath::Abs(Actual.B-Expected.Z)<4);
                return true;
            }
            if(Step>=7){
                const FColor Left=Pixels[32*64+24].ToFColorSRGB(),Right=Pixels[32*64+40].ToFColorSRGB();
                if(Step==7){
                    *BedBaseline={Left,Right};
                    TestTrue(TEXT("Riverbed control exposes distinct red and blue halves"),
                        FMath::Abs(int(Left.R)-int(Right.R))>120 && FMath::Abs(int(Left.B)-int(Right.B))>120);
                }else{
                    const double W=Step==9?.5*128/255.:Step==10?0:.5;
                    const FColor Sky=FLinearColor(.25,.5,1).ToFColorSRGB();
                    for(int32 Side=0;Side<2;++Side){
                        const auto B=(*BedBaseline)[Side],Actual=Side==0?Left:Right;
                        FVector Expected=FMath::Lerp(FVector(B.R,B.G,B.B),FVector(51,76.5,127.5),W);
                        if(Step==11)Expected=FMath::Lerp(Expected,FVector(Sky.R,Sky.G,Sky.B),W);
                        TestTrue(FString::Printf(TEXT("Riverbed %d side %d survives transmission: %s expected %s"),Step,Side,*Actual.ToString(),*Expected.ToString()),
                            FMath::Abs(Actual.R-Expected.X)<4 && FMath::Abs(Actual.G-Expected.Y)<4 && FMath::Abs(Actual.B-Expected.Z)<4);
                    }
                }
                return true;
            }
            const double Weight=.5*(Step==0?0.:(Step==1 || Step==4)?128./255:1.);
            const FColor BlendedSky=(Step==3?FLinearColor::Black:FLinearColor(.25f,.5f,1)).ToFColorSRGB();
            const FVector Native=FVector(.2,.3,.5)*Weight;
            const FVector Reflection(BlendedSky.R/255.,BlendedSky.G/255.,BlendedSky.B/255.);
            // Native shore stage 0 is texture * vertex diffuse * 2. Water's
            // blue material tint must not color the separately lit foam.
            const FVector Expected=(Step==6?FVector(.2,.4,.6):Step>=4?FMath::Lerp(Native,Reflection,Weight):Native)*255;
            const FColor Actual=Pixels[32*64+32].ToFColorSRGB();
            TestTrue(FString::Printf(TEXT("Water %d blends in native RGB: %s expected %s"),Step,*Actual.ToString(),*Expected.ToString()),
                FMath::Abs(Actual.R-Expected.X)<4 && FMath::Abs(Actual.G-Expected.Y)<4 && FMath::Abs(Actual.B-Expected.Z)<4);return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Reflection,Masks,SplitReflection,NoDistortion,SignedDistortion](){
        for(auto* T:{SplitReflection,NoDistortion,SignedDistortion})T->RemoveFromRoot();
        for(auto* T:Masks)T->RemoveFromRoot();Reflection->RemoveFromRoot();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AppearanceOpacityTest,"AC2.Data.AppearanceOpacity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AppearanceOpacityTest::RunTest(const FString&)
{
    FAC2AppearanceOpacity A;
    TestEqual(TEXT("No atom leaves the model opaque"),A.Value,1.f);
    A.Apply(2,.25f,.8f);
    A.Apply(1,0,1);
    TestEqual(TEXT("A lower priority invisibility atom cannot hide the model"),A.Value,.25f);
    A.Apply(2,-999.f,.75f);
    TestEqual(TEXT("Equal priority replaces with the applied scalar"),A.Value,.75f);
    A.Apply(3,-999.f,0);
    TestEqual(TEXT("Zero scalar represents invisibility, not a missing effect"),A.Value,0.f);
    A.Apply(4,-999.f,2);
    TestEqual(TEXT("Applied opacity is clamped"),A.Value,1.f);
    return true;
}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AnisotropicUVTest,"AC2.Render.AnisotropicTexcoords",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AnisotropicUVTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()){AddError(TEXT("Anisotropic lookup test requires a graphics device"));return false;}
    FAC2AssetLibrary Assets;
    FAC2DatArchive Portal;
    const FString Directory=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FString Error;
    if(!Assets.Open(Directory,Error) || !Portal.Open(Directory/TEXT("portal.dat"),1,Error)){AddError(Error);return false;}
    FAC2PreviewLighting Lighting;FAC2PreviewLight Sun;Sun.Directional=true;Sun.Direction=FVector(0,0,-1);
    Lighting.Lights.Add(Sun);Assets.SetPreviewLighting(Lighting,FTransform::Identity);
    uint32 Instance=0;
    for(uint32 Candidate:{0x310004cbu,0x310004d7u}){
        TArray<uint8> Bytes;
        if(Portal.Read(Candidate,Bytes,Error) && Bytes.Num()>=8){
            uint32 Template;FMemory::Memcpy(&Template,Bytes.GetData()+4,4);
            if(Template==0x2b000051u){Instance=Candidate;break;}
        }
    }
    if(!TestTrue(TEXT("profile contains an authored anisotropic material instance"),Instance!=0))return false;
    auto* Material=Cast<UMaterialInstanceDynamic>(Assets.Material(Instance));
    if(!TestNotNull(TEXT("high-detail lookup material"),Material))return false;
    TestTrue(TEXT("dedicated anisotropic parent"),Material->Parent->GetName().Contains(TEXT("Anisotropic")));
    TestEqual(TEXT("anisotropic high-detail technique"),Material->K2_GetVectorParameterValue(TEXT("AC2StageFlags")).B,5.f);
    TestEqual(TEXT("authored stage one coordinate generation"),Material->K2_GetVectorParameterValue(TEXT("AC2Stage1UV")).R,512.f);
    TestEqual(TEXT("preview directional light reaches the lookup"),Material->K2_GetVectorParameterValue(TEXT("AC2AnisotropicLight")).B,1.f);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    auto* Actor=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();
    Mesh->CreateMeshSection(0,{{-160,-160,0},{160,-160,0},{160,160,0},{-160,160,0}},
        {0,1,2,0,2,3,2,1,0,3,2,0},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
        {{0,0},{0,0},{0,0},{0,0}}, {},{},false);
    Mesh->SetMaterial(0,Material);
    auto* Texture=UTexture2D::CreateTransient(8,8,PF_B8G8R8A8);
    Texture->Filter=TF_Nearest;Texture->AddressX=Texture->AddressY=TA_Clamp;Texture->SRGB=true;
    auto& Data=Texture->GetPlatformData()->Mips[0].BulkData;
    auto* Texels=static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE));
    for(int32 Y=0;Y<8;++Y)for(int32 X=0;X<8;++X)Texels[Y*8+X]=FColor(X*255/7,Y*255/7,0,255);
    Data.Unlock();Texture->UpdateResource();
    Material->SetTextureParameterValue(TEXT("AC2Stage1Texture"),Texture);
    Material->SetVectorParameterValue(TEXT("AC2Stage1Color"),FLinearColor(2,2,0,0));
    Material->SetVectorParameterValue(TEXT("AC2Stage1RowU"),FLinearColor(1,0,0,0));
    Material->SetVectorParameterValue(TEXT("AC2Stage1RowV"),FLinearColor(0,1,0,0));
    Material->SetScalarParameterValue(TEXT("AC2NativeSpecularEnabled"),0);
    auto* Capture=NewObject<USceneCaptureComponent2D>(Actor);Capture->RegisterComponent();
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->CaptureSource=SCS_SceneColorHDR;
    Capture->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(-90,0,0));Capture->FOVAngle=90;
    Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetFog(false);
    auto* Image=NewObject<UTextureRenderTarget2D>(Actor);Image->InitCustomFormat(64,64,PF_FloatRGBA,true);
    Image->UpdateResourceImmediate(true);Capture->TextureTarget=Image;
    // The initial view requests the vertex-interpolator shader permutation.
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture](){
        World->SendAllEndOfFrameUpdates();Capture->CaptureScene();return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
    for(int32 Step=0;Step<3;++Step){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Material,Capture,Step](){
            Material->SetVectorParameterValue(TEXT("AC2AnisotropicLight"),Step==0?FLinearColor(1,0,0,0):FLinearColor(0,0,1,0));
            Material->SetVectorParameterValue(TEXT("AC2UIIgnore"),FLinearColor(Step==2?1.f:0.f,0,0,0));
            FinishMaterialShaders();World->SendAllEndOfFrameUpdates();Capture->CaptureScene();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Step](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("anisotropic lookup readback"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            // Each vertex is at the same distance from the eye. With the sun
            // ignored both coordinates equal 200/sqrt(200^2+2*160^2), texel 5.
            // Recomputing at the centre pixel would instead sample texel 7.
            const FLinearColor Pixel=Pixels[32*64+32];
            const float EyeValue=FLinearColor::FromSRGBColor(FColor(5*255/7,0,0)).R;
            TestTrue(FString::Printf(TEXT("lookup step %d returned %s"),Step,*Pixel.ToString()),
                Step==0?(Pixel.R<.03f && Pixel.G>.1f):Step==1?(Pixel.R>.96f && Pixel.G>.96f):
                (FMath::IsNearlyEqual(Pixel.R,EyeValue,.04f) && FMath::IsNearlyEqual(Pixel.G,EyeValue,.04f)));
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ProjectedFrameBufferTest,"AC2.Render.ProjectedFrameBuffer",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ProjectedFrameBufferTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()){AddError(TEXT("Framebuffer test requires a graphics device"));return false;}
    const FString Directory=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2AssetLibrary Assets;
    FAC2DatArchive Portal;
    FString Error;
    if(!Assets.Open(Directory,Error) || !Portal.Open(Directory/TEXT("portal.dat"),1,Error)){AddError(Error);return false;}
    FAC2DatEntry Entry;
    if(!Portal.Find(0x2b000065,Entry,Error)){AddInfo(TEXT("This DAT profile has no projected framebuffer template"));return true;}
    auto* Effect=Cast<UMaterialInstanceDynamic>(Assets.Material(0x31000323));
    if(!TestNotNull(TEXT("retail framebuffer material instance"),Effect))return false;
    TestEqual(TEXT("framebuffer parent with authored culling"),Effect->Parent->GetName(),FString(TEXT("M_AC2FrameBufferOneSided")));
    Assets.UpdateMaterials(0);
    TestTrue(TEXT("native projection modifiers"),Effect->K2_GetVectorParameterValue(TEXT("AC2UVRowU")).Equals(FLinearColor(.58f,0,-.5f,0)));
    TestNotNull(TEXT("authored stage-one mask"),Effect->K2_GetTextureParameterValue(TEXT("AC2Stage1Texture")));
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    auto* Actor=World->SpawnActor<AActor>();
    auto Plane=[&](float Y0,float Y1,float Z,UMaterialInterface* Material){
        auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);Mesh->RegisterComponent();
        Mesh->CreateMeshSection(0,{{-300,Y0,Z},{300,Y0,Z},{300,Y1,Z},{-300,Y1,Z}},
            {0,1,2,0,2,3,2,1,0,3,2,0},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
            {{0,0},{1,0},{1,1},{0,1}},{},{},false);
        Mesh->SetMaterial(0,Material);
    };
    for(int32 I=0;I<2;++I){
        auto* Color=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2UIOpaque.M_AC2UIOpaque")),Actor);
        Color->SetVectorParameterValue(TEXT("Tint"),I?FLinearColor::Blue:FLinearColor::Red);
        Color->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
        Plane(I?0:-300,I?300:0,0,Color);
    }
    Plane(-300,300,50,Effect);
    // White isolates the projection. The actual binding was checked above.
    Effect->SetTextureParameterValue(TEXT("AC2Stage1Texture"),LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
    auto* Camera=World->SpawnActor<AActor>();auto* Capture=NewObject<USceneCaptureComponent2D>(Camera);
    Camera->SetRootComponent(Capture);Capture->RegisterComponent();Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(-90,0,0));Capture->FOVAngle=90;
    Capture->CaptureSource=SCS_SceneColorHDR;Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetFog(false);
    auto* Image=NewObject<UTextureRenderTarget2D>(Camera);Image->InitCustomFormat(64,64,PF_FloatRGBA,true);Image->UpdateResourceImmediate(true);Capture->TextureTarget=Image;
    // The first offscreen view requests the translucent scene-color shader permutation.
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture](){
        World->SendAllEndOfFrameUpdates();Capture->CaptureScene();return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
    for(int32 Step=0;Step<3;++Step){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Effect,Step](){
            // Flip the projected copy, then reveal the original with zero opacity.
            Effect->SetVectorParameterValue(TEXT("AC2UVRowU"),FLinearColor(Step?-.58f:.58f,0,-.5f,0));
            Effect->SetScalarParameterValue(TEXT("AC2AppearanceOpacity"),Step==2?0.f:1.f);
            FinishMaterialShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Step](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("framebuffer readback"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            const auto Left=Pixels[32*64+16],Right=Pixels[32*64+48];
            const bool Flipped=Step==1;
            TestTrue(FString::Printf(TEXT("projected halves step %d: %s / %s"),Step,*Left.ToString(),*Right.ToString()),
                Flipped?(Left.B>Left.R+.2f && Right.R>Right.B+.2f):(Left.R>Left.B+.2f && Right.B>Right.R+.2f));
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
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
    // UI previews keep the exact native vertex-lighting cascade. World
    // materials now use shadowed direct lights, checked below by rendered pixels.
    for(bool Preview:{true})for(int32 Case=0;Case<7;++Case) {
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
    auto* Blocker=World->SpawnActor<AActor>();auto* Occluder=NewObject<UProceduralMeshComponent>(Blocker);
    Blocker->SetRootComponent(Occluder);Occluder->RegisterComponent();
    Occluder->CreateMeshSection(0,{{-50,23,-7},{-50,37,-7},{-50,37,27},{-50,23,27}},
        {0,1,2,0,2,3},{{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},{{0,0},{1,0},{1,1},{0,1}},
        {FColor::White,FColor::White,FColor::White,FColor::White},{},false);
    Capture->ShowOnlyActorComponents(Blocker);
    auto* SunActor=World->SpawnActor<AActor>();auto* Sun=NewObject<UDirectionalLightComponent>(SunActor);
    SunActor->SetRootComponent(Sun);Sun->SetMobility(EComponentMobility::Movable);Sun->SetIntensity(3);
    Sun->SetCastShadows(true);Sun->ShadowBias=.1f;Sun->RegisterComponent();
    SunActor->SetActorRotation(FVector(150,-90,-30).Rotation());Sun->SetVisibility(false);
    auto* PointActor=World->SpawnActor<AActor>();auto* Point=NewObject<UPointLightComponent>(PointActor);
    PointActor->SetRootComponent(Point);Point->SetMobility(EComponentMobility::Movable);
    Point->SetUseInverseSquaredFalloff(false);Point->SetAttenuationRadius(1000);Point->SetIntensity(4);
    Point->SetLightingChannels(false,true,false);Point->SetCastShadows(true);Point->ShadowBias=.1f;Point->RegisterComponent();
    PointActor->SetActorLocation(FVector(-150,90,30));Point->SetVisibility(false);
    auto Unshadowed=MakeShared<float>(0);
    for(int32 Op:{4,5,6})for(int32 Type=0;Type<2;++Type)for(bool Shadows:{false,true}) {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mesh,Occluder,Texture,Sun,Point,Type,Shadows,World,Op](){
            auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2OpaqueTwoSided.M_AC2OpaqueTwoSided"));
            if(!TestNotNull(TEXT("Shadowed world template"),Parent))return true;
            auto* M=UMaterialInstanceDynamic::Create(Parent,Mesh);Mesh->SetMaterial(0,M);Occluder->SetMaterial(0,M);
            M->SetTextureParameterValue(TEXT("Albedo"),Texture);M->SetTextureParameterValue(TEXT("AC2Stage0Texture"),Texture);
            M->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
            M->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
            M->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(Op,2,0,0));
            M->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,2,0,0));
            M->SetVectorParameterValue(TEXT("Tint"),FLinearColor::White);
            M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(1,1,0,0));
            M->SetVectorParameterValue(TEXT("AC2UIAmbient"),FLinearColor(.1,.1,.1));
            Mesh->SetLightingChannels(Type==0,Type==1,false);Occluder->SetLightingChannels(Type==0,Type==1,false);
            Occluder->SetCastShadow(Shadows);Sun->SetVisibility(Type==0);Point->SetVisibility(Type==1);
            FinishMaterialShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Unshadowed,Type,Shadows,Output,Op](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read world shadow pixels"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            float Level=0;for(int32 Y=30;Y<34;++Y)for(int32 X=30;X<34;++X)Level+=Pixels[Y*64+X].R/16;
            if(!Shadows)*Unshadowed=Level;
            else TestTrue(FString::Printf(TEXT("%s shadow darkens native surface, lit %.4f shadow %.4f"),Type==0?TEXT("Sun"):TEXT("Dungeon point light"),*Unshadowed,Level),*Unshadowed-Level>.02f);
            TArray<FColor> Colors;for(const auto& P:Pixels){auto C=P.ToFColorSRGB();C.A=255;Colors.Add(C);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(64,64,Colors,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("shadow-%d-%d-%d.png"),Op,Type,int(Shadows))));return true;
        }));
        if(Type==0 && !Shadows) {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureSource=ESceneCaptureSource::SCS_BaseColor;return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,Capture,Op](){
                TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
                if(TestTrue(TEXT("Read world albedo"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags))) {
                    const float Actual=Pixels[32*64+32].R,Expected=FLinearColor::FromSRGBColor(FColor(128,128,128)).R;
                    TestTrue(FString::Printf(TEXT("World stage %d retains mid-grey albedo before lighting (%.4f vs %.4f)"),Op,Actual,Expected),FMath::Abs(Actual-Expected)<.01f);
                }
                Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;return true;
            }));
        }
    }
    // Test the compiled alpha path with and without the native texture cascade.
    // Compare linear pixels against the full-opacity baseline: RGB must not be
    // multiplied twice, and the zero-opacity model must disappear completely.
    auto FullOpacity=MakeShared<float>(0);
    for(bool Preview:{true,false})for(bool Native:{false,true})for(float Opacity:{1.f,.5f,0.f}) {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mesh,Occluder,Texture,Sun,Point,World,Preview,Native,Opacity](){
            Occluder->SetVisibility(false);Sun->SetVisibility(!Preview);Point->SetVisibility(false);
            // The world fallback uses UE lighting, unlike the emissive native
            // cascade and UI preview. Give it a direct light on the same channel.
            Mesh->SetLightingChannels(true,false,false);
            const TCHAR* Path=Preview?TEXT("/Game/AC2/Materials/M_AC2UIAlpha.M_AC2UIAlpha"):TEXT("/Game/AC2/Materials/M_AC2SceneryAlpha.M_AC2SceneryAlpha");
            auto* Parent=LoadObject<UMaterialInterface>(nullptr,Path);
            if(!TestNotNull(TEXT("Appearance fade template"),Parent))return true;
            auto* M=UMaterialInstanceDynamic::Create(Parent,Mesh);Mesh->SetMaterial(0,M);
            M->SetTextureParameterValue(TEXT("Albedo"),Texture);M->SetTextureParameterValue(TEXT("AC2Stage0Texture"),Texture);
            M->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,2,0,0));
            M->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,2,0,0));
            M->SetVectorParameterValue(TEXT("AC2UIAmbient"),FLinearColor::White);
            M->SetScalarParameterValue(TEXT("AC2NativeStages"),Native?1.f:0.f);
            M->SetScalarParameterValue(TEXT("AC2AppearanceOpacity"),Opacity);
            FinishMaterialShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        // Loading a new parent may enqueue its shader after the first capture.
        // Resolve that work and render again, as in the native RGB cases above.
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Capture](){FinishMaterialShaders();Capture->CaptureScene();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Image,FullOpacity,Opacity,Preview,Native](){
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read appearance fade pixels"),Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            const float Actual=Pixels[32*64+32].R;
            if(Opacity==1.f) { *FullOpacity=Actual;TestTrue(FString::Printf(TEXT("Full-opacity surface, preview %d native %d: %.4f"),int(Preview),int(Native),Actual),Actual>.02f); }
            else TestTrue(FString::Printf(TEXT("Appearance opacity %.1f, preview %d native %d: %.4f vs %.4f"),Opacity,int(Preview),int(Native),Actual,*FullOpacity*Opacity),
                FMath::Abs(Actual-*FullOpacity*Opacity)<.005f);
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Texture](){Texture->RemoveFromRoot();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
