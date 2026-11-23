#pragma once
#include "AC2MapData.h"
#include "AC2NativeUI.h"
#include "AC2RadarData.h"
class AAC2GameMode;

class FAC2MapUI
{
  public:
    bool Open(const FString &Directory, FString &Error);
    void Update(AAC2GameMode &Mode, FIntPoint Size, FAC2AssetLibrary &Assets, FAC2Fonts &Fonts);
    int32 Paint(FSlateWindowElementList &, int32, const FGeometry &, FAC2Fonts &) const;
    void Move(FVector2D Point);
    bool Press(FVector2D Point, bool Right, bool Shift);
    bool Release(FVector2D Point, bool Right, bool Shift);
    bool DoubleClick(FVector2D Point);
    bool Wheel(FVector2D Point, float Delta);
    bool OwnsPointer(FVector2D Point) const { return Contains(Point); }
    bool CursorAt(FVector2D Point, uint32& Image,FIntPoint& Hotspot) const
    { return Contains(Point) && Native.CursorAt(Point,Image,Hotspot); }
    void Close()
    {
        Overview = 0;
        Pressed = 0;
        Dragging = false;
        OverviewDragging = false;
        Native.Pressed = 0;
        LastClickLand.Reset();
    }
    bool Validate(FString &Error) const;
    bool SmokeStep(int32 Step, FString &Error);
    bool SmokeHeading(float Yaw,FString& Error) const;
    bool SmokeNetworkTarget(const FString &Id, FString &Error);
    static FIntRect RadarPlacement(const FAC2UILayout &Layout, FIntPoint Display);
    void SetRadarOptions(uint32 Mask,bool Coordinates) { RadarMask=Mask&63;Coords=Coordinates;Tooltip.Reset(); }
    uint32 GetRadarMask() const { return RadarMask; }
    bool ShowCoordinates() const { return Coords; }
    TFunction<void(bool)> OnCoordinatesChanged;

  private:
    friend class SAC2GameUI;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2OptionsTest;
#endif
    FAC2MapData Data;
    FAC2RadarData RadarData;
    uint32 RadarMask=AC2Radar::DefaultMask;
    FAC2NativeUI Native;
    FAC2DatArchive Local;
    TMap<uint32, FString> ButtonTips;
    AAC2GameMode *Mode = nullptr;
    FIntPoint Display = FIntPoint(1280, 720);
    FIntRect MapRect, RadarRect, OverviewRect;
    FVector2D Origin = FVector2D(321, 195), Extent = FVector2D(1720, 1720);
    FVector2D MacroOrigin, MacroExtent, LastLand = FVector2D(692, 828), PlayerPixel;
    FVector2D Mouse = FVector2D(-1, -1), PressPoint, DragOrigin;
    TOptional<FVector2D> LastClickLand;
    FVector2D LastClickPoint;
    double LastClickTime = 0;
    bool Ready = false, Detail = false, Follow = false, Coords = true, PlayerVisible = false;
    bool Dragging = false, Moved = false;
    bool OverviewDragging = false;
    TOptional<FVector2D> OverviewOrigin;
    FVector2D OverviewDragOrigin;
    uint32 Overview = 0, Pressed = 0;
    uint32 HeadingImage = 0;
    float Heading = 0;
    FVector2D RadarCenter=FVector2D(71,69);
    float RadarRadius=60;
    struct FPicture
    {
        uint32 Image;
        FVector2D Point, Size;
    };
    struct FMarker
    {
        uint32 Image;
        FVector2D Point;
        FString Label;
    };
    struct FContact
    {
        FVector2D Point;
        FString Id, Name;
        TWeakObjectPtr<AActor> Actor;
        bool Selected;
        AC2Radar::FLook Look;
    };
    TArray<FPicture> Pictures;
    TArray<FMarker> Markers;
    TArray<FContact> Contacts;
    FString Tooltip;
    FString SelectedNote;
    int32 NoteScroll = 0;
    void Zoom(FVector2D Center);
    void Macro();
    uint32 ButtonAt(FVector2D Point) const;
    bool Contains(FVector2D Point) const;
    void Activate(uint32 Id, bool Shift);
};
