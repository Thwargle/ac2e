#include "AC2Assets.h"
#include "AC2Sky.h"
#include "AC2Fog.h"
#include <atomic>
#include "AC2CellConnectivity.h"
#include "AC2Animation.h"
#include "AC2Effects.h"
#include "AC2EffectLight.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/PlayerController.h"
#include "AC2StaticCollision.h"
#include "AC2AcademyRepairs.h"
#include "AC2AssetReader.h"
#include "AC2MaterialAlpha.h"
#include "AC2NativeMaterial.h"
#include "AC2TextureMips.h"
#include "AC2CellLighting.h"
#include "AC2Audio.h"
#include "AC2FXScalar.h"
#include "AC2AttachmentStreak.h"
#include "AC2GameMode.h"
#include "AC2Scenery.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "AC2CharacterData.h"
#endif

namespace
{
// FXManager::ChannelMissmatch: only channels set by preferences participate.
// Native gore choices: Fun=1, None=2, Low=3, Normal=4; hue Green=1, Red=2.
TMap<uint32,uint32> EffectChannels={{0x40000001,4},{0x40000007,2}};
uint32 EffectChannelRevision=0;
struct FAC2DecodedImage
{
    uint32 Id = 0, SurfaceId = 0;
    bool HighResolution = false;
    int32 W = 0, H = 0;
    TArray<FColor> Pixels;
    // RenderTexture::m_SourceLevels, indexed relative to the selected top surface.
    // Where a level is present both masked and ordinary samplers use its original alpha.
    TArray<TArray<FColor>> SourceMips;
    TArray<TArray<FColor>> Mips;
    TArray<TArray<FColor>> CutoutMips;
    TMap<uint32,UTexture2D*> Samplers;
    FVector2D CachedAlpha=FVector2D(-1,-1);
    FVector2D AlphaRange() {
        if(CachedAlpha.X<0) {
            uint8 Low=255,High=0;
            for(const auto &P:Pixels){Low=FMath::Min(Low,P.A);High=FMath::Max(High,P.A);}
            CachedAlpha=Pixels.IsEmpty()?FVector2D(1,1):FVector2D(Low/255.f,High/255.f);
        }
        return CachedAlpha;
    }
    FLinearColor Sample(double U, double V) const
    {
        return AC2SampleNativeRGB(Pixels,W,H,U,V);
    }
};
struct FAC2DecodedWeighted
{
    uint32 Id = 0;
    float Weight = 0, Tile = 1;
};
struct FAC2DecodedBand
{
    float Min = 0, Max = 1, Total = 0;
    FLinearColor Tint = FLinearColor::White;
    TArray<FAC2DecodedWeighted> Base;
};
struct FAC2DecodedField
{
    uint32 Kind = 0, Layer = 0, Index = 0;
};
struct FAC2DecodedProperty
{
    uint32 Name = 0;
    TArray<uint8> Data;
    TArray<FAC2DecodedField> Fields;
};
struct FAC2UVModifier
{
    uint32 Kind = 0, Index = 0;
    FAC2Wave U, V;
    FMatrix Matrix = FMatrix::Identity;
};
struct FAC2DecodedLayer
{
    uint32 SourceBlend = 0, DestBlend = 0, TrueFlags=0, FalseFlags=0, CullMode=2;
    bool PixelShader=false, ParticleLighting=false;
    bool DepthFog=false, DepthAlphaFog=false, DiffuseFog=false;
    TArray<uint32> Textures;
    TArray<FAC2AlphaStage> AlphaStages, ColorStages;
    TArray<uint32> TextureCoordinates, SpecialTextures;
    TArray<bool> ProjectedCoordinates;
    TArray<FIntPoint> AddressModes;
    TArray<FAC2UVModifier> Modifiers;
    FLinearColor Tint = FLinearColor::White;
    FLinearColor Ambient = FLinearColor::White;
    FLinearColor Specular = FLinearColor::White;
    FAC2Wave SpecularPower;
    bool DynamicLighting = false, IgnoreDirectional = false, IgnorePoint = false;
    bool Masked = false, SpecularHighlights = false;
    bool MaterialDiffuse = false, VertexDiffuse = false, ModulateVertexDiffuse = false;
};
struct FAC2DecodedMaterial
{
    float AppearanceOpacity = 1.f;
    TArray<FAC2DecodedLayer> Layers;
    TArray<FAC2DecodedProperty> Properties;
};
struct FAC2DecodedEntity
{
    uint32 Data = 0, Type = 0;
    FVector Origin = FVector::ZeroVector, Scale = FVector::OneVector;
    FQuat Rotation = FQuat::Identity;
    FString Name;
    TMap<uint32, TArray<uint8>> Properties;
};
struct FAC2FXNode
{
    FAC2EffectLight Light;
    FAC2Decal Decal;
    FAC2RepulsorFX Repulsor;
    FAC2StreakDefinition Streak;
    bool StartStreak = false, StopStreak = false;
    TMap<uint32,uint32> Channels;
    TMap<uint32, TSharedPtr<FAC2FXNode>> Terrain;
    TArray<uint32> Descriptors, Sounds;
    uint32 Holding = 0, Bone = 0;
    uint32 Visual=0,Behavior=0,TargetFX=0,Script=0;
    uint32 AppearanceTable=0,AppearanceKey=0;
    float AppearanceScalar=1;
    float Lifetime=0, RampTime=0;
    bool Parent=false,Retarget=false,FireAndForget=false;
    FVector Offset = FVector::ZeroVector;
    bool Water = false, RemoveRotation = false;
    bool DayTime=true,NightTime=true,PlayerOnly=false;
};
struct FAC2DecodedPart
{
    uint32 Setup = 0, Key = 0, Parent = 0, Fields = 0;
    uint32 Connection = 0, AnimMap = 0, FXTable = 0;
    FAC2Appearance Appearance;
    TArray<uint32> Startup;
    TMap<uint32,float> StartupStrength;
    TMap<uint32, TArray<FAC2FXNode>> Overrides;
};
struct FAC2MatAtom
{
    float Priority = 0;
    uint32 Template = 0, Modifier = 0, MeshType = 0, MaterialType = 0;
};
struct FAC2MeshAtom
{
    float Priority = 0;
    uint32 Type = 0, Mesh = 0;
};
struct FAC2PartAtom
{
    float Priority = 0;
    FAC2DecodedPart Part;
};
struct FAC2Atoms
{
    TArray<FVector2f> Opacities; // priority, authored opacity (including -999)
    FAC2AppearanceOpacity Opacity;
    TArray<FAC2IconLayer> Icons;
    TArray<FAC2MatAtom> Materials;
    TArray<FAC2MeshAtom> Meshes;
    TArray<FAC2PartAtom> Parts;
    TMap<uint32, TArray<FAC2FXNode>> Effects;
};
struct FAC2AtomCollection
{
    float Intensity = 0;
    FAC2Atoms Atoms;
};
struct FAC2AppearanceTable
{
    TArray<uint32> Bases;
    TMap<uint32, TArray<FAC2AtomCollection>> Collections;
};
struct FAC2DecodedVisual
{
    TArray<FAC2IconLayer> Icons;
    uint32 Parent = 0, MotionDesc = 0, BehaviorTable = 0;
    FVector Scale = FVector::OneVector;
    bool LocalizedSign = false;
    TArray<FAC2DecodedPart> Parts;
    FAC2DecodedPart FX;
    float ParticleScale = 1;
};
struct FAC2DecodedFragment
{
    uint32 Material = 0;
    TArray<int32> Indices;
    uint32 OriginalMaterial=0;
    TSharedPtr<FAC2Atoms> BaseAtoms;
};
struct FAC2DecodedGeometry
{
    uint32 Type = 0;
    struct FInfluence
    {
        uint8 Bones[4] = {};
        float Weights[4] = {};
    };
    TArray<FInfluence> Skin;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs, DetailUVs, UV2, UV3;
    TArray<FVector2D> FrillBend; // normalized root-to-tip weight, plant height in cm
    TArray<FColor> Colors;
    TArray<FAC2DecodedFragment> Fragments;
    bool NativeCollision = false;
    TArray<int32> PhysicsIndices;
};

TArray<FAC2DecodedProperty> Properties(FAC2AssetReader &R)
{
    TArray<FAC2DecodedProperty> Result;
    const int32 Count = R.Count(16);
    for (int32 I = 0; I < Count && R.Good; ++I)
    {
        FAC2DecodedProperty P;
        P.Name = R.Word();
        R.Word();
        const int32 N = R.Count(1), Start = R.Pos;
        if (R.Take(N))
            P.Data.Append(R.Data.GetData() + Start, N);
        const int32 Fields = R.Count(16);
        for (int32 J = 0; J < Fields; ++J)
        {
            FAC2DecodedField F;
            F.Kind = R.Word();
            R.Word();
            F.Layer = R.Word();
            F.Index = R.Word();
            P.Fields.Add(F);
        }
        Result.Add(MoveTemp(P));
    }
    return Result;
}

FLinearColor Color(FAC2AssetReader &R)
{
    const float A = R.Float(), B = R.Float(), C = R.Float(), D = R.Float();
    return FLinearColor(A, B, C, D);
}
FTransform Transform(FAC2AssetReader &R)
{
    const FVector P = R.Vector() * 100, S = R.Vector();
    const FQuat Q = R.Quaternion();
    return FTransform(Q, P, S);
}
uint32 Random(uint32 X, uint32 Y, uint32 K)
{
    return K * 0x62433D7D - (X * Y * Y * K * K * 0x2BD38901 + 0x6B3C5F11) * X + Y * uint32(-0x4A6506DB);
}
} // namespace

struct FAC2AssetLibrary::FImpl
{
    bool PreviewMode = false;
    FLinearColor InteriorAmbient=FLinearColor::Black;
    // CLandBlockInfo defaults: no indoor fog, white colour, zero range.
    uint32 InteriorFogType=0;
    FLinearColor InteriorFogColor=FLinearColor::White;
    float InteriorFogNear=0, InteriorFogFar=0;
    TArray<FAC2CellLight> CellLights;
    bool DamageDecalsLoaded=false;
    TArray<FAC2FXNode> DamageDecals;
    TArray<TPair<int32,TWeakObjectPtr<UPointLightComponent>>> RoomLights;
    bool RoomLightsUploaded=false;
    FAC2PreviewLighting PreviewLighting;
    FTransform PreviewToWorld;
    void LightPreview(UMaterialInstanceDynamic* M)
    {
        M->SetVectorParameterValue(TEXT("AC2UIAmbient"), PreviewLighting.Ambient);
        FLinearColor Previous;
        if(M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AC2AnisotropicLight")),Previous)){
            const auto* Sun=PreviewLighting.Lights.FindByPredicate([](const auto& L){return L.Directional && L.Intensity>0;});
            const FVector Direction=Sun?-PreviewToWorld.TransformVectorNoScale(Sun->Direction).GetSafeNormal():FVector::ZeroVector;
            M->SetVectorParameterValue(TEXT("AC2AnisotropicLight"),FLinearColor(Direction));
        }
        for (int32 I = 0; I < 8; ++I)
        {
            FVector P = FVector::ZeroVector;
            FLinearColor C(0, 0, 0, 0);
            float Range = 0;
            if (PreviewLighting.Lights.IsValidIndex(I))
            {
                const auto& L = PreviewLighting.Lights[I];
                P = L.Directional ? -PreviewToWorld.TransformVectorNoScale(L.Direction).GetSafeNormal()
                                  : PreviewToWorld.TransformPosition(L.Position);
                Range = L.Directional ? 0 : FMath::Max(L.Range, .02f);
                C = L.Diffuse * L.Intensity;
            }
            M->SetVectorParameterValue(*FString::Printf(TEXT("AC2UILight%d"), I), FLinearColor(P.X, P.Y, P.Z, Range));
            M->SetVectorParameterValue(*FString::Printf(TEXT("AC2UIColor%d"), I), C);
        }
    }
    FAC2Appearance CharacterAppearance;
    TMap<uint32, TSharedPtr<FAC2AppearanceTable>> AppearanceTables;
    uint32 NextMaterialVariant = 0xfe000000, AppearanceHash = 0;
    struct FAppearanceMaterialBinding
    {
        TWeakObjectPtr<UProceduralMeshComponent> Mesh;
        int32 Section=0;
        uint32 Base=0,Original=0,MeshType=0,Applied=0;
        TSharedPtr<FAC2Atoms> Atoms;
    };
    TArray<FAppearanceMaterialBinding> AppearanceBindings;
    TMap<uint32,TArray<FAC2AppearanceKey>> LiveAppearanceFX;
    TMap<uint32,float> AppearanceFXStrengths;
    TMap<FString,uint32> AppearanceFXMaterials;
    uint32 SeenEffectChannels=0,DamageAppearanceHash=0;
    float LastHealthFraction=-1;

    struct FDraw
    {
        TSharedPtr<FAC2DecodedGeometry> Mesh;
        FTransform Transform;
        bool Solid = false;
        bool Frill = false;
        int32 AnimationGroup = -1, Bone = 0;
        bool Structural = false;
        uint32 Cell = 0;
    };
    TArray<FDraw> Draws;
    TArray<TWeakObjectPtr<UProceduralMeshComponent>> CollisionMeshes;
    struct FCellMesh { TWeakObjectPtr<UProceduralMeshComponent> Mesh; uint32 Cell; bool Solid; };
    TArray<FCellMesh> CellMeshes;
    FAC2CellConnectivity Cells;
    TSet<uint32> VisibleCells;
    uint32 DrawingCell = 0, ActiveCell = MAX_uint32;
    bool ShowExterior = true;
    bool CellVisible(uint32 Cell) const
    { return ActiveCell == MAX_uint32 || (Cell ? VisibleCells.Contains(Cell) : ShowExterior); }
    FAC2DecodedImage TerrainAtlas;
    int32 NextDraw = 0;
    bool Preparing = false, SkyMode = false;
    bool Animating = false, Decoration = false;
    bool GroundDecoration = false;
    struct FAnimatedMesh
    {
        TSharedPtr<FAC2DecodedGeometry> Data;
        UProceduralMeshComponent *Component = nullptr;
        int32 Bone = 0;
    };
    struct FAnimatedGroup
    {
        FAC2Setup Setup;
        FTransform Frame;
        // Preserve the full affine attachment chain until it reaches vertices.
        // FTransform composition cannot retain rotation followed by unequal scales.
        FMatrix PartFrame=FMatrix::Identity;
        TArray<FMatrix> BindInverse, World, Bones;
        TArray<FAnimatedMesh> Meshes;
        bool Skinned = false;
        TSharedPtr<FAC2Animation> Idle;
        TSharedPtr<FAC2Animator> Locomotion;
        TMap<uint32,TSharedPtr<FAC2Animator>> Graphs;
        uint32 Map=0;
        FAC2AnimatorInput Motion;
        double Phase = 0, IdlePhase = 0;
        FAC2AnimationTriggerState IdleTriggers, MoveTriggers;
        float MoveWeight = 0;
        TArray<FTransform> PreviousMotionPose;
        TArray<FTransform> RenderedPose,TransitionPose;
        float TransitionTime=0,TransitionDuration=0;
        int32 Parent = -1;
        FAC2Setup::FSocket Socket;
    };
    TArray<FAnimatedGroup> Animated;
    FAC2AnimatorState AnimatorState;
    bool CreatureIdleAudio=false;
    float NextIdleSoundRoll=0;
    FVector AnimationNormal=FVector::UpVector,AnimationLook=FVector::ZeroVector;
    bool AnimationGrounded=false;
    FAC2MotionData Movement;
    FAC2BehaviorData Behaviors;
    TOptional<TSet<uint32>> HoldBehaviors;
    bool IsHoldBehavior(uint32 Behavior)
    {
        if(!HoldBehaviors.IsSet())
        {
            HoldBehaviors.Emplace();
            // CharTemplate::GetBehavior (7160 006244A0) reads global Behavior flags;
            // the similarly named per-body BehaviorInfo supplies ramp and layer metadata.
            constexpr uint32 CharTemplateId=0x2f000000;
            TArray<uint8> Bytes;
            if(Read(CharTemplateId,Bytes))
            {
                FAC2AssetReader R(MoveTemp(Bytes));
                if(R.Word()==CharTemplateId)
                {
                    const uint32 Count=R.Word()&0x00ffffff;
                    if(Count<=uint32(R.Left()/12))
                        for(uint32 I=0;I<Count;++I)
                        {
                            const uint32 Id=R.Word(),Flags=R.Word();R.Word();
                            if(Flags&2)HoldBehaviors->Add(Id);
                        }
                }
            }
        }
        return HoldBehaviors->Contains(Behavior);
    }
    uint32 Element=AC2Motion::Ground, Mode=AC2Motion::Peace;
    // BehaviorTracker keeps independent tracks per native layer. Damage reactions
    // must not replace an attack on another layer (Legions BehaviorTracker::Think).
    struct FActionTrack
    {
        uint32 Behavior=0,Key=0,Layer=0,Mode=0,Element=0;
        double Time=0,Previous=0;
        float Duration=0,Rate=1,Ramp=.1f;
        bool Hold=false;
        uint64 Instance=0;
        FAC2HoldLoop Loop;
    };
    TArray<FActionTrack> Actions;
    void BlendFromCurrent(float Seconds)
    {
        for(auto& G:Animated)
        {G.TransitionPose=G.RenderedPose;G.TransitionTime=0;G.TransitionDuration=Seconds;}
    }
    TSharedPtr<FAC2Animator> Graph(FAnimatedGroup& G,uint32 Key)
    {
        if(!Key)return nullptr;
        if(!G.Graphs.Contains(Key))G.Graphs.Add(Key,FAC2Animator::Load(Portal,G.Map,Key));
        return G.Graphs[Key];
    }
    struct FAppearanceAttachment
    {
        int32 Group;
        FAC2FXNode Node;
        uint64 Key;
        uint32 Descriptor;
    };
    TArray<FAppearanceAttachment> AppearanceAttachments;
    struct FNetworkEffect { FAppearanceAttachment Attachment; uint32 Effect; TWeakObjectPtr<USceneComponent> Target; FTransform BirthFrame; };
    TArray<FNetworkEffect> NetworkEffects;
    struct FVisualEffect {FAppearanceAttachment Attachment;uint32 Effect=0;float Age=0,Life=2;TWeakObjectPtr<AActor> Actor;TSharedPtr<FAC2AssetLibrary> Model;TWeakObjectPtr<USceneComponent> Target;};
    TArray<FVisualEffect> VisualEffects;
    struct FLightEffect { FAC2FXNode Node; uint32 Effect=0; float Age=0; TWeakObjectPtr<UPointLightComponent> Light; };
    TArray<FLightEffect> LightEffects;
    TArray<TSharedPtr<FAC2ParticleRepulsor>> Repulsors;
    TArray<FAC2AttachmentStreak> Streaks;
    struct FStreakController {uint32 Effect; uint64 Action;};
    TMap<uint32,FStreakController> StreakControllers; // keyed by native holding location
    uint64 StreakAction = 0;
    FVector StreakModelScale = FVector::OneVector;
    bool HeldStreakActive = false;
    bool ApplyStreak(const FAC2FXNode& Node, uint32 Effect)
    {
        if (Node.StartStreak) StreakControllers.Add(Node.Holding, {Effect, StreakAction});
        if (Node.StopStreak) StreakControllers.Remove(Node.Holding);
        if (!Node.Holding && (Node.StartStreak || Node.StopStreak))
            for (auto& Streak : Streaks) Streak.Activate(HeldStreakActive || !Node.StopStreak);
        if (Node.Streak.Enabled && Streaks.Num() < 16)
        {
            auto& Trail = Streaks.AddDefaulted_GetRef();
            Trail.Definition = Node.Streak;
            Trail.Effect = Effect;
            Trail.Created = PreviousAnimationTime;
            Trail.Activate(Node.Streak.Life > 0 || HeldStreakActive || StreakControllers.Contains(0));
            return true;
        }
        return Node.StartStreak || Node.StopStreak;
    }
    void ReleaseEndedStreakControllers()
    {
        // Interrupting an attack can skip its authored stop cue.
        for (auto It = StreakControllers.CreateIterator(); It; ++It)
        {
            const uint64 Owner = It.Value().Action;
            if (!Owner || Actions.ContainsByPredicate([Owner](const FActionTrack& A){return A.Instance == Owner;})) continue;
            if (!It.Key()) for (auto& Trail : Streaks) Trail.Activate(HeldStreakActive);
            It.RemoveCurrent();
        }
    }
    bool StartRepulsor(const FAC2FXNode& Node)
    {
        if (!Node.Repulsor.Enabled || PreviewMode || Repulsors.Num() >= 64) return false;
        auto Field = FAC2ParticleRepulsor::Register(ModelRoot.Get(), Node.Repulsor);
        if (!Field) return false;
        Repulsors.Add(MoveTemp(Field));
        return true;
    }
    bool StartLight(const FAC2FXNode& Node,uint32 Effect)
    {
        if(!Node.Light.Enabled || PreviewMode || !ModelRoot.IsValid() || LightEffects.Num()>=64)return false;
        auto* Owner=ModelRoot->GetOwner();
        auto* Light=NewObject<UPointLightComponent>(Owner);
        Owner->AddInstanceComponent(Light);Light->SetupAttachment(ModelRoot.Get());
        Light->SetMobility(EComponentMobility::Movable);
        // DoLightSourceFX stores the FX offset on the source object (not the
        // effect's visual mesh or the victim). Model scaling applies once.
        Light->SetRelativeLocation(Node.Offset*100);
        Light->bUseInverseSquaredFalloff=false;Light->LightFalloffExponent=2;
        Light->SetAttenuationRadius(FMath::Max(0.f,Node.Light.Range.Value(0))*100);
        Light->SetIntensity(FMath::Max(0.f,Node.Light.Intensity.Value(0))*4*Node.Light.Envelope(0));
        Light->SetLightColor(FLinearColor::FromSRGBColor(Node.Light.Color));
        Light->SetLightingChannels(true,true,false);
        Light->SetCastShadows(true);Light->ShadowBias=.25f;Light->SourceRadius=5;
        Light->ShadowResolutionScale=.5f;
        Light->RegisterComponent();RegisterAC2EffectLight(Light);
        LightEffects.Add({Node,Effect,0,Light});return true;
    }
    struct FScriptEffect {FAC2FXScript Script;float Phase=0;int32 Depth=0;};
    TArray<FScriptEffect> ScriptEffects;
    int32 EffectDepth=0;
    uint64 EffectSerial=0x8000000000000000ULL;
    uint32 ModelVisual=0;
    float ModelParticleScale=1;
    TMap<uint32, TArray<FAC2FXNode>> AppearanceEffects;
    TMap<uint32, TArray<FAC2FXNode>> StartupEffects;
    TMap<uint32,float> StartupStrength;
    struct FScalarEffect
    {
        TArray<uint32> Contexts;
        TArray<FAC2FXNode> Nodes;
        FAC2FXScalar Scalar;
        bool Startup=false;
    };
    TMap<uint32,FScalarEffect> ScalarEffects;
    TMap<uint32,bool> LoopingSounds;
    TMap<uint32,uint32> SoundChannels;
    TUniquePtr<FAC2Effects> AttachedEffects;
    bool HasLoopingParticles(uint32 Effect) const
    {
        return AttachedEffects && NetworkEffects.ContainsByPredicate([&](const auto& Child){
            return Child.Effect==Effect && AttachedEffects->LoopingSource(Child.Attachment.Key);
        });
    }
    TWeakObjectPtr<USceneComponent> ModelRoot;
    FTransform EffectFrame(const FAC2FXNode& Node) const
    {
        FTransform At(Node.Offset*100);
        for(const auto& G:Animated)
        {
            if(Node.Holding)
            {const auto* S=G.Setup.Sockets.Find(Node.Holding);if(!S || !G.Bones.IsValidIndex(S->Bone))continue;
                At=At*FTransform(S->Frame.ToMatrixWithScale()*G.Bones[S->Bone])*G.Frame;break;}
            if(Node.Bone)
            {int32 B=G.Setup.Names.IndexOfByKey(Node.Bone);if(!G.Bones.IsValidIndex(B))continue;At=At*FTransform(G.Bones[B])*G.Frame;break;}
        }
        if(ModelRoot.IsValid())At=At*ModelRoot->GetComponentTransform();
        if(Node.RemoveRotation)At.SetRotation(FQuat::Identity);
        return At;
    }
    FString Directory;
    float PreviousAnimationTime = 0;
    int32 MovingPoseUpdates = 0;
    FAC2DatArchive Portal, HighRes, Local;
    bool HasHighRes = false, HasLocal = false;
    FString TextureNamespace;
    FAC2AssetStats Stats;
    TMap<uint32, uint32> TerrainSurfaces, PropertyTypes;
    uint32 CloudShadowTexture = 0;
    FLinearColor CloudShadowMotion = FLinearColor::Black;
    uint32 DefaultSurface = 0;
    TMap<uint32, TArray<FAC2DecodedBand>> Surfaces;
    TMap<uint32, TSharedPtr<FAC2DecodedImage>> Images;
    TMap<uint32, TSharedPtr<FAC2DecodedMaterial>> Materials;
    struct FShoreMaterials { uint32 Waves = 0, Transmission = 0; };
    TOptional<FShoreMaterials> ShoreMaterials;
    TMap<uint32, TSharedPtr<FAC2DecodedGeometry>> Geometry;
    TMap<uint32, FAC2DecodedVisual> Visuals;
    TArray<FAC2ParticleSource> Sources;
    TArray<FAC2ScenerySound> Sounds;
    struct FMovingMaterial
    {
        UMaterialInstanceDynamic *Material;
        TArray<FAC2UVModifier> Modifiers;
        FAC2Wave SpecularPower;
    };
    TArray<FMovingMaterial> MovingMaterials;
    void TrackMaterial(uint32 Id, UMaterialInterface *Material)
    {
        auto Decoded = this->Material(Id);
        auto *MID = Cast<UMaterialInstanceDynamic>(Material);
        if (MID && Decoded && !Decoded->Layers.IsEmpty()) {
            const auto& L=Decoded->Layers[SelectedLayer(*Decoded)];
            if(!L.Modifiers.IsEmpty() || L.SpecularPower.Kind!=1 || L.SpecularPower.BaseSpeed!=0)
                MovingMaterials.Add({MID,L.Modifiers,L.SpecularPower});
        }
    }
    TMap<uint64, UMaterialInterface *> RuntimeMaterials;
    TSet<uint32> InvisibleMaterials;
    TArray<TObjectPtr<UObject>> Objects;
    TSet<uint32> Warnings;
    UMaterialInterface *Opaque = nullptr;
    UMaterialInterface *Cutout = nullptr;
    int64 ImageBytes = 0;
    TUniquePtr<FAC2SceneryGenerator> Scenery;
    TArray<FBox> PlacedBounds;
    FAC2WaterSource WaterSource;
    FAC2BlockWater BlockWater;
    const FAC2LandBlock *EnvironmentLand = nullptr;
    const FAC2LandscapeDefs *EnvironmentDefs = nullptr;
    FVector EnvironmentOffset = FVector::ZeroVector;
    uint32 EnvironmentTerrainMask = 0;
    TMap<uint32, TArray<FVector>> InteriorPositions;
    TMap<uint32,FVector> InteriorSoundOrigins;
    TArray<FAC2AmbientPoint> LandscapeSounds;
    TMap<uint32, FBox> InteriorBounds;
    TMap<uint32,TArray<FVector>> InteriorFloorTriangles;

    bool Read(uint32 Id, TArray<uint8> &Bytes)
    {
        FString Error;
        if (HasLocal && ((Id >= 0x40100000 && Id <= 0x40ffffff) || (Id >= 0x41100000 && Id <= 0x41ffffff)) &&
            Local.Read(Id, Bytes, Error))
            return true;
        if (Portal.Read(Id, Bytes, Error))
            return true;
        Warn(Id, TEXT("record unavailable"));
        return false;
    }
    void Warn(uint32 Id, const TCHAR *Why)
    {
        if (Warnings.Contains(Id))
            return;
        Warnings.Add(Id);
        UE_LOG(LogTemp, Warning, TEXT("AC2 resource %08X: %s"), Id, Why);
    }
    bool Metadata(FString &Error)
    {
        TArray<uint8> B;
        if (!Read(0x12000000, B))
            return false;
        FAC2AssetReader T(MoveTemp(B));
        if (T.Word() != 0x12000000)
            return false;
        T.Word();
        auto Terrain = [&]() {
            T.String();
            const uint32 Surface = T.Word(), Index = T.Word();
            T.Float();
            if (T.Word())
                T.Take(8);
            const int32 N = T.Count(8);
            T.Take(int64(N) * 8);
            TerrainSurfaces.Add(Index, Surface);
            return Surface;
        };
        if (T.Word())
            DefaultSurface = Terrain();
        const int32 NT = T.Count(22, 4096);
        for (int32 I = 0; I < NT && T.Good; ++I)
            Terrain();
        if (!T.End())
        {
            Error = TEXT("Unsupported TerrainDesc layout");
            return false;
        }
        if (!Read(0x13000000, B))
            return false;
        FAC2AssetReader S(MoveTemp(B));
        S.Word();
        if (S.Word() != 0x13000000)
            return false;
        S.Take(12); // far terrain texture, scale and far water material
        CloudShadowTexture = S.Word();
        CloudShadowMotion.R = S.Float(); // repeats per world metre
        CloudShadowMotion.G = S.Float(); // U and V drift in repeats per second
        CloudShadowMotion.B = S.Float();
        for (int32 List = 0; List < 2 && S.Good; ++List)
        {
            const int32 NS = S.Count(12, 4096);
            for (int32 I = 0; I < NS && S.Good; ++I)
            {
                const uint32 Surface = S.Word();
                TArray<FAC2DecodedBand> Bands;
                const int32 NG = S.Count(88, 4096);
                for (int32 J = 0; J < NG && S.Good; ++J)
                {
                    FAC2DecodedBand Band;
                    Band.Min = S.Float();
                    Band.Max = S.Float();
                    S.Take(16);
                    const int32 NC = S.Count(48, 4096);
                    for (int32 C = 0; C < NC; ++C)
                    {
                        S.Word();
                        const uint32 Tint = S.Word();
                        if (C == 0)
                            Band.Tint = FLinearColor(FColor(Tint));
                        S.Take(40);
                    }
                    S.Float();
                    for (int32 L = 0; L < 7; ++L)
                    {
                        const int32 NW = S.Count(12, 4096);
                        for (int32 K = 0; K < NW; ++K)
                        {
                            FAC2DecodedWeighted W;
                            W.Id = S.Word();
                            W.Weight = S.Float();
                            W.Tile = S.Float();
                            if (!L)
                                Band.Base.Add(W);
                        }
                        const float Total = S.Float();
                        if (!L)
                            Band.Total = Total;
                    }
                    Bands.Add(MoveTemp(Band));
                }
                S.Word();
                if (!List)
                    Surfaces.Add(Surface, MoveTemp(Bands));
            }
        }
        if (!S.End())
        {
            Error = TEXT("Unsupported SurfaceDesc layout");
            return false;
        }
        if (!Read(0x34000000, B))
            return false;
        FAC2AssetReader P(MoveTemp(B));
        if (P.Word() != 0x34000000)
            return false;
        P.Take(8);
        const int32 Names = P.Count(6);
        for (int32 I = 0; I < Names && P.Good; ++I)
        {
            P.Word();
            P.String();
        }
        const uint32 Count = P.Word() & 0xffff;
        for (uint32 I = 0; I < Count && P.Good; ++I)
        {
            const uint32 Name = P.Word(), Type = P.Word();
            P.Take(16);
            PropertyTypes.Add(Name, Type);
        }
        if (!P.End())
        {
            Error = TEXT("Unsupported MasterProperty layout");
            return false;
        }
        UE_LOG(LogTemp, Verbose, TEXT("AC2 assets: %d terrain types, %d surfaces, %d property types"),
               TerrainSurfaces.Num(), Surfaces.Num(), PropertyTypes.Num());
        return true;
    }

    static void ApplyMaterialProperty(FAC2DecodedMaterial& Material, const FAC2DecodedProperty& Reference,
                                      const TArray<uint8>& Data)
    {
        for (const FAC2DecodedField &F : Reference.Fields)
        {
            if (!Material.Layers.IsValidIndex(F.Layer))
                continue;
            FAC2DecodedLayer &L = Material.Layers[F.Layer];
            FAC2AssetReader V(Data);
            if (F.Kind == 10 && L.Textures.IsValidIndex(F.Index) && V.Left() >= 4)
                L.Textures[F.Index] = V.Word();
            if (F.Kind == 1 && V.Left() >= 16)
                L.Tint = Color(V);
            if (F.Kind == 0 && V.Left() >= 16)
                L.Ambient = Color(V);
            // MaterialProperty::Apply: RMFIELD_LAYER_SPECULAR / SPECULARPOWER.
            if (F.Kind == 2 && V.Left() >= 16)
                L.Specular = Color(V);
            if (F.Kind == 3 && V.Left() >= 44)
                L.SpecularPower = FAC2Wave::Read(V);
            if ((F.Kind==11 || F.Kind==12) && L.AddressModes.IsValidIndex(F.Index) && V.Left()>=4) {
                const int32 Address=V.Word()?1:3;
                if(F.Kind==11)L.AddressModes[F.Index].X=Address; else L.AddressModes[F.Index].Y=Address;
            }
            if (F.Kind == 8 && V.Left() >= 4)
                L.Masked = V.Word() != 0;
            // FIELD_CULL_MODE is the authored "two sided" boolean, not
            // the D3D enum stored directly in MaterialLayer::StreamPack.
            if (F.Kind == 5 && V.Left() >= 4)
                L.CullMode = V.Word() ? 1 : 2;
            if (F.Kind >= 33 && F.Kind <= 37 && L.Modifiers.IsValidIndex(F.Index) && V.Left() >= 44)
            {
                auto &Modifier = L.Modifiers[F.Index];
                if (F.Kind == 34 || F.Kind == 37)
                    Modifier.V = FAC2Wave::Read(V);
                else
                    Modifier.U = FAC2Wave::Read(V);
            }
        }
    }
    bool ModifyMaterial(TSharedPtr<FAC2DecodedMaterial> M, uint32 Mod)
    {
        TArray<uint8> B;
        if (!Read(Mod, B))
            return false;
        FAC2AssetReader MR(MoveTemp(B));
        if (MR.Word() != Mod)
            return false;
        auto Overrides = Properties(MR);
        if (!MR.End())
            return false;
        for (const FAC2DecodedProperty &O : Overrides)
        {
            const FAC2DecodedProperty *P =
                M->Properties.FindByPredicate([&](const FAC2DecodedProperty &V) { return V.Name == O.Name; });
            if (P)
                ApplyMaterialProperty(*M, *P, O.Data);
        }
        return true;
    }

