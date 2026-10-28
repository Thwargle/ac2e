#pragma once
#include "CoreMinimal.h"
#include "AC2PreviewLighting.h"
#include "AC2MaterialAlpha.h"
class UWorld;
class USceneComponent;
class UMaterialInstanceDynamic;
void RegisterAC2WorldMaterial(UMaterialInstanceDynamic* Material,USceneComponent* Component,bool Baked);
void UpdateAC2WorldLighting(UWorld* World,const FAC2PreviewLighting& Outdoor,
    TFunctionRef<bool(const FVector&,FAC2PreviewLighting&)> Interior);

// RenderMaterial::GetSubObjects and RenderVertexBuffer::RenderIndexedUsingMaterial.
// High-detail fixed-function is a retail-authored fallback for programmable passes.
inline bool AC2MaterialLayerEnabled(uint32 On, uint32 Off, uint32 Hardware, uint32 Detail=2, bool LinearFog=true)
{
    const uint32 Active=(0x100000u<<(Hardware-1)) | (0x10000u<<Detail) | (LinearFog?0x80000u:0u);
    constexpr uint32 Gates=0x7f0000;
    return (On&Gates&~Active)==0 && (Off&Gates&Active)==0;
}

// D3D fixed-function cascade terminates when COLORARG1 selects an unbound
// texture. A white substitute incorrectly adds 0.5 to skin/clothes at ADDSIGNED.
inline uint32 AC2BoundColorOperation(uint32 Operation,uint32 Argument1,bool Bound)
{ return !Bound && (Argument1&15)==2 ? 1 : Operation; }

inline int32 AC2ActiveStageCount(TConstArrayView<FAC2AlphaStage> ColorStages, TConstArrayView<bool> BoundTextures)
{
    int32 Count=0;
    for(const auto& Stage:ColorStages) {
        if(AC2BoundColorOperation(Stage.Operation,Stage.Argument1,
            BoundTextures.IsValidIndex(Count) && BoundTextures[Count])<=1) break;
        ++Count;
    }
    return Count;
}

#if WITH_EDITOR
class UMaterial;
class UMaterialExpression;
class UMaterialExpressionCustom;
struct FAC2NativeMaterialExpressions
{
    UMaterialExpression *Color=nullptr, *Alpha=nullptr;
    UMaterialExpressionCustom *Stages=nullptr;
};
FAC2NativeMaterialExpressions BuildAC2NativeStages(UMaterial *Material, UMaterialExpression *Base,
    UMaterialExpression *Tint, bool Preview);
#endif
