#pragma once
#include "CoreMinimal.h"
class UWorld;
class AActor;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
void ExcludeFromAC2WaterReflection(UWorld* World,UPrimitiveComponent* Surface);
void RegisterAC2WaterPlane(UWorld* World,AActor* Owner,uint32 Block,double WorldHeight);
void RegisterAC2WaterReflection(UWorld* World,UMaterialInstanceDynamic* Material);
void UpdateAC2WaterReflection(UWorld* World);
void ReleaseAC2WaterReflection(UWorld* World);
