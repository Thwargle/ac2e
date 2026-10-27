#include "AC2WorldStream.h"
#include "AC2Placement.h"
#include "AC2WorldGrid.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

struct FAC2WorldStream::FImpl
{
    struct FBlock
    {
        uint32 Id = 0;
        FAC2LandBlock Land;
        FAC2TerrainMesh Mesh;
        TUniquePtr<FAC2AssetLibrary> Assets;
        AActor *Actor = nullptr;
        UProceduralMeshComponent *Terrain = nullptr;
        UProceduralMeshComponent *WaterMesh = nullptr;
        FAC2BlockWater Water;
        bool Complete = false;
        ~FBlock()
        {
            if (IsValid(Actor))
                Actor->Destroy();
        }
    };
    UWorld *World = nullptr;
    FString Directory, LastError;
    FAC2DatArchive Cell, Portal;
    FAC2LandscapeDefs Defs;
    TMap<uint32, TUniquePtr<FBlock>> Blocks;
    TSet<uint32> Failed;
    TArray<uint32> Wanted;
    TUniquePtr<FBlock> Pending;
    TFuture<bool> Future;
    uint32 Center = 0;
    uint32 InteriorCell = 0;
    bool ExteriorVisible = true, TerrainCollision = true;
    void ApplyCellContext()
    {
        const auto* Active = Blocks.Find(InteriorCell >> 16);
        ExteriorVisible = !InteriorCell || (Active && (*Active)->Assets->CellReachesOutside(InteriorCell));
        for (auto& Pair : Blocks)
        {
            auto& B = *Pair.Value;
            B.Assets->SetActiveCell(!InteriorCell || (ExteriorVisible && B.Id != (InteriorCell >> 16)) ? 0 : InteriorCell);
            B.Terrain->SetVisibility(ExteriorVisible);
            B.Terrain->SetCollisionEnabled(ExteriorVisible && TerrainCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
            if (B.WaterMesh) B.WaterMesh->SetVisibility(ExteriorVisible);
        }
    }
    int32 UnloadedSinceGC = 0;
    bool Stopping = false;
    bool Load(uint32 Id, FAC2LandBlock &Land, FString &Error)
    {
        TArray<uint8> B;
        return Cell.Read((Id << 16) | 0xffff, B, Error) && Land.Decode((Id << 16) | 0xffff, B, Error);
    }
    void Commit()
    {
        auto &B = *Pending;
        B.Actor = World->SpawnActor<AActor>();
        B.Actor->SetActorLocation(AC2::BlockOrigin(B.Id));
        auto *Root = NewObject<USceneComponent>(B.Actor);
        B.Actor->SetRootComponent(Root);
        B.Actor->AddInstanceComponent(Root);
        Root->RegisterComponent();
        B.Actor->SetActorLocation(AC2::BlockOrigin(B.Id));
        B.Terrain = NewObject<UProceduralMeshComponent>(B.Actor);
        B.Actor->AddInstanceComponent(B.Terrain);
        B.Terrain->SetupAttachment(Root);
        B.Terrain->SetCollisionProfileName(TEXT("BlockAll"));
        B.Terrain->RegisterComponent();
        // This small 512-triangle cook is synchronous so a committed tile has a floor.
        B.Terrain->CreateMeshSection(0, B.Mesh.Vertices, B.Mesh.Indices, B.Mesh.Normals, B.Mesh.UVs, TArray<FColor>(),
                                     TArray<FProcMeshTangent>(), true);
        B.Terrain->SetMaterial(0, B.Assets->UploadTerrain());
        B.Water = B.Assets->Water();
        if (B.Water.Present)
        {
            auto *W = NewObject<UProceduralMeshComponent>(B.Actor);
            B.WaterMesh = W;
            B.Actor->AddInstanceComponent(W);
            W->SetupAttachment(Root);
            W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            W->SetCastShadow(false);
            W->RegisterComponent();
            const float Z = B.Water.Height * 100, T = 160 / B.Water.Tiling;
            const FVector2D Base((B.Id >> 8) * T, (B.Id & 255) * T);
            W->CreateMeshSection(
                0, {FVector(0, 0, Z), FVector(16000, 0, Z), FVector(0, 16000, Z), FVector(16000, 16000, Z)},
                {0, 2, 1, 1, 2, 3}, {FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector},
                {Base, Base + FVector2D(T, 0), Base + FVector2D(0, T), Base + FVector2D(T, T)}, TArray<FColor>(),
                TArray<FProcMeshTangent>(), false);
            W->SetMaterial(0, B.Assets->Material(B.Water.Above, true));
        }
        Blocks.Add(B.Id, MoveTemp(Pending));
        ApplyCellContext();
    }
};
FAC2WorldStream::FAC2WorldStream() : Impl(MakeUnique<FImpl>())
{
}
FAC2WorldStream::~FAC2WorldStream()
{
    Shutdown();
}
bool FAC2WorldStream::Open(UWorld *World, const FString &Directory, FString &Error)
{
    Impl->World = World;
    Impl->Directory = Directory;
    TArray<uint8> B;
    return Impl->Cell.Open(FPaths::Combine(Directory, TEXT("cell_1.dat")), 2, Error) &&
           Impl->Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) &&
           Impl->Portal.Read(0x36000000, B, Error) && Impl->Defs.Decode(B, Error);
}
void FAC2WorldStream::Shutdown()
{
    if (!Impl || Impl->Stopping)
        return;
    Impl->Stopping = true;
    // The worker references only its own library and copied block data. Join before
    // destroying them, including when PIE exits or a teleport makes work obsolete.
    if (Impl->Future.IsValid())
        Impl->Future.Wait();
    Impl->Pending.Reset();
    Impl->Blocks.Empty();
}
bool FAC2WorldStream::Destination(uint32 Block, FVector &Position, FString &Error)
{
    if ((Block >> 8) > 254 || (Block & 255) > 254)
    {
        Error = TEXT("Outdoor coordinates must be 00..FE on each axis");
        return false;
    }
    FAC2LandBlock Land;
    if (!Impl->Load(Block, Land, Error))
        return false;
    // A block center can lie in a cave mouth. Pick the nearest actual outdoor
    // triangle so teleporting never waits forever for a deliberately absent floor.
    const auto Mesh = BuildAC2TerrainMesh(Impl->Defs, Land);
    double Best = TNumericLimits<double>::Max();
    FVector Local;
    for (int32 I = 0; I < Mesh.Indices.Num(); I += 3)
    {
        const FVector P =
            (Mesh.Vertices[Mesh.Indices[I]] + Mesh.Vertices[Mesh.Indices[I + 1]] + Mesh.Vertices[Mesh.Indices[I + 2]]) /
            3;
        const double Distance = FMath::Square(P.X - 8000) + FMath::Square(P.Y - 8000);
        if (Distance < Best)
        {
            Best = Distance;
            Local = P;
        }
    }
    if (Mesh.Indices.IsEmpty())
    {
        Error = TEXT("Block has no outdoor ground; use flight to cross it");
        return false;
    }
    Position = AC2::BlockOrigin(Block) + Local + FVector(0, 0, 110);
    return true;
}
void FAC2WorldStream::Tick(const FVector &Position)
{
    if (Impl->Stopping || !AC2::WorldBlock(Position, Impl->Center))
        return;
    Impl->Wanted = AC2::NearbyBlocks(Impl->Center, Radius);
    for (auto It = Impl->Blocks.CreateIterator(); It; ++It)
        if (AC2::BlockDistance(It.Key(), Impl->Center) > Radius + 1)
        {
            It.RemoveCurrent();
            ++UnloadedTotal;
            ++Impl->UnloadedSinceGC;
        }
    if (Impl->UnloadedSinceGC >= 16)
    {
        // Retired actors/materials must not accumulate during sustained fast flight.
        GEngine->ForceGarbageCollection(false);
        Impl->UnloadedSinceGC = 0;
    }
    if (Impl->Pending && Impl->Future.IsReady())
    {
        const bool Success = Impl->Future.Get();
        if (Success && Impl->Wanted.Contains(Impl->Pending->Id))
        {
            Impl->Commit();
            ++LoadedTotal;
        }
        else
        {
            if (!Success)
            {
                Impl->Failed.Add(Impl->Pending->Id);
                UE_LOG(LogTemp, Warning, TEXT("AC2 streaming %04X failed: %s"), Impl->Pending->Id, *Impl->LastError);
            }
            Impl->Pending.Reset();
            ++DiscardedTotal;
        }
    }
    for (uint32 Id : Impl->Wanted)
        if (auto *B = Impl->Blocks.Find(Id))
            if (!(*B)->Complete)
            {
                (*B)->Complete = (*B)->Assets->UploadScenery((*B)->Actor, (*B)->Actor->GetRootComponent(), 4);
                if ((*B)->Complete)
                    (*B)->Assets->ReleaseCPUData();
                break;
            }
    for (const auto &B : Impl->Blocks)
    {
        B.Value->Assets->UpdateMaterials(Impl->World->GetTimeSeconds());
        FVector LocalViewer = Position - AC2::BlockOrigin(B.Key);
        B.Value->Assets->Animate(Impl->World->GetTimeSeconds(), 0, &LocalViewer);
    }
    if (!Impl->Pending)
    {
        for (uint32 Id : Impl->Wanted)
            if (!Impl->Blocks.Contains(Id) && !Impl->Failed.Contains(Id))
            {
                auto B = MakeUnique<FImpl::FBlock>();
                B->Id = Id;
                B->Assets = MakeUnique<FAC2AssetLibrary>();
                if (!Impl->Load(Id, B->Land, Impl->LastError) || !B->Assets->Open(Impl->Directory, Impl->LastError))
                {
                    Impl->Failed.Add(Id);
                    UE_LOG(LogTemp, Warning, TEXT("AC2 block %04X unavailable: %s"), Id, *Impl->LastError);
                    continue;
                }
                B->Mesh = BuildAC2TerrainMesh(Impl->Defs, B->Land);
                Impl->Pending = MoveTemp(B);
                auto *Job = Impl->Pending.Get();
                const auto Defs = Impl->Defs;
                const FString Directory = Impl->Directory;
                auto *State = Impl.Get();
                Impl->Future = Async(EAsyncExecution::ThreadPool, [Job, Defs, Directory, State]() {
                    return Job->Assets->PrepareBlock(Directory, Defs, Job->Land, Job->Mesh, State->LastError);
                });
                break;
            }
    }
}
bool FAC2WorldStream::ReadyAt(const FVector &P) const
{
    uint32 Id;
    return AC2::WorldBlock(P, Id) && Impl->Blocks.Contains(Id);
}
bool FAC2WorldStream::PlacementReadyAt(const FVector &P) const
{
    uint32 Id;
    if (!AC2::WorldBlock(P, Id))
        return false;
    for (uint32 Neighbor : AC2::NearbyBlocks(Id, 1))
    {
        const auto *B = Impl->Blocks.Find(Neighbor);
        if (!B || !(*B)->Complete || !(*B)->Assets->CollisionReady())
            return false;
    }
    return true;
}
void FAC2WorldStream::SetInteriorCell(uint32 Cell)
{
    if ((Cell & 0xffff) < 0x100) Cell = 0;
    if (Impl->InteriorCell == Cell) return;
    Impl->InteriorCell = Cell;
    Impl->ApplyCellContext();
}
bool FAC2WorldStream::ExteriorVisible() const { return Impl->ExteriorVisible; }
bool FAC2WorldStream::CellVisible(uint32 Cell) const
{
    const auto* Block = Impl->Blocks.Find(Cell >> 16);
    return Block && (*Block)->Assets->CellVisible((Cell & 0xffff) >= 0x100 ? Cell : 0);
}
bool FAC2WorldStream::NetworkLanding(const FVector& Feet, uint32 Cell, float CapsuleRadius, float HalfHeight,
                                   FVector& Center, const AActor* Ignore) const
{
    if (!Impl->World || Feet.ContainsNaN() || CapsuleRadius <= 0 || HalfHeight < CapsuleRadius) return false;
    if (AC2::StandingPosition(Impl->World, Feet, CapsuleRadius, HalfHeight, Center, Ignore)) return true;
    // A saved jump or swim is valid too. Do not hold login forever just because
    // that authoritative position is above the short standing-floor probe.
    const FVector AirCenter = Feet + FVector(0,0,HalfHeight);
    if (AC2::CapsuleClear(Impl->World, AirCenter, CapsuleRadius, HalfHeight, Ignore))
    {
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2ArrivalAir),false,Ignore);
        double Water;
        const bool Swimming = (Cell & 0xffff) < 0x100 && WaterAt(Feet,Water) && Feet.Z < Water;
        const bool AboveFloor = Impl->World->LineTraceSingleByChannel(Floor,Feet,Feet-FVector(0,0,500),ECC_Pawn,Query) &&
            !Floor.bStartPenetrating && Floor.ImpactNormal.Z >= .57357644f && Feet.Z-Floor.ImpactPoint.Z > 60;
        if (Swimming || AboveFloor) { Center=AirCenter; return true; }
    }
    // Keep authoritative positions when clear. Search locally at the same floor
    // level when old saves or a larger avatar put the capsule in a wall.
    for (int32 Ring = 1; Ring <= 8; ++Ring)
        for (int32 Angle = 0; Angle < 16; ++Angle)
        {
            const FVector Hint = Feet + FRotator(0, Angle * 22.5, 0).Vector() * (Ring * 50);
            if ((Cell & 0xffff) >= 0x100)
            {
                uint32 Found;
                if (!InteriorCellAt(Hint, Found) || (Found >> 16) != (Cell >> 16)) continue;
            }
            if (AC2::StandingPosition(Impl->World, Hint, CapsuleRadius, HalfHeight, Center, Ignore)) return true;
        }
    return false;
}
bool FAC2WorldStream::LandingPosition(const FVector &Preferred, FVector &Position, float CapsuleRadius,
                                      float HalfHeight) const
{
    uint32 Id;
    if (!AC2::WorldBlock(Preferred, Id) || !PlacementReadyAt(Preferred))
        return false;
    const auto &B = *Impl->Blocks[Id];
    TArray<FVector> Points;
    for (int32 I = 0; I < B.Mesh.Indices.Num(); I += 3)
        Points.Add(AC2::BlockOrigin(Id) + (B.Mesh.Vertices[B.Mesh.Indices[I]] + B.Mesh.Vertices[B.Mesh.Indices[I + 1]] +
                                           B.Mesh.Vertices[B.Mesh.Indices[I + 2]]) /
                                              3);
    Points.Sort([&](const FVector &A, const FVector &C) {
        return FVector::DistSquared2D(A, Preferred) < FVector::DistSquared2D(C, Preferred);
    });
    const auto *PC = Impl->World->GetFirstPlayerController();
    const AActor *Player = PC ? PC->GetPawn() : nullptr;
    // Prefer the exact click, then nearby triangles if slope or scenery obstructs the capsule.
    double Height;
    if (GroundAt(Preferred + FVector(0, 0, 10000000), Height))
        Points.Insert(FVector(Preferred.X, Preferred.Y, Height), 0);
    for (const FVector &P : Points)
    {
        if (B.Water.Present && B.Water.Height * 100 > P.Z + 130)
        {
            FVector Surface(P.X, P.Y, B.Water.Height * 100 + 20);
            if (AC2::CapsuleClear(Impl->World, Surface, CapsuleRadius, HalfHeight, Player))
            {
                Position = Surface;
                return true;
            }
        }
        else if (AC2::StandingPosition(Impl->World, P, CapsuleRadius, HalfHeight, Position, Player, B.Terrain))
            return true;
    }
    return false;
}
bool FAC2WorldStream::SpawnPosition(const FVector &Preferred, uint32 InteriorOwner, FVector &Position,
                                    float CapsuleRadius, float HalfHeight, const AActor *Ignore) const
{
    TArray<FVector> Points;
    if (InteriorOwner)
    {
        const auto *B = Impl->Blocks.Find(InteriorOwner);
        if (!B || !(*B)->Complete || !(*B)->Assets->CollisionReady())
            return false;
        const FVector Origin = AC2::BlockOrigin(InteriorOwner);
        (*B)->Assets->InteriorSpawnPoints(Preferred - Origin - FVector(0, 0, 103), 2000, Points);
        for (auto &P : Points)
            P += Origin;
    }
    else
    {
        for (const auto &Pair : Impl->Blocks)
        {
            const auto &B = *Pair.Value;
            if (!B.Complete || !B.Assets->CollisionReady())
                continue;
            for (int32 I = 0; I < B.Mesh.Indices.Num(); I += 3)
            {
                const FVector P = AC2::BlockOrigin(Pair.Key) +
                                  (B.Mesh.Vertices[B.Mesh.Indices[I]] + B.Mesh.Vertices[B.Mesh.Indices[I + 1]] +
                                   B.Mesh.Vertices[B.Mesh.Indices[I + 2]]) /
                                      3;
                if (FVector::DistSquared2D(P, Preferred) < FMath::Square(1600.) &&
                    !(B.Water.Present && B.Water.Height * 100 > P.Z + 100))
                    Points.Add(P);
            }
        }
    }
    Points.Sort([&](const FVector &A, const FVector &B) {
        return FVector::DistSquared(A, Preferred - FVector(0, 0, 103)) <
               FVector::DistSquared(B, Preferred - FVector(0, 0, 103));
    });
    for (const FVector &P : Points)
        // No ignored player: an NPC must also clear the player and previous NPCs.
        if (AC2::StandingPosition(Impl->World, P, CapsuleRadius, HalfHeight, Position, Ignore))
            return true;
    return false;
}
bool FAC2WorldStream::FailedAt(const FVector &P) const
{
    uint32 Id;
    return AC2::WorldBlock(P, Id) && Impl->Failed.Contains(Id);
}
bool FAC2WorldStream::GroundAt(const FVector &P, double &Height) const
{

    FHitResult Hit;
    FCollisionQueryParams Query;
    if (auto *PC = Impl->World->GetFirstPlayerController())
        if (PC->GetPawn())
            Query.AddIgnoredActor(PC->GetPawn());
    const bool Found = Impl->World->LineTraceSingleByObjectType(Hit, P, P - FVector(0, 0, 20000000),
                                                                FCollisionObjectQueryParams(ECC_WorldStatic), Query);
    if (Found)
        Height = Hit.Location.Z;
    return Found;
}
bool FAC2WorldStream::WaterAt(const FVector &P, double &Height) const
{
    uint32 Id;
    if (!AC2::WorldBlock(P, Id))
        return false;
    const auto *B = Impl->Blocks.Find(Id);
    if (!B || !(*B)->Water.Present)
        return false;
    Height = (*B)->Water.Height * 100;
    return true;
}
bool FAC2WorldStream::CellPosition(uint32 Id, FVector &Position) const
{
    const auto *B = Impl->Blocks.Find(Id >> 16);
    TArray<FVector> Points;
    if (!B || !(*B)->Assets->InteriorPositions(Id, Points))
        return false;
    if (!(*B)->Complete || !(*B)->Assets->CollisionReady())
        return false;
    const auto *PC = Impl->World->GetFirstPlayerController();
    const AActor *Player = PC ? PC->GetPawn() : nullptr;
    for (const FVector &P : Points)
        if (AC2::StandingPosition(Impl->World, P + AC2::BlockOrigin(Id >> 16), 34, 103, Position, Player))
            return true;
    return false;
}
int32 FAC2WorldStream::InteriorCount() const
{
    int32 N = 0;
    for (const auto &P : Impl->Blocks)
        N += P.Value->Assets->Stats().InteriorCells;
    return N;
}
int32 FAC2WorldStream::WaterCount() const
{
    int32 N = 0;
    for (const auto &P : Impl->Blocks)
        N += P.Value->Water.Present;
    return N;
}
bool FAC2WorldStream::AllReady() const
{
    if (Impl->Wanted.IsEmpty())
        return false;
    for (uint32 Id : Impl->Wanted)
    {
        const auto *B = Impl->Blocks.Find(Id);
        if (!B || !(*B)->Complete || !(*B)->Assets->CollisionReady())
            return false;
    }
    return true;
}
int32 FAC2WorldStream::ResidentCount() const
{
    return Impl->Blocks.Num();
}
int32 FAC2WorldStream::SceneryCount() const
{
    int32 Count = 0;
    for (const auto &P : Impl->Blocks)
        Count += P.Value->Assets->Stats().ScenerySections;
    return Count;
}
int32 FAC2WorldStream::GrassBatchCount() const
{
    int32 N = 0;
    for (const auto &P : Impl->Blocks)
        N += P.Value->Assets->Stats().FrillBatchesUploaded;
    return N;
}
bool FAC2WorldStream::GrassDepthReady() const
{
    int32 Sections = 0;
    for (const auto &P : Impl->Blocks)
    {
        const auto &S = P.Value->Assets->Stats();
        if (S.GrassSortedSections > 0)
            return false;
        Sections += S.GrassDepthSections;
    }
    return Sections > 0;
}
const FAC2LandscapeDefs &FAC2WorldStream::Landscape() const
{
    return Impl->Defs;
}
FString FAC2WorldStream::Status() const
{
    int32 Ready = 0, Grass = 0, High = 0, Textures = 0;
    bool HighAvailable = false;
    for (const auto &P : Impl->Blocks)
    {
        Ready += P.Value->Complete ? 1 : 0;
        const auto &S = P.Value->Assets->Stats();
        Grass += S.FrillsResolved;
        High += S.HighResSurfaces;
        Textures += S.Textures;
        HighAvailable |= S.HighResAvailable;
    }
    return FString::Printf(TEXT("Block %04X | %d resident / %d dressed | %d loaded / %d unloaded | %d unavailable\n"
                                "Grass %d placements | highres.dat %s (%d/%d texture references)"),
                           Impl->Center, Impl->Blocks.Num(), Ready, LoadedTotal, UnloadedTotal, Impl->Failed.Num(),
                           Grass, HighAvailable ? TEXT("active") : TEXT("unavailable"), High, Textures);
}
void FAC2WorldStream::SetTerrainCollision(bool Enabled)
{
    Impl->TerrainCollision = Enabled;
    Impl->ApplyCellContext();
}