    TSharedPtr<FAC2DecodedMaterial> Material(uint32 Id)
    {
        if (auto *Cached = Materials.Find(Id))
            return *Cached;
        Materials.Add(Id, nullptr);
        TArray<uint8> B;
        if (!Read(Id, B))
            return nullptr;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Id)
            return nullptr;
        if ((Id >> 24) == 0x31)
        {
            const uint32 Template = R.Word();
            R.Word();
            const int32 N = R.Count();
            TArray<uint32> Mods;
            for (int32 I = 0; I < N; ++I)
                Mods.Add(R.Word());
            if (!R.End() || (Template >> 24) != 0x2b)
                return nullptr;
            auto Base = Material(Template);
            if (!Base)
                return nullptr;
            auto M = MakeShared<FAC2DecodedMaterial>(*Base);
            for (uint32 Mod : Mods)
                if (!ModifyMaterial(M, Mod))
                    return nullptr;
            // Legions scenery bird 1F000003 has single-sheet wings at every
            // mesh LOD, but 31000005 inherits one-sided culling. Repair only
            // that wing material so its native feather texture renders below
            // the bird too; the body and other ordinary materials stay culled.
            if (Id == 0x31000005)
                for (auto& Layer : M->Layers)
                    Layer.CullMode = 1;
            Materials[Id] = M;
            return M;
        }
        if ((Id >> 24) != 0x2b)
            return nullptr;
        R.Word();
        auto M = MakeShared<FAC2DecodedMaterial>();
        M->Properties = Properties(R);
        const int32 NL = R.Count(32, 64);
        for (int32 I = 0; I < NL && R.Good; ++I)
        {
            FAC2DecodedLayer L;
            L.TrueFlags=R.Word(); L.FalseFlags=R.Word();
            const int32 NS = R.Count(56, 16);
            for (int32 J = 0; J < NS; ++J)
            {
                L.Textures.Add(R.Word());
                L.ColorStages.Add({R.Word(), R.Word(), R.Word()});
                FAC2AlphaStage Alpha;
                Alpha.Operation = R.Word();
                Alpha.Argument1 = R.Word();
                Alpha.Argument2 = R.Word();
                L.AlphaStages.Add(Alpha);
                const uint32 AU=R.Word(), AV=R.Word(); L.AddressModes.Add(FIntPoint(AU,AV));
                R.Take(8); // min/mag filters (all installed stages use linear)
                L.TextureCoordinates.Add(R.Word());
                L.ProjectedCoordinates.Add(R.Word()!=0);
                L.SpecialTextures.Add(R.Word());
            }
            const int32 NM = R.Count(8, 256);
            for (int32 J = 0; J < NM && R.Good; ++J)
            {
                FAC2UVModifier Modifier;
                Modifier.Kind = R.Word();
                Modifier.Index = R.Word();
                if (Modifier.Kind == 4 || Modifier.Kind == 6)
                {
                    Modifier.U = FAC2Wave::Read(R);
                    Modifier.V = FAC2Wave::Read(R);
                }
                else if (Modifier.Kind == 5)
                    Modifier.U = FAC2Wave::Read(R);
                else if (Modifier.Kind == 7)
                    for (int32 Row = 0; Row < 4; ++Row)
                        for (int32 Col = 0; Col < 4; ++Col)
                            Modifier.Matrix.M[Row][Col] = R.Float();
                else
                    R.Good = false;
                L.Modifiers.Add(MoveTemp(Modifier));
            }
            L.SourceBlend = R.Word();
            L.DestBlend = R.Word();
            R.Take(8); // depth test/write
            L.CullMode = R.Word();
            const uint32 AlphaMode = R.Word();
            L.Masked = AlphaMode != 0 && AlphaMode != 8;
            R.Take(44); // alpha-test waveform
            L.ParticleLighting = R.Word() != 0;
            L.DynamicLighting = R.Word() != 0;
            R.Word(); // per-pixel lighting
            L.IgnoreDirectional = R.Word() != 0;
            L.IgnorePoint = R.Word() != 0;
            L.SpecularHighlights = R.Word() != 0;
            R.Word(); // per-pixel specular
            L.MaterialDiffuse = R.Word() != 0;
            L.VertexDiffuse = R.Word() != 0;
            L.ModulateVertexDiffuse = R.Word() != 0;
            R.Word(); // per-pixel multiple lights
            L.DepthFog=R.Word()!=0;
            L.DepthAlphaFog=R.Word()!=0;
            L.DiffuseFog=R.Word()!=0;
            L.Ambient = Color(R);
            L.Tint = Color(R);
            L.Specular = Color(R);
            L.SpecularPower = FAC2Wave::Read(R);
            R.Take(20);
            int32 Shader = R.Count(1, 1024 * 1024);
            R.Take(Shader);
            Shader = R.Count(1, 1024 * 1024);
            L.PixelShader=Shader>0;
            R.Take(Shader);
            M->Layers.Add(MoveTemp(L));
        }
        R.Word();
        if (!R.End())
        {
            Warn(Id, TEXT("unsupported material layout"));
            return nullptr;
        }
        // RenderMaterial::GetSubObjects calls MaterialProperty::Apply with
        // each property's own data before MaterialInstance applies overrides.
        for (const auto& P : M->Properties)
            ApplyMaterialProperty(*M, P, P.Data);
        Materials[Id] = M;
        return M;
    }

    TSharedPtr<FAC2DecodedImage> Image(uint32 Id)
    {
        if (auto *Cached = Images.Find(Id))
            return *Cached;
        Images.Add(Id, nullptr);
        TArray<uint32> SurfacesToTry;
        if ((Id >> 24) == 0x41)
            SurfacesToTry.Add(Id);
        else
        {
            TArray<uint8> B;
            if (!Read(Id, B))
                return nullptr;
            FAC2AssetReader R(MoveTemp(B));
            if (R.Word() != Id || (Id >> 24) != 0x40)
                return nullptr;
            R.Word();
            const int32 N = R.Count(4, 32);
            for (int32 I = 0; I < N; ++I)
                SurfacesToTry.Add(R.Word());
            if (!R.End())
                return nullptr;
        }
        struct FSurfaceLevel
        {
            uint32 Id;
            bool HighResolution;
            TArray<FColor> Pixels;
        };
        TMap<FIntPoint, FSurfaceLevel> Levels;
        FIntPoint Largest(0, 0);
        int64 RetainedBytes = 0;
        constexpr int64 ImageBudget = 512LL * 1024 * 1024;
        for (uint32 Surface : SurfacesToTry)
        {
            // Compare every valid candidate, including portal fallback if a highres
            // record is missing, corrupt or smaller than the base surface.
            for (int32 Pass = 0; Pass < 2; ++Pass)
            {
                if (!Pass && !HasHighRes)
                    continue;
                FAC2DatArchive &Archive =
                    !Pass ? HighRes : (Surface >= 0x41100000 && HasLocal ? Local : Portal);
                FAC2DatEntry Entry;
                FString E;
                TArray<uint8> B;
                if (!Archive.Find(Surface, Entry, E) || !Archive.Read(Surface, B, E))
                    continue;
                int32 W = 0, H = 0;
                TArray<FColor> Pixels;
                if (!DecodeAC2Surface(B, Surface, W, H, Pixels))
                {
                    Warn(Surface, TEXT("unsupported texture pixels; trying fallback"));
                    continue;
                }
                const FIntPoint Size(W, H);
                const bool HighResolution = Pass == 0;
                const auto* Existing = Levels.Find(Size);
                if (!Existing || (HighResolution && !Existing->HighResolution))
                {
                    if (!Existing)
                        RetainedBytes += int64(Pixels.Num()) * sizeof(FColor);
                    if (RetainedBytes > ImageBudget)
                    {
                        Warn(Id, TEXT("texture source levels exceed memory limit"));
                        return nullptr;
                    }
                    Levels.Add(Size, FSurfaceLevel{Surface, HighResolution, MoveTemp(Pixels)});
                    if (int64(W) * H > int64(Largest.X) * Largest.Y)
                        Largest = Size;
                }
            }
        }
        if (Levels.IsEmpty())
            return nullptr;
        auto Best = MakeShared<FAC2DecodedImage>();
        Best->Id = Id;
        auto UseSurface = [&](FIntPoint Size, FSurfaceLevel& Source)
        {
            Best->W = Size.X;
            Best->H = Size.Y;
            Best->Pixels = MoveTemp(Source.Pixels);
            Best->SurfaceId = Source.Id;
            Best->HighResolution = Source.HighResolution;
        };
        UseSurface(Largest, Levels.FindChecked(Largest));
        while (Best->W > 2048 || Best->H > 2048)
        {
            const FIntPoint Next(FMath::Max(1, Best->W / 2), FMath::Max(1, Best->H / 2));
            if (auto* Source = Levels.Find(Next))
                UseSurface(Next, *Source);
            else
                Downsample(*Best);
        }
        if (SkyMode && Id == 0x400003e7)
        {
            // The horizon matte is white with a vertical opacity ramp. The DAT
            // encodes intermediate opacity with a repeating 4-bit DXT3 dither.
            // On a full-resolution dome the individual dither rows become
            // conspicuous. Reconstruct their coverage before magnification.
            TArray<int64> Prefix;
            Prefix.Add(0);
            for (int32 Y = 0; Y < Best->H; ++Y)
            {
                int64 Sum = Prefix.Last();
                for (int32 X = 0; X < Best->W; ++X)
                    Sum += Best->Pixels[Y * Best->W + X].A;
                Prefix.Add(Sum);
            }
            TArray<FColor> Ramp;
            Ramp.Reserve(Best->H);
            const int32 Radius = FMath::Max(1, Best->H / 256);
            for (int32 Y = 0; Y < Best->H; ++Y)
            {
                const int32 First = FMath::Max(0,Y - Radius), End = FMath::Min(Best->H,Y + Radius + 1);
                const int32 Samples = (End - First) * Best->W;
                Ramp.Add(FColor(255,255,255,(Prefix[End] - Prefix[First] + Samples / 2) / Samples));
            }
            Best->Pixels = MoveTemp(Ramp);
            Best->W = 1;
            Levels.Reset();
        }
        int64 Bytes = int64(Best->Pixels.Num()) * sizeof(FColor);
        FIntPoint Size(Best->W, Best->H);
        while (Size.X > 1 || Size.Y > 1)
        {
            Size = FIntPoint(FMath::Max(1, Size.X / 2), FMath::Max(1, Size.Y / 2));
            auto& Mip = Best->SourceMips.AddDefaulted_GetRef();
            if (auto* Source = Levels.Find(Size))
            {
                Mip = MoveTemp(Source->Pixels);
                Bytes += int64(Mip.Num()) * sizeof(FColor);
            }
        }
        if (ImageBytes + Bytes > ImageBudget)
        {
            Warn(Id, TEXT("patch texture memory limit reached"));
            return nullptr;
        }
        ImageBytes += Bytes;
        Images[Id] = Best;
        ++Stats.Textures;
        if (Best->HighResolution)
            ++Stats.HighResSurfaces;
        return Best;
    }

    static void Downsample(FAC2DecodedImage &Image)
    {
        const int32 W = FMath::Max(1, Image.W / 2), H = FMath::Max(1, Image.H / 2);
        TArray<FColor> Next;
        Next.SetNumUninitialized(W * H);
        ParallelFor(
            TEXT("AC2Mip"), H, 32,
            [&](int32 Y) {
                for (int32 X = 0; X < W; ++X)
                {
                    FLinearColor Sum(0, 0, 0, 0);
                    for (int32 DY = 0; DY < 2; ++DY)
                        for (int32 DX = 0; DX < 2; ++DX)
                            Sum += FLinearColor(Image.Pixels[FMath::Min(Y * 2 + DY, Image.H - 1) * Image.W +
                                                             FMath::Min(X * 2 + DX, Image.W - 1)]);
                    Next[Y * W + X] = (Sum / 4).ToFColorSRGB();
                }
            },
            EParallelForFlags::BackgroundPriority);
        Image.W = W;
        Image.H = H;
        Image.Pixels = MoveTemp(Next);
    }

    static void BuildMips(FAC2DecodedImage &Image, bool PreserveMask=false)
    {
        auto& Mips=PreserveMask?Image.CutoutMips:Image.Mips;
        if (!Mips.IsEmpty())
            return;
        int32 Width = Image.W, Height = Image.H;
        TConstArrayView<FColor> Previous = Image.Pixels;
        TArray<FColor> RawMask;
        const float Coverage=PreserveMask?AC2TextureMips::Coverage(Image.Pixels):0;
        while (Width > 1 || Height > 1)
        {
            const int32 Index = Mips.AddDefaulted();
            if (Image.SourceMips.IsValidIndex(Index) && !Image.SourceMips[Index].IsEmpty())
            {
                Previous = Image.SourceMips[Index];
            }
            else if (PreserveMask)
            {
                // Generate only missing levels. Avoid compounding coverage correction.
                RawMask = AC2TextureMips::Downsample(Previous, Width, Height, true);
                Mips[Index] = RawMask;
                AC2TextureMips::PreserveCoverage(Mips[Index], Coverage);
                Previous = RawMask;
            }
            else
            {
                Mips[Index] = AC2TextureMips::Downsample(Previous, Width, Height);
                Previous = Mips[Index];
            }
            Width = FMath::Max(1, Width / 2);
            Height = FMath::Max(1, Height / 2);
        }
    }
    UTexture2D *Texture(FAC2DecodedImage &Image, bool Clamp = false, uint32 AU=0, uint32 AV=0, bool PreserveMask=false)
    {
        if(!AU)AU=Clamp?3:1; if(!AV)AV=Clamp?3:1;
        const uint32 SamplerKey=(AU&15)|((AV&15)<<4)|(uint32(PreserveMask)<<8);
        if(auto* Found=Image.Samplers.Find(SamplerKey))return *Found;
        check(IsInGameThread());
        // Weak entries don't keep distant blocks' GPU resources alive.
        static TMap<FString, TWeakObjectPtr<UTexture2D>> SharedTextures;
        const FString CacheKey =
            FString::Printf(TEXT("%s:%08X:%08X:%dx%d:%d:%d"), *TextureNamespace, Image.Id, Image.SurfaceId,
                            Image.W, Image.H, Image.HighResolution, SamplerKey);
        if (Image.Id)
            if (auto *Cached = SharedTextures.Find(CacheKey))
                if (auto *T = Cached->Get())
                {
                    Objects.Add(T);
                    Image.Samplers.Add(SamplerKey,T);
                    return T;
                }
        UTexture2D *T = UTexture2D::CreateTransient(Image.W, Image.H, PF_B8G8R8A8);
        if (!T)
            return nullptr;
        T->SRGB = true;
        T->NeverStream = true;
        auto Address=[](uint32 A){return A==3?TA_Clamp:A==2?TA_Mirror:TA_Wrap;};
        T->AddressX=Address(AU);T->AddressY=Address(AV);
        T->Filter = TF_Default; // texture group supplies anisotropic filtering
        auto *Platform = T->GetPlatformData();
        BuildMips(Image,PreserveMask);
        const auto& Mips=PreserveMask?Image.CutoutMips:Image.Mips;
        int32 W = Image.W, H = Image.H;
        for (int32 Level = 0; Level <= Mips.Num(); ++Level)
        {
            const int32 Index = Level - 1;
            const bool Authored = Image.SourceMips.IsValidIndex(Index) && !Image.SourceMips[Index].IsEmpty();
            const auto &Pixels = Level == 0 ? Image.Pixels : Authored ? Image.SourceMips[Index] : Mips[Index];
            if (Level)
            {
                auto *NewMip = new FTexture2DMipMap();
                NewMip->SizeX = W;
                NewMip->SizeY = H;
                Platform->Mips.Add(NewMip);
            }
            auto &Target = Platform->Mips[Level];
            Target.BulkData.Lock(LOCK_READ_WRITE);
            void *Dest = Target.BulkData.Realloc(Pixels.Num() * sizeof(FColor));
            FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
            Target.BulkData.Unlock();
            W = FMath::Max(1, W / 2);
            H = FMath::Max(1, H / 2);
        }
        T->UpdateResource();
        Objects.Add(T);
        Image.Samplers.Add(SamplerKey,T);
        if (Image.Id)
            SharedTextures.Add(CacheKey, T);
        return T;
    }

    UMaterialInterface *NativeParent(const TCHAR* BaseName, bool TwoSided)
    {
        const FString Base(BaseName);
        auto* Parent=LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/AC2/Materials/")+Base+TEXT(".")+Base));
        if(!Parent || Parent->IsTwoSided()==TwoSided)return Parent;
        const FString Variant=Base+(TwoSided?TEXT("TwoSided"):TEXT("OneSided"));
        return LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/AC2/Materials/")+Variant+TEXT(".")+Variant));
    }
    UMaterialInterface *Instance(FAC2DecodedImage *Image, const FLinearColor &Tint, bool Masked, uint32 CullMode=0, bool ClampTexture=false)
    {
        UMaterialInterface *Parent = CullMode ? NativeParent(Masked?TEXT("M_AC2Cutout"):TEXT("M_AC2Opaque"),CullMode==1) : (Masked ? Cutout : Opaque);
        if (!Parent)
            return nullptr;
        auto *M = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
        Objects.Add(M);
        if (Image)
            M->SetTextureParameterValue(TEXT("Albedo"), Texture(*Image,ClampTexture,0,0,Masked));
        M->SetVectorParameterValue(TEXT("Tint"), Tint);
        return M;
    }
    void ConfigureTerrainCloud(UMaterialInstanceDynamic* Material)
    {
        if (Material && CloudShadowTexture)
            if (auto Mask = Image(CloudShadowTexture))
            {
                Material->SetTextureParameterValue(TEXT("AC2CloudShadow"), Texture(*Mask));
                Material->SetVectorParameterValue(TEXT("AC2CloudMotion"), CloudShadowMotion);
            }
    }
    UMaterialInterface* GroundMaterial(FAC2DecodedImage& Atlas)
    {
        auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Terrain.M_AC2Terrain"));
        if (!Parent) return nullptr;
        auto* Material = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
        Objects.Add(Material);
        Material->SetTextureParameterValue(TEXT("Albedo"), Texture(Atlas,true));
        ConfigureTerrainCloud(Material);
        return Material;
    }
    static bool UsesMaskedFrameBuffer(const FAC2DecodedLayer& L)
    {
        // Native frame-buffer copy selected by stage 0, retained as CURRENT
        // by stage 1. Ordinary texture stages still compute the authored alpha.
        return L.Textures.Num()==2 && !L.PixelShader && !L.DynamicLighting &&
            L.SpecialTextures[0]==1 && L.TextureCoordinates[0]==0x400 && L.ProjectedCoordinates[0] &&
            L.AddressModes[0]==FIntPoint(1,1) && L.SpecialTextures[1]==0 &&
            L.TextureCoordinates[1]<=3 && !L.ProjectedCoordinates[1] &&
            L.ColorStages[0].Operation==2 && L.ColorStages[0].Argument1==2 &&
            L.ColorStages[1].Operation==3 && L.ColorStages[1].Argument2==1 &&
            L.AlphaStages[0].Operation==3 && L.AlphaStages[0].Argument2==0 &&
            L.SourceBlend==5 && L.DestBlend==6 && !L.Masked;
    }
    static bool FixedFunction(const FAC2DecodedLayer &L)
    {
        if(L.PixelShader || L.Textures.Num()>4)return false;
        if(UsesMaskedFrameBuffer(L))return true;
        for(int32 I=0;I<L.Textures.Num();++I){
            if(L.SpecialTextures[I])return false;
            if(L.TextureCoordinates[I]>3 && !(L.TextureCoordinates[I]==0x200 && !L.ProjectedCoordinates[I] &&
                !L.Masked && L.SourceBlend==5 && L.DestBlend==6))return false;
        }
        return true;
    }
    static int32 SelectedLayer(const FAC2DecodedMaterial &M)
    {
        for(uint32 Hardware:{2u,1u}) for(int32 I=0;I<M.Layers.Num();++I)
            if(AC2MaterialLayerEnabled(M.Layers[I].TrueFlags,M.Layers[I].FalseFlags,Hardware) && FixedFunction(M.Layers[I]))return I;
        return 0;
    }
    void ConfigureStages(UMaterialInstanceDynamic *MID,const FAC2DecodedLayer &L,int32 LayerIndex,bool PreserveMask)
    {
        MID->SetVectorParameterValue(TEXT("AC2FogMode"),FLinearColor(L.DepthFog,L.DepthAlphaFog,L.DiffuseFog,0));
        if(!FixedFunction(L))return;
        MID->SetScalarParameterValue(TEXT("AC2NativeStages"),1);
        MID->SetVectorParameterValue(TEXT("AC2UIMaterialAmbient"),L.Ambient);
        MID->SetVectorParameterValue(TEXT("AC2UIFlags"),FLinearColor(L.DynamicLighting,L.MaterialDiffuse,L.VertexDiffuse,L.ModulateVertexDiffuse));
        MID->SetVectorParameterValue(TEXT("AC2UIIgnore"),FLinearColor(L.IgnoreDirectional,L.IgnorePoint,0,0));
        MID->SetVectorParameterValue(TEXT("AC2NativeSpecularColor"),L.Specular);
        MID->SetScalarParameterValue(TEXT("AC2NativeSpecularPower"),L.SpecularPower.Value(0));
        MID->SetScalarParameterValue(TEXT("AC2NativeSpecularEnabled"),L.DynamicLighting && L.SpecularHighlights ? 1.f : 0.f);
        MID->SetVectorParameterValue(TEXT("AC2StageFlags"),FLinearColor(L.MaterialDiffuse,L.VertexDiffuse,LayerIndex,1));
        for(int32 S=0;S<L.Textures.Num();++S)
        {
            const FString P=FString::Printf(TEXT("AC2Stage%d"),S);
            const auto &C=L.ColorStages[S], &A=L.AlphaStages[S];
            auto StageImage=L.Textures[S]?Image(L.Textures[S]):nullptr;
            const bool Bound=StageImage.IsValid() || L.SpecialTextures[S]==1;
            MID->SetVectorParameterValue(*(P+TEXT("Color")),FLinearColor(AC2BoundColorOperation(C.Operation,C.Argument1,Bound),C.Argument1,C.Argument2,0));
            MID->SetVectorParameterValue(*(P+TEXT("Alpha")),FLinearColor(A.Operation,A.Argument1,A.Argument2,0));
            MID->SetVectorParameterValue(*(P+TEXT("UV")),FLinearColor(L.TextureCoordinates[S],0,0,0));
            if(StageImage) MID->SetTextureParameterValue(*(P+TEXT("Texture")),Texture(*StageImage,false,L.AddressModes[S].X,L.AddressModes[S].Y,PreserveMask));
        }
    }
    void ConfigureShore(UMaterialInstanceDynamic* MID)
    {
        if (!ShoreMaterials.IsSet())
        {
            FString Error;
            const uint32 Group = Portal.ReadPublicEnumId(Portal.GetHeader().MasterMap, 0x40000002, Error);
            FShoreMaterials& Shore = ShoreMaterials.Emplace();
            Shore.Waves = Portal.ReadPublicEnumId(Group, 0x40000017, Error);
            Shore.Transmission = Portal.ReadPublicEnumId(Group, 0x40000024, Error);
        }
        // DrawShore selects these materials by enum. Their resolved instances
        // reference different texture DIDs in Legions and Icefire.
        auto BindStage = [&](uint32 Id, int32 Stage, const TCHAR* Parameter)
        {
            const auto Source = Id ? Material(Id) : nullptr;
            if (!Source || Source->Layers.IsEmpty()) return false;
            const auto& Layer = Source->Layers[SelectedLayer(*Source)];
            if (!Layer.Textures.IsValidIndex(Stage) || !Layer.Textures[Stage]) return false;
            const auto Pixels = Image(Layer.Textures[Stage]);
            if (!Pixels) return false;
            const auto Address = Layer.AddressModes[Stage];
            auto* Sampler = Texture(*Pixels, false, Address.X, Address.Y);
            if (!Sampler) return false;
            MID->SetTextureParameterValue(Parameter, Sampler);
            return true;
        };
        BindStage(ShoreMaterials->Transmission, 0, TEXT("AC2ShoreMask"));
        const bool Waves = BindStage(ShoreMaterials->Waves, 0, TEXT("AC2ShoreFoam"));
        const bool Mask = BindStage(ShoreMaterials->Waves, 2, TEXT("AC2ShoreFoamMask"));
        MID->SetScalarParameterValue(TEXT("AC2ShoreFoamEnabled"), Waves && Mask ? 1.f : 0.f);
    }

    TSharedPtr<FAC2DecodedImage> MaterialImage(uint32 Id, FLinearColor &Tint, bool &Masked)
    {
        if ((Id >> 24) == 0x40 || (Id >> 24) == 0x41)
            return Image(Id);
        auto M = Material(Id);
        if (!M)
            return nullptr;
        if(M->Layers.IsEmpty())return nullptr;
        const auto &L=M->Layers[SelectedLayer(*M)];
        Tint=L.Tint; Masked=L.Masked;
        TSharedPtr<FAC2DecodedImage> First;
        for(int32 S=0;S<L.Textures.Num();++S)
            if(L.SpecialTextures[S]==0 && L.Textures[S])
                if(auto I=Image(L.Textures[S])) { I->AlphaRange(); if(!First)First=I; }
        return First;
    }
    UMaterialInterface *RuntimeMaterial(uint32 Id, bool Grass = false)
    {
        const uint64 CacheKey = uint64(Id) | (uint64(Grass) << 32) | (uint64(DrawingCell & 0xffff) << 33);
        if (auto *M = RuntimeMaterials.Find(CacheKey))
            return *M;
        FLinearColor Tint = FLinearColor::White;
        bool Masked = false;
        auto I = MaterialImage(Id, Tint, Masked);
        if (!I)
        {
            auto Authored = Material(Id);
            bool RequiresTexture = false;
            if (Authored)
                for (const auto &L : Authored->Layers)
                    for (uint32 T : L.Textures)
                        RequiresTexture |= (T >> 24) == 0x40;
            if (!Authored || RequiresTexture)
            {
                ++Stats.MissingMaterials;
                Warn(Id, TEXT("cannot resolve authored albedo"));
            }
            else
                UE_LOG(LogTemp, Display, TEXT("AC2 material %08X uses authored constant colour"), Id);
        }
        auto Native = Material(Id);
        const auto *Layer = Native && !Native->Layers.IsEmpty() ? &Native->Layers[SelectedLayer(*Native)] : nullptr;
        UE_LOG(LogTemp, VeryVerbose, TEXT("AC2 material %08X image=%08X tint=%s blend=%u/%u"),
            Id, I ? I->Id : 0, *Tint.ToString(), Layer ? Layer->SourceBlend : 0, Layer ? Layer->DestBlend : 0);
        const float DiffuseAlpha = Layer && Layer->MaterialDiffuse ? Tint.A : 1.f;
        TArray<FVector2D> Ranges;
        TArray<bool> BoundTextures;
        if(Layer) for(int32 S=0;S<Layer->Textures.Num();++S) {
            auto ImageData=Layer->Textures[S]?Image(Layer->Textures[S]):nullptr;
            BoundTextures.Add(ImageData.IsValid() || Layer->SpecialTextures[S]==1);
            Ranges.Add(ImageData?ImageData->AlphaRange():FVector2D(1,1));
        }
        TConstArrayView<FAC2AlphaStage> AlphaStages;
        if(Layer) {
            // Do not classify a material using stages the native colour
            // cascade will never execute (notably empty optional detail maps).
            const int32 Active=FixedFunction(*Layer)?AC2ActiveStageCount(Layer->ColorStages,BoundTextures):Layer->AlphaStages.Num();
            AlphaStages=MakeArrayView(Layer->AlphaStages.GetData(),Active);
        }
        auto Alpha = [&](float A) {
            return Layer ? EvaluateAC2Alpha(AlphaStages, A, DiffuseAlpha, Tint.A) : 1.f;
        };
        float Minimum=Alpha(1),Maximum=Minimum;
        if(Layer) {
            Minimum=1;Maximum=0;
            // Independent endpoints, not one alpha reused for every texture.
            // Four native stages bound the enumeration to sixteen combinations.
            const int32 Count=FMath::Min(AlphaStages.Num(),4);
            for(int32 Mask=0;Mask<(1<<Count);++Mask) {
                TArray<float> Samples;
                for(int32 S=0;S<Count;++S)Samples.Add((Mask&(1<<S))?Ranges[S].Y:Ranges[S].X);
                const float A=EvaluateAC2Alpha(AlphaStages,Samples,DiffuseAlpha,Tint.A);
                Minimum=FMath::Min(Minimum,A);Maximum=FMath::Max(Maximum,A);
            }
        }
        const bool HasAlpha=Minimum<.98f;
        const float AppearanceOpacity = Native ? Native->AppearanceOpacity : 1.f;
        const bool Faded = AppearanceOpacity < 1.f;
        if(Layer && (Masked || Layer->DestBlend>2) && Maximum<=SMALL_NUMBER)InvisibleMaterials.Add(Id);
        if (!HasAlpha)
            Masked = false;
        UMaterialInterface *Result = nullptr;
        if(Layer && FixedFunction(*Layer) && Layer->TextureCoordinates.Contains(0x200))
        {
            const TCHAR* Name=PreviewMode?(Faded || HasAlpha?TEXT("M_AC2UIAlphaAnisotropic"):TEXT("M_AC2UIOpaqueAnisotropic"))
                :(Faded || HasAlpha?TEXT("M_AC2SceneryAlphaAnisotropic"):TEXT("M_AC2OpaqueAnisotropic"));
            if(auto* Parent=NativeParent(Name,Layer->CullMode==1)){
                auto* MID=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());Objects.Add(MID);
                if(I)MID->SetTextureParameterValue(TEXT("Albedo"),Texture(*I));
                MID->SetVectorParameterValue(TEXT("Tint"),Tint);
                if(PreviewMode)LightPreview(MID);
                Result=MID;
            }
        }
        else if (Layer && UsesMaskedFrameBuffer(*Layer))
        {
            if(auto* Parent=NativeParent(TEXT("M_AC2FrameBuffer"),Layer->CullMode==1))
            {
                auto* MID=UMaterialInstanceDynamic::Create(Parent,GetTransientPackage());
                MID->SetVectorParameterValue(TEXT("Tint"),Tint);
                Objects.Add(MID);
                Result=MID;
            }
        }
        else if (PreviewMode)
        {
            const uint32 Dest = Layer ? Layer->DestBlend : 1;
            const TCHAR* Name = Faded ? TEXT("M_AC2UIAlpha") : Masked ? TEXT("M_AC2UICutout") : Dest == 2 ? TEXT("M_AC2UIAdd")
                : Dest > 2 && HasAlpha ? TEXT("M_AC2UIAlpha") : TEXT("M_AC2UIOpaque");
            auto* Parent = NativeParent(Name, Layer && Layer->CullMode==1);
            if (Parent)
            {
                auto* MID = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
                Objects.Add(MID);
                if (I) MID->SetTextureParameterValue(TEXT("Albedo"), Texture(*I,false,0,0,Masked));
                MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                MID->SetVectorParameterValue(TEXT("AC2UIMaterialAmbient"), Layer ? Layer->Ambient : FLinearColor::White);
                MID->SetVectorParameterValue(TEXT("AC2UIFlags"), Layer ? FLinearColor(Layer->DynamicLighting, Layer->MaterialDiffuse,
                    Layer->VertexDiffuse, Layer->ModulateVertexDiffuse) : FLinearColor(0,1,0,0));
                MID->SetVectorParameterValue(TEXT("AC2UIIgnore"), Layer ? FLinearColor(Layer->IgnoreDirectional, Layer->IgnorePoint,0,0) : FLinearColor::Black);
                LightPreview(MID);
                Result = MID;
            }
        }
        else if (Grass)
        {
            // Frill cards must write depth; sorting merged translucent patches by
            // their centres makes whole clumps swap order as the camera moves.
            auto *Parent =
                LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Grass.M_AC2Grass"));
            if (Parent)
            {
                auto *MID = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
                Objects.Add(MID);
                if (I)
                {
                    auto *T = Texture(*I,false,0,0,true);
                    MID->SetTextureParameterValue(TEXT("Albedo"), T);
                    FAC2TextureSource Source;
                    Source.Texture = I->Id; Source.Surface = I->SurfaceId;
                    Source.DecodedSize = FIntPoint(I->W,I->H);
                    Source.UploadedSize = T ? FIntPoint(T->GetSizeX(),T->GetSizeY()) : FIntPoint::ZeroValue;
                    Source.HighResolution = I->HighResolution;
                    Stats.GrassTextures.Add(Source);
                }
                MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                Result = MID;
            }
        }
        else if (SkyMode)
        {
            auto Authored = Material(Id);
            const uint32 Dest = Authored && Authored->Layers.Num() ? Authored->Layers[SelectedLayer(*Authored)].DestBlend : 1;
            if (Authored && Authored->Layers.Num())
            {
                const auto &L = Authored->Layers[SelectedLayer(*Authored)];
                if (!L.MaterialDiffuse)
                    Tint = FLinearColor::White;
                UE_LOG(LogTemp, Display,
                       TEXT("AC2 sky material %08X: blend %u/%u vertex=%d modulate=%d image=%08X tint=%s"),
                       Id, L.SourceBlend, L.DestBlend, L.VertexDiffuse, L.ModulateVertexDiffuse,
                       I ? I->Id : 0, *Tint.ToString());
            }
            const TCHAR *Path = Dest == 2  ? TEXT("/Game/AC2/Materials/M_AC2SkyAdd.M_AC2SkyAdd")
                                : Dest > 2 ? TEXT("/Game/AC2/Materials/M_AC2SkyAlpha.M_AC2SkyAlpha")
                                           : TEXT("/Game/AC2/Materials/M_AC2Sky.M_AC2Sky");
            auto *Parent = LoadObject<UMaterialInterface>(nullptr, Path);
            if (Parent)
            {
                auto *MID = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
                Objects.Add(MID);
                if (I)
                    MID->SetTextureParameterValue(TEXT("Albedo"), Texture(*I));
                MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                Result = MID;
            }
        }
        else
        {
            auto Authored = Material(Id);
            uint32 Dest = Authored && !Authored->Layers.IsEmpty() ? Authored->Layers[SelectedLayer(*Authored)].DestBlend : 1;
            if (Faded || (!Masked && (Dest == 2 || (Dest > 2 && HasAlpha))))
            {
                auto *Parent = NativeParent(!Faded && Dest==2?TEXT("M_AC2SceneryAdd"):TEXT("M_AC2SceneryAlpha"),Layer && Layer->CullMode==1);
                auto *MID =
                    Parent ? UMaterialInstanceDynamic::Create(Parent, GetTransientPackage()) : nullptr;
                if (MID)
                {
                    Objects.Add(MID);
                    if (I)
                        MID->SetTextureParameterValue(TEXT("Albedo"), Texture(*I));
                    MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                }
                Result = MID;
            }
            else
                Result = Instance(I.Get(), Tint, Masked, Layer?Layer->CullMode:2);
        }
        if (auto *MID = Cast<UMaterialInstanceDynamic>(Result))
        {
            MID->SetScalarParameterValue(TEXT("AC2AppearanceOpacity"), AppearanceOpacity);
            MID->SetVectorParameterValue(TEXT("AC2Opacity"),
                                         FLinearColor(Alpha(1) - Alpha(0), Alpha(0), 0, 0));
            // Retail specular is a separate vertex colour added after the texture
            // cascade. UE's PBR lobe would add an unrelated white sheen on top.
            MID->SetScalarParameterValue(TEXT("AC2Specular"),0.f);
            if (Layer) {
                ConfigureStages(MID,*Layer,SelectedLayer(*Native),Grass || Masked);
                // Sky layers are unlit. Their authored texture/alpha operations
                // still apply, including whether vertex diffuse participates.
                if(SkyMode)MID->SetVectorParameterValue(TEXT("AC2StageFlags"),
                    FLinearColor(Layer->MaterialDiffuse,Layer->VertexDiffuse,SelectedLayer(*Native),0));
                if(!FixedFunction(*Layer))Warn(Id,TEXT("native programmable/generated-coordinate material uses base fallback"));
            }
        }
        TrackMaterial(Id, Result);
        RuntimeMaterials.Add(CacheKey, Result);
        return Result;
    }

    void SkipProperties(FAC2AssetReader &R, TMap<uint32, TArray<uint8>>* Out = nullptr)
    {
        const int32 NG = R.Count(8);
        for (int32 I = 0; I < NG && R.Good; ++I)
        {
            R.Word();
            const int32 NP = R.Count();
            for (int32 J = 0; J < NP && R.Good; ++J)
            {
                const uint32 Name = R.Word();
                const int32 Start = R.Pos;
                const uint32 *Type = PropertyTypes.Find(Name);
                if (!Type)
                {
                    R.Good = false;
                    break;
                }
                switch (*Type & 0xffff)
                {
                case 1:
                case 2:
                case 3:
                case 5:
                case 7:
                case 8:
                case 11:
                    R.Take(4);
                    break;
                case 4:
                    R.Take(12);
                    break;
                case 6:
                    R.String();
                    break;
                case 9:
                    R.Take(44);
                    break;
                case 13:
                    R.Take(8);
                    break;
                case 14:
                    R.Take(32);
                    break;
                case 10: {
                    R.Take(8);
                    const uint32 N = R.Word();
                    if (N >> 16)
                        R.String(2);
                    if (N & 65535)
                        R.Good = false;
                    break;
                }
                default:
                    R.Good = false;
                    break;
                }
                if (Out && R.Good)
                    Out->FindOrAdd(Name).Append(R.Data.GetData() + Start, R.Pos - Start);
            }
        }
    }
    FAC2DecodedEntity Entity(FAC2AssetReader &R)
    {
        FAC2DecodedEntity E;
        const uint32 Bits = R.Word();
        if (Bits & ~0x1ffu)
            R.Good = false;
        if (Bits & 1)
            R.Word();
        if (Bits & 2)
            E.Type = R.Word();
        if (Bits & 4)
            R.Take(8);
        if (Bits & 8)
            E.Data = R.Word();
        if (Bits & 0x10)
        {
            E.Origin = R.Vector();
            E.Rotation = R.Quaternion();
            R.Take(36);
        }
        if (Bits & 0x20)
            E.Scale = R.Vector();
        if (Bits & 0x80)
            SkipProperties(R, E.Type == 0x40000005 || E.Type == 0x40000006 ? &E.Properties : nullptr);
        if (Bits & 0x40)
            R.Word();
        if (Bits & 0x100)
        {
            uint32 N = R.Short();
            if (N == 65535) N = R.Word();
            const int32 Start = R.Pos;
            if (R.Take(N))
            {
                TArray<uint8> B;
                B.Append(R.Data.GetData() + Start, N);
                const uint32 Keys[] = {0xe5849238,0xeb2d767d,0xfc0fdd03,0xcb45a6a6,0x7f21e6bb,0xf5a6cdc8,0x22d9ad12,0xb2a3d4de,
                    0x3b834db7,0xc406c91c,0x8392305e,0x0a9f0099,0x0943b90c,0xa66d953c,0x0d929e1f,0x0e2e9ad4};
                for (int32 I = 0; I + 4 <= B.Num(); I += 4)
                { const uint32 W = AC2::U32(B.GetData() + I) ^ Keys[(I / 4) & 15]; FMemory::Memcpy(B.GetData() + I, &W, 4); }
                for (auto& V : B) V = uint8((V << 4) | (V >> 4));
                B.Add(0);
                E.Name = UTF8_TO_TCHAR(reinterpret_cast<const char*>(B.GetData()));
            }
            R.Align();
        }
        return E;
    }
    FAC2FXNode FXNode(FAC2AssetReader &R)
    {
        FAC2FXNode Node;
        const uint64 Low = R.Word(), Bits = Low | (uint64(R.Word()) << 32);
        auto Field = [&](uint64 Bit, int32 Bytes = 4) {
            if (Bits & Bit)
                R.Take(Bytes);
        };
        for (uint64 Bit : {1ULL, 0x8000000000ULL})
            if (Bits & Bit)
            {
                const int32 N = R.Count();
                for(int32 I=0;I<N;++I){const uint32 Id=R.Word();if(Bit==1)
                {
                    Node.Sounds.Add(Id);
                    if(!LoopingSounds.Contains(Id))
                    {
                        TArray<uint8> Sound;
                        const bool Valid=Read(Id,Sound)&&Sound.Num()>=8&&AC2::U32(Sound.GetData())==Id;
                        const uint32 Flags=Valid?AC2::U32(Sound.GetData()+4):0;
                        LoopingSounds.Add(Id,Valid&&(Flags&1));
                        // FXManager::DoSoundFX: MusicInfo uses Player unless
                        // DEFAULT_CHANNEL (0x40000) selects Music. UI media
                        // keeps its own Main-channel playback policy.
                        SoundChannels.Add(Id,Id>>24==0x2c?(Flags&0x40000?1u:2u):0u);
                    }
                }}
            }
        if (Bits & 2)
        {
            int32 N = R.Count(4, 4096);
            for (int32 I = 0; I < N; ++I)
                Node.Descriptors.Add(R.Word());
        }
        Field(4);
        if(Bits&8)Node.AppearanceTable=R.Word();
        if(Bits&16){Node.AppearanceKey=R.Word();Node.AppearanceScalar=R.Float();}
        if(Bits&32){Node.RampTime=R.Float();if(!FMath::IsFinite(Node.RampTime) || Node.RampTime<0)R.Good=false;}
        Field(64);
        if (Bits & 128)
            Node.Holding = R.Word();
        if (Bits & 256)
            Node.Bone = R.Word();
        if (Bits & 512)
            Node.Offset = R.Vector();
        if (Bits & 0x400) Node.Streak = FAC2StreakDefinition::Read(R);
        if (Bits & 0x800) Node.StartStreak = R.Word() != 0;
        if (Bits & 0x1000) Node.StopStreak = R.Word() != 0;
        Field(0x2000);
        Field(0x4000);
        Field(0x8000, 20);
        Field(0x10000);
        if(Bits&0x20000)Node.Visual=R.Word();
        if(Bits&0x40000)Node.Lifetime=R.Float();
        Field(0x2000000000ULL);
        if(Bits&0x80000)Node.Behavior=R.Word();
        if(Bits&0x100000)Node.Parent=R.Word()!=0;
        Field(0x200000);
        if(Bits&0x400000)Node.Retarget=R.Word()!=0;
        Field(0x800000);Field(0x800000000ULL);
        if(Bits&0x1000000)Node.TargetFX=R.Word();
        if(Bits&0x2000000)Node.DayTime=R.Word()!=0;
        if(Bits&0x4000000)Node.NightTime=R.Word()!=0;
        if (Bits & 0x8000000)
        {
            const uint32 N = R.Word() & 65535;
            for(uint32 I=0;I<N && R.Good;++I){const uint32 Key=R.Word(),Value=R.Word();Node.Channels.Add(Key,Value);}
        }
        if(Bits&0x10000000)Node.FireAndForget=R.Word()!=0;
        if (Bits & 0x20000000)
            Node.Water = R.Word() != 0;
        Field(0x40000000);
        if(Bits&0x80000000)Node.Decal=FAC2Decal::Read(R);
        if (Bits & 0x200000000ULL) Node.Repulsor = FAC2RepulsorFX::Read(R);
        if (Bits & 0x100000000ULL)
        {
            Node.Light.Enabled=R.Word()!=0;
            const auto E=Entity(R);
            Node.Light.Life=R.Float();Node.Light.Fade=R.Float();
            for(const auto& P:E.Properties)
            {
                FAC2AssetReader V(P.Value);
                if(P.Key==0x40000004)Node.Light.Range=FAC2Wave::Read(V);
                if(P.Key==0x40000008)Node.Light.Color=FColor(V.Word());
                if(P.Key==0x40000009)Node.Light.Intensity=FAC2Wave::Read(V);
                if(!V.Good)R.Good=false;
            }
            if(!FMath::IsFinite(Node.Light.Range.Base) || !FMath::IsFinite(Node.Light.Intensity.Base) ||
                !FMath::IsFinite(Node.Light.Life) || !FMath::IsFinite(Node.Light.Fade))Node.Light.Enabled=false;
        }
        if(Bits&0x400000000ULL)Node.PlayerOnly=R.Word()!=0;
        if(Bits&0x1000000000ULL)Node.Script=R.Word();
        if (Bits & 0x4000000000ULL)
            Node.RemoveRotation = R.Word() != 0;
        if (Bits & ~0xffffffffffULL)
            R.Good = false;
        return Node;
    }
    TMap<uint32, TArray<FAC2FXNode>> FXMap(FAC2AssetReader &R)
    {
        TMap<uint32, TArray<FAC2FXNode>> Map;
        const uint32 N = R.Word() & 65535;
        for (uint32 I = 0; I < N && R.Good; ++I)
        {
            uint32 Key = R.Word();
            auto &Nodes = Map.Add(Key);
            int32 K = R.Count(8, 4096);
            for (int32 J = 0; J < K && R.Good; ++J)
            {
                auto Node = FXNode(R);
                uint32 NT = R.Word() & 65535;
                for (uint32 T = 0; T < NT && R.Good; ++T)
                {
                    const uint32 Terrain = R.Word();
                    Node.Terrain.Add(Terrain, MakeShared<FAC2FXNode>(FXNode(R)));
                }
                if (!Node.Descriptors.IsEmpty() || !Node.Sounds.IsEmpty() || !Node.Terrain.IsEmpty() || Node.Visual || Node.TargetFX || Node.Script || Node.AppearanceTable || Node.Decal.Material || Node.Light.Enabled || Node.Repulsor.Enabled || Node.Streak.Enabled || Node.StartStreak || Node.StopStreak)
                    Nodes.Add(MoveTemp(Node));
            }
        }
        return Map;
    }
    void TableFX(uint32 Table, uint32 Key, TArray<FAC2FXNode> &Out, int32 Depth = 0)
    {
        if (Depth >= 8)
            return;
        TArray<uint8> B;
        if (!Read(Table, B))
            return;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Table)
            return;
        auto Map = FXMap(R);
        int32 N = R.Count(4, 4096);
        TArray<uint32> Sub;
        for (int32 I = 0; I < N; ++I)
            Sub.Add(R.Word());
        if (!R.End())
        {
            Warn(Table, TEXT("invalid native FX table"));
            return;
        }
        if (auto *Nodes = Map.Find(Key))
            Out.Append(*Nodes);
        for (uint32 Id : Sub)
            TableFX(Id, Key, Out, Depth + 1);
    }
    FAC2DecodedPart Part(FAC2AssetReader &R)
    {
        FAC2DecodedPart P;
        const uint32 Bits = R.Word();
        P.Fields = Bits;
        if (Bits & 1)
            P.Key = R.Word();
        if (Bits & 2)
            P.Parent = R.Word();
        if (Bits & 4)
            P.Connection = R.Word();
        if (Bits & 8)
            P.Setup = R.Word();
        if (Bits & 16)
            P.AnimMap = R.Word();
        if (Bits & 32)
        {
            const uint32 N = R.Word() & 65535;
            for (uint32 I = 0; I < N && R.Good; ++I)
            {
                uint32 Table = R.Word();
                const uint32 K = R.Word() & 65535;
                if (K > uint32(R.Left() / 8))
                {
                    R.Good = false;
                    break;
                }
                for (uint32 J = 0; J < K; ++J)
                {
                    uint32 Key = R.Word();
                    float Value = R.Float();
                    P.Appearance.Add({Table, Key, Value});
                }
            }
        }
        if (Bits & 64)
            P.FXTable = R.Word();
        if (Bits & 128)
        {
            const uint32 N = R.Word() & 65535;
            for (uint32 I = 0; I < N && R.Good; ++I)
            {
                const uint32 Key=R.Word();const float Strength=R.Float();
                P.Startup.AddUnique(Key);P.StartupStrength.Add(Key,Strength);
                if(!FMath::IsFinite(Strength) || Strength<0)R.Good=false;
            }
        }
        if (Bits & 256)
            P.Overrides = FXMap(R);
        if (Bits & ~511u)
            R.Good = false;
        return P;
    }
    // AppearanceTable::StreamPack / GetAtoms, AtomCollection::Update (Legions 7160).
    TSharedPtr<FAC2AppearanceTable> AppearanceTable(uint32 Id)
    {
        if (auto *Found = AppearanceTables.Find(Id))
            return *Found;
        AppearanceTables.Add(Id, nullptr);
        TArray<uint8> B;
        if ((Id >> 24) != 0x20 || !Read(Id, B))
            return nullptr;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Id)
            return nullptr;
        auto Table = MakeShared<FAC2AppearanceTable>();
        int32 N = R.Count();
        for (int32 I = 0; I < N; ++I)
            Table->Bases.Add(R.Word());
        N = R.Word() & 65535;
        for (int32 I = 0; I < N && R.Good; ++I)
        {
            uint32 Key = R.Word();
            int32 Count = R.Count(28, 4096);
            auto &Collections = Table->Collections.Add(Key);
            for (int32 J = 0; J < Count && R.Good; ++J)
            {
                FAC2AtomCollection C;
                C.Intensity = R.Float();
                int32 K = R.Count(20);
                for (int32 A = 0; A < K; ++A)
                {
                    FAC2MatAtom Atom;
                    Atom.Priority = R.Float();
                    Atom.Template = R.Word();
                    Atom.Modifier = R.Word();
                    Atom.MeshType = R.Word();
                    Atom.MaterialType = R.Word();
                    C.Atoms.Materials.Add(Atom);
                }
                K = R.Count(8);
                for (int32 A = 0; A < K; ++A)
                {
                    const float Priority = R.Float(), Opacity = R.Float();
                    C.Atoms.Opacities.Add(FVector2f(Priority, Opacity));
                }
                K = R.Count(12);
                for (int32 A = 0; A < K; ++A)
                {
                    FAC2MeshAtom Atom;
                    Atom.Priority = R.Float();
                    Atom.Type = R.Word();
                    Atom.Mesh = R.Word();
                    C.Atoms.Meshes.Add(Atom);
                }
                K = R.Count(8);
                for (int32 A = 0; A < K; ++A)
                {
                    FAC2PartAtom Atom;
                    Atom.Priority = R.Float();
                    Atom.Part = Part(R);
                    C.Atoms.Parts.Add(Atom);
                }
                K = R.Count(8);
                for (int32 A = 0; A < K && R.Good; ++A)
                {
                    int32 F = R.Count();
                    TArray<uint32> Keys;
                    for (int32 Q = 0; Q < F; ++Q)
                        Keys.Add(R.Word());
                    F = R.Count(8);
                    TArray<FAC2FXNode> Nodes;
                    for (int32 Q = 0; Q < F && R.Good; ++Q)
                    {
                        auto Node = FXNode(R);
                        uint32 T = R.Word() & 65535;
                        for (uint32 V = 0; V < T && R.Good; ++V)
                        {
                            uint32 Terrain = R.Word();
                            Node.Terrain.Add(Terrain, MakeShared<FAC2FXNode>(FXNode(R)));
                        }
                        Nodes.Add(MoveTemp(Node));
                    }
                    for (uint32 FX : Keys)
                        C.Atoms.Effects.Add(FX, Nodes);
                }
                K = R.Count(12);
                for(int32 A=0;A<K;++A){FAC2IconLayer Icon;Icon.Layer=R.Word();Icon.Image=R.Word();Icon.Color=R.Word();C.Atoms.Icons.Add(Icon);}
                Collections.Add(MoveTemp(C));
            }
        }
        N = R.Word() & 65535;
        R.Take(int64(N) * 8);
        if (!R.End())
        {
            Warn(Id, TEXT("invalid appearance table"));
            return nullptr;
        }
        AppearanceTables[Id] = Table;
        return Table;
    }
    void CollectAtoms(const FAC2AppearanceKey &Key, FAC2Atoms &Out, int32 Depth = 0)
    {
        if (Depth > 8 || Key.Value == -1 || !FMath::IsFinite(Key.Value))
            return;
        auto Table = AppearanceTable(Key.Table);
        if (!Table)
            return;
        for (uint32 Base : Table->Bases)
            CollectAtoms({Base, Key.Key, Key.Value}, Out, Depth + 1);
        auto *Choices = Table->Collections.Find(Key.Key);
        if (!Choices)
            return;
        const FAC2AtomCollection *Chosen = nullptr;
        for (const auto &C : *Choices)
        {
            if (Key.Value < C.Intensity)
                break;
            Chosen = &C;
        }
        if (!Chosen)
            return;
        for (const auto &A : Chosen->Atoms.Opacities)
            Out.Opacity.Apply(A.X, A.Y, Key.Value);
        for(const auto& Icon:Chosen->Atoms.Icons)
        {
            Out.Icons.RemoveAll([&](const auto& Old){return Old.Layer==Icon.Layer;});
            Out.Icons.Add(Icon);
        }
        for (const auto &FX : Chosen->Atoms.Effects)
            Out.Effects.Add(FX.Key, FX.Value);
        for (const auto &A : Chosen->Atoms.Materials)
        {
            auto Replaces = [](const FAC2MatAtom &X, const FAC2MatAtom &Y) {
                return X.Priority > Y.Priority && (X.MeshType == 1 || X.MeshType == Y.MeshType) &&
                       X.MaterialType == Y.MaterialType;
            };
            if (Out.Materials.ContainsByPredicate([&](const auto &B) { return Replaces(B, A); }))
                continue;
            Out.Materials.RemoveAll([&](const auto &B) { return Replaces(A, B); });
            Out.Materials.Add(A);
        }
        for (const auto &A : Chosen->Atoms.Meshes)
        {
            auto *B = Out.Meshes.FindByPredicate([&](const auto &V) { return V.Type == A.Type; });
            if (!B)
                Out.Meshes.Add(A);
            else if (A.Priority >= B->Priority)
                *B = A;
        }
        for (const auto &A : Chosen->Atoms.Parts)
        {
            auto *B = Out.Parts.FindByPredicate([&](const auto &V) { return V.Part.Key == A.Part.Key; });
            if (!B)
                Out.Parts.Add(A);
            else if (A.Priority >= B->Priority)
                *B = A;
        }
    }
    uint32 AppearanceMaterial(uint32 Id, uint32 MeshType, const FAC2Atoms &Atoms)
    {
        if (((Id >> 24) != 0x31 && (Id >> 24) != 0x2b) || (Atoms.Materials.IsEmpty() && !Atoms.Opacity.Defined))
            return Id;
        uint32 Template = Id, MaterialType = 1;
        TArray<uint32> Mods;
        if ((Id >> 24) == 0x31)
        {
            TArray<uint8> B;
            if (!Read(Id, B)) return Id;
            FAC2AssetReader R(MoveTemp(B));
            R.Word(); Template = R.Word(); MaterialType = R.Word();
            const int32 N = R.Count();
            for (int32 I = 0; I < N; ++I) Mods.Add(R.Word());
            if (!R.End()) return Id;
        }
        bool Changed = Atoms.Opacity.Defined;
        for (const auto &A : Atoms.Materials)
            if ((A.MeshType == 1 || A.MeshType == MeshType) &&
                (A.MaterialType == 1 || A.MaterialType == MaterialType))
            {
                if (A.Template)
                    Template = A.Template;
                if (A.Modifier)
                    Mods.Add(A.Modifier);
                Changed = true;
            }
        if (!Changed)
            return Id;
        auto Base = Material(Template);
        if (!Base)
            return Id;
        auto M = MakeShared<FAC2DecodedMaterial>(*Base);
        M->AppearanceOpacity = Atoms.Opacity.Value;
        for (uint32 Mod : Mods)
            if (!ModifyMaterial(M, Mod))
                return Id;
        for (const auto &Layer : M->Layers)
        {
            AppearanceHash = HashCombineFast(AppearanceHash, FCrc::MemCrc32(&Layer.Tint, sizeof(Layer.Tint)));
            for (uint32 Texture : Layer.Textures)
                AppearanceHash = HashCombineFast(AppearanceHash, Texture);
        }
        uint32 Variant = ++NextMaterialVariant;
        Materials.Add(Variant, M);
        return Variant;
    }
    TSharedPtr<FAC2DecodedGeometry> AppearanceMesh(uint32 Id, const FAC2Atoms &Atoms)
    {
        auto Original = Mesh(Id);
        if (!Original)
            return nullptr;
        uint32 Type = Original->Type;
        for (const auto &A : Atoms.Meshes)
            if (A.Type == Type)
            {
                Id = A.Mesh;
                break;
            }
        if (!Id)
            return nullptr;
        auto Source = Mesh(Id);
        if (!Source)
            return nullptr;
        auto G = MakeShared<FAC2DecodedGeometry>(*Source);
        AppearanceHash = HashCombineFast(
            AppearanceHash, FCrc::MemCrc32(G->Vertices.GetData(), G->Vertices.Num() * sizeof(FVector)));
        for (auto &F : G->Fragments)
        {
            F.OriginalMaterial=F.Material;
            F.BaseAtoms=MakeShared<FAC2Atoms>(Atoms);
            F.Material = AppearanceMaterial(F.Material, Type, Atoms);
        }
        return G;
    }
    void ApplyAppearanceNode(uint32 Effect,const FAC2FXNode& Node,float Scalar)
    {
        if(!Node.AppearanceTable || !FMath::IsFinite(Node.AppearanceScalar))return;
        auto& Keys=LiveAppearanceFX.FindOrAdd(Effect);
        Keys.RemoveAll([&](const FAC2AppearanceKey& Key){
            return Key.Table==Node.AppearanceTable && Key.Key==Node.AppearanceKey;
        });
        // DoAppearanceFX removes the key at zero; applying an opacity atom at
        // zero would instead hide the character and leave a stale override.
        const float Applied=Node.AppearanceScalar*Scalar;
        if(FMath::IsFinite(Applied) && Applied>0)
            Keys.Add({Node.AppearanceTable,Node.AppearanceKey,Applied});
        if(Keys.IsEmpty())LiveAppearanceFX.Remove(Effect);
    }
    // FXManager::DoAppearanceFX changes materials on the existing part groups.
    // Keep the skeleton, running action tracks, sockets and dyed base appearance.
    // Cache by selected atoms (not the continuously changing health fraction).
    void RefreshAppearanceFX()
    {
        TArray<uint32> Effects;LiveAppearanceFX.GetKeys(Effects);Effects.Sort();
        DamageAppearanceHash=0;
        for(auto& Binding:AppearanceBindings)if(auto* MeshComponent=Binding.Mesh.Get())
        {
            FAC2Atoms Atoms=Binding.Atoms?*Binding.Atoms:FAC2Atoms();
            for(uint32 Effect:Effects)for(const auto& Key:LiveAppearanceFX[Effect])CollectAtoms(Key,Atoms);
            FString Cache=FString::Printf(TEXT("%08X:%08X:%08X"),Binding.Original,Binding.MeshType,Binding.Base);
            for(const auto& Atom:Atoms.Materials)
                Cache+=FString::Printf(TEXT(":%g/%08X/%08X/%08X/%08X"),Atom.Priority,Atom.Template,Atom.Modifier,Atom.MeshType,Atom.MaterialType);
            const float Opacity = Atoms.Opacity.Value;
            // Fade scalars change continuously: cache one translucent material
            // per atom combination, then update its uniform without allocations.
            Cache += Opacity < 1.f ? TEXT(":fade") : TEXT(":solid");
            uint32 MaterialId=Binding.Base;
            if(!Effects.IsEmpty())
            {
                if(const auto* Found=AppearanceFXMaterials.Find(Cache))MaterialId=*Found;
                else
                {
                    const uint32 BaseAppearanceHash=AppearanceHash;
                    if (Opacity < 1.f) Atoms.Opacity.Value = .5f;
                    MaterialId=AppearanceMaterial(Binding.Original,Binding.MeshType,Atoms);
                    AppearanceHash=BaseAppearanceHash;
                    AppearanceFXMaterials.Add(Cache,MaterialId);
                }
            }
            if(auto Decoded=Material(MaterialId))for(const auto& Layer:Decoded->Layers)
            {
                DamageAppearanceHash=HashCombineFast(DamageAppearanceHash,FCrc::MemCrc32(&Layer.Tint,sizeof(Layer.Tint)));
                for(uint32 TextureId:Layer.Textures)DamageAppearanceHash=HashCombineFast(DamageAppearanceHash,TextureId);
            }
            auto* MaterialObject=RuntimeMaterial(MaterialId);
            if (auto* MID = Cast<UMaterialInstanceDynamic>(MaterialObject))
                MID->SetScalarParameterValue(TEXT("AC2AppearanceOpacity"), Opacity);
            MeshComponent->SetMeshSectionVisible(Binding.Section,Opacity > 0.f && !InvisibleMaterials.Contains(MaterialId));
            if(Binding.Applied==MaterialId)continue;
            Binding.Applied=MaterialId;
            MeshComponent->SetMaterial(Binding.Section,MaterialObject);
            if(!PreviewMode)RegisterAC2WorldMaterial(Cast<UMaterialInstanceDynamic>(MaterialObject),MeshComponent,false);
        }
    }
    TMap<FString, TSharedPtr<FAC2DecodedGeometry>> SceneAppearanceMeshes;
    TSharedPtr<FAC2DecodedGeometry> SceneAppearanceMesh(uint32 Id, const FAC2Atoms &Atoms)
    {
        if (Atoms.Materials.IsEmpty() && Atoms.Meshes.IsEmpty() && !Atoms.Opacity.Defined) return Mesh(Id);
        FString Key = FString::Printf(TEXT("%08X"), Id);
        Key += FString::Printf(TEXT(":opacity=%g"), Atoms.Opacity.Value);
        for (const auto &A : Atoms.Materials)
            Key += FString::Printf(TEXT("M%08X:%08X:%08X:%08X"), A.Template, A.Modifier, A.MeshType, A.MaterialType);
        for (const auto &A : Atoms.Meshes)
            Key += FString::Printf(TEXT("G%08X:%08X"), A.Type, A.Mesh);
        if (const auto *Found = SceneAppearanceMeshes.Find(Key)) return *Found;
        auto G = AppearanceMesh(Id, Atoms);
        SceneAppearanceMeshes.Add(Key, G);
        return G;
    }
    bool Visual(uint32 Id, FAC2DecodedVisual &V, int32 Depth = 0)
    {
        if (Depth > 8)
            return false;
        if (auto *Cached = Visuals.Find(Id))
        {
            V = *Cached;
            return true;
        }
        TArray<uint8> B;
        if (!Read(Id, B))
            return false;
        FAC2AssetReader R(MoveTemp(B));
        V.LocalizedSign = Id == 0x1f000e4a; // shared localized town-sign visual
        const uint32 Bits = R.Word();
        if (Bits & 1)
        {
            if (R.Word() != Id)
                return false;
        }
        if (Bits & 2)
            V.Parent = R.Word();
        if (Bits & 4)
            V.MotionDesc=R.Word();
        if (Bits & 8)
            V.BehaviorTable=R.Word();
        if (Bits & 16)
            R.Word();
        if (Bits & 32)
            V.Scale = R.Vector();
        if (Bits & 64)
            R.Float();
        if (Bits & 1024)
            V.ParticleScale = R.Float();
        if (Bits & 128)
        {
            const int32 N = R.Count(12);
            for(int32 I=0;I<N;++I){FAC2IconLayer Icon;Icon.Layer=R.Word();Icon.Image=R.Word();Icon.Color=R.Word();V.Icons.Add(Icon);}
        }
        if (Bits & 256)
            V.FX = Part(R);
        if (Bits & 512)
        {
            const uint32 N = R.Word() & 65535;
            for (uint32 I = 0; I < N && R.Good; ++I)
            {
                uint32 Key = R.Word();
                auto P = Part(R);
                if (!(P.Fields & 1))
                    P.Key = Key;
                V.Parts.Add(P);
            }
        }
        if (!R.End())
            return false;
        if (V.Parent)
        {
            FAC2DecodedVisual Parent;
            if (!Visual(V.Parent, Parent, Depth + 1))
                return false;
            V.LocalizedSign |= Parent.LocalizedSign;
            for(const auto& Icon:Parent.Icons)
                if(!V.Icons.ContainsByPredicate([&](const auto& Child){return Child.Layer==Icon.Layer;}))V.Icons.Add(Icon);
            if(!(Bits&4))V.MotionDesc=Parent.MotionDesc;
            if(!(Bits&8))V.BehaviorTable=Parent.BehaviorTable;
            if (!(Bits & 32))
                V.Scale = Parent.Scale;
            if (!(Bits & 1024))
                V.ParticleScale = Parent.ParticleScale;
            if (!V.FX.FXTable)
                V.FX.FXTable = Parent.FX.FXTable;
            for (const auto &Key : Parent.FX.Appearance)
                V.FX.Appearance.Insert(Key, 0);
            for (uint32 Key : Parent.FX.Startup)
            {
                V.FX.Startup.AddUnique(Key);
                if(!V.FX.StartupStrength.Contains(Key))V.FX.StartupStrength.Add(Key,Parent.FX.StartupStrength.FindRef(Key));
            }
            for (const auto &Override : Parent.FX.Overrides)
                if (!V.FX.Overrides.Contains(Override.Key))
                    V.FX.Overrides.Add(Override.Key, Override.Value);
            for (const FAC2DecodedPart &P : Parent.Parts)
            {
                auto *Child =
                    V.Parts.FindByPredicate([&](const FAC2DecodedPart &C) { return C.Key == P.Key; });
                if (!Child)
                {
                    V.Parts.Add(P);
                    continue;
                }
                if (!(Child->Fields & 2))
                    Child->Parent = P.Parent;
                if (!(Child->Fields & 4))
                    Child->Connection = P.Connection;
                if (!(Child->Fields & 8))
                    Child->Setup = P.Setup;
                if (!(Child->Fields & 16))
                    Child->AnimMap = P.AnimMap;
                if (!(Child->Fields & 32))
                    Child->Appearance = P.Appearance;
                if (!(Child->Fields & 64))
                    Child->FXTable = P.FXTable;
                if (!(Child->Fields & 128))
                {
                    Child->Startup = P.Startup;
                    Child->StartupStrength = P.StartupStrength;
                }
                if (!(Child->Fields & 256))
                    Child->Overrides = P.Overrides;
            }
        }
        Visuals.Add(Id, V);
        return true;
    }
    void StartFX(const FAC2DecodedVisual &V, const FTransform &Frame)
    {
        if (SkyMode || Animating)
            return;
        // Native PartGroupDataDesc::FillFXTable overlays appearance atoms after
        // explicit overrides. Static scenery must use the same selected FX as
        // animated models (for example the lineup's blue brazier variant).
        FAC2Atoms Atoms;
        for (const auto& Key : V.FX.Appearance) CollectAtoms(Key, Atoms);
        TArray<FAC2FXNode> Nodes;
        TArray<float> Strengths;
        for (uint32 Key : V.FX.Startup)
        {
            const int32 First=Nodes.Num();
            if (const auto* Appearance = Atoms.Effects.Find(Key))
                Nodes.Append(*Appearance);
            else if (const auto* Override = V.FX.Overrides.Find(Key))
                Nodes.Append(*Override);
            else if (V.FX.FXTable)
                TableFX(V.FX.FXTable, Key, Nodes);
            for(int32 I=First;I<Nodes.Num();++I)Strengths.Add(V.FX.StartupStrength.FindRef(Key));
        }
        FAC2Setup Setup;
        TArray<uint8> Bytes;
        for (const auto &P : V.Parts)
            if (!P.Parent && P.Setup && Read(P.Setup, Bytes) && Setup.Decode(P.Setup, Bytes))
                break;
        auto Rest = Setup.World(Setup.Rest);
        uint32 Terrain = 0;
        if (EnvironmentLand)
        {
            const FVector P = Frame.GetLocation() / 100;
            const int32 X = FMath::Clamp(FMath::RoundToInt(P.X / 10), 0, 16);
            const int32 Y = FMath::Clamp(FMath::RoundToInt(P.Y / 10), 0, 16);
            Terrain = EnvironmentLand->CellInfo[X * 17 + Y] & EnvironmentTerrainMask;
        }
        for (int32 Index=0;Index<Nodes.Num();++Index)
        {
            const auto& Default=Nodes[Index];
            const auto *Override = Default.Terrain.Find(Terrain);
            const auto &Node = Override ? **Override : Default;
            if(!Node.Sounds.IsEmpty() && !Node.PlayerOnly && Strengths[Index]>0)
            {
                FAC2ScenerySound Source;
                // FXManager::DoSoundFX uses the object's origin, not the particle
                // bone/holding offset. SoundInfo keeps its own native attenuation.
                Source.Position=Frame.GetLocation();
                const uint32 Choice=HashCombineFast(GetTypeHash(Source.Position),Index)%Node.Sounds.Num();
                Source.Sound=Node.Sounds[Choice];Source.Volume=Strengths[Index];
                Source.Channel=SoundChannels.FindRef(Source.Sound);
                Source.DayTime=Node.DayTime;Source.NightTime=Node.NightTime;
                Source.Channels=Node.Channels;
                Sounds.Add(MoveTemp(Source));
            }
            if (Node.Repulsor.Enabled && !Node.PlayerOnly && Sources.Num() < 256)
            {
                FAC2ParticleSource Source;
                Source.Repulsor = Node.Repulsor;
                Source.Frame = Frame;
                Sources.Add(MoveTemp(Source));
            }
            if (Node.Descriptors.IsEmpty() || Sources.Num()>=256)
                continue;
            FTransform Attachment(Node.Offset * 100);
            if (Node.Holding)
            {
                if (const auto *Socket = Setup.Sockets.Find(Node.Holding))
                    if (Rest.IsValidIndex(Socket->Bone))
                        Attachment =
                            Attachment * FTransform(Socket->Frame.ToMatrixWithScale() * Rest[Socket->Bone]);
            }
            else if (Node.Bone)
            {
                int32 Bone = Setup.Names.IndexOfByKey(Node.Bone);
                if (Rest.IsValidIndex(Bone))
                    Attachment = Attachment * FTransform(Rest[Bone]);
            }
            FAC2ParticleSource Source;
            Source.Descriptor = Node.Descriptors[Sources.Num() % Node.Descriptors.Num()];
            if ((Source.Descriptor >> 24) != 0x39)
                continue;
            if (Node.RemoveRotation)
                Attachment.SetRotation(FQuat::Identity);
            Source.Frame = Attachment * Frame;
            if (Node.Water && BlockWater.Present)
            {
                auto P = Source.Frame.GetLocation();
                P.Z = BlockWater.Height * 100;
                Source.Frame.SetLocation(P);
            }
            Source.ParticleScale = V.ParticleScale;
            Source.Shared = true;
            Sources.Add(MoveTemp(Source));
        }
    }

    bool StartSound(const FAC2FXNode& Node,uint32 Effect,float Strength,bool Startup=false,bool CreatureIdle=false)
    {
        if(PreviewMode || !ModelRoot.IsValid() || Node.Sounds.IsEmpty() || Strength<=0)return false;
        auto* World=ModelRoot->GetWorld();auto* Audio=FAC2Audio::Get(World);
        if(!Audio)return false;
        const auto* Game=World->GetAuthGameMode<AAC2GameMode>();
        const float Hour=Game?Game->MapHour():12;
        const auto* Player=World->GetFirstPlayerController();
        if(!(Hour>=6 && Hour<18?Node.DayTime:Node.NightTime) ||
            (Node.PlayerOnly && (!Player || Player->GetPawn()!=ModelRoot->GetOwner())) ||
            !FAC2AssetLibrary::EffectChannelsMatch(Node.Channels))return false;
        const uint32 Sound=Node.Sounds[FMath::RandHelper(Node.Sounds.Num())];
        if(CreatureIdle && !LoopingSounds.FindRef(Sound))
        {
            // A reedshark idle contains eight authored breath cues in 6.67 s. Space out
            // ambient opportunities per creature while retaining each cue's native timing.
            if(PreviousAnimationTime<NextIdleSoundRoll)return false;
            NextIdleSoundRoll=PreviousAnimationTime+FMath::FRandRange(6.f,12.f);
            if(FMath::FRand()>=.4f)return false;
        }
        FAC2FXScalar Scalar;Scalar.Start(Strength,Node.RampTime,Startup);
        if(const uint32 Context=Audio->Play(Sound,ModelRoot.Get(),Scalar.Current,SoundChannels.FindRef(Sound)))
        {
            if(Startup || (LoopingSounds.FindRef(Sound) && !Node.FireAndForget))
            {
                auto& State=ScalarEffects.FindOrAdd(Effect);State.Contexts.Add(Context);
                State.Scalar=Scalar;State.Startup=Startup;
            }
            return true;
        }
        return false;
    }

    TSharedPtr<FAC2DecodedGeometry> Mesh(uint32 Id)
    {
        if (auto *Existing = Geometry.Find(Id))
            return *Existing;
        Geometry.Add(Id, nullptr);
        TArray<uint8> B;
        if (!Read(Id, B))
            return nullptr;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Id)
            return nullptr;
        const uint32 MeshType = R.Word();
        const int32 NM = R.Count(4, 4096);
        TArray<uint32> Mats;
        for (int32 I = 0; I < NM; ++I)
            Mats.Add(R.Word());
        const uint32 FVF = R.Word();
        const int32 NV = R.Count(12, 1000000);
        const uint32 Position = FVF & 14;
        const int32 Weights = Position >= 6 ? (Position - 4) / 2 : 0;
        int32 Stride = (Position ? 12 : 0) + Weights * 4, Normal = -1, UV = -1, Diffuse = -1;
        if (FVF & 16)
        {
            Normal = Stride;
            Stride += 12;
        }
        if (FVF & 32)
            Stride += 4;
        if (FVF & 64)
        {
            Diffuse = Stride;
            Stride += 4;
        }
        if (FVF & 128)
            Stride += 4;
        if (FVF & 0x10000000)
            Stride += 12;
        if (FVF & 0x20000000)
            Stride += 12;
        if (FVF & 0x40000000)
            Stride += 12;
        const int32 UVCount = FMath::Min((FVF >> 8) & 15, 8u);
        if (UVCount)
            UV = Stride;
        Stride += UVCount * 8;
        const int32 MatrixOffset = Stride, MatrixCount = (FVF >> 16) & 255;
        Stride += MatrixCount * 5;
        if (FVF & 0x01000000)
            Stride = (Stride + 31) & ~31;
        const int32 Start = R.Pos;
        if (!Position || Stride < 12 || !R.Take(int64(Stride) * NV))
            return nullptr;
        auto G = MakeShared<FAC2DecodedGeometry>();
        G->Type = MeshType;
        G->Vertices.Reserve(NV);
        G->Normals.Reserve(NV);
        G->UVs.Reserve(NV);
        for (int32 I = 0; I < NV; ++I)
        {
            const uint8 *P = R.Data.GetData() + Start + I * Stride;
            const FVector V(AC2::F32(P), AC2::F32(P + 4), AC2::F32(P + 8));
            if (V.ContainsNaN() || V.GetAbsMax() > 1000000)
                return nullptr;
            G->Vertices.Add(V * 100);
            if (MatrixCount)
            {
                FAC2DecodedGeometry::FInfluence S;
                for (int32 J = 0; J < FMath::Min(MatrixCount, 4); ++J)
                {
                    S.Bones[J] = P[MatrixOffset + J];
                    S.Weights[J] = AC2::F32(P + MatrixOffset + MatrixCount + J * 4);
                    if (!FMath::IsFinite(S.Weights[J]) || S.Weights[J] < 0 || S.Weights[J] > 1.01f)
                        return nullptr;
                }
                G->Skin.Add(S);
            }
            G->Colors.Add(Diffuse >= 0 ? FColor(AC2::U32(P + Diffuse)) : FColor::White);
            const FVector N = Normal >= 0 ? FVector(AC2::F32(P + Normal), AC2::F32(P + Normal + 4),
                                                    AC2::F32(P + Normal + 8))
                                          : FVector::UpVector;
            if (N.ContainsNaN())
                return nullptr;
            G->Normals.Add(N.GetSafeNormal());
            FVector2D T =
                UV >= 0 ? FVector2D(AC2::F32(P + UV), AC2::F32(P + UV + 4)) : FVector2D::ZeroVector;
            // Shared localized road-arrow meshes (including their distant LOD).
            // Correct the lettering's horizontal texture orientation without
            // reflecting the arrow geometry: its tip must keep pointing toward
            // the DAT-authored destination. Both faces have separate UV charts.
            if(Id==0x06000554 || Id==0x06000555)T.X=1.-T.X;
            if (T.ContainsNaN())
                return nullptr;
            G->UVs.Add(T);
            const FVector2D Detail = UVCount > 1 ? FVector2D(AC2::F32(P + UV + 8), AC2::F32(P + UV + 12)) : T;
            if (Detail.ContainsNaN()) return nullptr;
            G->DetailUVs.Add(Detail);
            for(int32 Channel=2;Channel<4;++Channel) {
                FVector2D Extra=UVCount>Channel?FVector2D(AC2::F32(P+UV+Channel*8),AC2::F32(P+UV+Channel*8+4)):T;
                if(Extra.ContainsNaN())return nullptr;
                (Channel==2?G->UV2:G->UV3).Add(Extra);
            }
        }
        R.Align();
        R.Take(40);
        const int32 ND = R.Count(12, 256);
        bool Preferred = false;
        for (int32 D = 0; D < ND && R.Good; ++D)
        {
            R.Word();
            const uint32 Bits = R.Word();
            const int32 NF = R.Count(8, 4096);
            TArray<FAC2DecodedFragment> Fragments;
            for (int32 J = 0; J < NF && R.Good; ++J)
            {
                const uint32 Slot = R.Word();
                const int32 NI = R.Count(2, 3000000);
                FAC2DecodedFragment F;
                if (!Mats.IsValidIndex(Slot) || NI % 3)
                {
                    R.Good = false;
                    break;
                }
                F.Material = Mats[Slot];
                F.Indices.Reserve(NI);
                for (int32 K = 0; K < NI; ++K)
                {
                    const uint16 Index = R.Short();
                    if (Index >= NV)
                        R.Good = false;
                    F.Indices.Add(Index);
                }
                R.Align();
                // AC mesh data is counter-clockwise; UE's visible face uses clockwise order.
                for (int32 K = 0; K < NI; K += 3)
                    Swap(F.Indices[K + 1], F.Indices[K + 2]);
                Fragments.Add(MoveTemp(F));
            }
            if (D == 0 || (!Preferred && (Bits & 4)))
            {
                G->Fragments = MoveTemp(Fragments);
                Preferred = (Bits & 4) != 0;
            }
        }
        if (((Id >> 24) == 6 || (Id >> 24) == 1) && R.Good)
        {
            FAC2AssetReader Physics(R.Data);
            Physics.Pos=R.Pos;
            G->NativeCollision=ReadAC2StaticCollision(Physics,NV,G->PhysicsIndices,(Id >> 24) == 1);
            if (!G->NativeCollision) Warn(Id,TEXT("unsupported static collision payload"));
        }
        if (!R.Good)
        {
            Warn(Id, TEXT("unsupported mesh layout"));
            return nullptr;
        }
        Geometry[Id] = G;
        return G;
    }

    UProceduralMeshComponent *RenderDraw(const FDraw &Draw, AActor *Owner, USceneComponent *Root)
    {
        check(IsInGameThread());
        const auto &G = Draw.Mesh;
        TGuardValue<uint32> RoomContext(DrawingCell,Draw.Cell?Draw.Cell:DrawingCell);
        auto *C = NewObject<UProceduralMeshComponent>(Owner);
        Owner->AddInstanceComponent(C);
        C->SetupAttachment(Root);
        C->SetRelativeTransform(Draw.Transform);
        C->bUseAsyncCooking = true;
        C->SetCollisionProfileName(SkyMode || Animating ? TEXT("NoCollision") : TEXT("BlockAll"));
        C->SetCastShadow(!SkyMode);
        // CPU skinning changes vertices without moving the component. VSM
        // pages must invalidate for these meshes even while the actor is idle.
        if(Animating && !SkyMode)C->ShadowCacheInvalidationBehavior=EShadowCacheInvalidationBehavior::Always;
        if (Draw.Frill)
        {
            C->SetCullDistance(20000); // Ground cover fades at 120-160 m, before batch culling.
            FBox FrillBounds(ForceInit);for(const auto& V:G->Vertices)FrillBounds+=V;
            const auto Extent=FrillBounds.GetExtent();
            C->BoundsScale=1+75.f/FMath::Max(1.,FMath::Min3(Extent.X,Extent.Y,Extent.Z));
        }
        C->RegisterComponent();
        Stats.FrillBatchesUploaded += Draw.Frill ? 1 : 0;
        bool HasCollision = false;
        for (int32 J = 0; J < G->Fragments.Num(); ++J)
        {
            const auto &F = G->Fragments[J];
            auto *Material = RuntimeMaterial(F.Material, Draw.Frill);
            if (Draw.Frill)
            {
                Stats.GrassDepthSections += Material && Material->GetBlendMode() == BLEND_Masked ? 1 : 0;
                Stats.GrassSortedSections += !Material || Material->GetBlendMode() != BLEND_Masked ? 1 : 0;
            }
            auto Authored = this->Material(F.Material);
            const bool VertexColors =
                Authored && Authored->Layers.Num() &&
                (Authored->Layers[SelectedLayer(*Authored)].VertexDiffuse || Authored->Layers[SelectedLayer(*Authored)].ModulateVertexDiffuse);
            // Leaf cards and translucent surfaces don't become invisible collision walls.
            const bool Collision = !G->NativeCollision && Draw.Solid && !SkyMode && !Animating && Material && !F.Indices.IsEmpty() &&
                                   (Draw.Structural || Material->GetBlendMode() == BLEND_Opaque);
            HasCollision |= Collision;
            // Reuse the fourth UV only when the DAT material does not read it.
            // Preserve all authored texture coordinates and vertex colours.
            const bool Bend=Draw.Frill && G->FrillBend.Num()==G->Vertices.Num() && Authored && !Authored->Layers.IsEmpty() &&
                !Authored->Layers[SelectedLayer(*Authored)].TextureCoordinates.Contains(3);
            if(Draw.Frill && !Bend && Material){
                // A DAT that consumes UV3 must retain those coordinates, without
                // inheriting the shared grass material's deformation switch.
                auto* Fixed=UMaterialInstanceDynamic::Create(Material,C);
                Fixed->SetScalarParameterValue(TEXT("AC2FoliageBend"),0);Material=Fixed;
            }
            if(Draw.Frill)if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(Material))RegisterAC2FoliageMaterial(Dynamic,C,Bend);
            C->CreateMeshSection(J, G->Vertices, F.Indices, G->Normals, G->UVs, G->DetailUVs, G->UV2, Bend?G->FrillBend:G->UV3,
                                 VertexColors ? G->Colors : TArray<FColor>(), TArray<FProcMeshTangent>(),
                                 Collision);
            C->SetMaterial(J, Material);
            if(Animating && ModelRoot.IsValid() && !SkyMode)
                AppearanceBindings.Add({C,J,F.Material,F.OriginalMaterial?F.OriginalMaterial:F.Material,G->Type,F.Material,F.BaseAtoms});
            if(!SkyMode && !PreviewMode) RegisterAC2WorldMaterial(Cast<UMaterialInstanceDynamic>(Material),C,Draw.Structural);
            if (InvisibleMaterials.Contains(F.Material) || (Authored && Authored->AppearanceOpacity <= 0.f))
                C->SetMeshSectionVisible(J, false);
            ++Stats.ScenerySections;
            Stats.SceneryTriangles += F.Indices.Num() / 3;
        }
        if (G->NativeCollision && Draw.Solid && !SkyMode && !Animating && !G->PhysicsIndices.IsEmpty())
        {
            const int32 Section=G->Fragments.Num();
            C->bUseComplexAsSimpleCollision=true;
            C->CreateMeshSection(Section,G->Vertices,G->PhysicsIndices,TArray<FVector>(),TArray<FVector2D>(),
                TArray<FColor>(),TArray<FProcMeshTangent>(),true);
            C->SetMeshSectionVisible(Section,false);
            HasCollision=true;
        }
        if (HasCollision)
            CollisionMeshes.Add(C);
        const uint32 Cell = Draw.Cell ? Draw.Cell : DrawingCell;
        CellMeshes.Add({C, Cell, HasCollision});
        const bool Visible = CellVisible(Cell);
        C->SetVisibility(Visible);
        if (!Visible) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(!SkyMode && !PreviewMode)RegisterAC2FogComponent(C);
        return C;
    }
    bool AnimatedSetup(const FAC2DecodedPart &P, const FTransform &Frame, AActor *Owner,
                       USceneComponent *Root, int32 Parent, const FAC2Atoms &Atoms)
    {
        TArray<uint8> B;
        if (!Read(P.Setup, B))
            return false;
        FAnimatedGroup Group;
        if (!Group.Setup.Decode(P.Setup, B))
            return false;
        Group.Frame = Frame;
        Group.PartFrame = Frame.ToMatrixWithScale();
        Group.Parent = Parent;
        Group.Map=P.AnimMap;
        if (Animated.IsValidIndex(Parent))
        {
            const auto *S = Animated[Parent].Setup.Sockets.Find(P.Connection);
            if(!S || !Animated[Parent].Setup.Parents.IsValidIndex(S->Bone))return false;
            Group.Socket=*S;
        }
        for (const FMatrix &M : Group.Setup.World(Group.Setup.Bind))
            Group.BindInverse.Add(M.Inverse());
        Group.Idle = FAC2Animation::Load(Portal, P.AnimMap, 0x80000012);
        Group.Locomotion = FAC2Animator::Load(Portal, P.AnimMap, 0x80000001);
        for (int32 I = 0; I < Group.Setup.Meshes.Num(); ++I)
            if (auto G = AppearanceMesh(Group.Setup.Meshes[I], Atoms))
            {
                auto *C = RenderDraw({G, Frame, false}, Owner, Root);
                Group.Meshes.Add({G, C, I});
                Group.Skinned |= !G->Skin.IsEmpty();
            }
        if (Group.Meshes.IsEmpty())
            return false;
        UE_LOG(LogTemp, Display,
               TEXT("AC2 native skeleton %08X: %d bones, %d meshes, idle=%d locomotion=%d map=%08X"), P.Setup,
               Group.Setup.Parents.Num(), Group.Meshes.Num(), !!Group.Idle, !!Group.Locomotion, P.AnimMap);
        Animated.Add(MoveTemp(Group));
        return true;
    }
    bool PrepareMovingSetup(const FAC2DecodedPart &P, const FTransform &Frame, const FAC2Atoms &Atoms)
    {
        if (!Preparing || Decoration || !P.AnimMap || Animated.Num() >= 32)
            return false;
        auto Idle = FAC2Animation::Load(Portal, P.AnimMap, 0x80000012);
        if (!Idle)
            return false;
        TArray<uint8> B;
        FAnimatedGroup G;
        if (!Read(P.Setup, B) || !G.Setup.Decode(P.Setup, B))
            return false;
        G.Frame = Frame;
        G.PartFrame = Frame.ToMatrixWithScale();
        G.Idle = Idle;
        for (const FMatrix &M : G.Setup.World(G.Setup.Bind))
            G.BindInverse.Add(M.Inverse());
        bool Any = false;
        const int32 Group = Animated.Num();
        // CPartArray::RefreshMatrices treats the whole group as skinned when
        // any part has weights. Its unweighted collision part stays in group
        // space; mesh ordinal 1 is not bone 1 of an animated hanging ornament.
        TArray<TSharedPtr<FAC2DecodedGeometry>> Parts;
        for(uint32 MeshId:G.Setup.Meshes) {
            auto Part=SceneAppearanceMesh(MeshId,Atoms);
            G.Skinned |= Part && !Part->Skin.IsEmpty();
            Parts.Add(Part);
        }
        const auto Rest=G.Setup.World(G.Setup.Rest,false);
        for (int32 I = 0; I < G.Setup.Meshes.Num(); ++I)
            if (auto GeometryData = Parts[I])
            {
                if (Draws.Num() >= 4096)
                    break;
                Draws.Add({GeometryData, Frame, false, false, Group, I});
                // Decorative animation must not remove the native walkable
                // surface. These unweighted physics vertices are static in
                // setup space; never cook deforming cloth as a walkable mesh.
                if(GeometryData->NativeCollision && !GeometryData->PhysicsIndices.IsEmpty() && GeometryData->Skin.IsEmpty()) {
                    auto Collision=MakeShared<FAC2DecodedGeometry>(*GeometryData);
                    Collision->Fragments.Reset();
                    const FTransform CollisionFrame=G.Skinned || !Rest.IsValidIndex(I)?Frame:FTransform(Rest[I])*Frame;
                    Draws.Add({Collision,CollisionFrame,true,false});
                }
                for (const auto &F : GeometryData->Fragments)
                {
                    FLinearColor Tint;
                    bool Masked = false;
                    MaterialImage(F.Material, Tint, Masked);
                }
                Any = true;
            }
        if (Any) {
            SetupCollision(G.Setup,Frame,nullptr,nullptr);
            Animated.Add(MoveTemp(G));
        }
        return Any;
    }
    void SetupCollision(const FAC2Setup& Setup,const FTransform& Placement,AActor* Owner,USceneComponent* Root)
    {
        if(!EnvironmentLand || Decoration || SkyMode || Animating)return;
        // CPhysicsObj::FindObjCollisions prefers the physics tree, then setup
        // cylinders, then setup spheres. Never turn leaf cards into blockers.
        for(uint32 Id:Setup.Meshes)if(auto G=Mesh(Id);G && (!G->PhysicsIndices.IsEmpty() || !G->Skin.IsEmpty()))return;
        auto G=MakeShared<FAC2DecodedGeometry>();G->NativeCollision=true;
        constexpr int32 Sides=32;
        const FVector Scale=Placement.GetScale3D().GetAbs();
        for(const auto& C:Setup.Cylinders)if(C.Radius>0 && C.Height>0) {
            const FVector Low=Placement.TransformPosition(C.Low);
            // Native CCylSphere stays upright even when the scenery is tilted.
            const double Radius=C.Radius*(Scale.X+Scale.Y)*.5,Height=C.Height*Scale.Z;
            const int32 Base=G->Vertices.Num();
            for(int32 Z=0;Z<2;++Z)for(int32 I=0;I<Sides;++I) {
                const double A=2*PI*I/Sides;
                G->Vertices.Add(Low+FVector(Radius*FMath::Cos(A),Radius*FMath::Sin(A),Z*Height));
            }
            for(int32 I=0;I<Sides;++I){const int32 A=Base+I,B=Base+(I+1)%Sides;
                G->PhysicsIndices.Append({A,B,A+Sides,B,B+Sides,A+Sides});
            }
            for(int32 I=1;I<Sides-1;++I)G->PhysicsIndices.Append({Base,Base+I+1,Base+I,Base+Sides,Base+Sides+I,Base+Sides+I+1});
        }
        if(Setup.Cylinders.IsEmpty())for(const auto& S:Setup.Spheres)if(S.Radius>0) {
            const FVector Center=Placement.TransformPosition(S.Center);const double Radius=S.Radius*Scale.X;
            const int32 Base=G->Vertices.Num();constexpr int32 Rings=16;
            for(int32 J=0;J<=Rings;++J)for(int32 I=0;I<Sides;++I){const double A=2*PI*I/Sides,B=PI*J/Rings;
                G->Vertices.Add(Center+Radius*FVector(FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A),FMath::Cos(B)));
            }
            for(int32 J=0;J<Rings;++J)for(int32 I=0;I<Sides;++I){const int32 A=Base+J*Sides+I,B=Base+J*Sides+(I+1)%Sides;
                if(J>0)G->PhysicsIndices.Append({A,A+Sides,B});
                if(J<Rings-1)G->PhysicsIndices.Append({B,A+Sides,B+Sides});
            }
        }
        if(G->PhysicsIndices.IsEmpty())return;
        if(Preparing)Draws.Add({G,FTransform::Identity,true,false});
        else RenderDraw({G,FTransform::Identity,true,false},Owner,Root);
    }
    bool DrawSetup(uint32 Id, const FTransform &Placement, AActor *Owner, USceneComponent *Root,
                   bool RecordBounds, const FAC2Atoms &Atoms = {})
    {
        TArray<uint8> B;
        if (!Read(Id, B))
            return false;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != Id)
            return false;
        R.Word();
        const int32 NM = R.Count(4, 4096);
        TArray<uint32> Meshes;
        for (int32 I = 0; I < NM; ++I)
            Meshes.Add(R.Word());
        const int32 NB = R.Count(8, 4096);
        TArray<uint32> Parents;
        for (int32 I = 0; I < NB; ++I)
        {
            Parents.Add(R.Word());
            R.Word();
        }
        const int32 NP = R.Count(8, 4096);
        TArray<FTransform> Rest;
        for (int32 I = 0; I < NP && R.Good; ++I)
        {
            R.Word();
            const int32 N = R.Count(40, 4096);
            for (int32 J = 0; J < N; ++J)
            {
                const FTransform T = Transform(R);
                if (!I)
                    Rest.Add(T);
            }
        }
        if (!R.Good)
            return false;
        TArray<FTransform> World;
        World.SetNum(NB);
        for (int32 I = 0; I < NB; ++I)
        {
            World[I] = Rest.IsValidIndex(I) ? Rest[I] : FTransform::Identity;
            if (Parents[I] < uint32(I))
                World[I] = World[I] * World[Parents[I]];
        }
        bool Drawn = false;
        bool Skinned=false;
        for(uint32 MeshId:Meshes)if(auto Part=Mesh(MeshId))Skinned|=!Part->Skin.IsEmpty();
        for (int32 I = 0; I < NM; ++I)
        {
            if (!Meshes[I])
                continue;
            auto G = SceneAppearanceMesh(Meshes[I], Atoms);
            if (!G)
                continue;
            if (Stats.ScenerySections >= 12000)
            {
                Warn(Id, TEXT("patch scenery section limit reached"));
                break;
            }
            const FTransform MeshTransform =
                (!Skinned && World.IsValidIndex(I) ? World[I] : FTransform::Identity) * Placement;
            if (RecordBounds)
            {
                FBox Box(ForceInit);
                for (const FVector &V : G->Vertices)
                    Box += MeshTransform.TransformPosition(V);
                PlacedBounds.Add(Box);
            }
            if (Preparing)
            {
                if (Decoration)
                    QueueFrill(G, MeshTransform);
                else
                {
                    if (Draws.Num() >= 4096)
                        break;
                    Draws.Add({G, MeshTransform, true, false});
                }
                for (const auto &F : G->Fragments)
                {
                    FLinearColor Tint;
                    bool Masked = false;
                    MaterialImage(F.Material, Tint, Masked);
                }
            }
            else
                RenderDraw({G, MeshTransform, !Decoration, Decoration}, Owner, Root);
            Drawn = true;
        }
        if(Drawn){FAC2Setup Setup;if(Setup.Decode(Id,R.Data))SetupCollision(Setup,Placement,Owner,Root);}
        return Drawn;
    }
    // Grass retains its native mesh, transforms and material, but shares a draw
    // within a 20 m cell. Small bounds keep distance culling useful while walking.
    TMap<uint64, int32> FrillBatches;
    void QueueFrill(const TSharedPtr<FAC2DecodedGeometry> &Source, const FTransform &Frame)
    {
        const FVector P = Frame.GetLocation();
        TArray<FVector> Vertices;
        TArray<FVector2D> BendWeights;
        auto Fields = Source;
        double MinZ = TNumericLimits<double>::Max(),MaxZ=-TNumericLimits<double>::Max();
        for (const FVector &V : Source->Vertices){MinZ=FMath::Min(MinZ,V.Z);MaxZ=FMath::Max(MaxZ,V.Z);}
        // Native grass meshes can start a few centimetres above their origin.
        // Keep buried roots buried, but seat raised bases on the rendered terrain.
        const double BaseZ = FMath::Max(0.,MinZ);
        const double LocalHeight=FMath::Max(1.,MaxZ-BaseZ);
        const double PlantHeight=LocalHeight*FMath::Abs(Frame.GetScale3D().Z);
        auto BendWeight=[&](double Z){return FVector2D(FMath::Clamp((Z-BaseZ)/LocalHeight,0.,1.),PlantHeight);};
        FTransform LocalFrame = Frame;
        LocalFrame.AddToTranslation(-EnvironmentOffset);
        for (const FVector &V : Source->Vertices)
        {
            FVector World = Frame.TransformPosition(V);
            if (GroundDecoration && EnvironmentDefs && EnvironmentLand)
            {
                const FVector Fitted = FitAC2GroundFrill(*EnvironmentDefs,*EnvironmentLand,LocalFrame,V,BaseZ);
                if (FMath::Abs(V.Z-BaseZ) < .01)
                {
                    double Height; FVector Normal;
                    if (SampleAC2Terrain(*EnvironmentDefs,*EnvironmentLand,Fitted.X/100.,Fitted.Y/100.,Height,Normal))
                    {
                        ++Stats.GrassGroundContacts;
                        Stats.GrassMaxRootGapBefore = FMath::Max(Stats.GrassMaxRootGapBefore,
                            World.Z-EnvironmentOffset.Z-Height*100.);
                        Stats.GrassMaxRootGapAfter = FMath::Max(Stats.GrassMaxRootGapAfter,FMath::Abs(Fitted.Z-Height*100.));
                    }
                }
                World = Fitted + EnvironmentOffset;
            }
            if (!GroundDecoration || !EnvironmentDefs || !EnvironmentLand)
            {
                Vertices.Add(World);
                BendWeights.Add(BendWeight(V.Z));
            }
        }
        if (GroundDecoration && EnvironmentDefs && EnvironmentLand)
        {
            Fields = MakeShared<FAC2DecodedGeometry>();
            for (const auto &Fragment : Source->Fragments)
            {
                FAC2DecodedFragment F;
                F.Material = Fragment.Material;
                for (int32 T = 0; T < Fragment.Indices.Num(); T += 3)
                {
                    const int32 IA=Fragment.Indices[T],IB=Fragment.Indices[T+1],IC=Fragment.Indices[T+2];
                    const FVector A=Source->Vertices[IA],B=Source->Vertices[IB],C=Source->Vertices[IC];
                    const auto Cuts=SplitAC2GroundFrill(*EnvironmentLand,LocalFrame,A,B,C,BaseZ);
                    for (int32 Triangle=0;Triangle<Cuts.Num();Triangle+=3)
                    {
                        FVector Card[3], World[3];
                        for(int32 Corner=0;Corner<3;++Corner)
                        {
                            const FVector W=Cuts[Triangle+Corner];
                            Card[Corner]=A*W.X+B*W.Y+C*W.Z;
                            World[Corner]=FitAC2GroundFrill(*EnvironmentDefs,*EnvironmentLand,LocalFrame,Card[Corner],BaseZ);
                            F.Indices.Add(Vertices.Add(World[Corner]+EnvironmentOffset));
                            BendWeights.Add(BendWeight(Card[Corner].Z));
                            auto UV=[&](const TArray<FVector2D>& U){return U[IA]*W.X+U[IB]*W.Y+U[IC]*W.Z;};
                            Fields->UVs.Add(UV(Source->UVs)); Fields->DetailUVs.Add(UV(Source->DetailUVs));
                            Fields->UV2.Add(UV(Source->UV2)); Fields->UV3.Add(UV(Source->UV3));
                            const FColor CA=Source->Colors[IA],CB=Source->Colors[IB],CC=Source->Colors[IC];
                            auto Channel=[&](uint8 X,uint8 Y,uint8 Z){return uint8(FMath::Clamp(FMath::RoundToInt(X*W.X+Y*W.Y+Z*W.Z),0,255));};
                            Fields->Colors.Add(FColor(Channel(CA.R,CB.R,CC.R),Channel(CA.G,CB.G,CC.G),
                                Channel(CA.B,CB.B,CC.B),Channel(CA.A,CB.A,CC.A)));
                        }
                        // Check the rendered base edges as well as mesh corners.
                        for(int32 Edge=0;Edge<3;++Edge)
                        {
                            const int32 Next=(Edge+1)%3;
                            if(FMath::Abs(Card[Edge].Z-BaseZ)>.00001 || FMath::Abs(Card[Next].Z-BaseZ)>.00001)continue;
                            const FVector Mid=(World[Edge]+World[Next])*.5;
                            double Height; FVector Normal;
                            if(SampleAC2Terrain(*EnvironmentDefs,*EnvironmentLand,World[Edge].X/100.,World[Edge].Y/100.,Height,Normal) &&
                                SampleAC2Terrain(*EnvironmentDefs,*EnvironmentLand,World[Next].X/100.,World[Next].Y/100.,Height,Normal) &&
                                SampleAC2Terrain(*EnvironmentDefs,*EnvironmentLand,Mid.X/100.,Mid.Y/100.,Height,Normal))
                            {
                                ++Stats.GrassGroundContacts;
                                Stats.GrassMaxRootGapAfter=FMath::Max(Stats.GrassMaxRootGapAfter,FMath::Abs(Mid.Z-Height*100.));
                            }
                        }
                    }
                }
                Fields->Fragments.Add(MoveTemp(F));
            }
        }
        // FrillRender::RefreshFrillMesh replaces every vertex normal with the
        // placement's transformed up axis. Card normals make neighboring blades
        // alternate between bright and dark and can point under the ground.
        const FVector FrillNormal = Frame.GetUnitAxis(EAxis::Z);
        const int32 X = FMath::Clamp(FMath::FloorToInt(P.X / 2000), 0, 7);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(P.Y / 2000), 0, 7);
        for (const auto &Fragment : Fields->Fragments)
        {
            const uint64 Key = (uint64(X * 8 + Y) << 32) | Fragment.Material;
            int32 *Existing = FrillBatches.Find(Key);
            int32 Index = Existing ? *Existing : -1;
            if (Index < 0 || Draws[Index].Mesh->Vertices.Num() + Vertices.Num() > 60000)
            {
                auto Batch = MakeShared<FAC2DecodedGeometry>();
                FAC2DecodedFragment F;
                F.Material = Fragment.Material;
                Batch->Fragments.Add(MoveTemp(F));
                Index = Draws.Add(
                    {Batch, FTransform(FVector((X + .5) * 2000, (Y + .5) * 2000, P.Z)), false, true});
                FrillBatches.Add(Key, Index);
                ++Stats.FrillBatches;
            }
            auto &Draw = Draws[Index];
            auto &G = *Draw.Mesh;
            const int32 Base = G.Vertices.Num();
            for (int32 V = 0; V < Vertices.Num(); ++V)
            {
                G.Vertices.Add(Vertices[V] - Draw.Transform.GetLocation());
                G.Normals.Add(FrillNormal);
                G.UVs.Add(Fields->UVs[V]);
                G.DetailUVs.Add(Fields->DetailUVs[V]);
                G.UV2.Add(Fields->UV2[V]);G.UV3.Add(Fields->UV3[V]);
                G.Colors.Add(Fields->Colors[V]);
                G.FrillBend.Add(BendWeights[V]);
            }
            for (int32 I : Fragment.Indices)
                G.Fragments[0].Indices.Add(Base + I);
        }
    }

    bool DrawEntity(FAC2DecodedEntity E, AActor *Owner, USceneComponent *Root, const FVector &Offset,
                    bool RecordBounds)
    {
        TArray<uint8> B;
        for (int32 Depth = 0; (E.Data >> 24) == 0x47 && Depth < 8; ++Depth)
        {
            if (!Read(E.Data, B))
                return false;
            FAC2AssetReader T(MoveTemp(B));
            const auto Template = Entity(T);
            if (!T.End())
                return false;
            E.Scale *= Template.Scale;
            E.Data = Template.Data;
        }
        const FTransform Placement(E.Rotation, E.Origin * 100 + Offset, E.Scale);
        if ((E.Data >> 24) == 4)
            return DrawSetup(E.Data, Placement, Owner, Root, RecordBounds);
        FAC2DecodedVisual V;
        if ((E.Data >> 24) != 0x1f || !Visual(E.Data, V))
        {
            Warn(E.Data, TEXT("visual not supported by static scenery pass"));
            return false;
        }
        if (Animating)
        {
            ModelVisual=E.Data;
            ModelParticleScale=V.ParticleScale;
            StreakModelScale=E.Scale*V.Scale;
            TArray<uint8> Data;
            if(V.MotionDesc && Read(V.MotionDesc,Data))Movement.Decode(V.MotionDesc,Data);
            if(V.BehaviorTable && Read(V.BehaviorTable,Data))Behaviors.Decode(V.BehaviorTable,Data);
            V.FX.Appearance.Append(CharacterAppearance);
            FAC2Atoms Atoms;
            for (const auto &Key : V.FX.Appearance)
                CollectAtoms(Key, Atoms);
            for (const auto &A : Atoms.Parts)
            {
                auto *Existing = V.Parts.FindByPredicate([&](const auto &P) { return P.Key == A.Part.Key; });
                if (Existing)
                    *Existing = A.Part;
                else
                    V.Parts.Add(A.Part);
            }
            // FillFXTable supplies overrides; only ApplyStartupFX makes emitters
            // persistent. Spell/hit/death overrides must wait for their trigger.
            auto Startup=[&](const FAC2DecodedPart& Part){
                for(uint32 Key:Part.Startup)
                {
                    StartupStrength.Add(Key,Part.StartupStrength.FindRef(Key));
                    if(const auto* Override=Part.Overrides.Find(Key))StartupEffects.FindOrAdd(Key).Append(*Override);
                    else if(Part.FXTable)TableFX(Part.FXTable,Key,StartupEffects.FindOrAdd(Key));
                }
            };
            Startup(V.FX);for(const auto& P:V.Parts)Startup(P);
        }
        FTransform Scaled = Placement;
        // The localized sign's lateral model axis is right-handed. Convert the
        // complete sign at its root, including the back face and collision, so
        // both sides remain readable without mirroring a texture atlas.
        Scaled.SetScale3D(E.Scale * V.Scale * (V.LocalizedSign ? FVector(-1,1,1) : FVector::OneVector));
        StartFX(V, Scaled);
        bool Drawn = false;
        TMap<uint32, int32> Done;
        TMap<uint32, FTransform> Frames;
        TMap<uint32, FAC2Setup> Setups;
        for (int32 Pass = 0; Pass < V.Parts.Num(); ++Pass)
            for (const auto &P : V.Parts)
            {
                if (!P.Setup || Done.Contains(P.Key) || (P.Parent && !Done.Contains(P.Parent)))
                    continue;
                FTransform Frame = Scaled;
                if (!Animating && P.Parent)
                {
                    Frame = Frames[P.Parent];
                    const auto *S = Setups.Find(P.Parent);
                    if (const auto *Socket = S ? S->Sockets.Find(P.Connection) : nullptr)
                    {
                        const auto Pose = S->World(S->Rest);
                        if (Pose.IsValidIndex(Socket->Bone))
                            Frame =
                                FTransform(Socket->Frame.ToMatrixWithScale() * Pose[Socket->Bone]) * Frame;
                    }
                }
                if (!P.Startup.IsEmpty())
                {
                    FAC2DecodedVisual PartFX;
                    PartFX.FX = P;
                    PartFX.FX.Appearance = V.FX.Appearance;
                    PartFX.FX.Appearance.Append(P.Appearance);
                    PartFX.Parts.Add(P);
                    PartFX.Parts[0].Parent = 0; // this part's local setup is the FX attachment root
                    PartFX.ParticleScale = V.ParticleScale;
                    StartFX(PartFX, Frame);
                }
                const int32 Index = Animated.Num();
                if (Animating)
                {
                    FAC2Atoms Atoms;
                    for (const auto &Key : V.FX.Appearance)
                        CollectAtoms(Key, Atoms);
                    for (const auto &Key : P.Appearance)
                        CollectAtoms(Key, Atoms);
                    for (const auto &FX : Atoms.Effects)
                        AppearanceEffects.Add(FX.Key, FX.Value);
                    if (!AnimatedSetup(P, Scaled, Owner, Root, P.Parent ? Done[P.Parent] : -1, Atoms))
                        continue;
                    Drawn = true;
                }
                else
                {
                    FAC2Atoms Atoms;
                    for (const auto &Key : V.FX.Appearance) CollectAtoms(Key, Atoms);
                    for (const auto &Key : P.Appearance) CollectAtoms(Key, Atoms);
                    Drawn |= PrepareMovingSetup(P, Frame, Atoms) || DrawSetup(P.Setup, Frame, Owner, Root, RecordBounds, Atoms);
                    TArray<uint8> D;
                    FAC2Setup S;
                    if (Read(P.Setup, D) && S.Decode(P.Setup, D))
                        Setups.Add(P.Key, MoveTemp(S));
                    Frames.Add(P.Key, Frame);
                }
                Done.Add(P.Key, Index);
            }
        return Drawn;
    }
};

