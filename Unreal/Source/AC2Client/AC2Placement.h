#pragma once
#include "CoreMinimal.h"
class UWorld;
class AActor;
class UPrimitiveComponent;

namespace AC2
{
// GroundHint is a point on a known terrain/cell floor, in centimetres. Never
// searches through roofs or stacked dungeon floors with an unbounded vertical ray.
bool StandingPosition(UWorld *World, const FVector &GroundHint, float Radius, float HalfHeight, FVector &Position,
                      const AActor *Ignore = nullptr, const UPrimitiveComponent *Support = nullptr);
bool CapsuleClear(UWorld *World, const FVector &Position, float Radius, float HalfHeight,
                  const AActor *Ignore = nullptr);
} // namespace AC2
