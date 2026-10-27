#pragma once
#include "AC2UILayout.h"
#include "AC2Font.h"
#include "AC2Assets.h"

// A resolved instance of a DAT ElementDesc, in physical pixels (exclusive Max after placement).
// Reference instances retain their own identity/anchors while inheriting their template's contents.
struct FAC2NativeNode
{
    uint32 Id = 0, Kind = 0, State = 0;
    int32 Parent = INDEX_NONE;
    int32 Z = 0;
    FIntRect Rect, Clip;
    TMap<uint32, uint32> Attributes;
    TArray<FAC2UIArt> Media;
    FString Text;
    float Rotation=0;
    uint32 Attr(const TCHAR *Name, uint32 Default = 0) const;
    float Float(const TCHAR *Name, float Default = 0) const;
};

// UIElement::{SetState,UpdateSizeAndPosition,ResetMedia}, LayoutDesc::DereferenceElementDesc.
// This evaluates DAT layout data; game bindings supply only changing values and visibility.
class FAC2NativeUI
{
  public:
    bool Open(const FString &Directory, FString &Error);
    TMap<uint32, FAC2UILayout> Layouts;
    TArray<FAC2NativeNode> Nodes;
    TMap<uint32, bool> Visibility;
    TMap<uint32, uint32> States;
    TMap<uint32, FString> Text;
    TMap<uint32, float> Meters;
    TMap<uint32, float> Positions;
    uint32 Hovered = 0, Pressed = 0;
    void Begin(FIntPoint Viewport);
    bool Root(uint32 Layout, const TCHAR *Name);
    bool Template(uint32 Layout, const TCHAR *Name, FIntRect Rect, FIntRect Clip, uint32 ParentId = 0);
    bool Template(uint32 Layout, uint32 ElementId, FIntRect Rect, FIntRect Clip, uint32 ParentId = 0);
    const FAC2NativeNode *Find(const TCHAR *Name) const;
    const FAC2NativeNode *Find(uint32 Id) const;
    const FAC2NativeNode *Descendant(const FAC2NativeNode &Parent, const TCHAR *Name) const;
    FIntRect Bounds(const TCHAR *Name) const;
    void ConstrainRoot(const TCHAR *Name, FIntRect Area);
    TArray<int32> PaintOrder() const;
    bool CursorAt(FVector2D Point, uint32& Image, FIntPoint& Hotspot) const;
    void Prepare(FAC2AssetLibrary &Assets, FAC2Fonts &Fonts);
    int32 Paint(FSlateWindowElementList &, int32 Layer, const FGeometry &, FAC2Fonts &,
                TFunction<int32(const FAC2NativeNode &, int32)> Content = {}) const;
    int32 PaintText(const FAC2NativeNode &, const FString &, FSlateWindowElementList &, int32,
                    const FGeometry &, FAC2Fonts &) const;
    const FSlateBrush *Brush(uint32 Id) const
    {
        return Images.Find(Id);
    }
    void LoadImage(uint32 Id, FAC2AssetLibrary &Assets);
    int32 MissingImages = 0;
    // Pure placement, used by both runtime and boundary tests. Input rectangles are inclusive DAT boxes.
    static FIntRect Place(const FAC2UIElement &, FIntRect Original, FIntPoint AuthoredParent,
                          FIntPoint ParentSize);

  private:
    FAC2DatArchive Local;
    FIntPoint Display = FIntPoint(1024, 768);
    TMap<uint64, FString> Strings;
    TMap<uint32, FSlateBrush> Images;
    TSet<uint32> AttemptedImages;
    bool Add(const FAC2UILayout &, int32 Element, int32 Parent, FIntPoint OriginalParent,
             uint32 InheritedState, const FIntRect *Override = nullptr, const FIntRect *Clip = nullptr,
             int32 Depth = 0);
};
