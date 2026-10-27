#pragma once
#include "CoreMinimal.h"

// InitializeMissile presentation only. Hits and damage remain server-owned.
namespace AC2Projectile
{
inline FQuat Facing(const FVector& Velocity)
{
    // Native models face +Y. Keep pitch as well as yaw for rising/falling shots.
    return Velocity.IsNearlyZero()?FQuat::Identity:
        Velocity.Rotation().Quaternion()*FQuat(FVector::UpVector,-HALF_PI);
}
inline bool Valid(const FVector& Position,const FVector& Velocity,const FVector& Offset)
{
    return !Position.ContainsNaN() && !Velocity.ContainsNaN() && !Offset.ContainsNaN()
        && Velocity.SizeSquared()<=FMath::Square(100000.) && Offset.SizeSquared()<=FMath::Square(100000000.);
}
inline FVector Advance(const FVector& Position,const FVector& Velocity,const FVector* Target,float Delta,bool& Arrived)
{
    Arrived=false;
    if(!FMath::IsFinite(Delta) || Delta<=0)return Position;
    if(!Target)return Position+Velocity*Delta;
    const FVector Difference=*Target-Position;
    const double Distance=Difference.Size(),Step=Velocity.Size()*Delta;
    Arrived=Distance<=FMath::Max(Step,5.);
    return Arrived?*Target:Position+Difference.GetSafeNormal()*Step;
}
}
