#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void AAC2GameMode::MapSmokeTick(float Delta)
{
    MapTime += Delta;
    auto Finish = [&](bool OK, const FString &Error) {
        FFileHelper::SaveStringToFile(
            OK ? TEXT("PASS") : TEXT("FAIL: ") + Error,
            *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/AC2Map-smoke.txt")));
        UE_LOG(LogTemp, Display, TEXT("AC2 map smoke: %s %s"), OK ? TEXT("PASS") : TEXT("FAIL"), *Error);
        bMapSmoke = false;
        FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1);
    };
    if (MapTime > 150)
    {
        Finish(false, TEXT("Timed out"));
        return;
    }
    auto *PC = Cast<AAC2PlayerController>(GetWorld()->GetFirstPlayerController());
    auto *HUD = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    if (!CharacterUIStarted || !HUD || !HUD->GameUI || !HUD->CharacterUI || bWaiting || MapTime < MapNext)
        return;
    if (MapStage == 9 && (PendingCell || !bIndoors))
        return;
    auto Capture = [](const TCHAR *Name) {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Name), true, false);
    };
    FString Error;
    bool OK = true;
    switch (MapStage)
    {
    case 0:
        OK = HUD->CharacterUI->SmokePreview(0) && HUD->CharacterUI->SmokeCreate(Error);
        PC->Map();
        break;
    case 1:
        OK = HUD->GameUI->MapSmokeStep(0, Error);
        Capture(TEXT("AC2Map-world.png"));
        break;
    case 2:
        OK = HUD->GameUI->MapSmokeStep(1, Error);
        break;
    case 3:
        OK = HUD->GameUI->MapSmokeStep(2, Error);
        SpawnMob();
        break;
    case 4:
        OK = HUD->GameUI->MapSmokeStep(3, Error);
        Capture(TEXT("AC2Map-detail-radar.png"));
        break;
    case 5:
        OK = HUD->GameUI->MapSmokeStep(4, Error);
        break;
    case 6:
        OK = HUD->GameUI->MapSmokeStep(5, Error);
        Capture(TEXT("AC2Map-overview.png"));
        break;
    case 7:
        OK = HUD->GameUI->MapSmokeStep(6, Error);
        PC->ConsoleCommand(TEXT("r.SetRes 1280x720w"));
        break;
    case 8:
        OK = HUD->GameUI->MapSmokeStep(7, Error);
        LoadCell(0x59600110);
        break;
    case 9:
        OK = bIndoors && HUD->GameUI->MapSmokeStep(8, Error);
        Capture(TEXT("AC2Map-interior.png"));
        break;
    case 10:
        OK = HUD->GameUI->MapSmokeStep(9, Error);
        break;
    case 11:
        OK = HUD->GameUI->MapSmokeStep(10, Error);
        break;
    case 12:
        OK = HUD->GameUI->MapSmokeStep(11, Error);
        break;
    case 13:
        if (auto* Pawn = Cast<AAC2Explorer>(PC->GetPawn()))
            OK = !Pawn->IsHeld() && Pawn->GetCharacterMovement()->IsMovingOnGround() &&
                 FVector::DistSquared2D(Pawn->GetActorLocation(), FVector(1384500, 1657000, 0)) < FMath::Square(2000.);
        else OK = false;
        if (!OK) Error = TEXT("Map double-click failed to land near the original clicked point");
        Capture(TEXT("AC2Map-travel.png"));
        break;
    case 14:
        PC->CloseUI();
        OK = !bMap && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored();
        Finish(OK, OK ? TEXT("") : TEXT("Closing maps did not restore world input"));
        return;
    }
    if (!OK)
    {
        Finish(false, Error.IsEmpty() ? FString::Printf(TEXT("Stage %d failed"), MapStage) : Error);
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("AC2 map smoke stage %d complete"), MapStage);
    ++MapStage;
    MapNext = MapTime + (MapStage == 12 ? .1f : 2.f);
}