FAC2AssetLibrary::FAC2AssetLibrary() : Impl(MakeUnique<FImpl>())
{
    Impl->AnimatorState.Random.GenerateNewSeed();
}
FAC2AssetLibrary::~FAC2AssetLibrary()
{
    if(Impl->ModelRoot.IsValid())if(auto* Audio=FAC2Audio::Get(Impl->ModelRoot->GetWorld()))
        for(const auto& FX:Impl->ScalarEffects)for(uint32 Context:FX.Value.Contexts)Audio->Stop(Context);
    for(auto& E:Impl->VisualEffects){E.Model.Reset();if(E.Actor.IsValid())E.Actor->Destroy();}
    for(auto& E:Impl->LightEffects)if(E.Light.IsValid())E.Light->DestroyComponent();
    for (auto& Streak : Impl->Streaks) Streak.Release();
}
bool FAC2AssetLibrary::ValidateGrassMaterials(int32 &Count)
{
    Count = 0;
    TSet<uint32> Seen;
    for (const auto &Draw : Impl->Draws)
        if (Draw.Frill)
            for (const auto &Fragment : Draw.Mesh->Fragments)
                Seen.Add(Fragment.Material);
    for (uint32 Id : Seen)
    {
        auto *Original = Impl->RuntimeMaterial(Id);
        auto *Grass = Impl->RuntimeMaterial(Id, true);
        UE_LOG(LogTemp, Display, TEXT("AC2 grass %08X: original blend=%d grass blend=%d"), Id,
               Original ? int32(Original->GetBlendMode()) : -1, Grass ? int32(Grass->GetBlendMode()) : -1);
        if (!Grass || Grass->GetBlendMode() != BLEND_Masked)
            return false;
        ++Count;
    }
    return Count > 0;
}
bool FAC2AssetLibrary::AnimatedHoldingLocation(uint32 Location, FTransform &Out) const
{
    for(const auto& G:Impl->Animated)
        if(const auto* Socket=G.Setup.Sockets.Find(Location))
            if(G.Bones.IsValidIndex(Socket->Bone))
            {
                // Keep the full part matrix until the socket is in model space.
                // Decomposing a scaled parent first loses the translation of
                // rotated attachments on non-uniformly scaled body parts.
                Out=FTransform(Socket->Frame.ToMatrixWithScale()*G.Bones[Socket->Bone]*G.PartFrame);
                return true;
            }
    return false;
}
bool FAC2AssetLibrary::HoldingLocation(uint32 Object, uint32 Location, FTransform &Out)
{
    FAC2DecodedVisual V;
    if (!Impl->Visual(Object, V)) return false;
    for (const auto &P : V.Parts)
    {
        TArray<uint8> B;
        FAC2Setup S;
        if (P.Parent || !Impl->Read(P.Setup, B) || !S.Decode(P.Setup, B)) continue;
        const auto *Socket = S.Sockets.Find(Location);
        auto World = S.World(S.Rest);
        if (Socket && World.IsValidIndex(Socket->Bone))
        {
            Out = FTransform(Socket->Frame.ToMatrixWithScale() * World[Socket->Bone]) *
                  FTransform(FQuat::Identity, FVector::ZeroVector, V.Scale);
            return true;
        }
    }
    return false;
}
bool FAC2AssetLibrary::DrawUIScene(uint32 Id, AActor *Owner, USceneComponent *Root, FAC2UISceneInfo *Info)
{
    TArray<uint8> B;
    if (!Impl->Read(Id, B))
        return false;
    FAC2AssetReader R(MoveTemp(B));
    if (R.Word() != Id)
        return false;
    TArray<FAC2DecodedEntity> Entities;
    int32 Count = R.Count(4, 4096);
    for (int32 I = 0; I < Count && R.Good; ++I)
        Entities.Add(Impl->Entity(R));
    R.Take(16); // fog colour, enabled, near and far
    if (!R.End())
        return false;
    FAC2UISceneInfo Scene;
    for (const auto& E : Entities)
    {
        auto Word = [&](uint32 Key, uint32 Default) { const auto* P = E.Properties.Find(Key); return P && P->Num() >= 4 ? AC2::U32(P->GetData()) : Default; };
        auto Float = [&](uint32 Key, float Default, int32 Offset = 0) { const auto* P = E.Properties.Find(Key); return P && P->Num() >= Offset + 4 ? AC2::F32(P->GetData() + Offset) : Default; };
        if (E.Type == 0x40000005)
        {
            Scene.Camera = FTransform(E.Rotation, E.Origin * 100);
            Scene.VerticalFOV = Float(0x4000001f, 65);
            Scene.Orthographic = Word(0x4000001e,0)!=0;
            Scene.OrthoHeight = Float(0x40000020,1)*100;
        }
        if (E.Type != 0x40000006) continue;
        FAC2PreviewLight L;
        L.Name = E.Name; L.Position = E.Origin * 100;
        L.Directional = Word(0x40000002, 0x40000001) == 0x40000002;
        L.Direction = FVector(Float(0x40000003, 0), Float(0x40000003, 0, 4), Float(0x40000003, 0, 8)).GetSafeNormal();
        L.Range = Float(0x40000004, 5, 4) * 100;
        L.Intensity = Float(0x40000009, .5f, 4);
        const FColor Color(Word(0x40000008, 0xffffffff));
        L.Diffuse = Color.ReinterpretAsLinear(); // Native values are not sRGB-decoded.
        Scene.Lighting.Lights.Add(L);
    }
    Scene.Lighting.UseDefault();
    if (Info) *Info = Scene;
    if (Impl->PreviewMode) SetPreviewLighting(Scene.Lighting, Owner->GetActorTransform());
    bool Drawn = false;
    for (const auto &E : Entities)
    {
        if (Info && E.Type == 0x40000005)
            Info->Camera = FTransform(E.Rotation, E.Origin * 100);
        if (E.Type == 0x40000001 || E.Type == 0x40000007)
        {
            Drawn |= Impl->DrawEntity(E, Owner, Root, FVector::ZeroVector, false);
            // First physics object is CHARGEN_LANDMESH; its holding locations place the selectable avatars.
            if (Info && Info->Locations.IsEmpty())
            {
                uint32 Object = E.Data;
                for (int32 Depth = 0; (Object >> 24) == 0x47 && Depth < 8; ++Depth)
                {
                    if (!Impl->Read(Object, B))
                        break;
                    FAC2AssetReader ER(MoveTemp(B));
                    Object = Impl->Entity(ER).Data;
                }
                FAC2DecodedVisual V;
                if (Impl->Visual(Object, V))
                    for (const auto &P : V.Parts)
                    {
                        FAC2Setup S;
                        if (P.Parent || !Impl->Read(P.Setup, B) || !S.Decode(P.Setup, B))
                            continue;
                        auto World = S.World(S.Rest);
                        for (const auto &Socket : S.Sockets)
                            if (World.IsValidIndex(Socket.Value.Bone))
                                Info->Locations.Add(
                                    Socket.Key,
                                    FTransform(Socket.Value.Frame.ToMatrixWithScale() *
                                               World[Socket.Value.Bone]) *
                                        FTransform(E.Rotation, E.Origin * 100, E.Scale * V.Scale));
                    }
            }
        }
    }
    return Drawn;
}
void FAC2AssetLibrary::SetPreviewLighting(const FAC2PreviewLighting& Lighting, const FTransform& SceneToWorld)
{
    Impl->PreviewMode = true;
    Impl->PreviewLighting = Lighting;
    Impl->PreviewToWorld = SceneToWorld;
    for (const auto& Pair : Impl->RuntimeMaterials)
        if (auto* MID = Cast<UMaterialInstanceDynamic>(Pair.Value)) Impl->LightPreview(MID);
}
uint32 FAC2AssetLibrary::AppearanceFingerprint() const
{
    return Impl->AppearanceHash;
}
FBox FAC2AssetLibrary::ModelBounds(uint32 MeshType) const
{
    FBox Bounds(ForceInit);
    for (const auto& Group : Impl->Animated)
        for (const auto& Mesh : Group.Meshes)
        {
            if (MeshType && Mesh.Data->Type != MeshType) continue;
            for (int32 S = 0; S < Mesh.Component->GetNumSections(); ++S)
            {
                const auto* Section = Mesh.Component->GetProcMeshSection(S);
                if (!Section || !Section->bSectionVisible) continue;
                const FTransform Frame = Mesh.Component->GetRelativeTransform();
                for (int32 I = 0; I + 2 < Section->ProcIndexBuffer.Num(); I += 3)
                {
                    FVector P[3];
                    for (int32 J = 0; J < 3; ++J)
                        P[J] = Frame.TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[I + J]].Position);
                    if (FVector::CrossProduct(P[1] - P[0], P[2] - P[0]).SizeSquared() > SMALL_NUMBER)
                        for (const auto& V : P) Bounds += V;
                }
            }
        }
    return Bounds;
}
bool FAC2AssetLibrary::HitModel(const FVector& Start, const FVector& End, double& Fraction) const
{
    Fraction = 1;
    const double LengthSquared = FVector::DistSquared(Start, End);
    if (LengthSquared <= SMALL_NUMBER) return false;
    bool Hit = false;
    for (const auto& Group : Impl->Animated)
        for (const auto& Mesh : Group.Meshes)
        {
            if (!Mesh.Component->IsVisible()) continue;
            const FTransform Frame = Mesh.Component->GetComponentTransform();
            const FVector LocalStart = Frame.InverseTransformPosition(Start);
            const FVector LocalEnd = Frame.InverseTransformPosition(End);
            for (int32 S = 0; S < Mesh.Component->GetNumSections(); ++S)
            {
                const auto* Section = Mesh.Component->GetProcMeshSection(S);
                if (!Section || !Section->bSectionVisible ||
                    !FMath::LineBoxIntersection(Section->SectionLocalBox, LocalStart, LocalEnd, LocalEnd-LocalStart)) continue;
                for (int32 I = 0; I + 2 < Section->ProcIndexBuffer.Num(); I += 3)
                {
                    FVector Point, Normal;
                    if (FMath::SegmentTriangleIntersection(LocalStart, LocalEnd,
                        Section->ProcVertexBuffer[Section->ProcIndexBuffer[I]].Position,
                        Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+1]].Position,
                        Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+2]].Position, Point, Normal))
                    {
                        const double T = FVector::DotProduct(Frame.TransformPosition(Point)-Start, End-Start) / LengthSquared;
                        if (T >= 0 && T < Fraction) { Fraction = T; Hit = true; }
                    }
                }
            }
        }
    return Hit;
}
const FAC2MotionData& FAC2AssetLibrary::MotionData() const { return Impl->Movement; }
bool FAC2AssetLibrary::SetMotionDescriptor(uint32 Id)
{
    TArray<uint8> Data;FAC2MotionData Motion;
    if((Id>>24)!=0x32||!Impl->Read(Id,Data)||!Motion.Decode(Id,Data))return false;
    Impl->Movement=MoveTemp(Motion);return true;
}
void FAC2AssetLibrary::EnableCreatureIdleAudio()
{
    Impl->CreatureIdleAudio=true;
}
void FAC2AssetLibrary::SetAnimationState(uint32 Element,uint32 Mode)
{
    if(Impl->Mode==AC2Motion::Dead && Mode!=AC2Motion::Dead)StopBehavior();
    // Ground actions retain their original element/mode while playing. They
    // must not continue overriding the water locomotion after a mode change.
    if (Element==AC2Motion::Water && Impl->Element!=Element && Mode!=AC2Motion::Dead)
        StopBehavior();
    if(Impl->Element!=Element || Impl->Mode!=Mode)
    {
        Impl->BlendFromCurrent(Impl->Behaviors.ModeRamp(Impl->Element,Impl->Mode,Element,Mode));
        // Preserve the gait phase across ground/air/stance changes. Resetting it
        // on every contact change repeatedly snaps a running body's legs back.
        for(auto& G:Impl->Animated){G.IdleTriggers={};G.MoveTriggers={};}
    }
    Impl->Element=Element;Impl->Mode=Mode;
}
bool FAC2AssetLibrary::PlayBehavior(uint32 Behavior,uint32 Mode,float Rate,bool Hold)
{
    if(Impl->Actions.ContainsByPredicate([](const auto& A){return A.Behavior==AC2Motion::Death;}))
        return Behavior==AC2Motion::Death; // Duplicate death/hit packets cannot restart a corpse.
    if(Mode)SetAnimationState(Impl->Element,Mode);
    uint32 Element=Impl->Element,BehaviorMode=Impl->Mode;
    uint32 Key=Impl->Behaviors.Key(Element,BehaviorMode,Behavior);
    if(!Key){Element=AC2Motion::Ground;Key=Impl->Behaviors.Key(Element,BehaviorMode,Behavior);}
    if(!Key && (Behavior==AC2Motion::Jump||Behavior==AC2Motion::Land||Behavior==AC2Motion::Fall))
        {Element=AC2Motion::Air;Key=Impl->Behaviors.Key(Element,BehaviorMode,Behavior);}
    if(!Key){Element=AC2Motion::Ground;BehaviorMode=AC2Motion::Peace;Key=Impl->Behaviors.Key(Element,BehaviorMode,Behavior);}
    if(!Key)return false;
    float Duration=0;int32 DriverBones=0;
    const bool NativeHold=Impl->IsHoldBehavior(Behavior) && Behavior!=AC2Motion::Death;
    FAC2HoldLoop Loop;
    for(auto& G:Impl->Animated)
        if(G.Parent<0 && G.Setup.Parents.Num()>DriverBones)if(auto Graph=Impl->Graph(G,Key))
        {auto Input=G.Motion;Input.Mode=BehaviorMode;Input.Element=Element;
            const float Period=Graph->Period(Input);
            if(Period>.001f)
            {
                Duration=Period;DriverBones=G.Setup.Parents.Num();
                if(NativeHold)Loop=Graph->HoldLoop(Input);
            }}
    if(Duration<=.001f)return false;
    if(Behavior==AC2Motion::Death){Impl->Actions.Reset();Hold=true;}
    const uint32 Layer=Impl->Behaviors.Layer(Element,BehaviorMode,Behavior);
    const float Ramp=Impl->Behaviors.Ramp(Behavior);
    if(Impl->Actions.ContainsByPredicate([&](const auto& A){return A.Layer==Layer;}))Impl->BlendFromCurrent(Ramp);
    Impl->Actions.RemoveAll([&](const auto& A){return A.Layer==Layer;});
    Impl->ReleaseEndedStreakControllers();
    // Also unique across model rebuilds (equipment/appearance changes).
    static std::atomic<uint64> NextActionInstance{0};
    uint64 Instance=0;while(!Instance)Instance=NextActionInstance.fetch_add(1,std::memory_order_relaxed)+1;
    Impl->Actions.Add({Behavior,Key,Layer,BehaviorMode,Element,0,0,Duration,FMath::Clamp(Rate,.05f,10.f),Ramp,Hold||Loop.Active(),Instance,Loop});
    Impl->Actions.StableSort([&](const auto& A,const auto& B){
        // ComboInfo stores a layer INDEX, not the enum in BehaviorTable::mLayers.
        // AnimCompositor::Animate gives the highest index first claim on each bone.
        return A.Layer<B.Layer;
    });
    UE_LOG(LogTemp,Verbose,TEXT("AC2 behavior %08X mode=%08X key=%08X duration=%g"),Behavior,Impl->Mode,Key,Duration);
    return true;
}
void FAC2AssetLibrary::StopBehavior(uint32 Behavior)
{
    if(Impl->Actions.ContainsByPredicate([&](const auto& A){return !Behavior || A.Behavior==Behavior;}))
        Impl->BlendFromCurrent(Behavior?Impl->Behaviors.Ramp(Behavior):.25f);
    Impl->Actions.RemoveAll([&](const auto& A){return !Behavior || A.Behavior==Behavior;});
    Impl->ReleaseEndedStreakControllers();
}
void FAC2AssetLibrary::ReleaseBehavior(uint32 Behavior)
{
    // ModeTracker::ReleaseBehavior preserves the current track and its phase.
    // Releasing an absent/non-held action must not restart it or cancel another
    // layer. Death is a persistent presentation state, not a releasable hold.
    if(Behavior==AC2Motion::Death)return;
    if(auto* Track=Impl->Actions.FindByPredicate([&](const auto& A){return A.Behavior==Behavior && A.Hold;}))
    {
        if(Track->Loop.Active())
        {
            Track->Time=Track->Previous=Track->Loop.PhaseAt(Track->Time/Track->Duration)*Track->Duration;
            Track->Loop={};
        }
        else Impl->BlendFromCurrent(Track->Ramp);
        Track->Hold=false;
    }
}
void FAC2AssetLibrary::StopAnimationLayer(uint32 Layer)
{
    // BehaviorTracker::StopLayer receives a layer index, as stored by ComboInfo.
    // Stop only that layer; a damage reaction must not cancel an independent cast.
    TArray<uint32> Matches;
    for(const auto& A:Impl->Actions)if(A.Layer==Layer && A.Behavior!=AC2Motion::Death)Matches.Add(A.Behavior);
    for(uint32 Behavior:Matches)StopBehavior(Behavior);
}
float FAC2AssetLibrary::BehaviorDuration(uint32 Behavior) const
{
    const auto* Track=Impl->Actions.FindByPredicate([&](const auto& A){return A.Behavior==Behavior;});
    return Track?Track->Duration/Track->Rate:0;
}
bool FAC2AssetLibrary::CompletedHoldCycles(uint32 Behavior,uint32 Cycles) const
{
    const auto* Track=Impl->Actions.FindByPredicate([&](const auto& A){return A.Behavior==Behavior;});
    return Track && Track->Loop.Active() && Track->Loop.Repeats(Track->Time/Track->Duration)>=Cycles;
}
void FAC2AssetLibrary::SeekBehavior(uint32 Behavior,float Elapsed)
{
    if(!FMath::IsFinite(Elapsed))return;
    if(auto* Track=Impl->Actions.FindByPredicate([&](const auto& A){return A.Behavior==Behavior;}))
    {
        const double Time=FMath::Max(0.,double(Elapsed)*Track->Rate);
        Track->Previous=Track->Time=Track->Loop.Active()?Time:FMath::Min(Time,double(Track->Duration));
        // Seeking restores a presentation snapshot; blending from the fresh
        // model's standing pose would briefly resurrect a rebuilt corpse.
        for(auto& G:Impl->Animated){G.TransitionPose.Reset();G.TransitionTime=G.TransitionDuration=0;}
    }
}
uint64 FAC2AssetLibrary::BehaviorInstance(uint32 Behavior) const
{
    const auto* Track=Impl->Actions.FindByPredicate([&](const auto& A){return A.Behavior==Behavior;});
    return Track?Track->Instance:0;
}
bool FAC2AssetLibrary::HandlingItem() const
{
    return BehaviorDuration(AC2Motion::Pickup)>0 || BehaviorDuration(AC2Motion::Putdown)>0;
}
bool FAC2AssetLibrary::ReleasedHeldItem(uint32 Holding) const
{
    if(Holding!=4)return false;
    // Legions StoryManager::CreateAndParentMissiles (00502DE0) fades the
    // equipped thrown weapon while its missile clone is in use; CleanupStory
    // (00502BA0) restores it. Our network missile is a separate actor. Keep the
    // held visual until release, then hide it through recovery. Restrict this
    // bridge to the verified Tumerok clips, not bows sharing holding point 4.
    for(const auto& Action:Impl->Actions)if(Action.Mode==0x41000010)
        for(auto& G:Impl->Animated)if(G.Parent<0)if(auto Graph=Impl->Graph(G,Action.Key))
        {
            auto Input=G.Motion;Input.Mode=Action.Mode;Input.Element=Action.Element;
            for(uint32 Clip:Graph->ActiveClips(Input))
            {
                const float Release=Clip==0x05000705?.472692996f:Clip==0x05000707?.629001975f:2.f;
                if(Action.Time/Action.Duration>=Release)return true;
            }
        }
    return false;
}
float FAC2AssetLibrary::LocomotionScale() const
{
    const FImpl::FAnimatedGroup* Driver=nullptr;
    for(const auto& G:Impl->Animated)if(G.Parent<0 && (!Driver || G.Setup.Parents.Num()>Driver->Setup.Parents.Num()))Driver=&G;
    return Driver?FMath::Max(.01f,float(FMath::Abs(Driver->Frame.GetScale3D().Y))):1.f;
}
TArray<float> FAC2AssetLibrary::StoryHookDelays(uint32 Behavior) const
{
    const auto* A=Impl->Actions.FindByPredicate([&](const auto& V){return V.Behavior==Behavior;});
    if(A)for(auto& G:Impl->Animated)if(G.Parent<0)if(auto Graph=Impl->Graph(G,A->Key))
    {
        auto Input=G.Motion;Input.Mode=A->Mode;Input.Element=A->Element;
        auto Times=Graph->StoryTriggers(Input);
        for(float& T:Times)T=FMath::Max(0.f,(T*A->Duration-A->Time)/A->Rate);
        return Times;
    }
    return {};
}
float FAC2AssetLibrary::JumpDelay() const
{
    const auto* A=Impl->Actions.FindByPredicate([](const auto& V){return V.Behavior==AC2Motion::Jump;});
    if(A)for(auto& G:Impl->Animated)if(G.Parent<0)if(auto Graph=Impl->Graph(G,A->Key))
    {auto Input=G.Motion;Input.Mode=A->Mode;Input.Element=A->Element;return Graph->JumpDelay(Input)/A->Rate;}
    return 0;
}
bool FAC2AssetLibrary::AuditAttachments(FString& Error) const
{
    for(const auto& G:Impl->Animated)
        if(Impl->Animated.IsValidIndex(G.Parent))
        {
            const auto& P=Impl->Animated[G.Parent];
            if(!P.Bones.IsValidIndex(G.Socket.Bone)){Error=TEXT("Missing attachment bone");return false;}
            const auto Expected=G.Socket.Frame.ToMatrixWithScale()*P.Bones[G.Socket.Bone]*P.PartFrame;
            if(!G.PartFrame.Equals(Expected,.001)){Error=TEXT("Child detached from animated connection point");return false;}
        }
    return !Impl->Animated.IsEmpty();
}
bool FAC2AssetLibrary::AttachmentSeamGap(uint32 MeshType, int32& MatchedVertices, double& Gap) const
{
    MatchedVertices=0;Gap=0;
    auto RigidlyWeighted=[](const FAC2DecodedGeometry& D,int32 V,int32 Bone)
    {
        if(!D.Skin.IsValidIndex(V))return false;
        float Weight=0;
        for(int32 I=0;I<4;++I)if(D.Skin[V].Bones[I]==Bone)Weight+=D.Skin[V].Weights[I];
        return Weight>.9999f;
    };
    for(const auto& G:Impl->Animated)if(Impl->Animated.IsValidIndex(G.Parent))
    {
        const auto& Parent=Impl->Animated[G.Parent];
        const auto Bind=Parent.Setup.World(Parent.Setup.Bind,false);
        if(!Bind.IsValidIndex(G.Socket.Bone))continue;
        const FMatrix Attachment=G.Socket.Frame.ToMatrixWithScale()*Bind[G.Socket.Bone];
        for(const auto& Child:G.Meshes)if(Child.Data->Type==MeshType && Child.Component)
        {
            const auto* ChildSection=Child.Component->GetProcMeshSection(0);
            if(!ChildSection)continue;
            TArray<FVector> Checked;
            for(int32 C=0;C<Child.Data->Vertices.Num();++C)
            {
                if(!RigidlyWeighted(*Child.Data,C,0))continue;
                const FVector Source=FVector(Attachment.TransformPosition(Child.Data->Vertices[C]));
                if(Checked.ContainsByPredicate([&](const FVector& V){return V.Equals(Source,.01);}))continue;
                for(const auto& Body:Parent.Meshes)if(Body.Component)
                {
                    const auto* BodySection=Body.Component->GetProcMeshSection(0);
                    if(!BodySection)continue;
                    const int32 B=Body.Data->Vertices.IndexOfByPredicate([&](const FVector& V){return V.Equals(Source,.01);});
                    if(B==INDEX_NONE || !RigidlyWeighted(*Body.Data,B,G.Socket.Bone))continue;
                    const FVector A=Child.Component->GetComponentTransform().TransformPosition(ChildSection->ProcVertexBuffer[C].Position);
                    const FVector P=Body.Component->GetComponentTransform().TransformPosition(BodySection->ProcVertexBuffer[B].Position);
                    Gap=FMath::Max(Gap,FVector::Distance(A,P));++MatchedVertices;Checked.Add(Source);break;
                }
            }
        }
    }
    return MatchedVertices>0;
}
TArray<FAC2IconLayer> FAC2AssetLibrary::IconLayers(uint32 Id,const FAC2Appearance& Appearance)
{
    FAC2DecodedVisual V;
    if(!Impl->Visual(Id,V))return {};
    FAC2Atoms Atoms;
    for(const auto& Key:V.FX.Appearance)Impl->CollectAtoms(Key,Atoms);
    for(const auto& Key:Appearance)Impl->CollectAtoms(Key,Atoms);
    for(const auto& Icon:Atoms.Icons)
    {
        V.Icons.RemoveAll([&](const auto& Old){return Old.Layer==Icon.Layer;});
        V.Icons.Add(Icon);
    }
    // A null image clears an inherited layer; it is not a missing texture.
    V.Icons.RemoveAll([](const auto& Icon){return !Icon.Image;});
    V.Icons.Sort([](const auto& A,const auto& B){return A.Layer<B.Layer;});
    return V.Icons;
}
bool FAC2AssetLibrary::HasLocomotion() const
{
    return Impl->Animated.ContainsByPredicate([](const auto& G){return G.Parent<0 && G.Locomotion.IsValid();});
}
int32 FAC2AssetLibrary::EnableWorldObjectCollision()
{
    if (!Impl->ModelRoot.IsValid()) return 0;
    // MotionData is inherited by animated props too (including lifestones).
    // Only a model with an actual locomotion graph is a moving creature.
    if (HasLocomotion()) return 0;
    int32 Count=0;
    TInlineComponentArray<UProceduralMeshComponent*> Meshes(Impl->ModelRoot->GetOwner());
    for(auto* Mesh:Meshes)
    {
        bool Solid=false;
        for(int32 S=0;S<Mesh->GetNumSections();++S)
            if(auto* Section=Mesh->GetProcMeshSection(S))
            {
                const auto* Mat=Mesh->GetMaterial(S);
                Section->bEnableCollision=Section->bSectionVisible && Mat && Mat->GetBlendMode()==BLEND_Opaque;
                Solid|=Section->bEnableCollision;
            }
        if(!Solid)continue;
        Mesh->bUseAsyncCooking=false;
        Mesh->bUseComplexAsSimpleCollision=true;
        // SetProcMeshSection rebuilds the cooked collision after the flags change.
        const FProcMeshSection Section=*Mesh->GetProcMeshSection(0);
        Mesh->SetProcMeshSection(0,Section);
        Mesh->SetCollisionObjectType(ECC_WorldDynamic);
        Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
        Mesh->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
        Mesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        ++Count;
    }
    return Count;
}
bool FAC2AssetLibrary::DrawModel(uint32 Id, AActor *Owner, USceneComponent *Root,
                                 const FAC2Appearance &Appearance)
{
    Impl->Animating = true;
    Impl->ModelVisual=Id;
    Impl->ModelRoot = Root;
    Impl->CharacterAppearance = Appearance;
    Impl->AppearanceHash = 0;
    FAC2DecodedEntity E;
    E.Data = Id;
    bool OK = Impl->DrawEntity(E, Owner, Root, FVector::ZeroVector, false);
    Impl->RefreshAppearanceFX();
    TArray<uint32> Keys;
    Impl->StartupEffects.GetKeys(Keys);
    Keys.Sort();
    for (uint32 Key : Keys)
        for (const auto &Node : Impl->AppearanceEffects.Contains(Key)?Impl->AppearanceEffects[Key]:Impl->StartupEffects[Key])
        {
            const auto* Game=Owner->GetWorld()->GetAuthGameMode<AAC2GameMode>();
            const uint32 Terrain=Game?Game->SoundTerrain(Root->GetComponentLocation()):0;
            const auto* Variant=Node.Terrain.Find(Terrain);
            Impl->StartSound(Variant?**Variant:Node,Key,Impl->StartupStrength.FindRef(Key),true);
            Impl->StartLight(Node,Key);
            Impl->StartRepulsor(Variant ? **Variant : Node);
            Impl->ApplyStreak(Variant ? **Variant : Node, Key);
            // Root emitters must also follow models without an animated group.
            if(Impl->Animated.IsEmpty() && !Node.Bone && !Node.Holding &&
                !Node.Descriptors.IsEmpty() && Node.Descriptors[0]>>24==0x39)
            {
                const uint32 Descriptor=Node.Descriptors[0];
                Impl->AppearanceAttachments.Add({INDEX_NONE,Node,uint64(Key)<<32|uint32(Impl->AppearanceAttachments.Num()+1),Descriptor});
                Impl->AppearanceHash=HashCombineFast(Impl->AppearanceHash,Descriptor);
            }
            for (int32 I = 0; I < Impl->Animated.Num(); ++I)
            {
                const auto &G = Impl->Animated[I];
                if ((Node.Holding && !G.Setup.Sockets.Contains(Node.Holding)) ||
                    (Node.Bone && !G.Setup.Names.Contains(Node.Bone)) ||
                    (!Node.Holding && !Node.Bone && G.Parent >= 0))
                    continue;
                if (!Node.Descriptors.IsEmpty() && Node.Descriptors[0] >> 24 == 0x39)
                {
                    uint32 Descriptor = Node.Descriptors[0];
                    Impl->AppearanceAttachments.Add(
                        {I, Node, uint64(Key) << 32 | uint32(Impl->AppearanceAttachments.Num() + 1),
                         Descriptor});
                    Impl->AppearanceHash = HashCombineFast(Impl->AppearanceHash, Descriptor);
                }
                break;
            }
        }
    if (!Impl->AppearanceAttachments.IsEmpty())
    {
        Impl->AttachedEffects = MakeUnique<FAC2Effects>();
        Impl->AttachedEffects->Open(Owner->GetWorld(), Impl->Directory, !Impl->PreviewMode);
        if (auto *Capture = Owner->FindComponentByClass<USceneCaptureComponent2D>())
            Capture->ShowOnlyActors.Add(Impl->AttachedEffects->RenderActor());
    }
    Animate(0, 0);
    return OK;
}
void FAC2AssetLibrary::SetAnimationEnvironment(bool Grounded,const FVector& GroundNormal,const FVector& Look)
{
    Impl->AnimationGrounded=Grounded;
    Impl->AnimationNormal=GroundNormal.ContainsNaN()?FVector::UpVector:GroundNormal.GetSafeNormal();
    Impl->AnimationLook=Look.ContainsNaN()?FVector::ZeroVector:Look;
}
void FAC2AssetLibrary::Animate(float Time, float Speed,
                               const FVector *LocalVelocity)
{
    if(Impl->SeenEffectChannels!=EffectChannelRevision)
    {
        Impl->SeenEffectChannels=EffectChannelRevision;
        const auto Effects=Impl->AppearanceFXStrengths;
        for(const auto& Effect:Effects)PlayEffect(Effect.Key,Effect.Value);
    }
    TArray<FAC2Animation::FEffect> AnimationEffects,CreatureIdleEffects;
    const float Delta = FMath::Clamp(Time - Impl->PreviousAnimationTime, 0.f, .2f);
    Impl->PreviousAnimationTime = Time;
    for(auto& Action:Impl->Actions){Action.Previous=Action.Time;Action.Time+=Delta*Action.Rate;}
    for (auto &G : Impl->Animated)
    {
        auto Pose = G.Idle ? G.Idle->Sample(Time, G.Setup.Rest) : G.Setup.Rest;
        G.Motion.Element=Impl->Element;G.Motion.Mode=Impl->Mode;G.Motion.Terrain=0x80000001;
        G.Motion.Time=Time;G.Motion.State=&Impl->AnimatorState;G.Motion.BoneNames=G.Setup.Names;
        G.Motion.Grounded=Impl->AnimationGrounded;G.Motion.GroundNormal=Impl->AnimationNormal;G.Motion.Look=Impl->AnimationLook;
        const uint32 IdleKey=Impl->Behaviors.Key(Impl->Element,Impl->Mode,0x40000002);
        auto Idle=Impl->Graph(G,IdleKey);
        auto IdleInput=G.Motion;
        float IdlePeriod=Idle?Idle->Period(IdleInput):0;
        // Some state/map combinations intentionally have no idle. Keep the
        // native ground idle underneath instead of exposing the bind/rest pose.
        if(!Idle || (IdlePeriod<=.001f && Idle->ActiveClips(IdleInput).IsEmpty()))
        {
            Idle=Impl->Graph(G,0x80000012);
            IdleInput.Element=AC2Motion::Ground;IdleInput.Mode=AC2Motion::Peace;
            IdlePeriod=Idle?Idle->Period(IdleInput):0;
        }
        if(Idle)
        {
            if(IdlePeriod>.001f)
            {
                const double Previous=G.IdlePhase;
                G.IdlePhase=Impl->Animated.IsValidIndex(G.Parent)?Impl->Animated[G.Parent].IdlePhase:G.IdlePhase+Delta/IdlePeriod;
                if(G.Parent<0 && G.MoveWeight<1 && Impl->Mode!=AC2Motion::Dead)
                {
                    auto& Effects=Impl->CreatureIdleAudio?CreatureIdleEffects:AnimationEffects;
                    Effects.Append(Idle->EffectsBetween(Previous,G.IdlePhase,IdleInput,true,&G.IdleTriggers));
                }
            }
            // Zero-period assets are valid static poses, not missing animation.
            Pose=Idle->Sample(float(FMath::Frac(G.IdlePhase)),IdleInput,G.Setup.Rest);
        }
        uint32 MoveKey=Impl->Behaviors.Key(Impl->Element,Impl->Mode,AC2Motion::Move);
        auto MovingGraph=MoveKey ? Impl->Graph(G,MoveKey) : G.Locomotion;
        if (MovingGraph)
        {
            const FVector Scale = G.Frame.GetScale3D().GetAbs().ComponentMax(FVector(.01));
            const FVector Velocity = (LocalVelocity ? *LocalVelocity : FVector(0, Speed, 0)) / (100 * Scale);
            // AMHuman supplies velocity sliders to AnimCube. Smooth actual local
            // velocity, including reversing/sidestepping, rather than requested input.
            G.Motion.Forward = FMath::FInterpTo(G.Motion.Forward, float(Velocity.Y), Delta, 10);
            G.Motion.Side = FMath::FInterpTo(G.Motion.Side, float(Velocity.X), Delta, 10);
            G.Motion.Vertical = FMath::Max(-10.f, float(Velocity.Z));
            const bool WantsMove=Speed>10 || Impl->Element==AC2Motion::Air ||
                (Impl->Element==AC2Motion::Water && FMath::Abs(Velocity.Z)>.1);
            const float MoveRamp=Impl->Behaviors.Ramp(WantsMove?AC2Motion::Move:0x40000002);
            G.MoveWeight = FMath::FInterpConstantTo(G.MoveWeight, WantsMove ? 1.f : 0.f, Delta, MoveRamp>0?1.f/MoveRamp:10000.f);
            const float Period = MovingGraph->Period(G.Motion);
            if (Period > .001f && G.MoveWeight > 0)
            {
                const double Previous=G.Phase;
                G.Phase = Impl->Animated.IsValidIndex(G.Parent) ? Impl->Animated[G.Parent].Phase : G.Phase + Delta / Period;
                if(G.Parent<0 && WantsMove && Impl->Mode!=AC2Motion::Dead)
                    AnimationEffects.Append(MovingGraph->EffectsBetween(Previous,G.Phase,G.Motion,true,&G.MoveTriggers));
                const auto Moving = MovingGraph->Sample(float(FMath::Frac(G.Phase)), G.Motion, G.Setup.Rest);
                bool Changed = false;
                for (int32 I = 0; I < Pose.Num(); ++I)
                {
                    if (G.PreviousMotionPose.IsValidIndex(I) &&
                        !Moving[I].Equals(G.PreviousMotionPose[I], .001f))
                        Changed = true;
                    FTransform Blended;
                    Blended.Blend(Pose[I], Moving[I], G.MoveWeight);
                    Pose[I] = Blended;
                }
                if (Changed && Speed > 20 && G.MoveWeight > .9f)
                    ++Impl->MovingPoseUpdates;
                G.PreviousMotionPose = Moving;
            }
        }
        for(const auto& Action:Impl->Actions)
            if(auto ActionGraph=Impl->Graph(G,Action.Key))
            {
                auto Input=G.Motion;Input.Mode=Action.Mode;Input.Element=Action.Element;
                const float Period=ActionGraph->Period(Input);
                if(Period>.001f)
                {
                    // CPartGroup shares the BehaviorTrack phase with every part.
                    // Timing a head/weapon by its own clip period separates it from the body.
                    const double From=Action.Previous/Action.Duration, To=Action.Time/Action.Duration;
                    const double Phase=Action.Loop.PhaseAt(To);
                    if(G.Parent<0)
                    {
                        // Split at the authored range edges. Each repetition can replay
                        // its own cues, while the windup and release cues occur just once.
                        const double PreviousPhase=Action.Loop.PhaseAt(From);
                        const int32 FirstEvent = AnimationEffects.Num();
                        const double Wraps=Action.Loop.Repeats(To)-Action.Loop.Repeats(From);
                        if(Wraps>0)
                        {
                            AnimationEffects.Append(ActionGraph->EffectsBetween(PreviousPhase,Action.Loop.End,Input));
                            for(int32 I=1;I<FMath::Min(16.,Wraps);++I)
                                AnimationEffects.Append(ActionGraph->EffectsBetween(Action.Loop.Start,Action.Loop.End,Input));
                        }
                        AnimationEffects.Append(ActionGraph->EffectsBetween(Wraps>0?Action.Loop.Start:PreviousPhase,FMath::Min(Phase,1.),Input));
                        for (int32 I=FirstEvent; I<AnimationEffects.Num(); ++I) AnimationEffects[I].SourceAction=Action.Instance;
                    }
                    auto ActionPose=ActionGraph->Sample(float(FMath::Min(Phase,.99999)),Input,Pose);
                    const float In=Action.Ramp>0?float(FMath::Min(1.,Action.Time/Action.Ramp)):1.f;
                    const float Out=Action.Hold||Action.Ramp<=0?1.f:float(FMath::Clamp((Action.Duration-Action.Time)/Action.Ramp,0.,1.));
                    const float Weight=FMath::Min(In,Out);
                    for(int32 I=0;I<Pose.Num();++I){FTransform Blend;Blend.Blend(Pose[I],ActionPose[I],Weight);Pose[I]=Blend;}
                }
            }
        // Retail starts these tracks from BehaviorTable::mInitBehaviors.
        // Look and contact-angle tracks have no period: apply them additively
        // over locomotion/actions instead of rejecting them as empty clips.
        if(Impl->Mode!=AC2Motion::Dead)for(uint32 Behavior:Impl->Behaviors.InitialBehaviors)
            if(Behavior==0x40000003 || Behavior==0x40000010)
                if(auto Tweak=Impl->Graph(G,Impl->Behaviors.Key(Impl->Element,Impl->Mode,Behavior)))
                    Pose=Tweak->Sample(0,G.Motion,Pose);
        if(G.TransitionPose.Num()==Pose.Num() && G.TransitionTime<G.TransitionDuration)
        {
            G.TransitionTime=FMath::Min(G.TransitionDuration,G.TransitionTime+Delta);
            const float Alpha=G.TransitionTime/G.TransitionDuration;
            for(int32 I=0;I<Pose.Num();++I){FTransform Blended;Blended.Blend(G.TransitionPose[I],Pose[I],Alpha);Pose[I]=Blended;}
        }
        G.RenderedPose=Pose;
        G.World = G.Setup.World(Pose);
        // CPartArray::UpdateBones keeps unscaled attachment/rigid matrices separate from skin deformation.
        G.Bones = G.Setup.World(Pose, false);
        if (Impl->Animated.IsValidIndex(G.Parent))
        {
            const auto &Parent = Impl->Animated[G.Parent];
            if (Parent.Bones.IsValidIndex(G.Socket.Bone))
            {
                G.PartFrame=G.Socket.Frame.ToMatrixWithScale()*Parent.Bones[G.Socket.Bone]*Parent.PartFrame;
                G.Frame=FTransform(G.PartFrame);
            }
        }
        TArray<FMatrix> Skin, SkinNormals;
        for (int32 I = 0; I < G.World.Num(); ++I)
        {
            Skin.Add(G.BindInverse[I]*G.World[I]*G.PartFrame);
            SkinNormals.Add(Skin.Last().Inverse().GetTransposed());
        }
        for (auto &M : G.Meshes)
        {
            const auto &D = *M.Data;
            TArray<FVector> V, N;
            V.SetNumUninitialized(D.Vertices.Num());
            N.SetNumUninitialized(D.Vertices.Num());
            if (D.Skin.IsEmpty())
            {
                const FMatrix Rigid=(!G.Skinned && G.Bones.IsValidIndex(M.Bone)?G.Bones[M.Bone]:FMatrix::Identity)*G.PartFrame;
                const FMatrix Normal=Rigid.Inverse().GetTransposed();
                for(int32 I=0;I<D.Vertices.Num();++I)
                {
                    V[I]=FVector(Rigid.TransformPosition(D.Vertices[I]));
                    N[I]=FVector(Normal.TransformVector(D.Normals[I])).GetSafeNormal();
                }
            }
            else for (int32 I = 0; I < D.Vertices.Num(); ++I)
            {
                FVector P = FVector::ZeroVector, Normal = FVector::ZeroVector;
                float Total = 0;
                for (int32 J = 0; J < 4; ++J)
                {
                    int32 Bone = D.Skin[I].Bones[J];
                    float W = D.Skin[I].Weights[J];
                    if (Skin.IsValidIndex(Bone) && W > 0)
                    {
                        P += FVector(Skin[Bone].TransformPosition(D.Vertices[I])) * W;
                        Normal += FVector(SkinNormals[Bone].TransformVector(D.Normals[I])) * W;
                        Total += W;
                    }
                }
                V[I] = Total > 0 ? P / Total : FVector(G.PartFrame.TransformPosition(D.Vertices[I]));
                N[I] = Total > 0 ? Normal.GetSafeNormal() : FVector(G.PartFrame.Inverse().GetTransposed().TransformVector(D.Normals[I])).GetSafeNormal();
            }
            // Retail CPartArray::RefreshMatrices/Draw composes matrices before
            // object scaling. Bake every part into their common model root so
            // UE cannot reorder nonuniform character scale across a tail/head
            // rotation and tear an otherwise coincident skin seam open.
            M.Component->SetRelativeTransform(FTransform::Identity);
            for (int32 J = 0; J < D.Fragments.Num(); ++J)
                M.Component->UpdateMeshSection(J, V, N, D.UVs, D.DetailUVs, D.UV2, D.UV3, D.Colors, TArray<FProcMeshTangent>());
            MarkAC2FogGeometryDirty(M.Component);
        }
    }
    if(Impl->ModelRoot.IsValid())
    {
        auto* Audio=FAC2Audio::Get(Impl->ModelRoot->GetWorld());
        bool AppearanceChanged=false;
        for(auto It=Impl->ScalarEffects.CreateIterator();It;++It)
        {
            auto& State=It.Value();
            if(Audio)State.Contexts.RemoveAll([&](uint32 Context){return Audio->Finished(Context);});
            const bool HasAppearance=State.Nodes.ContainsByPredicate([](const auto& Node){return Node.AppearanceTable!=0;});
            if(State.Contexts.IsEmpty() && !HasAppearance && !Impl->HasLoopingParticles(It.Key())){It.RemoveCurrent();continue;}
            // Retail shares the scalar across the nodes of a single FX key,
            // visiting their authored ramps in order during UseTime.
            if(State.Scalar.Current!=State.Scalar.Desired)
            {
                for(const auto& Node:State.Nodes)
                {
                    State.Scalar.Ramp=Node.RampTime;State.Scalar.Advance(Delta);
                    if(Node.AppearanceTable)
                    {Impl->ApplyAppearanceNode(It.Key(),Node,State.Scalar.Current);AppearanceChanged=true;}
                    if(Audio)for(uint32 Context:State.Contexts)Audio->SetSoundVolume(Context,State.Scalar.Current);
                }
            }
        }
        if(AppearanceChanged)Impl->RefreshAppearanceFX();
    }
    Impl->Actions.RemoveAll([](const auto& A){return !A.Hold && A.Time>=A.Duration;});
    auto DispatchAnimationFX=[&](const TArray<FAC2Animation::FEffect>& Effects,bool CreatureIdle)
    {
        for(const auto& E:Effects)
        {
            ++Impl->Stats.AnimationEffectTriggers;
            TGuardValue<uint64> ActionScope(Impl->StreakAction,E.SourceAction);
            if(E.Stop)StopEffect(E.Id);
            if(E.Play && FMath::FRand()<FMath::Clamp(E.Probability,0.f,1.f))
                PlayModelEffect(E.Id,FMath::Max(0.f,E.Intensity+FMath::FRandRange(-E.Variation,E.Variation)),nullptr,CreatureIdle);
        }
    };
    DispatchAnimationFX(CreatureIdleEffects,true);
    DispatchAnimationFX(AnimationEffects,false);
    Impl->ReleaseEndedStreakControllers();
    // Snapshot due events before dispatch: an FX node may schedule another script.
    TArray<TPair<FAC2Animation::FEffect,int32>> ScriptTriggers;
    for(int32 I=Impl->ScriptEffects.Num()-1;I>=0;--I)
    {
        auto& Track=Impl->ScriptEffects[I];const float From=Track.Phase;
        Track.Phase=FMath::Min(1.f,From+Delta/Track.Script.Period);
        for(const auto& E:Track.Script.EffectsBetween(From,Track.Phase))ScriptTriggers.Add({E,Track.Depth+1});
        if(Track.Phase>=1)Impl->ScriptEffects.RemoveAt(I);
    }
    for(const auto& Trigger:ScriptTriggers)
    {
        const auto& E=Trigger.Key;const int32 Depth=Impl->EffectDepth;Impl->EffectDepth=Trigger.Value;
        if(E.Stop)StopEffect(E.Id);
        if(E.Play && FMath::FRand()<=FMath::Clamp(E.Probability,0.f,1.f))PlayEffect(E.Id,FMath::Max(0.f,E.Intensity+FMath::FRandRange(-E.Variation,E.Variation)));
        Impl->EffectDepth=Depth;
    }
    if (!Impl->Repulsors.IsEmpty())
    {
        const float WorldSpeed = LocalVelocity ? LocalVelocity->Size() : Speed;
        Impl->Repulsors.RemoveAllSwap([WorldSpeed](const auto& Field) { return !Field->Update(WorldSpeed); });
    }
    if (auto* Root = Impl->ModelRoot.Get())
        for (auto& Streak : Impl->Streaks) Streak.Update(Time, *Root, Impl->StreakModelScale, *this, !Impl->PreviewMode);
    if (Impl->AttachedEffects && Impl->ModelRoot.IsValid())
    {
        TArray<FAC2ParticleSource> Sources;
        for (const auto &A : Impl->AppearanceAttachments)
        {
            FTransform At(A.Node.Offset * 100);
            if(Impl->Animated.IsValidIndex(A.Group))
            {
                const auto &G = Impl->Animated[A.Group];
                if (const auto *Socket = G.Setup.Sockets.Find(A.Node.Holding))
                {
                    if (G.Bones.IsValidIndex(Socket->Bone))
                        At = At * FTransform(Socket->Frame.ToMatrixWithScale() * G.Bones[Socket->Bone]);
                }
                else if (A.Node.Bone)
                {
                    int32 Bone = G.Setup.Names.IndexOfByKey(A.Node.Bone);
                    if (G.Bones.IsValidIndex(Bone))At = At * FTransform(G.Bones[Bone]);
                }
                At=At*G.Frame;
            }
            FAC2ParticleSource S;
            S.Key = A.Key;
            S.Descriptor = A.Descriptor;
            S.Frame = At * Impl->ModelRoot->GetComponentTransform();
            S.ParticleScale=Impl->ModelParticleScale;
            if(A.Node.RemoveRotation)S.Frame.SetRotation(FQuat::Identity);
            Sources.Add(S);
        }
        Impl->AttachedEffects->SyncSources(Sources);
        for(int32 I=Impl->NetworkEffects.Num()-1;I>=0;--I)
        {
            const auto& A=Impl->NetworkEffects[I].Attachment;
            FTransform At=Impl->EffectFrame(A.Node);
            if(A.Node.RemoveRotation)At.SetRotation(FQuat::Identity);
            // Native FireParticleFX leaves the emitter at its birth frame.
            // Retarget means clone an FX target, not move the source to a victim.
            if(A.Node.FireAndForget)At=Impl->NetworkEffects[I].BirthFrame;
            if(!Impl->AttachedEffects->MoveSource(A.Key,At))Impl->NetworkEffects.RemoveAt(I);
        }
        AActor *Owner = Impl->ModelRoot->GetOwner();
        FTransform View = Owner->GetActorTransform();
        if (auto *Capture = Owner->FindComponentByClass<USceneCaptureComponent2D>())
            View = Capture->GetComponentTransform();
        else if (auto *PC = Owner->GetWorld()->GetFirstPlayerController())
            if (PC->PlayerCameraManager)
                View = FTransform(PC->PlayerCameraManager->GetCameraRotation(),
                                  PC->PlayerCameraManager->GetCameraLocation());
        FAC2PreviewLighting Preview=Impl->PreviewLighting;
        if(Impl->PreviewMode)for(auto& Light:Preview.Lights)
        {
            Light.Position=Impl->PreviewToWorld.TransformPosition(Light.Position);
            Light.Direction=Impl->PreviewToWorld.TransformVectorNoScale(Light.Direction);
        }
        Impl->AttachedEffects->Update(View.GetLocation(), View.GetUnitAxis(EAxis::Y),
                                      View.GetUnitAxis(EAxis::Z), Delta, 0, FVector::ZeroVector, true,
                                      Impl->PreviewMode?&Preview:nullptr);
    }
    for(int32 I=Impl->LightEffects.Num()-1;I>=0;--I)
    {
        auto& E=Impl->LightEffects[I];E.Age+=Delta;
        if(!E.Light.IsValid() || (E.Node.Light.Life>0 && E.Age>=E.Node.Light.Life))
        {if(E.Light.IsValid())E.Light->DestroyComponent();Impl->LightEffects.RemoveAt(I);continue;}
        E.Light->SetIntensity(FMath::Max(0.f,E.Node.Light.Intensity.Value(E.Age))*4*E.Node.Light.Envelope(E.Age));
        E.Light->SetAttenuationRadius(FMath::Max(0.f,E.Node.Light.Range.Value(E.Age))*100);
    }
    for(int32 I=Impl->VisualEffects.Num()-1;I>=0;--I)
    {
        auto& E=Impl->VisualEffects[I];E.Age+=Delta;
        if(!E.Actor.IsValid() || E.Age>=E.Life)
        {E.Model.Reset();if(E.Actor.IsValid())E.Actor->Destroy();Impl->VisualEffects.RemoveAt(I);continue;}
        if(E.Attachment.Node.Parent || E.Attachment.Node.Holding || E.Attachment.Node.Bone)
        {
            const auto& N=E.Attachment.Node;FTransform At=Impl->EffectFrame(N);
            if(N.RemoveRotation)At.SetRotation(FQuat::Identity);
            E.Actor->SetActorTransform(At);
        }
        E.Model->Animate(E.Age,0);E.Model->UpdateMaterials(E.Age);
    }
}
bool FAC2AssetLibrary::PlayEffectScript(uint32 Script)
{
    if(Impl->EffectDepth>=4 || Impl->ScriptEffects.Num()>=16)return false;
    TArray<uint8> Bytes;FString Error;FAC2FXScript Decoded;
    if(!Impl->Portal.Read(Script,Bytes,Error) || !Decoded.Decode(Script,Bytes))return false;
    Impl->ScriptEffects.Add({MoveTemp(Decoded),0,Impl->EffectDepth});return true;
}
void FAC2AssetLibrary::SetEffectChannel(uint32 Channel,uint32 Value)
{
    if(EffectChannels.Contains(Channel) && EffectChannels[Channel]==Value)return;
    EffectChannels.Add(Channel,Value);
    ++EffectChannelRevision;
}
void FAC2AssetLibrary::SetHealth(float Current,float Maximum)
{
    if(!FMath::IsFinite(Current) || !FMath::IsFinite(Maximum) || Current<0 || Maximum<=0)return;
    const float Missing=FMath::Clamp((Maximum-Current)/Maximum,0.f,1.f);
    if(Impl->LastHealthFraction==Missing)return;
    Impl->LastHealthFraction=Missing;
    PlayEffect(0x41000002,Missing);
}
uint32 FAC2AssetLibrary::DamageAppearanceFingerprint() const {return Impl->DamageAppearanceHash;}
bool FAC2AssetLibrary::DamageDecal(FAC2Decal& Out)
{
    // AC2E's ordinary-hit ground-splatter presentation reuses only the native
    // blood decal (not the ability animation/particles). The surviving table
    // proves this asset, its gore/hue filters and lifetime for Decapitate; it
    // does not establish an identical decal trigger on every retail attack.
    // The native Decapitate key is stable, but its table is not: stock uses
    // 1E000076 and Icefire uses 1E000077. Discover the authored decal variants
    // once per archive, preserving their gore/hue gates and material IDs.
    if(!Impl->DamageDecalsLoaded)
    {
        TArray<FAC2DatEntry> Entries;FString Error;
        if(!Impl->Portal.Enumerate(Entries,Error))return false;
        Entries.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
        for(const auto& Entry:Entries)if((Entry.Id>>24)==0x1e)
        {
            // Only decode tables that actually mention the key. Subtables are
            // themselves indexed records, so inherited variants are found too.
            // This avoids loading unrelated sounds/materials during a hit.
            TArray<uint8> Bytes;if(!Impl->Portal.Read(Entry.Id,Bytes,Error))continue;
            bool MentionsKey=false;
            for(int32 Offset=4;Offset+4<=Bytes.Num();Offset+=4)
                if(AC2::U32(Bytes.GetData()+Offset)==0x41000620){MentionsKey=true;break;}
            if(!MentionsKey)continue;
            TArray<FAC2FXNode> Nodes;Impl->TableFX(Entry.Id,0x41000620,Nodes);
            for(auto& Node:Nodes)
                if(Node.Decal.Material && Node.Channels.Contains(0x40000001) && Node.Channels.Contains(0x40000007))
                    Impl->DamageDecals.Add(MoveTemp(Node));
        }
        Impl->DamageDecalsLoaded=true;
    }
    for(const auto& Node:Impl->DamageDecals){
        if(!Node.Decal.Material)continue;
        bool Match=true;for(const auto& C:EffectChannels)if(const auto* V=Node.Channels.Find(C.Key))Match&=*V==C.Value;
        if(Match){Out=Node.Decal;return true;}
    }
    return false;
}
bool FAC2AssetLibrary::PlayEffect(uint32 Effect,float Strength,USceneComponent* Target)
{
    return PlayModelEffect(Effect,Strength,Target,false);
}
bool FAC2AssetLibrary::PlayModelEffect(uint32 Effect,float Strength,USceneComponent* Target,bool CreatureIdle)
{
    if(!Impl->ModelRoot.IsValid() || !FMath::IsFinite(Strength) || Strength<0 || Impl->EffectDepth>=4)return false;
    if(Impl->StartupEffects.Contains(Effect))return false;
    if(Strength==0 && !Impl->ScalarEffects.Contains(Effect)){StopEffect(Effect);return true;}
    FAC2DecodedVisual V;if(!Impl->Visual(Impl->ModelVisual,V))return false;
    TArray<FAC2FXNode> Nodes;
    auto Resolve=[&](const FAC2DecodedPart& Part){
        if(auto N=Part.Overrides.Find(Effect))Nodes.Append(*N);
        else if(Part.FXTable)Impl->TableFX(Part.FXTable,Effect,Nodes);
    };
    if(auto N=Impl->AppearanceEffects.Find(Effect))Nodes.Append(*N);else Resolve(V.FX);
    if(Nodes.IsEmpty())for(const auto& Part:V.Parts)Resolve(Part);
    UE_LOG(LogTemp,Verbose,TEXT("AC2 FX resolve visual=%08X key=%08X nodes=%d groups=%d"),Impl->ModelVisual,Effect,Nodes.Num(),Impl->Animated.Num());
    if(Nodes.IsEmpty())return false;
    const bool HasAppearance=Nodes.ContainsByPredicate([](const FAC2FXNode& Node){return Node.AppearanceTable!=0;});
    if(HasAppearance)
    {
        Impl->AppearanceFXStrengths.Add(Effect,Strength);
        Impl->LiveAppearanceFX.Remove(Effect);
    }
    bool Played=false;
    auto* Audio=FAC2Audio::Get(Impl->ModelRoot->GetWorld());
    auto* Game=Impl->ModelRoot->GetWorld()->GetAuthGameMode<AAC2GameMode>();
    const uint32 Terrain=Game?Game->SoundTerrain(Impl->ModelRoot->GetComponentLocation()):0;
    const float Hour=Game?Game->MapHour():12;
    const bool DayTime=Hour>=6 && Hour<18;
    const auto* Player=Impl->ModelRoot->GetWorld()->GetFirstPlayerController();
    if(Audio)for(auto It=Impl->ScalarEffects.CreateIterator();It;++It)
    {
        It.Value().Contexts.RemoveAll([&](uint32 Context){return Audio->Finished(Context);});
        if(It.Value().Contexts.IsEmpty() && !It.Value().Nodes.ContainsByPredicate([](const auto& Node){return Node.AppearanceTable!=0;}) && !Impl->HasLoopingParticles(It.Key()))It.RemoveCurrent();
    }
    TArray<FAC2FXNode> ActiveNodes;
    for(const auto& DefaultNode:Nodes)
    {
        const auto* Variant=DefaultNode.Terrain.Find(Terrain);
        const auto& Node=Variant?**Variant:DefaultNode;
        bool Mismatch=false;
        for(const auto& Channel:EffectChannels)if(const auto* Value=Node.Channels.Find(Channel.Key))Mismatch|=*Value!=Channel.Value;
        if(Mismatch || !(DayTime?Node.DayTime:Node.NightTime) ||
            (Node.PlayerOnly && (!Player || Player->GetPawn()!=Impl->ModelRoot->GetOwner())))continue;
        ActiveNodes.Add(Node);
    }
    if(auto* State=Impl->ScalarEffects.Find(Effect))
    {
        // DoTargettedFX updates a live key rather than creating its children
        // again. Appearance and sound share the scalar and node traversal.
        State->Nodes=ActiveNodes;
        for(const auto& Node:State->Nodes)
        {
            State->Scalar.Ramp=Node.RampTime;State->Scalar.Set(Strength);
            Impl->ApplyAppearanceNode(Effect,Node,State->Scalar.Current);
        }
        if(Audio)for(uint32 Context:State->Contexts)Audio->SetSoundVolume(Context,State->Scalar.Current);
        for(auto& Child:Impl->NetworkEffects)if(Child.Effect==Effect && !Child.Attachment.Node.FireAndForget)Child.Target=Target;
        for(auto& Child:Impl->VisualEffects)if(Child.Effect==Effect)Child.Target=Target;
        if(HasAppearance)Impl->RefreshAppearanceFX();
        return true;
    }
    for(const auto& Node:ActiveNodes)
    {
        FAC2FXScalar Initial;Initial.Start(Strength,Node.RampTime);
        if(Node.AppearanceTable)
        {
            Impl->ApplyAppearanceNode(Effect,Node,Initial.Current);
            Played=true;
        }
        Played|=Impl->StartSound(Node,Effect,Strength,false,CreatureIdle);
        Played |= Impl->StartRepulsor(Node);
        Played |= Impl->ApplyStreak(Node, Effect);
        UE_LOG(LogTemp,Verbose,TEXT("AC2 FX node descriptor=%08X holding=%08X bone=%08X"),Node.Descriptors.IsEmpty()?0:Node.Descriptors[0],Node.Holding,Node.Bone);
        if(Impl->NetworkEffects.Num()>=64)continue;
        if(Node.Script)Played|=PlayEffectScript(Node.Script);
        if(Node.Decal.Material && !Impl->PreviewMode && Game)
            Played|=Game->GroundDecal(Node.Decal,Impl->ModelRoot->GetComponentTransform(),Impl->ModelRoot->GetOwner());
        FTransform Source=Impl->EffectFrame(Node);
        Played|=Impl->StartLight(Node,Effect);
        if(Node.RemoveRotation)Source.SetRotation(FQuat::Identity);
        if(Node.Visual && Impl->VisualEffects.Num()<16)
        {
            auto* World=Impl->ModelRoot->GetWorld();auto* Actor=World->SpawnActor<AActor>();
            auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();Actor->SetActorTransform(Source);
            auto Model=MakeShared<FAC2AssetLibrary>();FString Error;
            Model->Impl->EffectDepth=Impl->EffectDepth+1;
            if(Impl->PreviewMode)Model->SetPreviewLighting(Impl->PreviewLighting,Impl->PreviewToWorld);
            if(Model->Open(Impl->Directory,Error) && Model->DrawModel(Node.Visual,Actor,Root))
            {
                if(auto* Capture=Impl->ModelRoot->GetOwner()->FindComponentByClass<USceneCaptureComponent2D>())
                    Capture->ShowOnlyActors.Add(Actor);
                if(Node.Behavior)Model->PlayBehavior(Node.Behavior);
                if(Node.TargetFX)Model->PlayEffect(Node.TargetFX,Strength,Target);
                const float Life=FMath::IsFinite(Node.Lifetime)&&Node.Lifetime>0?FMath::Clamp(Node.Lifetime,.05f,30.f):FMath::Clamp(Model->BehaviorDuration(Node.Behavior),.5f,30.f);
                Impl->VisualEffects.Add({{-1,Node,0,0},Effect,0,Life,Actor,Model,Target});Played=true;
            }
            else Actor->Destroy();
        }
        if(Node.Descriptors.IsEmpty())continue;
        if(!Impl->AttachedEffects)
        {
            Impl->AttachedEffects=MakeUnique<FAC2Effects>();
            Impl->AttachedEffects->Open(Impl->ModelRoot->GetWorld(),Impl->Directory,!Impl->PreviewMode);
            if(auto* Capture=Impl->ModelRoot->GetOwner()->FindComponentByClass<USceneCaptureComponent2D>())
                Capture->ShowOnlyActors.Add(Impl->AttachedEffects->RenderActor());
        }
        // Root effects also belong to static projectile/effect models, which
        // need no animation setup. Named sockets still require a matching rig.
        int32 Group=INDEX_NONE;
        for(int32 I=0;I<Impl->Animated.Num();++I)
        {
            const auto& G=Impl->Animated[I];
            if((Node.Holding && !G.Setup.Sockets.Contains(Node.Holding)) || (Node.Bone && !G.Setup.Names.Contains(Node.Bone)) || (!Node.Holding && !Node.Bone && G.Parent>=0))continue;
            Group=I;break;
        }
        if(Group!=INDEX_NONE || (!Node.Holding && !Node.Bone))
        {
            const uint32 Descriptor=Node.Descriptors[FMath::RandRange(0,Node.Descriptors.Num()-1)];if(Descriptor>>24!=0x39)continue;
            const uint64 Key=++Impl->EffectSerial;
            // CPartArray::CreateParticleSystem does not scale geometry by the FX
            // scalar. QueueParticleSystems supplies the visual's particle scale.
            FAC2ParticleSource S;S.Key=Key;S.Descriptor=Descriptor;S.Frame=Source;S.ParticleScale=V.ParticleScale;
            if(Impl->AttachedEffects->NativeBurst(S))
            {Impl->NetworkEffects.Add({{Group,Node,Key,Descriptor},Effect,Target,Source});Played=true;}
        }
    }
    if((HasAppearance || Impl->HasLoopingParticles(Effect)) && !ActiveNodes.IsEmpty())Impl->ScalarEffects.FindOrAdd(Effect);
    if(auto* State=Impl->ScalarEffects.Find(Effect);State && !ActiveNodes.IsEmpty())
    {
        State->Nodes=MoveTemp(ActiveNodes);
        State->Scalar.Start(Strength,State->Nodes.Last().RampTime);
    }
    if(HasAppearance)Impl->RefreshAppearanceFX();
    return Played;
}
void FAC2AssetLibrary::StopEffect(uint32 Effect)
{
    // StopFX ignores startup keys unless the model itself is being destroyed.
    // Destruction releases the owned emitters/components directly.
    if(Impl->StartupEffects.Contains(Effect))return;
    for (auto It=Impl->StreakControllers.CreateIterator(); It; ++It)
        if (It.Value().Effect == Effect)
        {
            if (!It.Key()) for (auto& Trail : Impl->Streaks) Trail.Activate(Impl->HeldStreakActive);
            It.RemoveCurrent();
        }
    Impl->Streaks.RemoveAll([Effect](FAC2AttachmentStreak& Trail)
    {
        if (Trail.Effect != Effect) return false;
        Trail.Release();
        return true;
    });
    for(int32 I=Impl->LightEffects.Num()-1;I>=0;--I)if(Impl->LightEffects[I].Effect==Effect)
    {if(Impl->LightEffects[I].Light.IsValid())Impl->LightEffects[I].Light->DestroyComponent();Impl->LightEffects.RemoveAt(I);}
    Impl->AppearanceFXStrengths.Remove(Effect);
    if(Impl->LiveAppearanceFX.Remove(Effect))Impl->RefreshAppearanceFX();
    if(auto* Contexts=Impl->ScalarEffects.Find(Effect))
    {
        if(Impl->ModelRoot.IsValid())if(auto* Audio=FAC2Audio::Get(Impl->ModelRoot->GetWorld()))
            for(uint32 Context:Contexts->Contexts)Audio->Stop(Context);
        Impl->ScalarEffects.Remove(Effect);
    }
    for(int32 I=Impl->VisualEffects.Num()-1;I>=0;--I)if(Impl->VisualEffects[I].Effect==Effect)
    {auto& E=Impl->VisualEffects[I];E.Model.Reset();if(E.Actor.IsValid())E.Actor->Destroy();Impl->VisualEffects.RemoveAt(I);}
    for(int32 I=Impl->NetworkEffects.Num()-1;I>=0;--I)if(Impl->NetworkEffects[I].Effect==Effect)
    {if(Impl->AttachedEffects)Impl->AttachedEffects->StopSource(Impl->NetworkEffects[I].Attachment.Key);Impl->NetworkEffects.RemoveAt(I);}
}
bool FAC2AssetLibrary::HasStartupEffects() const {return !Impl->StartupEffects.IsEmpty();}
bool FAC2AssetLibrary::HoldingStreakActive(uint32 Holding) const
{
    return Holding && Impl->StreakControllers.Contains(Holding);
}
void FAC2AssetLibrary::SetHeldStreakActive(bool Active)
{
    if (Impl->HeldStreakActive == Active) return;
    Impl->HeldStreakActive = Active;
    for (auto& Streak : Impl->Streaks) Streak.Activate(Active || Impl->StreakControllers.Contains(0));
}
int32 FAC2AssetLibrary::EffectParticleCount() const
{
    int32 Count=Impl->AttachedEffects?Impl->AttachedEffects->Count():0;
    for(const auto& E:Impl->VisualEffects)if(E.Model)Count+=E.Model->EffectParticleCount();return Count;
}
bool FAC2AssetLibrary::PrepareBlock(const FString &Directory, const FAC2LandscapeDefs &Defs,
                                    const FAC2LandBlock &Land, const FAC2TerrainMesh &Mesh, FString &Error)
{
    Impl->Preparing = true;
    FAC2DatArchive Cell;
    if (!Cell.Open(FPaths::Combine(Directory, TEXT("cell_1.dat")), 2, Error))
        return false;
    TerrainMaterial(Defs, Land, Mesh);
    Impl->BlockWater = Impl->WaterSource.At(Defs, Land);
    if (Impl->BlockWater.Present)
    {
        FLinearColor Tint;
        bool Masked = false;
        Impl->MaterialImage(Impl->BlockWater.Above, Tint, Masked);
    }
    AddScenery(Cell, Defs, Land, nullptr, nullptr, FVector::ZeroVector);
    if (Impl->CloudShadowTexture) Impl->Image(Impl->CloudShadowTexture);
    FImpl::BuildMips(Impl->TerrainAtlas);
    for (auto &Pair : Impl->Images)
        if (Pair.Value)
            FImpl::BuildMips(*Pair.Value);
    Impl->Preparing = false;
    return true;
}
void FAC2AssetLibrary::ConfigureTerrainCloud(UMaterialInstanceDynamic* Material)
{
    Impl->ConfigureTerrainCloud(Material);
}
UMaterialInterface *FAC2AssetLibrary::UploadTerrain()
{
    check(IsInGameThread());
    ++Impl->Stats.TerrainMaterials;
    // A landblock atlas is not tileable: wrapping blends the opposite bank into
    // its edges. Keep the clamp sampler on the texture actually bound to the MID.
    return Impl->GroundMaterial(Impl->TerrainAtlas);
}
bool FAC2AssetLibrary::UploadScenery(AActor *Owner, USceneComponent *Root, int32 MeshBudget)
{
    if(!Impl->RoomLightsUploaded) {
        Impl->RoomLightsUploaded=true;
        for(int32 I=0;I<Impl->CellLights.Num();++I) {
            const auto& Native=Impl->CellLights[I];
            if(Native.Sunlight || Native.Directional || Native.Intensity<=0 || Native.Range<=0)continue;
            auto* Light=NewObject<UPointLightComponent>(Owner);
            Owner->AddInstanceComponent(Light);Light->SetupAttachment(Root);
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetRelativeLocation(Native.Position);
            Light->bUseInverseSquaredFalloff=false;Light->LightFalloffExponent=2;
            Light->SetAttenuationRadius(Native.Range);
            Light->SetIntensity(Native.Intensity*4);
            Light->SetLightColor(FLinearColor::FromSRGBColor(Native.Diffuse.ToFColor(false)));
            Light->SetLightingChannels(false,true,false);
            Light->SetCastShadows(true);Light->ShadowBias=.25f;Light->SourceRadius=5;
            Light->ShadowResolutionScale=.5f;
            // Fade the light and its shadow together outside the nearby room.
            // Never switch shadow casting between the eight closest lights.
            Light->MaxDrawDistance=Native.Range+3000;
            Light->MaxDistanceFadeRange=1000;
            bool Visible=false;for(const auto& Pair:Native.Vertices)Visible|=Impl->CellVisible(Pair.Key);
            Light->SetVisibility(Visible);Light->RegisterComponent();
            Impl->RoomLights.Add({I,Light});
        }
    }
    const double Start = FPlatformTime::Seconds();
    while (Impl->NextDraw < Impl->Draws.Num() && MeshBudget-- > 0)
    {
        const auto &Draw = Impl->Draws[Impl->NextDraw++];
        auto *Component = Impl->RenderDraw(Draw, Owner, Root);
        if (Impl->Animated.IsValidIndex(Draw.AnimationGroup))
            Impl->Animated[Draw.AnimationGroup].Meshes.Add({Draw.Mesh, Component, Draw.Bone});
        if (FPlatformTime::Seconds() - Start > 0.004)
            break;
    }
    return Impl->NextDraw >= Impl->Draws.Num();
}
bool FAC2AssetLibrary::CollisionReady() const
{
    for (const auto &Weak : Impl->CollisionMeshes)
        if (auto *C = Weak.Get())
            if (C->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            if (!C->GetBodyInstance() || !C->GetBodyInstance()->IsValidBodyInstance())
                return false;
    return true;
}
void FAC2AssetLibrary::SetActiveCell(uint32 Cell)
{
    if (Impl->ActiveCell == Cell) return;
    Impl->ActiveCell = Cell;
    Impl->VisibleCells = Impl->Cells.Reachable(Cell);
    Impl->ShowExterior = Impl->Cells.ReachesOutside(Cell);
    if (Cell && Impl->ShowExterior) Impl->VisibleCells.Append(Impl->Cells.Reachable(0));
    for (const auto& Entry : Impl->CellMeshes)
        if (auto* C = Entry.Mesh.Get())
        {
            const bool Visible = Impl->CellVisible(Entry.Cell);
            C->SetVisibility(Visible);
            C->SetCollisionEnabled(Visible && Entry.Solid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        }
    for(const auto& Entry:Impl->RoomLights)if(auto* Light=Entry.Value.Get()) {
        bool Visible=false;for(const auto& Pair:Impl->CellLights[Entry.Key].Vertices)Visible|=Impl->CellVisible(Pair.Key);
        Light->SetVisibility(Visible);
    }
}
bool FAC2AssetLibrary::CellVisible(uint32 Cell) const { return Impl->CellVisible(Cell); }
bool FAC2AssetLibrary::CellReachesOutside(uint32 Cell) const { return Impl->Cells.ReachesOutside(Cell); }
void FAC2AssetLibrary::InteriorSpawnPoints(const FVector &Near, float Radius, TArray<FVector> &Out) const
{
    for (const auto &Cell : Impl->InteriorPositions)
        if (Impl->CellVisible(Cell.Key))
        for (const FVector &P : Cell.Value)
            if (FVector::DistSquared2D(P, Near) <= FMath::Square(Radius) && FMath::Abs(P.Z - Near.Z) < 180)
                Out.Add(P);
}
void FAC2AssetLibrary::ReleaseCPUData()
{
    check(IsInGameThread());
    Impl->Draws.Empty();
    Impl->FrillBatches.Empty();
    Impl->Geometry.Empty();
    Impl->SceneAppearanceMeshes.Empty();
    Impl->Visuals.Empty();
    Impl->Images.Empty();
    Impl->Materials.Empty();
    Impl->TerrainAtlas.Pixels.Empty();
    Impl->TerrainAtlas.Mips.Empty();
    Impl->Scenery.Reset();
}
bool FAC2AssetLibrary::DrawSky(uint32 ObjectId, AActor *Owner, USceneComponent *Root)
{
    check(IsInGameThread());
    Impl->SkyMode = true;
    // Celestial objects can reuse a material but have independent animated tints.
    Impl->RuntimeMaterials.Reset();
    FAC2DecodedEntity E;
    E.Data = ObjectId;
    E.Scale = FVector(1.0);
    if (!Impl->DrawEntity(E, Owner, Root, FVector::ZeroVector, false))
        return false;
    // Retail draws the sky before the world with depth writes disabled. Unreal
    // draws translucency later: preserve angular size but put every layer beyond
    // the navigable world, otherwise its nearby glow meshes smear across terrain.
    TInlineComponentArray<UProceduralMeshComponent *> Components(Owner);
    double Nearest = TNumericLimits<double>::Max();
    for (auto *C : Components)
        for (int32 I = 0; I < C->GetNumSections(); ++I)
            if (auto *S = C->GetProcMeshSection(I))
                for (const auto &V : S->ProcVertexBuffer)
                {
                    double D = C->GetRelativeTransform().TransformPosition(V.Position).Length();
                    if (D > 1)
                        Nearest = FMath::Min(Nearest, D);
                }
    if (Nearest < TNumericLimits<double>::Max())
        for (auto *C : Components)
        {
            auto T = C->GetRelativeTransform();
            const double Scale = FMath::Max(1.0, 10000000.0 / Nearest);
            T.SetTranslation(T.GetTranslation() * Scale);
            T.SetScale3D(T.GetScale3D() * Scale);
            C->SetRelativeTransform(T);
            C->SetReceivesDecals(false);
        }
    UE_LOG(LogTemp, Display, TEXT("AC2 sky depth %08X: native nearest %.2f cm, projected beyond 100 km"),
           ObjectId, Nearest);
    return true;
}
UMaterialInterface *FAC2AssetLibrary::Material(uint32 Id, bool Water, bool Below)
{
    if (!Water)
        return Impl->RuntimeMaterial(Id);
    FLinearColor Tint;
    bool Masked = false;
    auto I = Impl->MaterialImage(Id, Tint, Masked);
    auto *Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Water.M_AC2Water"));
    if (!Parent)
        return nullptr;
    auto *M = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
    Impl->Objects.Add(M);
    if (I)
        M->SetTextureParameterValue(TEXT("Albedo"), Impl->Texture(*I));
    M->SetVectorParameterValue(TEXT("Tint"), Tint);
    M->SetScalarParameterValue(TEXT("AC2WaterBelow"),Below?1.f:0.f);
    // Water still uses the DAT material cascade and its UV/texture modifiers.
    // Taking only its first image loses the ripples, transparency and motion.
    if(auto Native=Impl->Material(Id);Native && !Native->Layers.IsEmpty())
    {
        const int32 Index=FImpl::SelectedLayer(*Native);
        const auto& Layer=Native->Layers[Index];
        Impl->ConfigureStages(M,Layer,Index,false);
    }
    if (!Below)
        Impl->ConfigureShore(M);
    Impl->TrackMaterial(Id,M);
    return M;
}
int32 FAC2AssetLibrary::AnimationCount() const
{
    int32 N = 0;
    for (const auto &G : Impl->Animated)
        N += (G.Idle ? 1 : 0) + (G.Locomotion ? 1 : 0);
    return N;
}
int32 FAC2AssetLibrary::MovementAnimationUpdates() const
{
    return Impl->MovingPoseUpdates;
}
bool FAC2AssetLibrary::AuditLocomotion(uint32 VisualId, FString &Details)
{
    FAC2DecodedVisual Visual;
    if (!Impl->Visual(VisualId, Visual))
        return false;
    bool Found = false;
    for (const auto &Part : Visual.Parts)
    {
        if (!Part.AnimMap || !Part.Setup)
            continue;
        auto Graph = FAC2Animator::Load(Impl->Portal, Part.AnimMap, 0x80000001);
        TArray<uint8> Bytes;
        FAC2Setup Setup;
        if (!Graph || !Impl->Read(Part.Setup, Bytes) || !Setup.Decode(Part.Setup, Bytes))
            continue;
        FAC2AnimatorInput Input;
        Input.Forward = 1.6f / FMath::Max(.01, Visual.Scale.Y);
        const auto A = Graph->Sample(.1f, Input, Setup.Rest), B = Graph->Sample(.4f, Input, Setup.Rest);
        int32 Changed = 0;
        for (int32 I = 0; I < A.Num(); ++I)
            Changed += !A[I].Equals(B[I], .01f) ? 1 : 0;
        FString Clips;
        for (uint32 Clip : Graph->ActiveClips(Input))
            Clips += FString::Printf(TEXT(" %08X"), Clip);
        const bool Pass = Changed >= 2 && Graph->Period(Input) > .001f;
        Found |= Pass;
        Details += FString::Printf(TEXT("map=%08X setup=%08X movingBones=%d period=%g clips=%s; "),
                                   Part.AnimMap, Part.Setup, Changed, Graph->Period(Input), *Clips);
    }
    return Found;
}
bool FAC2AssetLibrary::ParticleMaterialUsesLighting(uint32 Id)
{
    const auto M=Impl->Material(Id);
    return M && !M->Layers.IsEmpty() && M->Layers[0].ParticleLighting;
}
UMaterialInterface *FAC2AssetLibrary::ParticleMaterial(uint32 Id)
{
    FLinearColor Tint = FLinearColor::White;
    bool Masked = false;
    auto I = Impl->MaterialImage(Id, Tint, Masked);
    auto Native = Impl->Material(Id);
    bool Additive = Native && !Native->Layers.IsEmpty() && Native->Layers[FImpl::SelectedLayer(*Native)].DestBlend == 2;
    auto *Parent = LoadObject<UMaterialInterface>(
        nullptr, Additive ? TEXT("/Game/AC2/Materials/M_AC2ParticleAdd.M_AC2ParticleAdd")
                          : TEXT("/Game/AC2/Materials/M_AC2Particle.M_AC2Particle"));
    if (!Parent)
        return nullptr;
    auto *M = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
    Impl->Objects.Add(M);
    if (I)
        M->SetTextureParameterValue(TEXT("Albedo"), Impl->Texture(*I));
    M->SetVectorParameterValue(TEXT("Tint"), Tint);
    Impl->TrackMaterial(Id, M);
    return M;
}
UTexture2D *FAC2AssetLibrary::Texture(uint32 Id)
{
    auto I = Impl->Image(Id);
    return I ? Impl->Texture(*I) : nullptr;
}
FAC2BlockWater FAC2AssetLibrary::Water() const
{
    return Impl->BlockWater;
}
bool FAC2AssetLibrary::InteriorAt(const FVector &P) const
{
    for (const auto &B : Impl->InteriorBounds)
        if (Impl->CellVisible(B.Key) && B.Value.ExpandBy(FVector(10, 10, 60)).IsInsideOrOn(P))
            return true;
    return false;
}
bool FAC2AssetLibrary::InteriorPositions(uint32 Id, TArray<FVector> &P) const
{
    const auto *V = Impl->InteriorPositions.Find(Id);
    if (!V)
        return false;
    P = *V;
    return !P.IsEmpty();
}
bool FAC2AssetLibrary::InteriorCellAt(const FVector& P,uint32& Cell) const
{
    double Best=180;
    bool Found=false;
    for(const auto& Entry:Impl->InteriorFloorTriangles)
    {
        // A loaded landblock can contain many unrelated rooms. Lighting asks
        // this for every material, even on outdoor foliage. Reject each room
        // by its existing geometry bounds before testing its floor triangles.
        const FBox* Bounds=Impl->InteriorBounds.Find(Entry.Key);
        // Retain the narrow phase's barycentric edge tolerance even in large
        // rooms. Missing broad-phase data must fall back to the exact query.
        if(Bounds && !Bounds->ExpandBy(FVector(10,10,180)+Bounds->GetSize()*.002).IsInsideOrOn(P))continue;
        if (Impl->CellVisible(Entry.Key))
        for(int32 I=0;I+2<Entry.Value.Num();I+=3)
        {
            const FVector A=Entry.Value[I],B=Entry.Value[I+1],C=Entry.Value[I+2];
            const double Den=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
            if(FMath::Abs(Den)<.01)continue;
            const double U=((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y))/Den;
            const double V=((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y))/Den;
            if(U<-.001||V<-.001||U+V>1.001)continue;
            const double Distance=FMath::Abs(P.Z-(U*A.Z+V*B.Z+(1-U-V)*C.Z));
            if(Distance<Best){Best=Distance;Cell=Entry.Key;Found=true;}
        }
    }
    return Found;
}
TArray<FColor> FAC2AssetLibrary::TerrainPalette()
{
    TArray<FColor> Out;
    Out.Init(FColor(100, 110, 80), 128);
    for (const auto &Pair : Impl->TerrainSurfaces)
    {
        const auto *Bands = Impl->Surfaces.Find(Pair.Value);
        if (!Bands || Bands->IsEmpty() || (*Bands)[0].Base.IsEmpty() || Pair.Key >= 128)
            continue;
        FLinearColor Tint;
        bool Masked = false;
        auto I = Impl->MaterialImage((*Bands)[0].Base[0].Id, Tint, Masked);
        if (!I)
            continue;
        FLinearColor Mean(0, 0, 0, 0);
        for (int32 Y = 0; Y < 8; ++Y)
            for (int32 X = 0; X < 8; ++X)
                Mean += I->Sample(X / 8.0, Y / 8.0);
        Out[Pair.Key] = (Mean / 64 * Tint * (*Bands)[0].Tint).ToFColor(false);
    }
    return Out;
}
const FAC2AssetStats &FAC2AssetLibrary::Stats() const
{
    return Impl->Stats;
}
void FAC2AssetLibrary::AddReferencedObjects(FReferenceCollector &Collector)
{
    Collector.AddReferencedObjects(Impl->Objects);
}
bool FAC2AssetLibrary::Open(const FString &Directory, FString &Error)
{
    Impl->Directory = AC2::ResolveDatDirectory(Directory);
    Impl->HoldBehaviors.Reset();
    check(IsInGameThread());
    FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!Impl->Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error))
        return false;
    Impl->ShoreMaterials.Reset();
    Impl->DamageDecalsLoaded=false;
    Impl->DamageDecals.Reset();
    FString OptionalError;
    Impl->HasHighRes = Impl->HighRes.Open(FPaths::Combine(Directory, TEXT("highres.dat")), 1, OptionalError);
    Impl->TextureNamespace = Impl->Directory;
    Impl->Stats.HighResAvailable = Impl->HasHighRes;
    if (!Impl->HasHighRes)
        UE_LOG(LogTemp, Warning, TEXT("AC2 highres.dat unavailable: %s"), *OptionalError);
    Impl->HasLocal =
        Impl->Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, OptionalError);
    Impl->Opaque =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Opaque.M_AC2Opaque"));
    Impl->Cutout =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Cutout.M_AC2Cutout"));
    if (!Impl->Opaque || !Impl->Cutout)
    {
        Error = TEXT("Missing AC2 materials. Run Launch-AC2.bat to prepare them.");
        return false;
    }
    Impl->Objects.Add(Impl->Opaque);
    Impl->Objects.Add(Impl->Cutout);
    if (!Impl->Metadata(Error))
        return false;
    if (!Impl->WaterSource.Load(Impl->Portal, Error))
        return false;
    Impl->Scenery = MakeUnique<FAC2SceneryGenerator>(Impl->Portal);
    return Impl->Scenery->Initialize(Error);
}

