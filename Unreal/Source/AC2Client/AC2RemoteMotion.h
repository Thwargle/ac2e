#pragma once
#include "CoreMinimal.h"

// One packet interval of presentation latency, without exponential easing at
// every update. A constant server velocity should produce a constant gait.
struct FAC2RemoteMotion
{
    FVector From=FVector::ZeroVector,To=FVector::ZeroVector;
    double Started=0,LastPacket=-1,Interval=.1;
    bool Active=false;
    void Push(FVector Current,FVector Goal,double Now)
    {
        const double Gap=LastPacket<0?.1:Now-LastPacket;
        const bool Snap=Gap>.5 || FVector::DistSquared(Current,Goal)>FMath::Square(1000.);
        if(Gap>.01 && Gap<=.5)Interval=FMath::Lerp(Interval,Gap,.25);
        From=Snap?Goal:Current;To=Goal;Started=Now;LastPacket=Now;Active=true;
    }
    FVector Sample(double Now,FVector Default) const
    {
        return Active?FMath::Lerp(From,To,FMath::Clamp((Now-Started)/FMath::Clamp(Interval,.05,.25),0.,1.)):Default;
    }
};
