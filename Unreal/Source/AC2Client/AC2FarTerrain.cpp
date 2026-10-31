#include "AC2FarTerrain.h"
#include "AC2WorldGrid.h"
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
    void Build(FJob &J, int32 Near)
    {
        J.Pixels.Init(FColor::White, AtlasSize * AtlasSize);
        FAC2DatArchive Cell;
        FAC2DatArchive Portal;
        FAC2WaterSource Water;
        FString Error;
        TArray<uint8> B;
        if (!Cell.Open(FPaths::Combine(Directory, TEXT("cell_1.dat")), 2, Error) ||
            !Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) || !Water.Load(Portal, Error))
            return;
        const FVector Origin = AC2::BlockOrigin(J.Center);
        for (int32 X = FMath::Max(0, int32(J.Center >> 8) - FarRadius);
             X <= FMath::Min(254, int32(J.Center >> 8) + FarRadius); ++X)
            for (int32 Y = FMath::Max(0, int32(J.Center & 255) - FarRadius);
                 Y <= FMath::Min(254, int32(J.Center & 255) + FarRadius); ++Y)
            {
                uint32 Id = (X << 8) | Y;
                if (AC2::BlockDistance(Id, J.Center) <= Near)
                    continue;
                FAC2LandBlock Land;
                if (!Cell.Read((Id << 16) | 0xffff, B, Error) || !Land.Decode((Id << 16) | 0xffff, B, Error))
                    continue;
                ++J.Tiles;
                const FVector Offset = AC2::BlockOrigin(Id) - Origin;
                const int32 TX = X - int32(J.Center >> 8) + FarRadius;
                const int32 TY = Y - int32(J.Center & 255) + FarRadius;
                constexpr int32 Gutter = 2, ContentPixels = TilePixels - Gutter * 2;
                const auto NativeMesh = BuildAC2TerrainMesh(Defs, Land);
                const auto Pixels = Assets->TerrainPixels(Defs, Land, NativeMesh, ContentPixels);
                for (int32 V = 0; V < TilePixels; ++V)
                    for (int32 U = 0; U < TilePixels; ++U)
                        J.Pixels[(TY * TilePixels + V) * AtlasSize + TX * TilePixels + U] =
                            Pixels[FMath::Clamp(V-Gutter,0,ContentPixels-1)*ContentPixels+FMath::Clamp(U-Gutter,0,ContentPixels-1)];
                const int32 Base = J.Vertices.Num();
                // 8x8 quads nearby and 4x4 beyond 1.3 km. Full-resolution edge samples
                // are unnecessary because adjoining far blocks use a short skirt.
                const auto W = Water.At(Defs, Land);
                // Coarse diagonals can put the shoreline below the water plane,
                // erasing narrow banks. Preserve the authored shore triangles.
                int32 Step = W.Present ? 1 : (AC2::BlockDistance(Id, J.Center) < 8 ? 2 : 4);
                const int32 Row = 16 / Step + 1;
                for (int32 I = 0; I <= 16; I += Step)
                    for (int32 K = 0; K <= 16; K += Step)
                    {
                        J.Vertices.Add(Offset +
                                       FVector(I * 1000, K * 1000, Defs.Heights[Land.Heights[I * 17 + K]] * 100));
                        int32 L = FMath::Max(0, I - 1), R = FMath::Min(16, I + 1), D = FMath::Max(0, K - 1),
                              U = FMath::Min(16, K + 1);
                        double DX = (Defs.Heights[Land.Heights[R * 17 + K]] - Defs.Heights[Land.Heights[L * 17 + K]]) /
                                    (10 * (R - L)),
                               DY = (Defs.Heights[Land.Heights[I * 17 + U]] - Defs.Heights[Land.Heights[I * 17 + D]]) /
                                    (10 * (U - D));
                        J.Normals.Add(FVector(-DX, -DY, 1).GetSafeNormal());
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
                if (W.Present)
                {
                    int32 N = J.Vertices.Num();
                    float Z = W.Height * 100;
                    J.Vertices.Append({Offset + FVector(0, 0, Z), Offset + FVector(16000, 0, Z),
                                       Offset + FVector(0, 16000, Z), Offset + FVector(16000, 16000, Z)});
                    J.Indices.Append({N, N + 2, N + 1, N + 1, N + 2, N + 3});
                    for (int32 I = 0; I < 4; ++I)
                    {
                        J.Normals.Add(FVector::UpVector);
                        // Centre tile is omitted (near terrain), leaving a white texel for water tint.
                        J.UVs.Add(FVector2D(.5f, .5f));
                        J.Colors.Add(FColor(20, 50, 65));
                    }
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
            C->SetMaterial(0, Material);
            Impl->Current = Id;
            Impl->Count = J.Tiles;
            UE_LOG(LogTemp, Display, TEXT("AC2 distant terrain: %d tiles, %d triangles"), J.Tiles, J.Indices.Num() / 3);
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
void FAC2FarTerrain::SetVisible(bool Visible)
{
    if (Impl->Actor) Impl->Actor->SetActorHiddenInGame(!Visible);
}