bool FAC2WorldStream::InteriorAt(const FVector &P, uint32 &Block) const
{
    for (const auto &B : Impl->Blocks)
        if (B.Value->Complete && B.Value->Assets->InteriorAt(P - AC2::BlockOrigin(B.Key) - FVector(0, 0, 98)))
        {
            Block = B.Key;
            return true;
        }
    return false;
}
void FAC2WorldStream::SetDrawRadius(int32 VisibleRadius)
{
    for (const auto &B : Impl->Blocks)
    {
        bool Hidden = AC2::BlockDistance(B.Key, Impl->Center) > VisibleRadius;
        if (B.Value->Actor->IsHidden() != Hidden)
            B.Value->Actor->SetActorHiddenInGame(Hidden);
    }
}
bool FAC2WorldStream::InteriorCellAt(const FVector& Feet,uint32& Cell) const
{
    for(const auto& B:Impl->Blocks)
        if(B.Value->Complete && B.Value->Assets->InteriorCellAt(Feet-AC2::BlockOrigin(B.Key),Cell))return true;
    return false;
}

TArray<FAC2ParticleSource> FAC2WorldStream::ParticleSources(const FVector &Viewer) const
{
    TArray<FAC2ParticleSource> Out;
    for (const auto &Block : Impl->Blocks)
    {
        // Prepared source data is ready when terrain commits; uploading hundreds
        // of foliage batches must not delay campfires and other world effects.
        const auto &Sources = Block.Value->Assets->ParticleSources();
        for (int32 I = 0; I < Sources.Num(); ++I)
        {
            FAC2ParticleSource S = Sources[I];
            if (!Block.Value->Assets->CellVisible(S.Cell)) continue;
            S.Frame.AddToTranslation(AC2::BlockOrigin(Block.Key));
            if (FVector::DistSquared(S.Frame.GetLocation(), Viewer) > FMath::Square(22000.))
                continue;
            S.Key = (uint64(Block.Key) << 32) | uint64(I + 1);
            Out.Add(MoveTemp(S));
        }
    }
    Out.Sort([&](const auto &A, const auto &B) {
        return FVector::DistSquared(A.Frame.GetLocation(), Viewer) <
               FVector::DistSquared(B.Frame.GetLocation(), Viewer);
    });
    if (Out.Num() > 48)
        Out.SetNum(48);
    return Out;
}
