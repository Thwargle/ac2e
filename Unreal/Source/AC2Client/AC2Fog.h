#pragma once
#include "CoreMinimal.h"

class UWorld;

// Reserved postprocess UserFlags bit, uploaded per view including scene captures.
constexpr int32 AC2FogViewFlag = 1 << 28;

// Native CLightInfo fog values are distances in metres and display-space RGB.
// RANGEFOGENABLE makes the linear ramp use camera-to-surface distance, not height.
struct FAC2FogParameters
{
    float Start = 0;
    float End = 1000;
    FLinearColor Color = FLinearColor::White;
    bool Enabled = true;
    // Native depth-alpha fog fades frills over the final eight metres.
    float AlphaStart = 152;
    float AlphaEnd = 160;
};

void StartupAC2Fog();
void ShutdownAC2Fog();
void UpdateAC2Fog(UWorld* World, const FAC2FogParameters& Parameters);
void ReleaseAC2Fog(UWorld* World);
bool GetAC2Fog(UWorld* World, FAC2FogParameters& OutParameters);

class UMeshComponent;
void RegisterAC2FogComponent(UMeshComponent* Component);
// Call once after CPU mesh updates; static proxy geometry is never recopied each frame.
void MarkAC2FogGeometryDirty(UMeshComponent* Component);

