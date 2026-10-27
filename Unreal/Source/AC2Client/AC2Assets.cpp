#include "AC2Assets.h"
#include "AC2Animation.h"
#include "AC2Effects.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/PlayerController.h"
#include "AC2AssetReader.h"
#include "AC2MaterialAlpha.h"
#include "AC2Scenery.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "ProceduralMeshComponent.h"

namespace
{
struct FAC2DecodedImage
{
    uint32 Id = 0, SurfaceId = 0;
    bool HighResolution = false;
    int32 W = 0, H = 0;
    TArray<FColor> Pixels;
    TArray<TArray<FColor>> Mips;
    UTexture2D *Texture = nullptr;
    FLinearColor Sample(double U, double V) const
    {
        if (Pixels.IsEmpty())
            return FLinearColor(0.5, 0.5, 0.5);
        const double X = (U - FMath::FloorToDouble(U)) * W - 0.5, Y = (V - FMath::FloorToDouble(V)) * H - 0.5;
        const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
        auto P = [&](int32 A, int32 B) {
            return FLinearColor(Pixels[((B % H + H) % H) * W + (A % W + W) % W]);
        };
        return FMath::Lerp(FMath::Lerp(P(IX, IY), P(IX + 1, IY), float(X - IX)),
                           FMath::Lerp(P(IX, IY + 1), P(IX + 1, IY + 1), float(X - IX)), float(Y - IY));
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
    uint32 SourceBlend = 0, DestBlend = 0;
    TArray<uint32> Textures;
    TArray<FAC2AlphaStage> AlphaStages;
    TArray<FAC2UVModifier> Modifiers;
    FLinearColor Tint = FLinearColor::White;
    bool Masked = false;
    bool MaterialDiffuse = false, VertexDiffuse = false, ModulateVertexDiffuse = false;
};
struct FAC2DecodedMaterial
{
    TArray<FAC2DecodedLayer> Layers;
    TArray<FAC2DecodedProperty> Properties;
};
struct FAC2DecodedEntity
{
    uint32 Data = 0, Type = 0;
    FVector Origin = FVector::ZeroVector, Scale = FVector::OneVector;
    FQuat Rotation = FQuat::Identity;
};
struct FAC2FXNode
{
    TMap<uint32, TSharedPtr<FAC2FXNode>> Terrain;
    TArray<uint32> Descriptors;
    uint32 Holding = 0, Bone = 0;
    uint32 Visual=0,Behavior=0,TargetFX=0,Script=0;
    float Lifetime=0;
    bool Parent=false,Retarget=false,FireAndForget=false;
    FVector Offset = FVector::ZeroVector;
    bool Water = false, RemoveRotation = false;
};
struct FAC2DecodedPart
{
    uint32 Setup = 0, Key = 0, Parent = 0, Fields = 0;
    uint32 Connection = 0, AnimMap = 0, FXTable = 0;
    FAC2Appearance Appearance;
    TArray<uint32> Startup;
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
    uint32 Parent = 0, MotionDesc = 0, BehaviorTable = 0;
    FVector Scale = FVector::OneVector;
    TArray<FAC2DecodedPart> Parts;
    FAC2DecodedPart FX;
    float ParticleScale = 1;
};
struct FAC2DecodedFragment
{
    uint32 Material = 0;
    TArray<int32> Indices;
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
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FAC2DecodedFragment> Fragments;
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
    FAC2Appearance CharacterAppearance;
    TMap<uint32, TSharedPtr<FAC2AppearanceTable>> AppearanceTables;
    uint32 NextMaterialVariant = 0xfe000000, AppearanceHash = 0;

