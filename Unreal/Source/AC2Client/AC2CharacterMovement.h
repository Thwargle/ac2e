#pragma once
#include "GameFramework/CharacterMovementComponent.h"
#include "AC2MotionData.h"
#include "AC2CharacterMovement.generated.h"
// Native movement values and water forces; Unreal supplies swept capsule contacts.
UCLASS()
class UAC2CharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    FAC2MotionValues Native;
    double WaterLevel=0;
    float WaterDepthOffset=0;
    float Pace=1;
    virtual void CalcVelocity(float DeltaTime,float Friction,bool Fluid,float Braking) override;
    virtual FVector NewFallVelocity(const FVector& Initial,const FVector& Gravity,float DeltaTime) const override;
    virtual void PhysCustom(float DeltaTime,int32 Iterations) override;
};
