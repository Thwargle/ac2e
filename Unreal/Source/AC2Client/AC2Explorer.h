#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/SceneComponent.h"
#include "AC2Assets.h"
#include "AC2Explorer.generated.h"

class UCameraComponent;
class USpringArmComponent;
struct FAC2StoryFrame;
class SAC2StoryScene;
UCLASS()
class AAC2Explorer : public ACharacter
{
    GENERATED_BODY()
  public:
    AAC2Explorer(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool ApplyAvatar(uint32 Physics, float Height, float Build, const FAC2Appearance &Appearance = {});
    bool PlayBehavior(uint32 Behavior,uint32 Mode=0,float Rate=1,bool Hold=false);
    bool HandlingItem() const {return Model && Model->HandlingItem();}
    bool ReleasedHeldItem(uint32 Holding) const {return Model && Model->ReleasedHeldItem(Holding);}
    bool HoldingStreakActive(uint32 Holding) const {return Model && Model->HoldingStreakActive(Holding);}
    bool PlayEffect(uint32 Effect,float Strength=1,USceneComponent* Target=nullptr) {return Model && Model->PlayEffect(Effect,Strength,Target);}
    bool PlayEffectScript(uint32 Script) {return Model && Model->PlayEffectScript(Script);}
    void StopEffect(uint32 Effect) {if(Model)Model->StopEffect(Effect);}
    void SetHealth(float Current,float Maximum) {AppearanceHealth=Current;AppearanceMaxHealth=Maximum;if(Model)Model->SetHealth(Current,Maximum);}
    uint32 DamageAppearanceFingerprint() const {return Model?Model->DamageAppearanceFingerprint():0;}
    int32 EffectParticleCount() const {return Model?Model->EffectParticleCount():0;}
    uint32 AvatarAppearanceFingerprint() const {return Model?Model->AppearanceFingerprint():0;}
    void StopBehavior(uint32 Behavior=0);
    void ReleaseBehavior(uint32 Behavior);
    void StopAnimationLayer(uint32 Layer);
    bool PlayPickup();
    uint64 BehaviorInstance(uint32 Behavior) const;
    bool CompletedHoldCycles(uint32 Behavior,uint32 Cycles) const;
    TArray<float> StoryHookDelays(uint32 Behavior) const {return Model?Model->StoryHookDelays(Behavior):TArray<float>();}
    float StoryBehaviorDuration(uint32 Behavior) const {return Model?Model->BehaviorDuration(Behavior):0;}
    void SetNativeMode(uint32 Mode);
    uint32 GetNativeMode() const { return NativeMode; }
    void SetMotionParameter(const FString& Parameter,double Value);
    void ResetMotionParameters();
    void SetDead(bool Dead);
    bool IsDead() const { return bDead; }
    float MouseSensitivity=1;
    bool InvertMouse=false;
    void Hit(float Damage);
    virtual void Landed(const FHitResult& Hit) override;
    bool AuditAttachments(FString& Error) const { return Model && Model->AuditAttachments(Error); }
    bool HoldingTransform(uint32 Location,FTransform& Out) const
    { if(!Model || !Model->AnimatedHoldingLocation(Location,Out)) return false; Out=Out*Body->GetComponentTransform();return true; }
    void ToggleView();
    void ZoomCamera(float Delta);
    void SetCameraDistance(float Distance);
    void SetWater(bool InWater, double Level);
    void UpdateWater(bool Present,double Level);
    bool IsSwimming() const
    {
        return bSwim;
    }
    bool HasModel() const
    {
        return bModel && Model && Model->AnimationCount() > 0;
    }
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Body;
    virtual void SetupPlayerInputComponent(UInputComponent *Input) override;
    void SetFlying(bool Flying);
    void SetRunning(bool Running) {bFast=Running;UpdateSpeed();}
    void ToggleAutoRun();
    void StopAutoRun() { bAutoRun=false; }
    bool IsAutoRunning() const { return bAutoRun; }
    bool HasManualMovementInput() const;
    bool IsRunning() const { return bFast; }
    bool IsFlying() const;
    void SetHeld(bool Held);
    void SetServerMovementLocked(bool Locked);
    void BeginShardOrbit(TFunction<bool(FVector&, FVector&)> CameraSockets, double Duration = 10.4167);
    bool PlayShardScene(const TArray<FAC2StoryFrame>& Frames, TFunction<void()> OnFinished);
    void CancelShardPresentation();
    bool IsHeld() const
    {
        return bHeld;
    }
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

  private:
    friend class AAC2PlayerController;
    TFunction<bool(FVector&, FVector&)> ShardCameraSockets;
    double ShardCameraUntil = 0;
    TSharedPtr<SAC2StoryScene> ShardScene;
    TWeakObjectPtr<class UGameViewportClient> ShardViewport;
    TWeakObjectPtr<APlayerController> ShardController;
    TWeakPtr<class SWidget> ShardPreviousFocus;
    TFunction<void()> ShardCompleted;
    void FinishShardScene(bool Notify);
    TUniquePtr<FAC2AssetLibrary> Model;
    float AppearanceHealth=-1,AppearanceMaxHealth=0;
    TArray<TWeakObjectPtr<UActorComponent>> ModelComponents;
    float AnimationTime = 0;
    bool bThirdPerson = true, bSwim = false, bWade=false, bModel = false;
    float ThirdPersonDistance=380;
    FVector ManualAxes=FVector::ZeroVector;
    float ManualKeyboardTurn=0,ManualMouseTurn=0;
    double WaterLevel = 0;
    bool bFast = true, bHeld = false, bFly = false, bAutoRun = false;
    bool bDead=false;
    bool bLocalHold=false, bServerMovementLocked=false;
    uint32 NativeMode=AC2Motion::Peace;
    uint32 GlobalModifier=0,MotionDescriptor=0;
    float VelocityScale=1,AccelerationScale=1,JumpScale=1;
    float GroundTime=0;
    float PendingJump=-1;
    void Forward(float V);
    void Right(float V);
    void Up(float V);
    void Turn(float V);
    void Look(float V);
    void KeyboardTurn(float V);
    void ZoomIn() { ZoomCamera(1); }
    void ZoomOut() { ZoomCamera(-1); }
    void ToggleFly();
    void UpdateWalkModifier();
    void StartJump();
    void EndJump();
    bool bSwimUp=false;
    void UpdateSpeed();
};
