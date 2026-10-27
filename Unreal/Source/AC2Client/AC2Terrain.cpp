#include "AC2Terrain.h"

bool FAC2LandscapeDefs::Decode(TConstArrayView<uint8> Bytes, FString &Error)
{
    *this = {};
    Error.Reset();
    // LandscapeDefs::UnPack, admin 0x00629D60. DID then 48 bytes and 256 floats.
    if (Bytes.Num() != 1076 || AC2::U32(Bytes.GetData()) != 0x36000000)
    {
        Error = TEXT("Unsupported LandscapeDefs envelope or length");
        return false;
    }
    CellSize = AC2::F32(Bytes.GetData() + 4);
    CellsPerBlock = AC2::U32(Bytes.GetData() + 8);
    Subdivisions = AC2::U32(Bytes.GetData() + 12);
    if (CellSize != 20.0f || CellsPerBlock != 8 || Subdivisions != 2)
    {
        Error = TEXT("Unsupported landscape lattice (expected 20m / 8 cells / 2 subdivisions)");
        return false;
    }
    uint64 TotalBits = 0;
    for (int32 I = 0; I < 6; ++I)
        TotalBits += AC2::U32(Bytes.GetData() + 28 + I * 4);
    const uint32 TerrainBits = AC2::U32(Bytes.GetData() + 28);
    if (!TerrainBits || TotalBits > 32)
    {
        Error = TEXT("Invalid landscape cell-info bit fields");
        return false;
    }
    TerrainMask = TerrainBits == 32 ? MAX_uint32 : (1u << TerrainBits) - 1;
    for (int32 I = 0; I < 256; ++I)
    {
        const float Height = AC2::F32(Bytes.GetData() + 52 + I * 4);
        if (!FMath::IsFinite(Height) || FMath::Abs(Height) > 100000)
        {
            Error = TEXT("Invalid landscape height table");
            Heights.Reset();
            return false;
        }
        Heights.Add(Height);
    }
    return true;
}

bool FAC2LandBlock::Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes, FString &Error)
{
    *this = {};
    Error.Reset();
    // CLandBlockData::StreamPack, admin 0x0048F310 / client0930 0x004916A0.
    // Counted bytes are NOT aligned before the following count. Alignment is at the end.
    if (Bytes.Num() != 1468 || AC2::U32(Bytes.GetData()) != ExpectedId || (ExpectedId & 0xFFFF) != 0xFFFF ||
        AC2::U32(Bytes.GetData() + 12) != 289 || AC2::U32(Bytes.GetData() + 305) != 289)
    {
        Error = FString::Printf(TEXT("Unsupported CLandBlockData layout for 0x%08X"), ExpectedId);
        return false;
    }
    Id = ExpectedId;
    InfoId = AC2::U32(Bytes.GetData() + 4);
    PathId = AC2::U32(Bytes.GetData() + 8);
    Heights.Append(Bytes.GetData() + 16, 289);
    for (int32 I = 0; I < 289; ++I)
        CellInfo.Add(AC2::U32(Bytes.GetData() + 309 + I * 4));
    return true;
}

FAC2TerrainMesh BuildAC2TerrainMesh(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block)
{
    FAC2TerrainMesh Mesh;
    if (Defs.Heights.Num() != 256 || Block.Heights.Num() != 289 || Block.CellInfo.Num() != 289)
        return Mesh;
    const float Spacing = Defs.CellSize / Defs.Subdivisions * 100.0f;
    for (int32 X = 0; X < 17; ++X)
    {
        for (int32 Y = 0; Y < 17; ++Y)
        {
            Mesh.Vertices.Add(FVector(X * Spacing, Y * Spacing, Defs.Heights[Block.Heights[X * 17 + Y]] * 100.0f));
            Mesh.UVs.Add(FVector2D(X / 16.0, Y / 16.0));
        }
    }
    const uint32 BlockX = Block.Id >> 24, BlockY = (Block.Id >> 16) & 255;
    for (int32 X = 0; X < 16; ++X)
    {
        for (int32 Y = 0; Y < 16; ++Y)
        {
            const int32 A = X * 17 + Y, B = A + 17, C = A + 1, D = B + 1;
            // CacheFarValues: polygons touching terrain type 0x1E are cave mouths.
            if ((Block.CellInfo[A] & Defs.TerrainMask) == 0x1E || (Block.CellInfo[B] & Defs.TerrainMask) == 0x1E ||
                (Block.CellInfo[C] & Defs.TerrainMask) == 0x1E || (Block.CellInfo[D] & Defs.TerrainMask) == 0x1E)
                continue;
            // CLandBlockData::CachePackValues deterministic diagonal, uint32 wrapping.
            const uint32 GX = BlockX * 16 + X, GY = BlockY * 16 + Y;
            const uint32 Seed = (GY * GX * GX * 0x39557FCFu - 0x4A6506DBu) * GY - GX * 0x6B3C5F11u + 0xAE226CEBu;
            // UE front faces use clockwise winding in this axis mapping.
            if (Seed & 0x80000000u)
                Mesh.Indices.Append({A, D, B, A, C, D});
            else
                Mesh.Indices.Append({A, C, B, B, C, D});
        }
    }
    Mesh.Normals.Init(FVector::ZeroVector, Mesh.Vertices.Num());
    for (int32 I = 0; I < Mesh.Indices.Num(); I += 3)
    {
        const int32 A = Mesh.Indices[I], B = Mesh.Indices[I + 1], C = Mesh.Indices[I + 2];
        const FVector N =
            FVector::CrossProduct(Mesh.Vertices[C] - Mesh.Vertices[A], Mesh.Vertices[B] - Mesh.Vertices[A]);
        Mesh.Normals[A] += N;
        Mesh.Normals[B] += N;
        Mesh.Normals[C] += N;
    }
    for (FVector &N : Mesh.Normals)
        N = N.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    return Mesh;
}
