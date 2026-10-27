#include "AC2GameMode.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2Explorer.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void AAC2GameMode::UISmokeTick(float Delta)
{
    UITime += Delta;
    auto Finish = [&](bool OK, const FString &Error) {
        FFileHelper::SaveStringToFile(
            OK ? TEXT("PASS") : TEXT("FAIL: ") + Error,
            *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/AC2UI-smoke.txt")));
        UE_LOG(LogTemp, Display, TEXT("AC2 character UI smoke: %s %s"), OK ? TEXT("PASS") : TEXT("FAIL"),
               *Error);
        bUISmoke = false;
        FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1);
    };
    if (UITime > 160)
    {
        Finish(false, TEXT("Timed out"));
        return;
    }
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *HUD = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    if (!CharacterUIStarted || !HUD || !HUD->CharacterUI || bWaiting)
        return;
    if (!CharacterDataReady)
    {
        Finish(false, CharacterError);
        return;
    }
    auto UI = HUD->CharacterUI;
    auto Capture = [](const TCHAR *Name) {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Name), true, false);
    };
    if (FParse::Param(FCommandLine::Get(), TEXT("AC2PortraitSmoke")))
    {
        if (UITime < (UIStage + 1) * .75f) return;
        if (UIStage >= CharacterCatalog.Avatars.Num() * 8)
        { Finish(true, TEXT("Every race/sex at both height/build extremes; body/head framing, rotation and grounded feet")); return; }
        if (!UIStage) UI->Open(SAC2CharacterUI::EScreen::Create);
        const int32 Avatar = UIStage / 8;
        const bool Tall = (UIStage % 8) >= 4, Head = (UIStage % 4) >= 2;
        FString Error;
        if (!(UIStage % 2))
        {
            if (!UI->SmokeFraming(Avatar, Tall, Head, Error))
            { Finish(false, FString::Printf(TEXT("Avatar %d: %s"), Avatar, *Error)); return; }
        }
        else
        {
            if (!UI->AuditPortrait(Error)) { Finish(false, Error); return; }
            Capture(*FString::Printf(TEXT("AC2Portrait-%02d-%s-%s.png"), Avatar, Tall ? TEXT("max") : TEXT("min"), Head ? TEXT("head") : TEXT("body")));
        }
        ++UIStage;
        return;
    }
    if (UITime < (UIStage + 1) * 3.f)
        return;
    if (UIStage < 10)
    {
        if (!UI->SmokePreview(UIStage))
        {
            Finish(false, FString::Printf(TEXT("Avatar %d failed"), UIStage));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 UI preview verified: %d"), UIStage);
    }
    else if (UIStage == 10)
    {
        FString Error;
        if (!UI->SmokeAppearanceAudit(Error))
        {
            Finish(false, Error);
            return;
        }
        UI->SmokeCreatorPage(1);
    }
    else if (UIStage == 11)
        Capture(TEXT("AC2UI-creation-body.png"));
    else if (UIStage == 12)
        UI->SmokeCreatorPage(0);
    else if (UIStage == 13)
        Capture(TEXT("AC2UI-creation-head.png"));
    else if (UIStage == 14)
        UI->SmokeCreatorPage(2);
    else if (UIStage == 15)
        Capture(TEXT("AC2UI-creation-clothing.png"));
    else if (UIStage == 16)
        UI->SmokeCreatorPage(2, true);
    else if (UIStage == 17)
        Capture(TEXT("AC2UI-creation-name.png"));
    else if (UIStage == 18)
    {
        FString Error;
        if (!UI->SmokeCreate(Error))
        {
            Finish(false, Error);
            return;
        }
        HUD->GameUI->ToggleInventory();
        if (!HUD->GameUI->SmokeEquip())
        {
            Finish(false, TEXT("Equipment interaction failed"));
            return;
        }
    }
    else if (UIStage == 19)
        Capture(TEXT("AC2UI-inventory.png"));
    else if (UIStage == 20)
        HUD->GameUI->ToggleStats();
    else if (UIStage == 21)
        Capture(TEXT("AC2UI-stats.png"));
    else if (UIStage == 22)
    {
        FAC2CharacterStore Reloaded;
        FString Error;
        if (!Reloaded.Load(CharacterSavePath, CharacterCatalog, Error) || !Reloaded.Active() ||
            Reloaded.Active()->Items[0].Equipped != Characters.Active()->Items[0].Equipped ||
            !Reloaded.Active()->Appearance.OrderIndependentCompareEqual(Characters.Active()->Appearance))
        {
            Finish(false, TEXT("Saved equipment or appearance did not reload"));
            return;
        }
        UI->Close();
        HUD->GameUI->ClosePanels();
        if (PC->IsMoveInputIgnored() || PC->bShowMouseCursor==Cast<AAC2PlayerController>(PC)->WantsMouseLook() ||
            !Cast<AAC2Explorer>(PC->GetPawn())->HasModel())
        {
            Finish(false, TEXT("World input or model not restored"));
            return;
        }
        UI->Open(SAC2CharacterUI::EScreen::Create);
        UI->SmokeCreatorPage(-1);
    }
    else if (UIStage == 23)
        Capture(TEXT("AC2UI-creation-lineup.png"));
    else if (UIStage == 24)
    {
        if (!UI->SmokeEyes(false))
        {
            Finish(false, TEXT("Eye FX preview failed"));
            return;
        }
    }
    else if (UIStage == 25)
        Capture(TEXT("AC2UI-empyrean-eyes-first.png"));
    else if (UIStage == 26)
        UI->SmokeEyes(true);
    else if (UIStage == 27)
        Capture(TEXT("AC2UI-empyrean-eyes-last.png"));
    else if (UIStage == 28)
    {
        UI->Close();
        HUD->GameUI->ClosePanels();
        FString Error;
        if (!HUD->GameUI || !HUD->GameUI->Smoke(Error))
        {
            Finish(false, Error);
            return;
        }
        Characters.Active()->Health = 73;
        Characters.Active()->Vigor = 42;
        SpawnMob();
        TargetNext();
    }
    else if (UIStage == 29)
        Capture(TEXT("AC2UI-world-hud.png"));
    else if (UIStage == 30)
        HUD->GameUI->SmokeDraft();
    else if (UIStage == 31)
    {
        if (!HUD->GameUI->HasChatFocus())
        {
            Finish(false, TEXT("Chat entry did not receive Slate keyboard focus"));
            return;
        }
        Capture(TEXT("AC2UI-world-chat.png"));
    }
    else if (UIStage == 32)
    {
        HUD->GameUI->ToggleInventory();
        if (HUD->GameUI->ChatActive() || PC->IsMoveInputIgnored() || !HUD->GameUI->InventoryOpen())
        {
            Finish(false, TEXT("Chat to inventory focus handoff failed"));
            return;
        }
    }
    else if (UIStage == 33)
        Capture(TEXT("AC2UI-world-inventory.png"));
    else if (UIStage == 34)
    {
        UI->Close();
        HUD->GameUI->ClosePanels();
        PC->ConsoleCommand(TEXT("r.SetRes 1280x720w"));
        HUD->GameUI->SmokeDraft();
    }
    else if (UIStage == 35)
        Capture(TEXT("AC2UI-world-chat-1280.png"));
    else if (UIStage == 36)
        HUD->GameUI->ToggleStats();
    else if (UIStage == 37)
        Capture(TEXT("AC2UI-world-stats-1280.png"));
    else if (UIStage == 38)
        UI->Open(SAC2CharacterUI::EScreen::Select);
    else if (UIStage == 39)
        Capture(TEXT("AC2UI-character-selection.png"));
    else if (UIStage == 40)
    {
        UI->Close();
        HUD->GameUI->ClosePanels();
        Characters.Active()->Health = Characters.Active()->Vigor = 100;
        if (PC->IsMoveInputIgnored() || PC->bShowMouseCursor==Cast<AAC2PlayerController>(PC)->WantsMouseLook())
        {
            Finish(false, TEXT("World input remained captured"));
            return;
        }
        Finish(true,
               TEXT("10 avatars and stable appearance camera; DAT fonts and selection labels; native HUD "
                    "and vitals; chat input/history/offline channels; inventory focus; persistence"));
    }
    ++UIStage;
}
