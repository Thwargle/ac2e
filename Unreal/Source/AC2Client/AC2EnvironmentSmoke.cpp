#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2GameUI.h"
#include "AC2WorldGrid.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Reproducible DAT scenery views. No account service or persistent character store.
void AAC2GameMode::EnvironmentSmokeTick(float Delta)
{
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
    if (SmokeTime > 180) { Finish(false); return; }
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
    case 9: Capture(TEXT("AC2Environment-foliage.png")); break;
    default: Finish(Stream->GrassBatchCount()>0 && Stream->GrassDepthReady()); return;
    }
    ++SmokeStage; StageTime=0;
}
