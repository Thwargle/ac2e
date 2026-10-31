#pragma once
#include "AC2Terrain.h"

struct FAC2BlockWater
{
    bool Present = false;
    uint32 Above = 0, Below = 0, Color = 0xffffffff;
    float Height = 0, Tiling = 10;
};
// Shared near/distant surface geometry: native height and world-aligned UVs.
void AppendAC2WaterMesh(uint32 Block, const FAC2BlockWater& Water,
                       const FVector& Offset, FAC2TerrainMesh& Mesh);
class FAC2WaterSource
{
  public:
    bool Load(FAC2DatArchive &Portal, FString &Error);
    FAC2BlockWater At(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land) const;
    bool Decode(TConstArrayView<uint8> Bytes, uint32 &HeightMap, uint32 &TypeMap);

  private:
    struct FMap
    {
        uint32 H = 0, W = 0;
        TArray<uint8> Values;
        uint8 At(uint32 X, uint32 Y) const;
        bool Decode(TConstArrayView<uint8> Bytes, uint32 Id);
    };
    FMap Heights, Types;
    FAC2BlockWater Default;
    TMap<uint32, FAC2BlockWater> WaterTypes;
};
