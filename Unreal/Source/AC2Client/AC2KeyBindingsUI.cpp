#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class SAC2KeyBindingEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAC2KeyBindingEditor) {}
        SLATE_ARGUMENT(AAC2PlayerController*, Controller)
        SLATE_EVENT(FSimpleDelegate, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Controller = Args._Controller;
        Draft = Args._Controller->GameplayKeys();
        OnClose = Args._OnClose;
        const FLinearColor Gold(.86f, .73f, .47f);
        SAssignNew(List, SScrollBox);
        for (int32 I = 0; I < Draft.Rows.Num(); ++I)
        {
            const auto& Row = Draft.Rows[I];
            List->AddSlot().Padding(0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Row.Label)) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SBox).WidthOverride(150)
                  [ SNew(SButton).Tag(Row.Id).HAlign(HAlign_Center).Text_Lambda([this, I]()
                    {
                        if (Active == I) return FText::FromString(TEXT("Press a key…"));
                        const auto& Chord = Draft.Rows[I].Chord;
                        if (!Chord.Key.IsValid()) return FText::FromString(TEXT("Unbound"));
                        return Chord.HasAnyModifierKeys() ? Chord.GetInputText() : Chord.Key.GetDisplayName();
                    })
                    .OnClicked_Lambda([this, I]()
                    {
                        Active = I;
                        Modifier = EKeys::Invalid;
                        Status = TEXT("Press a key or chord. Click Stop listening to cancel.");
                        List->SetVisibility(EVisibility::HitTestInvisible);
                        return FReply::Handled().SetUserFocus(AsShared());
                    }) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("Clear")))
                    .OnClicked_Lambda([this, I]()
                    {
                        Draft.Assign(I, {}, Status);
                        return FReply::Handled();
                    }) ]
            ];
        }
        const auto* White = FCoreStyle::Get().GetBrush("WhiteBrush");
        ChildSlot
        [
            SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0, 0, 0, .55f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBorder).Padding(15).BorderImage(White).BorderBackgroundColor(FLinearColor(.045f, .04f, .035f))
                [ SNew(SBox).WidthOverride(485).MaxDesiredHeight(540)
                  [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Key Bindings"))).ColorAndOpacity(Gold)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(
                        TEXT("Left drag orbits the camera. Right drag turns your character. Hold both buttons to run. Click to select, double-click to use, or right-click to examine."))) ]
                    + SVerticalBox::Slot().FillHeight(1)
                    [ List.ToSharedRef() ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 7)
                    [ SNew(STextBlock).ColorAndOpacity(Gold).AutoWrapText(true)
                        .Text_Lambda([this] { return FText::FromString(Status); }) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SHorizontalBox)
                      + SHorizontalBox::Slot().AutoWidth()
                      [ SNew(SButton).Text(FText::FromString(TEXT("Restore defaults")))
                        .OnClicked_Lambda([this] { Stop(); Draft.Reset(); return FReply::Handled(); }) ]
                      + SHorizontalBox::Slot().FillWidth(1).Padding(6, 0).HAlign(HAlign_Left)
                      [ SNew(SButton).Text(FText::FromString(TEXT("Stop listening")))
                        .Visibility_Lambda([this] { return Active == INDEX_NONE ? EVisibility::Collapsed : EVisibility::Visible; })
                        .OnClicked_Lambda([this] { Stop(); return FReply::Handled(); }) ]
                      + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
                      [ SNew(SButton).Tag(TEXT("KeyBindingsCancel")).Text(FText::FromString(TEXT("Cancel")))
                        .OnClicked_Lambda([this] { OnClose.ExecuteIfBound(); return FReply::Handled(); }) ]
                      + SHorizontalBox::Slot().AutoWidth()
                      [ SNew(SButton).Text(FText::FromString(TEXT("Save")))
                        .OnClicked_Lambda([this]
                        {
                            if (Controller.IsValid())
                            {
                                Controller->ApplyGameplayKeys(Draft);
                                Draft.Save(GGameUserSettingsIni);
                            }
                            OnClose.ExecuteIfBound();
                            return FReply::Handled();
                        }) ] ]
                  ] ]
            ]
        ];
    }

#if !UE_BUILD_SHIPPING
    bool SmokeCapture(FString& Error)
    {
        auto FindButton = [](const auto& Self, const TSharedRef<SWidget>& Widget, FName Name) -> TSharedPtr<SButton>
        {
            if (Widget->GetTag() == Name) return StaticCastSharedRef<SButton>(Widget);
            FChildren* Children = Widget->GetChildren();
            for (int32 I = 0; I < Children->Num(); ++I)
                if (auto Found = Self(Self, Children->GetChildAt(I), Name)) return Found;
            return {};
        };
        auto Button = FindButton(FindButton, AsShared(), TEXT("Jump"));
        const auto Cancel = FindButton(FindButton, AsShared(), TEXT("KeyBindingsCancel"));
        if (!Button || !Cancel) { Error = TEXT("Jump or Cancel button missing"); return false; }
        FKey Free;
        TArray<FKey> Candidates;
        EKeys::GetAllKeys(Candidates);
        for (const auto& Key : Candidates)
            if (Key.GetFName().ToString().Len() == 1 && !Draft.Rows.ContainsByPredicate([&](const auto& R){ return R.Chord.Key == Key; }))
            { Free = Key; break; }
        if (!Free.IsValid()) { Error = TEXT("No unused key for binding smoke"); return false; }
        Button->SimulateClick();
        const int32 I = Active;
        if (!Draft.Rows.IsValidIndex(I) || Draft.Rows[I].Id != TEXT("Jump"))
        { Error = TEXT("Clicking Jump did not begin listening"); return false; }
        OnPreviewKeyDown(GetCachedGeometry(), FKeyEvent(Free, FModifierKeysState(), 0, false, 0, 0));
        if (Active != INDEX_NONE || Draft.Rows[I].Chord.Key != Free || Controller->GameplayKeys().Rows[I].Chord.Key == Free)
        { Error = TEXT("Key capture did not update the draft independently"); return false; }
        Cancel->SimulateClick();
        return true;
    }
