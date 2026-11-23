#pragma once
#include "CoreMinimal.h"

// Native models face +Y. The head sliders are raw input to the weighted AMLook
// graph. Target height already includes the actor's scale.
namespace AC2Look
{
// LookAtLogic aims at three quarters of a creature's scaled height and the
// midpoint of other objects. Feet must be the physics origin, not mesh bounds
// that expand and contract with animation.
inline FVector TargetPoint(const FVector& Feet,double ScaledHeight,bool Creature)
{
    return Feet+FVector(0,0,FMath::IsFinite(ScaledHeight)?FMath::Max(0.,ScaledHeight)*(Creature?.75:.5):0.);
}

inline FVector At(const FTransform& Body,const FVector& Eye,const FVector& Target)
{
    const FVector D=Body.InverseTransformVectorNoScale(Target-Eye);
    if(D.ContainsNaN() || D.IsNearlyZero())return FVector::ZeroVector;
    const double Range=D.Size2D();
    return FVector(Range>0?D.Z/Range:0.,0,FMath::Atan2(FMath::Max(0.,D.Y),D.X)-UE_HALF_PI);
}

// LookAtLogic measures horizontal range in model space, not eye-to-target 3D
// distance. A player relinquishes a target only after >2 seconds behind/out of
// range. This timer does not clear the selected combat/interaction target.
struct FInterest
{
    float Disinterested=0;
    bool Update(const FVector& LocalTarget,float Delta)
    {
        if(LocalTarget.ContainsNaN())return true;
        if(LocalTarget.Y<0 || LocalTarget.Size2D()>1000)
        {
            if(FMath::IsFinite(Delta) && Delta>0)Disinterested+=Delta;
        }
        else Disinterested=0;
        return Disinterested>2.f;
    }
};

// GetInterpViewerOffset describes a displacement in metres. Camera FK uses its
// vertical component directly, including the magnitude of the camera orbit.
inline FVector Camera(const FTransform& Body,const FVector& CameraToFocusMetres)
{
    const FVector D=Body.InverseTransformVectorNoScale(CameraToFocusMetres);
    if(D.ContainsNaN())return FVector::ZeroVector;
    double Yaw=D.Size2D()<.0002?0.:FMath::Atan2(-D.X,D.Y);
    double Pitch=D.Z;
    // LookAtManager::Update (0930 0x50769A): the positive side has a
    // transition band. The negative side changes hemisphere at -Lower.
    constexpr double Lower=1.4707963f,Upper=1.6707964f;
    if(Yaw>Lower && Yaw<Upper)
    {
        const double Factor=Yaw*.63661975f-1.5707964f;
        Pitch*=Factor;Yaw*=Factor;
    }
    else if(Yaw>Upper){Pitch=-Pitch;Yaw-=UE_PI;}
    else if(Yaw<-Lower){Pitch=-Pitch;Yaw+=UE_PI;}
    const FVector Scale=Body.GetScale3D();
    return FVector(Scale.Z!=0?Pitch/Scale.Z:0.,0,Scale.X!=0?Yaw/Scale.X:0.);
}

// Native SliderTracker retargets a fixed-speed ramp only when its endpoint
// changes. An unchanged endpoint must be reached in the original duration.
struct FSlider
{
    double Value=0,Goal=0,Velocity=0;
    void Set(double Position){Value=Goal=Position;Velocity=0;}
    void Move(double Position,double Duration,double Delta)
    {
        if(FMath::Abs(Position-Goal)>=.0002){Goal=Position;Velocity=(Goal-Value)/Duration;}
        Value=Velocity>0?FMath::Min(Goal,Value+Velocity*Delta):FMath::Max(Goal,Value+Velocity*Delta);
    }
};

struct FPose
{
    FSlider Pitch,Yaw;
    FString Target;
    FVector Previous=FVector::ZeroVector,LastAim=FVector::ZeroVector;
    double Elapsed=1,Weight=0;

    FVector Value() const{return FVector(Pitch.Value,0,Yaw.Value);}

    FVector Aim(const FString& Id,const FVector& Raw,double Delta)
    {
        if(Id!=Target){Target=Id;Previous=Value();Elapsed=0;Weight=0;}
        Elapsed=FMath::Min(.5,Elapsed+FMath::Max(0.,Delta));
        Weight=Elapsed*2;
        LastAim=Raw;
        // LookAtLogic (0930 0x50732B) divides height by range * weight.
        // Only the yaw slider linearly blends the target angle.
        Pitch.Set(Previous.X*(1.-Weight)+(Weight>0?Raw.X/Weight:0.));
        Yaw.Set(FMath::Lerp(Previous.Z,Raw.Z,Weight));
        return Value();
    }

    FVector Direction(const FVector& Raw,double Delta,bool Local)
    {
        Delta=FMath::Max(0.,Delta);
        if(!Target.IsEmpty()){Target.Reset();Weight=0;}
        Elapsed=FMath::Min(1.,Elapsed+Delta);
        if(Elapsed<.5)Weight=Elapsed*2;
        else if(Elapsed>.5)Weight=(1.-Elapsed)*2;
        else Weight=FMath::Min(1.,Weight+Delta*2);
        const FVector Goal=Local?FMath::Lerp(Raw,LastAim,Weight):Raw;
        const auto Duration=[Local](double Difference){return Local?(FMath::Abs(Difference)>.2?.2:.1):.5;};
        Pitch.Move(Goal.X,Duration(Goal.X-Pitch.Value),Delta);
        Yaw.Move(Goal.Z,Duration(Goal.Z-Yaw.Value),Delta);
        return Value();
    }
};

// PlayerMovementModule reports changed head sliders only when not looking at
// an object, more than half a second after the previous direction report.
struct FReporter
{
    FVector Last=FVector::ZeroVector;
    double Sent=0;
    bool Update(double Now,bool HasTarget,const FVector& Pose)
    {
        if(!FMath::IsFinite(Now) || Pose.ContainsNaN() || HasTarget)return false;
        if(Now<Sent){Sent=Now;Last=FVector::ZeroVector;return false;}
        if(Now-Sent<=.5 || (FMath::Abs(Pose.X-Last.X)<=.0002 && FMath::Abs(Pose.Z-Last.Z)<=.0002))return false;
        Sent=Now;Last=Pose;return true;
    }
};
}
