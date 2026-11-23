#include "AC2FarTerrain.h"
#include "AC2WorldGrid.h"
#include "AC2NativeMaterial.h"
#include "AC2WaterReflection.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

struct FAC2FarTerrain::FImpl
{
    static constexpr int32 FarRadius = 14, TilePixels = 64, AtlasSize = (FarRadius * 2 + 1) * TilePixels;
    struct FJob
    {
        uint32 Center = 0;
        int32 Tiles = 0;
        TArray<FVector> Vertices, Normals;
        TArray<FVector2D> UVs;
        TArray<int32> Indices;
        TArray<FColor> Colors;
        TArray<FColor> Pixels;
        TArray<TArray<FColor>> Mips;
        TMap<uint32,FAC2ShoreWaterMesh> WaterMeshes;
        TMap<uint32,double> WaterHeights;
        TMap<uint32,FAC2TerrainMesh> BelowMeshes;
        TMap<uint32,uint32> BelowMaterials;
        int32 WaterTiles = 0;
    };
    UWorld *World = nullptr;
    FString Directory;
    FAC2LandscapeDefs Defs;
    TUniquePtr<FAC2AssetLibrary> Assets;
    TUniquePtr<FJob> Job;
    TFuture<void> Future;
    AActor *Actor = nullptr;
    uint32 Current = MAX_uint32;
    int32 Count = 0;
    int32 WaterCount = 0, WaterBatches = 0;
    TMap<uint32,UMaterialInterface*> WaterMaterials;
    TMap<uint32,UMaterialInterface*> BelowWaterMaterials;
    void Build(FJob &J, int32 Near)
    {
        J.Pixels.Init(FColor::White, AtlasSize * AtlasSize);
        FAC2DatArchive Cell;
        FAC2DatArchive Portal;
        FAC2WaterSource Water;
        FString Error;
        if (!Cell.Open(FPaths::Combine(Directory, TEXT("cell_1.dat")), 2, Error) ||
            !Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) || !Water.Load(Portal, Error))
            return;
        const FVector Origin = AC2::BlockOrigin(J.Center);
        // Reuse a single DAT halo for the whole job, including the outer ring's
        // normals, rather than rereading eight neighbours for every far tile.
        const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,J.Center,FarRadius+1);
        for (int32 X = FMath::Max(0, int32(J.Center >> 8) - FarRadius);
             X <= FMath::Min(254, int32(J.Center >> 8) + FarRadius); ++X)
            for (int32 Y = FMath::Max(0, int32(J.Center & 255) - FarRadius);
                 Y <= FMath::Min(254, int32(J.Center & 255) + FarRadius); ++Y)
            {
                uint32 Id = (X << 8) | Y;
                const auto* Found=Neighborhood.Find(Id);
                if(!Found)continue;
                const auto& Land=*Found;
                const FVector Offset = AC2::BlockOrigin(Id) - Origin;
                const auto W = Water.At(Defs, Land);
                if(W.Present)
                {
                    J.WaterHeights.Add(Id,W.Height*100.);
                    AppendAC2ShoreWaterMesh(Id,W,Offset,Defs,Neighborhood,J.WaterMeshes.FindOrAdd(W.Above),&Cell);
                    if(W.Below){AppendAC2WaterMesh(Id,W,Offset,J.BelowMeshes.FindOrAdd(W.Above));J.BelowMaterials.Add(W.Above,W.Below);}
                    ++J.WaterTiles;
                }
                // Water spans the whole landscape radius, including nearby
                // tiles. This removes both the flat-colour LOD boundary and
                // translucent overlaps while the terrain streamer changes tiles.
                if (AC2::BlockDistance(Id, J.Center) <= Near)
                    continue;
                ++J.Tiles;
                const int32 TX = X - int32(J.Center >> 8) + FarRadius;
                const int32 TY = Y - int32(J.Center & 255) + FarRadius;
                constexpr int32 Gutter = 2, ContentPixels = TilePixels - Gutter * 2;
                const auto NativeMesh = BuildAC2TerrainMesh(Defs, Land, &Neighborhood);
                const auto Pixels = Assets->TerrainPixels(Defs, Land, NativeMesh, ContentPixels);
                for (int32 V = 0; V < TilePixels; ++V)
                    for (int32 U = 0; U < TilePixels; ++U)
                        J.Pixels[(TY * TilePixels + V) * AtlasSize + TX * TilePixels + U] =
                            Pixels[FMath::Clamp(V-Gutter,0,ContentPixels-1)*ContentPixels+FMath::Clamp(U-Gutter,0,ContentPixels-1)];
                const int32 Base = J.Vertices.Num();
                // 8x8 quads nearby and 4x4 beyond 1.3 km. Full-resolution edge samples
                // are unnecessary because adjoining far blocks use a short skirt.
                // Coarse diagonals can put the shoreline below the water plane,
                // erasing narrow banks. Preserve the authored shore triangles.
                int32 Step = W.Present ? 1 : (AC2::BlockDistance(Id, J.Center) < 8 ? 2 : 4);
                const int32 Row = 16 / Step + 1;
                for (int32 I = 0; I <= 16; I += Step)
                    for (int32 K = 0; K <= 16; K += Step)
                    {
                        J.Vertices.Add(Offset +
                                       FVector(I * 1000, K * 1000, Defs.Heights[Land.Heights[I * 17 + K]] * 100));
                        J.Normals.Add(NativeMesh.Normals[I*17+K]);
                        J.UVs.Add(FVector2D((TX * TilePixels + Gutter + .5f + I / 16.f * (ContentPixels - 1)) / AtlasSize,
                            (TY * TilePixels + Gutter + .5f + K / 16.f * (ContentPixels - 1)) / AtlasSize));
                        J.Colors.Add(FColor::White);
                    }
                if(Step==1)for(int32 Index:NativeMesh.Indices)J.Indices.Add(Base+Index);
                else for (int32 I = 0; I < Row - 1; ++I)
                    for (int32 K = 0; K < Row - 1; ++K)
                    {
                        int32 A = Base + I * Row + K;
                        J.Indices.Append({A, A + 1, A + Row, A + 1, A + Row + 1, A + Row});
                    }
                // Skirts seal the differing sample rates at LOD boundaries.
                auto Skirt = [&](int32 A, int32 Z) {
                    const int32 N = J.Vertices.Num();
                    J.Vertices.Append({J.Vertices[A] - FVector(0, 0, 1200), J.Vertices[Z] - FVector(0, 0, 1200)});
                    J.Normals.Append({FVector::UpVector, FVector::UpVector});
                    J.UVs.Append({J.UVs[A], J.UVs[Z]});
                    J.Colors.Append({J.Colors[A], J.Colors[Z]});
                    J.Indices.Append({A, N, Z, Z, N, N + 1});
                };
                for (int32 I = 0; I < Row - 1; ++I)
                {
                    Skirt(Base + I, Base + I + 1);
                    Skirt(Base + (Row - 1) * Row + I + 1, Base + (Row - 1) * Row + I);
                    Skirt(Base + (I + 1) * Row, Base + I * Row);
                    Skirt(Base + I * Row + Row - 1, Base + (I + 1) * Row + Row - 1);
                }
            }
        // Filter on the worker and stop before unrelated atlas cells merge.
        // This also avoids moving the large atlas resampling cost onto a frame.
        for (int32 Level=1, Size=AtlasSize/2; Level<=4; ++Level, Size/=2)
        {
            const auto& Source = Level==1 ? J.Pixels : J.Mips.Last();
            TArray<FColor> Mip; Mip.SetNumUninitialized(Size*Size);
            for (int32 Y=0;Y<Size;++Y) for(int32 X=0;X<Size;++X)
            {
                FLinearColor C(0,0,0,0);
                for(int32 V=0;V<2;++V) for(int32 U=0;U<2;++U) C+=FLinearColor(Source[(Y*2+V)*(Size*2)+X*2+U]);
                Mip[Y*Size+X]=(C/4).ToFColorSRGB();
            }
            J.Mips.Add(MoveTemp(Mip));
        }
    }
};
FAC2FarTerrain::FAC2FarTerrain() : Impl(MakeUnique<FImpl>()) {}
FAC2FarTerrain::~FAC2FarTerrain()
{
    if (Impl->Future.IsValid())
        Impl->Future.Wait();
    if (IsValid(Impl->Actor))
        Impl->Actor->Destroy();
}
bool FAC2FarTerrain::Open(UWorld *World, const FString &Directory, FString &Error)
{
    Impl->World = World;
    Impl->Directory = Directory;
    Impl->Assets = MakeUnique<FAC2AssetLibrary>();
    FAC2DatArchive P;
    TArray<uint8> B;
    return Impl->Assets->Open(Directory, Error) && P.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) &&
           P.Read(0x36000000, B, Error) && Impl->Defs.Decode(B, Error);
}
void FAC2FarTerrain::Tick(const FVector &Viewer, int32 Near, bool Ready)
{
    // This library owns the combined near/distant water materials. It is not
    // part of WorldStream's per-block tick, so advance its DAT UV waveforms
    // here too, on the same world clock as the temporary near surfaces.
    if (Impl->Assets && Impl->World)
        Impl->Assets->UpdateMaterials(Impl->World->GetTimeSeconds());
    uint32 Id;
    if (!AC2::WorldBlock(Viewer, Id))
        return;
    if (Impl->Job && Impl->Future.IsReady())
    {
        Impl->Future.Get();
        auto &J = *Impl->Job;
        if (J.Center == Id && Ready)
        {
            if (Impl->Actor)
                Impl->Actor->Destroy();
            Impl->Actor = Impl->World->SpawnActor<AActor>();
            auto *C = NewObject<UProceduralMeshComponent>(Impl->Actor);
            C->ComponentTags.Add(TEXT("AC2TerrainSurface"));
            Impl->Actor->SetRootComponent(C);
            Impl->Actor->AddInstanceComponent(C);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            C->SetCastShadow(false);
            C->RegisterComponent();
            Impl->Actor->SetActorLocation(AC2::BlockOrigin(Id));
            C->CreateMeshSection(0, J.Vertices, J.Indices, J.Normals, J.UVs, J.Colors, TArray<FProcMeshTangent>(),
                                 false);
            auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AC2/Materials/M_AC2Far.M_AC2Far"));
            auto* Material = UMaterialInstanceDynamic::Create(Parent, C);
            auto* Texture = UTexture2D::CreateTransient(FImpl::AtlasSize, FImpl::AtlasSize, PF_B8G8R8A8);
            Texture->SRGB = true; Texture->NeverStream = true;
            Texture->AddressX = Texture->AddressY = TA_Clamp;
            auto& Mip = Texture->GetPlatformData()->Mips[0];
            void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(Data, J.Pixels.GetData(), J.Pixels.Num() * sizeof(FColor));
            Mip.BulkData.Unlock();
            for(int32 Level=0, Size=FImpl::AtlasSize/2;Level<J.Mips.Num();++Level,Size/=2)
            {
                auto* Next=new FTexture2DMipMap(); Next->SizeX=Next->SizeY=Size;
                Texture->GetPlatformData()->Mips.Add(Next);
                Next->BulkData.Lock(LOCK_READ_WRITE);
                FMemory::Memcpy(Next->BulkData.Realloc(J.Mips[Level].Num()*sizeof(FColor)),J.Mips[Level].GetData(),J.Mips[Level].Num()*sizeof(FColor));
                Next->BulkData.Unlock();
            }
            Texture->UpdateResource();
            Material->SetTextureParameterValue(TEXT("Albedo"), Texture);
            Impl->Assets->ConfigureTerrainCloud(Material);
            C->SetMaterial(0, Material);
            RegisterAC2WorldMaterial(Material,C,false);
            Impl->WaterBatches=0;
            for(const auto& Plane:J.WaterHeights)RegisterAC2WaterPlane(Impl->Actor->GetWorld(),Impl->Actor,Plane.Key,Plane.Value);
            for(const auto& Pair:J.WaterMeshes)
            {
                auto* Surface=NewObject<UProceduralMeshComponent>(Impl->Actor);
                Impl->Actor->AddInstanceComponent(Surface);
                Surface->SetupAttachment(C);
                Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Surface->SetCastShadow(false);
                ExcludeFromAC2WaterReflection(Impl->World,Surface);
                Surface->RegisterComponent();
                const auto& Mesh=Pair.Value;
                Surface->CreateMeshSection(0,Mesh.Vertices,Mesh.Indices,Mesh.Normals,Mesh.UVs,
                                          Mesh.ShoreUVs,{},{},
                                          TArray<FColor>(),TArray<FProcMeshTangent>(),false);
                auto*& WaterMaterial=Impl->WaterMaterials.FindOrAdd(Pair.Key);
                if(!WaterMaterial)WaterMaterial=Impl->Assets->Material(Pair.Key,true);
                Surface->SetMaterial(0,WaterMaterial);
                RegisterAC2WorldMaterial(Cast<UMaterialInstanceDynamic>(WaterMaterial),Surface,false);
                RegisterAC2WaterReflection(Impl->Actor->GetWorld(),Cast<UMaterialInstanceDynamic>(WaterMaterial));
                if(const auto* Under=J.BelowMeshes.Find(Pair.Key)){
                    Surface->CreateMeshSection(1,Under->Vertices,Under->Indices,Under->Normals,Under->UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),false);
                    const uint32 BelowId=J.BelowMaterials[Pair.Key];
                    auto*& UnderMaterial=Impl->BelowWaterMaterials.FindOrAdd(BelowId);
                    if(!UnderMaterial)UnderMaterial=Impl->Assets->Material(BelowId,true,true);
                    Surface->SetMaterial(1,UnderMaterial);RegisterAC2WorldMaterial(Cast<UMaterialInstanceDynamic>(UnderMaterial),Surface,false);
                    RegisterAC2WaterReflection(Impl->Actor->GetWorld(),Cast<UMaterialInstanceDynamic>(UnderMaterial));
                }
                ++Impl->WaterBatches;
                if(!Mesh.Strip.Indices.IsEmpty()){
                    auto* Shore=NewObject<UProceduralMeshComponent>(Impl->Actor);
                    Impl->Actor->AddInstanceComponent(Shore);Shore->SetupAttachment(C);
                    Shore->SetCollisionEnabled(ECollisionEnabled::NoCollision);Shore->SetCastShadow(false);
                    ExcludeFromAC2WaterReflection(Impl->World,Shore);
                    Shore->SetTranslucentSortPriority(1);Shore->RegisterComponent();
                    const auto& Strip=Mesh.Strip;
                    Shore->CreateMeshSection(0,Strip.Vertices,Strip.Indices,Strip.Normals,Strip.UVs,
                        Mesh.StripUVs,{},{},TArray<FColor>(),TArray<FProcMeshTangent>(),false);
                    Shore->SetMaterial(0,WaterMaterial);
                    const auto& Foam=Mesh.Foam;
                    if(!Foam.Indices.IsEmpty()){
                        Shore->CreateMeshSection(1,Foam.Vertices,Foam.Indices,Foam.Normals,Foam.UVs,
                            Mesh.FoamMaskUVs,Mesh.FoamPassUVs,{},TArray<FColor>(),TArray<FProcMeshTangent>(),false);
                        Shore->SetMaterial(1,Shore->GetMaterial(0));
                        ++Impl->WaterBatches;
                    }
                    ++Impl->WaterBatches;
                }
            }
            Impl->Current = Id;
            Impl->Count = J.Tiles;
            Impl->WaterCount=J.WaterTiles;
            UE_LOG(LogTemp, Display, TEXT("AC2 distant terrain: %d tiles, %d triangles; native water: %d tiles, %d batches"),
                   J.Tiles,J.Indices.Num()/3,J.WaterTiles,Impl->WaterBatches);
        }
        Impl->Job.Reset();
    }
    if (!Impl->Job && Ready && Id != Impl->Current)
    {
        Impl->Job = MakeUnique<FImpl::FJob>();
        Impl->Job->Center = Id;
        auto *State = Impl.Get();
        Impl->Future = Async(EAsyncExecution::ThreadPool, [State, Near]() { State->Build(*State->Job, Near); });
    }
}
int32 FAC2FarTerrain::TileCount() const
{
    return Impl->Count;
}

uint32 FAC2FarTerrain::Center() const
{
    return Impl->Current;
}
int32 FAC2FarTerrain::WaterTileCount() const {return Impl->WaterCount;}
int32 FAC2FarTerrain::WaterBatchCount() const {return Impl->WaterBatches;}
void FAC2FarTerrain::SetVisible(bool Visible)
{
    if (Impl->Actor) Impl->Actor->SetActorHiddenInGame(!Visible);
}
