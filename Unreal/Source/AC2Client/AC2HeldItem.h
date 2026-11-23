#pragma once
#include "CoreMinimal.h"

// Legions CPhysicsObj::UpdateChild (004D83D0): parent scale moves the
// holding point, but the child keeps its own scale. Height/build must not
// stretch a javelin or drum after placing it at the animated socket.
inline FTransform AC2HeldItemTransform(const FTransform &WorldSocket, const FVector &ItemScale) {
  return FTransform(WorldSocket.GetRotation(), WorldSocket.GetLocation(), ItemScale);
}
