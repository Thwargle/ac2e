#pragma once
#include "AC2Assets.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AC2Mob.generated.h"
UCLASS()
class AAC2Mob : public ACharacter
{
    GENERATED_BODY()
  public:
    AAC2Mob();
    bool Initialize(const FString &Directory, uint32 Visual);
    void PlaceAt(const FVector &Position);
    void SetViewerDistance(float Distance);
    uint32 VisualId = 0;
    bool HasAnimation() const
    {
        return Model && Model->AnimationCount() > 0;
    }
    int32 MovementAnimationUpdates() const
    {
        return Model ? Model->MovementAnimationUpdates() : 0;
    }
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Hit(float Damage = 25);
    float Health = 100;
    FString DisplayName = TEXT("Wandering scout");

  private:
    TUniquePtr<FAC2AssetLibrary> Model;
    UPROPERTY() TObjectPtr<USceneComponent> Body;
    FVector Home, Direction;
    float Time = 0, Decision = 0, Death = 0;
    bool Aggressive=false;
    float AttackTime=0;
    bool VisibleToViewer = true;
};
