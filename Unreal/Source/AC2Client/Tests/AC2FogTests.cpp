#include "AC2Fog.h"
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
namespace
{
constexpr int32 ProbeSize = 96;
const FLinearColor SurfaceRGB(.8f,.2f,.1f,1.f);
const FLinearColor MistRGB(.1f,.4f,.9f,1.f);
const FLinearColor BedRGB(.15f,.65f,.25f,1.f);

// The oracle is expressed in native display RGB. It does not call the
// renderer's fog factor or conversion helper, so gamma-space mistakes fail.
FLinearColor DecodeDisplay(FLinearColor C)
{
    auto Channel=[](float X){return X<=.04045f?X/12.92f:FMath::Pow((X+.055f)/1.055f,2.4f);};
    return FLinearColor(Channel(C.R),Channel(C.G),Channel(C.B),C.A);
}
FLinearColor FoggedDisplay(FLinearColor C,float Amount)
{
    return DecodeDisplay(FMath::Lerp(C,MistRGB,FMath::Clamp(Amount,0.f,1.f)));
}

struct FFogProbe
{
    UWorld* World=nullptr;
    AActor* Actor=nullptr;
    UProceduralMeshComponent* Surface=nullptr;
    UProceduralMeshComponent* Bed=nullptr;
    UProceduralMeshComponent* Mixed=nullptr;
    UMaterialInstanceDynamic* ExemptMaterial=nullptr;
    USceneCaptureComponent2D* Capture=nullptr;
    UTextureRenderTarget2D* Image=nullptr;
    TMap<FString,UMaterialInstanceDynamic*> Materials;
    UTexture2D* OpaqueMask=nullptr;
    UTexture2D* ClearMask=nullptr;
    FVector Origin=FVector::ZeroVector;

