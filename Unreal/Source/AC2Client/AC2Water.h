#pragma once
#include "AC2Terrain.h"

struct FAC2SkyKey;
struct FAC2WaterLightKey
{
    float Depth=0,FogMin=0,FogMax=100,FogSky=0,FogDensity=0,Direct=0,Ambient=0;
    uint32 FogType=0,Combine=0;
    FLinearColor FogColor,DirectColor,AmbientColor;
    FVector2D Multipliers[7]{};
};
struct FAC2BlockWater
{
    bool Present = false;
    uint32 Above = 0, Below = 0, Color = 0xffffffff;
    float Height = 0, Tiling = 10;
    TArray<FAC2WaterLightKey> Lighting;
};
// Native LightFogInfo combination, shared by region corners and water depth keys.
FAC2SkyKey AC2CombineFogLighting(const FAC2WaterLightKey& Fog,const FAC2SkyKey& Day);
// LScapeLighting::CalcWaterLight: combine each depth key with daylight, then interpolate.
FAC2SkyKey AC2WaterLighting(const FAC2BlockWater& Water,float Depth,const FAC2SkyKey& Day);
// Shared near/distant surface geometry: native height and world-aligned UVs.
void AppendAC2WaterMesh(uint32 Block, const FAC2BlockWater& Water,
                       const FVector& Offset, FAC2TerrainMesh& Mesh);
struct FAC2ShoreWaterMesh : FAC2TerrainMesh
{
    TArray<FVector2D> ShoreUVs;
    FAC2TerrainMesh Strip;
    // Native strip phase and fraction of the bank-to-water width.
    TArray<FVector2D> StripUVs;
    // Separate native ribbons keep affine UVs through banks that bend or fold.
    FAC2TerrainMesh Foam;
    TArray<FVector2D> FoamMaskUVs;
    // X is the ten-metre pattern coordinate, Y selects the foam pass.
    TArray<FVector2D> FoamPassUVs;
};
// Derive the shore from the actual DAT triangles, never from camera depth.
void AppendAC2ShoreWaterMesh(uint32 Block,const FAC2BlockWater& Water,const FVector& Offset,
    const FAC2LandscapeDefs& Defs,const TMap<uint32,FAC2LandBlock>& Neighborhood,FAC2ShoreWaterMesh& Mesh,
    FAC2DatArchive* Cell=nullptr);
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
