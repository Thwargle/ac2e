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
// Rendering supplies a one-block halo so shared vertices use the same complete
// triangle fan for lighting and slope-based material selection.
TMap<uint32, FAC2LandBlock> LoadAC2TerrainNeighborhood(FAC2DatArchive& Cell, uint32 Center, int32 Radius);
FAC2TerrainMesh BuildAC2TerrainMesh(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                                  const TMap<uint32, FAC2LandBlock>* Neighborhood = nullptr);

// Block-local metres, on the same triangle/diagonal used by BuildAC2TerrainMesh.
// Cave mouths and positions outside this block have no ground sample.
bool SampleAC2Terrain(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                      double X, double Y, double &Height, FVector &Normal);

// Render-only fitting of an authored grass card to the terrain beneath its base.
// Keeps its horizontal coordinates and height above the base; never creates instances.
FVector FitAC2GroundFrill(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                         const FTransform &Frame, const FVector &Vertex, double BaseZ);

// Barycentric triangle list split at terrain grid/diagonal boundaries. Fitting
// only the original card corners would still leave its edges bridging a crease.
TArray<FVector> SplitAC2GroundFrill(const FAC2LandBlock &Block, const FTransform &Frame,
                                  const FVector &A, const FVector &B, const FVector &C, double BaseZ);
