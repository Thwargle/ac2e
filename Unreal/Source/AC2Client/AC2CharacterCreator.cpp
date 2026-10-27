#include "AC2CharacterUI.h"
#include "AC2GameMode.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"

namespace
{
struct FPhysControl
{
    uint32 Type;
    const TCHAR *Name;
    const TCHAR *Fallback;
    int32 Tab;
};
// gmAvatarModificationUI::InitUITables / MakeCurrentMask, Legions 7160.
const FPhysControl Controls[] = {{2, TEXT("Height"), TEXT("Height"), 1},
                                 {1, TEXT("Build"), TEXT("Build"), 1},
                                 {4, TEXT("SkinTone"), TEXT("Skin tone"), 1},
                                 {8, TEXT("SkinDetail"), TEXT("Skin detail"), 1},
                                 {128, TEXT("Special"), TEXT("Special"), 1},
                                 {16, TEXT("HeadDetail"), TEXT("Head shape"), 0},
                                 {32, TEXT("HeadFrill"), TEXT("Hair style"), 0},
                                 {64, TEXT("FrillColor"), TEXT("Hair color"), 0},
                                 {2048, TEXT("FaceDetail"), TEXT("Face"), 0},
                                 {4096, TEXT("EyeColor"), TEXT("Eye color"), 0},
                                 {256, TEXT("ShirtClothingColor"), TEXT("Shirt color"), 2},
                                 {512, TEXT("PantsClothingColor"), TEXT("Pants color"), 2},
                                 {1024, TEXT("BootsClothingColor"), TEXT("Boots color"), 2}};
} // namespace

