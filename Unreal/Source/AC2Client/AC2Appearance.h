#pragma once
#include "CoreMinimal.h"

// AppearanceProfile / AppliedAppearanceKey from the native character matrix.
struct FAC2AppearanceKey
{
    uint32 Table = 0, Key = 0;
    float Value = 0;
};
using FAC2Appearance = TArray<FAC2AppearanceKey>;

// Legions OpacityAtom::Replaces / AtomCollection::UpdateAtomCollection.
// Equal priority replaces the previous atom; -999 means the applied scalar.
struct FAC2AppearanceOpacity
{
    float Priority = -MAX_flt;
    float Value = 1.f;
    bool Defined = false;
    void Apply(float InPriority, float Opacity, float Scalar)
    {
        if (!FMath::IsFinite(InPriority) || !FMath::IsFinite(Opacity) || !FMath::IsFinite(Scalar)) return;
        if (!Defined || InPriority >= Priority)
        {
            Priority = InPriority;
            Value = FMath::Clamp(Opacity == -999.f ? Scalar : Opacity, 0.f, 1.f);
            Defined = true;
        }
    }
};
