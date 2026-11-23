#pragma once
#include "CoreMinimal.h"

// GameplayOptionsProfile presence bits; omitted fields are not zero values.
struct FAC2GameOptions
{
    static constexpr uint32 All=0x10a8;
    uint32 Fields=0,Bits=0,Radar=0,Height=1;
    float Range=-1;
};
