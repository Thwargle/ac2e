#pragma once
#include "AC2AssetReader.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"
#include "Types/SlateEnums.h"
DECLARE_DELEGATE_OneParam(FOnAC2NameChanged, const FText &);
DECLARE_DELEGATE_OneParam(FOnAC2History, int32);

class UTexture2D;
class AHUD;

// Font::StreamPack, FontCharDesc and GetStringWidthInternal (Legions 7160).
struct FAC2FontGlyph
{
    uint32 Width = 0, Offset = 0;
    uint16 Unicode = 0;
    FIntPoint Atlas = FIntPoint::ZeroValue;
    FSlateBrush Brush;
};
struct FAC2FontData
{
    uint32 Height = 0, MaxWidth = 0, AverageWidth = 0, StripWidth = 0;
    bool Antialiased = false;
    TArray<FAC2FontGlyph> Glyphs;
    TArray<uint8> Coverage;
    TMap<uint16, int32> Map;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
    const FAC2FontGlyph *Glyph(TCHAR C) const;
    uint8 Pixel(uint32 X, uint32 Y) const;
    float Width(const FString &Text) const;
    TArray<FString> Lines(const FString &Text, float WrapWidth = 0) const;
};
class FAC2Font : public FAC2FontData
{
  public:
    TStrongObjectPtr<UTexture2D> Texture;
    bool BuildAtlas();
    void Draw(FSlateWindowElementList &Out, int32 Layer, const FGeometry &Geometry, const FString &Text,
              FVector2D Position, FLinearColor Color) const;
    void DrawHUD(AHUD &HUD, const FString &Text, FVector2D Position, FLinearColor Color, float Scale) const;
};
class FAC2Fonts
{
  public:
    bool Open(const FString &Directory, FString &Error);
    TSharedPtr<FAC2Font> Get(uint32 Id);
    TSharedPtr<FAC2Font> Body(int32 Size = 14);

  private:
    FAC2DatArchive Portal, Local;
    TMap<uint32, TSharedPtr<FAC2Font>> Cache;
};

// Bitmap text uses the authored advances, glyph coverage and line height, with no system-font fallback.
class SAC2Text : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2Text)
        : _ColorAndOpacity(FLinearColor::White), _AutoWrapText(false), _Justification(ETextJustify::Left),
          _ShadowOffset(1, 1), _ShadowColorAndOpacity(FLinearColor::Black)
    {
    }
    SLATE_ATTRIBUTE(FText, Text)
    SLATE_ARGUMENT(TSharedPtr<FAC2Font>, Font)
    SLATE_ARGUMENT(FLinearColor, ColorAndOpacity)
    SLATE_ARGUMENT(bool, AutoWrapText)
    SLATE_ARGUMENT(ETextJustify::Type, Justification)
    SLATE_ARGUMENT(FVector2D, ShadowOffset)
    SLATE_ARGUMENT(FLinearColor, ShadowColorAndOpacity)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args);
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual int32 OnPaint(const FPaintArgs &, const FGeometry &, const FSlateRect &,
                          FSlateWindowElementList &, int32, const FWidgetStyle &, bool) const override;

  private:
    TAttribute<FText> Text;
    TSharedPtr<FAC2Font> Font;
    FLinearColor Color, ShadowColor;
    FVector2D Shadow;
    bool Wrap = false;
    ETextJustify::Type Justify = ETextJustify::Left;
};

// Shared DAT editor. Naming restricts the alphabet; chat allows every printable glyph in its DAT font.
// Selection, scrolling and mouse hit testing use the same advances as the rendered text.
class SAC2TextEntry : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2TextEntry) : _NameOnly(true), _Password(false), _MaxCharacters(16)
    {
    }
    SLATE_ARGUMENT(FString, Text)
    SLATE_ARGUMENT(TSharedPtr<FAC2Font>, Font)
    SLATE_ARGUMENT(bool, NameOnly)
    SLATE_ARGUMENT(bool, Password)
    SLATE_ARGUMENT(int32, MaxCharacters)
    SLATE_EVENT(FSimpleDelegate, OnEscape)
    SLATE_EVENT(FOnAC2History, OnHistory)
    SLATE_EVENT(FSimpleDelegate, OnEnter)
    SLATE_EVENT(FOnAC2NameChanged, OnTextChanged)
    SLATE_END_ARGS()
    void Construct(const FArguments &Args);
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }
    virtual FVector2D ComputeDesiredSize(float) const override;
    virtual int32 OnPaint(const FPaintArgs &, const FGeometry &, const FSlateRect &,
                          FSlateWindowElementList &, int32, const FWidgetStyle &, bool) const override;
    virtual FReply OnKeyDown(const FGeometry &, const FKeyEvent &) override;
    virtual FReply OnKeyChar(const FGeometry &, const FCharacterEvent &) override;
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseMove(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonUp(const FGeometry &, const FPointerEvent &) override;
    bool ReplaceSelection(const FString &Inserted);
    void SetText(const FString &Value);
    void SetFont(TSharedPtr<FAC2Font> Value){Font=Value;Invalidate(EInvalidateWidgetReason::Layout);}
    const FString &Value() const
    {
        return Text;
    }

  private:
    FString Text;
    TSharedPtr<FAC2Font> Font;
    int32 Caret = 0, Anchor = 0;
    FSimpleDelegate OnEnter, OnEscape;
    FOnAC2History OnHistory;
    bool NameOnly = true;
    bool Password=false;
    FString DisplayText() const {return Password?FString::ChrN(Text.Len(),TEXT('*')):Text;}
    int32 MaxCharacters = 16;
    FOnAC2NameChanged OnChanged;
    float Scroll(float Available) const;
    int32 Hit(const FGeometry &, const FPointerEvent &) const;
};