TArray<FColor> FAC2AssetLibrary::TerrainPixels(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
    const FAC2TerrainMesh &Mesh, int32 Resolution)
{
    check(Resolution > 0 && Resolution <= 2048);
    struct FCorner
    {
        TSharedPtr<FAC2DecodedImage> Image;
        float Tile = 10;
        FLinearColor Tint = FLinearColor::White;
    };
    TArray<FCorner> Corners;
    Corners.SetNum(289);
    const uint32 BX = Land.Id >> 24, BY = (Land.Id >> 16) & 255;
    for (int32 X = 0; X < 17; ++X)
        for (int32 Y = 0; Y < 17; ++Y)
        {
            const int32 I = X * 17 + Y;
            const uint32 Terrain = Land.CellInfo[I] & Defs.TerrainMask;
            const uint32 *Surface = Impl->TerrainSurfaces.Find(Terrain);
            const auto *Bands = Impl->Surfaces.Find(Surface ? *Surface : Impl->DefaultSurface);
            if (!Bands)
                continue;
            const float Pitch = Mesh.Normals[I].X * Mesh.Normals[I].X + Mesh.Normals[I].Y * Mesh.Normals[I].Y;
            const FAC2DecodedBand *Band = Bands->FindByPredicate(
                [&](const FAC2DecodedBand &B) { return B.Min * B.Min <= Pitch && Pitch <= B.Max * B.Max; });
            if (!Band || Band->Base.IsEmpty())
                continue;
            const float Draw =
                float(Random(BX * 16 + X, BY * 16 + Y, 0x003c47f7)) * 2.3283064e-10f * Band->Total;
            float Sum = 0;
            const FAC2DecodedWeighted *Choice = &Band->Base.Last();
            for (const auto &W : Band->Base)
            {
                Sum += W.Weight;
                if (Draw <= Sum)
                {
                    Choice = &W;
                    break;
                }
            }
            bool Masked = false;
            FLinearColor Tint = FLinearColor::White;
            Corners[I].Image = Impl->MaterialImage(Choice->Id, Tint, Masked);
            Corners[I].Tile = FMath::Max(0.1f, Choice->Tile);
            Corners[I].Tint = Tint * Band->Tint;
        }
    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(Resolution * Resolution);
    ParallelFor(
        TEXT("AC2TerrainAtlas"), Resolution, 32,
        [&](int32 Y) {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const double LX = (X + 0.5) / Resolution * 16, LY = (Y + 0.5) / Resolution * 16;
                const int32 IX = FMath::Min(15, int32(LX)), IY = FMath::Min(15, int32(LY));
                const double WX = (BX + LX / 16) * Defs.BlockSize(), WY = (BY + LY / 16) * Defs.BlockSize();
                auto Sample = [&](int32 A, int32 B) {
                    const auto &C = Corners[A * 17 + B];
                    return C.Image ? C.Image->Sample(WX / C.Tile, WY / C.Tile) * C.Tint
                                   : FLinearColor(0.3f, 0.3f, 0.3f);
                };
                FLinearColor Color = FMath::Lerp(
                    FMath::Lerp(Sample(IX, IY), Sample(IX + 1, IY), float(LX - IX)),
                    FMath::Lerp(Sample(IX, IY + 1), Sample(IX + 1, IY + 1), float(LX - IX)), float(LY - IY));
                Color.A = 1;
                // Color is already native byte RGB. Encoding it again brightens
                // the authored surface tint and disagrees with the far palette.
                Pixels[Y * Resolution + X] = Color.ToFColor(false);
            }
        },
        EParallelForFlags::BackgroundPriority);
    return Pixels;
}

