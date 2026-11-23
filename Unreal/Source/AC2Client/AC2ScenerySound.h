#pragma once
#include "CoreMinimal.h"

// Startup FX sounds belong to the placed object, independently of its particle
// budget. Positions are block-local until the world streamer supplies its origin.
struct FAC2ScenerySound
{
    uint64 Key=0;
    uint32 Sound=0, Cell=0;
    FVector Position=FVector::ZeroVector;
    float Volume=1;
    uint32 Channel=0;
    bool DayTime=true, NightTime=true;
    TMap<uint32,uint32> Channels;
};
