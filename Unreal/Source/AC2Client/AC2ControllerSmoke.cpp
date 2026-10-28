#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void AAC2GameMode::ControllerSmokeTick(float Dt)
{
    ControllerTime+=Dt;
    auto Finish=[&](FString Error){
        FFileHelper::SaveStringToFile(Error.IsEmpty()?TEXT("PASS"):TEXT("FAIL: ")+Error,
            *(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Controller-smoke.txt")));
        UE_LOG(LogTemp,Display,TEXT("AC2 controller smoke %s"),Error.IsEmpty()?TEXT("PASS"):*Error);
        bControllerSmoke=false;FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);
    };
    if(ControllerTime>160){Finish(TEXT("Timed out at stage ")+FString::FromInt(ControllerStage));return;}
    auto* PC=Cast<AAC2PlayerController>(GetWorld()->GetFirstPlayerController());
    auto* H=PC?Cast<AAC2HUD>(PC->GetHUD()):nullptr;
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(!H||!P||!H->CharacterUI||!H->GameUI)return;
    if(ControllerStage==4||ControllerStage==21||ControllerStage==22) {P->AddMovementInput(P->GetActorForwardVector());}
    if(ControllerTime<ControllerNext)return;
    FString Error;
    auto CheckEnvironment = [&](int32 Expected) {
        int32 Suns=0, Ambient=0, Fog=0;
        for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
        {
            ++Suns;
            if (Sky && !It->GetActorForwardVector().Equals(Sky->Lighting().LightDirection(), .002)) return false;
        }
        for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It) ++Ambient;
        for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It) ++Fog;
        return Suns==Expected && Ambient==Expected && Fog==Expected;
    };
    auto Capture=[](const TCHAR* Name){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots")/Name,true,false);};
    switch(ControllerStage)
    {
    case 0:
        if(InWorld()||bReady||!P->IsHidden()||P->HasModel()||H->CharacterUI->CurrentScreen()!=SAC2CharacterUI::EScreen::Login)
        {Finish(TEXT("World or default avatar loaded before login"));return;}
        Capture(TEXT("AC2Controller-login.png"));break;
    case 1:
        Offline();
        if(!H->CharacterUI->SmokeCreate(Error)){Finish(Error);return;}
        break;
    case 2: {
        if(!InWorld()||bWaiting){ControllerNext=ControllerTime+.5f;return;}
        if(P->IsHidden()||P->IsHeld()||!P->HasModel()||H->CharacterUI->IsOpen()) {Finish(TEXT("Entry did not restore character and input"));return;}
        if(!CheckEnvironment(1)){Finish(TEXT("Sky direction or light count incorrect on entry"));return;}
        if(IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt()!=1 ||
           IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt()!=0 ||
           !P->Camera->PostProcessSettings.bOverride_MotionBlurAmount || P->Camera->PostProcessSettings.MotionBlurAmount!=0)
        {Finish(TEXT("Temporal AA or motion blur enabled for CPU-skinned models"));return;}
        const auto* Viewport=GetWorld()->GetGameViewport();
        if(!Viewport || Viewport->EngineShowFlags.Tonemapper || Viewport->EngineShowFlags.Bloom ||
           Viewport->EngineShowFlags.EyeAdaptation || Viewport->EngineShowFlags.ColorGrading ||
           IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat()!=100)
        {Finish(TEXT("Native display gamma / full-resolution policy not applied"));return;}
        UE_LOG(LogTemp,Display,TEXT("AC2 render: native display gamma, 100%% resolution, spatial FXAA, motion blur off, DAT sunlight direction PASS"));
        if(!H->GameUI->SmokeCombatInput(0,Error)){Finish(Error);return;}
        PC->SetControlRotation(FRotator(-8,P->GetActorRotation().Yaw+160,0));
        P->GetCharacterMovement()->bUseControllerDesiredRotation=false;
        P->Boom->bDoCollisionTest=false;
        P->Boom->TargetArmLength=460;
        Capture(TEXT("AC2Controller-standing.png"));break;
    }
    case 3:
        if(!H->GameUI->SmokeCombatInput(1,Error)){Finish(Error);return;}
        SmokeStart=P->GetActorLocation();
        if(Mobs.Num()<3) for(int32 I=0;I<3;++I) SpawnMob();
        P->SetRunning(true);
        break;
    case 4:
        if(!H->GameUI->SmokeCombatInput(2,Error)){Finish(Error);return;}
        if(FVector::Dist2D(SmokeStart,P->GetActorLocation())<60){Finish(TEXT("Ground movement did not advance"));return;}
        if(!P->AuditAttachments(Error)){Finish(Error);return;}
        Capture(TEXT("AC2Controller-running.png"));
        break; // Keep velocity through the screenshot's render frame.
    case 5:
        if(!H->GameUI->SmokeCombatInput(3,Error)){Finish(Error);return;}
        P->SetRunning(false);P->GetCharacterMovement()->StopMovementImmediately();
        if(!P->PlayBehavior(AC2Motion::Jump)){Finish(TEXT("Native jump animation missing"));return;}
        P->Jump();ControllerNext=ControllerTime+.2f;++ControllerStage;return;
    case 6:
        if(!H->GameUI->SmokeCombatInput(4,Error)){Finish(Error);return;}
        if(!P->GetCharacterMovement()->IsFalling()){Finish(TEXT("Jump did not enter air"));return;}
        Capture(TEXT("AC2Controller-jumping.png"));break;
    case 7:
        if(InCombat() || P->GetNativeMode()!=AC2Motion::Peace){Finish(TEXT("Offline Z did not restore peace"));return;}
        if(!P->PlayBehavior(AC2Motion::Attack,AC2Motion::Unarmed)){Finish(TEXT("Native attack animation missing"));return;}
        ControllerNext=ControllerTime+.2f;++ControllerStage;return;
    case 8:Capture(TEXT("AC2Controller-attack.png"));break;
    case 9:
        P->SetDead(true);ControllerNext=ControllerTime+1.8f;++ControllerStage;return;
    case 10:
        if(!P->IsHeld()||!P->IsDead()){Finish(TEXT("Dead character can move"));return;}
        Capture(TEXT("AC2Controller-death.png"));break;
    case 11:
        P->SetDead(false);PC->CloseUI();
        if(!H->GameUI->OptionsOpen()){Finish(TEXT("Escape did not open options"));return;}
        break;
    case 12:
        if(!H->GameUI->SmokeOptions(0,Error)){Finish(Error);return;}
        Capture(TEXT("AC2Controller-options.png"));break;
    case 13:if(!H->GameUI->SmokeOptions(1,Error)){Finish(Error);return;}break;
    case 14:Capture(TEXT("AC2Controller-client-options.png"));break;
    case 15:
        H->GameUI->SmokeOptions(2,Error);
        if(InWorld()||bReady||!P->IsHidden()){Finish(TEXT("Logout left world visible"));return;}
        if(!CheckEnvironment(0)){Finish(TEXT("Logout left lights or fog active"));return;}
        break;
    case 16:Capture(TEXT("AC2Controller-selection.png"));break;
    case 17:H->CharacterUI->SmokeEnter();break;
    case 18:
        if(!InWorld()||bWaiting){ControllerNext=ControllerTime+.5f;return;}
        if(!P->HasModel()||P->IsHidden()||P->IsHeld()){Finish(TEXT("Reentry failed"));return;}
        if(!CheckEnvironment(1)){Finish(TEXT("Reentry duplicated lighting"));return;}
        break;
    case 19:
        if(!P->PlayBehavior(AC2Motion::Hit,AC2Motion::Melee)){Finish(TEXT("Native hit reaction unavailable"));return;}
        ControllerNext=ControllerTime+.15f;++ControllerStage;return;
    case 20:
        Capture(TEXT("AC2Controller-hit.png"));
        P->GetCharacterMovement()->StopMovementImmediately();SmokeStart=P->GetActorLocation();
        P->SetMotionParameter(TEXT("velocity"),0);break;
    case 21:
        if(FVector::Dist2D(SmokeStart,P->GetActorLocation())>5){Finish(TEXT("Server zero velocity scale ignored"));return;}
        P->SetMotionParameter(TEXT("velocity"),1);break;
    case 22:
        if(FVector::Dist2D(SmokeStart,P->GetActorLocation())<60){Finish(TEXT("Server movement scale not restored"));return;}
        P->SetMotionParameter(TEXT("jump"),0);break;
    case 23:
        if(P->GetCharacterMovement()->JumpZVelocity!=0){Finish(TEXT("Server zero jump scale ignored"));return;}
        P->ResetMotionParameters();
        Sky->TimeScale=0;Sky->SetHour(8);break;
    case 24:
        if(!CheckEnvironment(1)){Finish(TEXT("Morning lighting differs from DAT sky"));return;}
        Capture(TEXT("AC2Controller-light-morning.png"));break;
    case 25: Sky->SetHour(16.8f);break;
    case 26:
        if(!CheckEnvironment(1)){Finish(TEXT("Zenith lighting differs from DAT sky"));return;}
        Capture(TEXT("AC2Controller-light-zenith.png"));break;
    case 27: Sky->SetHour(0);break;
    case 28:
        if(!CheckEnvironment(1)){Finish(TEXT("Night lighting differs from DAT sky"));return;}
        Capture(TEXT("AC2Controller-light-night.png"));break;
    case 29:
        UE_LOG(LogTemp,Display,TEXT("AC2 render: day/zenith/night light alignment and logout/reentry cleanup PASS"));
        Sky->SetHour(12);
        break;
    default:
        if(ControllerStage<42)
        {
            if(!H->GameUI->SmokeMovementInput(ControllerStage-30,Error)){Finish(Error);return;}
            break;
        }
        Finish(TEXT(""));return;
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 controller stage %d complete"),ControllerStage);
    ++ControllerStage;ControllerNext=ControllerTime+2;
}
