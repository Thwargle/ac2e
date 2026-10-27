#pragma once
#include "AC2DatArchive.h"

struct FAC2LandscapeDefs
{
    float CellSize = 0;
    uint32 CellsPerBlock = 0;
    uint32 Subdivisions = 0;
    uint32 TerrainMask = 0;
    TArray<float> Heights;
    bool Decode(TConstArrayView<uint8> Bytes, FString &Error);
    float BlockSize() const
    {
        return CellSize * CellsPerBlock;
    }
};

struct FAC2LandBlock
{
    uint32 Id = 0;
    uint32 InfoId = 0;
    uint32 PathId = 0;
    TArray<uint8> Heights;
    TArray<uint32> CellInfo;
    bool Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes, FString &Error);
};

struct FAC2TerrainMesh
{
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
};

// Heights and topology from CLandBlockData, with metres converted to UE centimetres.
// Mesh-local X/Y follow the archive axes. Materials and scenery are supplied by AC2Assets.
FAC2TerrainMesh BuildAC2TerrainMesh(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block);
