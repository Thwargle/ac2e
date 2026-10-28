#include "AC2MapUI.h"
#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2WorldGrid.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

namespace
{
uint32 T(const TCHAR *S)
{
    return FAC2UILayout::Token(S);
}
bool Inside(FIntRect R, FVector2D P)
{
    return R.Width() > 0 && R.Height() > 0 && P.X >= R.Min.X && P.Y >= R.Min.Y && P.X < R.Max.X &&
           P.Y < R.Max.Y;
}
// The five continent buttons and OverviewMapClientArea's native layout states.
const uint32 Continents[] = {0xdabcf722, 0x7b3e01e1, 0x037c1c30, 0x0e05f2ab, 0xaa12169a};
const uint32 Overviews[] = {0x05e947be, 0x47c002fe, 0x140fff33, 0xbfb2f786, 0xc4cb8f2e};
} // namespace
bool FAC2MapUI::Open(const FString &Directory, FString &Error)
{
    Ready = Native.Open(Directory, Error) && Data.Open(Directory, Error) &&
            Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, Error);
    if (!Ready && Error.IsEmpty())
        Error = TEXT("Unsupported map record");
    return Ready;
}
void FAC2MapUI::Zoom(FVector2D Center)
{
    Detail = true;
    Extent = FVector2D(128, 128);
    Origin = FAC2MapData::DetailOrigin(Center);
}
void FAC2MapUI::Macro()
{
    SelectedNote.Reset();
    NoteScroll = 0;
    Detail = Follow = false;
    Origin = MacroOrigin;
    Extent = MacroExtent;
}
FIntRect FAC2MapUI::RadarPlacement(const FAC2UILayout &Layout, FIntPoint Size)
{
    FIntRect R = Layout.Bounds(TEXT("RadarField"));
    R.Max += FIntPoint(1, 1);
    R += FIntPoint(Size.X - Layout.Width, 0);
    R += FIntPoint(FMath::Clamp(R.Min.X, 0, FMath::Max(0, Size.X - R.Width())) - R.Min.X,
                   FMath::Clamp(R.Min.Y, 0, FMath::Max(0, Size.Y - R.Height())) - R.Min.Y);
    return R;
}
void FAC2MapUI::Update(AAC2GameMode &InMode, FIntPoint Size, FAC2AssetLibrary &Assets, FAC2Fonts &Fonts)
{
    Mode = &InMode;
    Display = Size;
    Pictures.Reset();
    Markers.Reset();
    Contacts.Reset();
    MapRect = RadarRect = OverviewRect = FIntRect(0, 0, 0, 0);
    if (!Ready)
        return;
    auto *PC = Mode->GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->bShowMouseCursor)
        Native.Hovered = 0;
    APawn *Pawn = PC ? PC->GetPawn().Get() : nullptr;
    PlayerVisible = Pawn && !Mode->bIndoors;
    uint32 Block;
    if (PlayerVisible && AC2::WorldBlock(Pawn->GetActorLocation(), Block))
        LastLand = FAC2MapData::Landscape(Pawn->GetActorLocation());
    else
        PlayerVisible = false;
    if (Detail && Follow && PlayerVisible)
        Zoom(LastLand);
    const float RadarYaw = Pawn ? FAC2MapData::RadarYaw(Pawn->GetActorRotation().Yaw) : 90.f;
    Heading = FAC2MapData::RadarRotation(RadarYaw);
    Native.Begin(Size);
    Native.Visibility.Add(0xd740cf56, false);
    auto Show = [&](const TCHAR *Name, bool Value) { Native.Visibility.Add(T(Name), Value); };
    auto State = [&](const TCHAR *Name, const TCHAR *Value) { Native.States.Add(T(Name), T(Value)); };
    for (const TCHAR *Name :
         {TEXT("QuestField"), TEXT("SkillField"), TEXT("SocialField"), TEXT("OptionField"),
          TEXT("ExaminationField"), TEXT("InventoryField"), TEXT("MapField")})
        Show(Name, false);
    for (const TCHAR *Name : {TEXT("NavigateUpButton"), TEXT("NavigateDownButton"),
                              TEXT("NavigateLeftButton"), TEXT("NavigateRightButton")})
        Show(Name, Detail && !Follow);
    for (uint32 Id : Continents)
        Native.Visibility.Add(Id, !Detail);
    State(TEXT("ParentField"), Detail ? TEXT("DetailMap") : TEXT("MacroMap"));
    Native.States.Add(0xed9f9c38, T(Detail ? TEXT("DetailMap") : TEXT("MacroMap")));
    Native.States.Add(0x5f4cc0b6, T(Follow ? TEXT("highlight") : TEXT("normal")));
    Native.Text.Add(T(TEXT("CoordinatesText")),
                    PlayerVisible ? FAC2MapData::Coordinates(LastLand) : TEXT(""));
    const int32 Minute = FMath::FloorToInt(Mode->MapHour() * 60) % 1440;
    Native.Text.Add(T(TEXT("TimeText")), FString::Printf(TEXT("%02d:%02d"), Minute / 60, Minute % 60));
    Native.Text.Add(T(TEXT("DateText")), TEXT("")); // No fabricated calendar date without server game-time.
    if (Mode->bMap)
    {
        Native.Root(0x22000001, TEXT("LeftSidePanel"));
        Native.ConstrainRoot(TEXT("LeftSidePanel"), FIntRect(0, 0, Size.X, Size.Y));
        if (auto *N = Native.Find(TEXT("NavigationField")))
        {
            MacroOrigin = FVector2D(N->Attr(TEXT("MacroXOffset"), 321), N->Attr(TEXT("MacroYOffset"), 195));
            int32 Span = int32(FAC2MapData::LandCells * N->Float(TEXT("MacroScale"), .8435f));
            MacroExtent = FVector2D(Span, Span);
            if (!Detail)
            {
                Origin = MacroOrigin;
                Extent = MacroExtent;
            }
        }
        MapRect = Native.Bounds(TEXT("MapParentField"));
        Native.Visibility.Add(0x3f50a253, Detail);
        if (!Detail)
            Pictures.Add({Data.Image, FVector2D(MapRect.Min), FVector2D(MapRect.Size())});
        else
        {
            TSet<uint32> Used, NoteIds;
            for (FVector2D Corner :
                 {Origin, Origin + FVector2D(128, 0), Origin + FVector2D(0, 128), Origin + Extent})
                if (const auto *Tile =
                        Data.At(FIntPoint(FMath::FloorToInt(Corner.X), FMath::FloorToInt(Corner.Y))))
                    if (!Used.Contains(Tile->Value))
                    {
                        Used.Add(Tile->Value);
                        FVector2D TopLeft(Tile->Cells.Min.X, Tile->Cells.Max.Y);
                        Pictures.Add({Tile->Image, FAC2MapData::Project(TopLeft, Origin, Extent, MapRect),
                                      FVector2D(Tile->Cells.Size()) / Extent * FVector2D(MapRect.Size())});
                        for (uint32 Id : Tile->Notes)
                            NoteIds.Add(Id);
                    }
            for (uint32 Id : NoteIds)
                if (const auto *Notes = Data.Notes.Find(Id))
                    for (const auto &Note : *Notes)
                    {
                        FVector2D P = FAC2MapData::Project(FVector2D(Note.Cell), Origin, Extent, MapRect);
                        if (Inside(MapRect, P))
                            Markers.Add({Note.Image, P, Note.Label});
                    }
        }
        PlayerPixel = FAC2MapData::Project(LastLand, Origin, Extent, MapRect);
    }
    State(TEXT("BaseField"), Coords ? TEXT("open") : TEXT("closed"));
    // The coordinate backing element also switches with BaseField.
    Native.States.Add(0xcdfcce86, T(Coords ? TEXT("open") : TEXT("closed")));
    Native.Template(0x22000005, TEXT("RadarField"), RadarPlacement(Native.Layouts[0x22000005], Size),
                    FIntRect(0, 0, Size.X, Size.Y));
    RadarRect = Native.Bounds(TEXT("RadarField"));
    if (const auto* Field=Native.Find(TEXT("RadarField")))
    {
        RadarCenter=FVector2D(Field->Attr(TEXT("RadarCenter_X"),71),Field->Attr(TEXT("RadarCenter_Y"),69));
        RadarRadius=Field->Attr(TEXT("RadarRadius"),60);
    }
    if (!Coords)
        for (auto &N : Native.Nodes)
            if (N.Id == T(TEXT("CoordinatesText")) && N.Rect.Min.X >= RadarRect.Min.X)
                N.Text.Reset();
    Native.Template(0x22000005, TEXT("HeadingField"),
                    FIntRect(RadarRect.Min, RadarRect.Min + FIntPoint(143, 143)),
                    FIntRect(0, 0, Size.X, Size.Y));
    for (auto &N : Native.Nodes)
        if (N.Id == T(TEXT("HeadingField")))
            N.Media.Reset();
    if (Pawn)
    {
        const float Range = Mode->bIndoors ? 50.f : 75.f; // RadarControl 7160 defaults.
        const FVector2D Center = FVector2D(RadarRect.Min) + RadarCenter;
        auto Add = [&](AActor *Actor, FString Id, FString Name, bool Selected) {
            if (!Actor || Actor->IsHidden())
                return;
            const FVector Delta = Actor->GetActorLocation() - Pawn->GetActorLocation();
            if (Delta.SizeSquared2D() > FMath::Square(Range * 100))
                return;
            Contacts.Add({Center + FAC2MapData::RadarOffset(Delta, RadarYaw, Range)*(RadarRadius/60.f),
                          MoveTemp(Id), MoveTemp(Name), Actor, Selected});
        };
        if (Mode->Network)
            for (const auto &C : Mode->Network->RadarContacts())
                Add(C.Actor.Get(), C.Id, C.Name, C.Id == Mode->Network->TargetId);
        else
            for (const auto &Mob : Mode->MobList())
                if (Mob.IsValid() && Mob->Health > 0)
                    Add(Mob.Get(), TEXT(""), Mob->DisplayName, Mode->Target == Mob.Get());
    }
    if (Overview && Mode->bMap)
    {
        Native.States.Add(T(TEXT("OverviewMapClientArea")), Overview);
        Native.States.Add(0x3c8fbbba, Overview);
        FIntRect Box = Native.Layouts[0x22000005].Bounds(TEXT("OverviewMap"));
        Box.Max += FIntPoint(1, 1);
        if (OverviewOrigin.IsSet())
            Box += FIntPoint(FMath::RoundToInt(OverviewOrigin->X), FMath::RoundToInt(OverviewOrigin->Y)) -
                   Box.Min;
        // Clamp before resolving children: shrinking the viewport must not discard their clips.
        Box += FIntPoint(FMath::Clamp(Box.Min.X, 0, FMath::Max(0, Size.X - Box.Width())) - Box.Min.X,
                         FMath::Clamp(Box.Min.Y, 0, FMath::Max(0, Size.Y - Box.Height())) - Box.Min.Y);
        Native.Template(0x22000005, TEXT("OverviewMap"), Box, FIntRect(0, 0, Size.X, Size.Y));
        OverviewRect = Native.Bounds(TEXT("OverviewMap"));
        // Showing a movable window raises it above the anchored HUD roots.
        for (auto &N : Native.Nodes)
            if (N.Id == T(TEXT("OverviewMap")))
                N.Z = 0;
    }
    Native.Prepare(Assets, Fonts);
    for (auto &N : Native.Nodes)
    {
        uint32 Key = N.Attr(TEXT("tooltip_entry"));
        if (Key && !ButtonTips.Contains(N.Id))
            ButtonTips.Add(N.Id, AC2ReadUIString(Local, 0x25000012, Key));
        if (N.Id == 0x04c3d25a && !SelectedNote.IsEmpty())
            if (auto Font = Fonts.Get(N.Attr(TEXT("font"), 0x4200000c)))
            {
                auto Lines = Font->Lines(SelectedNote, N.Rect.Width());
                NoteScroll = FMath::Clamp(NoteScroll, 0,
                                          FMath::Max(0, Lines.Num() - N.Rect.Height() / int32(Font->Height)));
                N.Text.Reset();
                for (int32 I = NoteScroll; I < Lines.Num(); ++I)
                {
                    if (!N.Text.IsEmpty())
                        N.Text += TEXT("\n");
                    N.Text += Lines[I];
                }
            }
    }
    for (const auto &Pic : Pictures)
        Native.LoadImage(Pic.Image, Assets);
    for (const auto &Marker : Markers)
        Native.LoadImage(Marker.Image, Assets);
    for (uint32 Id : {0x41100000u, 0x41000a43u, 0x4100801bu})
        Native.LoadImage(Id, Assets);
    Move(Mouse);
    if (!PC || !PC->bShowMouseCursor)
    {
        Tooltip.Reset();
        Native.Hovered = 0;
    }
}
int32 FAC2MapUI::Paint(FSlateWindowElementList &Out, int32 Layer, const FGeometry &G, FAC2Fonts &Fonts) const
{
    if (!Ready)
        return Layer;
    const auto *Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    auto Box = [&](const FSlateBrush *Brush, FVector2D Point, FVector2D Size, FLinearColor Tint, int32 L) {
        if (Brush)
            FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Point)), Brush,
                                       ESlateDrawEffect::None, Tint);
    };
    Layer = Native.Paint(Out, Layer, G, Fonts, [&](const FAC2NativeNode &N, int32 L) {
        if (N.Id == T(TEXT("MapParentField")))
        {
            Box(Native.Brush(0x4100801b), FVector2D(MapRect.Min), FVector2D(MapRect.Size()),
                FLinearColor::White, ++L);
            for (const auto &Pic : Pictures)
                Box(Native.Brush(Pic.Image), Pic.Point, Pic.Size, FLinearColor::White, ++L);
            for (const auto &Marker : Markers)
                if (const auto *B = Native.Brush(Marker.Image))
                    Box(B, Marker.Point - B->ImageSize / 2, B->ImageSize, FLinearColor::White, ++L);
            if (PlayerVisible && Inside(MapRect, PlayerPixel))
                Box(Native.Brush(0x41000a43), PlayerPixel - FVector2D(4, 4), FVector2D(9, 9),
                    FLinearColor::White, ++L);
        }
        if (N.Id == T(TEXT("HeadingField")))
        {
            if (const auto *B = Native.Brush(0x41100000))
                FSlateDrawElement::MakeRotatedBox(
                    Out, ++L,
                    G.ToPaintGeometry(B->ImageSize, FSlateLayoutTransform(FVector2D(N.Rect.Min)+RadarCenter-B->ImageSize*.5)), B,
                    ESlateDrawEffect::None, Heading, TOptional<FVector2D>(),
                    FSlateDrawElement::RelativeToElement);
            for (const auto &C : Contacts)
            {
                if (C.Selected)
                    Box(Solid, C.Point - FVector2D(4, 4), FVector2D(8, 8), FLinearColor::White, ++L);
                Box(Solid, C.Point - FVector2D(2, 2), FVector2D(4, 4),
                    C.Id.IsEmpty() ? FLinearColor(1, .15f, .1f) : FLinearColor(1, .8f, .2f), ++L);
            }
        }
        return L;
    });
    if (!Tooltip.IsEmpty())
        if (auto Font = Fonts.Get(0x4200000c))
        {
            FVector2D S(FMath::Min(Font->Width(Tooltip) + 12.f, float(Display.X)), Font->Height + 8);
            FVector2D P(FMath::Clamp(Mouse.X + 16, 0., FMath::Max(0., Display.X - S.X)),
                        FMath::Clamp(Mouse.Y + 18, 0., FMath::Max(0., Display.Y - S.Y)));
            Box(Solid, P, S, FLinearColor(0, 0, 0, .94f), ++Layer);
            Font->Draw(Out, ++Layer, G, Tooltip, P + FVector2D(6, 4), FLinearColor::White);
        }
    return Layer;
}
bool FAC2MapUI::SmokeHeading(float Yaw,FString& Error) const
{
    if(!FMath::IsNearlyEqual(Heading,FAC2MapData::RadarRotation(Yaw),.001f))
    {Error=TEXT("Radar followed camera heading instead of character heading");return false;}
    return true;
}
bool FAC2MapUI::Contains(FVector2D Point) const
{
    return Inside(RadarRect, Point) || Inside(OverviewRect, Point) ||
           (Mode && Mode->bMap && Inside(Native.Bounds(TEXT("NavigationField")), Point));
}
uint32 FAC2MapUI::ButtonAt(FVector2D Point) const
{
    auto Order = Native.PaintOrder();
    for (int32 I = Order.Num() - 1; I >= 0; --I)
    {
        const auto &N = Native.Nodes[Order[I]];
        if (N.Kind == T(TEXT("button")) && Inside(N.Clip, Point) &&
            (!Inside(OverviewRect, Point) || Inside(OverviewRect, FVector2D(N.Rect.Min))))
            return N.Id;
    }
    return 0;
}
void FAC2MapUI::Move(FVector2D Point)
{
    Mouse = Point;
    Tooltip.Reset();
    Native.Hovered = ButtonAt(Point);
    if (const auto *Tip = ButtonTips.Find(Native.Hovered))
        Tooltip = *Tip;
    if (OverviewDragging)
    {
        FVector2D P = OverviewDragOrigin + Point - PressPoint;
        OverviewOrigin =
            FVector2D(FMath::Clamp(P.X, 0., FMath::Max(0., double(Display.X - OverviewRect.Width()))),
                      FMath::Clamp(P.Y, 0., FMath::Max(0., double(Display.Y - OverviewRect.Height()))));
        return;
    }
    if (Dragging && FVector2D::Distance(Point, PressPoint) > 3)
    {
        Moved = true;
        Follow = false;
        FVector2D D = (Point - PressPoint) / FVector2D(MapRect.Size()) * Extent;
        Origin = FAC2MapData::DetailOrigin(DragOrigin + FVector2D(-D.X, D.Y) + Extent / 2);
    }
    if (Inside(OverviewRect, Point))
        return;
    if (!Native.Hovered && Inside(MapRect, Point))
    {
        Tooltip = FAC2MapData::Coordinates(FAC2MapData::Unproject(Point, Origin, Extent, MapRect));
        for (const auto &M : Markers)
            if (FVector2D::Distance(M.Point, Point) < 7)
            {
                Tooltip = M.Label;
                break;
            }
        if (PlayerVisible && FVector2D::Distance(PlayerPixel, Point) < 6)
            Tooltip = TEXT("Your position - click to center");
    }
    if (Inside(RadarRect, Point))
        for (const auto &C : Contacts)
            if (FVector2D::Distance(C.Point, Point) < 6)
            {
                Tooltip = C.Name;
                break;
            }
}
bool FAC2MapUI::Press(FVector2D Point, bool Right, bool Shift)
{
    LastClickLand.Reset();
    Move(Point);
    if (!Contains(Point))
        return false;
    Pressed = ButtonAt(Point);
    Native.Pressed = Pressed;
    PressPoint = Point;
    Moved = false;
    Dragging = !Right && !Pressed && Detail && Inside(MapRect, Point) && !Inside(OverviewRect, Point);
    DragOrigin = Origin;
    OverviewDragging = !Right && !Pressed && Inside(OverviewRect, Point) && Point.Y < OverviewRect.Min.Y + 30;
    OverviewDragOrigin = FVector2D(OverviewRect.Min);
    return true;
}
bool FAC2MapUI::Release(FVector2D Point, bool Right, bool Shift)
{
    Move(Point);
    const uint32 Id = Pressed;
    Pressed = Native.Pressed = 0;
    Dragging = false;
    if (OverviewDragging)
    {
        OverviewDragging = false;
        return true;
    }
    if (Moved)
    {
        Moved = false;
        return true;
    }
    if (!Contains(PressPoint))
        return false;
    if (Right)
    {
        if (Inside(MapRect, Point))
            Macro();
        return true;
    }
    if (Id)
    {
        if (Id == ButtonAt(Point))
            Activate(Id, Shift);
        return true;
    }
    if (Inside(OverviewRect, Point))
        return true;
    if (Inside(MapRect, Point))
    {
        // Remember the pre-zoom projection for the second click of a double-click.
        LastClickLand = FAC2MapData::Unproject(Point, Origin, Extent, MapRect);
        LastClickPoint = Point;
        LastClickTime = FPlatformTime::Seconds();
        for (const auto &Marker : Markers)
            if (FVector2D::Distance(Marker.Point, Point) < 7)
            {
                SelectedNote = Marker.Label;
                NoteScroll = 0;
                return true;
            }
        if (PlayerVisible && FVector2D::Distance(PlayerPixel, Point) < 7)
        {
            Follow = true;
            Zoom(LastLand);
        }
        else if (!Detail)
            Zoom(FAC2MapData::Unproject(Point, Origin, Extent, MapRect));
        return true;
    }
    if (Inside(RadarRect, Point))
    {
        const FContact *Closest = nullptr;
        double Distance = 6;
        for (const auto &C : Contacts)
        {
            double D = FVector2D::Distance(C.Point, Point);
            if (D < Distance)
            {
                Closest = &C;
                Distance = D;
            }
        }
        if (Closest && Mode)
        {
            if (Mode->Network)
                Mode->Network->SelectTarget(Closest->Id);
            else if (auto *Mob = Cast<AAC2Mob>(Closest->Actor.Get()))
            {
                Mode->Target = Mob;
                Mode->Notice = TEXT("Target: ") + Mob->DisplayName;
            }
        }
        return true;
    }
    return true;
}
bool FAC2MapUI::DoubleClick(FVector2D Point)
{
    if (!Mode || !Mode->bMap || !Inside(MapRect, Point) || Inside(OverviewRect, Point) || ButtonAt(Point))
        return false;
    // A short drag or a click on another control must not turn into travel.
    if (!LastClickLand.IsSet() || FPlatformTime::Seconds() - LastClickTime >= 1 ||
        FVector2D::Distance(Point, LastClickPoint) >= 8)
        return false;
    const FVector2D Land = LastClickLand.GetValue();
    Close();
    Follow = false;
    Mode->TravelToMap(Land);
    return true;
}
bool FAC2MapUI::Wheel(FVector2D Point, float Delta)
{
    LastClickLand.Reset();
    if (Inside(OverviewRect, Point))
        return true;
    if (const auto *N = Native.Find(0x04c3d25a); N && Inside(N->Clip, Point))
    {
        NoteScroll = FMath::Max(0, NoteScroll - FMath::RoundToInt(Delta));
        return true;
    }
    if (!Inside(MapRect, Point) || Inside(OverviewRect, Point))
        return false;
    if (Delta > 0 && !Detail)
        Zoom(FAC2MapData::Unproject(Point, Origin, Extent, MapRect));
    else if (Delta < 0)
        Macro();
    return true;
}
void FAC2MapUI::Activate(uint32 Id, bool Shift)
{
    if (Id == 0xf2e033b3)
    {
        NoteScroll = FMath::Max(0, NoteScroll - 1);
        return;
    }
    if (Id == 0x51e30526)
    {
        ++NoteScroll;
        return;
    }
    if (Id == T(TEXT("CoordBoxButton")))
    {
        Coords = !Coords;
        return;
    }
    if (Id == T(TEXT("OverviewMapCloseButton")))
    {
        Overview = 0;
        return;
    }
    if (Id == T(TEXT("NavigationCloseButton")))
    {
        auto *PC = Mode ? Mode->GetWorld()->GetFirstPlayerController() : nullptr;
        auto *HUD = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
        if (HUD && HUD->GameUI)
            HUD->GameUI->ToggleMap();
        return;
    }
    if (Id == 0x3f50a253)
    {
        Macro();
        return;
    }
    if (Id == 0x5f4cc0b6)
    {
        Follow = !Follow;
        if (Follow)
            Zoom(LastLand);
        return;
    }
    for (int32 I = 0; I < 5; ++I)
        if (Id == Continents[I])
        {
            Overview = Overviews[I];
            return;
        }
    FVector2D Pan = FVector2D::ZeroVector;
    if (Id == T(TEXT("NavigateUpButton")))
        Pan.Y = 1;
    if (Id == T(TEXT("NavigateDownButton")))
        Pan.Y = -1;
    if (Id == T(TEXT("NavigateLeftButton")))
        Pan.X = -1;
    if (Id == T(TEXT("NavigateRightButton")))
        Pan.X = 1;
    if (!Pan.IsNearlyZero())
    {
        Follow = false;
        Origin = FAC2MapData::DetailOrigin(Origin + Extent / 2 + Pan * (Shift ? 3 : 9));
    }
    if (Id == 0x802e6b0c || Id == T(TEXT("OverviewMapHelpButton")))
        Mode->Notice = TEXT("Map: double-click to travel (offline/local AC2E testing); click or wheel up to zoom; drag or arrows to pan; right-click/wheel down "
                            "for world map; click your marker to follow. Alt enables radar selection.");
}
bool FAC2MapUI::Validate(FString &Error) const
{
    if (!Ready || !Native.Find(TEXT("RadarField")) || !Native.Brush(0x41100000) || Native.MissingImages)
    {
        Error = TEXT("Native map/radar resources unavailable");
        return false;
    }
    if (Mode && Mode->bMap && (MapRect.Width() != 238 || Pictures.IsEmpty()))
    {
        Error = TEXT("Native navigation geometry or tile content missing");
        return false;
    }
    return true;
}
bool FAC2MapUI::SmokeStep(int32 Step, FString &Error)
{
    if (!Validate(Error))
        return false;
    auto Click = [&](uint32 Id) {
        const auto *N = Native.Find(Id);
        if (!N)
        {
            Error = FString::Printf(TEXT("Missing map button %08X"), Id);
            return false;
        }
        FVector2D P = FVector2D(N->Rect.Min) + FVector2D(N->Rect.Size()) / 2;
        if (ButtonAt(P) != Id)
        {
            Error = FString::Printf(TEXT("Occluded map button %08X (hit %08X)"), Id, ButtonAt(P));
            return false;
        }
        return Press(P, false, false) && Release(P, false, false);
    };
    if (Step == 0)
    {
        if (Detail || !PlayerVisible || !Inside(MapRect, PlayerPixel))
        {
            Error = TEXT("Outdoor marker is absent from macro map");
            return false;
        }
        return true;
    }
    if (Step == 1)
    {
        Press(PlayerPixel, false, false);
        Release(PlayerPixel, false, false);
        return Detail && Follow;
    }
    if (Step == 2)
    {
        if (!Detail || Pictures.IsEmpty())
            return false;
        auto *Lock = Native.Find(0x5f4cc0b6);
        if (!Lock || !Lock->Media.ContainsByPredicate([](const auto &A) { return A.Image == 0x41002529; }))
        {
            Error = TEXT("Player-follow checkbox is not visibly checked");
            return false;
        }
        if (!Click(0x5f4cc0b6) || Follow)
        {
            Error = TEXT("Player-follow checkbox did not unlock");
            return false;
        }
        // Pan with the same pointer handlers used by Slate, then rotate the player.
        FVector2D P = FVector2D(MapRect.Min) + FVector2D(80, 80), Before = Origin;
        Press(P, false, false);
        Move(P + FVector2D(20, 10));
        Release(P + FVector2D(20, 10), false, false);
        if (DoubleClick(P + FVector2D(20, 10)))
        {
            Error = TEXT("A map drag was interpreted as travel");
            return false;
        }
        if (Origin.Equals(Before) || Follow)
        {
            Error = TEXT("Detail drag did not pan/unlock");
            return false;
        }
        if (APawn *Pawn = Mode->GetWorld()->GetFirstPlayerController()->GetPawn())
            Pawn->SetActorRotation(FRotator(0, Pawn->GetActorRotation().Yaw + 90, 0));
        return true;
    }
    if (Step == 3)
    {
        FVector2D Before = Origin;
        if (!Click(T(TEXT("NavigateRightButton"))) || !FMath::IsNearlyEqual(Origin.X, Before.X + 9))
        {
            Error = TEXT("Native pan arrow did not move nine land cells");
            return false;
        }
        if (Markers.IsEmpty() || Markers[0].Label.IsEmpty())
        {
            Error = TEXT("Native map-note text missing");
            return false;
        }
        auto Marker = Markers[0];
        Press(Marker.Point, false, false);
        Release(Marker.Point, false, false);
        if (SelectedNote != Marker.Label)
        {
            Error = TEXT("Map-note click did not update description");
            return false;
        }
        if (Contacts.IsEmpty())
        {
            Error = TEXT("Spawned mob absent from radar");
            return false;
        }
        FContact C = Contacts[0];
        Press(C.Point, false, false);
        Release(C.Point, false, false);
        if (Mode->Target != C.Actor.Get())
        {
            Error = TEXT("Radar click did not select mob");
            return false;
        }
        return true;
    }
    if (Step == 4)
    {
        Macro();
        return true;
    }
    if (Step == 5)
        return Click(Continents[0]);
    if (Step == 6)
    {
        if (!Native.Find(TEXT("OverviewMapClientArea")) || !Native.Brush(0x41006f0e))
            return false;
        FVector2D P = FVector2D(OverviewRect.Min) + FVector2D(100, 15);
        FVector2D Delta(Display.X - OverviewRect.Max.X, 30);
        Press(P, false, false);
        Move(P + Delta);
        Release(P + Delta, false, false);
        if (!OverviewOrigin.IsSet() || !OverviewOrigin->Equals(FVector2D(OverviewRect.Min) + Delta))
        {
            Error = TEXT("Overview title bar did not drag");
            return false;
        }
        return true;
    }
    if (Step == 7)
    {
        const auto *Close = Native.Find(TEXT("OverviewMapCloseButton"));
        if (!Close || Close->Clip.Width() != 17 || OverviewRect.Max.X > Display.X)
        {
            Error = TEXT("Resizing clipped the dragged overview's controls");
            return false;
        }
        if (!Click(T(TEXT("OverviewMapCloseButton"))))
            return false;
        bool Before = Coords;
        if (!Click(T(TEXT("CoordBoxButton"))) || Coords == Before)
            return false;
        return true;
    }
    if (Step == 9)
    {
        Close();
        Macro();
        return true;
    }
    if (Step == 10 || Step == 11)
    {
        auto* PC = Mode->GetWorld()->GetFirstPlayerController();
        auto* HUD = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
        if (!HUD || !HUD->GameUI) return false;
        const FVector2D Target(692.25, 828.5);
        const FVector2D Pixel = Step == 10 ? FAC2MapData::Project(Target, Origin, Extent, MapRect) : LastClickPoint;
        const auto& G = HUD->GameUI->GetCachedGeometry();
        const FVector2D Absolute = G.LocalToAbsolute(Pixel / G.GetAccumulatedLayoutTransform().GetScale());
        const FPointerEvent Down(0, Absolute, Absolute, {EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        const FPointerEvent Up(0, Absolute, Absolute, {}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        if (Step == 10)
        {
            HUD->GameUI->OnMouseButtonDown(G, Down);
            HUD->GameUI->OnMouseButtonUp(G, Up);
            if (!Detail || !LastClickLand.IsSet() || !LastClickLand->Equals(Target, .001))
            { Error = TEXT("First map click lost its original world projection"); return false; }
        }
        else
        {
            if (!Detail || !LastClickLand.IsSet() || !LastClickLand->Equals(Target, .001))
            { Error = TEXT("Map zoom discarded the pending double-click destination"); return false; }
            if (!HUD->GameUI->OnMouseButtonDoubleClick(G, Down).IsEventHandled()) return false;
            HUD->GameUI->OnMouseButtonUp(G, Up);
        }
        return true;
    }
    if (Step == 8)
    {
        if (PlayerVisible)
        {
            Error = TEXT("Interior drew an outdoor player marker");
            return false;
        }
        return Click(T(TEXT("CoordBoxButton")));
    }
    return false;
}
bool FAC2MapUI::SmokeNetworkTarget(const FString &Id, FString &Error)
{
    if (!Validate(Error) || !Mode || !Mode->Network)
        return false;
    const auto *Contact = Contacts.FindByPredicate([&](const auto &C) { return C.Id == Id; });
    if (!Contact)
    {
        Error = TEXT("Server object missing from native radar");
        return false;
    }
    FVector2D P = Contact->Point;
    Press(P, false, false);
    Release(P, false, false);
    if (Mode->Network->TargetId != Id)
    {
        Error = TEXT("Native radar did not select server target");
        return false;
    }
    return true;
}
