#pragma once
#include "AC2Water.h"

// CFogDesc / LScapeLighting::GetFogCorners. Distances in the DAT are metres;
// the viewer is in Unreal world centimetres, matching the outdoor block lattice.
class FAC2RegionFog
{
public:
    bool Load(FAC2DatArchive& Portal,uint32 DescId,FString& Error);
    bool Decode(uint32 DescId,TConstArrayView<uint8> Bytes,uint32& DistanceMapId);
    bool DecodeMap(uint32 MapId,TConstArrayView<uint8> Bytes);
    FAC2SkyKey Apply(const FVector& ViewerCm,const FAC2SkyKey& Day) const;

private:
    FAC2WaterLightKey Default;
    TMap<uint32,FAC2WaterLightKey> Types;
    uint32 Width=0,Height=0;
    TArray<uint8> Values;
    const FAC2WaterLightKey& At(uint32 X,uint32 Y) const;
};
