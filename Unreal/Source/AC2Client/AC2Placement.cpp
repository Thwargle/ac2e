#include "AC2Placement.h"
#include "Engine/World.h"

bool AC2::CapsuleClear(UWorld *World, const FVector &Position, float Radius, float HalfHeight, const AActor *Ignore)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AC2LandingClearance), false, Ignore);
    return World && !World->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_Pawn,
                                                         FCollisionShape::MakeCapsule(Radius, HalfHeight), Q);
}

bool AC2::StandingPosition(UWorld *World, const FVector &GroundHint, float Radius, float HalfHeight, FVector &Position,
                           const AActor *Ignore, const UPrimitiveComponent *Support)
{
    if (!World || GroundHint.ContainsNaN() || Radius <= 0 || HalfHeight < Radius)
        return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AC2LandingFloor), false, Ignore);
    FHitResult Floor;
    constexpr float WalkableZ = .57357644f; // CharacterMovement's 55 degree limit.
    if (!World->LineTraceSingleByChannel(Floor, GroundHint + FVector(0, 0, 30), GroundHint - FVector(0, 0, 60),
                                         ECC_Pawn, Q) ||
        Floor.bStartPenetrating || Floor.ImpactNormal.Z < WalkableZ || (Support && Floor.GetComponent() != Support))
        return false;
    // A small lift works in low ceilings; larger lifts allow the curved capsule
    // bottom to clear slopes. A tree/wall overlapping the capsule rejects this spot.
    for (float Lift : {4.f, 12.f, 24.f, 48.f})
    {
        const FVector Start = Floor.ImpactPoint + FVector(0, 0, HalfHeight + Lift);
        if (!CapsuleClear(World, Start, Radius, HalfHeight, Ignore))
            continue;
        FHitResult Hit;
        if (!World->SweepSingleByChannel(Hit, Start, Floor.ImpactPoint + FVector(0, 0, HalfHeight - 60),
                                         FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight),
                                         Q) ||
            Hit.bStartPenetrating || Hit.ImpactNormal.Z < WalkableZ || (Support && Hit.GetComponent() != Support))
            continue;
        const FVector Candidate = Hit.Location + FVector(0, 0, 3);
        if (CapsuleClear(World, Candidate, Radius + .5f, HalfHeight + .5f, Ignore))
        {
            Position = Candidate;
            return true;
        }
    }
    return false;
}