UMaterialInterface *FAC2AssetLibrary::TerrainMaterial(const FAC2LandscapeDefs &Defs,
                                                      const FAC2LandBlock &Land, const FAC2TerrainMesh &Mesh)
{
    FAC2DecodedImage Atlas;
    Atlas.W = Atlas.H = 2048;
    Atlas.Pixels = TerrainPixels(Defs, Land, Mesh, Atlas.W);
    if (Impl->Preparing)
    {
        Impl->TerrainAtlas = MoveTemp(Atlas);
        return nullptr;
    }
    ++Impl->Stats.TerrainMaterials;
    return Impl->GroundMaterial(Atlas);
}

TSharedPtr<FJsonObject> FAC2AssetLibrary::AuditRenderRecords(FString &Error)
{
    TArray<FAC2DatEntry> Entries;
    if (!Impl->Portal.Enumerate(Entries, Error)) return nullptr;
    auto Report = MakeShared<FJsonObject>();
    bool Passed = true;
    for (uint32 Type : {0x04u, 0x05u, 0x06u, 0x1fu, 0x20u, 0x2bu, 0x31u, 0x57u})
    {
        int32 Decoded = 0, Fallback = 0, AlternateLayers = 0, OpacityAtoms = 0;
        TArray<TSharedPtr<FJsonValue>> Rejected, Limited, Opacities;
        for (const auto &E : Entries) if ((E.Id >> 24) == Type)
        {
            bool Valid = false;
            if (Type == 0x04 || Type == 0x57)
            {
                TArray<uint8> Bytes;
                if (Impl->Read(E.Id, Bytes))
                {
                    if (Type == 0x04) { FAC2Setup Setup; Valid = Setup.Decode(E.Id, Bytes); }
                    else { FAC2FXScript Script; Valid = Script.Decode(E.Id, Bytes); }
                }
            }
            else if (Type == 0x05) Valid = FAC2Animator::Load(Impl->Portal, E.Id, 0).IsValid();
            else if (Type == 0x06)
            {
                Valid = Impl->Mesh(E.Id).IsValid();
                Impl->Geometry.Reset();
            }
            else if (Type == 0x1f)
            {
                FAC2DecodedVisual V; Valid = Impl->Visual(E.Id, V);
                Impl->Visuals.Reset();
            }
            else if (Type == 0x20)
            {
                auto Table = Impl->AppearanceTable(E.Id); Valid = Table.IsValid();
                if (Table) for (const auto &Pair : Table->Collections) for (const auto &C : Pair.Value)
                    for (const auto &A : C.Atoms.Opacities)
                    {
                        ++OpacityAtoms;
                        auto Atom = MakeShared<FJsonObject>();
                        Atom->SetStringField(TEXT("table"), FString::Printf(TEXT("%08X"), E.Id));
                        Atom->SetStringField(TEXT("key"), FString::Printf(TEXT("%08X"), Pair.Key));
                        Atom->SetNumberField(TEXT("threshold"), C.Intensity);
                        Atom->SetNumberField(TEXT("priority"), A.X); Atom->SetNumberField(TEXT("opacity"), A.Y);
                        Opacities.Add(MakeShared<FJsonValueObject>(Atom));
                    }
                Impl->AppearanceTables.Reset();
            }
            else
            {
                auto M = Impl->Material(E.Id); Valid = M.IsValid();
                if (M && !M->Layers.IsEmpty())
                {
                    const int32 Selected = FImpl::SelectedLayer(*M);
                    if (!FImpl::FixedFunction(M->Layers[Selected]))
                    {
                        ++Fallback; Limited.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%08X"), E.Id)));
                    }
                    for (int32 I=0; I<M->Layers.Num(); ++I)
                        if (I!=Selected && !FImpl::FixedFunction(M->Layers[I])) ++AlternateLayers;
                }
                Impl->Materials.Reset();
            }
            if (Valid) ++Decoded;
            else Rejected.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%08X"), E.Id)));
        }
        auto Family = MakeShared<FJsonObject>();
        Family->SetNumberField(TEXT("decoded"), Decoded);
        Family->SetArrayField(TEXT("rejected"), Rejected);
        Family->SetNumberField(TEXT("fallback_materials"), Fallback);
        Family->SetArrayField(TEXT("fallback_ids"), Limited);
        Family->SetNumberField(TEXT("unsupported_alternate_layers"), AlternateLayers);
        if (Type == 0x20)
        {
            Family->SetNumberField(TEXT("opacity_atoms"), OpacityAtoms);
            Family->SetArrayField(TEXT("opacity_definitions"), Opacities);
        }
        Report->SetObjectField(FString::Printf(TEXT("%02X"), Type), Family);
        Passed &= Rejected.IsEmpty();
        UE_LOG(LogTemp, Display, TEXT("AC2 render records %02X: %d decoded, %d rejected, %d fallback, %d opacity atoms"),
            Type, Decoded, Rejected.Num(), Fallback, OpacityAtoms);
    }
    Report->SetBoolField(TEXT("decoders_passed"), Passed);
    Report->SetStringField(TEXT("scope"), TEXT("Setups, animation trees, geometry, visual descriptions, appearance tables, materials and FX scripts. Fallback counts use the runtime-selected material layer. Decoding does not establish complete rendering or gameplay parity."));
    return Report;
}