    bool Open()
    {
        const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        if(!World)return false;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        Actor=World->SpawnActor<AActor>();
        Surface=NewObject<UProceduralMeshComponent>(Actor);Actor->SetRootComponent(Surface);Surface->RegisterComponent();
        Bed=NewObject<UProceduralMeshComponent>(Actor);Bed->RegisterComponent();
        // Small triangles prevent vertex interpolation from confounding the
        // distance test. The same flat surface covers center and oblique rays.
        TArray<FVector> V,N;TArray<FVector2D> UV;TArray<int32> I;
        constexpr int32 Cells=40;
        for(int32 Y=0;Y<=Cells;++Y)for(int32 X=0;X<=Cells;++X){
            V.Add(FVector(-5000+X*250,-5000+Y*250,0));N.Add(FVector::UpVector);
            UV.Add(FVector2D(double(X)/Cells,double(Y)/Cells));
            if(X<Cells && Y<Cells){const int32 A=Y*(Cells+1)+X;I.Append({A,A+Cells+2,A+1,A,A+Cells+1,A+Cells+2});}
        }
        for(auto* Mesh:{Surface,Bed}){
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);
            Mesh->CreateMeshSection(0,V,I,N,UV,{},{},false);
        }
        for(const TCHAR* Name:{TEXT("M_AC2Opaque"),TEXT("M_AC2SceneryAlpha"),TEXT("M_AC2SceneryAdd"),
            TEXT("M_AC2UIAlpha"),TEXT("M_AC2SkyAlpha"),TEXT("M_AC2Sky"),TEXT("M_AC2Water"),TEXT("M_AC2Grass")}){
            const FString Path=FString::Printf(TEXT("/Game/AC2/Materials/%s.%s"),Name,Name);
            auto* Parent=LoadObject<UMaterialInterface>(nullptr,*Path);
            if(!Parent)return false;
            auto* M=UMaterialInstanceDynamic::Create(Parent,Actor);M->AddToRoot();Materials.Add(Name,M);
            M->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
            M->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
            M->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
            M->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,0,0,0));
            M->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,0,0,0));
            M->SetVectorParameterValue(TEXT("Tint"),SurfaceRGB);
        }
        auto* Ground=UMaterialInstanceDynamic::Create(Materials[TEXT("M_AC2Opaque")]->Parent,Actor);
        Ground->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
        Ground->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(0,1,0,0));
        Ground->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(1,0,0,1));
        Ground->SetVectorParameterValue(TEXT("AC2Stage0Color"),FLinearColor(2,0,0,0));
        Ground->SetVectorParameterValue(TEXT("AC2Stage0Alpha"),FLinearColor(2,0,0,0));
        Ground->SetVectorParameterValue(TEXT("Tint"),BedRGB);Bed->SetMaterial(0,Ground);
        ExemptMaterial=UMaterialInstanceDynamic::Create(Materials[TEXT("M_AC2Opaque")]->Parent,Actor);
        ExemptMaterial->CopyParameterOverrides(Materials[TEXT("M_AC2Opaque")]);
        ExemptMaterial->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));
        ExemptMaterial->AddToRoot();
        Mixed=NewObject<UProceduralMeshComponent>(Actor);Mixed->RegisterComponent();
        Mixed->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mixed->SetCastShadow(false);
        for(int32 Section=0;Section<2;++Section){
            const double A=Section==0?-5000:0,B=Section==0?0:5000;
            Mixed->CreateMeshSection(Section,{{-5000,A,0},{5000,A,0},{5000,B,0},{-5000,B,0}},
                {0,2,1,0,3,2},{FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
                {{0,0},{1,0},{1,1},{0,1}},{},{},false);
            Mixed->SetMaterial(Section,Section==0?ExemptMaterial:Materials[TEXT("M_AC2Opaque")]);
        }
        auto Mask=[&](uint8 A){
            auto* T=UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);T->SRGB=true;T->NeverStream=true;
            auto& Data=T->GetPlatformData()->Mips[0].BulkData;
            *static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE))=FColor(255,255,255,A);
            Data.Unlock();T->UpdateResource();T->AddToRoot();return T;
        };
        OpaqueMask=Mask(255);ClearMask=Mask(0);
        auto* Water=Materials[TEXT("M_AC2Water")];
        Water->SetTextureParameterValue(TEXT("AC2ShoreMask"),OpaqueMask);
        Water->SetTextureParameterValue(TEXT("AC2ShoreFoamMask"),ClearMask);
        Water->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),0);
        auto* Camera=World->SpawnActor<AActor>();Capture=NewObject<USceneCaptureComponent2D>(Camera);
        Camera->SetRootComponent(Capture);Capture->RegisterComponent();
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->SetWorldLocationAndRotation(FVector::ZeroVector,FRotator(-90,0,0));Capture->FOVAngle=90;
        Capture->CaptureSource=SCS_SceneColorHDR;
        Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetEyeAdaptation(false);
        Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetFog(true);
        Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetAtmosphere(false);
        Image=NewObject<UTextureRenderTarget2D>(Camera);Image->ClearColor=FLinearColor::Black;
        Image->InitCustomFormat(ProbeSize,ProbeSize,PF_FloatRGBA,true);Image->UpdateResourceImmediate(true);
        Capture->TextureTarget=Image;
        Place(TEXT("M_AC2Opaque"),2000);
        return true;
    }
    void Place(const TCHAR* Material,float Depth,bool WithBed=false)
    {
        Surface->SetMaterial(0,Materials[Material]);Surface->SetWorldScale3D(FVector::OneVector);
        Surface->SetWorldLocation(Origin-FVector(0,0,Depth));Surface->SetVisibility(true);
        Bed->SetWorldLocation(Origin-FVector(0,0,4000));Bed->SetVisibility(WithBed);
        Mixed->SetVisibility(false);Mixed->SetWorldLocation(Origin-FVector(0,0,4000));
        ExemptMaterial->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));
        Capture->SetWorldLocationAndRotation(Origin,FRotator(-90,0,0));
        Capture->FOVAngle=90;
        Capture->ShowFlags.SetFog(true);
        Capture->ShowFlags.SetAtmosphere(false);
        for(auto& Pair:Materials){
            Pair.Value->SetVectorParameterValue(TEXT("Tint"),SurfaceRGB);
            Pair.Value->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,0,0,0));
        }
        Materials[TEXT("M_AC2Water")]->SetScalarParameterValue(TEXT("AC2WaterBelow"),0);
        Materials[TEXT("M_AC2Water")]->SetTextureParameterValue(TEXT("AC2ShoreMask"),OpaqueMask);
        RegisterAC2FogComponent(Surface);RegisterAC2FogComponent(Bed);RegisterAC2FogComponent(Mixed);
    }
    void SetFog(bool Enabled=true)
    {
        FAC2FogParameters Fog;Fog.Start=10;Fog.End=30;Fog.Color=MistRGB;Fog.Enabled=Enabled;
        UpdateAC2Fog(World,Fog);
    }
    void SetAlphaRange(float Start,float End)
    {
        FAC2FogParameters Fog;
        if(GetAC2Fog(World,Fog)){Fog.AlphaStart=Start;Fog.AlphaEnd=End;UpdateAC2Fog(World,Fog);}
    }
    void Render()
    {
#if WITH_EDITOR
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
        World->SendAllEndOfFrameUpdates();Capture->CaptureScene();
    }
    void Close()
    {
        if(World){ReleaseAC2Fog(World);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);World=nullptr;}
        for(auto& Pair:Materials)Pair.Value->RemoveFromRoot();Materials.Reset();
        if(ExemptMaterial){ExemptMaterial->RemoveFromRoot();ExemptMaterial=nullptr;}
        if(OpaqueMask){OpaqueMask->RemoveFromRoot();OpaqueMask=nullptr;}
        if(ClearMask){ClearMask->RemoveFromRoot();ClearMask=nullptr;}
    }
    ~FFogProbe(){Close();}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeFogRenderTest,"AC2.Rendering.NativeFog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2NativeFogRenderTest::RunTest(const FString&)
{
    if(!FApp::CanEverRender()){AddError(TEXT("Native fog test requires a graphics device"));return false;}
    auto Probe=MakeShared<FFogProbe>();auto Other=MakeShared<FFogProbe>();
    if(!TestTrue(TEXT("Create independent fog capture worlds"),Probe->Open() && Other->Open()))return false;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Probe,Other](){Probe->Render();Other->Render();return true;}));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
    constexpr int32 Cases=47;
    for(int32 Step=0;Step<Cases;++Step){
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Probe,Other,Step](){
            // Case42 deliberately keeps the same component/material state
            // across a hidden-to-visible transition without re-registration.
            if(Step!=42){Probe->Origin=FVector::ZeroVector;Probe->Place(TEXT("M_AC2Opaque"),2000);Probe->SetFog();}
            switch(Step){
            case 0:ReleaseAC2Fog(Probe->World);break;
            case 1:Probe->Place(TEXT("M_AC2Opaque"),1000);break;
            case 3:Probe->Place(TEXT("M_AC2Opaque"),3000);break;
            case 4:Probe->Place(TEXT("M_AC2Opaque"),4000);break;
            case 5:Probe->Place(TEXT("M_AC2Opaque"),500);break;
            case 6:Probe->Origin=FVector(2200000,-3000000,8000000);Probe->Place(TEXT("M_AC2Opaque"),2000);break;
            case 7:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000);break;
            case 8:{Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);auto Color=SurfaceRGB;Color.A=.4f;
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("Tint"),Color);break;}
            case 9:Probe->Place(TEXT("M_AC2SceneryAdd"),2000);break;
            case 10:Probe->Place(TEXT("M_AC2SceneryAdd"),4000);break;
            case 11:Probe->Place(TEXT("M_AC2UIAlpha"),2000);break;
            case 12:Probe->Place(TEXT("M_AC2SkyAlpha"),6000000);Probe->Surface->SetWorldScale3D(FVector(10000));break;
            case 13:Probe->Capture->ShowFlags.SetFog(false);break;
            case 14:Probe->SetFog(false);break;
            case 15:ReleaseAC2Fog(Probe->World);break;
            case 16:Other->Place(TEXT("M_AC2Opaque"),2000);break;
            case 17:Probe->Place(TEXT("M_AC2Water"),2000,true);break;
            case 18:Probe->Place(TEXT("M_AC2Water"),2000,true);
                Probe->Materials[TEXT("M_AC2Water")]->SetTextureParameterValue(TEXT("AC2ShoreMask"),Probe->ClearMask);break;
            case 19:Probe->Place(TEXT("M_AC2Water"),2000);
                Probe->Materials[TEXT("M_AC2Water")]->SetScalarParameterValue(TEXT("AC2WaterBelow"),1);
                Probe->Capture->SetWorldLocationAndRotation(FVector(0,0,-4000),FRotator(90,0,0));break;
            case 20:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000);Probe->SetFog(false);break;
            case 21:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000);Probe->Capture->ShowFlags.SetFog(false);break;
            case 22:Probe->Place(TEXT("M_AC2Water"),2000,true);Probe->Capture->ShowFlags.SetFog(false);break;
            case 23:Probe->Place(TEXT("M_AC2SceneryAlpha"),4000);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));break;
            case 24:Probe->Place(TEXT("M_AC2SceneryAdd"),4000);
                Probe->Materials[TEXT("M_AC2SceneryAdd")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));break;
            case 25:Probe->Place(TEXT("M_AC2Water"),2000,true);
                Probe->Materials[TEXT("M_AC2Water")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));break;
            case 26:Probe->Place(TEXT("M_AC2Opaque"),4000);
                Probe->Materials[TEXT("M_AC2Opaque")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));break;
            case 27:Probe->Place(TEXT("M_AC2Opaque"),4000,true);
                Probe->Materials[TEXT("M_AC2Opaque")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));
                Probe->Bed->SetWorldLocation(FVector(0,0,-2000));break;
            case 28:Probe->Place(TEXT("M_AC2Opaque"),2000);break;
            case 29:Probe->Surface->SetVisibility(false);Probe->Mixed->SetVisibility(true);break;
            case 30:Probe->Mixed->SetVisibility(true);break;
            case 31:Probe->Surface->SetVisibility(false);Probe->Mixed->SetVisibility(true);
                Probe->ExemptMaterial->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,0,0,0));break;
            case 32:{Probe->Surface->SetVisibility(false);Probe->Mixed->SetVisibility(true);
                const TArray<FVector> Moved={{-5000,-5000,1000},{5000,-5000,1000},{5000,0,1000},{-5000,0,1000}};
                Probe->Mixed->UpdateMeshSection(0,Moved,TArray<FVector>(),TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>());
                MarkAC2FogGeometryDirty(Probe->Mixed);break;}
            case 33:Probe->Place(TEXT("M_AC2Sky"),6000000);Probe->Surface->SetWorldScale3D(FVector(10000));
                // UE routes bIsSky opaque materials through SkyPass, which
                // requires Atmosphere even without a SkyAtmosphere actor.
                Probe->Capture->ShowFlags.SetAtmosphere(true);break;
            case 34:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,0,1,0));break;
            case 35:Probe->Place(TEXT("M_AC2SceneryAlpha"),800,true);Probe->SetAlphaRange(4,12);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,0,0));break;
            case 36:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);Probe->SetAlphaRange(4,12);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,0,0));break;
            case 37:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);Probe->SetAlphaRange(10,50);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,1,0));break;
            case 38:Probe->Place(TEXT("M_AC2SceneryAlpha"),800,true);Probe->SetAlphaRange(4,12);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,0,0));
                Probe->Capture->ShowFlags.SetFog(false);break;
            case 39:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);Probe->SetFog(false);Probe->SetAlphaRange(10,30);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,0,0));break;
            case 40:Probe->Place(TEXT("M_AC2SceneryAlpha"),2000,true);Probe->SetAlphaRange(10,30);
                Probe->Materials[TEXT("M_AC2SceneryAlpha")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,1,0,0));
                ReleaseAC2Fog(Probe->World);break;
            case 41:Probe->Surface->SetVisibility(false);Probe->Mixed->SetVisibility(true);
                Probe->Mixed->SetMeshSectionVisible(0,false);break;
            case 42:Probe->Mixed->SetMeshSectionVisible(0,true);
                Probe->World->Tick(LEVELTICK_All,1.f/60);break;
            case 43:case 44:case 45:case 46:
                Probe->Place(TEXT("M_AC2Grass"),Step==43?14800:Step==45?16400:15600);
                Probe->Capture->FOVAngle=10;Probe->SetFog(false);Probe->SetAlphaRange(152,160);
                Probe->Materials[TEXT("M_AC2Grass")]->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,Step==46?0:1,0,0));break;
            }
            if(Step!=42){RegisterAC2FogComponent(Probe->Surface);RegisterAC2FogComponent(Probe->Bed);RegisterAC2FogComponent(Probe->Mixed);}
            Probe->World->SendAllEndOfFrameUpdates();Other->World->SendAllEndOfFrameUpdates();return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.15f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Probe,Other,Step](){(Step==16?Other:Probe)->Render();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Probe,Other,Step](){(Step==16?Other:Probe)->Render();return true;}));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.15f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Probe,Other,Step](){
            auto Selected=Step==16?Other:Probe;
            TArray<FLinearColor> Pixels;FReadSurfaceDataFlags Flags(RCM_MinMax);Flags.SetLinearToGamma(false);
            if(!TestTrue(TEXT("Read native fog HDR pixels"),Selected->Image->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,Flags)))return true;
            auto Compare=[&](FString Label,int32 X,int32 Y,FLinearColor Expected){
                const auto Actual=Pixels[Y*ProbeSize+X];
                TestTrue(FString::Printf(TEXT("%s: actual %s expected %s"),*Label,*Actual.ToString(),*Expected.ToString()),
                    FMath::Abs(Actual.R-Expected.R)<.016f && FMath::Abs(Actual.G-Expected.G)<.016f && FMath::Abs(Actual.B-Expected.B)<.016f);
            };
            FLinearColor Expected=FoggedDisplay(SurfaceRGB,.5f);
            FString Label=FString::Printf(TEXT("Native fog case %d"),Step);
            switch(Step){
            case 0:Label=TEXT("Unregistered world remains unfogged");Expected=DecodeDisplay(SurfaceRGB);break;
            case 1:Label=TEXT("Fog begins at authored near distance");Expected=DecodeDisplay(SurfaceRGB);break;
            case 2:Label=TEXT("Fog midpoint is a 50 percent display-RGB blend");break;
            case 3:Label=TEXT("Authored far distance reaches full fog");Expected=DecodeDisplay(MistRGB);break;
            case 4:Label=TEXT("Beyond far stays fully fogged");Expected=DecodeDisplay(MistRGB);break;
            case 5:Label=TEXT("Before near remains clear");Expected=DecodeDisplay(SurfaceRGB);break;
            case 6:Label=TEXT("Large-coordinate altitude translation leaves fog unchanged");break;
            case 7:Label=TEXT("Alpha surface uses its own depth");break;
            case 8:Label=TEXT("Partial alpha blends once over the already-fogged bed");Expected=Expected*.4f+DecodeDisplay(MistRGB)*.6f;break;
            case 9:Label=TEXT("Additive emission attenuates toward black without fog tint");Expected=DecodeDisplay(SurfaceRGB*.5f);break;
            case 10:Label=TEXT("Distant additive emission disappears");Expected=FLinearColor::Black;break;
            case 11:Label=TEXT("UI preview material is exempt from world fog");Expected=DecodeDisplay(SurfaceRGB);break;
            case 12:Label=TEXT("Sky alpha layer is exempt from distance fog");Expected=DecodeDisplay(SurfaceRGB);break;
            case 13:Label=TEXT("Fog-disabled capture remains clear");Expected=DecodeDisplay(SurfaceRGB);break;
            case 14:Label=TEXT("Explicitly disabled fog remains clear");Expected=DecodeDisplay(SurfaceRGB);break;
            case 15:Label=TEXT("Releasing a world removes its fog state");Expected=DecodeDisplay(SurfaceRGB);break;
            case 16:Label=TEXT("Another world's fog cannot leak into an unregistered world");Expected=DecodeDisplay(SurfaceRGB);break;
            case 17:Label=TEXT("Water fogs its own surface without fogging the bed twice");Expected=DecodeDisplay(FMath::Lerp(MistRGB,FMath::Lerp(SurfaceRGB,MistRGB,.5f),.5f));break;
            case 18:Label=TEXT("Transparent shoreline preserves the once-fogged bed");Expected=DecodeDisplay(MistRGB);break;
            case 19:Label=TEXT("Underwater surface receives the same native distance fog");break;
            case 20:Label=TEXT("Disabling fog also clears translucent surface fog");Expected=DecodeDisplay(SurfaceRGB);break;
            case 21:Label=TEXT("Fog-disabled capture also bypasses translucent fog in the same world");Expected=DecodeDisplay(SurfaceRGB);break;
            case 22:Label=TEXT("Fog-disabled water capture preserves unfogged bed and surface");Expected=DecodeDisplay(FMath::Lerp(BedRGB,SurfaceRGB,.5f));break;
            case 23:Label=TEXT("Native no-fog alpha material stays visible beyond fog end");Expected=DecodeDisplay(SurfaceRGB);break;
            case 24:Label=TEXT("Native no-fog additive material retains its emission beyond fog end");Expected=DecodeDisplay(SurfaceRGB);break;
            case 25:Label=TEXT("Native no-fog water leaves its surface clear without undoing bed fog");Expected=DecodeDisplay(FMath::Lerp(MistRGB,SurfaceRGB,.5f));break;
            case 26:Label=TEXT("Native opaque no-fog material stays clear beyond fog end");Expected=DecodeDisplay(SurfaceRGB);break;
            case 27:Label=TEXT("Occluded no-fog object cannot exempt the ordinary foreground");Expected=FoggedDisplay(BedRGB,.5f);break;
            case 28:Label=TEXT("Re-enabling ordinary opaque fog clears its old exemption");break;
            case 29:Label=TEXT("Fogged half of mixed-section mesh reaches full fog");Expected=DecodeDisplay(MistRGB);break;
            case 30:Label=TEXT("Occluded mixed-section exemption cannot clear foreground fog");break;
            case 31:Label=TEXT("Removing mixed-section exemption restores ordinary fog");Expected=DecodeDisplay(MistRGB);break;
            case 32:Label=TEXT("Animating an exempt section does not clear adjacent ordinary fog");Expected=DecodeDisplay(MistRGB);break;
            case 33:Label=TEXT("Opaque native sky geometry is excluded from distance fog");Expected=DecodeDisplay(SurfaceRGB);break;
            case 34:Label=TEXT("DiffuseFog fades alpha while preserving unfogged source RGB");Expected=DecodeDisplay(SurfaceRGB)*.5f+DecodeDisplay(MistRGB)*.5f;break;
            case 35:Label=TEXT("DepthAlphaFog uses its independent range before world RGB fog starts");Expected=DecodeDisplay(SurfaceRGB)*.5f+DecodeDisplay(MistRGB)*.5f;break;
            case 36:Label=TEXT("DepthAlphaFog is fully transparent beyond its own far distance");Expected=DecodeDisplay(MistRGB);break;
            case 37:Label=TEXT("DepthAlphaFog and DiffuseFog multiply independent visibility factors");Expected=DecodeDisplay(SurfaceRGB)*.375f+DecodeDisplay(MistRGB)*.625f;break;
            case 38:Label=TEXT("Fog-disabled captures bypass independent alpha fog too");Expected=DecodeDisplay(SurfaceRGB);break;
            case 39:Label=TEXT("DepthAlphaFog remains active when world RGB fog is disabled");Expected=DecodeDisplay(SurfaceRGB)*.5f+DecodeDisplay(BedRGB)*.5f;break;
            case 40:Label=TEXT("Releasing fog also removes independent alpha-fade state");Expected=DecodeDisplay(SurfaceRGB);break;
            case 41:Label=TEXT("Hiding exempt section preserves ordinary section fog");Expected=DecodeDisplay(MistRGB);break;
            case 42:Label=TEXT("Revealing exempt section preserves ordinary section fog");Expected=DecodeDisplay(MistRGB);break;
            }
            if(Step<43)Compare(Label,ProbeSize/2,ProbeSize/2,Expected);
            else{
                // Render the actual masked grass template. A successful load
                // can still use UE's fallback shader after a custom-expression
                // compilation error; both source RGB and mask coverage must
                // therefore be measured. Narrow FOV holds radial fade nearly
                // constant across the sample while retaining 1024 dither taps.
                const FLinearColor Grass=DecodeDisplay(SurfaceRGB);
                int32 Visible=0,Unexpected=0,Total=0;
                for(int32 Y=ProbeSize/2-16;Y<ProbeSize/2+16;++Y)
                    for(int32 X=ProbeSize/2-16;X<ProbeSize/2+16;++X){
                        const auto P=Pixels[Y*ProbeSize+X];++Total;
                        const bool Source=FMath::Abs(P.R-Grass.R)<.016f && FMath::Abs(P.G-Grass.G)<.016f && FMath::Abs(P.B-Grass.B)<.016f;
                        const bool Empty=FMath::Abs(P.R)<.004f && FMath::Abs(P.G)<.004f && FMath::Abs(P.B)<.004f;
                        Visible+=Source;Unexpected+=!Source && !Empty;
                    }
                const float Coverage=float(Visible)/Total;
                TestEqual(TEXT("Grass renders native RGB or empty coverage, never a fallback shader"),Unexpected,0);
                if(Step==43)TestTrue(FString::Printf(TEXT("Grass remains fully visible before native 8m fade: coverage %.3f"),Coverage),Coverage>.99f);
                if(Step==44)TestTrue(FString::Printf(TEXT("Grass alpha fog precedes coverage dithering at fade midpoint: coverage %.3f"),Coverage),Coverage>.42f && Coverage<.58f);
                if(Step==45)TestEqual(TEXT("Grass disappears beyond its independent alpha-fog end"),Visible,0);
                if(Step==46)TestTrue(TEXT("Disabling depth-alpha fog preserves full grass coverage inside the fade band"),Coverage>.99f);
            }
            if(Step==29 || Step==31 || Step==32 || Step==42){
                Compare(TEXT("Mixed mesh left section respects its own fog flags"),24,48,
                    DecodeDisplay(Step!=31?SurfaceRGB:MistRGB));
                Compare(TEXT("Mixed mesh right section remains fogged"),72,48,DecodeDisplay(MistRGB));
            }
            if(Step==41)Compare(TEXT("Hidden exempt section does not render"),24,48,FLinearColor::Black);
            if(Step==2 || Step==7){
                // Native fragment.dll f_CalculateLinearFog consumes reciprocal
                // view distance, not view-space Z. At 90-degree square FOV the
                // ray through this pixel travels farther than the center ray.
                constexpr int32 X=72,Y=48;
                const double RayX=2.*(X+.5)/ProbeSize-1.,RayY=2.*(Y+.5)/ProbeSize-1.;
                const double Metres=20.*FMath::Sqrt(1.+RayX*RayX+RayY*RayY);
                Compare(Step==2?TEXT("Opaque oblique ray uses native radial distance"):
                    TEXT("Translucent oblique ray uses native radial distance"),X,Y,
                    FoggedDisplay(SurfaceRGB,float((Metres-10.)/20.)));
            }
            if(Step==30){
                const double RayX=2.*(24+.5)/ProbeSize-1.,RayY=2.*(48+.5)/ProbeSize-1.;
                const double Metres=20.*FMath::Sqrt(1.+RayX*RayX+RayY*RayY);
                Compare(TEXT("Hidden exempt section cannot remove foreground fog at its screen position"),24,48,
                    FoggedDisplay(SurfaceRGB,float((Metres-10.)/20.)));
            }
            if(Step==15){FAC2FogParameters Unused;TestFalse(TEXT("Released world has no registered fog"),GetAC2Fog(Probe->World,Unused));}
            if(Step==16){FAC2FogParameters Unused;TestFalse(TEXT("Second world has no fog registry entry"),GetAC2Fog(Other->World,Unused));}
            TArray<FColor> Colors;for(auto C:Pixels){auto P=C.ToFColorSRGB();P.A=255;Colors.Add(P);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(ProbeSize,ProbeSize,Colors,PNG);
            const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/NativeFog");IFileManager::Get().MakeDirectory(*Dir,true);
            FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("case-%02d.png"),Step)));
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Probe,Other](){Probe->Close();Other->Close();return true;}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeFogModuleLifecycleTest,"AC2.Rendering.NativeFogModuleLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2NativeFogModuleLifecycleTest::RunTest(const FString&)
{
    // This test deliberately stops the fog module. Refuse to disturb an
    // unrelated active gameplay world when run from an interactive editor.
    for(const auto& Context:GEngine->GetWorldContexts()){
        FAC2FogParameters Existing;
        if(Context.World() && GetAC2Fog(Context.World(),Existing)){
            AddError(TEXT("NativeFogModuleLifecycle requires no other active AC2 fog world"));return false;
        }
    }
    auto Probe=MakeShared<FFogProbe>();
    if(!TestTrue(TEXT("Create a surviving world for fog module lifecycle"),Probe->Open()))return false;
    // Clear the fixture's initial mixed mask before assigning distinct
    // original stencil state, then register both all-exempt and mixed meshes.
    Probe->ExemptMaterial->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(1,0,0,0));
    RegisterAC2FogComponent(Probe->Mixed);
    Probe->Surface->SetRenderCustomDepth(true);
    Probe->Surface->SetCustomDepthStencilValue(5);
    Probe->Surface->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_4);
    Probe->Mixed->SetRenderCustomDepth(false);
    Probe->Mixed->SetCustomDepthStencilValue(23);
    Probe->Mixed->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_16);
    Probe->Surface->SetMaterial(0,Probe->ExemptMaterial);
    Probe->Bed->SetMaterial(0,Probe->ExemptMaterial);
    Probe->ExemptMaterial->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(0,0,0,0));
    Probe->Mixed->SetVisibility(true);
    auto Proxies=[&](){
        TArray<UProceduralMeshComponent*> Result;
        TInlineComponentArray<UProceduralMeshComponent*> Components(Probe->Actor);
        for(auto* C:Components)if(C->GetAttachParent()==Probe->Mixed && C->IsRegistered())Result.Add(C);
        return Result;
    };
    auto CheckRestored=[&](){
        TestTrue(TEXT("Shutdown leaves original source meshes alive"),Probe->Surface->IsRegistered() && Probe->Mixed->IsRegistered());
        TestTrue(TEXT("Shutdown restores source custom-depth enable state"),Probe->Surface->bRenderCustomDepth);
        TestEqual(TEXT("Shutdown restores the original source stencil value"),Probe->Surface->CustomDepthStencilValue,5);
        TestTrue(TEXT("Shutdown restores the original source write mask"),Probe->Surface->CustomDepthStencilWriteMask==ERendererStencilMask::ERSM_4);
        TestFalse(TEXT("Shutdown disables custom depth on a source that originally disabled it"),Probe->Bed->bRenderCustomDepth);
        TestEqual(TEXT("Shutdown restores a source's originally empty stencil"),Probe->Bed->CustomDepthStencilValue,0);
        TestTrue(TEXT("Shutdown restores a source's original default write mask"),Probe->Bed->CustomDepthStencilWriteMask==ERendererStencilMask::ERSM_Default);
        TestFalse(TEXT("Mixed source retains its original custom-depth disable state"),Probe->Mixed->bRenderCustomDepth);
        TestEqual(TEXT("Mixed source retains its original stencil value"),Probe->Mixed->CustomDepthStencilValue,23);
        TestTrue(TEXT("Mixed source retains its original write mask"),Probe->Mixed->CustomDepthStencilWriteMask==ERendererStencilMask::ERSM_16);
        TestEqual(TEXT("Shutdown removes the mixed-section mask proxy"),Proxies().Num(),0);
        FAC2FogParameters Existing;
        TestFalse(TEXT("Shutdown removes fog parameters without destroying the world"),GetAC2Fog(Probe->World,Existing));
    };
    TWeakObjectPtr<UProceduralMeshComponent> PreviousProxy;
    for(int32 Cycle=0;Cycle<2;++Cycle){
        Probe->SetFog();RegisterAC2FogComponent(Probe->Surface);RegisterAC2FogComponent(Probe->Bed);RegisterAC2FogComponent(Probe->Mixed);
        const auto Current=Proxies();
        TestEqual(TEXT("Registration creates exactly one mixed-section mask"),Current.Num(),1);
        TestEqual(TEXT("Exemption preserves original stencil bits while adding its bit"),Probe->Surface->CustomDepthStencilValue,5|128);
        TestTrue(TEXT("Registered exemption uses its own stencil write mask"),Probe->Surface->CustomDepthStencilWriteMask==ERendererStencilMask::ERSM_128);
        TestTrue(TEXT("Exemption enables custom depth on a previously disabled source"),Probe->Bed->bRenderCustomDepth);
        FAC2FogParameters Registered;
        TestTrue(TEXT("Restart permits fresh world fog registration"),GetAC2Fog(Probe->World,Registered));
        if(!Current.IsEmpty()){
            if(Cycle>0)TestTrue(TEXT("Restart creates a fresh proxy instead of retaining the destroyed mask"),Current[0]!=PreviousProxy.Get());
            PreviousProxy=Current[0];
        }
        ShutdownAC2Fog();
        CheckRestored();
        TestTrue(TEXT("Old mask proxy is destroyed or unregistered"),!PreviousProxy.IsValid() || !PreviousProxy->IsRegistered());
        // No early exits occur between shutdown and startup, including on
        // failed assertions, so later tests keep their normal module service.
        StartupAC2Fog();
    }
    Probe->Close();
    return true;
}
#endif
