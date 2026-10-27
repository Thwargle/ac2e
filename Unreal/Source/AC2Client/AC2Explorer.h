#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/SceneComponent.h"
#include "AC2Assets.h"
#include "AC2Explorer.generated.h"

class UCameraComponent;
class USpringArmComponent;
UCLASS()
class AAC2Explorer : public ACharacter
{
    GENERATED_BODY()
  public:
    AAC2Explorer(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool ApplyAvatar(uint32 Physics, float Height, float Build, const FAC2Appearance &Appearance = {});
    bool PlayBehavior(uint32 Behavior,uint32 Mode=0,float Rate=1,bool Hold=false);
    bool PlayEffect(uint32 Effect,float Strength=1,USceneComponent* Target=nullptr) {return Model && Model->PlayEffect(Effect,Strength,Target);}
    bool PlayEffectScript(uint32 Script) {return Model && Model->PlayEffectScript(Script);}
    void StopEffect(uint32 Effect) {if(Model)Model->StopEffect(Effect);}
    int32 EffectParticleCount() const {return Model?Model->EffectParticleCount():0;}
    uint32 AvatarAppearanceFingerprint() const {return Model?Model->AppearanceFingerprint():0;}
    void StopBehavior(uint32 Behavior=0);
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
    bool IsFlying() const;
    void SetHeld(bool Held);
    bool IsHeld() const
    {
        return bHeld;
    }
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

  private:
    TUniquePtr<FAC2AssetLibrary> Model;
    TArray<TWeakObjectPtr<UActorComponent>> ModelComponents;
    float AnimationTime = 0;
    bool bThirdPerson = true, bSwim = false, bWade=false, bModel = false;
    double WaterLevel = 0;
    bool bFast = false, bHeld = false, bFly = false;
    bool bDead=false;
    uint32 NativeMode=AC2Motion::Peace;
    uint32 GlobalModifier=0,MotionDescriptor=0;
    float VelocityScale=1,AccelerationScale=1,JumpScale=1;
    float GroundTime=0;
    void Forward(float V);
    void Right(float V);
    void Up(float V);
    void Turn(float V);
    void Look(float V);
    void ToggleFly();
    void Sprint();
    void StopSprint();
    void StartJump();
    void EndJump();
    bool bSwimUp=false;
    void UpdateSpeed();
};