void FAC2AssetLibrary::AddScenery(FAC2DatArchive &Cell, const FAC2LandscapeDefs &Defs,
                                  const FAC2LandBlock &Land, AActor *Owner, USceneComponent *Root,
                                  const FVector &Offset)
{
    const uint32 Id = (Land.Id & 0xffff0000) | 0xfffe;
    TArray<uint8> B;
    FString Error;
    FAC2DatEntry Entry;
    TArray<FAC2DecodedEntity> Entities;
    int32 CellCount = 0;
    uint32 LightInfo=0; bool HasLightInfo=false;
    Impl->PlacedBounds.Reset();
    TGuardValue<const FAC2LandBlock *> EnvironmentContext(Impl->EnvironmentLand, &Land);
    TGuardValue<const FAC2LandscapeDefs *> GroundContext(Impl->EnvironmentDefs, &Defs);
    TGuardValue<FVector> OffsetContext(Impl->EnvironmentOffset, Offset);
    Impl->EnvironmentTerrainMask = Defs.TerrainMask;
    if (Cell.Find(Id, Entry, Error) && Cell.Read(Id, B, Error))
    {
        FAC2AssetReader R(MoveTemp(B));
        const uint32 Bits = R.Word();
        if (R.Word() != Id)
            return;
        if (Bits & 1)
            R.Word();
        CellCount = R.Count(0, 16384);
        LightInfo=R.Word(); R.Word();
        const int32 N = R.Count(4, 16384);
        for (int32 I = 0; I < N && R.Good; ++I)
            Entities.Add(Impl->Entity(R));
        if (!R.Good)
        {
            Impl->Warn(Id, TEXT("unsupported placed entity layout"));
            Impl->Stats.SkippedEntities += N;
            return;
        }
        const uint32 Links=R.Word();
        if(Links==0) {
            TMap<uint32,TArray<uint8>> Props; Impl->SkipProperties(R,&Props);
            if(const auto* A=Props.Find(0x4000005a); A && A->Num()>=4)
                Impl->InteriorAmbient=AC2NativeRGB(FColor(AC2::U32(A->GetData())));
            // CLandBlockInfo::SetProperty: interior fog is independent of the
            // surrounding daylight/weather and may explicitly be disabled.
            if(const auto* P=Props.Find(0x40000056); P && P->Num()>=4)
                Impl->InteriorFogType=AC2::U32(P->GetData())==0x40000002?1:0;
            if(const auto* P=Props.Find(0x40000057); P && P->Num()>=4)
                Impl->InteriorFogColor=AC2NativeRGB(FColor(AC2::U32(P->GetData())));
            if(const auto* P=Props.Find(0x40000058); P && P->Num()>=4)
                Impl->InteriorFogNear=AC2::F32(P->GetData());
            if(const auto* P=Props.Find(0x40000059); P && P->Num()>=4)
                Impl->InteriorFogFar=AC2::F32(P->GetData());
        } else Impl->Warn(Id,TEXT("unsupported entity links before interior lighting properties"));
    }
    Impl->CellLights.Reset();
    if(LightInfo && Cell.Read(LightInfo,B,Error)) {
        FAC2AssetReader R(MoveTemp(B));
        HasLightInfo=DecodeAC2CellLights(R,LightInfo,[&](FAC2AssetReader& Reader,TMap<uint32,TArray<uint8>>& Props){Impl->SkipProperties(Reader,&Props);},Impl->CellLights);
        if(!HasLightInfo) Impl->Warn(LightInfo,TEXT("unsupported static light record"));
        else UE_LOG(LogTemp,Display,TEXT("AC2 native cell lighting %08X: %d lights"),LightInfo,Impl->CellLights.Num());
    }
    for (FAC2DecodedEntity E : Entities)
    {
        ++Impl->Stats.Entities;
        if (!Impl->DrawEntity(E, Owner, Root, Offset, true))
            ++Impl->Stats.SkippedEntities;
    }
    for (int32 C = 0; C < CellCount; ++C)
    {
        const uint32 CellId = (Land.Id & 0xffff0000) | (0x100 + C);
        if (!Cell.Read(CellId, B, Error))
            continue;
        FAC2AssetReader R(MoveTemp(B));
        if (R.Word() != CellId)
            continue;
        TGuardValue<uint32> CellContext(Impl->DrawingCell, CellId);
        const int32 FirstDraw = Impl->Draws.Num(), FirstSource = Impl->Sources.Num(), FirstSound=Impl->Sounds.Num();
        Impl->Cells.Links.FindOrAdd(CellId);
        const FVector Origin = R.Vector() * 100;
        const FQuat Rotation = R.Quaternion();
        const uint32 Flags = R.Word(), MeshId = R.Word();
        auto G = Impl->Mesh(MeshId);
        if (!R.Good || !G)
            continue;
        const FTransform Frame(Rotation, Origin + Offset);
        // A shared room mesh can appear in many cells with different light lists.
        G=MakeShared<FAC2DecodedGeometry>(*G);
        // Native room vertex colors contain baked illumination. Actual shadowed
        // lights now supply that illumination once, for both rooms and actors.
        G->Colors.Init(FColor::White,G->Vertices.Num());
        FBox Bounds(ForceInit);
        for (const auto &V : G->Vertices)
            Bounds += Frame.TransformPosition(V);
        TArray<FVector> Floors;
        TArray<FVector> FloorTriangles;
        for (const auto &Fragment : G->Fragments)
            for (int32 I = 0; I + 2 < Fragment.Indices.Num(); I += 3)
            {
                FVector A = Frame.TransformPosition(G->Vertices[Fragment.Indices[I]]),
                        B0 = Frame.TransformPosition(G->Vertices[Fragment.Indices[I + 1]]),
                        C0 = Frame.TransformPosition(G->Vertices[Fragment.Indices[I + 2]]);
                FVector N = FVector::CrossProduct(C0 - A, B0 - A);
                if (N.Size() > 2500 && N.GetSafeNormal().Z > .7)
                {
                    Floors.Add((A + B0 + C0) / 3);
                    FloorTriangles.Append({A,B0,C0});
                }
            }
        const FVector Center = Bounds.GetCenter();
        Floors.Sort([&](const FVector &A, const FVector &B0) {
            return FVector::DistSquared2D(A, Center) < FVector::DistSquared2D(B0, Center);
        });
        if (Floors.Num() > 256)
            Floors.SetNum(256);
        Impl->InteriorPositions.Add(CellId, MoveTemp(Floors));
        Impl->InteriorSoundOrigins.Add(CellId,Frame.GetLocation());
        Impl->InteriorFloorTriangles.Add(CellId,MoveTemp(FloorTriangles));
        Impl->InteriorBounds.Add(CellId, Bounds);
        if (Impl->Preparing)
        {
            Impl->Draws.Add({G, Frame, true, false, -1, 0, true});
            for (const auto &F : G->Fragments)
            {
                FLinearColor Tint;
                bool Masked = false;
                Impl->MaterialImage(F.Material, Tint, Masked);
            }
        }
        else
            Impl->RenderDraw({G, Frame, true, false, -1, 0, true}, Owner, Root);
        ++Impl->Stats.InteriorCells;
        // CEnvCell::StreamPack portal links, not XY overlap, join dungeon rooms.
        if (Flags & 1) Impl->Cells.Outside.Add(CellId);
        const int32 NP = R.Count(8, 4096);
        for (int32 J = 0; J < NP && R.Good; ++J)
        {
            uint16 Bits = R.Short();
            R.Short(); // portal polygon
            const uint16 Other = R.Short();
            R.Short(); // other portal
            if (Bits & 2) Impl->Cells.Outside.Add(CellId);
            else Impl->Cells.Connect(CellId, (CellId & 0xffff0000) | Other);
            if (Bits & 4)
            {
                int32 N = R.Short();
                R.Take(int64(N) * 2);
                R.Align();
            }
        }
        R.Align();
        for (int32 J = 0; J < 2; ++J)
        {
            int32 N = R.Count(2, 16384);
            for (int32 K = 0; K < N; ++K)
            {
                const uint16 Other = R.Short();
                if (J == 1) Impl->Cells.Connect(CellId, (CellId & 0xffff0000) | Other);
            }
            R.Align();
        }
        if (Flags & 2)
        {
            R.Word();
            int32 N = R.Count(4, 16384);
            for (int32 J = 0; J < N && R.Good; ++J)
            {
                auto E = Impl->Entity(R);
                if(const auto* RoomFloors=Impl->InteriorFloorTriangles.Find(CellId))
                    if(RepairAC2AcademyPlacement(CellId,J,E.Data,E.Origin,Offset,*RoomFloors))
                    {
                        ++Impl->Stats.AcademyPlacementRepairs;
                        UE_LOG(LogTemp,Display,TEXT("AC2 academy placement repair cell=%08X ordinal=%d entity=%08X position=%s"),CellId,J,E.Data,*E.Origin.ToString());
                    }
                // CEnvCell::CreatePhysicsObj applies EntityDesc::m_offset directly
                // in landblock space. Only the room mesh uses the cell frame.
                // Applying it twice displaces torches and furnishings from their lights.
                Impl->DrawEntity(E, Owner, Root, Offset, false);
            }
        }
        for (int32 J = FirstDraw; J < Impl->Draws.Num(); ++J) Impl->Draws[J].Cell = CellId;
        for (int32 J = FirstSource; J < Impl->Sources.Num(); ++J) Impl->Sources[J].Cell = CellId;
        for (int32 J = FirstSound; J < Impl->Sounds.Num(); ++J) Impl->Sounds[J].Cell = CellId;
    }
    for (FBox &Box : Impl->PlacedBounds)
    {
        Box.Min = (Box.Min - Offset) / 100;
        Box.Max = (Box.Max - Offset) / 100;
    }
    TArray<FAC2SceneryPlacement> Placements;
    Impl->Scenery->Generate(Defs, Land, Impl->PlacedBounds, Placements, Impl->BlockWater);
    Impl->LandscapeSounds=Impl->Scenery->AmbientPoints(Defs,Land);
    for (const auto &P : Placements)
    {
        Impl->Decoration = P.Frill;
        Impl->GroundDecoration = P.GroundFrill;
        Impl->Stats.FrillsGenerated += P.Frill ? 1 : 0;
        FAC2DecodedEntity E;
        E.Data = P.Object;
        E.Origin = P.Transform.GetLocation();
        E.Rotation = P.Transform.GetRotation();
        E.Scale = P.Transform.GetScale3D();
        if (Impl->DrawEntity(E, Owner, Root, Offset, false))
        {
            ++Impl->Stats.ProceduralEntities;
            Impl->Stats.FrillsResolved += P.Frill ? 1 : 0;
        }
        else
            ++Impl->Stats.SkippedEntities;
    }
    Impl->Decoration = false;
    Impl->GroundDecoration = false;
    Impl->Stats.UnsupportedSceneGroups = Impl->Scenery->UnsupportedGroups;
    UE_LOG(LogTemp, Display,
           TEXT("AC2 environment %04X: grass %d/%d in %d batches; highres %d/%d textures; world FX %d"),
           Land.Id >> 16, Impl->Stats.FrillsResolved, Impl->Stats.FrillsGenerated, Impl->Stats.FrillBatches,
           Impl->Stats.HighResSurfaces, Impl->Stats.Textures, Impl->Sources.Num());
}

