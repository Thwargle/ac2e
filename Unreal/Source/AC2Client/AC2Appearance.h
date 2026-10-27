#pragma once
#include "CoreMinimal.h"

// AppearanceProfile / AppliedAppearanceKey from the native character matrix.
struct FAC2AppearanceKey
{
    uint32 Table = 0, Key = 0;
    float Value = 0;
};
using FAC2Appearance = TArray<FAC2AppearanceKey>;
