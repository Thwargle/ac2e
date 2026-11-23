#pragma once
#include "AC2Appearance.h"
#include "AC2PreviewLighting.h"
#include "AC2Particle.h"
#include "AC2Terrain.h"
#include "AC2Scenery.h"
#include "AC2ScenerySound.h"
#include "AC2Water.h"
#include "AC2MotionData.h"
#include "UObject/GCObject.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class AActor;

struct FAC2TextureSource
{
    uint32 Texture = 0, Surface = 0;
    FIntPoint DecodedSize = FIntPoint::ZeroValue, UploadedSize = FIntPoint::ZeroValue;
    bool HighResolution = false;
};

struct FAC2AssetStats
{
    bool HighResAvailable = false;
    int32 FrillsGenerated = 0, FrillsResolved = 0, FrillBatches = 0, FrillBatchesUploaded = 0;
    int32 GrassDepthSections = 0, GrassSortedSections = 0;
    int32 GrassGroundContacts = 0;
    double GrassMaxRootGapBefore = 0, GrassMaxRootGapAfter = 0; // centimetres, base vertices and edges
    TArray<FAC2TextureSource> GrassTextures;
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
    int32 AcademyPlacementRepairs = 0;
    uint64 AnimationEffectTriggers = 0;
};

struct FAC2UISceneInfo
{
    FTransform Camera;
    float VerticalFOV = 65;
    bool Orthographic = false;
    float OrthoHeight = 100;
    FAC2PreviewLighting Lighting;
    TMap<uint32, FTransform> Locations;
};
struct FAC2IconLayer
{
    uint32 Layer=0, Image=0, Color=0xffffffff;
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
    void SetActiveCell(uint32 Cell);
    bool CellVisible(uint32 Cell) const;
    bool CellReachesOutside(uint32 Cell) const;
    void InteriorSpawnPoints(const FVector &Near, float Radius, TArray<FVector> &Out) const;
    bool DrawSky(uint32 ObjectId, AActor *Owner, USceneComponent *Root);
    bool DrawModel(uint32 ObjectId, AActor *Owner, USceneComponent *Root,
                   const FAC2Appearance &Appearance = {});
    uint32 AppearanceFingerprint() const;
    // Posed, rendered triangles in model-root coordinates, excluding invisible DAT helpers.
    // Optional MeshType uses the native MeshType enum (feet/head/body etc.).
    FBox ModelBounds(uint32 MeshType = 0) const;
    // Selection scenes have no physics collision. Pick the visible, posed mesh
    // using a world-space segment; Fraction is comparable across scaled models.
    bool HitModel(const FVector& Start, const FVector& End, double& Fraction) const;
    // Static network props keep solid surfaces; animated bodies and FX do not
    // become collision walls. Movement remains server owned.
    int32 EnableWorldObjectCollision();
    bool HasLocomotion() const;
    TArray<FAC2IconLayer> IconLayers(uint32 Visual, const FAC2Appearance& Appearance = {});
    bool DrawUIScene(uint32 Id, AActor *Owner, USceneComponent *Root, FAC2UISceneInfo *Info = nullptr);
    // Call before drawing a preview, and again when its camera light changes.
    void SetPreviewLighting(const FAC2PreviewLighting& Lighting, const FTransform& SceneToWorld);
    bool HoldingLocation(uint32 Object, uint32 Location, FTransform &Out);
    bool AnimatedHoldingLocation(uint32 Location, FTransform &Out) const;
    void Animate(float Time, float Speed,
                 const FVector *LocalVelocity = nullptr);
    void EnableCreatureIdleAudio();
    void SetAnimationState(uint32 Element, uint32 Mode);
    // Model-local contact normal and native look sliders, independent of body scale.
    void SetAnimationEnvironment(bool Grounded,const FVector& GroundNormal,const FVector& Look=FVector::ZeroVector);
    bool PlayBehavior(uint32 Behavior, uint32 Mode = 0, float Rate = 1, bool Hold = false);
    void StopBehavior(uint32 Behavior = 0);
    void ReleaseBehavior(uint32 Behavior);
    void StopAnimationLayer(uint32 Layer);
    float BehaviorDuration(uint32 Behavior) const;
    bool CompletedHoldCycles(uint32 Behavior,uint32 Cycles) const;
    TArray<float> StoryHookDelays(uint32 Behavior) const;
    void SeekBehavior(uint32 Behavior,float Elapsed);
    // Identifies this particular playing action, including replacement by the
    // same behavior. Zero means absent; independent animation layers keep IDs.
    uint64 BehaviorInstance(uint32 Behavior) const;
    bool HandlingItem() const;
    bool ReleasedHeldItem(uint32 Holding) const;
    bool HoldingStreakActive(uint32 Holding) const;
    void SetHeldStreakActive(bool Active);
    float LocomotionScale() const;
    float JumpDelay() const;
    bool PlayEffect(uint32 Effect,float Strength=1,USceneComponent* Target=nullptr);
    bool DamageDecal(FAC2Decal& Out);
    // Native BloodyDamaged FX selects material stages from missing health.
    void SetHealth(float Current, float Maximum);
    uint32 DamageAppearanceFingerprint() const;
    bool PlayEffectScript(uint32 Script);
    void StopEffect(uint32 Effect);
    int32 EffectParticleCount() const;
    static void SetEffectChannel(uint32 Channel,uint32 Value);
    static bool EffectChannelsMatch(const TMap<uint32,uint32>& Channels);
    bool HasStartupEffects() const;
    const FAC2MotionData &MotionData() const;
    bool SetMotionDescriptor(uint32 Id);
    bool AuditAttachments(FString &Error) const;
    // Compare authored shared seam vertices with the actual posed mesh sections.
    bool AttachmentSeamGap(uint32 MeshType, int32& MatchedVertices, double& Gap) const;
    void UpdateMaterials(float Time);
    void RetainRuntimeMaterials(const TArray<UMaterialInterface *> &Materials);
    int32 AnimationCount() const;
    int32 MovementAnimationUpdates() const;
    bool AuditLocomotion(uint32 Visual, FString &Details);
    // Offline inspection only: enumerate renderer record families through the
    // runtime decoders. Does not imply that all decoded semantics are rendered.
    TSharedPtr<class FJsonObject> AuditRenderRecords(FString &Error);
    bool ValidateGrassMaterials(int32 &Count);
    UMaterialInterface *ParticleMaterial(uint32 Id);
    bool ParticleMaterialUsesLighting(uint32 Id);
    UMaterialInterface *Material(uint32 Id, bool Water = false, bool Below = false);
    class UTexture2D *Texture(uint32 Id);
    TArray<FColor> TerrainPalette();
    TArray<FColor> TerrainPixels(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
        const FAC2TerrainMesh &Mesh, int32 Resolution);
    FAC2BlockWater Water() const;
    bool LightingCellAt(const FVector& Position,uint32& Cell) const;
    FAC2PreviewLighting InteriorLighting(uint32 Cell,const FVector& LocalViewer) const;
    void InteriorFog(FAC2SkyKey& Fog) const;
    const TArray<FAC2ParticleSource> &ParticleSources() const;
    const TArray<FAC2ScenerySound> &SoundSources() const;
    TArray<FAC2AmbientPoint> AmbientPoints(bool Landscape) const;
    bool InteriorAt(const FVector &Position) const;
    bool InteriorCellAt(const FVector& Feet,uint32& Cell) const;
    bool InteriorPositions(uint32 CellId, TArray<FVector> &LocalPositions) const;
    void ConfigureTerrainCloud(UMaterialInstanceDynamic* Material);
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
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2AppearanceRampTest;
    friend class FAC2AttachmentStreakTest;
#endif
    bool PlayModelEffect(uint32 Effect,float Strength,USceneComponent* Target,bool CreatureIdle);
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

// Creates project-owned shader templates; DAT pixels stay transient at runtime.
bool PrepareAC2Materials(FString &Error);