const TArray<FAC2ParticleSource> &FAC2AssetLibrary::ParticleSources() const
{
    return Impl->Sources;
}
const TArray<FAC2ScenerySound> &FAC2AssetLibrary::SoundSources() const
{
    return Impl->Sounds;
}
bool FAC2AssetLibrary::EffectChannelsMatch(const TMap<uint32,uint32>& Channels)
{
    for(const auto& Channel:EffectChannels)
        if(const auto* Value=Channels.Find(Channel.Key))if(*Value!=Channel.Value)return false;
    return true;
}
TArray<FAC2AmbientPoint> FAC2AssetLibrary::AmbientPoints(bool Landscape) const
{
    TArray<FAC2AmbientPoint> Out;
    if(Landscape)Out=Impl->LandscapeSounds;
    for(const auto& Pair:Impl->InteriorSoundOrigins)
        if(Impl->CellVisible(Pair.Key))Out.Add({2,Pair.Value,false});
    return Out;
}

void FAC2AssetLibrary::UpdateMaterials(float Time)
{
    for (auto &M : Impl->MovingMaterials)
    {
        M.Material->SetScalarParameterValue(TEXT("AC2NativeSpecularPower"),M.SpecularPower.Value(Time));
        for (uint32 Stage = 0; Stage < 4; ++Stage)
        {
        FMatrix UV = FMatrix::Identity;
        for (const auto &Op : M.Modifiers)
        {
            if (Op.Index != Stage)
                continue;
            FMatrix T = FMatrix::Identity;
            const float U = Op.U.Value(Time), V = Op.V.Value(Time);
            if (Op.Kind == 4)
            {
                T.M[3][0] = U;
                T.M[3][1] = V;
            }
            if (Op.Kind == 5)
            {
                // LM_UVRotate converts its authored degree waveform before RotateZ.
                const float Angle = FMath::DegreesToRadians(U);
                T.M[0][0] = T.M[1][1] = FMath::Cos(Angle);
                T.M[0][1] = FMath::Sin(Angle);
                T.M[1][0] = -FMath::Sin(Angle);
            }
            if (Op.Kind == 6)
            {
                T.M[0][0] = U;
                T.M[1][1] = V;
            }
            if (Op.Kind == 7)
                T = Op.Matrix;
            UV = UV * T;
        }
        M.Material->SetVectorParameterValue(*FString::Printf(TEXT("AC2Stage%dRowU"),Stage),
                                            FLinearColor(UV.M[0][0], UV.M[1][0], UV.M[3][0], 0));
        M.Material->SetVectorParameterValue(*FString::Printf(TEXT("AC2Stage%dRowV"),Stage),
                                            FLinearColor(UV.M[0][1], UV.M[1][1], UV.M[3][1], 0));
        if(Stage==0)M.Material->SetVectorParameterValue(TEXT("AC2UVRowU"),
                                            FLinearColor(UV.M[0][0], UV.M[1][0], UV.M[3][0], 0));
        if(Stage==0)M.Material->SetVectorParameterValue(TEXT("AC2UVRowV"),
                                            FLinearColor(UV.M[0][1], UV.M[1][1], UV.M[3][1], 0));
        }
    }
}