void SAC2CharacterUI::SetCreatorStep(int32 Step)
{
    CreationStep = FMath::Clamp(Step, 0, 2);
    Message.Empty();
    if (Portrait.IsValid())
        Portrait->SetCamera(CreationStep == 1 && HeadZoom, Rotation);
    if (Lineup.IsValid())
    {
        Lineup->SetActorTickEnabled(CreationStep == 0);
        Lineup->Capture->bCaptureEveryFrame = CreationStep == 0;
    }
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(CreationStep != 0);
        Portrait->Capture->bCaptureEveryFrame = CreationStep != 0;
    }
    Refresh();
    if (CreationStep == 2 && NameEntry)
        FSlateApplication::Get().SetKeyboardFocus(NameEntry);
}
void SAC2CharacterUI::SetChoice(uint32 Type, int32 Index)
{
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A)
        return;
    int32 Count = Type == 1 ? A->Builds.Num() : Type == 2 ? A->Heights.Num() : A->Choices.FindRef(Type).Num();
    if (!Count)
        return;
    int32 &Current = Type == 1 ? BuildIndex : Type == 2 ? HeightIndex : Appearance.FindOrAdd(Type);
    Index = FMath::Clamp(Index, 0, Count - 1);
    if (Current == Index)
        return;
    Current = Index;
    PreviewDraft();
}
void SAC2CharacterUI::Randomize(bool All)
{
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A)
        return;
    for (const auto &C : Controls)
        if (All || C.Tab == AppearanceTab)
        {
            if (C.Type == 1)
                BuildIndex = FMath::RandHelper(A->Builds.Num());
            else if (C.Type == 2)
                HeightIndex = FMath::RandHelper(A->Heights.Num());
            else if (auto *Values = A->Choices.Find(C.Type))
                Appearance.Add(C.Type, FMath::RandHelper(Values->Num()));
        }
    PreviewDraft();
    Refresh();
}
namespace
{
// Native tree owns artwork, text, hit boxes and scrollbar thumbs. The controller supplies actions.
class SAC2CreatorLayout : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2CreatorLayout)
    {
    }
    SLATE_ARGUMENT(FAC2NativeUI *, UI)
    SLATE_ARGUMENT(FAC2Fonts *, Fonts)
    SLATE_ARGUMENT(TFunction<void()>, Update)
    SLATE_ARGUMENT(TFunction<void(uint32)>, Click)
    SLATE_ARGUMENT(TFunction<void(uint32, float)>, Slide)
    SLATE_ARGUMENT(TFunction<void(FVector2D, bool)>, Avatar)
    SLATE_ARGUMENT(TFunction<void(float)>, Wheel)
    SLATE_ARGUMENT(
        TFunction<int32(const FAC2NativeNode &, FSlateWindowElementList &, int32, const FGeometry &)>, Scene)
    SLATE_END_ARGS()
    void Construct(const FArguments &A)
    {
        UI = A._UI;
        Fonts = A._Fonts;
        Update = A._Update;
        Click = A._Click;
        Slide = A._Slide;
        Avatar = A._Avatar;
        Wheel = A._Wheel;
        Scene = A._Scene;
    }
    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(1024, 768);
    }
    virtual void Tick(const FGeometry &G, double T, float D) override
    {
        SLeafWidget::Tick(G, T, D);
        Update();
    }
    virtual int32 OnPaint(const FPaintArgs &, const FGeometry &G, const FSlateRect &,
                          FSlateWindowElementList &Out, int32 L, const FWidgetStyle &, bool) const override
    {
        return UI->Paint(Out, L, G, *Fonts,
                         [&](const FAC2NativeNode &N, int32 Layer) { return Scene(N, Out, Layer, G); });
    }
    virtual FReply OnMouseMove(const FGeometry &G, const FPointerEvent &E) override
    {
        FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
        UI->Hovered = 0;
        auto Order = UI->PaintOrder();
        for (int32 I = Order.Num() - 1; I >= 0; --I)
        {
            const auto &N = UI->Nodes[Order[I]];
            if (N.Kind == FAC2UILayout::Token(TEXT("button")) &&
                N.State != FAC2UILayout::Token(TEXT("ghosted")) && N.Clip.Contains(FIntPoint(P.X, P.Y)))
            {
                UI->Hovered = N.Id;
                break;
            }
        }
        if (Dragging)
        {
            MoveSlider(P);
            return FReply::Handled();
        }
        Avatar(P, false);
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonDown(const FGeometry &G, const FPointerEvent &E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton)
            return FReply::Unhandled();
        OnMouseMove(G, E);
        FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
        UI->Pressed = UI->Hovered;
        if (UI->Pressed)
            return FReply::Handled().CaptureMouse(SharedThis(this));
        for (const auto &N : UI->Nodes)
            if (N.Kind == FAC2UILayout::Token(TEXT("scrollbar")) && N.Clip.Contains(FIntPoint(P.X, P.Y)))
            {
                Dragging = N.Id;
                MoveSlider(P);
                return FReply::Handled().CaptureMouse(SharedThis(this));
            }
        Avatar(P, true);
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry &G, const FPointerEvent &E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton)
            return FReply::Unhandled();
        uint32 Id = UI->Pressed;
        UI->Pressed = 0;
        Dragging = 0;
        if (Id && Id == UI->Hovered)
            Click(Id);
        return FReply::Handled().ReleaseMouseCapture();
    }

  private:
    FAC2NativeUI *UI = nullptr;
    FAC2Fonts *Fonts = nullptr;
    uint32 Dragging = 0;
    TFunction<void()> Update;
    TFunction<void(uint32)> Click;
    TFunction<void(uint32, float)> Slide;
    TFunction<void(FVector2D, bool)> Avatar;
    TFunction<void(float)> Wheel;
    virtual FReply OnMouseWheel(const FGeometry &, const FPointerEvent &E) override
    {
        Wheel(E.GetWheelDelta());
        return FReply::Handled();
    }
    TFunction<int32(const FAC2NativeNode &, FSlateWindowElementList &, int32, const FGeometry &)> Scene;
    void MoveSlider(FVector2D P)
    {
        if (const auto *N = UI->Find(Dragging))
        {
            const auto *W = UI->Descendant(*N, TEXT("widget"));
            const float Thumb = W ? W->Rect.Width() : 0;
            Slide(Dragging, FMath::Clamp(float(P.X - N->Rect.Min.X - Thumb / 2) /
                                             FMath::Max(1.f, N->Rect.Width() - Thumb),
                                         0.f, 1.f));
        }
    }
};
} // namespace
void SAC2CharacterUI::BuildCreatorLayout()
{
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A)
        return;
    auto T = [](const TCHAR *Name) { return FAC2UILayout::Token(Name); };
    CreatorLayout.Begin(FIntPoint(1024, 768));
    CreatorLayout.Text.Reset();
    CreatorLayout.Visibility.Reset();
    CreatorLayout.States.Reset();
    if (Screen == EScreen::Select)
    {
        CreatorLayout.Root(0x22000002, TEXT("Viewport"));
        for (const TCHAR *Name :
             {TEXT("DeleteButton"), TEXT("RestoreButton"), TEXT("OptionsButton"), TEXT("CreditsButton")})
            CreatorLayout.States.Add(T(Name), T(TEXT("ghosted")));
        CreatorLayout.States.Add(T(TEXT("CreateButton")),
                                 T(Mode->Characters.Characters.Num() < 8 ? TEXT("normal") : TEXT("ghosted")));
        CreatorLayout.States.Add(T(TEXT("EnterButton")),
                                 T(Mode->Characters.Active() ? TEXT("normal") : TEXT("ghosted")));
        CreatorLayout.Text.Add(T(TEXT("ErrorMsg")), Message);
        CreatorLayout.Root(0x22000002, TEXT("BackgroundField"));
        CreatorLayout.Root(0x22000002, TEXT("EnterButtonField"));
        FIntRect List = CreatorLayout.Bounds(TEXT("CharacterListBox"));
        for (int32 I = Page; I < FMath::Min(Page + 5, Mode->Characters.Characters.Num()); ++I)
        {
            const auto &P = Mode->Characters.Characters[I];
            CreatorLayout.Text.Add(T(TEXT("list_box_item")), P.Name);
            CreatorLayout.States.Add(
                T(TEXT("list_box_item")),
                T(P.Id == Mode->Characters.ActiveId ? TEXT("highlight") : TEXT("normal")));
            FIntPoint Origin = List.Min + FIntPoint(0, (I - Page) * 20);
            CreatorLayout.Template(0x22000002, TEXT("list_box_item"),
                                   FIntRect(Origin, Origin + FIntPoint(List.Width(), 20)), List);
        }
    }
    else if (CreationStep == 0)
    {
        uint32 State = T(TEXT("stateless"));
        if (Mode->CharacterCatalog.Avatars.IsValidIndex(HoveredAvatar))
        {
            const auto &Choice = Mode->CharacterCatalog.Avatars[HoveredAvatar];
            State = FAC2UILayout::Token(FAC2CharacterCatalog::RaceName(Choice.Race).ToLower() +
                                        (Choice.Sex == 4096 ? TEXT("male") : TEXT("female")));
        }
        CreatorLayout.States.Add(T(TEXT("RaceDescField")), State);
        CreatorLayout.Root(0x22000006, TEXT("AvatarSelectionViewport"));
        CreatorLayout.Root(0x22000006, TEXT("AvatarSelectionFieldBottom"));
        CreatorLayout.Root(0x22000006, TEXT("AvatarSelectionFieldTop"));
    }
    else
    {
        const TCHAR *Parts[] = {TEXT("Head"), TEXT("Body"), TEXT("Clothing")};
        for (int32 I = 0; I < 3; ++I)
        {
            CreatorLayout.Visibility.Add(T(*(FString(Parts[I]) + TEXT("Field"))), I == AppearanceTab);
            CreatorLayout.States.Add(
                T(*(FString(Parts[I]) + TEXT("Button"))),
                T(I == AppearanceTab || CreationStep == 2 ? TEXT("ghosted") : TEXT("normal")));
        }
        for (const auto &C : Controls)
        {
            const int32 Count = C.Type == 1   ? A->Builds.Num()
                                : C.Type == 2 ? A->Heights.Num()
                                              : A->Choices.FindRef(C.Type).Num();
            FString Name = C.Name;
            CreatorLayout.Visibility.Add(T(*(Name + TEXT("Field"))), Count > 0 && C.Tab == AppearanceTab);
            CreatorLayout.Text.Add(T(*(Name + TEXT("Label"))),
                                   A->Labels.Contains(C.Type) ? A->Labels[C.Type] : C.Fallback);
            const int32 Value = C.Type == 1   ? BuildIndex
                                : C.Type == 2 ? HeightIndex
                                              : Appearance.FindRef(C.Type);
            CreatorLayout.Positions.Add(T(*(Name + TEXT("Slider"))),
                                        Count > 1 ? float(Value) / (Count - 1) : 0);
            if (CreationStep == 2 || Count <= 1)
            {
                CreatorLayout.States.Add(T(*(Name + TEXT("LeftButton"))), T(TEXT("ghosted")));
                CreatorLayout.States.Add(T(*(Name + TEXT("RightButton"))), T(TEXT("ghosted")));
            }
        }
        CreatorLayout.Positions.Add(T(TEXT("RotationSlider")), (Rotation + 180) / 360);
        CreatorLayout.Root(0x22000007, *(FAC2CharacterCatalog::RaceName(Race).ToLower() + TEXT("Viewport")));
        CreatorLayout.Root(0x22000007, TEXT("AvatarModificationField"));
        if (CreationStep == 2)
        {
            CreatorLayout.States.Add(T(TEXT("ButtonField")), T(TEXT("nonadminclient")));
            CreatorLayout.Text.Add(T(TEXT("NameEntry")), TEXT(""));
            CreatorLayout.Text.Add(T(TEXT("GenerationConfirmationErrorMsg")), Message);
            CreatorLayout.Root(0x22000008, TEXT("GenerationConfirmationField"));
        }
    }
    CreatorLayout.Prepare(*Assets, Fonts);
}
void SAC2CharacterUI::CreatorSlider(uint32 Id, float Value)
{
    if (Screen != EScreen::Create || CreationStep != 1)
        return;
    if (Id == FAC2UILayout::Token(TEXT("RotationSlider")))
    {
        Rotation = Value * 360 - 180;
        if (Portrait.IsValid())
            Portrait->SetCamera(HeadZoom, Rotation);
        return;
    }
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A)
        return;
    for (const auto &C : Controls)
        if (Id == FAC2UILayout::Token(FString(C.Name) + TEXT("Slider")))
        {
            int32 Count = C.Type == 1   ? A->Builds.Num()
                          : C.Type == 2 ? A->Heights.Num()
                                        : A->Choices.FindRef(C.Type).Num();
            SetChoice(C.Type, FMath::RoundToInt(Value * FMath::Max(0, Count - 1)));
            return;
        }
}
void SAC2CharacterUI::CreatorAction(uint32 Id)
{
    auto Is = [Id](const TCHAR *Name) { return Id == FAC2UILayout::Token(Name); };
    if (Screen == EScreen::Select)
    {
        if (Is(TEXT("CreateButton")) && Mode->Characters.Characters.Num() < 8)
        {
            Open(EScreen::Create);
            return;
        }
        if (Is(TEXT("EnterButton")))
        {
            Enter();
            return;
        }
        if (Id == 0x69fd2076)
        {
            Page = FMath::Max(0, Page - 1);
            return;
        }
        if (Id == 0x260afc72)
        {
            Page = FMath::Min(FMath::Max(0, Mode->Characters.Characters.Num() - 5), Page + 1);
            return;
        }
        if (Is(TEXT("QuitButton")))
        {
            Mode->GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
            return;
        }
        return;
    }
    if (CreationStep == 2)
    {
        if (Is(TEXT("CancelButton")))
            SetCreatorStep(1);
        if (Is(TEXT("EnterButton")))
            Create();
        return;
    }
    if (Is(TEXT("CancelButton")))
    {
        Open(EScreen::Select);
        return;
    }
    if (Is(TEXT("BackButton")))
    {
        SetCreatorStep(0);
        return;
    }
    if (Is(TEXT("NextButton")))
    {
        if (PreviewReady())
            SetCreatorStep(2);
        return;
    }
    if (Is(TEXT("RandomizeCurrentButton")))
    {
        Randomize(false);
        return;
    }
    if (Is(TEXT("RandomizeAllButton")))
    {
        Randomize(true);
        return;
    }
    if (Is(TEXT("ZoomButton")))
    {
        HeadZoom = !HeadZoom;
        if (Portrait.IsValid())
            Portrait->SetCamera(HeadZoom, Rotation);
        return;
    }
    const TCHAR *Parts[] = {TEXT("HeadButton"), TEXT("BodyButton"), TEXT("ClothingButton")};
    for (int32 I = 0; I < 3; ++I)
        if (Is(Parts[I]))
        {
            AppearanceTab = I;
            HeadZoom = I == 0;
            if (Portrait.IsValid())
                Portrait->SetCamera(HeadZoom, Rotation);
            return;
        }
    for (const auto &C : Controls)
    {
        FString Name = C.Name;
        const int32 Value = C.Type == 1 ? BuildIndex : C.Type == 2 ? HeightIndex : Appearance.FindRef(C.Type);
        if (Is(*(Name + TEXT("LeftButton"))))
        {
            SetChoice(C.Type, Value - 1);
            return;
        }
        if (Is(*(Name + TEXT("RightButton"))))
        {
            SetChoice(C.Type, Value + 1);
            return;
        }
    }
}
TSharedRef<SWidget> SAC2CharacterUI::Creation()
{
    if (Screen == EScreen::Create && CreationStep == 0 && !Lineup.IsValid())
    {
        Lineup = Mode->GetWorld()->SpawnActor<AAC2Portrait>(FVector(0, 0, -600000), FRotator::ZeroRotator);
        if (Lineup.IsValid() && Lineup->ShowLineup(Mode->DatDirectory, Mode->CharacterCatalog))
        {
            LineupBrush.SetResourceObject(Lineup->Image);
            LineupBrush.ImageSize = FVector2D(1024, 619);
            LineupBrush.DrawAs = ESlateBrushDrawType::Image;
        }
    }
    BuildCreatorLayout();
    auto View =
        SNew(SAC2CreatorLayout)
            .UI(&CreatorLayout)
            .Fonts(&Fonts)
            .Update([this]() { BuildCreatorLayout(); })
            .Click([this](uint32 Id) { CreatorAction(Id); })
            .Slide([this](uint32 Id, float V) { CreatorSlider(Id, V); })
            .Wheel([this](float V) {
                if (Screen == EScreen::Select)
                    Page = FMath::Clamp(Page - FMath::RoundToInt(V), 0,
                                        FMath::Max(0, Mode->Characters.Characters.Num() - 5));
            })
            .Avatar([this](FVector2D P, bool Click) {
                if (Screen == EScreen::Select)
                {
                    if (Click)
                    {
                        FIntRect R = CreatorLayout.Bounds(TEXT("CharacterListBox"));
                        int32 Index = Page + int32((P.Y - R.Min.Y) / 20);
                        if (R.Contains(FIntPoint(P.X, P.Y)) &&
                            Mode->Characters.Characters.IsValidIndex(Index))
                        {
                            Mode->Characters.ActiveId = Mode->Characters.Characters[Index].Id;
                            Open(EScreen::Select);
                        }
                    }
                    return;
                }
                if (CreationStep != 0 || !Lineup.IsValid())
                    return;
                FIntRect R = CreatorLayout.Bounds(TEXT("AvatarSelectionViewport"));
                HoveredAvatar = R.Contains(FIntPoint(P.X, P.Y))
                                    ? Lineup->PickAvatar((P - FVector2D(R.Min)) / FVector2D(R.Size()))
                                    : INDEX_NONE;
                if (Click && Mode->CharacterCatalog.Avatars.IsValidIndex(HoveredAvatar))
                {
                    const auto &A = Mode->CharacterCatalog.Avatars[HoveredAvatar];
                    Race = A.Race;
                    Sex = A.Sex;
                    ResetAppearance();
                    PreviewDraft();
                    SetCreatorStep(1);
                }
            })
            .Scene([this](const FAC2NativeNode &N, FSlateWindowElementList &Out, int32 Layer,
                          const FGeometry &G) {
                const FString ViewportName =
                    Screen == EScreen::Select ? TEXT("Viewport")
                    : CreationStep == 0 ? TEXT("AvatarSelectionViewport")
                                        : FAC2CharacterCatalog::RaceName(Race).ToLower() + TEXT("Viewport");
                if (N.Id != FAC2UILayout::Token(ViewportName))
                    return Layer;
                FIntRect R = N.Rect;
                const FSlateBrush *B =
                    Screen == EScreen::Create && CreationStep == 0 ? &LineupBrush : &PortraitBrush;
                if (R.Width() > 0 && B->GetResourceObject())
                    FSlateDrawElement::MakeBox(
                        Out, ++Layer,
                        G.ToPaintGeometry(FVector2D(R.Size()), FSlateLayoutTransform(FVector2D(R.Min))), B);
                return Layer;
            });
    auto Overlay = SNew(SOverlay);
    Overlay->AddSlot()[View];
    if (Screen == EScreen::Create && CreationStep == 2)
    {
        auto EntryCanvas = SNew(SCanvas).Visibility(EVisibility::SelfHitTestInvisible);
        FIntRect R = CreatorLayout.Bounds(TEXT("NameEntry"));
        const auto *N = CreatorLayout.Find(TEXT("NameEntry"));
        EntryCanvas->AddSlot()
            .Position(FVector2D(R.Min))
            .Size(FVector2D(
                R.Size()))[SAssignNew(NameEntry, SAC2TextEntry)
                               .Text(DraftName)
                               .Font(Fonts.Get(N ? N->Attr(TEXT("font"), 0x42000024) : 0x42000024))
                               .OnTextChanged_Lambda([this](const FText &V) { DraftName = V.ToString(); })
                               .OnEnter_Lambda([this]() { Create(); })
                               .OnEscape_Lambda([this]() { SetCreatorStep(1); })];
        Overlay->AddSlot()[EntryCanvas];
    }
    return Overlay;
}

