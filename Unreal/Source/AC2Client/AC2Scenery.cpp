#include "AC2Scenery.h"
#include "AC2AssetReader.h"
#include "Math/RotationMatrix.h"

FQuat AC2::ScenePlaneRotation(const FVector &Normal)
{
    // Plane::OrientFrameToPlane fixes forward in the Y/Z plane. The shortest
    // arc from up to N twists a compound slope's frame and its child offsets.
    if (FMath::Abs(Normal.X) < .0002 && FMath::Abs(Normal.Y) < .0002)
        return FQuat::Identity;
    const FVector Up = FMath::Abs(Normal.Z) > .0002 ? Normal.GetSafeNormal()
        : FVector(Normal.X, Normal.Y, 0).GetSafeNormal();
    const FVector Forward = FMath::Abs(Normal.Z) > .0002
        ? FVector(0, Normal.Z, -Normal.Y).GetSafeNormal() : FVector::UpVector;
    return FRotationMatrix::MakeFromYZ(Forward, Up).ToQuat();
}

double AC2::SceneRandom(uint32 X, uint32 Y)
{
    return double(((Y * X * X * 0x019a266f - 0x4a6506db) * Y - X * 0x6b3c5f11 - 0x39b625fb) >> 16) / 65536.0;
}
double AC2::PlacementRandom(uint32 X, uint32 Y, uint32 K)
{
    return double((K * 0x62433d7d - (X * Y * Y * K * K * 0x2bd38901 + 0x6b3c5f11) * X - Y * 0x4a6506db) >> 16) /
           65536.0;
}

namespace
{
struct FSceneGroup;
struct FSceneNode
{
    uint32 Kind = 0, Id = 0;
    TSharedPtr<FSceneGroup> Group;
};
struct FSceneGroup
{
    FVector MinLoc, MaxLoc, MinScale, MaxScale;
    float MinRotation = 0, MaxRotation = 0, Frequency = 0, MinSlope = 0, MaxSlope = 0, ScaleDepth = 1;
    bool Align = false, Orient = false;
    float HeightBands[10] = {};
    float Radius = 0, CheckRadius = 0, MinRadiusSlope = 0, MaxRadiusSlope = 0;
    bool ScaleCheckRadius = false, ScaleLocation = false, ScaleSize = false, RotateLocation = false,
         RotateOrientation = false, ScaleClearance = false, NoLegalCheck = false, Use3D = false;
    uint32 MaxObjects = 0, HeightRelative = 0, ScaleType = 0, BandTypes[5] = {};
    TArray<FSceneNode> Children;
};
struct FSceneFile
{
    TSharedPtr<FSceneGroup> Frill;
    TArray<FSceneNode> Nodes;
};
struct FSceneWeight
{
    uint32 Id = 0;
    float Weight = 0;
};
struct FTerrainScenes
{
    uint32 Default = 0;
    TMap<uint32, uint32> Indices;
};

TArray<FSceneNode> SceneNodes(FAC2AssetReader &R, int32 Depth, int32 &Budget);
TSharedPtr<FSceneGroup> SceneGroup(FAC2AssetReader &R, int32 Depth, int32 &Budget)
{
    if (Depth > 24 || --Budget < 0)
    {
        R.Good = false;
        return nullptr;
    }
    auto G = MakeShared<FSceneGroup>();
    G->MinLoc = R.Vector();
    G->MaxLoc = R.Vector();
    G->MinRotation = R.Float();
    G->MaxRotation = R.Float();
    G->Frequency = R.Float();
    G->MinScale = R.Vector();
    G->MaxScale = R.Vector();
    G->ScaleDepth = R.Float();
    G->MinSlope = R.Float();
    G->MaxSlope = R.Float();
    G->Align = R.Word() != 0;
    G->Orient = R.Word() != 0;
    for (float &F : G->HeightBands)
        F = R.Float();
    G->Radius = R.Float();
    G->CheckRadius = R.Float();
    G->ScaleCheckRadius = R.Word() != 0;
    G->MinRadiusSlope = R.Float();
    G->MaxRadiusSlope = R.Float();
    R.Float();
    G->MaxObjects = R.Word();
    R.Float();
    G->ScaleLocation = R.Word() != 0;
    G->ScaleSize = R.Word() != 0;
    G->RotateLocation = R.Word() != 0;
    G->RotateOrientation = R.Word() != 0;
    G->ScaleClearance = R.Word() != 0;
    G->NoLegalCheck = R.Word() != 0;
    G->Use3D = R.Word() != 0;
    R.Take(8);
    G->HeightRelative = R.Word();
    G->ScaleType = R.Word();
    for (uint32 &T : G->BandTypes)
        T = R.Word();
    G->Children = SceneNodes(R, Depth + 1, Budget);
    return G;
}
TArray<FSceneNode> SceneNodes(FAC2AssetReader &R, int32 Depth, int32 &Budget)
{
    TArray<FSceneNode> Result;
    const int32 N = R.Count(8, 16384);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        if (--Budget < 0)
        {
            R.Good = false;
            break;
        }
        FSceneNode Node;
        Node.Kind = R.Word();
        if (Node.Kind == 1 || Node.Kind == 3)
            Node.Id = R.Word();
        else if (Node.Kind == 2)
            Node.Group = SceneGroup(R, Depth, Budget);
        else
            R.Good = false;
        Result.Add(MoveTemp(Node));
    }
    return Result;
}
bool Band(uint32 Type, float Min, float Max, double Value)
{
    return Type == 0 ? false : Type == 1 ? Value >= Min : Type == 2 ? Value >= Min && Value <= Max : true;
}
} // namespace

