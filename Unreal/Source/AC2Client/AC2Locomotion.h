#pragma once
#include "AC2MotionData.h"

// Presentation uses each body's replicated feet and its own native water thresholds.
// No local-player mode is copied onto remote bodies.
namespace AC2Locomotion
{
inline uint32 AnimationMode(uint32 Element,uint32 RequestedMode)
{
    // Unmounted avatars use the ordinary water branch, even if the server's
    // last stance is armed. Keep death authoritative and restore that stance
    // when leaving the water. Do not substitute the native riding mode here.
    return Element==AC2Motion::Water && RequestedMode!=AC2Motion::Dead
        ? AC2Motion::Peace : RequestedMode;
}
inline uint32 Element(bool HasWater,double WaterAboveFeet,double Height,const FAC2MotionData& Motion,
                      bool Grounded,uint32 Previous)
{
    Height=FMath::Max(1.,Height);
    if(HasWater && WaterAboveFeet>Height*Motion.Water-(Previous==AC2Motion::Water?30.:0.))return AC2Motion::Water;
    if(Grounded)return HasWater&&WaterAboveFeet>Height*Motion.SeaFloor?AC2Motion::SeaFloor:AC2Motion::Ground;
    return AC2Motion::Air;
}
// CPhysicsObj::CalcAcceleration / CalcFriction, in Unreal centimetres.
inline double WaterLift(double Depth,const FAC2MotionValues& Values,double Gravity)
{
    return Depth>1.?Values.Lift*100.:Depth>-1.&&Depth<1.?Values.Lift*Depth*100.:Gravity;
}
inline FVector Friction(FVector Velocity,FVector Acceleration,float Drag,float Dt)
{
    const FVector Direction=Acceleration.GetSafeNormal();
    const FVector Along=Direction*FMath::Max(0.,FVector::DotProduct(Velocity,Direction));
    return Along+(Velocity-Along)*FMath::Pow(FMath::Clamp(1.-double(Drag),0.,1.),double(Dt));
}
}
