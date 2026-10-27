#pragma once
#include "AC2Appearance.h"
#include "AC2Particle.h"
#include "AC2Terrain.h"
#include "AC2Water.h"
#include "AC2MotionData.h"
#include "UObject/GCObject.h"

class UMaterialInterface;
class USceneComponent;
class AActor;

struct FAC2AssetStats
{
    bool HighResAvailable = false;
    int32 FrillsGenerated = 0, FrillsResolved = 0, FrillBatches = 0, FrillBatchesUploaded = 0;
    int32 GrassDepthSections = 0, GrassSortedSections = 0;
    int32 Textures = 0;
    int32 HighResSurfaces = 0;
    int32 TerrainMaterials = 0;
    int32 Entities = 0;
    int32 ProceduralEntities = 0;
    int32 UnsupportedSceneGroups = 0;
    int32 ScenerySections = 0;
    int32 SceneryTriangles = 0;
    int32 SkippedEntities = 0;
    int32 MissingMaterials = 0;
    int32 InteriorCells = 0, WaterSections = 0;
};

struct FAC2UISceneInfo
{
    FTransform Camera;
    TMap<uint32, FTransform> Locations;
};

// Owns this loaded patch's decoded resources and GC references. Never writes DATs.
class FAC2AssetLibrary : public FGCObject
{
  public:
    FAC2AssetLibrary();
    virtual ~FAC2AssetLibrary();
    bool Open(const FString &Directory, FString &Error);
    // Open on the game thread, prepare with worker-owned handles, then upload on
    // the game thread. A library is never read and written by two threads at once.
    bool PrepareBlock(const FString &Directory, const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
                      const FAC2TerrainMesh &Mesh, FString &Error);
    UMaterialInterface *UploadTerrain();
    bool UploadScenery(AActor *Owner, USceneComponent *Root, int32 MeshBudget = 4);
    void ReleaseCPUData();
    bool CollisionReady() const;
    void InteriorSpawnPoints(const FVector &Near, float Radius, TArray<FVector> &Out) const;
    bool DrawSky(uint32 ObjectId, AActor *Owner, USceneComponent *Root);
    bool DrawModel(uint32 ObjectId, AActor *Owner, USceneComponent *Root,
                   const FAC2Appearance &Appearance = {});
    uint32 AppearanceFingerprint() const;
    bool DrawUIScene(uint32 Id, AActor *Owner, USceneComponent *Root, FAC2UISceneInfo *Info = nullptr);
    bool HoldingLocation(uint32 Object, uint32 Location, FTransform &Out);
    void Animate(float Time, float Speed, const FVector *LocalViewer = nullptr,
                 const FVector *LocalVelocity = nullptr);
    void SetAnimationState(uint32 Element, uint32 Mode);
    bool PlayBehavior(uint32 Behavior, uint32 Mode = 0, float Rate = 1, bool Hold = false);
    void StopBehavior(uint32 Behavior = 0);
    const FAC2MotionData &MotionData() const;
    bool SetMotionDescriptor(uint32 Id);
    bool AuditAttachments(FString &Error) const;
    void UpdateMaterials(float Time);
    void RetainRuntimeMaterials(const TArray<UMaterialInterface *> &Materials);
    int32 AnimationCount() const;
    int32 MovementAnimationUpdates() const;
    bool AuditLocomotion(uint32 Visual, FString &Details);
    bool ValidateGrassMaterials(int32 &Count);
    UMaterialInterface *ParticleMaterial(uint32 Id);
    UMaterialInterface *Material(uint32 Id, bool Water = false);
    class UTexture2D *Texture(uint32 Id);
    TArray<FColor> TerrainPalette();
    FAC2BlockWater Water() const;
    const TArray<FAC2ParticleSource> &ParticleSources() const;
    bool InteriorAt(const FVector &Position) const;
    bool InteriorCellAt(const FVector& Feet,uint32& Cell) const;
    bool InteriorPositions(uint32 CellId, TArray<FVector> &LocalPositions) const;
    UMaterialInterface *TerrainMaterial(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
                                        const FAC2TerrainMesh &Mesh);
    void AddScenery(FAC2DatArchive &Cell, const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
                    AActor *Owner, USceneComponent *Root, const FVector &Offset);
    const FAC2AssetStats &Stats() const;
    virtual void AddReferencedObjects(FReferenceCollector &Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("FAC2AssetLibrary");
    }

  private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

// Creates project-owned shader templates; DAT pixels stay transient at runtime.
bool PrepareAC2Materials(FString &Error);
