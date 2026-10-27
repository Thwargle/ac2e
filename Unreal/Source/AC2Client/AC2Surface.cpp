#include "AC2AssetReader.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace
{
FColor RGB565(uint16 V)
{
    const uint8 R = (V >> 11) & 31, G = (V >> 5) & 63, B = V & 31;
    return FColor((R << 3) | (R >> 2), (G << 2) | (G >> 4), (B << 3) | (B >> 2));
}
FColor Mix(const FColor &A, const FColor &B, int32 WA, int32 WB, int32 Den)
{
    return FColor((A.R * WA + B.R * WB) / Den, (A.G * WA + B.G * WB) / Den, (A.B * WA + B.B * WB) / Den);
}
} // namespace

bool DecodeAC2Surface(TConstArrayView<uint8> Bytes, uint32 Id, int32 &Width, int32 &Height, TArray<FColor> &Pixels)
{
    Pixels.Reset();
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id)
        return false;
    R.Word(); // Surface category
    Width = int32(R.Word());
    Height = int32(R.Word());
    const uint32 Format = R.Word();
    const int32 Size = R.Count(1, 64 * 1024 * 1024), Start = R.Pos;
    R.Take(Size);
    if (!R.End())
        return false;
    const uint8 *Src = R.Data.GetData() + Start;
    if (Format == 500) // JFIF surfaces may store zero dimensions in the DAT header.
    {
        auto &Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        auto JPEG = Module.CreateImageWrapper(EImageFormat::JPEG);
        if (!JPEG->SetCompressed(Src, Size))
            return false;
        Width = JPEG->GetWidth();
        Height = JPEG->GetHeight();
        if (Width <= 0 || Height <= 0 || int64(Width) * Height > 16 * 1024 * 1024)
            return false;
        TArray64<uint8> Raw;
        if (!JPEG->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() != int64(Width) * Height * 4)
            return false;
        Pixels.SetNumUninitialized(Width * Height);
        FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), Raw.Num());
        return true;
    }
    if (Width <= 0 || Height <= 0 || Width > 8192 || Height > 8192 || int64(Width) * Height > 16 * 1024 * 1024)
        return false;
    if (Format == 20 || Format == 21 || Format == 28)
    {
        const int32 Stride = Format == 20 ? 3 : Format == 21 ? 4 : 1;
        if (int64(Width) * Height * Stride != Size)
            return false;
        Pixels.SetNumUninitialized(Width * Height);
        for (FColor &P : Pixels)
        {
            P = Format == 28 ? FColor(0, 0, 0, Src[0]) : FColor(Src[2], Src[1], Src[0], Stride == 4 ? Src[3] : 255);
            Src += Stride;
        }
        return true;
    }
    const bool BC1 = Format == 0x31545844, BC2 = Format == 0x32545844 || Format == 0x33545844;
    const bool BC3 = Format == 0x34545844 || Format == 0x35545844;
    if (!BC1 && !BC2 && !BC3)
        return false;
    const int32 NX = (Width + 3) / 4, NY = (Height + 3) / 4, Stride = BC1 ? 8 : 16;
    if (int64(NX) * NY * Stride != Size)
        return false;
    Pixels.SetNumUninitialized(Width * Height);
    for (int32 BY = 0; BY < NY; ++BY)
        for (int32 BX = 0; BX < NX; ++BX, Src += Stride)
        {
            const uint8 *C = Src + (BC1 ? 0 : 8);
            const uint16 C0 = C[0] | C[1] << 8, C1 = C[2] | C[3] << 8;
            FColor Palette[4] = {RGB565(C0), RGB565(C1), FColor::Black, FColor::Transparent};
            if (C0 > C1 || !BC1)
            {
                Palette[2] = Mix(Palette[0], Palette[1], 2, 1, 3);
                Palette[3] = Mix(Palette[0], Palette[1], 1, 2, 3);
            }
            else
                Palette[2] = Mix(Palette[0], Palette[1], 1, 1, 2);
            const uint32 Indices = AC2::U32(C + 4);
            uint8 Alpha[8] = {Src[0], Src[1], 0, 0, 0, 0, 0, 255};
            uint64 AlphaIndices = 0;
            if (BC3)
            {
                const int32 N = Alpha[0] > Alpha[1] ? 7 : 5;
                for (int32 I = 1; I < N; ++I)
                    Alpha[I + 1] = ((N - I) * Alpha[0] + I * Alpha[1]) / N;
                for (int32 I = 0; I < 6; ++I)
                    AlphaIndices |= uint64(Src[I + 2]) << (I * 8);
            }
            for (int32 Y = 0; Y < 4; ++Y)
                for (int32 X = 0; X < 4; ++X)
                {
                    if (BX * 4 + X >= Width || BY * 4 + Y >= Height)
                        continue;
                    const int32 I = Y * 4 + X;
                    FColor P = Palette[(Indices >> (I * 2)) & 3];
                    if (BC2)
                        P.A = ((Src[I / 2] >> ((I & 1) * 4)) & 15) * 17;
                    if (BC3)
                        P.A = Alpha[(AlphaIndices >> (I * 3)) & 7];
                    // DXT2/4 use premultiplied RGB, while the runtime shader expects straight alpha.
                    if ((Format == 0x32545844 || Format == 0x34545844) && P.A)
                    {
                        P.R = FMath::Min(255, int32(P.R) * 255 / P.A);
                        P.G = FMath::Min(255, int32(P.G) * 255 / P.A);
                        P.B = FMath::Min(255, int32(P.B) * 255 / P.A);
                    }
                    Pixels[(BY * 4 + Y) * Width + BX * 4 + X] = P;
                }
        }
    return true;
}
