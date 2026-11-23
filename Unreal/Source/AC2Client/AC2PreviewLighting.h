#pragma once
#include "CoreMinimal.h"

// UIScene / LightSource values, in scene-local centimetres and native RGB.
struct FAC2PreviewLight
{
    FString Name;
    FVector Position = FVector::ZeroVector, Direction = FVector::ZeroVector;
    FLinearColor Diffuse = FLinearColor::White;
    float Range = 500, Intensity = .5f;
    bool Directional = false;
};
struct FAC2PreviewLighting
{
    TArray<FAC2PreviewLight> Lights;
    FLinearColor Ambient = FLinearColor(.1f, .1f, .1f);

    // UIScene::Render's fallback when a scene contains no light entities.
    void UseDefault()
    {
        if (!Lights.IsEmpty()) return;
        FAC2PreviewLight Light;
        Light.Directional = true;
        Light.Direction = FVector(.3, -.9, -1).GetSafeNormal();
        Lights.Add(Light);
    }
    void FollowCamera(FVector Direction)
    {
        Ambient = FLinearColor(.1f, .1f, .1f);
        for (auto& Light : Lights)
            if (Light.Name == TEXT("Light_1"))
            {
                // gmAvatarModificationUI::UpdateCamera tilts the camera light down.
                Light.Directional = true;
                Light.Direction = (Direction + FVector(0, 0, -.5)).GetSafeNormal();
                Light.Diffuse = FLinearColor(.6f, .6f, .6f);
                Ambient += FLinearColor(.1f, .1f, .1f, 0);
            }
    }
};

// FillVerts::GetLighting: lit particles receive the first five scene lights,
// without a Lambert normal. CalculateAttenuation uses the native range curve;
// light/material RGB stays in byte space until the particle material renders.
inline FLinearColor AC2LitParticleColor(FLinearColor Color,const FVector& Position,const FAC2PreviewLighting& Scene)
{
    FLinearColor Light=Scene.Ambient;
    for(int32 I=0;I<FMath::Min(5,Scene.Lights.Num());++I)
    {
        const auto& L=Scene.Lights[I];
        const double Falloff=L.Directional?1:1+10*FVector::DistSquared(Position,L.Position)/FMath::Square(FMath::Max(.02f,L.Range));
        Light+=L.Diffuse*(L.Intensity/Falloff);
    }
    Color.R=FMath::Clamp(Color.R*Light.R,0.f,1.f);
    Color.G=FMath::Clamp(Color.G*Light.G,0.f,1.f);
    Color.B=FMath::Clamp(Color.B*Light.B,0.f,1.f);
    return Color;
}