struct FAC2SceneryGenerator::FImpl
{
    FAC2DatArchive &Portal;
    TMap<uint32, FTerrainScenes> Terrains;
    FTerrainScenes DefaultTerrain;
    TMap<uint32, TArray<FSceneWeight>> SceneTypes;
    TMap<uint32, TSharedPtr<FSceneFile>> Files;
    const FAC2LandscapeDefs *Defs = nullptr;
    const FAC2LandBlock *Land = nullptr;
    TArray<FAC2SceneryPlacement> *Output = nullptr;
    TArray<FBox> Blockers;
    uint32 GX = 0, GY = 0;
    int32 Work = 0, Unsupported = 0, OutputLimit = 4096;
    bool Frills = false;
    FAC2BlockWater Water;
    struct FFrillJob
    {
        TSharedPtr<FSceneGroup> Group;
        FTransform Pose;
        uint32 Seed, X, Y;
    };
    TArray<FFrillJob> FrillJobs;
    explicit FImpl(FAC2DatArchive &In) : Portal(In) {}

    TSharedPtr<FSceneFile> File(uint32 Id)
    {
        if (auto *F = Files.Find(Id))
            return *F;
        Files.Add(Id, nullptr);
        TArray<uint8> B;
        FString E;
        if (!Portal.Read(Id, B, E))
            return nullptr;
        FAC2AssetReader R(MoveTemp(B));
        R.Word();
        if (R.Word() != Id)
            return nullptr;
        R.Word();
        int32 Budget = 16384;
        auto F = MakeShared<FSceneFile>();
        if (R.Word())
            F->Frill = SceneGroup(R, 0, Budget);
        const int32 N = R.Count(16, 256);
        for (int32 I = 0; I < N && R.Good; ++I)
        {
            R.Take(12);
            auto Nodes = SceneNodes(R, 0, Budget);
            if (!I)
                F->Nodes = MoveTemp(Nodes);
        }
        if (!R.End())
        {
            UE_LOG(LogTemp, Warning, TEXT("AC2 SceneFile %08X rejected"), Id);
            return nullptr;
        }
        Files[Id] = F;
        return F;
    }
    bool Ground(double X, double Y, double &Height, FVector &Normal, bool &Road) const
    {
        if (!SampleAC2Terrain(*Defs, *Land, X, Y, Height, Normal))
            return false;
        const int32 IX = int32(X / 10), IY = int32(Y / 10);
        const int32 Indices[4] = {IX * 17 + IY, (IX + 1) * 17 + IY, IX * 17 + IY + 1, (IX + 1) * 17 + IY + 1};
        Road = false;
        for (int32 I = 0; I < 4; ++I)
        {
            // Conservative road exclusion for this preview: reserve the whole marked quad.
            Road |= ((Land->CellInfo[Indices[I]] >> 24) & 15) != 0;
        }
        return true;
    }
    void Scene(uint32 Id, const FTransform &Pose, uint32 Seed, int32 Depth)
    {
        if (Depth > 24 || Work++ > 50000 || Output->Num() >= OutputLimit)
            return;
        auto F = File(Id);
        if (!F)
            return;
        Nodes(F->Nodes, Pose, Id * Seed * 0x923b1, Depth + 1, 0);
        if (F->Frill)
        {
            const uint32 FrillSeed = Id * Seed * 0x923b1 + 0x03804309;
            if (Frills)
                Group(*F->Frill, Pose, FrillSeed, Depth + 1);
            else
                FrillJobs.Add({F->Frill, Pose, FrillSeed, GX, GY});
        }
    }
    void Nodes(const TArray<FSceneNode> &List, const FTransform &Pose, uint32 Seed, int32 Depth, uint32 Max,
               bool GroundFrill = false)
    {
        uint32 Emitted = 0;
        for (int32 I = 0; I < List.Num() && Work < 50000 && Output->Num() < OutputLimit; ++I)
        {
            const FSceneNode &N = List[I];
            const uint32 Child = Seed + uint32(I) * 0x2d41bf19;
            if (N.Kind == 1)
            {
                FAC2SceneryPlacement P;
                P.Object = N.Id;
                P.Transform = Pose;
                P.Frill = Frills;
                P.GroundFrill = GroundFrill;
                Output->Add(P);
                ++Emitted;
            }
            else if (N.Kind == 2 && N.Group)
                Group(*N.Group, Pose, Child, Depth + 1);
            else if (N.Kind == 3)
            {
                const int32 Before = Output->Num();
                Scene(N.Id, Pose, Child, Depth + 1);
                Emitted += Output->Num() - Before;
            }
            if (Max && Emitted >= Max)
                break;
        }
    }
    void Group(const FSceneGroup &G, const FTransform &Base, uint32 Seed, int32 Depth)
    {
        if (Depth > 24 || Work++ > 50000)
            return;
        const uint32 K = Seed + 0x01191e67;
        const double Frequency =
            double(((0x62433d7d - K * GX * GX * GY * GY * 0x2bd38901) * K - GY * 0x4a6506db - GX * 0x6b3c5f11) >> 16) /
            65536;
        if (Frequency >= G.Frequency)
            return;
        if (G.HeightRelative > 9 || G.ScaleType > 9)
        {
            ++Unsupported;
            return;
        }
        auto Rand = [&](uint32 Offset) { return AC2::PlacementRandom(GX, GY, Seed + Offset); };
        const double RA = Rand(0x515e71), RB = Rand(0xde876c7), RC = Rand(0x17e0784f);
        FVector RScale(RA, RB, RC);
        if (G.ScaleType == 1)
            RScale = FVector(RA, RA, RB);
        if (G.ScaleType == 2)
            RScale = FVector(RA, RB, RA);
        if (G.ScaleType == 3)
            RScale = FVector(RA, RB, RB);
        if (G.ScaleType == 4)
            RScale = FVector(RA);
        FVector Scale =
            (G.MinScale + (G.MaxScale - G.MinScale) * RScale) * (G.ScaleSize ? Base.GetScale3D() : FVector::OneVector);
        const double RX = Rand(0x28d0642d), RY = Rand(0x62ade4f), RZ = Rand(0x71d36d);
        FVector Delta = (G.MinLoc + (G.MaxLoc - G.MinLoc) * FVector(RX, RY, RZ)) *
                        (G.ScaleLocation ? Base.GetScale3D() : FVector::OneVector);
        if (G.HeightRelative != 9)
            Delta.Z = 0;
        if (G.RotateLocation)
            Delta = Base.GetRotation().RotateVector(Delta);
        FVector P = Base.GetLocation() + Delta;
        const double Radius =
            FMath::Max(0.0, double(G.Radius) * (G.ScaleClearance ? FMath::Max(Scale.X, Scale.Y) : 1.0));
        if (P.X < Radius || P.Y < Radius || P.X >= 160 - Radius || P.Y >= 160 - Radius)
            return;
        double Z;
        FVector Normal;
        bool Road;
        if (!Ground(P.X, P.Y, Z, Normal, Road) || (!Frills && !G.NoLegalCheck && Road) || Normal.Z > G.MinSlope ||
            Normal.Z < G.MaxSlope)
            return;
        if (G.HeightRelative == 1 && !Water.Present)
            return;
        if (G.HeightRelative != 9)
            P.Z =
                (G.HeightRelative == 0   ? Z
                 : G.HeightRelative == 1 ? Water.Height
                                         : 0) +
                FMath::Lerp(double(G.MinLoc.Z), double(G.MaxLoc.Z), RX) * (G.ScaleLocation ? Base.GetScale3D().Z : 1.0);
        const double DepthWater = Water.Present ? Water.Height - Z : 0;
        if (G.ScaleType >= 5 && G.ScaleType <= 7)
        {
            if (DepthWater <= 0.0002 || G.ScaleDepth <= 0)
                return;
            double Factor = DepthWater / G.ScaleDepth;
            if (G.ScaleType == 7)
                Scale = FVector(Factor);
            else
                Scale.Z = Factor;
        }
        if (G.ScaleType == 8 || G.ScaleType == 9)
        {
            if (FMath::Abs(Normal.X) >= .9998 || FMath::Abs(Normal.Y) >= .9998)
                return;
            Scale.X = 1 / FMath::Sqrt(1 - Normal.X * Normal.X);
            Scale.Y = 1 / FMath::Sqrt(1 - Normal.Y * Normal.Y);
            if (G.ScaleType == 9)
                Scale.Z = (Scale.X + Scale.Y) * .5;
        }
        const double Above = P.Z - (DepthWater > .0002 ? Water.Height : Z);
        const int32 BandIndex = DepthWater > .0002 ? (Above < -.0002 ? 2 : 1) : (Above < -.0002 ? 4 : 0);
        if (DepthWater > .0002 && !Band(G.BandTypes[3], G.HeightBands[6], G.HeightBands[7], DepthWater))
            return;
        if (!Band(G.BandTypes[BandIndex], G.HeightBands[BandIndex * 2], G.HeightBands[BandIndex * 2 + 1],
                  FMath::Abs(Above)))
            return;
        if (G.CheckRadius > 0)
        {
            const double Check = G.CheckRadius * (G.ScaleCheckRadius ? Scale.GetAbsMax() : 1.0);
            for (int32 I = 0; I < 8; ++I)
            {
                const double A = I * UE_PI / 4;
                double H;
                FVector N;
                bool B;
                if (!Ground(P.X + FMath::Cos(A) * Check, P.Y + FMath::Sin(A) * Check, H, N, B) ||
                    N.Z > G.MinRadiusSlope || N.Z < G.MaxRadiusSlope)
                    return;
            }
        }
        if (!Frills && !G.NoLegalCheck)
        {
            for (const FBox &B : Blockers)
            {
                const FBox Expanded = B.ExpandBy(Radius);
                if (P.X >= Expanded.Min.X && P.X <= Expanded.Max.X && P.Y >= Expanded.Min.Y && P.Y <= Expanded.Max.Y &&
                    (!G.Use3D || (P.Z >= Expanded.Min.Z && P.Z <= Expanded.Max.Z)))
                    return;
            }
        }
        double Degrees = FMath::Lerp(double(G.MinRotation), double(G.MaxRotation), RX);
        if (G.Orient)
            Degrees += FMath::RadiansToDegrees(FMath::Atan2(Normal.X, Normal.Y)) + 180;
        FQuat Q(FVector::UpVector, -FMath::DegreesToRadians(Degrees));
        if (G.Align)
            Q = AC2::ScenePlaneRotation(Normal) * Q;
        else if (G.RotateOrientation && !G.Orient)
            Q = Base.GetRotation() * Q;
        const int32 Before = Output->Num();
        const bool GroundFrill = Frills && G.Align && G.HeightRelative == 0 &&
            FMath::Abs(G.MinLoc.Z) < .0002 && FMath::Abs(G.MaxLoc.Z) < .0002;
        Nodes(G.Children, FTransform(Q, P, Scale), Seed, Depth + 1, G.MaxObjects, GroundFrill);
        if (!Frills && Output->Num() > Before && Radius > 0)
            Blockers.Add(FBox(P - FVector(Radius), P + FVector(Radius)));
    }
};