void FAC2AssetLibrary::RetainRuntimeMaterials(const TArray<UMaterialInterface *> &Materials)
{
    // Active MIDs retain their texture parameters through normal UObject references.
    // Drop old cache roots so visiting more FX types does not retain retired GPU textures.
    Impl->Images.Empty();
    Impl->Materials.Empty();
    Impl->RuntimeMaterials.Empty();
    Impl->ImageBytes = 0;
    Impl->Objects.RemoveAll(
        [&](const auto &Object) { return !Materials.Contains(Cast<UMaterialInterface>(Object.Get())); });
    Impl->MovingMaterials.RemoveAll([&](const auto &Entry) { return !Materials.Contains(Entry.Material); });
}

void FAC2AssetLibrary::InteriorFog(FAC2SkyKey& Fog) const
{
    Fog.FogType=Impl->InteriorFogType;
    Fog.FogMin=Impl->InteriorFogNear;
    Fog.FogMax=Impl->InteriorFogFar;
    Fog.FogColor=Impl->InteriorFogColor;
}

FAC2PreviewLighting FAC2AssetLibrary::InteriorLighting(uint32 Cell,const FVector& LocalViewer) const
{
    FAC2PreviewLighting Result; Result.Ambient=Impl->InteriorAmbient;
    for(const auto& L:Impl->CellLights) if(!L.Sunlight && L.Vertices.Contains(Cell)) Result.Lights.Add(L);
    Result.Lights.Sort([&](const FAC2PreviewLight& A,const FAC2PreviewLight& B){return FVector::DistSquared(A.Position,LocalViewer)<FVector::DistSquared(B.Position,LocalViewer);});
    if(Result.Lights.Num()>8)Result.Lights.SetNum(8);
    return Result;
}

bool FAC2AssetLibrary::LightingCellAt(const FVector& Position,uint32& Cell) const
{
    if(!InteriorAt(Position))return false;
    if(InteriorCellAt(Position,Cell))return true;
    double Smallest=DBL_MAX;
    for(const auto& B:Impl->InteriorBounds)
        if(Impl->CellVisible(B.Key) && B.Value.IsInsideOrOn(Position) && B.Value.GetVolume()<Smallest)
        { Cell=B.Key; Smallest=B.Value.GetVolume(); }
    return Smallest<DBL_MAX;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AttachmentStreakTest,"AC2.Data.AttachmentStreaks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2AttachmentStreakTest::RunTest(const FString&)
{
    const FString Directory=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2CharacterCatalog Catalog;
    FString Error;
    if (!TestTrue(TEXT("Native avatar catalog"), Catalog.Open(Directory,Error) && !Catalog.Avatars.IsEmpty())) return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto MakeModelRoot = [&](){
        auto* Actor=World->SpawnActor<AActor>();
        auto* Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        return Root;
    };
    {
        FAC2AssetLibrary Holder, Weapon;
        auto* Body=MakeModelRoot();
        auto* Grip=MakeModelRoot();
        const auto& Avatar=Catalog.Avatars[0];
        const bool Loaded=Holder.Open(Directory,Error) && Holder.DrawModel(Avatar.Physics,Body->GetOwner(),Body,Avatar.Resolve(Avatar.Defaults()))
            && Weapon.Open(Directory,Error) && Weapon.DrawModel(0x1f000056,Grip->GetOwner(),Grip);
        if (TestTrue(TEXT("Native avatar and native blade render"),Loaded)
            && TestEqual(TEXT("Blade startup installs one dormant ribbon"),Weapon.Impl->Streaks.Num(),1))
        {
            const auto& Trail = Weapon.Impl->Streaks[0];
            const auto& D = Trail.Definition;
            TestFalse(TEXT("Blade motion alone cannot start the ribbon"),Trail.Active || Trail.Mesh.IsValid());
            TestEqual(TEXT("Native material"),D.Material,0x31000200u);
            AddInfo(FString::Printf(TEXT("Blade edges %s to %s threshold %.3f life %.3f duration %.3f"),
                *D.Edge0.ToString(), *D.Edge1.ToString(), D.MinDistance, D.Life, D.Duration));
            TestTrue(TEXT("Appearance-overridden blade edges, threshold and lifetime"),
                D.Edge0.Equals(FVector(0,10,0),.001) && D.Edge1.Equals(FVector(-20,80,0),.001)
                && FMath::IsNearlyEqual(D.MinDistance,10.f) && D.Duration>0 && D.Life==0);
            const auto Material=Weapon.Impl->Material(D.Material);
            if (Material && Material->Layers.Num())
            {
                const auto& L=Material->Layers[0];
                AddInfo(FString::Printf(TEXT("Native blade material %08X blend %u/%u vertex=%d modulate=%d cull=%u"),
                    D.Material,L.SourceBlend,L.DestBlend,L.VertexDiffuse,L.ModulateVertexDiffuse,L.CullMode));
            }
            constexpr uint32 Start=0x810002ae,Stop=0x810002af;
            TestTrue(TEXT("Native shared avatar start cue dispatches"),Holder.PlayEffect(Start));
            TestTrue(TEXT("Only the authored holding location activates"),Holder.HoldingStreakActive(1) && !Holder.HoldingStreakActive(2));
            Weapon.SetHeldStreakActive(Holder.HoldingStreakActive(1));
            Grip->SetWorldScale3D(FVector(1,1.5,1));
            Weapon.Animate(.01f,0);
            Grip->SetWorldLocation(FVector(5,0,0));
            Weapon.Animate(.02f,0);
            if (TestTrue(TEXT("Moving live tip generates geometry"),Trail.Mesh.IsValid() && Trail.Mesh->GetNumSections()==1))
            {
                const auto* S=Trail.Mesh->GetProcMeshSection(0);
                TestEqual(TEXT("Sub-threshold motion updates a single quad"),S->ProcVertexBuffer.Num(),4);
                const auto Base = S->ProcVertexBuffer[0].Position;
                TestTrue(TEXT("Scaled authored edges span the weapon"),FMath::IsNearlyEqual(
                    FVector::Distance(Base,S->ProcVertexBuffer[1].Position),
                    FVector::Distance(D.Edge0,D.Edge1)*1.5*Weapon.Impl->StreakModelScale.GetAbsMax(),.01));
                Grip->SetWorldLocation(FVector(40,0,0));
                Weapon.Animate(.04f,0);
                S=Trail.Mesh->GetProcMeshSection(0);
                TestEqual(TEXT("Threshold commits a new world-space segment"),S->ProcVertexBuffer.Num(),6);
                TestTrue(TEXT("Previous vertex remains at its world anchor"),Trail.Mesh->GetComponentLocation().IsNearlyZero()
                    && Base.Equals(S->ProcVertexBuffer[0].Position));
                TestTrue(TEXT("Texture U advances by travelled metres"),FMath::IsNearlyEqual(S->ProcVertexBuffer.Last().UV0.X,.4,.001));
                TestTrue(TEXT("Native stop cue dispatches"),Holder.PlayEffect(Stop));
                TestFalse(TEXT("Stop clears its holding"),Holder.HoldingStreakActive(1));
                Weapon.SetHeldStreakActive(Holder.HoldingStreakActive(1));
                Grip->SetWorldLocation(FVector(500,0,0));
                Weapon.Animate(.04f + D.Duration*.5f,0);
                S=Trail.Mesh->GetProcMeshSection(0);
                TestTrue(TEXT("Deactivation fades existing samples quadratically"),S && FMath::Abs(
                    int32(S->ProcVertexBuffer.Last().Color.A)-FMath::RoundToInt(D.Color.A*.25))<=1);
                TestTrue(TEXT("Stopped blade creates no more path"),S && FMath::IsNearlyEqual(S->ProcVertexBuffer.Last().UV0.X,.4,.001));
                Weapon.Animate(D.Duration+.2f,0);
                TestEqual(TEXT("Expired ribbon removes its triangles"),Trail.Mesh->GetNumSections(),0);
            }
            Holder.PlayEffect(Start);Holder.StopEffect(Start);
            TestFalse(TEXT("Explicit cancellation releases its holding"),Holder.HoldingStreakActive(1));
            // This mode selects the shared basic attack's blade-trail clip (05000A01).
            constexpr uint32 BladeMode=0x4100000e;
            if (TestTrue(TEXT("Avatar can play its authored blade swing"),Holder.PlayBehavior(AC2Motion::Attack,BladeMode)))
            {
                for (int32 Frame=1; Frame<=180 && !Holder.HoldingStreakActive(1); ++Frame)
                    Holder.Animate(Frame/60.f,0);
                TestTrue(TEXT("Native attack frame activates the wielded blade"),Holder.HoldingStreakActive(1));
                Holder.StopBehavior(AC2Motion::Attack);
                TestFalse(TEXT("Interrupting the swing releases its wielded blade"),Holder.HoldingStreakActive(1));
            }
            FAC2AssetLibrary DoubleBlade;
            auto* DoubleGrip=MakeModelRoot();
            TestTrue(TEXT("Double blade imports both independent native startup ribbons"),
                DoubleBlade.Open(Directory,Error) && DoubleBlade.DrawModel(0x1f0001fb,DoubleGrip->GetOwner(),DoubleGrip)
                    && DoubleBlade.Impl->Streaks.Num()==2);
        }
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AppearanceRampTest,"AC2.Data.AppearanceEffectRamps",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AppearanceRampTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);
    Actor->SetRootComponent(Root);Root->RegisterComponent();
    {
        FAC2AssetLibrary Model;FString Error;
        if(TestTrue(TEXT("Installed model for appearance ramp"),Model.Open(AC2::ResolveDatDirectory(TEXT("")),Error) &&
            Model.DrawModel(0x1f000026,Actor,Root)))
        {
            // Synthetic appearance atoms exercise a recovered record type
            // absent from both installed datasets, on actual rendered sections.
            constexpr uint32 FX=0x81fffffe,TableId=0x28fffffe,Key=1;
            auto Table=MakeShared<FAC2AppearanceTable>();FAC2AtomCollection Collection;
            Collection.Atoms.Opacities.Add({10000,-999});Table->Collections.Add(Key,{Collection});
            Model.Impl->AppearanceTables.Add(TableId,Table);
            FAC2FXNode Node;Node.AppearanceTable=TableId;Node.AppearanceKey=Key;Node.RampTime=2;
            Node.Light.Enabled=true;
            Model.Impl->AppearanceEffects.Add(FX,{Node});
            auto Opacity=[&](float Expected){
                TestTrue(TEXT("Model has bound material sections"),!Model.Impl->AppearanceBindings.IsEmpty());
                for(const auto& B:Model.Impl->AppearanceBindings)if(B.Mesh.IsValid())
                {
                    float Value=-1;auto* MID=Cast<UMaterialInstanceDynamic>(B.Mesh->GetMaterial(B.Section));
                    TestTrue(TEXT("Ramped scalar reaches material opacity"),MID &&
                        MID->GetScalarParameterValue(TEXT("AC2AppearanceOpacity"),Value) &&
                        FMath::IsNearlyEqual(Value,Expected,.001f));
                }
            };
            TestTrue(TEXT("Ramped appearance starts"),Model.PlayEffect(FX,.8f));
            TestFalse(TEXT("Zero initial scalar removes its appearance key"),Model.Impl->LiveAppearanceFX.Contains(FX));
            Opacity(1);
            Model.Animate(.2f,0);Opacity(.1f);
            TestTrue(TEXT("Active scalar can be retargeted"),Model.PlayEffect(FX,.3f));Opacity(.1f);
            TestEqual(TEXT("Retarget does not duplicate a child light"),Model.Impl->LightEffects.Num(),1);
            Model.Animate(.4f,0);Opacity(.2f);
            Model.Animate(.6f,0);Opacity(.3f);
            const int32 CachedMaterials=Model.Impl->AppearanceFXMaterials.Num();
            TestTrue(TEXT("Zero desired scalar ramps down"),Model.PlayEffect(FX,0));Opacity(.3f);
            Model.Animate(.8f,0);Opacity(.2f);
            Model.Animate(1.f,0);Opacity(.1f);
            Model.Animate(1.2f,0);Model.Animate(1.4f,0);Opacity(1);
            TestFalse(TEXT("Completed ramp removes its key and restores base appearance"),Model.Impl->LiveAppearanceFX.Contains(FX));
            TestEqual(TEXT("Ramping does not allocate a material for each frame"),Model.Impl->AppearanceFXMaterials.Num(),CachedMaterials);
            Model.StopEffect(FX);
            TestFalse(TEXT("Explicit stop removes scalar state"),Model.Impl->ScalarEffects.Contains(FX));
            TestTrue(TEXT("Explicit stop releases child lights"),Model.Impl->LightEffects.IsEmpty());
            Node.RampTime=0;Model.Impl->AppearanceEffects.Add(FX,{Node});
            Model.PlayEffect(FX,.4f);Opacity(.4f);
            Model.PlayEffect(FX,0);Opacity(1);
            Model.StopEffect(FX);
            // Startup keys are protected in native DoTargettedFX/StopFX.
            Model.PlayEffect(FX,.6f);Model.Impl->StartupEffects.Add(FX,{Node});
            TestFalse(TEXT("Updates cannot replace startup FX"),Model.PlayEffect(FX,.1f));
            Model.StopEffect(FX);Opacity(.6f);
            TestTrue(TEXT("Ordinary stop cannot cancel a startup key"),Model.Impl->ScalarEffects.Contains(FX));
            // A native looping particle key has the same update-in-place
            // behavior even when it carries no sound or appearance atom.
            TArray<FAC2DatEntry> Entries;Model.Impl->Portal.Enumerate(Entries,Error);
            uint32 LoopId=0;
            for(const auto& Entry:Entries)if(Entry.Id>>24==0x39)
            {
                TArray<uint8> Bytes;FAC2ParticlePreset Preset;
                if(Model.Impl->Portal.Read(Entry.Id,Bytes,Error) && Preset.Decode(Entry.Id,Bytes) && Preset.ApproximateLifetime()<0)
                {LoopId=Entry.Id;break;}
            }
            if(TestTrue(TEXT("Installed profile supplies a looping particle descriptor"),LoopId!=0))
            {
                constexpr uint32 ParticleFX=0x81fffffd;
                FAC2FXNode ParticleNode;ParticleNode.Descriptors.Add(LoopId);ParticleNode.RampTime=1;
                Model.Impl->AppearanceEffects.Add(ParticleFX,{ParticleNode});
                TestTrue(TEXT("Looping particle-only effect starts"),Model.PlayEffect(ParticleFX,.5f));
                const int32 Children=Model.Impl->NetworkEffects.Num();
                TestTrue(TEXT("Looping key is retained for updates"),Model.Impl->ScalarEffects.Contains(ParticleFX));
                Model.Animate(1.6f,0);
                TestTrue(TEXT("Looping effect accepts a new scalar"),Model.PlayEffect(ParticleFX,.8f));
                TestEqual(TEXT("Update does not duplicate looping emitters"),Model.Impl->NetworkEffects.Num(),Children);
                Model.StopEffect(ParticleFX);
                TestFalse(TEXT("Explicit stop clears particle key"),Model.Impl->HasLoopingParticles(ParticleFX));
            }
            // Both installed FX tables include nodes consisting solely of a
            // repulsor. The moving field must survive filtering and model dispatch.
            constexpr uint32 FieldTable = 0x1e000057;
            TArray<uint8> FieldBytes;
            if (TestTrue(TEXT("Installed repulsor effect table"), Model.Impl->Read(FieldTable, FieldBytes)))
            {
                FAC2AssetReader FieldReader(MoveTemp(FieldBytes));
                TestEqual(TEXT("Repulsor table record ID"), FieldReader.Word(), FieldTable);
                const auto FieldMap = Model.Impl->FXMap(FieldReader);
                TestTrue(TEXT("Repulsor table decoded"), FieldReader.Good);
                int32 Fields = 0;
                bool Started = false;
                for (const auto& Entry : FieldMap)
                    for (const auto& FieldNode : Entry.Value)
                    {
                        const auto& D = FieldNode.Repulsor;
                        if (!D.Enabled) continue;
                        ++Fields;
                        if (D.MinimumVelocity <= 0) continue;
                        TestTrue(TEXT("Moving field preserves authored thresholds"),
                            D.Acceleration == 3 && D.Range == 3.5f && D.Lifetime == -1 &&
                            !D.Attractor && FMath::IsNearlyEqual(D.MinimumVelocity, .1f));
                        // The avatar can already own this field through a startup
                        // key. Targeted replay of a startup key is ignored by retail.
                        if (Model.Impl->StartupEffects.Contains(Entry.Key))
                            TestTrue(TEXT("Avatar startup created its native moving field"),
                                Model.Impl->Repulsors.ContainsByPredicate([&](const auto& Field) {
                                    return Field->Data.MinimumVelocity == D.MinimumVelocity && Field->Data.Range == D.Range;
                                }));
                        constexpr uint32 Trigger = 0x81fffffc;
                        Model.Impl->AppearanceEffects.Add(Trigger, {FieldNode});
                        const int32 Before = Model.Impl->Repulsors.Num();
                        Started = Model.PlayEffect(Trigger, 1) && Model.Impl->Repulsors.Num() == Before + 1;
                        if (Started)
                        {
                            Model.Animate(2.f, 110);
                            const auto& Field = Model.Impl->Repulsors.Last();
                            TestTrue(TEXT("Model animation updates the owner velocity in meters per second"),
                                FMath::IsNearlyEqual(Field->Speed, 1.1f));
                        }
                    }
                TestEqual(TEXT("Six authored repulsor-only nodes in the native FX table"), Fields, 6);
                TestTrue(TEXT("Movement-driven model effect registers its field"), Started);
            }
        }
    }
    Actor->Destroy();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
