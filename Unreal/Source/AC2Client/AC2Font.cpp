#include "AC2Font.h"
#include "Engine/Texture2D.h"
#include "GameFramework/HUD.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

bool FAC2FontData::Decode(uint32 Id, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id || Id >> 24 != 0x42)
        return false;
    Height = R.Word();
    MaxWidth = R.Word();
    int32 Count = R.Count(12, 65536);
    AverageWidth = R.Word();
    StripWidth = R.Word();
    uint32 AA = R.Word();
    Antialiased = AA != 0;
    if (!R.Good || !Height || Height > 256 || !MaxWidth || MaxWidth > 256 || !StripWidth ||
        StripWidth > 4194304 || AA > 1 || (!AA && StripWidth % 8))
        return false;
    for (int32 I = 0; I < Count && R.Good; ++I)
    {
        FAC2FontGlyph G;
        G.Width = R.Word();
        G.Unicode = R.Short();
        R.Short();
        G.Offset = R.Word();
        if (G.Width > MaxWidth || uint64(G.Offset) + G.Width > StripWidth || Map.Contains(G.Unicode))
            return false;
        Map.Add(G.Unicode, Glyphs.Add(G));
    }
    int64 Size = int64(StripWidth) * Height / (AA ? 1 : 8);
    if (!Count || !R.Good || Size != R.Left())
        return false;
    Coverage.Append(R.Data.GetData() + R.Pos, int32(Size));
    return true;
}
const FAC2FontGlyph *FAC2FontData::Glyph(TCHAR C) const
{
    const int32 *Index = Map.Find(uint16(C));
    if (!Index)
        Index = Map.Find('?');
    return Index ? &Glyphs[*Index] : nullptr;
}
uint8 FAC2FontData::Pixel(uint32 X, uint32 Y) const
{
    if (X >= StripWidth || Y >= Height)
        return 0;
    return Antialiased ? Coverage[Y * StripWidth + X]
                       : ((Coverage[Y * (StripWidth / 8) + X / 8] >> (7 - X % 8)) & 1) * 255;
}
float FAC2FontData::Width(const FString &Text) const
{
    float X = 0, Max = 0;
    for (TCHAR C : Text)
        if (C == '\n')
        {
            Max = FMath::Max(Max, X);
            X = 0;
        }
        else if (C != '\r')
            if (const auto *G = Glyph(C))
                X += G->Width;
    return FMath::Max(X, Max);
}
TArray<FString> FAC2FontData::Lines(const FString &Text, float WrapWidth) const
{
    TArray<FString> Lines;
    FString Line;
    for (TCHAR C : Text)
    {
        if (C == '\r')
            continue;
        if (C == '\n')
        {
            Lines.Add(Line);
            Line.Empty();
            continue;
        }
        Line.AppendChar(C);
        if (WrapWidth > 0 && Line.Len() > 1 && Width(Line) > WrapWidth)
        {
            int32 Space = INDEX_NONE;
            Line.FindLastChar(' ', Space);
            if (Space > 0)
            {
                Lines.Add(Line.Left(Space));
                Line = Line.Mid(Space + 1);
            }
            else
            {
                Lines.Add(Line.LeftChop(1));
                Line = Line.Right(1);
            }
        }
    }
    Lines.Add(Line);
    return Lines;
}
bool FAC2Font::BuildAtlas()
{
    constexpr int32 AtlasWidth = 1024;
    int32 X = 1, Y = 1;
    for (auto &G : Glyphs)
    {
        if (X + int32(G.Width) + 1 > AtlasWidth)
        {
            X = 1;
            Y += Height + 2;
        }
        G.Atlas = FIntPoint(X, Y);
        X += G.Width + 2;
    }
    int32 AtlasHeight = FMath::RoundUpToPowerOfTwo(Y + Height + 1);
    if (AtlasHeight > 16384)
        return false;
    TArray<FColor> Pixels;
    Pixels.Init(FColor(255, 255, 255, 0), AtlasWidth * AtlasHeight);
    for (const auto &G : Glyphs)
        for (uint32 Row = 0; Row < Height; ++Row)
            for (uint32 Col = 0; Col < G.Width; ++Col)
                Pixels[(G.Atlas.Y + Row) * AtlasWidth + G.Atlas.X + Col].A = Pixel(G.Offset + Col, Row);
    Texture.Reset(UTexture2D::CreateTransient(AtlasWidth, AtlasHeight, PF_B8G8R8A8));
    if (!Texture.IsValid())
        return false;
    Texture->SRGB = true;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = Texture->AddressY = TA_Clamp;
    Texture->NeverStream = true;
    Texture->LODGroup = TEXTUREGROUP_UI;
    auto &Mip = Texture->GetPlatformData()->Mips[0];
    void *Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    for (auto &G : Glyphs)
    {
        G.Brush.SetResourceObject(Texture.Get());
        G.Brush.DrawAs = ESlateBrushDrawType::Image;
        G.Brush.ImageSize = FVector2D(G.Width, Height);
        G.Brush.SetUVRegion(FBox2f(
            FVector2f(float(G.Atlas.X) / AtlasWidth, float(G.Atlas.Y) / AtlasHeight),
            FVector2f(float(G.Atlas.X + G.Width) / AtlasWidth, float(G.Atlas.Y + Height) / AtlasHeight)));
    }
    return true;
}
void FAC2Font::Draw(FSlateWindowElementList &Out, int32 Layer, const FGeometry &Geometry, const FString &Text,
                    FVector2D Position, FLinearColor Color) const
{
    const double Start = Position.X;
    for (TCHAR C : Text)
    {
        if (C == '\n')
        {
            Position.X = Start;
            Position.Y += Height;
            continue;
        }
        if (C == '\r')
            continue;
        if (const auto *G = Glyph(C))
        {
            if (G->Width)
                FSlateDrawElement::MakeBox(
                    Out, Layer,
                    Geometry.ToPaintGeometry(FVector2D(G->Width, Height), FSlateLayoutTransform(Position)),
                    &G->Brush, ESlateDrawEffect::None, Color);
            Position.X += G->Width;
        }
    }
}
void FAC2Font::DrawHUD(AHUD &HUD, const FString &Text, FVector2D Position, FLinearColor Color,
                       float Scale) const
{
    const double Start = Position.X;
    for (TCHAR C : Text)
    {
        if (C == '\n')
        {
            Position.X = Start;
            Position.Y += Height * Scale;
            continue;
        }
        if (C == '\r')
            continue;
        if (const auto *G = Glyph(C))
        {
            if (G->Width)
                HUD.DrawTexture(Texture.Get(), Position.X, Position.Y, G->Width * Scale, Height * Scale,
                                float(G->Atlas.X) / Texture->GetSizeX(),
                                float(G->Atlas.Y) / Texture->GetSizeY(),
                                float(G->Width) / Texture->GetSizeX(), float(Height) / Texture->GetSizeY(),
                                Color, BLEND_Translucent);
            Position.X += G->Width * Scale;
        }
    }
}
bool FAC2Fonts::Open(const FString &Directory, FString &Error)
{
    Cache.Empty();
    const FString Dir = AC2::ResolveDatDirectory(Directory);
    return Portal.Open(FPaths::Combine(Dir, TEXT("portal.dat")), 1, Error) &&
           Local.Open(FPaths::Combine(Dir, TEXT("local_English.dat")), 3, Error);
}
TSharedPtr<FAC2Font> FAC2Fonts::Get(uint32 Id)
{
    if (const auto *Found = Cache.Find(Id))
        return *Found;
    TArray<uint8> Bytes;
    FString Error;
    auto Font = MakeShared<FAC2Font>();
    if (!(Id < 0x42001000 ? Portal : Local).Read(Id, Bytes, Error) || !Font->Decode(Id, Bytes) ||
        !Font->BuildAtlas())
    {
        UE_LOG(LogTemp, Error, TEXT("AC2 DAT font %08X could not be loaded: %s"), Id, *Error);
        Cache.Add(Id, nullptr);
        return nullptr;
    }
    Cache.Add(Id, Font);
    UE_LOG(LogTemp, Display, TEXT("AC2 DAT font %08X: %d glyphs, authored height %d"), Id, Font->Glyphs.Num(),
           Font->Height);
    return Font;
}
TSharedPtr<FAC2Font> FAC2Fonts::Body(int32 Size)
{
    // Native in-game text family, discrete DAT sizes; no scaling a substitute font to fit.
    return Get(Size <= 12   ? 0x4200001b
               : Size <= 14 ? 0x4200000c
               : Size <= 18 ? 0x4200000d
               : Size <= 24 ? 0x4200000e
                            : 0x42000021);
}
void SAC2Text::Construct(const FArguments &Args)
{
    Text = Args._Text;
    Font = Args._Font;
    Color = Args._ColorAndOpacity;
    ShadowColor = Args._ShadowColorAndOpacity;
    Shadow = Args._ShadowOffset;
    Wrap = Args._AutoWrapText;
    Justify = Args._Justification;
    SetClipping(EWidgetClipping::ClipToBounds);
}
FVector2D SAC2Text::ComputeDesiredSize(float) const
{
    if (!Font)
        return FVector2D::ZeroVector;
    const float Width = Wrap ? GetCachedGeometry().GetLocalSize().X : 0;
    auto Lines = Font->Lines(Text.Get().ToString(), Width);
    float Max = 0;
    for (const auto &Line : Lines)
        Max = FMath::Max(Max, Font->Width(Line));
    return FVector2D(Max + Shadow.X, Lines.Num() * Font->Height + Shadow.Y);
}
int32 SAC2Text::OnPaint(const FPaintArgs &, const FGeometry &G, const FSlateRect &,
                        FSlateWindowElementList &Out, int32 Layer, const FWidgetStyle &Style,
                        bool Enabled) const
{
    if (!Font)
        return Layer;
    auto Lines = Font->Lines(Text.Get().ToString(), Wrap ? G.GetLocalSize().X : 0);
    float Y = 0;
    FLinearColor Tint = Color * Style.GetColorAndOpacityTint();
    if (!ShouldBeEnabled(Enabled))
        Tint.A *= .5f;
    for (const auto &Line : Lines)
    {
        float X = Justify == ETextJustify::Center  ? (G.GetLocalSize().X - Font->Width(Line)) / 2
                  : Justify == ETextJustify::Right ? G.GetLocalSize().X - Font->Width(Line)
                                                   : 0;
        if (!Shadow.IsZero())
            Font->Draw(Out, Layer, G, Line, FVector2D(X, Y) + Shadow, ShadowColor.CopyWithNewOpacity(Tint.A));
        Font->Draw(Out, Layer + 1, G, Line, FVector2D(X, Y), Tint);
        Y += Font->Height;
    }
    return Layer + 1;
}
void SAC2TextEntry::Construct(const FArguments &Args)
{
    Text = Args._Text;
    Font = Args._Font;
    Caret = Anchor = Text.Len();
    OnEnter = Args._OnEnter;
    OnEscape = Args._OnEscape;
    OnHistory = Args._OnHistory;
    NameOnly = Args._NameOnly;
    Password=Args._Password;
    MaxCharacters = FMath::Clamp(Args._MaxCharacters, 1, 512);
    OnChanged = Args._OnTextChanged;
    SetClipping(EWidgetClipping::ClipToBounds);
}
FVector2D SAC2TextEntry::ComputeDesiredSize(float) const
{
    return FVector2D(180, Font ? Font->Height : 20);
}
float SAC2TextEntry::Scroll(float Available) const
{
    return Font ? FMath::Max(0.f, Font->Width(DisplayText().Left(Caret)) - Available + 3) : 0;
}
int32 SAC2TextEntry::OnPaint(const FPaintArgs &, const FGeometry &G, const FSlateRect &,
                             FSlateWindowElementList &Out, int32 Layer, const FWidgetStyle &, bool) const
{
    if (!Font)
        return Layer;
    float Offset = Scroll(G.GetLocalSize().X), Y = (G.GetLocalSize().Y - Font->Height) / 2;
    auto Box = [&](float X, float W, FLinearColor C) {
        FSlateDrawElement::MakeBox(
            Out, Layer,
            G.ToPaintGeometry(FVector2D(W, Font->Height), FSlateLayoutTransform(FVector2D(X - Offset, Y))),
            FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, C);
    };
    if (HasKeyboardFocus())
    {
        float A = Font->Width(DisplayText().Left(FMath::Min(Anchor, Caret)));
        float B = Font->Width(DisplayText().Left(FMath::Max(Anchor, Caret)));
        if (A != B)
            Box(A, B - A, FLinearColor(.25f, .27f, .16f, .8f));
        if (FMath::Fmod(FPlatformTime::Seconds(), 1.) < .6)
            Box(Font->Width(DisplayText().Left(Caret)), 1, FLinearColor(.95f, .95f, .8f));
    }
    Font->Draw(Out, Layer + 1, G, DisplayText(), FVector2D(-Offset, Y), FLinearColor(.95f, .95f, .8f));
    return Layer + 1;
}
bool SAC2TextEntry::ReplaceSelection(const FString &Inserted)
{
    for (TCHAR C : Inserted)
        if ((NameOnly &&
             !((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == ' ' || C == '\'' || C == '-')) ||
            (!NameOnly && (C < 32 || (!Password && Font && !Font->Map.Contains(uint16(C))))))
            return false;
    int32 Start = FMath::Min(Caret, Anchor), End = FMath::Max(Caret, Anchor);
    FString Next = Text.Left(Start) + Inserted + Text.Mid(End);
    if (Next.Len() > MaxCharacters)
        return false;
    Text = Next;
    Caret = Anchor = Start + Inserted.Len();
    OnChanged.ExecuteIfBound(FText::FromString(Text));
    Invalidate(EInvalidateWidgetReason::Paint);
    return true;
}
FReply SAC2TextEntry::OnKeyChar(const FGeometry &, const FCharacterEvent &E)
{
    if (!E.IsControlDown() && !E.IsAltDown() && E.GetCharacter() >= 32)
        ReplaceSelection(FString::Chr(E.GetCharacter()));
    return FReply::Handled();
}
FReply SAC2TextEntry::OnKeyDown(const FGeometry &, const FKeyEvent &E)
{
    const auto K = E.GetKey();
    if (K == EKeys::Enter)
        OnEnter.ExecuteIfBound();
    else if (K == EKeys::Escape && OnEscape.IsBound())
        OnEscape.Execute();
    else if ((K == EKeys::Up || K == EKeys::Down) && OnHistory.IsBound())
        OnHistory.Execute(K == EKeys::Up ? -1 : 1);
    else if (E.IsControlDown() && K == EKeys::A)
    {
        Anchor = 0;
        Caret = Text.Len();
    }
    else if (E.IsControlDown() && (K == EKeys::C || K == EKeys::X))
    {
        if(!Password)FPlatformApplicationMisc::ClipboardCopy(
            *Text.Mid(FMath::Min(Caret, Anchor), FMath::Abs(Caret - Anchor)));
        if (K == EKeys::X)
            ReplaceSelection(TEXT(""));
    }
    else if (E.IsControlDown() && K == EKeys::V)
    {
        FString Pasted;
        FPlatformApplicationMisc::ClipboardPaste(Pasted);
        ReplaceSelection(Pasted);
    }
    else if (K == EKeys::BackSpace || K == EKeys::Delete)
    {
        if (Anchor == Caret)
            Anchor = FMath::Clamp(Caret + (K == EKeys::BackSpace ? -1 : 1), 0, Text.Len());
        ReplaceSelection(TEXT(""));
    }
    else if (K == EKeys::Left || K == EKeys::Right || K == EKeys::Home || K == EKeys::End)
    {
        if (K == EKeys::Home)
            Caret = 0;
        else if (K == EKeys::End)
            Caret = Text.Len();
        else if (!E.IsShiftDown() && Anchor != Caret)
            Caret = K == EKeys::Left ? FMath::Min(Caret, Anchor) : FMath::Max(Caret, Anchor);
        else
            Caret = FMath::Clamp(Caret + (K == EKeys::Left ? -1 : 1), 0, Text.Len());
        if (!E.IsShiftDown())
            Anchor = Caret;
    }
    else
        return FReply::Unhandled();
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Handled();
}
int32 SAC2TextEntry::Hit(const FGeometry &G, const FPointerEvent &E) const
{
    if (!Font)
        return 0;
    float X = G.AbsoluteToLocal(E.GetScreenSpacePosition()).X + Scroll(G.GetLocalSize().X), Width = 0;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const auto *Glyph = Font->Glyph(Password?TEXT('*'):Text[I]);
        float Advance = Glyph ? Glyph->Width : 0;
        if (X < Width + Advance / 2)
            return I;
        Width += Advance;
    }
    return Text.Len();
}
FReply SAC2TextEntry::OnMouseButtonDown(const FGeometry &G, const FPointerEvent &E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton)
        return FReply::Unhandled();
    Caret = Hit(G, E);
    if (!E.IsShiftDown())
        Anchor = Caret;
    return FReply::Handled().SetUserFocus(AsShared()).CaptureMouse(AsShared());
}
FReply SAC2TextEntry::OnMouseMove(const FGeometry &G, const FPointerEvent &E)
{
    if (!HasMouseCapture())
        return FReply::Unhandled();
    Caret = Hit(G, E);
    return FReply::Handled();
}
FReply SAC2TextEntry::OnMouseButtonUp(const FGeometry &, const FPointerEvent &E)
{
    return E.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture()
               ? FReply::Handled().ReleaseMouseCapture()
               : FReply::Unhandled();
}

void SAC2TextEntry::SetText(const FString &Value)
{
    Anchor = 0;
    Caret = Text.Len();
    ReplaceSelection(Value);
}
