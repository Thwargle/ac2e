#pragma once
#include "AC2DatArchive.h"

namespace AC2Radar
{
enum : uint32 { Monsters=1, NPCs=2, Players=4, Portals=8, Lifestones=16, Items=32, DefaultMask=31 };
struct FLook
{
    uint32 Category=0, Shape=0x4000000b;
    FLinearColor Color=FLinearColor::White;
    bool Selectable=true;
    bool Visible(uint32 Mask) const { return Selectable && (Category&Mask) && Shape>=0x40000001 && Shape<=0x4000000a; }
};
// Pixel patterns from gmRadarUI::DrawBlip / DrawSelected, Legions 7160.
TArray<FIntPoint> Pixels(uint32 Shape, bool Selected=false);
FLook DefaultLook(uint32 Category);
FLook Resolve(FLook Look,int64 Health,TOptional<uint32> Shape,TOptional<bool> Selectable,TOptional<uint32> Color);
}

// Classify through the installed script inheritance, never localized names or
// model IDs. EntityDesc overrides are inherited before radar filtering.
class FAC2RadarData
{
  public:
    bool Open(const FString& Directory,FString& Error);
    bool DecodeClasses(TConstArrayView<uint8> Bytes);
    uint32 Category(uint32 Package) const;
    AC2Radar::FLook Look(uint32 Entity);
  private:
    FAC2DatArchive Portal;
    TMap<uint32,uint32> Parents,Types;
    TMap<uint32,AC2Radar::FLook> Looks;
    AC2Radar::FLook ReadEntity(uint32 Entity,TSet<uint32>& Seen);
};
