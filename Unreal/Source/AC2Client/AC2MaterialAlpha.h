#pragma once
#include "CoreMinimal.h"

// D3D fixed-function alpha operations from MaterialLayer::StreamPack.
// Colour textures can contain specular masks: their alpha is not automatically opacity.
struct FAC2AlphaStage
{
    uint32 Operation = 1, Argument1 = 1, Argument2 = 1;
};
inline float EvaluateAC2Alpha(TConstArrayView<FAC2AlphaStage> Stages, float TextureAlpha, float DiffuseAlpha = 1,
                              float FactorAlpha = 1)
{
    float Current = DiffuseAlpha;
    for (const auto &Stage : Stages)
    {
        if (Stage.Operation == 1)
            break;
        auto Argument = [&](uint32 Arg) {
            float V = (Arg & 15) == 0   ? DiffuseAlpha
                      : (Arg & 15) == 2 ? TextureAlpha
                      : (Arg & 15) == 3 ? FactorAlpha
                                        : Current;
            return (Arg & 16) ? 1 - V : V;
        };
        const float A = Argument(Stage.Argument1), B = Argument(Stage.Argument2);
        switch (Stage.Operation)
        {
        case 2:
            Current = A;
            break;
        case 3:
            Current = B;
            break;
        case 4:
            Current = A * B;
            break;
        case 5:
            Current = 2 * A * B;
            break;
        case 6:
            Current = 4 * A * B;
            break;
        case 7:
            Current = A + B;
            break;
        case 8:
            Current = A + B - .5f;
            break;
        case 9:
            Current = 2 * (A + B - .5f);
            break;
        case 10:
            Current = A - B;
            break;
        case 11:
            Current = A + B - A * B;
            break;
        case 12:
            Current = FMath::Lerp(B, A, DiffuseAlpha);
            break;
        case 13:
            Current = FMath::Lerp(B, A, TextureAlpha);
            break;
        case 14:
            Current = FMath::Lerp(B, A, FactorAlpha);
            break;
        case 15:
            Current = A + B * (1 - TextureAlpha);
            break;
        case 16:
            Current = FMath::Lerp(B, A, Current);
            break;
        default:
            break;
        }
        Current = FMath::Clamp(Current, 0.f, 1.f);
    }
    return Current;
}