FAC2SceneryGenerator::FAC2SceneryGenerator(FAC2DatArchive &Portal) : Impl(MakeUnique<FImpl>(Portal)) {}
FAC2SceneryGenerator::~FAC2SceneryGenerator() = default;
bool FAC2SceneryGenerator::Initialize(FString &Error)
{
    TArray<uint8> B;
    if (!Impl->Portal.Read(0x12000000, B, Error))
        return false;
    FAC2AssetReader R(MoveTemp(B));
    R.Take(8);
    auto Terrain = [&]() {
        FTerrainScenes T;
        R.String();
        R.Word();
        const uint32 Index = R.Word();
        R.Float();
        if (R.Word())
        {
            T.Default = R.Word();
            R.Word();
        }
        const int32 N = R.Count(8);
        for (int32 I = 0; I < N; ++I)
        {
            const uint32 E = R.Word(), K = R.Word();
            T.Indices.Add(K, E);
        }
        Impl->Terrains.Add(Index, T);
        return T;
    };
    if (R.Word())
        Impl->DefaultTerrain = Terrain();
    const int32 N = R.Count(22, 4096);
    for (int32 I = 0; I < N && R.Good; ++I)
        Terrain();
    if (!R.End())
    {
        Error = TEXT("Terrain scenes layout");
        return false;
    }
    if (!Impl->Portal.Read(0x11000000, B, Error))
        return false;
    FAC2AssetReader S(MoveTemp(B));
    S.Word();
    if (S.Word() != 0x11000000)
        return false;
    const int32 NS = S.Count(12, 16384);
    for (int32 I = 0; I < NS && S.Good; ++I)
    {
        const uint32 Index = S.Word();
        S.Word();
        const int32 NW = S.Count(12, 16384);
        TArray<FSceneWeight> Weights;
        for (int32 J = 0; J < NW; ++J)
        {
            FSceneWeight W;
            W.Id = S.Word();
            W.Weight = S.Float();
            S.Word();
            Weights.Add(W);
        }
        Impl->SceneTypes.Add(Index, MoveTemp(Weights));
    }
    if (!S.End())
    {
        Error = TEXT("SceneDesc layout");
        return false;
    }
    return true;
}
void FAC2SceneryGenerator::Generate(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land,
                                    const TArray<FBox> &PlacedBounds, TArray<FAC2SceneryPlacement> &Out,
                                    const FAC2BlockWater &Water)
{
    Impl->Defs = &Defs;
    Impl->Land = &Land;
    Impl->Output = &Out;
    Impl->Blockers = PlacedBounds;
    Impl->FrillJobs.Reset();
    Impl->Frills = false;
    Impl->Water = Water;
    Impl->Work = 0;
    Impl->OutputLimit = 4096;
    Impl->Unsupported = 0;
    const uint32 BX = Land.Id >> 24, BY = (Land.Id >> 16) & 255;
    for (int32 X = 0; X < 17; ++X)
        for (int32 Y = 0; Y < 17; ++Y)
        {
            const uint32 Info = Land.CellInfo[X * 17 + Y];
            const auto *T = Impl->Terrains.Find(Info & Defs.TerrainMask);
            if (!T)
                T = &Impl->DefaultTerrain;
            const uint32 *Type = T->Indices.Find((Info >> 7) & 31);
            const auto *List = Impl->SceneTypes.Find(Type ? *Type : T->Default);
            if (!List || List->IsEmpty())
                continue;
            Impl->GX = BX * 16 + X;
            Impl->GY = BY * 16 + Y;
            double Total = 0;
            for (const auto &W : *List)
                Total += W.Weight;
            const double Draw = AC2::SceneRandom(Impl->GX, Impl->GY) * Total;
            double Sum = 0;
            uint32 Id = List->Last().Id;
            for (const auto &W : *List)
            {
                Sum += W.Weight;
                if (Draw < Sum)
                {
                    Id = W.Id;
                    break;
                }
            }
            Impl->Scene(
                Id,
                FTransform(FQuat::Identity, FVector(X * 9.9998, Y * 9.9998, Defs.Heights[Land.Heights[X * 17 + Y]])),
                0x923b1, 0);
        }
    Impl->Frills = true;
    // Give every vertex scene a share instead of exhausting a whole-block cap
    // in the first corner. Runtime grass is merged into spatial material batches.
    const int32 Limit = Out.Num() + 32768;
    for (const auto &J : Impl->FrillJobs)
    {
        if (Out.Num() >= Limit)
            break;
        Impl->Work = 0;
        Impl->OutputLimit = FMath::Min(Limit, Out.Num() + 128);
        Impl->GX = J.X;
        Impl->GY = J.Y;
        Impl->Group(*J.Group, J.Pose, J.Seed, 0);
    }
    Impl->FrillJobs.Reset();
    UnsupportedGroups += Impl->Unsupported;
}
