#pragma once
#include "AC2DatArchive.h"

struct FAC2MapNote
{
    uint32 Image = 0;
    FIntPoint Cell;
    FString Label;
};
struct FAC2MapTile
{
    uint32 Value = 0, Image = 0;
    FIntRect Cells;
    TArray<uint32> Notes;
};
// CRegionDesc, CDetailMapDesc and CMapNoteDesc. Coordinates are land cells, not block IDs.
class FAC2MapData
{
  public:
    uint32 Image = 0;
    TArray<FAC2MapTile> Tiles;
    TMap<uint32, TArray<FAC2MapNote>> Notes;
    bool Open(const FString &Directory, FString &Error);
    const FAC2MapTile *At(FIntPoint Cell) const;
    static constexpr int32 LandCells = 2040;
    static FVector2D Landscape(const FVector &World)
    {
        return FVector2D(World.X, World.Y) / 2000.;
    }
    static FVector2D Project(FVector2D Cell, FVector2D Origin, FVector2D Extent, FIntRect Pixels);
    static FVector2D Unproject(FVector2D Pixel, FVector2D Origin, FVector2D Extent, FIntRect Pixels);
    static FVector2D DetailOrigin(FVector2D Center);
    static FVector2D RadarOffset(FVector Delta, float ActorYaw, float RangeMetres);
    static FString Coordinates(FVector2D Cell);

  private:
    uint32 Width = 0, Height = 0;
    TArray<uint8> Blocks;
};
