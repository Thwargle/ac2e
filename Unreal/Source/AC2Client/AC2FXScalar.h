#pragma once
#include "CoreMinimal.h"

// FXManager::UseTime advances a scalar by quantum / ramp_time. This is a
// constant rate in scalar units, not an exponential interpolation or a new
// fixed-duration fade each time the desired value changes.
struct FAC2FXScalar
{
    float Current=0, Desired=0, Ramp=0;
    void Start(float Value,float Seconds,bool Startup=false)
    {
        Desired=Value;Ramp=Seconds;
        Current=Startup || Ramp<.0002f?Desired:0.f;
    }
    void Set(float Value)
    {
        Desired=Value;
        if(Ramp<.0002f)Current=Desired;
    }
    bool Advance(float Delta)
    {
        const float Before=Current;
        if(FMath::IsFinite(Delta) && Delta>0)
            Current=Ramp<.0002f?Desired:FMath::FInterpConstantTo(Current,Desired,Delta,1.f/Ramp);
        return Before!=Current;
    }
};
