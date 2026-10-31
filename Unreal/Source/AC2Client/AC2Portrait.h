#pragma once
#include "AC2Assets.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AC2Portrait.generated.h"

struct FAC2SelectionFigure
{
    uint32 Physics = 0;
    float Height = 1, Build = 1;
    FAC2Appearance Appearance;
};

UCLASS()
class AAC2Portrait : public AActor
{
    GENERATED_BODY()
  public:
    AAC2Portrait();
    bool Show(const FString &Directory, uint32 Physics, float Height, float Build,
              const FAC2Appearance &Appearance = {}, uint32 Scene = 0);
    void Rotate(float Degrees);
    bool ShowLineup(const FString &Directory, const class FAC2CharacterCatalog &Catalog,
                   const TMap<uint32, struct FAC2CharacterDraft>& Drafts);
    bool ShowSelection(const FString &Directory, const TArray<FAC2SelectionFigure> &Figures, int32 Selected, int32 Slots);
    bool ShowScene(const FString& Directory, uint32 Scene, FIntPoint Size);
    void SelectCharacter(int32 Index);
    bool SelectionMoving() const { return ArmTime < 1.5f || ArmCurrent != ArmTarget; }
    int32 SelectionIndex() const { return ArmCurrent; }
    FVector2D AvatarPoint(int32 Index) const;
    int32 PickAvatar(FVector2D Point) const;
    void HoverAvatar(int32 Index);
    bool BeginAvatarSelection(int32 Index);
    bool AvatarSelectionComplete() const;
    float AvatarSelectionDuration() const;
    void SetCamera(bool Head, float Rotation);
    void SetViewport(FIntPoint Size, float BodyFill);
    void SetHealth(float Current,float Maximum) { if(Model)Model->SetHealth(Current,Maximum); }
    bool AuditFraming(FString& Error) const;
    uint32 Fingerprint() const
    {
        return Model ? Model->AppearanceFingerprint() : 0;
    }
    uint32 LineupFingerprint(int32 Index) const
    { return LineupModels.IsValidIndex(Index) ? LineupModels[Index]->AppearanceFingerprint() : 0; }
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> Image;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Composite;
    UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<USceneComponent> Body;
    UPROPERTY() TObjectPtr<USceneComponent> Stage;
    bool Ready = false;

  private:
    TUniquePtr<FAC2AssetLibrary> Model, Backdrop;
    FAC2PreviewLighting Lighting;
    TArray<TWeakObjectPtr<UActorComponent>> StageComponents;
    uint32 SceneId = 0;
    struct FReference { FBox Body{ForceInit}, Head{ForceInit}; float Feet = 0; };
    TMap<uint32, FReference> References;
    FReference Reference;
    FBox HeadBounds{ForceInit};
    TArray<float> StageFloors;
    float Floor = 0;
    TArray<TUniquePtr<FAC2AssetLibrary>> LineupModels;
    TArray<FBox> LineupBounds;
    int32 HoveredLineupAvatar=INDEX_NONE, SelectedLineupAvatar=INDEX_NONE;
    TArray<TWeakObjectPtr<USceneComponent>> Arms;
    TArray<FQuat> ArmStart, ArmEnd;
    TArray<FTransform> FigureFrames;
    int32 ArmCurrent = 0, ArmTarget = 0;
    float ArmTime = 1.5f;
    TArray<TWeakObjectPtr<UActorComponent>> Generated;
    float Time = 0, Yaw = 0;
    bool HeadCamera = false;
    float FullBodyFill = .7f;
    TUniquePtr<class FAC2Effects> SceneEffects;
    void RefreshSceneEffects(const FString& Directory);
};
