#include "AC2CharacterMovement.h"
#include "AC2Locomotion.h"
#include "GameFramework/Character.h"
void UAC2CharacterMovement::CalcVelocity(float Dt,float Friction,bool Fluid,float Braking)
{
    if(!IsMovingOnGround()||!CharacterOwner){Super::CalcVelocity(Dt,Friction,Fluid,Braking);return;}
    // CMotionInterp approaches each authored local velocity independently, using
    // stop/brake acceleration on a zero or reversing target. UE resolves contacts.
    const FQuat Rotation=CharacterOwner->GetActorQuat();
    const FVector Input=Rotation.UnrotateVector(Acceleration/FMath::Max(1.f,GetMaxAcceleration())).GetClampedToMaxSize(1);
    FVector Local=Rotation.UnrotateVector(Velocity);
    auto Approach=[Dt](double V,double Target,float Accel,float Brake){
        float Rate=(FMath::IsNearlyZero(Target)||V*Target<0)?Brake:Accel;
        return FMath::FInterpConstantTo(V,Target,Dt,FMath::Max(0.f,Rate)*100);
    };
    // Landing can call CalcVelocity before the actor's next Tick updates Native.
    // Air values often have zero forward speed; using them here kills momentum.
    Local.X=Approach(Local.X,Input.X*(Input.X<0?GroundNative.Backward:GroundNative.Forward)*100*Pace,
        Input.X<0?GroundNative.BackwardAccel:GroundNative.ForwardAccel,Local.X<0?GroundNative.BackwardBrake:GroundNative.ForwardBrake);
    Local.Y=Approach(Local.Y,Input.Y*GroundNative.Strafe*100*Pace,GroundNative.StrafeAccel,GroundNative.StrafeBrake);
    Local.Z=0;Velocity=Rotation.RotateVector(Local);
}
FVector UAC2CharacterMovement::NewFallVelocity(const FVector& Initial,const FVector& Gravity,float Dt) const
{
    return Super::NewFallVelocity(Initial,Native.Gravity?Gravity:FVector::ZeroVector,Dt)
        .GetClampedToMaxSize(FMath::Max(1.f,Native.Terminal)*100);
}
void UAC2CharacterMovement::PhysCustom(float Dt,int32 Iterations)
{
    if(CustomMovementMode!=1 || !UpdatedComponent){Super::PhysCustom(Dt,Iterations);return;}
    float Remaining=Dt;
    while(Remaining>=MIN_TICK_TIME && Iterations++<MaxSimulationIterations)
    {
        const float Step=GetSimulationTimeStep(Remaining,Iterations);Remaining-=Step;
        const FVector Start=UpdatedComponent->GetComponentLocation();
        FVector Input=Acceleration.GetClampedToMaxSize(Native.ForwardAccel*100);
        Input.Z=FMath::Clamp(Acceleration.Z/FMath::Max(1.f,MaxAcceleration),-1.,1.)*Native.VerticalAccel*100;
        const double Depth=(WaterLevel-(Start.Z+WaterDepthOffset))/100.;
        const double Lift=AC2Locomotion::WaterLift(Depth,Native,GetGravityZ());
        Velocity+=(Input+FVector(0,0,Lift+(Native.Gravity?GetGravityZ():0)))*Step;
        Velocity=AC2Locomotion::Friction(Velocity,Input,Native.Drag,Step);
        const FVector Horizontal=FVector(Velocity.X,Velocity.Y,0).GetClampedToMaxSize(Native.Forward*100);
        Velocity.X=Horizontal.X;Velocity.Y=Horizontal.Y;
        if(Input.Z!=0)Velocity.Z=FMath::Clamp(Velocity.Z,-Native.Vertical*100.,Native.Vertical*100.);
        Velocity=Velocity.GetClampedToMaxSize(FMath::Max(1.f,Native.Terminal)*100);
        FHitResult Hit;SafeMoveUpdatedComponent(Velocity*Step,UpdatedComponent->GetComponentQuat(),true,Hit);
        if(Hit.IsValidBlockingHit())SlideAlongSurface(Velocity*Step,1-Hit.Time,Hit.Normal,Hit,true);
        if(Step>0)Velocity=(UpdatedComponent->GetComponentLocation()-Start)/Step;
    }
    UpdateComponentVelocity();
}
