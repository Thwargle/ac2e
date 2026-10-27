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
