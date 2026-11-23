#pragma once
#include "CoreMinimal.h"
#include "AC2Particle.h"

// LightSourceFX::UnPack and CPhysicsObjRep's queued-light lifetime envelope.
// Lifespan zero is persistent; FadeTime applies at both ends when they do not overlap.
struct FAC2EffectLight
{
    bool Enabled=false;
    float Life=0,Fade=0;
    FAC2Wave Range{1,5},Intensity{1,.5f};
    FColor Color=FColor::White;
    float Envelope(float Age) const
    {
        if(Life<=0)return 1;
        if(Age>=Life)return 0;
        const float In=FMath::Min(Life,Fade),Out=FMath::Max(In,Life-Fade);
        if(Fade>.0002f && In<Out)
        {
            if(Age<In)return FMath::Max(0.f,Age)/In;
            if(Age>Out)return (Life-Age)/(Life-Out);
        }
        return 1;
    }
};
