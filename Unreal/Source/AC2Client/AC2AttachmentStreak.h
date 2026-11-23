#pragma once
#include "CoreMinimal.h"

class FAC2AssetLibrary;
struct FAC2AssetReader;
class UProceduralMeshComponent;
class USceneComponent;

struct FAC2StreakDefinition
{
    bool Enabled = false;
    FColor Color = FColor::White;
    FVector Edge0 = FVector::ZeroVector, Edge1 = FVector::ZeroVector;
    uint32 Material = 0;
    float Life = 0, Duration = 0, MinTime = 0, MinDistance = 0, TextureScale = 1;
    static FAC2StreakDefinition Read(FAC2AssetReader& Reader);
};

// The two authored points are local to the moving weapon. Previous samples
// stay in world space when the hand rotates or the player moves.
struct FAC2AttachmentStreak
{
    FAC2StreakDefinition Definition;
    uint32 Effect = 0;
    bool Active = false;
    double Created = 0;
    TWeakObjectPtr<UProceduralMeshComponent> Mesh;
    void Activate(bool On);
    void Update(double Time, USceneComponent& Root, const FVector& ModelScale, FAC2AssetLibrary& Assets, bool WorldLighting);
    void Release();
private:
    struct FSample { FVector A, B; double Time, Distance; };
    TArray<FSample> Samples;
    FVector Origin = FVector::ZeroVector;
};
