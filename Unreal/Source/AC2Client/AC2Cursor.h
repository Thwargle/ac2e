#pragma once
#include "AC2NativeUI.h"
#include "Widgets/SLeafWidget.h"

// MD_Data_Cursor / Media_Cursor: image plus an authored pixel hotspot, not a centered sprite.
struct FAC2CursorIcon
{
    uint32 Image = 0x41000870;
    FIntPoint Hotspot = FIntPoint::ZeroValue;
};

class SAC2Cursor : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2Cursor) {} SLATE_ARGUMENT(FString, Directory)
    SLATE_ATTRIBUTE(FAC2CursorIcon, Icon) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1,1); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
        FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
    bool Ready() const { return Images.Brush(0x41000870)!=nullptr; }
  private:
    TAttribute<FAC2CursorIcon> Icon;
    FAC2AssetLibrary Assets;
    FAC2NativeUI Images;
};
