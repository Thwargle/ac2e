#include "AC2GameUI.h"
#include "AC2CharacterUI.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Portrait.h"
#include "AC2WorldGrid.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCanvas.h"

namespace
{
uint32 Token(const TCHAR *Name)
{
    return FAC2UILayout::Token(Name);
}
AAC2PlayerController *Controller(const TWeakObjectPtr<AAC2GameMode> &Mode)
{
    return Mode.IsValid() ? Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController())
                          : nullptr;
}
bool Inside(const FSlateRect &R, FVector2D P)
{
    return P.X >= R.Left && P.X < R.Right && P.Y >= R.Top && P.Y < R.Bottom;
}
FSlateRect SlateRect(FIntRect R)
{
    return FSlateRect(R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
}
const TCHAR *SlotNames[] = {TEXT("Head"),          TEXT("Torso"),         TEXT("Back"),
                            TEXT("Legs"),          TEXT("Feet"),          TEXT("Hands"),
                            TEXT("PrimaryHand"),   TEXT("SecondaryHand"), TEXT("PrimaryRing"),
                            TEXT("SecondaryRing"), TEXT("Necklace")};
const TCHAR *PackNames[] = {TEXT("PackMainContainer"), TEXT("PackOneContainer"), TEXT("PackTwoContainer"),
                            TEXT("PackThreeContainer")};
const TCHAR *PackButtons[] = {TEXT("PackMainButton"), TEXT("PackOneButton"), TEXT("PackTwoButton"),
                              TEXT("PackThreeButton")};
} // namespace
void SAC2GameUI::Construct(const FArguments &Args)
{
    Mode = Args._Mode;
    Frontend=Args._Frontend; OnClose=Args._OnClose; ShowOptions=Frontend;
    Assets = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    Assets->Open(Mode->DatDirectory, Error);
    Fonts.Open(Mode->DatDirectory, Error);
    if (!Native.Open(Mode->DatDirectory, Error))
        UE_LOG(LogTemp, Error, TEXT("Native HUD layout: %s"), *Error);
    if (!MapUI.Open(Mode->DatDirectory, Error))
        UE_LOG(LogTemp, Error, TEXT("Native maps: %s"), *Error);
    BodyFont = ChatFont = Fonts.Get(0x4200000c);
    TitleFont = Fonts.Get(0x4200001d);
    ChildSlot
        [SNew(SCanvas) +
         SCanvas::Slot()
             .Position_Lambda([this]() { return FVector2D(Native.Bounds(TEXT("ChatEntry")).Min) * Scale; })
             .Size_Lambda([this]() {
                 return FVector2D(Native.Bounds(TEXT("ChatEntry")).Size());
             })[SAssignNew(Entry, SAC2TextEntry)
                    .Font(ChatFont)
                    .NameOnly(false)
                    .MaxCharacters(512)
                    .RenderTransformPivot(FVector2D::ZeroVector)
                    .RenderTransform_Lambda([this]() { return FSlateRenderTransform(Scale); })
                    .Visibility_Lambda(
                        [this]() { return Editing ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnEnter_Lambda([this]() {
                        Submit(Entry->Value());
                        Entry->SetText(TEXT(""));
                        EndChat();
                    })
                    .OnEscape_Lambda([this]() { EndChat(); })
                    .OnHistory_Lambda([this](int32 D) { Entry->SetText(Log.Recall(D, Entry->Value())); })]];
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() {
        if (!VisibleWorld())
            return EVisibility::Collapsed;
        auto *PC = Controller(Mode);
        return PC && PC->bShowMouseCursor ? EVisibility::Visible : EVisibility::HitTestInvisible;
    }));
    Log.Add(EAC2ChatType::System, TEXT("Welcome to Dereth. Enter opens local chat; /help lists commands."));
    Log.Add(EAC2ChatType::System, Mode->Network ? TEXT("Server chat: /say, /me, /tell Name, message. T: target; 1: attack; 2: use. /stop ends combat.") : TEXT("Offline world: messages stay in this session."));
    if (Frontend) RestoreOptions();
    Rebuild();
}
SAC2GameUI::~SAC2GameUI()
{
    if (Portrait.IsValid())
        Portrait->Destroy();
}
bool SAC2GameUI::VisibleWorld() const
{
    if (Frontend) return ShowOptions;
    auto *PC = Controller(Mode);
    auto *H = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    return H && Mode.IsValid() && Mode->InWorld() && (!H->CharacterUI || !H->CharacterUI->IsCreation());
}
FSlateRect SAC2GameUI::ChatRect() const
{
    return SlateRect(Native.Bounds(TEXT("ChatWindow1")));
}
void SAC2GameUI::Tick(const FGeometry &G, double Time, float Delta)
{
    SCompoundWidget::Tick(G, Time, Delta);
    // Native HUD placement is in pixels, anchored to the actual viewport, never a scaled mockup page.
    Scale = 1.f / FMath::Max(.1f, G.GetAccumulatedLayoutTransform().GetScale());
    Size = G.GetLocalSize() / Scale;
    if (Mode.IsValid() && !Mode->Notice.IsEmpty() && LastNotice != Mode->Notice)
    {
        LastNotice = Mode->Notice;
        Log.Add(LastNotice.StartsWith(TEXT("Hit:")) || LastNotice.Contains(TEXT("defeat"))
                    ? EAC2ChatType::Combat
                    : EAC2ChatType::System,
                LastNotice);
    }
    Rebuild();
}
void SAC2GameUI::Bind(const TCHAR *Control, FName Action)
{
    if (const auto *N = Native.Find(Control))
    {
        Buttons.Add(Control, SlateRect(N->Clip));
        ButtonActions.Add(Control, Action);
    }
}
void SAC2GameUI::Rebuild()
{
    if (!Mode.IsValid())
        return;
    Native.Begin(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
    Buttons.Reset();
    OptionSliders.Reset();
    ButtonActions.Reset();
    Icons.Reset();
    Hints.Reset();
    if (Frontend)
    {
        Native.Visibility.Add(Token(TEXT("OptionField")),true);
        Native.Visibility.Add(Token(TEXT("ExaminationField")),false);
        Native.Root(0x22000001,TEXT("LeftSidePanel"));
        BuildOptions();
        Native.Prepare(*Assets,Fonts);
        return;
    }
    auto Show = [&](const TCHAR *Name, bool Value) { Native.Visibility.Add(Token(Name), Value); };
    auto SetText = [&](const TCHAR *Name, FString Value) { Native.Text.Add(Token(Name), MoveTemp(Value)); };
    // gmToolbarUI creates these roots but keeps its mutually exclusive panels hidden until selected.
    for (const TCHAR *Name :
         {TEXT("NavigationField"), TEXT("OptionField"), TEXT("SocialField"), TEXT("QuestField"),
          TEXT("SkillField"), TEXT("DismountButton"), TEXT("ConsignmentsButton"), TEXT("OptionsField"),
          TEXT("FocusText"), TEXT("ExperienceText"), TEXT("PKText"), TEXT("PKVigorlossText"),
          TEXT("TimeLeftMeter")})
        Show(Name, false);
    Native.Visibility.Add(0x2b1d8dd8, false);
    Native.Visibility.Add(0xc4ce9c47, false);
    for (auto Name : SlotNames)
        Show(*(FString(Name) + TEXT("_Durability")), false);
    Show(TEXT("InventoryField"), ShowInventory);
    Show(TEXT("ExaminationField"), ShowStats);
    for (int32 I = 0; I < 4; ++I)
    {
        Show(PackNames[I], I == Pack);
        Native.States.Add(Token(PackButtons[I]), Token(I == Pack ? TEXT("normal_pressed") : TEXT("normal")));
    }
    Show(TEXT("HealthText"), HealthNumbers);
    Show(TEXT("VigorText"), VigorNumbers);
    const auto *P = Mode->Characters.Active();
    const float MaxHealth=Mode->Network ? Mode->Network->MaxHealth : 100, MaxVigor=Mode->Network ? Mode->Network->MaxVigor : 100;
    SetText(TEXT("HealthText"), FString::Printf(TEXT("%.0f / %.0f"), P ? P->Health : 0,MaxHealth));
    SetText(TEXT("VigorText"), FString::Printf(TEXT("%.0f / %.0f"), P ? P->Vigor : 0,MaxVigor));
    SetText(TEXT("AmountLabel"), Mode->Network ? FString::FromInt(Mode->Network->Gold) : TEXT("0"));
    SetText(TEXT("SetNumberDisplay"), TEXT("1"));
    SetText(TEXT("ChatEntry"), Editing ? TEXT("") : TEXT(""));
    Native.Meters.Add(Token(TEXT("HealthMeter")), P && MaxHealth>0 ? P->Health / MaxHealth : 0);
    Native.Meters.Add(Token(TEXT("VigorMeter")), P && MaxVigor>0 ? P->Vigor / MaxVigor : 0);
    Native.Meters.Add(Token(TEXT("PKMeter")), 1);
    Native.Meters.Add(Token(TEXT("PKVigorlossMeter")), 1);
    Native.Meters.Add(Token(TEXT("FocusMeter")), 1);
    Native.Meters.Add(Token(TEXT("ExperienceMeter")), 0);
    Native.States.Add(Token(TEXT("FocusMeter")), Token(TEXT("not_hero")));
    Native.Root(0x2200002b, TEXT("StatusbarField"));
    Native.Root(0x22000001, TEXT("ToolbarField"));
    Native.Root(0x22000001, TEXT("ShortcutField"));
    Native.Root(0x22000001, TEXT("ChatWindow1"));
    // Preserve the DAT window size, but keep its entry accessible on shorter displays.
    Native.ConstrainRoot(TEXT("ChatWindow1"), FIntRect(0, 0, FMath::RoundToInt(Size.X),
                                                       Native.Bounds(TEXT("ToolbarField")).Min.Y));
    Show(TEXT("OptionField"),ShowOptions);
    if(ShowOptions)Show(TEXT("ExaminationField"),false);
    if (ShowStats || ShowOptions)
        Native.Root(0x22000001, TEXT("LeftSidePanel"));
    if (ShowInventory)
        Native.Root(0x22000001, TEXT("RightSidePanel"));
    Bind(TEXT("InventoryButton"), TEXT("Inventory"));
    Bind(TEXT("InventoryCloseButton"), TEXT("Inventory"));
    Bind(TEXT("ExaminationButton"), TEXT("Stats"));
    Bind(TEXT("ExaminationCloseButton"), TEXT("Stats"));
    Bind(TEXT("NavigationButton"), TEXT("Map"));
    Bind(TEXT("OptionButton"), TEXT("Options"));
    Bind(TEXT("StartAttackButton"), TEXT("Strike"));
    Bind(TEXT("StopAttackButton"), TEXT("StopAttack"));
    Bind(TEXT("SkillButton"), TEXT("Skills"));
    Bind(TEXT("SocialButton"), TEXT("Social"));
    Bind(TEXT("QuestButton"), TEXT("Quests"));
    Bind(TEXT("HealthField"), TEXT("Health"));
    Bind(TEXT("VigorField"), TEXT("Vigor"));
    Bind(TEXT("ChatEntry"), TEXT("ChatEntry"));
    if(ShowOptions)BuildOptions();
    if (const auto *N = Native.Find(0x518fb9c2))
        Buttons.Add(TEXT("Stats"), SlateRect(N->Clip));
    if (const auto *N = Native.Find(0xe0ff9433))
        Buttons.Add(TEXT("ChatUp"), SlateRect(N->Clip));
    if (const auto *N = Native.Find(0xf94b392b))
        Buttons.Add(TEXT("ChatDown"), SlateRect(N->Clip));
    FIntRect Shortcuts = Native.Bounds(TEXT("ShortcutListBox"));
    Native.States.Add(Token(TEXT("ShortcutTemplate")), Token(TEXT("normal")));
    // Empty shortcut numbers are already part of the native background. Instantiate occupied slots only.
    for (int32 I = 0; I < 2 && Shortcuts.Width() > 0; ++I)
    {
        SetText(TEXT("NumberDisplay"), FString::FromInt((I + 1) % 10));
        FIntRect R(Shortcuts.Min + FIntPoint(I * 32, 0), Shortcuts.Min + FIntPoint((I + 1) * 32, 32));
        const int32 First = Native.Nodes.Num();
        Native.Template(0x22000001, TEXT("ShortcutTemplate"), R, Shortcuts);
        for (int32 J = First; J < Native.Nodes.Num(); ++J)
        {
            if (Native.Nodes[J].Id == Token(TEXT("ShortcutIcon")))
            {
                FAC2UIArt Art;
                Art.Image = I == 0 ? 0x41000ceb : 0x410002e0;
                Native.Nodes[J].Media.Add(Art);
            }
        }
        Buttons.Add(I == 0 ? TEXT("Strike") : TEXT("Cast"), SlateRect(R));
    }
    if (ShowInventory && P)
    {
        Native.Root(0x22000001, TEXT("PaperDollViewport"));
        const FIntRect List = Native.Bounds(TEXT("ContainerListBox"));
        for (int32 C = 0; C < 24; ++C)
        {
            const int32 Cell = Pack * 24 + C;
            const int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return !I.Equipped && I.Cell == Cell; });
            const FIntPoint XY = List.Min + FIntPoint((C % 6) * 36, (C / 6 - PackRow) * 36);
            FIntRect R(XY, XY + FIntPoint(36, 36));
            if (R.Max.Y <= List.Min.Y || R.Min.Y >= List.Max.Y)
                continue;
            Native.Template(0x22000001, TEXT("ItemBackground"), R, List);
            FName Key(*FString::Printf(TEXT("Cell%d"), Cell));
            Buttons.Add(Key, SlateRect(R));
            if (Index != INDEX_NONE)
            {
                const auto &D = Mode->CharacterCatalog.Items[P->Items[Index].Definition];
                Icons.Add({FIntRect(XY + FIntPoint(2, 2), XY + FIntPoint(34, 34)), List, D.Icon});
                Hints.Add(Key, D.Name);
            }
        }
        for (int32 I = 0; I < 4; ++I)
            Bind(PackButtons[I], FName(*FString::Printf(TEXT("Pack%d"), I)));
        for (int32 Bit = 0; Bit < 11; ++Bit)
        {
            const auto *Slot = Native.Find(*(FString(SlotNames[Bit]) + TEXT("_Slot")));
            if (!Slot)
                continue;
            const int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return I.Equipped == (1u << Bit); });
            if (Index != INDEX_NONE)
            {
                const auto &D = Mode->CharacterCatalog.Items[P->Items[Index].Definition];
                if (const auto *Icon = Native.Descendant(*Slot, TEXT("ItemIcon")))
                    Icons.Add({Icon->Rect, Icon->Clip, D.Icon});
                FName Key(*FString::Printf(TEXT("Item%d"), Index));
                Buttons.Add(Key, SlateRect(Slot->Clip));
                Hints.Add(Key, D.Name);
            }
        }
    }
    if (ShowStats && P)
    {
        const FIntRect List = Native.Bounds(TEXT("ExaminationListBox"));
        TArray<FString> Lines;
        if (P->Items.IsValidIndex(SelectedItem))
        {
            const auto &Item = P->Items[SelectedItem];
            const auto &D = Mode->CharacterCatalog.Items[Item.Definition];
            Lines.Add(D.Name);
            Lines.Add(D.Description);
            Lines.Add(FAC2CharacterCatalog::SlotName(D.Preferred));
            if (D.HasArmor)
                Lines.Add(FString::Printf(TEXT("Armor: %d"), D.Armor));
            if (D.HasDamage)
                Lines.Add(FString::Printf(TEXT("Damage: %d"), D.Damage));
            Lines.Add(Item.Equipped ? TEXT("Equipped") : TEXT("In backpack"));
            Lines.Add(TEXT("Double-click the item to equip or remove it."));
        }
        else
        {
            Lines = {P->Name,
                     FAC2CharacterCatalog::RaceName(P->Race),
                     FString::Printf(TEXT("Level: %d"), P->Level),
                     FString::Printf(TEXT("Experience: %d"), P->Experience),
                     FString::Printf(TEXT("Health: %.0f / %.0f"), P->Health,MaxHealth),
                     FString::Printf(TEXT("Vigor: %.0f / %.0f"), P->Vigor,MaxVigor),
                     FString::Printf(TEXT("Known armor: %d"), P->Armor(Mode->CharacterCatalog)),
                     Mode->Network ? TEXT("Server character profile") : TEXT("Offline character profile")};
        }
        int32 Y = List.Min.Y;
        for (const FString &Line : Lines)
        {
            const int32 Start = Native.Nodes.Num();
            FIntRect R(List.Min.X, Y, List.Max.X, Y + 20);
            Native.Template(0x22000001, TEXT("ExaminationTextTemplate"), R, List);
            if (Native.Nodes.IsValidIndex(Start))
            {
                auto &N = Native.Nodes[Start];
                N.Text = Line;
                auto F = Fonts.Get(N.Attr(TEXT("font"), 0x4200000c));
                int32 Height = F ? FMath::Max(16, int32(F->Lines(Line, List.Width()).Num() * F->Height)) : 20;
                N.Rect.Max.Y = Y + Height;
                N.Clip.Max.Y = FMath::Min(List.Max.Y, N.Rect.Max.Y);
                Y += Height;
            }
        }
    }
    for (auto &Icon : Icons)
        Native.LoadImage(Icon.Image, *Assets);
    Native.Prepare(*Assets, Fonts);
    MapUI.Update(*Mode.Get(), FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)), *Assets, Fonts);
}
int32 SAC2GameUI::OnPaint(const FPaintArgs &Args, const FGeometry &G, const FSlateRect &Cull,
                          FSlateWindowElementList &Out, int32 Layer, const FWidgetStyle &Style,
                          bool Enabled) const
{
    if (!Mode.IsValid() || !VisibleWorld())
        return Layer;
    const FGeometry P = G.MakeChild(Size, FSlateLayoutTransform(Scale));
    Layer = Native.Paint(Out, Layer, P, Fonts);
    for (const auto &I : Icons)
        if (const auto *B = Native.Brush(I.Image))
        {
            Out.PushClip(FSlateClippingZone(
                P.MakeChild(FVector2D(I.Clip.Size()), FSlateLayoutTransform(FVector2D(I.Clip.Min)))));
            FSlateDrawElement::MakeBox(
                Out, ++Layer,
                P.ToPaintGeometry(FVector2D(I.Rect.Size()), FSlateLayoutTransform(FVector2D(I.Rect.Min))), B);
            Out.PopClip();
        }
    if (ShowInventory && Portrait.IsValid() && Portrait->Ready)
    {
        auto R = Native.Bounds(TEXT("PaperDollViewport"));
        FSlateDrawElement::MakeBox(
            Out, ++Layer, P.ToPaintGeometry(FVector2D(R.Size()), FSlateLayoutTransform(FVector2D(R.Min))),
            &PortraitBrush);
    }
    if (const auto *N = Native.Find(TEXT("ChatLog")))
    {
        TArray<FAC2ChatLine> Rows;
        for (const auto &L : Log.Lines)
            for (const auto &Line : ChatFont->Lines(L.Text, N->Rect.Width()))
                Rows.Add({L.Type, Line});
        const int32 RowHeight = ChatFont->Height;
        const int32 Count = N->Rect.Height() / RowHeight;
        MaxScroll = FMath::Max(0, Rows.Num() - Count);
        const int32 Start = FMath::Max(0, Rows.Num() - Count - FMath::Clamp(Scroll, 0, MaxScroll));
        Out.PushClip(FSlateClippingZone(
            P.MakeChild(FVector2D(N->Clip.Size()), FSlateLayoutTransform(FVector2D(N->Clip.Min)))));
        for (int32 I = Start; I < FMath::Min(Start + Count, Rows.Num()); ++I)
        {
            auto &L = Rows[I];
            FLinearColor Color = L.Type == EAC2ChatType::Local    ? FLinearColor::White
                                 : L.Type == EAC2ChatType::Combat ? FLinearColor(.9f, .63f, 0)
                                 : L.Type == EAC2ChatType::Error  ? FLinearColor(1, .35f, .3f)
                                                                  : FLinearColor(.8f, .8f, .8f);
            ChatFont->Draw(Out, ++Layer, P, L.Text,
                           FVector2D(N->Rect.Min) + FVector2D(0, (I - Start) * RowHeight), Color);
        }
        Out.PopClip();
    }
    Layer = MapUI.Paint(Out, Layer + 1, P, Fonts);
    return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 1, Style, Enabled);
}
FReply SAC2GameUI::OnMouseMove(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(DraggingOption!=INDEX_NONE){DragOption(Mouse.X);return FReply::Handled();}
    MapUI.Move(Mouse);
    Native.Hovered = 0;
    for (int32 I = Native.Nodes.Num() - 1; I >= 0; --I)
        if (Native.Nodes[I].Kind == Token(TEXT("button")) && Inside(SlateRect(Native.Nodes[I].Clip), Mouse))
        {
            Native.Hovered = Native.Nodes[I].Id;
            break;
        }
    return FReply::Unhandled();
}
void SAC2GameUI::OnMouseCaptureLost(const FCaptureLostEvent& E)
{
    DraggingOption=INDEX_NONE;PressedAction=NAME_None;Native.Pressed=0;
    if(MapCaptured){MapCaptured=false;MapUI.Release(Mouse,false,false);}
    SCompoundWidget::OnMouseCaptureLost(E);
}
FReply SAC2GameUI::OnMouseButtonDown(const FGeometry &G, const FPointerEvent &E)
{
    OnMouseMove(G, E);
    if(ShowOptions && E.GetEffectingButton()==EKeys::LeftMouseButton)
        for(const auto& Slider:OptionSliders)if(Inside(Slider.Value,Mouse))
        {DraggingOption=Slider.Key;DragOption(Mouse.X);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if ((E.GetEffectingButton() == EKeys::LeftMouseButton || E.GetEffectingButton() == EKeys::RightMouseButton) &&
        MapUI.Press(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown()))
    {
        MapCaptured = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
        for (const auto &B : Buttons)
            if (Inside(B.Value, Mouse))
            {
                PressedAction = B.Key;
                Native.Pressed = Native.Hovered;
                return FReply::Handled().CaptureMouse(SharedThis(this));
            }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseButtonUp(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(DraggingOption!=INDEX_NONE && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragOption(Mouse.X);DraggingOption=INDEX_NONE;return FReply::Handled().ReleaseMouseCapture();}
    if (MapCaptured)
    {
        MapCaptured = false;
        MapUI.Release(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown());
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (E.GetEffectingButton() == EKeys::LeftMouseButton && !PressedAction.IsNone())
    {
        FName ActionName = PressedAction;
        PressedAction = NAME_None;
        Native.Pressed = 0;
        const auto *R = Buttons.Find(ActionName);
        if (R && Inside(*R, Mouse))
            Action(ActionName);
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseButtonDoubleClick(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if (MapUI.Press(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown()))
    {
        MapCaptured = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
        for (const auto &B : Buttons)
            if (Inside(B.Value, Mouse) &&
                (B.Key.ToString().StartsWith(TEXT("Item")) || B.Key.ToString().StartsWith(TEXT("Cell"))))
            {
                Action(B.Key);
                EquipSelected();
                return FReply::Handled();
            }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseWheel(const FGeometry &G, const FPointerEvent &E)
{
    auto P = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(ShowOptions&&OptionsTab==1&&Inside(SlateRect(Native.Bounds(TEXT("ClientList"))),P))
    {OptionsScroll=FMath::Clamp(OptionsScroll-FMath::RoundToInt(E.GetWheelDelta()),0,1);Rebuild();return FReply::Handled();}
    if (MapUI.Wheel(P, E.GetWheelDelta())) return FReply::Handled();
    if (ShowInventory && Inside(SlateRect(Native.Bounds(TEXT("ContainerListBox"))), P))
    {
        PackRow = FMath::Clamp(PackRow - FMath::RoundToInt(E.GetWheelDelta()), 0, 1);
        return FReply::Handled();
    }
    if (Inside(ChatRect(), P))
    {
        Scroll = FMath::Clamp(Scroll + FMath::RoundToInt(E.GetWheelDelta() * 3), 0, MaxScroll);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnKeyDown(const FGeometry &, const FKeyEvent &E)
{
    if (Editing && (E.GetKey() == EKeys::PageUp || E.GetKey() == EKeys::PageDown))
    {
        Scroll = FMath::Clamp(Scroll + (E.GetKey() == EKeys::PageUp ? 6 : -6), 0, MaxScroll);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
void SAC2GameUI::Action(FName Name)
{
    if (const FName *Bound = ButtonActions.Find(Name))
        Name = *Bound;
    if(OptionAction(Name))return;
    if (Name == TEXT("ChatEntry"))
    {
        BeginChat();
        return;
    }
    if (Name == TEXT("Health"))
    {
        HealthNumbers = !HealthNumbers;
        return;
    }
    if (Name == TEXT("Vigor"))
    {
        VigorNumbers = !VigorNumbers;
        return;
    }
    if (Name == TEXT("ChatUp") || Name == TEXT("ChatDown"))
    {
        Scroll = FMath::Clamp(Scroll + (Name == TEXT("ChatUp") ? 1 : -1), 0, MaxScroll);
        return;
    }
    if (Editing)
        EndChat();
    if (Name == TEXT("StopAttack"))
    {
        if(Mode->Network)Mode->Network->Chat(TEXT("/stop"));
        if(auto* P=Cast<AAC2Explorer>(Controller(Mode)->GetPawn()))P->SetNativeMode(AC2Motion::Peace);
        Mode->Target = nullptr;
        return;
    }
    if (Name == TEXT("Skills") || Name == TEXT("Quests") || Name == TEXT("Social"))
    {
        Log.Add(EAC2ChatType::System,
                TEXT("This panel's gameplay data is not available in the offline client yet."));
        return;
    }
    FString Key = Name.ToString();
    if (Key.StartsWith(TEXT("Pack")))
    {
        Pack = FMath::Clamp(FCString::Atoi(*Key.Mid(4)), 0, 3);
        PackRow = 0;
        return;
    }
    if (Key.StartsWith(TEXT("Item")))
    {
        SelectItem(FCString::Atoi(*Key.Mid(4)));
        return;
    }
    if (Key.StartsWith(TEXT("Cell")))
    {
        if (auto *P = Mode->Characters.Active())
        {
            int32 Cell = FCString::Atoi(*Key.Mid(4));
            int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return !I.Equipped && I.Cell == Cell; });
            if (Index != INDEX_NONE)
                SelectItem(Index);
            else if (P->Items.IsValidIndex(SelectedItem))
            {
                if(Mode->Network) {Mode->Network->MoveItem(SelectedItem,Cell);return;}
                auto Previous = Mode->Characters;
                FString Error;
                if (P->Move(SelectedItem, Cell, Error) &&
                    Mode->Characters.Save(Mode->CharacterSavePath, Error))
                    UpdatePortrait();
                else
                {
                    Mode->Characters = Previous;
                    Log.Add(EAC2ChatType::Error, Error);
                }
            }
        }
        return;
    }
    Mode->UIAction(Name);
}
void SAC2GameUI::ToggleInventory()
{
    EndChat();
    ShowInventory = !ShowInventory;
    if (ShowInventory)
        UpdatePortrait();
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(ShowInventory);
        Portrait->Capture->bCaptureEveryFrame = ShowInventory;
    }
    if (auto *PC = Controller(Mode))
        PC->SetCursor(ShowInventory || ShowStats || (Mode.IsValid() && Mode->bMap));
    Rebuild();
}
void SAC2GameUI::ToggleStats()
{
    EndChat();
    SelectedItem = INDEX_NONE;
    ShowStats = !ShowStats;
    if (ShowStats && Mode.IsValid()) { ShowOptions=false; Mode->bMap = false; MapUI.Close(); }
    if (auto *PC = Controller(Mode))
        PC->SetCursor(ShowInventory || ShowStats || (Mode.IsValid() && Mode->bMap));
    Rebuild();
}
bool SAC2GameUI::ClosePanels()
{
    if (!ShowInventory && !ShowStats && !ShowOptions && (!Mode.IsValid() || !Mode->bMap))
        return false;
    if (Mode.IsValid()) Mode->bMap = false;
    MapUI.Close();
    ShowInventory = ShowStats = ShowOptions = false;
    DraggingOption=INDEX_NONE;PressedAction=NAME_None;
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(false);
        Portrait->Capture->bCaptureEveryFrame = false;
    }
    if (auto *PC = Controller(Mode))
        PC->SetCursor(false);
    return true;
}
void SAC2GameUI::ToggleMap()
{
    if (!Mode.IsValid()) return;
    EndChat();
    Mode->bMap = !Mode->bMap;
    if (Mode->bMap) ShowStats = ShowOptions = false;
    else MapUI.Close();
    if (auto *PC = Controller(Mode)) PC->SetCursor(ShowInventory || ShowStats || Mode->bMap);
    Rebuild();
}
void SAC2GameUI::SelectItem(int32 Index)
{
    SelectedItem = Index;
    ShowStats = true;
    if (Mode.IsValid()) { Mode->bMap = false; MapUI.Close(); }
}
void SAC2GameUI::UpdatePortrait()
{
    const auto *P = Mode->Characters.Active();
    const auto *A = P ? Mode->CharacterCatalog.Avatar(P->Race, P->Sex) : nullptr;
    if (!A)
        return;
    if (!Portrait.IsValid())
        Portrait = Mode->GetWorld()->SpawnActor<AAC2Portrait>(FVector(0, 0, -800000), FRotator::ZeroRotator);
    if (Portrait.IsValid() &&
        Portrait->Show(Mode->DatDirectory, Mode->Network ? Mode->Network->Visual(P->Id) : A->Physics, P->Height, P->Build,
                       Mode->Network ? Mode->Network->Appearance(P->Id) : A->Resolve(P->Appearance, P->ClothingMask(Mode->CharacterCatalog)), 0x21000000))
    {
        Portrait->Image->ResizeTarget(306, 400);
        PortraitBrush.SetResourceObject(Portrait->Image);
        PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
        PortraitBrush.ImageSize = FVector2D(153, 200);
    }
}
void SAC2GameUI::EquipSelected()
{
    if(Mode.IsValid() && Mode->Network) {Mode->Network->UseItem(SelectedItem);return;}
    auto Previous = Mode->Characters;
    FString Error;
    if (auto *P = Mode->Characters.Active())
        if (P->Equip(SelectedItem, Mode->CharacterCatalog, Error))
        {
            if (Mode->Characters.Save(Mode->CharacterSavePath, Error))
            {
                UpdatePortrait();
                if (auto *PC = Controller(Mode))
                    if (auto *Pawn = Cast<AAC2Explorer>(PC->GetPawn()))
                        if (const auto *A = Mode->CharacterCatalog.Avatar(P->Race, P->Sex))
                            Pawn->ApplyAvatar(
                                A->Physics, P->Height, P->Build,
                                A->Resolve(P->Appearance, P->ClothingMask(Mode->CharacterCatalog)));
                Log.Add(EAC2ChatType::System, TEXT("Equipment saved."));
                return;
            }
            Mode->Characters = Previous;
        }
    if (!Error.IsEmpty())
        Log.Add(EAC2ChatType::Error, Error);
}
bool SAC2GameUI::SmokeEquip()
{
    auto *P = Mode->Characters.Active();
    if (!P || P->Items.IsEmpty())
        return false;
    SelectedItem = 0;
    uint32 Before = P->Items[0].Equipped;
    EquipSelected();
    return Mode->Characters.Active()->Items[0].Equipped != Before;
}
void SAC2GameUI::BeginChat()
{
    auto *PC = Controller(Mode);
    auto *H = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    if (!PC || Editing || (H && H->CharacterUI && H->CharacterUI->IsOpen()))
        return;
    PreviousCursor = PC->bShowMouseCursor;
    Editing = true;
    PC->SetCursor(true);
    PC->ResetIgnoreMoveInput();
    PC->SetIgnoreMoveInput(true);
    PC->FlushPressedKeys();
    if (auto *P = Cast<AAC2Explorer>(PC->GetPawn()))
        P->GetCharacterMovement()->StopMovementImmediately();
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(Entry);
    Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Input);
    FSlateApplication::Get().SetKeyboardFocus(Entry);
}
void SAC2GameUI::EndChat()
{
    if (!Editing)
        return;
    Editing = false;
    if (auto *PC = Controller(Mode))
    {
        PC->ResetIgnoreMoveInput();
        PC->FlushPressedKeys();
        PC->SetCursor(PreviousCursor);
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
void SAC2GameUI::Submit(const FString &Text)
{
    if(Mode.IsValid() && Text.TrimStartAndEnd().Equals(TEXT("/recall"),ESearchCase::IgnoreCase)){Mode->Recall();return;}
    if(Mode.IsValid() && Mode->Network && !Text.Equals(TEXT("/clear")) && !Text.Equals(TEXT("/loc")))
    { Log.Remember(Text); Mode->Network->Chat(Text); return; }
    auto Request = FAC2ChatRequest::Parse(Text);
    if (Text.TrimStartAndEnd().IsEmpty())
        return;
    Log.Remember(Text);
    Scroll = 0;
    const auto *Character = Mode.IsValid() ? Mode->Characters.Active() : nullptr;
    const FString Name = Character ? Character->Name : TEXT("Explorer");
    switch (Request.Command)
    {
    case EAC2ChatCommand::Say:
        Log.Add(EAC2ChatType::Local, Name + TEXT(": ") + Request.Text);
        break;
    case EAC2ChatCommand::Emote:
        Log.Add(EAC2ChatType::Local, TEXT("* ") + Name + TEXT(" ") + Request.Text);
        break;
    case EAC2ChatCommand::Help:
        Log.Add(EAC2ChatType::System,
                TEXT("/say or /s: local speech; /me: emote; /loc: position; /clear: clear log; /recall: return."));
        Log.Add(EAC2ChatType::System,
                TEXT("Up/Down: history. Mouse wheel or Page Up/Down: scroll. Escape: cancel entry."));
        break;
    case EAC2ChatCommand::Clear:
        Log.Lines.Reset();
        break;
    case EAC2ChatCommand::Location:
        if (auto *PC = Controller(Mode))
            if (APawn *Pawn = PC->GetPawn())
            {
                uint32 Block = 0;
                const FVector P = Pawn->GetActorLocation();
                AC2::WorldBlock(P, Block);
                Log.Add(EAC2ChatType::System,
                        FString::Printf(TEXT("Block %04X | position %.1f, %.1f, %.1f m"), Block, P.X / 100,
                                        P.Y / 100, P.Z / 100));
            }
        break;
    default:
        Log.Add(EAC2ChatType::Error, Request.Text);
        break;
    }
}
bool SAC2GameUI::Smoke(FString &Error)
{
    auto *PC = Controller(Mode);
    if (!PC || !BodyFont || !ChatFont || !TitleFont || Native.Nodes.IsEmpty() || Native.MissingImages != 0)
    {
        Error = TEXT("Native game UI resources incomplete");
        return false;
    }
    PC->SetCursor(false);
    BeginChat();
    if (!Editing || !PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored())
    {
        Error = TEXT("Chat did not capture world input");
        return false;
    }
    Entry->SetText(TEXT("Hello Dereth! 123"));
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
    if (Editing || PC->IsMoveInputIgnored() || PC->bShowMouseCursor ||
        Log.Lines.Last().Type != EAC2ChatType::Local ||
        !Log.Lines.Last().Text.EndsWith(TEXT("Hello Dereth! 123")))
    {
        Error = TEXT("Chat submit or world input restoration failed");
        return false;
    }
    BeginChat();
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Up, FModifierKeysState(), 0, false, 0, 0));
    if (Entry->Value() != TEXT("Hello Dereth! 123"))
    {
        Error = TEXT("Chat history failed");
        return false;
    }
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
    if (Editing || PC->IsMoveInputIgnored())
    {
        Error = TEXT("Chat cancel failed");
        return false;
    }
    Submit(TEXT("/tell nobody test"));
    if (Log.Lines.Last().Type != EAC2ChatType::Error)
    {
        Error = TEXT("Offline chat channel not identified");
        return false;
    }
    Submit(TEXT("/loc"));
    Log.Add(EAC2ChatType::Combat, TEXT("Combat messages appear here when you strike a target."));
    return true;
}
void SAC2GameUI::SmokeDraft()
{
    BeginChat();
    Entry->SetText(TEXT("/say Exploring Dereth!"));
}