#endif

    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (Active == INDEX_NONE)
        {
            if (Event.GetKey() != EKeys::Escape) return FReply::Unhandled();
            OnClose.ExecuteIfBound();
            return FReply::Handled();
        }
        if (!Event.IsRepeat())
        {
            const FKey Key = Event.GetKey();
            Modifier = Key.IsModifierKey() ? Key : EKeys::Invalid;
            if (!Key.IsModifierKey()) Capture(Key, Event);
        }
        return FReply::Handled();
    }
    FReply OnKeyUp(const FGeometry&, const FKeyEvent& Event) override
    {
        if (Active != INDEX_NONE && Modifier.IsValid() && Event.GetKey() == Modifier)
            SetChord(FInputChord(Modifier));
        return FReply::Handled();
    }
    FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Active == INDEX_NONE || !FAC2KeyBindings::PointerKey(Event.GetEffectingButton())) return FReply::Unhandled();
        Capture(Event.GetEffectingButton(), Event);
        return FReply::Handled();
    }
    FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Active != INDEX_NONE && Event.GetWheelDelta() != 0)
            Capture(Event.GetWheelDelta() > 0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown, Event);
        return FReply::Handled();
    }
    FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

private:
    void Capture(FKey Key, const FInputEvent& Event)
    {
        Modifier = EKeys::Invalid;
        SetChord(FInputChord(Key, Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown(), Event.IsCommandDown()));
    }
    void SetChord(const FInputChord& Chord)
    {
        if (Draft.Assign(Active, Chord, Status)) Stop();
    }
    void Stop()
    {
        Active = INDEX_NONE;
        Modifier = EKeys::Invalid;
        Status.Reset();
        List->SetVisibility(EVisibility::Visible);
    }
    TWeakObjectPtr<AAC2PlayerController> Controller;
    FAC2KeyBindings Draft;
    FSimpleDelegate OnClose;
    TSharedPtr<SScrollBox> List;
    FString Status;
    FKey Modifier;
    int32 Active = INDEX_NONE;
};
}

void SAC2GameUI::OpenKeyBindings()
{
    if (KeyBindingEditor || !Mode.IsValid()) return;
    auto* World = Mode->GetWorld();
    auto* PC = Cast<AAC2PlayerController>(World->GetFirstPlayerController());
    auto* Viewport = World->GetGameViewport();
    if (!PC || !Viewport) return;
    EndChat();
    CancelCameraGesture();
    PC->FlushPressedKeys();
    PC->SetCursor(true);
    KeyBindingPreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
    KeyBindingViewport = Viewport;
    KeyBindingEditor = SNew(SAC2KeyBindingEditor).Controller(PC).OnClose(this, &SAC2GameUI::CloseKeyBindings);
    Viewport->AddViewportWidgetContent(KeyBindingEditor.ToSharedRef(), 50);
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(KeyBindingEditor);
    Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Input);
    FSlateApplication::Get().SetKeyboardFocus(KeyBindingEditor);
}

void SAC2GameUI::RemoveKeyBindings()
{
    if (KeyBindingViewport.IsValid() && KeyBindingEditor)
        KeyBindingViewport->RemoveViewportWidgetContent(KeyBindingEditor.ToSharedRef());
    KeyBindingViewport.Reset();
    KeyBindingEditor.Reset();
}

void SAC2GameUI::CloseKeyBindings()
{
    RemoveKeyBindings();
    if (Mode.IsValid())
        if (auto* PC = Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))
        {
            PC->FlushPressedKeys();
            PC->SetCursor(true);
        }
    auto& App = FSlateApplication::Get();
    const auto Focus = KeyBindingPreviousFocus.Pin();
    KeyBindingPreviousFocus.Reset();
    if (Focus && Focus->GetVisibility().IsVisible()) App.SetKeyboardFocus(Focus);
    else App.SetAllUserFocusToGameViewport();
}


bool SAC2GameUI::SmokeKeyBindingEditor(FString& Error)
{
#if !UE_BUILD_SHIPPING
    const auto* Button = Buttons.Find(TEXT("KeyBindingsButton"));
    if (!Button) { Error = TEXT("Core settings has no Key Bindings hit target"); return false; }
    const auto& Geometry = GetCachedGeometry();
    const FVector2D Point = Geometry.LocalToAbsolute(FVector2D((Button->Left + Button->Right) * .5, (Button->Top + Button->Bottom) * .5) * Scale);
    OnMouseButtonDown(Geometry, FPointerEvent(0, Point, Point, {EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    OnMouseButtonUp(Geometry, FPointerEvent(0, Point, Point, {}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    if (!KeyBindingEditor) { Error = TEXT("Clicking Key Bindings did not open the editor"); return false; }
    const auto Editor = StaticCastSharedPtr<SAC2KeyBindingEditor>(KeyBindingEditor);
    if (!Editor->SmokeCapture(Error)) return false;
    if (KeyBindingEditor) { Error = TEXT("Cancel did not close the key binding editor"); return false; }
#endif
    return true;
}