    struct FDraw
    {
        TSharedPtr<FAC2DecodedGeometry> Mesh;
        FTransform Transform;
        bool Solid = false;
        bool Frill = false;
        int32 AnimationGroup = -1, Bone = 0;
        bool Structural = false;
    };
    TArray<FDraw> Draws;
    TArray<TWeakObjectPtr<UProceduralMeshComponent>> CollisionMeshes;
    FAC2DecodedImage TerrainAtlas;
    int32 NextDraw = 0;
    bool Preparing = false, SkyMode = false;
    bool Animating = false, Decoration = false;
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
        TArray<FMatrix> BindInverse, World, Bones;
        TArray<FAnimatedMesh> Meshes;
        TSharedPtr<FAC2Animation> Idle;
        TSharedPtr<FAC2Animator> Locomotion;
        TMap<uint32,TSharedPtr<FAC2Animator>> Graphs;
        uint32 Map=0;
        FAC2AnimatorInput Motion;
        float Phase = 0, MoveWeight = 0;
        TArray<FTransform> PreviousMotionPose;
        int32 Parent = -1;
        FAC2Setup::FSocket Socket;
    };
    TArray<FAnimatedGroup> Animated;
    FAC2MotionData Movement;
    FAC2BehaviorData Behaviors;
    uint32 Element=AC2Motion::Ground, Mode=AC2Motion::Peace, Action=0, ActionKey=0;
    float ActionTime=0, ActionRate=1, ActionDuration=0;
    bool HoldAction=false;
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
    struct FScriptEffect {FAC2FXScript Script;float Phase=0;int32 Depth=0;};
    TArray<FScriptEffect> ScriptEffects;
    int32 EffectDepth=0;
    uint64 EffectSerial=0x8000000000000000ULL;
    uint32 ModelVisual=0;
    TMap<uint32, TArray<FAC2FXNode>> AppearanceEffects;
    TUniquePtr<FAC2Effects> AttachedEffects;
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
    uint32 DefaultSurface = 0;
    TMap<uint32, TArray<FAC2DecodedBand>> Surfaces;
    TMap<uint32, TSharedPtr<FAC2DecodedImage>> Images;
    TMap<uint32, TSharedPtr<FAC2DecodedMaterial>> Materials;
    TMap<uint32, TSharedPtr<FAC2DecodedGeometry>> Geometry;
    TMap<uint32, FAC2DecodedVisual> Visuals;
    TArray<FAC2ParticleSource> Sources;
    struct FMovingMaterial
    {
        UMaterialInstanceDynamic *Material;
        TArray<FAC2UVModifier> Modifiers;
    };
    TArray<FMovingMaterial> MovingMaterials;
    void TrackMaterial(uint32 Id, UMaterialInterface *Material)
    {
        auto Decoded = this->Material(Id);
        auto *MID = Cast<UMaterialInstanceDynamic>(Material);
        if (MID && Decoded && !Decoded->Layers.IsEmpty() && !Decoded->Layers[0].Modifiers.IsEmpty())
            MovingMaterials.Add({MID, Decoded->Layers[0].Modifiers});
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
    uint32 EnvironmentTerrainMask = 0;
    TMap<uint32, TArray<FVector>> InteriorPositions;
    TArray<FBox> InteriorBounds;
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
        S.Take(28);
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
            if (!P)
                continue;
            for (const FAC2DecodedField &F : P->Fields)
            {
                if (!M->Layers.IsValidIndex(F.Layer))
                    continue;
                FAC2DecodedLayer &L = M->Layers[F.Layer];
                FAC2AssetReader V(O.Data);
                if (F.Kind == 10 && L.Textures.IsValidIndex(F.Index) && V.Left() >= 4)
                    L.Textures[F.Index] = V.Word();
                if (F.Kind == 1 && V.Left() >= 16)
                    L.Tint = Color(V);
                if (F.Kind == 8 && V.Left() >= 4)
                    L.Masked = V.Word() != 0;
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
            R.Take(8);
            const int32 NS = R.Count(56, 16);
            for (int32 J = 0; J < NS; ++J)
            {
                L.Textures.Add(R.Word());
                R.Take(12); // colour operation and arguments
                FAC2AlphaStage Alpha;
                Alpha.Operation = R.Word();
                Alpha.Argument1 = R.Word();
                Alpha.Argument2 = R.Word();
                L.AlphaStages.Add(Alpha);
                R.Take(28);
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
            R.Take(12);
            const uint32 AlphaMode = R.Word();
            L.Masked = AlphaMode != 0 && AlphaMode != 8;
            R.Take(44 + 28);
            L.MaterialDiffuse = R.Word() != 0;
            L.VertexDiffuse = R.Word() != 0;
            L.ModulateVertexDiffuse = R.Word() != 0;
            R.Take(16 + 16);
            L.Tint = Color(R);
            R.Take(16 + 44 + 20);
            int32 Shader = R.Count(1, 1024 * 1024);
            R.Take(Shader);
            Shader = R.Count(1, 1024 * 1024);
            R.Take(Shader);
            M->Layers.Add(MoveTemp(L));
        }
        R.Word();
        if (!R.End())
        {
            Warn(Id, TEXT("unsupported material layout"));
            return nullptr;
        }
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
        auto Best = MakeShared<FAC2DecodedImage>();
        Best->Id = Id;
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
                if (int64(W) * H > int64(Best->W) * Best->H)
                {
                    Best->W = W;
                    Best->H = H;
                    Best->Pixels = MoveTemp(Pixels);
                    Best->SurfaceId = Surface;
                    Best->HighResolution = Pass == 0;
                }
            }
        }
        if (Best->Pixels.IsEmpty())
            return nullptr;
        // Bound patch memory even when a record requests an unusually large texture.
        while (Best->W > 2048 || Best->H > 2048)
            Downsample(*Best);
        const int64 Bytes = Best->Pixels.Num() * sizeof(FColor);
        if (ImageBytes + Bytes > 512LL * 1024 * 1024)
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

