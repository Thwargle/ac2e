#pragma once
#include "CoreMinimal.h"

struct FAC2CreaturePreview
{
    uint32 Visual;
    const TCHAR *Name;
    uint32 SourceEntity;
};
// Legions 7160 NPC templates' PhysObj properties resolve to these VisualDesc
// resources. These are preview species, not authored encounter locations.
inline constexpr FAC2CreaturePreview AC2CreaturePreviews[] = {
    {0x1f000036, TEXT("Drudge"), 0x4700053f},     {0x1f00061b, TEXT("Shreth"), 0x47000f8b},
    {0x1f0004a9, TEXT("Reedshark"), 0x47000810},  {0x1f000031, TEXT("Beetle"), 0x4700088c},
    {0x1f000075, TEXT("Banderling"), 0x470007a4},
};
