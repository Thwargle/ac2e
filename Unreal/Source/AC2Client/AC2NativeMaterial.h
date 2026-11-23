#pragma once
#include "CoreMinimal.h"
#include "AC2PreviewLighting.h"
#include "AC2MaterialAlpha.h"
class UWorld;
class USceneComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
void RegisterAC2WorldMaterial(UMaterialInstanceDynamic* Material,USceneComponent* Component,bool Baked);
void RegisterAC2FoliageMaterial(UMaterialInstanceDynamic* Material,USceneComponent* Component,bool Bend);
// Cosmetic contacts in centimetres; no collision or gameplay state is changed.
struct FAC2FoliageContact { uint32 Id=0; FVector Feet=FVector::ZeroVector; float Radius=45,Height=180; };
constexpr int32 AC2FoliageContactLimit=8;
void UpdateAC2FoliageInteraction(UWorld* World,TConstArrayView<FAC2FoliageContact> Contacts,float Delta);
void UpdateAC2WorldLighting(UWorld* World,const FAC2PreviewLighting& Outdoor,
    TFunctionRef<bool(const FVector&,FAC2PreviewLighting&)> Interior);
void RegisterAC2EffectLight(UPointLightComponent* Light);
void AppendAC2EffectLights(UWorld* World,const FVector& Position,FAC2PreviewLighting& Lighting);

// Native texture filtering/tinting operates on byte RGB, before the final
// conversion to UE's linear scene colour. FLinearColor(FColor) decodes sRGB and
// must not be used when composing the native terrain atlas or its far palette.
inline FLinearColor AC2SampleNativeRGB(TConstArrayView<FColor> Pixels,int32 W,int32 H,double U,double V)
{
    if(W<=0 || H<=0 || Pixels.Num()!=W*H)return FLinearColor(.5f,.5f,.5f);
    const double X=(U-FMath::FloorToDouble(U))*W-.5,Y=(V-FMath::FloorToDouble(V))*H-.5;
    const int32 IX=FMath::FloorToInt(X),IY=FMath::FloorToInt(Y);
    auto P=[&](int32 A,int32 B){return Pixels[((B%H+H)%H)*W+(A%W+W)%W].ReinterpretAsLinear();};
    return FMath::Lerp(FMath::Lerp(P(IX,IY),P(IX+1,IY),float(X-IX)),
        FMath::Lerp(P(IX,IY+1),P(IX+1,IY+1),float(X-IX)),float(Y-IY));
}

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
    UMaterialExpression *Tint, bool Preview, bool Shadowed, bool Water, bool Anisotropic);
#endif
