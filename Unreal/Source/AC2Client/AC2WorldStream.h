#pragma once
#include "AC2Assets.h"
#include "Async/Future.h"

class UWorld;
class UProceduralMeshComponent;
class FAC2WorldStream
{
  public:
    FAC2WorldStream();
    ~FAC2WorldStream();
    bool Open(UWorld *World, const FString &Directory, FString &Error);
    bool Destination(uint32 Block, FVector &Position, FString &Error);
    void Tick(const FVector &Position);
    void Shutdown();
    bool ReadyAt(const FVector &Position) const;
    bool PlacementReadyAt(const FVector &Position) const;
    void SetInteriorCell(uint32 Cell);
    bool NetworkLanding(const FVector& Feet, uint32 Cell, float CapsuleRadius, float HalfHeight,
                        FVector& Center, const AActor* Ignore) const;
    bool ExteriorVisible() const;
    bool InteriorLighting(const FVector& Viewer,FAC2PreviewLighting& Lighting) const;
    bool CellVisible(uint32 Cell) const;
    bool LandingPosition(const FVector &Preferred, FVector &Position, float CapsuleRadius, float HalfHeight) const;
    bool SpawnPosition(const FVector &Preferred, uint32 InteriorOwner, FVector &Position, float CapsuleRadius = 34,
                       float HalfHeight = 103, const AActor *Ignore = nullptr) const;
    bool FailedAt(const FVector &Position) const;
    bool GroundAt(const FVector &Position, double &Height) const;
    bool WaterAt(const FVector &Position, double &Height) const;
    bool InteriorAt(const FVector &Position, uint32 &Block) const;
    bool InteriorCellAt(const FVector& Feet,uint32& Cell) const;
    void SetDrawRadius(int32 Radius);
    bool CellPosition(uint32 Id, FVector &Position) const;
    int32 InteriorCount() const;
    int32 WaterCount() const;
    TArray<FAC2ParticleSource> ParticleSources(const FVector &Viewer) const;
    bool AllReady() const;
    FString Status() const;
    int32 Radius = 2;
    int32 LoadedTotal = 0, UnloadedTotal = 0, DiscardedTotal = 0;
    int32 ResidentCount() const;
    int32 SceneryCount() const;
    int32 GrassBatchCount() const;
    bool GrassDepthReady() const;
    const FAC2LandscapeDefs &Landscape() const;
    void SetTerrainCollision(bool Enabled);

  private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
