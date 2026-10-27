#pragma once
#include "AC2DatArchive.h"

// Sticky failure: a truncated/unsupported object can never advance outside its record.
struct FAC2AssetReader
{
    TArray<uint8> Data;
    int32 Pos = 0;
    bool Good = true;
    explicit FAC2AssetReader(TArray<uint8> &&In) : Data(MoveTemp(In)) {}
    explicit FAC2AssetReader(TConstArrayView<uint8> In)
    {
        Data.Append(In.GetData(), In.Num());
    }
    int32 Left() const
    {
        return Data.Num() - Pos;
    }
    bool Take(int64 N)
    {
        if (!Good || N < 0 || N > Left())
        {
            Good = false;
            return false;
        }
        Pos += int32(N);
        return true;
    }
    uint32 Word()
    {
        const int32 P = Pos;
        return Take(4) ? AC2::U32(Data.GetData() + P) : 0;
    }
    uint16 Short()
    {
        const int32 P = Pos;
        return Take(2) ? uint16(Data[P] | (Data[P + 1] << 8)) : 0;
    }
    float Float()
    {
        const uint32 W = Word();
        float F;
        FMemory::Memcpy(&F, &W, 4);
        if (!FMath::IsFinite(F))
            Good = false;
        return Good ? F : 0;
    }
    int32 Count(int32 ItemBytes = 4, uint32 Max = 65536)
    {
        const uint32 N = Word();
        if (!Good || N > Max || uint64(N) * ItemBytes > uint64(Left()))
        {
            Good = false;
            return 0;
        }
        return int32(N);
    }
    void Align()
    {
        Take((4 - (Pos & 3)) & 3);
    }
    void String(int32 CharBytes = 1)
    {
        uint32 N = Short();
        if (N == 65535)
            N = Word();
        Take(int64(N) * CharBytes);
        Align();
    }
    FVector Vector()
    {
        const float X = Float(), Y = Float(), Z = Float();
        return FVector(X, Y, Z);
    }
    FQuat Quaternion()
    {
        const float W = Float(), X = Float(), Y = Float(), Z = Float();
        FQuat Q(X, Y, Z, W);
        return Q.SizeSquared() > SMALL_NUMBER ? Q.GetNormalized() : FQuat::Identity;
    }
    bool End() const
    {
        return Good && Left() == 0;
    }
};

// CPU decode also feeds the terrain atlas; runtime textures retain authored alpha.
bool DecodeAC2Surface(TConstArrayView<uint8> Bytes, uint32 Id, int32 &Width, int32 &Height, TArray<FColor> &Pixels);