bool SAC2CharacterUI::SmokeAppearance(FString &Error)
{
    Race = 1;
    Sex = 4096;
    ResetAppearance();
    PreviewDraft();
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    for (const auto &C : Controls)
    {
        if (C.Type <= 2)
            continue;
        const auto *Choices = A->Choices.Find(C.Type);
        if (!Choices || Choices->Num() < 2)
            continue;
        const uint32 Before = Portrait->Fingerprint();
        SetChoice(C.Type, Choices->Num() - 1);
        if (!PreviewReady() || Before == Portrait->Fingerprint())
        {
            Error = FString::Printf(TEXT("Appearance did not change rendered assets: %s"), C.Name);
            return false;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 creator appearance verified: %s %08X -> %08X"), C.Name, Before,
               Portrait->Fingerprint());
    }
    return true;
}
void SAC2CharacterUI::SmokeCreatorPage(int32 Tab, bool Confirm)
{
    if (Tab < 0)
        HoveredAvatar = 0;
    if (Confirm)
        DraftName = TEXT("UI Adventurer");
    CreationStep = Tab < 0 ? 0 : Confirm ? 2 : 1;
    AppearanceTab = FMath::Max(0, Tab);
    HeadZoom = Tab == 0;
    if (Portrait.IsValid())
    {
        Portrait->SetCamera(!Confirm && HeadZoom, Rotation);
        Portrait->SetActorTickEnabled(CreationStep != 0);
        Portrait->Capture->bCaptureEveryFrame = CreationStep != 0;
    }
    if (Lineup.IsValid())
    {
        Lineup->SetActorTickEnabled(CreationStep == 0);
        Lineup->Capture->bCaptureEveryFrame = CreationStep == 0;
    }
    Refresh();
}

bool SAC2CharacterUI::SmokeEyes(bool Last)
{
    Race = 16;
    Sex = 4096;
    ResetAppearance();
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    Appearance[4096] = Last ? A->Choices[4096].Num() - 1 : 0;
    PreviewDraft();
    SmokeCreatorPage(0);
    return PreviewReady();
}