    static void BuildMips(FAC2DecodedImage &Image)
    {
        if (!Image.Mips.IsEmpty())
            return;
        FAC2DecodedImage M;
        M.W = Image.W;
        M.H = Image.H;
        M.Pixels = Image.Pixels;
        while (M.W > 1 || M.H > 1)
        {
            Downsample(M);
            Image.Mips.Add(M.Pixels);
        }
    }
    UTexture2D *Texture(FAC2DecodedImage &Image, bool Clamp = false)
    {
        if (Image.Texture)
            return Image.Texture;
        check(IsInGameThread());
        // Weak entries don't keep distant blocks' GPU resources alive.
        static TMap<FString, TWeakObjectPtr<UTexture2D>> SharedTextures;
        const FString CacheKey =
            FString::Printf(TEXT("%s:%08X:%08X:%dx%d:%d:%d"), *TextureNamespace, Image.Id, Image.SurfaceId,
                            Image.W, Image.H, Image.HighResolution, Clamp);
        if (Image.Id)
            if (auto *Cached = SharedTextures.Find(CacheKey))
                if (auto *T = Cached->Get())
                {
                    Objects.Add(T);
                    Image.Texture = T;
                    return T;
                }
        UTexture2D *T = UTexture2D::CreateTransient(Image.W, Image.H, PF_B8G8R8A8);
        if (!T)
            return nullptr;
        T->SRGB = true;
        T->NeverStream = true;
        T->AddressX = Clamp ? TA_Clamp : TA_Wrap;
        T->AddressY = T->AddressX;
        T->Filter = TF_Default; // texture group supplies anisotropic filtering
        auto *Platform = T->GetPlatformData();
        BuildMips(Image);
        int32 W = Image.W, H = Image.H;
        for (int32 Level = 0; Level <= Image.Mips.Num(); ++Level)
        {
            const auto &Pixels = Level ? Image.Mips[Level - 1] : Image.Pixels;
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
        Image.Texture = T;
        if (Image.Id)
            SharedTextures.Add(CacheKey, T);
        return T;
    }

    UMaterialInterface *Instance(FAC2DecodedImage *Image, const FLinearColor &Tint, bool Masked)
    {
        UMaterialInterface *Parent = Masked ? Cutout : Opaque;
        if (!Parent)
            return nullptr;
        auto *M = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
        Objects.Add(M);
        if (Image)
            M->SetTextureParameterValue(TEXT("Albedo"), Texture(*Image));
        M->SetVectorParameterValue(TEXT("Tint"), Tint);
        return M;
    }
    TSharedPtr<FAC2DecodedImage> MaterialImage(uint32 Id, FLinearColor &Tint, bool &Masked)
    {
        if ((Id >> 24) == 0x40 || (Id >> 24) == 0x41)
            return Image(Id);
        auto M = Material(Id);
        if (!M)
            return nullptr;
        if (!M->Layers.IsEmpty())
        {
            Tint = M->Layers[0].Tint;
            Masked = M->Layers[0].Masked;
        }
        for (const FAC2DecodedLayer &L : M->Layers)
            for (uint32 TextureId : L.Textures)
                if ((TextureId >> 24) == 0x40 || (TextureId >> 24) == 0x41)
                {
                    Tint = L.Tint;
                    Masked = L.Masked;
                    return Image(TextureId);
                }
        return nullptr;
    }
    UMaterialInterface *RuntimeMaterial(uint32 Id, bool Grass = false)
    {
        const uint64 CacheKey = uint64(Id) | (uint64(Grass) << 32);
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
        const auto *Layer = Native && !Native->Layers.IsEmpty() ? &Native->Layers[0] : nullptr;
        const float DiffuseAlpha = Layer && Layer->MaterialDiffuse ? Tint.A : 1.f;
        auto Alpha = [&](float A) {
            return Layer ? EvaluateAC2Alpha(Layer->AlphaStages, A, DiffuseAlpha, Tint.A) : 1.f;
        };
        uint8 MinAlpha = 255, MaxAlpha = 255;
        if (I)
        {
            MaxAlpha = 0;
            for (const FColor &P : I->Pixels)
            {
                MinAlpha = FMath::Min(MinAlpha, P.A);
                MaxAlpha = FMath::Max(MaxAlpha, P.A);
            }
        }
        // Untextured red/white cell helper materials have diffuse alpha zero.
        // The alpha combiner applies to constant colour as well as texture pixels.
        const bool HasAlpha = FMath::Min(Alpha(MinAlpha / 255.f), Alpha(MaxAlpha / 255.f)) < .98f;
        if (Layer && (Masked || Layer->DestBlend > 2) &&
            FMath::Max(Alpha(MinAlpha / 255.f), Alpha(MaxAlpha / 255.f)) <= SMALL_NUMBER)
            InvisibleMaterials.Add(Id);
        if (!HasAlpha)
            Masked = false;
        UMaterialInterface *Result = nullptr;
        if (Grass)
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
                    MID->SetTextureParameterValue(TEXT("Albedo"), Texture(*I));
                MID->SetVectorParameterValue(TEXT("Tint"), Tint);
                Result = MID;
            }
        }
        else if (SkyMode)
        {
            auto Authored = Material(Id);
            const uint32 Dest = Authored && Authored->Layers.Num() ? Authored->Layers[0].DestBlend : 1;
            if (Authored && Authored->Layers.Num())
            {
                const auto &L = Authored->Layers[0];
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
            uint32 Dest = Authored && !Authored->Layers.IsEmpty() ? Authored->Layers[0].DestBlend : 1;
            if (!Masked && (Dest == 2 || (Dest > 2 && HasAlpha)))
            {
                const TCHAR *Path = Dest == 2
                                        ? TEXT("/Game/AC2/Materials/M_AC2SceneryAdd.M_AC2SceneryAdd")
                                        : TEXT("/Game/AC2/Materials/M_AC2SceneryAlpha.M_AC2SceneryAlpha");
                auto *Parent = LoadObject<UMaterialInterface>(nullptr, Path);
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
                Result = Instance(I.Get(), Tint, Masked);
        }
        if (auto *MID = Cast<UMaterialInstanceDynamic>(Result))
            MID->SetVectorParameterValue(TEXT("AC2Opacity"),
                                         FLinearColor(Alpha(1) - Alpha(0), Alpha(0), 0, 0));
        TrackMaterial(Id, Result);
        RuntimeMaterials.Add(CacheKey, Result);
        return Result;
    }

    void SkipProperties(FAC2AssetReader &R)
    {
        const int32 NG = R.Count(8);
        for (int32 I = 0; I < NG && R.Good; ++I)
        {
            R.Word();
            const int32 NP = R.Count();
            for (int32 J = 0; J < NP && R.Good; ++J)
            {
                const uint32 Name = R.Word();
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
            SkipProperties(R);
        if (Bits & 0x40)
            R.Word();
        if (Bits & 0x100)
            R.String();
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
                R.Take(int64(N) * 4);
            }
        if (Bits & 2)
        {
            int32 N = R.Count(4, 4096);
            for (int32 I = 0; I < N; ++I)
                Node.Descriptors.Add(R.Word());
        }
        Field(4);
        Field(8);
        Field(16, 8);
        Field(32);
        Field(64);
        if (Bits & 128)
            Node.Holding = R.Word();
        if (Bits & 256)
            Node.Bone = R.Word();
        if (Bits & 512)
            Node.Offset = R.Vector();
        Field(1024, 56);
        Field(0x800);
        Field(0x1000);
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
        for (uint64 Bit : {0x2000000ULL, 0x4000000ULL})
            Field(Bit);
        if (Bits & 0x8000000)
        {
            const uint32 N = R.Word() & 65535;
            R.Take(int64(N) * 8);
        }
        if(Bits&0x10000000)Node.FireAndForget=R.Word()!=0;
        if (Bits & 0x20000000)
            Node.Water = R.Word() != 0;
        Field(0x40000000);
        Field(0x80000000, 160);
        Field(0x200000000ULL, 24);
        if (Bits & 0x100000000ULL)
        {
            R.Word();
            Entity(R);
            R.Take(8);
        }
        Field(0x400000000ULL);
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
                if (!Node.Descriptors.IsEmpty() || !Node.Terrain.IsEmpty() || Node.Visual || Node.TargetFX || Node.Script)
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
                P.Startup.Add(R.Word());
                R.Float();
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
                R.Take(int64(K) * 8); // Opacity atoms remain unsupported.
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
                R.Take(int64(K) * 12);
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
        if ((Id >> 24) != 0x31 || Atoms.Materials.IsEmpty())
            return Id;
        TArray<uint8> B;
        if (!Read(Id, B))
            return Id;
        FAC2AssetReader R(MoveTemp(B));
        R.Word();
        uint32 Template = R.Word(), MaterialType = R.Word();
        TArray<uint32> Mods;
        int32 N = R.Count();
        for (int32 I = 0; I < N; ++I)
            Mods.Add(R.Word());
        if (!R.End())
            return Id;
        bool Changed = false;
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
            F.Material = AppearanceMaterial(F.Material, Type, Atoms);
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
            R.Take(int64(N) * 12);
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
                V.FX.Startup.AddUnique(Key);
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
                    Child->Startup = P.Startup;
                if (!(Child->Fields & 256))
                    Child->Overrides = P.Overrides;
            }
        }
        Visuals.Add(Id, V);
        return true;
    }
    void StartFX(const FAC2DecodedVisual &V, const FTransform &Frame)
    {
        if (SkyMode || Animating || Sources.Num() >= 256)
            return;
        TArray<FAC2FXNode> Nodes;
        for (uint32 Key : V.FX.Startup)
            if (auto *Override = V.FX.Overrides.Find(Key))
                Nodes.Append(*Override);
            else if (V.FX.FXTable)
                TableFX(V.FX.FXTable, Key, Nodes);
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
        for (const auto &Default : Nodes)
        {
            if (Sources.Num() >= 256)
                break;
            const auto *Override = Default.Terrain.Find(Terrain);
            const auto &Node = Override ? **Override : Default;
            if (Node.Descriptors.IsEmpty())
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
            Sources.Add(MoveTemp(Source));
        }
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
            const FVector2D T =
                UV >= 0 ? FVector2D(AC2::F32(P + UV), AC2::F32(P + UV + 4)) : FVector2D::ZeroVector;
            if (T.ContainsNaN())
                return nullptr;
            G->UVs.Add(T);
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
        // The remaining physics payload is deliberately not interpreted by this renderer.
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
        auto *C = NewObject<UProceduralMeshComponent>(Owner);
        Owner->AddInstanceComponent(C);
        C->SetupAttachment(Root);
        C->SetRelativeTransform(Draw.Transform);
        C->bUseAsyncCooking = true;
        C->SetCollisionProfileName(SkyMode || Animating ? TEXT("NoCollision") : TEXT("BlockAll"));
        C->SetCastShadow(!SkyMode);
        if (Draw.Frill)
            C->SetCullDistance(12000); // Shader fades individual pixels at 60-80 m, before batch culling.
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
            auto Authored = SkyMode ? this->Material(F.Material) : nullptr;
            const bool VertexColors =
                Authored && Authored->Layers.Num() &&
                (Authored->Layers[0].VertexDiffuse || Authored->Layers[0].ModulateVertexDiffuse);
            // Leaf cards and translucent surfaces don't become invisible collision walls.
            const bool Collision = Draw.Solid && !SkyMode && !Animating && Material && !F.Indices.IsEmpty() &&
                                   (Draw.Structural || Material->GetBlendMode() == BLEND_Opaque);
            HasCollision |= Collision;
            C->CreateMeshSection(J, G->Vertices, F.Indices, G->Normals, G->UVs,
                                 VertexColors ? G->Colors : TArray<FColor>(), TArray<FProcMeshTangent>(),
                                 Collision);
            C->SetMaterial(J, Material);
            if (InvisibleMaterials.Contains(F.Material))
                C->SetMeshSectionVisible(J, false);
            ++Stats.ScenerySections;
            Stats.SceneryTriangles += F.Indices.Num() / 3;
        }
        if (HasCollision)
            CollisionMeshes.Add(C);
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
            }
        if (Group.Meshes.IsEmpty())
            return false;
        UE_LOG(LogTemp, Display,
               TEXT("AC2 native skeleton %08X: %d bones, %d meshes, idle=%d locomotion=%d map=%08X"), P.Setup,
               Group.Setup.Parents.Num(), Group.Meshes.Num(), !!Group.Idle, !!Group.Locomotion, P.AnimMap);
        Animated.Add(MoveTemp(Group));
        return true;
    }
    bool PrepareMovingSetup(const FAC2DecodedPart &P, const FTransform &Frame)
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
        G.Idle = Idle;
        for (const FMatrix &M : G.Setup.World(G.Setup.Bind))
            G.BindInverse.Add(M.Inverse());
        bool Any = false;
        const int32 Group = Animated.Num();
        for (int32 I = 0; I < G.Setup.Meshes.Num(); ++I)
            if (auto GeometryData = Mesh(G.Setup.Meshes[I]))
            {
                if (Draws.Num() >= 4096)
                    break;
                Draws.Add({GeometryData, Frame, false, false, Group, I});
                for (const auto &F : GeometryData->Fragments)
                {
                    FLinearColor Tint;
                    bool Masked = false;
                    MaterialImage(F.Material, Tint, Masked);
                }
                Any = true;
            }
        if (Any)
            Animated.Add(MoveTemp(G));
        return Any;
    }
    bool DrawSetup(uint32 Id, const FTransform &Placement, AActor *Owner, USceneComponent *Root,
                   bool RecordBounds)
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
        for (int32 I = 0; I < NM; ++I)
        {
            if (!Meshes[I])
                continue;
            auto G = Mesh(Meshes[I]);
            if (!G)
                continue;
            if (Stats.ScenerySections >= 12000)
            {
                Warn(Id, TEXT("patch scenery section limit reached"));
                break;
            }
            const FTransform MeshTransform =
                (World.IsValidIndex(I) ? World[I] : FTransform::Identity) * Placement;
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
        return Drawn;
    }
    // Grass retains its native mesh, transforms and material, but shares a draw
    // within a 20 m cell. Small bounds keep distance culling useful while walking.
    TMap<uint64, int32> FrillBatches;
    void QueueFrill(const TSharedPtr<FAC2DecodedGeometry> &Source, const FTransform &Frame)
    {
        const FVector P = Frame.GetLocation();
        // FrillRender::RefreshFrillMesh replaces every vertex normal with the
        // placement's transformed up axis. Card normals make neighboring blades
        // alternate between bright and dark and can point under the ground.
        const FVector FrillNormal = Frame.GetUnitAxis(EAxis::Z);
        const int32 X = FMath::Clamp(FMath::FloorToInt(P.X / 2000), 0, 7);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(P.Y / 2000), 0, 7);
        for (const auto &Fragment : Source->Fragments)
        {
            const uint64 Key = (uint64(X * 8 + Y) << 32) | Fragment.Material;
            int32 *Existing = FrillBatches.Find(Key);
            int32 Index = Existing ? *Existing : -1;
            if (Index < 0 || Draws[Index].Mesh->Vertices.Num() + Source->Vertices.Num() > 60000)
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
            for (int32 V = 0; V < Source->Vertices.Num(); ++V)
            {
                G.Vertices.Add(Frame.TransformPosition(Source->Vertices[V]) - Draw.Transform.GetLocation());
                G.Normals.Add(FrillNormal);
                G.UVs.Add(Source->UVs[V]);
                G.Colors.Add(Source->Colors[V]);
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
        }
        FTransform Scaled = Placement;
        Scaled.SetScale3D(E.Scale * V.Scale);
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
                    Drawn |=
                        PrepareMovingSetup(P, Frame) || DrawSetup(P.Setup, Frame, Owner, Root, RecordBounds);
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
}
FAC2AssetLibrary::~FAC2AssetLibrary()
{
    for(auto& E:Impl->VisualEffects){E.Model.Reset();if(E.Actor.IsValid())E.Actor->Destroy();}
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
                Out=FTransform(Socket->Frame.ToMatrixWithScale()*G.Bones[Socket->Bone])*G.Frame;
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
const FAC2MotionData& FAC2AssetLibrary::MotionData() const { return Impl->Movement; }
bool FAC2AssetLibrary::SetMotionDescriptor(uint32 Id)
{
    TArray<uint8> Data;FAC2MotionData Motion;
    if((Id>>24)!=0x32||!Impl->Read(Id,Data)||!Motion.Decode(Id,Data))return false;
    Impl->Movement=MoveTemp(Motion);return true;
}
void FAC2AssetLibrary::SetAnimationState(uint32 Element,uint32 Mode)
{
    Impl->Element=Element;Impl->Mode=Mode;
}
bool FAC2AssetLibrary::PlayBehavior(uint32 Behavior,uint32 Mode,float Rate,bool Hold)
{
    if(Mode)Impl->Mode=Mode;
    uint32 Key=Impl->Behaviors.Key(Impl->Element,Impl->Mode,Behavior);
    if(!Key)Key=Impl->Behaviors.Key(AC2Motion::Ground,Impl->Mode,Behavior);
    if(!Key && (Behavior==AC2Motion::Jump||Behavior==AC2Motion::Land||Behavior==AC2Motion::Fall))
        Key=Impl->Behaviors.Key(AC2Motion::Air,Impl->Mode,Behavior);
    if(!Key)Key=Impl->Behaviors.Key(AC2Motion::Ground,AC2Motion::Peace,Behavior);
    if(!Key)return false;
    float Duration=0;
    for(auto& G:Impl->Animated)
        if(auto Graph=Impl->Graph(G,Key))
        {G.Motion.Mode=Impl->Mode;G.Motion.Element=Impl->Element;Duration=FMath::Max(Duration,Graph->Period(G.Motion));}
    if(Duration<=.001f)return false;
    Impl->Action=Behavior;Impl->ActionKey=Key;Impl->ActionTime=0;Impl->ActionDuration=Duration;
    Impl->ActionRate=FMath::Clamp(Rate,.05f,10.f);Impl->HoldAction=Hold;
    UE_LOG(LogTemp,Verbose,TEXT("AC2 behavior %08X mode=%08X key=%08X duration=%g"),Behavior,Impl->Mode,Key,Duration);
    return true;
}
void FAC2AssetLibrary::StopBehavior(uint32 Behavior)
{
    if(!Behavior||Behavior==Impl->Action){Impl->Action=Impl->ActionKey=0;Impl->HoldAction=false;}
}
bool FAC2AssetLibrary::AuditAttachments(FString& Error) const
{
    for(const auto& G:Impl->Animated)
        if(Impl->Animated.IsValidIndex(G.Parent))
        {
            const auto& P=Impl->Animated[G.Parent];
            if(!P.Bones.IsValidIndex(G.Socket.Bone)){Error=TEXT("Missing attachment bone");return false;}
            const auto Expected=FTransform(G.Socket.Frame.ToMatrixWithScale()*P.Bones[G.Socket.Bone])*P.Frame;
            if(!G.Frame.Equals(Expected,.001)){Error=TEXT("Child detached from animated connection point");return false;}
        }
    return !Impl->Animated.IsEmpty();
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
    TArray<uint32> Keys;
    Impl->AppearanceEffects.GetKeys(Keys);
    Keys.Sort();
    for (uint32 Key : Keys)
        for (const auto &Node : Impl->AppearanceEffects[Key])
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
    if (!Impl->AppearanceAttachments.IsEmpty())
    {
        Impl->AttachedEffects = MakeUnique<FAC2Effects>();
        Impl->AttachedEffects->Open(Owner->GetWorld(), Impl->Directory);
        if (auto *Capture = Owner->FindComponentByClass<USceneCaptureComponent2D>())
            Capture->ShowOnlyActors.Add(Impl->AttachedEffects->RenderActor());
    }
    Animate(0, 0);
    return OK;
}
void FAC2AssetLibrary::Animate(float Time, float Speed, const FVector *LocalViewer,
                               const FVector *LocalVelocity)
{
    TArray<FAC2Animation::FEffect> AnimationEffects;
    const float PreviousActionTime=Impl->ActionTime;
    const float Delta = FMath::Clamp(Time - Impl->PreviousAnimationTime, 0.f, .2f);
    Impl->PreviousAnimationTime = Time;
    Impl->ActionTime+=Delta*Impl->ActionRate;
    const bool ActionFinished=Impl->Action && !Impl->HoldAction && Impl->ActionTime>Impl->ActionDuration;
    for (auto &G : Impl->Animated)
    {
        if (LocalViewer && FVector::DistSquared(G.Frame.GetLocation(), *LocalViewer) > FMath::Square(12000.))
            continue;
        auto Pose = G.Idle ? G.Idle->Sample(Time, G.Setup.Rest) : G.Setup.Rest;
        G.Motion.Element=Impl->Element;G.Motion.Mode=Impl->Mode;G.Motion.Terrain=0x80000001;
        const uint32 IdleKey=Impl->Behaviors.Key(Impl->Element,Impl->Mode,0x40000002);
        if(auto Idle=Impl->Graph(G,IdleKey))
        {
            float Period=Idle->Period(G.Motion);
            if(Period>.001f)Pose=Idle->Sample(Time/Period,G.Motion,G.Setup.Rest);
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
            G.MoveWeight = FMath::FInterpConstantTo(G.MoveWeight, Speed > 10 || Impl->Element==AC2Motion::Air ? 1.f : 0.f, Delta, 7);
            const float Period = MovingGraph->Period(G.Motion);
            if (Period > .001f && G.MoveWeight > 0)
            {
                G.Phase = Impl->Animated.IsValidIndex(G.Parent) ? Impl->Animated[G.Parent].Phase : FMath::Frac(G.Phase + Delta / Period);
                const auto Moving = MovingGraph->Sample(G.Phase, G.Motion, G.Setup.Rest);
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
        if(Impl->Action)
            if(auto ActionGraph=Impl->Graph(G,Impl->ActionKey))
            {
                const float Period=ActionGraph->Period(G.Motion);
                if(Period>.001f)
                {
                    const float Phase=FMath::Min(Impl->ActionTime/Period,.99999f);
                    if(G.Parent<0)AnimationEffects.Append(ActionGraph->EffectsBetween(PreviousActionTime/Period,FMath::Min(Impl->ActionTime/Period,1.f),G.Motion));
                    auto ActionPose=ActionGraph->Sample(Phase,G.Motion,Pose);
                    const float Weight=FMath::Min(1.f,Impl->ActionTime/.08f);
                    for(int32 I=0;I<Pose.Num();++I){FTransform Blend;Blend.Blend(Pose[I],ActionPose[I],Weight);Pose[I]=Blend;}
                }
            }
        G.World = G.Setup.World(Pose);
        // CPartArray::UpdateBones keeps unscaled attachment/rigid matrices separate from skin deformation.
        G.Bones = G.Setup.World(Pose, false);
        if (Impl->Animated.IsValidIndex(G.Parent))
        {
            const auto &Parent = Impl->Animated[G.Parent];
            if (Parent.Bones.IsValidIndex(G.Socket.Bone))
                G.Frame = FTransform(G.Socket.Frame.ToMatrixWithScale() * Parent.Bones[G.Socket.Bone]) *
                          Parent.Frame;
        }
        TArray<FMatrix> Skin;
        for (int32 I = 0; I < G.World.Num(); ++I)
            Skin.Add(G.BindInverse[I] * G.World[I]);
        for (auto &M : G.Meshes)
        {
            const auto &D = *M.Data;
            if (D.Skin.IsEmpty())
            {
                M.Component->SetRelativeTransform(
                    (G.Bones.IsValidIndex(M.Bone) ? FTransform(G.Bones[M.Bone]) : FTransform::Identity) *
                    G.Frame);
                continue;
            }
            TArray<FVector> V, N;
            V.SetNumUninitialized(D.Vertices.Num());
            N.SetNumUninitialized(D.Vertices.Num());
            for (int32 I = 0; I < D.Vertices.Num(); ++I)
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
                        Normal += FVector(Skin[Bone].TransformVector(D.Normals[I])) * W;
                        Total += W;
                    }
                }
                V[I] = Total > 0 ? P / Total : D.Vertices[I];
                N[I] = Total > 0 ? Normal.GetSafeNormal() : D.Normals[I];
            }
            M.Component->SetRelativeTransform(G.Frame);
            for (int32 J = 0; J < D.Fragments.Num(); ++J)
                M.Component->UpdateMeshSection(J, V, N, D.UVs, TArray<FColor>(), TArray<FProcMeshTangent>());
        }
    }
    if(ActionFinished)StopBehavior();
    for(const auto& E:AnimationEffects)
    {
        if(E.Stop)StopEffect(E.Id);
        if(E.Play && FMath::FRand()<=FMath::Clamp(E.Probability,0.f,1.f))PlayEffect(E.Id,FMath::Max(.01f,E.Intensity+FMath::FRandRange(-E.Variation,E.Variation)));
    }
    // Snapshot due events before dispatch: an FX node may schedule another script.
    TArray<TPair<FAC2Animation::FEffect,int32>> ScriptTriggers;
    for(int32 I=Impl->ScriptEffects.Num()-1;I>=0;--I)
    {
        auto& Track=Impl->ScriptEffects[I];const float From=Track.Phase;
        Track.Phase=FMath::Min(1.f,From+Delta/Track.Script.Period);
        if(Track.Phase>From)for(const auto& E:Track.Script.Effects)
            if((E.Phase>From || (From==0 && E.Phase==0)) && E.Phase<=Track.Phase)ScriptTriggers.Add({E,Track.Depth+1});
        if(Track.Phase>=1)Impl->ScriptEffects.RemoveAt(I);
    }
    for(const auto& Trigger:ScriptTriggers)
    {
        const auto& E=Trigger.Key;const int32 Depth=Impl->EffectDepth;Impl->EffectDepth=Trigger.Value;
        if(E.Stop)StopEffect(E.Id);
        if(E.Play && FMath::FRand()<=FMath::Clamp(E.Probability,0.f,1.f))PlayEffect(E.Id,FMath::Max(.01f,E.Intensity+FMath::FRandRange(-E.Variation,E.Variation)));
        Impl->EffectDepth=Depth;
    }
    if (Impl->AttachedEffects && Impl->ModelRoot.IsValid())
    {
        TArray<FAC2ParticleSource> Sources;
        for (const auto &A : Impl->AppearanceAttachments)
        {
            const auto &G = Impl->Animated[A.Group];
            FTransform At(A.Node.Offset * 100);
            if (const auto *Socket = G.Setup.Sockets.Find(A.Node.Holding))
            {
                if (G.Bones.IsValidIndex(Socket->Bone))
                    At = At * FTransform(Socket->Frame.ToMatrixWithScale() * G.Bones[Socket->Bone]);
            }
            else if (A.Node.Bone)
            {
                int32 Bone = G.Setup.Names.IndexOfByKey(A.Node.Bone);
                if (G.Bones.IsValidIndex(Bone))
                    At = At * FTransform(G.Bones[Bone]);
            }
            FAC2ParticleSource S;
            S.Key = A.Key;
            S.Descriptor = A.Descriptor;
            S.Frame = At * G.Frame * Impl->ModelRoot->GetComponentTransform();
            Sources.Add(S);
        }
        Impl->AttachedEffects->SyncSources(Sources);
        for(int32 I=Impl->NetworkEffects.Num()-1;I>=0;--I)
        {
            const auto& A=Impl->NetworkEffects[I].Attachment;
            FTransform At=Impl->EffectFrame(A.Node);
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
        Impl->AttachedEffects->Update(View.GetLocation(), View.GetUnitAxis(EAxis::Y),
                                      View.GetUnitAxis(EAxis::Z), Delta, 0, FVector::ZeroVector, true);
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
bool FAC2AssetLibrary::PlayEffect(uint32 Effect,float Strength,USceneComponent* Target)
{
    if(!Impl->ModelRoot.IsValid() || !FMath::IsFinite(Strength) || Strength<=0 || Impl->NetworkEffects.Num()>=64 || Impl->EffectDepth>=4)return false;
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
    if(!Impl->AttachedEffects){Impl->AttachedEffects=MakeUnique<FAC2Effects>();Impl->AttachedEffects->Open(Impl->ModelRoot->GetWorld(),Impl->Directory);}
    bool Played=false;
    for(const auto& Node:Nodes)
    {
        UE_LOG(LogTemp,Verbose,TEXT("AC2 FX node descriptor=%08X holding=%08X bone=%08X"),Node.Descriptors.IsEmpty()?0:Node.Descriptors[0],Node.Holding,Node.Bone);
        if(Impl->NetworkEffects.Num()>=64)continue;
        if(Node.Script)Played|=PlayEffectScript(Node.Script);
        FTransform Source=Impl->EffectFrame(Node);
        if(Node.RemoveRotation)Source.SetRotation(FQuat::Identity);
        if(Node.Visual && Impl->VisualEffects.Num()<16)
        {
            auto* World=Impl->ModelRoot->GetWorld();auto* Actor=World->SpawnActor<AActor>();
            auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();Actor->SetActorTransform(Source);
            auto Model=MakeShared<FAC2AssetLibrary>();FString Error;
            Model->Impl->EffectDepth=Impl->EffectDepth+1;
            if(Model->Open(Impl->Directory,Error) && Model->DrawModel(Node.Visual,Actor,Root))
            {
                if(Node.Behavior)Model->PlayBehavior(Node.Behavior);
                if(Node.TargetFX)Model->PlayEffect(Node.TargetFX,Strength,Target);
                const float Life=FMath::IsFinite(Node.Lifetime)&&Node.Lifetime>0?FMath::Clamp(Node.Lifetime,.05f,30.f):FMath::Clamp(Model->Impl->ActionDuration,.5f,5.f);
                Impl->VisualEffects.Add({{-1,Node,0,0},Effect,0,Life,Actor,Model,Target});Played=true;
            }
            else Actor->Destroy();
        }
        if(Node.Descriptors.IsEmpty())continue;
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
            FAC2ParticleSource S;S.Key=Key;S.Descriptor=Descriptor;S.Frame=Source;S.ParticleScale=FMath::Clamp(Strength,.01f,10.f)*V.ParticleScale;
            if(Impl->AttachedEffects->NativeBurst(S))
            {Impl->NetworkEffects.Add({{Group,Node,Key,Descriptor},Effect,Target,Source});Played=true;}
        }
    }
    return Played;
}
void FAC2AssetLibrary::StopEffect(uint32 Effect)
{
    for(int32 I=Impl->VisualEffects.Num()-1;I>=0;--I)if(Impl->VisualEffects[I].Effect==Effect)
    {auto& E=Impl->VisualEffects[I];E.Model.Reset();if(E.Actor.IsValid())E.Actor->Destroy();Impl->VisualEffects.RemoveAt(I);}
    for(int32 I=Impl->NetworkEffects.Num()-1;I>=0;--I)if(Impl->NetworkEffects[I].Effect==Effect)
    {if(Impl->AttachedEffects)Impl->AttachedEffects->StopSource(Impl->NetworkEffects[I].Attachment.Key);Impl->NetworkEffects.RemoveAt(I);}
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
    FImpl::BuildMips(Impl->TerrainAtlas);
    for (auto &Pair : Impl->Images)
        if (Pair.Value)
            FImpl::BuildMips(*Pair.Value);
    Impl->Preparing = false;
    return true;
}
UMaterialInterface *FAC2AssetLibrary::UploadTerrain()
{
    check(IsInGameThread());
    Impl->Texture(Impl->TerrainAtlas, true);
    ++Impl->Stats.TerrainMaterials;
    return Impl->Instance(&Impl->TerrainAtlas, FLinearColor::White, false);
}
bool FAC2AssetLibrary::UploadScenery(AActor *Owner, USceneComponent *Root, int32 MeshBudget)
{
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
            if (!C->GetBodyInstance() || !C->GetBodyInstance()->IsValidBodyInstance())
                return false;
    return true;
}
void FAC2AssetLibrary::InteriorSpawnPoints(const FVector &Near, float Radius, TArray<FVector> &Out) const
{
    for (const auto &Cell : Impl->InteriorPositions)
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
UMaterialInterface *FAC2AssetLibrary::Material(uint32 Id, bool Water)
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
UMaterialInterface *FAC2AssetLibrary::ParticleMaterial(uint32 Id)
{
    FLinearColor Tint = FLinearColor::White;
    bool Masked = false;
    auto I = Impl->MaterialImage(Id, Tint, Masked);
    auto Native = Impl->Material(Id);
    bool Additive = Native && !Native->Layers.IsEmpty() && Native->Layers[0].DestBlend == 2;
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
        if (B.ExpandBy(FVector(10, 10, 60)).IsInsideOrOn(P))
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
    check(IsInGameThread());
    FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!Impl->Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error))
        return false;
    FString OptionalError;
    Impl->HasHighRes = Impl->HighRes.Open(FPaths::Combine(Directory, TEXT("highres.dat")), 1, OptionalError);
    Impl->TextureNamespace = FPaths::ConvertRelativePathToFull(Directory);
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

UMaterialInterface *FAC2AssetLibrary::TerrainMaterial(const FAC2LandscapeDefs &Defs,
                                                      const FAC2LandBlock &Land, const FAC2TerrainMesh &Mesh)
{
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
    constexpr int32 Resolution = 2048;
    FAC2DecodedImage Atlas;
    Atlas.W = Resolution;
    Atlas.H = Resolution;
    Atlas.Pixels.SetNumUninitialized(Resolution * Resolution);
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
                Atlas.Pixels[Y * Resolution + X] = Color.ToFColorSRGB();
            }
        },
        EParallelForFlags::BackgroundPriority);
    if (Impl->Preparing)
    {
        Impl->TerrainAtlas = MoveTemp(Atlas);
        return nullptr;
    }
    Impl->Texture(Atlas, true);
    ++Impl->Stats.TerrainMaterials;
    return Impl->Instance(&Atlas, FLinearColor::White, false);
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
    Impl->PlacedBounds.Reset();
    TGuardValue<const FAC2LandBlock *> EnvironmentContext(Impl->EnvironmentLand, &Land);
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
        R.Take(8);
        const int32 N = R.Count(4, 16384);
        for (int32 I = 0; I < N && R.Good; ++I)
            Entities.Add(Impl->Entity(R));
        if (!R.Good)
        {
            Impl->Warn(Id, TEXT("unsupported placed entity layout"));
            Impl->Stats.SkippedEntities += N;
            return;
        }
    }
    // Entity links and the block property/stab tail do not affect these draw placements.
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
        const FVector Origin = R.Vector() * 100;
        const FQuat Rotation = R.Quaternion();
        const uint32 Flags = R.Word(), MeshId = R.Word();
        auto G = Impl->Mesh(MeshId);
        if (!R.Good || !G)
            continue;
        const FTransform Frame(Rotation, Origin + Offset);
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
        Impl->InteriorFloorTriangles.Add(CellId,MoveTemp(FloorTriangles));
        Impl->InteriorBounds.Add(Bounds);
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
        // Portal lists are still consumed to locate the cell's own scenery group.
        const int32 NP = R.Count(8, 4096);
        for (int32 J = 0; J < NP && R.Good; ++J)
        {
            uint16 Bits = R.Short();
            R.Take(6);
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
            R.Take(int64(N) * 2);
            R.Align();
        }
        if (Flags & 2)
        {
            R.Word();
            int32 N = R.Count(4, 16384);
            for (int32 J = 0; J < N && R.Good; ++J)
            {
                auto E = Impl->Entity(R);
                E.Origin = Frame.TransformPosition(E.Origin * 100) / 100;
                E.Rotation = Rotation * E.Rotation;
                Impl->DrawEntity(E, Owner, Root, FVector::ZeroVector, false);
            }
        }
    }
    for (FBox &Box : Impl->PlacedBounds)
    {
        Box.Min = (Box.Min - Offset) / 100;
        Box.Max = (Box.Max - Offset) / 100;
    }
    TArray<FAC2SceneryPlacement> Placements;
    Impl->Scenery->Generate(Defs, Land, Impl->PlacedBounds, Placements, Impl->BlockWater);
    for (const auto &P : Placements)
    {
        Impl->Decoration = P.Frill;
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

void FAC2AssetLibrary::UpdateMaterials(float Time)
{
    for (auto &M : Impl->MovingMaterials)
    {
        FMatrix UV = FMatrix::Identity;
        for (const auto &Op : M.Modifiers)
        {
            if (Op.Index != 0)
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
        M.Material->SetVectorParameterValue(TEXT("AC2UVRowU"),
                                            FLinearColor(UV.M[0][0], UV.M[1][0], UV.M[3][0], 0));
        M.Material->SetVectorParameterValue(TEXT("AC2UVRowV"),
                                            FLinearColor(UV.M[0][1], UV.M[1][1], UV.M[3][1], 0));
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
