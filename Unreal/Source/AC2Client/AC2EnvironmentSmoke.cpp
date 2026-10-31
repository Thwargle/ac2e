#include "AC2GameMode.h"
#include "AC2Performance.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "AC2Explorer.h"
#include "AC2GameUI.h"
#include "AC2WorldGrid.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Reproducible DAT scenery views. No account service or persistent character store.
void AAC2GameMode::EnvironmentSmokeTick(float Delta)
{
    if(AC2Performance::Enabled())
    {
        SmokeTime+=Delta;
        if(SmokeTime>180)
        {
            UE_LOG(LogTemp,Error,TEXT("AC2 performance: world/shaders did not become ready in 180 seconds"));
            FPlatformMisc::RequestExitWithStatus(false,1);
            return;
        }
        // Stand still in the requested block, wait for streaming/shaders, then
        // measure real rendered frames without altering placement or detail.
#if WITH_EDITOR
        if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling())return;
#endif
        if(bWaiting || !Stream->AllReady()) { StageTime=0; return; }
        if(auto* PC=GetWorld()->GetFirstPlayerController())
            if(auto* P=Cast<AAC2Explorer>(PC->GetPawn()))P->SetHeld(true);
        StageTime+=Delta;
        if(StageTime<8) { AC2Performance::Reset(); return; }
        ++SmokeStage;
        AC2Performance::Add(TEXT("frame"),Delta*1000.);
        AC2Performance::Add(TEXT("game_thread"),FPlatformTime::ToMilliseconds(GGameThreadTime));
        AC2Performance::Add(TEXT("render_thread"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
        AC2Performance::Add(TEXT("gpu"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
        AC2Performance::Add(TEXT("draw_calls"),GNumDrawCallsRHI[0]);
        if(StageTime<23)return;
        uint32 ProfileBlock=InitialBlock;
        AC2::WorldBlock(StreamFocus,ProfileBlock);
        const FString Report=FString::Printf(TEXT("block=%04X frames=%d grass_batches=%d\n"),ProfileBlock,SmokeStage,Stream->GrassBatchCount())+AC2Performance::Report(SmokeStage);
        FString Path=FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Performance.txt");
        FParse::Value(FCommandLine::Get(),TEXT("AC2PerformanceReport="),Path);
        FFileHelper::SaveStringToFile(Report,*Path);
        UE_LOG(LogTemp,Display,TEXT("AC2 performance:\n%s"),*Report);
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling())return;
#endif
    SmokeTime += Delta; StageTime += Delta;
    if (EnvironmentModel) { EnvironmentModel->Animate(SmokeTime,0); EnvironmentModel->UpdateMaterials(SmokeTime); }
    auto Finish = [&](bool OK) {
        EnvironmentModel.Reset();
        FFileHelper::SaveStringToFile(OK ? TEXT("PASS") : TEXT("FAIL: environment streaming timed out"),
            *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/AC2Environment-smoke.txt")));
        bEnvironmentSmoke = false;
        FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1);
    };
    if (SmokeTime > 240) { Finish(false); return; }
    auto *PC = GetWorld()->GetFirstPlayerController();
    if (!PC || bWaiting || !Stream->AllReady() || StageTime < 3) return;
    auto *P = Cast<AAC2Explorer>(PC->GetPawn());
    if (P) { P->SetActorHiddenInGame(true); P->SetHeld(true); }
    if (auto *H = Cast<AAC2HUD>(PC->GetHUD()))
    { H->bShowHUD=false; if(H->GameUI) H->GameUI->SetVisibility(EVisibility::Collapsed); }
    auto View = [&](FVector Eye, FVector Target) {
        if (!EnvironmentCamera.IsValid())
        {
            auto *Camera = GetWorld()->SpawnActor<ACameraActor>();
            Camera->GetCameraComponent()->SetFieldOfView(60);
            EnvironmentCamera=Camera;
        }
        EnvironmentCamera->SetActorLocationAndRotation(Eye, (Target-Eye).Rotation());
        PC->SetViewTarget(EnvironmentCamera.Get());
    };
    auto Capture = [&](const TCHAR *Name) {
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots"),Name),false,false);
    };
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainSeamSmoke")))
    {
        if(!Far || Far->Center()!=InitialBlock)return;
        if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainOnly")) || FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainFlat")))
            for(TActorIterator<AActor> It(GetWorld());It;++It)
            {
                TArray<UProceduralMeshComponent*> Components;It->GetComponents(Components);
                for(auto* C:Components)
                    if(!C->ComponentHasTag(TEXT("AC2TerrainSurface")))C->SetVisibility(false);
                    else if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainFlat")))
                        if(auto* M=Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
                            M->SetTextureParameterValue(TEXT("Albedo"),LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
            }
        Sky->TimeScale=0;
        // Reported seam: 86B0, world 21440.5, 28301.5 metres (west tile edge).
        const FVector Seam(2144050,2830150,0);
        double Ground=0;
        if(!Stream->GroundAt(Seam+FVector(0,0,1000000),Ground)){Finish(false);return;}
        FString Label=TEXT("terrain-seam");
        FParse::Value(FCommandLine::Get(),TEXT("AC2TerrainCapture="),Label);
        switch(SmokeStage)
        {
        case 0: View(Seam+FVector(400,-700,Ground+350),Seam+FVector(-400,1700,Ground-50));break;
        case 1: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-north.png")));break;
        case 2: View(Seam+FVector(1700,-400,Ground+1800),Seam+FVector(0,0,Ground));break;
        case 3: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-overhead.png")));break;
        default: Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2WaterDistanceSmoke")))
    {
        const uint32 ExpectedCenter=SmokeStage>=5?0x85b0:InitialBlock;
        if(!Far || Far->Center()!=ExpectedCenter)return;
        int32 WaterBatches=0,WaterTriangles=0;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            if(It->IsHidden())continue;
            TArray<UProceduralMeshComponent*> Components;It->GetComponents(Components);
            for(auto* C:Components)if(C->IsVisible())
                for(int32 Section=0;Section<C->GetNumSections();++Section)
                    if(auto* M=C->GetMaterial(Section);M && M->GetBaseMaterial()->GetName()==TEXT("M_AC2Water"))
                    {++WaterBatches;WaterTriangles+=C->GetProcMeshSection(Section)->ProcIndexBuffer.Num()/3;}
        }
        if(Far->WaterTileCount()<=Stream->WaterCount() || WaterBatches!=Far->WaterBatchCount() ||
           WaterTriangles!=Far->WaterTileCount()*2)
        {UE_LOG(LogTemp,Error,TEXT("AC2 water: incomplete or overlapping surface (%d batches, %d triangles)"),WaterBatches,WaterTriangles);Finish(false);return;}
        Sky->TimeScale=0;
        // Reported coastline: 38.1N, 3.7E. Look over the near/far terrain boundary.
        const FVector Shore=AC2::BlockOrigin(0x84b0)+FVector(3480,1240,0);
        double Ground=0;
        if(!Stream->GroundAt(Shore+FVector(0,0,1000000),Ground)){Finish(false);return;}
        FString Label=TEXT("water-distance");
        FParse::Value(FCommandLine::Get(),TEXT("AC2WaterCapture="),Label);
        switch(SmokeStage)
        {
        case 0: View(Shore+FVector(0,0,Ground+500),Shore+FVector(60000,-10000,7100));break;
        case 1: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-coast.png")));break;
        case 2: View(Shore+FVector(-18000,12000,Ground+26000),Shore+FVector(30000,-5000,7100));break;
        case 3: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-overview.png")));break;
        case 4: if(!LoadBlock(0x85b0)){Finish(false);return;}break;
        case 5: View(Shore+FVector(0,0,Ground+500),Shore+FVector(60000,-10000,7100));break;
        case 6: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-streamed.png")));break;
        default: UE_LOG(LogTemp,Display,TEXT("AC2 water: PASS %d tiles in %d batches after streaming 84B0 to 85B0, no overlapping near surface"),Far->WaterTileCount(),WaterBatches);Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2ShorelineSmoke")))
    {
        const FVector Bank=AC2::BlockOrigin(0x74a9);
        double Surface=0;
        if(!Stream->WaterAt(Bank+FVector(5550,15540,9390),Surface) || FMath::Abs(Surface-9670)>0.1)
        { UE_LOG(LogTemp,Error,TEXT("AC2 shoreline: rendered bank and swimming surface disagree"));Finish(false);return; }
        switch(SmokeStage)
        {
        case 0: View(Bank+FVector(14000,18500,11000),Bank+FVector(5550,15540,9670));break;
        case 1: Capture(TEXT("AC2Environment-shoreline-bank.png"));break;
        case 2: View(Bank+FVector(5000,5000,19000),Bank+FVector(12000,14000,9670));break;
        case 3: Capture(TEXT("AC2Environment-shoreline-overview.png"));break;
        default: UE_LOG(LogTemp,Display,TEXT("AC2 shoreline: PASS 74A9 surface=%.2f cm"),Surface);Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    const FVector Sign = AC2::BlockOrigin(0x75b8)+FVector(13075.708,12993.612,14183.672);
    switch (SmokeStage)
    {
    case 0: View(Sign+FVector(-700,-1250,500),Sign+FVector(0,0,400)); break;
    case 1: Capture(TEXT("AC2Environment-sign-front.png")); break;
    case 2: View(Sign+FVector(700,1250,500),Sign+FVector(0,0,400)); break;
    case 3: Capture(TEXT("AC2Environment-sign-back.png")); break;
    case 4: LoadCell(0x03f40154); break;
    case 5: {
        auto *Actor=GetWorld()->SpawnActor<AActor>(); EnvironmentProp=Actor;
        auto *Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Root->RegisterComponent();
        const FVector Location=AC2::BlockOrigin(0x03f4)+FVector(18173.0484,6294.236,1512.1846);
        Actor->SetActorLocation(Location); EnvironmentModel=MakeUnique<FAC2AssetLibrary>(); FString Error;
        if(!EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(0x1f0012be,Actor,Root)) { Finish(false); return; }
        View(Location+FVector(-900,-750,360),Location+FVector(0,0,250));
        break;
    }
    case 6: Capture(TEXT("AC2Environment-menhir.png")); break;
    case 7: EnvironmentModel.Reset(); EnvironmentProp->Destroy(); if(!LoadBlock(0x5667)) { Finish(false); return; } break;
    case 8: {
        if (!Far || Far->TileCount()==0) return;
        const FVector LookAt = P->GetActorLocation();
        View(LookAt+FVector(0,0,140),LookAt+FVector(1300,200,-50));
        break;
    }
    case 9:
        if(Stream->GrassBatchCount()==0 || !Stream->GrassDepthReady()) { Finish(false); return; }
        Capture(TEXT("AC2Environment-foliage.png")); break;
    case 10: if(!LoadBlock(0x77b8)) { Finish(false); return; } break;
    case 11: {
        const FVector Origin=AC2::BlockOrigin(0x77b8);
        View(Origin+FVector(-700,7900,18700),Origin+FVector(1325,10716,18550));
        break;
    }
    case 12: Capture(TEXT("AC2Environment-arwic-placement.png")); break;
    case 13: LoadCell(0x7cb90102); break;
    case 14: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(15700,2100,6150),Origin+FVector(16117,2449,6180));
        break;
    }
    case 15: Capture(TEXT("AC2Environment-cave-torch.png")); break;
    case 16: if(!LoadBlock(0x7cb9)) { Finish(false); return; } break;
    case 17: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(1500,2600,9600),Origin+FVector(2500,5000,9700));
        auto* Actor=GetWorld()->SpawnActor<AActor>(); EnvironmentProp=Actor;
        auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        Actor->SetActorLocation(Origin+FVector(1800,3000,9500));
        EnvironmentModel=MakeUnique<FAC2AssetLibrary>();FString Error;
        if(!EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(0x1f000023,Actor,Root)) {Finish(false);return;}
        EnvironmentModel->SetAnimationState(AC2Motion::Ground,0x41000010);
        View(Actor->GetActorLocation()+FVector(-180,320,130),Actor->GetActorLocation()+FVector(0,0,100));
        break;
    }
    case 18: Capture(TEXT("AC2Environment-bow-idle.png")); break;
    case 19: EnvironmentModel->PlayBehavior(0x410000e2,0x41000010,.2f); break;
    case 20: Capture(TEXT("AC2Environment-bow-attack.png")); break;
    case 21: Capture(TEXT("AC2Environment-bow-recovery.png")); break;
    case 22: EnvironmentModel.Reset();EnvironmentProp->Destroy();break;
    case 23: {
        auto* Actor=GetWorld()->SpawnActor<AActor>();EnvironmentProp=Actor;
        auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        Actor->SetActorLocation(AC2::BlockOrigin(0x7cb9)+FVector(1800,3000,9600));
        EnvironmentModel=MakeUnique<FAC2AssetLibrary>();FString Error;
        if(!EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(0x1f0007dd,Actor,Root)){Finish(false);return;}
        const FVector Focus=Actor->GetActorLocation()+FVector(-85,0,16);
        View(Focus+FVector(0,300,30),Focus);break;
    }
    case 24: Capture(TEXT("AC2Environment-road-sign-front.png"));break;
    case 25: {
        const FVector Focus=EnvironmentProp->GetActorLocation()+FVector(-85,0,16);
        View(Focus+FVector(0,-300,30),Focus);break;
    }
    case 26: Capture(TEXT("AC2Environment-road-sign-back.png"));break;
    case 27: EnvironmentModel.Reset();EnvironmentProp->Destroy();break;
    case 28: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(1500,2600,10400),Origin+FVector(2500,5000,9600));break;
    }
    case 29: Capture(TEXT("AC2Environment-ground-cover.png"));break;
    default: Finish(true); return;
    }
    ++SmokeStage; StageTime=0;
}
