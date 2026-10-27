#pragma once
#include "CoreMinimal.h"

namespace AC2
{
constexpr double BlockCentimetres = 16000;
// The retail outdoor lattice has 255 by 255 blocks (00..FE on each axis).
inline bool WorldBlock(const FVector &P, uint32 &Block)
{
    if (P.ContainsNaN() || P.X < 0 || P.Y < 0 || P.X >= 255 * BlockCentimetres || P.Y >= 255 * BlockCentimetres)
        return false;
    Block = (uint32(P.X / BlockCentimetres) << 8) | uint32(P.Y / BlockCentimetres);
    return true;
}
inline FVector BlockOrigin(uint32 Block)
{
    return FVector((Block >> 8) * BlockCentimetres, (Block & 255) * BlockCentimetres, 0);
}
inline int32 BlockDistance(uint32 A, uint32 B)
{
    return FMath::Max(FMath::Abs(int32(A >> 8) - int32(B >> 8)), FMath::Abs(int32(A & 255) - int32(B & 255)));
}
inline TArray<uint32> NearbyBlocks(uint32 Center, int32 Radius)
{
    TArray<uint32> Blocks;
    Radius = FMath::Clamp(Radius, 0, 6);
    for (int32 X = FMath::Max(0, int32(Center >> 8) - Radius); X <= FMath::Min(254, int32(Center >> 8) + Radius); ++X)
        for (int32 Y = FMath::Max(0, int32(Center & 255) - Radius); Y <= FMath::Min(254, int32(Center & 255) + Radius);
             ++Y)
            Blocks.Add((X << 8) | Y);
    Blocks.Sort([Center](uint32 A, uint32 B) {
        auto D = [Center](uint32 V) {
            return FMath::Square(int32(V >> 8) - int32(Center >> 8)) +
                   FMath::Square(int32(V & 255) - int32(Center & 255));
        };
        return D(A) == D(B) ? A < B : D(A) < D(B);
    });
    return Blocks;
}
} // namespace AC2
