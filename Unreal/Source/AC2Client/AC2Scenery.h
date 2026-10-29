#pragma once
#include "AC2Terrain.h"
#include "AC2Water.h"

struct FAC2SceneryPlacement
{
    uint32 Object = 0;
    FTransform Transform;
    bool Frill = false;
    // Only zero-offset, land-relative frills may fit their cards to the ground.
    // Water, frame-relative and deliberately elevated decorations keep their authored height.
    bool GroundFrill = false;
};

// Native deterministic SceneFile groups with bounded client frills.
class FAC2SceneryGenerator
{
  public:
    explicit FAC2SceneryGenerator(FAC2DatArchive &Portal);
    ~FAC2SceneryGenerator();
    bool Initialize(FString &Error);
    void Generate(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land, const TArray<FBox> &PlacedBounds,
                  TArray<FAC2SceneryPlacement> &Out, const FAC2BlockWater &Water = {});
    int32 UnsupportedGroups = 0;

  private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

namespace AC2
{
double SceneRandom(uint32 X, uint32 Y);
double PlacementRandom(uint32 X, uint32 Y, uint32 Key);
FQuat ScenePlaneRotation(const FVector &Normal);
} // namespace AC2
