#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2GameUI.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerInput.h"

void AAC2PlayerController::ApplyGameplayKeys(const FAC2KeyBindings& NewKeys)
{
    if (!InputComponent || !PlayerInput) return;
    FlushPressedKeys();
    if (auto* Explorer = Cast<AAC2Explorer>(GetPawn())) Explorer->EndJump();
    InputComponent->KeyBindings.RemoveAt(FirstGameplayKey, NumGameplayKeys, EAllowShrinking::No);
    for (const auto& Old : Keys.Rows)
        if (!Old.Axis.IsNone() && Old.Chord.Key.IsValid())
            PlayerInput->RemoveAxisMapping({Old.Axis, Old.Chord.Key, Old.Direction});

    Keys = NewKeys;
    FirstGameplayKey = InputComponent->KeyBindings.Num();
    for (const auto& Binding : Keys.Rows)
    {
        if (!Binding.Chord.Key.IsValid()) continue;
        if (!Binding.Axis.IsNone())
        {
            PlayerInput->AddAxisMapping({Binding.Axis, Binding.Chord.Key, Binding.Direction});
            continue;
        }
        auto Install = [&](EInputEvent Event)
        {
            FInputKeyBinding Key(Binding.Chord, Event);
            // Use and swim-up share E. Allow the pawn's movement axis to see it too.
            Key.bConsumeInput = !Keys.Rows.ContainsByPredicate([&](const auto& Other)
            { return !Other.Axis.IsNone() && Other.Chord == Binding.Chord; });
            Key.KeyDelegate.GetDelegateForManualSet().BindLambda([this, Binding, Event]()
            {
                const auto& Chord = Binding.Chord;
                // Held movement works alongside other keys. For shortcuts and
                // chorded commands use retail's exact modifier layer.
                if (!Binding.Hold && !Chord.Key.IsModifierKey() && (Binding.Shortcut || Chord.HasAnyModifierKeys()) &&
                    (PlayerInput->IsShiftPressed() != bool(Chord.bShift) || PlayerInput->IsCtrlPressed() != bool(Chord.bCtrl) ||
                     PlayerInput->IsAltPressed() != bool(Chord.bAlt) || PlayerInput->IsCmdPressed() != bool(Chord.bCmd))) return;
                DispatchGameplayKey(Binding, Event == IE_Pressed);
            });
            InputComponent->KeyBindings.Add(MoveTemp(Key));
        };
        Install(IE_Pressed);
        if (Binding.Hold) Install(IE_Released);
    }
    NumGameplayKeys = InputComponent->KeyBindings.Num() - FirstGameplayKey;
}

bool AAC2PlayerController::WalkKeyDown() const
{
    for (const auto& Binding : Keys.Rows)
        if ((Binding.Id == TEXT("Walk") || Binding.Id == TEXT("WalkAlt")) &&
            Binding.Chord.Key.IsValid() && IsInputKeyDown(Binding.Chord.Key)) return true;
    return false;
}

void AAC2PlayerController::DispatchPointerKey(FKey Key, const FModifierKeysState& Mods, float Amount)
{
    const FInputChord Chord(Key, Mods.IsShiftDown(), Mods.IsControlDown(), Mods.IsAltDown(), Mods.IsCommandDown());
    for (const auto& Binding : Keys.Rows)
        if (Binding.Chord == Chord) { DispatchGameplayKey(Binding, true, Amount); return; }
    // Preserve plain wheel/autorun while walking, unless the modifier has its own binding.
    for (const auto& Binding : Keys.Rows)
        if (Binding.Chord == FInputChord(Key) && !Binding.Shortcut) { DispatchGameplayKey(Binding, true, Amount); return; }
}

void AAC2PlayerController::DispatchGameplayKey(const FAC2KeyBinding& Binding, bool Pressed, float Amount)
{
    if (Binding.Shortcut)
    {
        if (auto* H = Cast<AAC2HUD>(GetHUD()); H && H->GameUI) H->GameUI->HandleShortcutAction(Binding.Shortcut);
        return;
    }
    const FName Id = Binding.Id;
    if (Id == TEXT("Map")) Map();
    else if (Id == TEXT("Target")) Select();
    else if (Id == TEXT("Use")) UseTarget();
    else if (Id == TEXT("Combat")) CombatMode();
    else if (Id == TEXT("Recall")) AC2Recall();
    else if (Id == TEXT("Close")) CloseUI();
    else if (Id == TEXT("Inventory")) Inventory();
    else if (Id == TEXT("Character")) CharacterStats();
    else if (Id == TEXT("SelectCharacter")) CharacterSelect();
    else if (Id == TEXT("Chat")) Chat();
    else if (Id == TEXT("Cursor")) Cursor();
    else if (auto* Explorer = Cast<AAC2Explorer>(GetPawn()))
    {
        if (Id == TEXT("Jump"))
        {
            if (Pressed) Explorer->StartJump();
            else Explorer->EndJump();
        }
        else if (Id == TEXT("Walk") || Id == TEXT("WalkAlt")) Explorer->UpdateWalkModifier();
        else if (Id == TEXT("AutoRun")) Explorer->ToggleAutoRun();
        else if (Id == TEXT("Fly")) Explorer->ToggleFly();
        else if (Id == TEXT("View")) Explorer->ToggleView();
        else if (Id == TEXT("ZoomIn")) Explorer->ZoomCamera(Amount);
        else if (Id == TEXT("ZoomOut")) Explorer->ZoomCamera(-Amount);
    }
}
