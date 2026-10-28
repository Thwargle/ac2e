#pragma once
#include "CoreMinimal.h"

// Cutout RGB must not average transparent atlas padding into visible leaves.
// Preserve silhouette coverage at the shader's alpha-test threshold across mips.
namespace AC2TextureMips
{
float Coverage(TConstArrayView<FColor> Pixels, float Threshold=.4f);
TArray<FColor> Downsample(TConstArrayView<FColor> Pixels,int32 Width,int32 Height,bool Cutout=false);
void PreserveCoverage(TArray<FColor>& Pixels,float Target,float Threshold=.4f);
}
