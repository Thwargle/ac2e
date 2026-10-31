#pragma once
#include "AC2Assets.h"
#include "Async/Future.h"
class UWorld;
class FAC2FarTerrain
{
  public:
    FAC2FarTerrain();
    ~FAC2FarTerrain();
    bool Open(UWorld *World, const FString &Directory, FString &Error);
    void Tick(const FVector &Viewer, int32 NearRadius, bool NearReady);
    int32 TileCount() const;
    int32 WaterTileCount() const;
    int32 WaterBatchCount() const;
    uint32 Center() const;
    void SetVisible(bool Visible);

  private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
