#include "AC2MapData.h"
#include "AC2AssetReader.h"
#include "AC2CharacterData.h"
#include "Misc/Paths.h"

bool FAC2MapData::Open(const FString &Directory, FString &Error)
{
    Image = 0;
    Tiles.Reset();
    Notes.Reset();
    Blocks.Reset();
    FAC2DatArchive Portal, Local;
    TArray<uint8> B;
    if (!Portal.Open(FPaths::Combine(Directory, TEXT("portal.dat")), 1, Error) ||
        !Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, Error) ||
        !Portal.Read(0x0f000000, B, Error))
        return false;
    FAC2AssetReader R(B);
    if (R.Word() != 0x0f000000)
        return false;
    R.Word();
    R.String();
    Image = R.Word();
    R.Take(24);
    uint32 Detail = 0;
    for (int32 List = 0; List < 11 && R.Good; ++List)
    {
        int32 N = R.Count(8, 4096);
        for (int32 I = 0; I < N; ++I)
        {
            uint32 Id = R.Word();
            if (List == 5 && !Detail)
                Detail = Id;
        }
        R.Take(N * 4);
        R.Align();
    }
    if (!R.End() || !Image || !Detail || !Portal.Read(Detail, B, Error))
        return false;
    FAC2AssetReader D(B);
    if (D.Word() != Detail)
        return false;
    uint32 BlockMap = D.Word();
    int32 Count = D.Count(28, 4096);
    for (int32 I = 0; I < Count && D.Good; ++I)
    {
        FAC2MapTile Tile;
        Tile.Value = D.Word();
        Tile.Image = D.Word();
        uint32 X = D.Word(), Y = D.Word(), H = D.Word(), W = D.Word();
        if (X > 255 || Y > 255 || W > 256 || H > 256 || !W || !H)
            return false;
        Tile.Cells = FIntRect(X * 8, Y * 8, (X + W) * 8, (Y + H) * 8);
        int32 N = D.Count(4, 4096);
        for (int32 J = 0; J < N; ++J)
            Tile.Notes.Add(D.Word());
        Tiles.Add(MoveTemp(Tile));
    }
    if (!D.End() || !Portal.Read(BlockMap, B, Error))
        return false;
    FAC2AssetReader Map(B);
    if (Map.Word() != BlockMap)
        return false;
    Height = Map.Word();
    Width = Map.Word();
    if (!Height || !Width || Height > 256 || Width > 256 || Width * Height > uint32(Map.Left()))
        return false;
    Blocks.Append(Map.Data.GetData() + Map.Pos, Width * Height);
    for (const auto &Tile : Tiles)
        for (uint32 Id : Tile.Notes)
            if (!Notes.Contains(Id))
            {
                if (!Portal.Read(Id, B, Error))
                    return false;
                FAC2AssetReader N(B);
                if (N.Word() != Id)
                    return false;
                uint32 Table = N.Word();
                int32 Rows = N.Count(16, 16384);
                TArray<FAC2MapNote> Entries;
                for (int32 J = 0; J < Rows && N.Good; ++J)
                {
                    uint32 Key = N.Word(), Icon = N.Word();
                    int32 X = int32(N.Word()), Y = int32(N.Word());
                    const FString Location = Coordinates(FVector2D(X, Y));
                    Entries.Add({Icon, FIntPoint(X, Y), AC2ReadUIString(Local, Table, Key, &Location)});
                }
                if (!N.End())
                    return false;
                Notes.Add(Id, MoveTemp(Entries));
            }
    return true;
}
const FAC2MapTile *FAC2MapData::At(FIntPoint Cell) const
{
    if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= LandCells || Cell.Y >= LandCells)
        return nullptr;
    uint32 X = Cell.X / 8, Y = Cell.Y / 8;
    uint32 Index = X < Width && Y < Height ? X * Height + Y : 0;
    if (!Blocks.IsValidIndex(Index))
        return nullptr;
    return Tiles.FindByPredicate([&](const auto &Tile) { return Tile.Value == Blocks[Index]; });
}
FVector2D FAC2MapData::Project(FVector2D Cell, FVector2D Origin, FVector2D Extent, FIntRect Pixels)
{
    if (Extent.X <= 0 || Extent.Y <= 0)
        return FVector2D(Pixels.Min);
    FVector2D P = (Cell - Origin) / Extent;
    return FVector2D(Pixels.Min) + FVector2D(P.X * Pixels.Width(), (1 - P.Y) * Pixels.Height());
}
FVector2D FAC2MapData::Unproject(FVector2D Pixel, FVector2D Origin, FVector2D Extent, FIntRect Pixels)
{
    if (Pixels.Width() <= 0 || Pixels.Height() <= 0)
        return Origin;
    FVector2D P = (Pixel - FVector2D(Pixels.Min)) / FVector2D(Pixels.Size());
    return Origin + FVector2D(P.X * Extent.X, (1 - P.Y) * Extent.Y);
}
FVector2D FAC2MapData::DetailOrigin(FVector2D Center)
{
    return FVector2D(FMath::Clamp(Center.X - 64., 0., double(LandCells - 128)),
                     FMath::Clamp(Center.Y - 64., 0., double(LandCells - 128)));
}
FVector2D FAC2MapData::RadarOffset(FVector Delta, float ActorYaw, float RangeMetres)
{
    // AC2's forward axis is +Y; Unreal's character forward is +X. Screen Y points down.
    const double Angle = FMath::DegreesToRadians(90. - ActorYaw);
    double C = FMath::Cos(Angle), S = FMath::Sin(Angle);
    return FVector2D(C * Delta.X - S * Delta.Y, -S * Delta.X - C * Delta.Y) *
           (60. / (100. * FMath::Max(1.f, RangeMetres)));
}
FString FAC2MapData::Coordinates(FVector2D Cell)
{
    FVector2D C = (Cell - FVector2D(1020, 1020)) / 10.2;
    return FString::Printf(TEXT("%.1f%c, %.1f%c"), FMath::Abs(C.Y), C.Y >= 0 ? TEXT('N') : TEXT('S'),
                           FMath::Abs(C.X), C.X >= 0 ? TEXT('E') : TEXT('W'));
}
