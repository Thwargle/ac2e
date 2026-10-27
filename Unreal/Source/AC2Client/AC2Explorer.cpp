#include "AC2Explorer.h"
#include "AC2CharacterMovement.h"
#include "AC2GameMode.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "ProceduralMeshComponent.h"

AAC2Explorer::AAC2Explorer(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer.SetDefaultSubobjectClass<UAC2CharacterMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34, 103);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("View"));
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0;
    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    Boom->SetupAttachment(GetCapsuleComponent());
    Boom->SetRelativeLocation(FVector(0, 0, 78));
    Boom->TargetArmLength = 380;
    Boom->bUsePawnControlRotation = true;
    Camera->SetupAttachment(Boom);
    Body = CreateDefaultSubobject<USceneComponent>(TEXT("NativeBody"));
    Body->SetupAttachment(GetCapsuleComponent());
    Body->SetRelativeLocation(FVector(0, 0, -103));
    Body->SetRelativeRotation(FRotator(0, -90, 0));
    bUseControllerRotationYaw = false;
    auto *M = GetCharacterMovement();
    M->MaxStepHeight = 60;
    M->SetWalkableFloorAngle(55);
    M->JumpZVelocity = 500;
    M->GravityScale = 1.4f;
    M->BrakingDecelerationFlying = 12000;
    M->MaxAcceleration = 18000;
    M->bOrientRotationToMovement = false;
    M->bUseControllerDesiredRotation = false;
    M->RotationRate = FRotator(0, 540, 0);
    UpdateSpeed();
}
void AAC2Explorer::SetupPlayerInputComponent(UInputComponent *Input)
{
    Super::SetupPlayerInputComponent(Input);
    static bool Added = false;
    if (!Added)
    {
        Added = true;
        for (auto Map : {FInputAxisKeyMapping(TEXT("AC2Forward"), EKeys::W, 1),
                         FInputAxisKeyMapping(TEXT("AC2Forward"), EKeys::S, -1),
                         FInputAxisKeyMapping(TEXT("AC2Forward"), EKeys::Up, 1),
                         FInputAxisKeyMapping(TEXT("AC2Forward"), EKeys::Down, -1),
                         FInputAxisKeyMapping(TEXT("AC2KeyboardTurn"), EKeys::Right, 1),
                         FInputAxisKeyMapping(TEXT("AC2KeyboardTurn"), EKeys::Left, -1),
                         FInputAxisKeyMapping(TEXT("AC2Right"), EKeys::D, 1),
                         FInputAxisKeyMapping(TEXT("AC2Right"), EKeys::A, -1),
                         FInputAxisKeyMapping(TEXT("AC2Up"), EKeys::E, 1),
                         FInputAxisKeyMapping(TEXT("AC2Up"), EKeys::Q, -1),
                         FInputAxisKeyMapping(TEXT("AC2Turn"), EKeys::MouseX, 1),
                         FInputAxisKeyMapping(TEXT("AC2Look"), EKeys::MouseY, -1)})
            UPlayerInput::AddEngineDefinedAxisMapping(Map);
    }
    Input->BindAxis(TEXT("AC2Forward"), this, &AAC2Explorer::Forward);
    Input->BindAxis(TEXT("AC2Right"), this, &AAC2Explorer::Right);
    Input->BindAxis(TEXT("AC2Up"), this, &AAC2Explorer::Up);
    Input->BindAxis(TEXT("AC2Turn"), this, &AAC2Explorer::Turn);
    Input->BindAxis(TEXT("AC2Look"), this, &AAC2Explorer::Look);
    Input->BindAxis(TEXT("AC2KeyboardTurn"), this, &AAC2Explorer::KeyboardTurn);
    Input->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&AAC2Explorer::ZoomIn);
    Input->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&AAC2Explorer::ZoomOut);
    Input->BindKey(EKeys::Tab, IE_Pressed, this, &AAC2Explorer::ToggleView);
    Input->BindKey(EKeys::F, IE_Pressed, this, &AAC2Explorer::ToggleFly);
    Input->BindKey(EKeys::LeftShift, IE_Pressed, this, &AAC2Explorer::Sprint);
    Input->BindKey(EKeys::LeftShift, IE_Released, this, &AAC2Explorer::StopSprint);
    Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAC2Explorer::StartJump);
    Input->BindKey(EKeys::SpaceBar, IE_Released, this, &AAC2Explorer::EndJump);
}
void AAC2Explorer::Forward(float V)
{
    const auto* PC=Cast<AAC2PlayerController>(Controller);
    if(PC && PC->MouseForward)V=1;
    // Camera orbit alone must not rotate the character or send a different network heading.
    MovementSteersCamera=!FMath::IsNearlyZero(V);
    GetCharacterMovement()->bUseControllerDesiredRotation=(PC && PC->CameraSteersPlayer) || (MovementSteersCamera && !(PC && PC->FreeOrbit));
    if (!bHeld && Controller)
    {
        FRotator R = PC && PC->FreeOrbit ? GetActorRotation() : GetControlRotation();
        if (!bFly)
            R.Pitch = 0;
        AddMovementInput(R.Vector(), V);
    }
}
void AAC2Explorer::Right(float V)
{
    const auto* PC=Cast<AAC2PlayerController>(Controller);
    const bool Orbit=PC && PC->FreeOrbit;
    GetCharacterMovement()->bUseControllerDesiredRotation|=!Orbit && !FMath::IsNearlyZero(V);
    if (!bHeld && Controller)
        AddMovementInput(FRotationMatrix(FRotator(0, Orbit?GetActorRotation().Yaw:GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V);
}
void AAC2Explorer::Up(float V)
{
    if (!bHeld && (bFly || bSwim))
        AddMovementInput(FVector::UpVector, V);
}
void AAC2Explorer::Turn(float V)
{
    AddControllerYawInput(V*MouseSensitivity);
}
void AAC2Explorer::Look(float V)
{
    AddControllerPitchInput(V*MouseSensitivity*(InvertMouse?-1:1));
}
void AAC2Explorer::KeyboardTurn(float V)
{
    if(bHeld || bDead || FMath::IsNearlyZero(V) || !Controller || Controller->IsMoveInputIgnored())return;
    auto R=Controller->GetControlRotation();
    R.Yaw+=V*GetCharacterMovement()->RotationRate.Yaw*GetWorld()->GetDeltaSeconds();
    Controller->SetControlRotation(R);
    GetCharacterMovement()->bUseControllerDesiredRotation=true;
}
void AAC2Explorer::Sprint()
{
    bFast = true;
    UpdateSpeed();
}
void AAC2Explorer::StopSprint()
{
    bFast = false;
    UpdateSpeed();
}
void AAC2Explorer::StartJump()
{
    if (!bHeld && !bDead && !bFly)
    {
        if (bSwim)
        {bSwimUp=true;AddMovementInput(FVector::UpVector, 1);}
        else if(GroundTime>=.5f && GetCharacterMovement()->IsMovingOnGround())
        {
            PlayBehavior(AC2Motion::Jump);
            Jump();
            GroundTime=0;
        }
    }
}
void AAC2Explorer::EndJump(){bSwimUp=false;StopJumping();}
void AAC2Explorer::UpdateSpeed()
{
    auto* M=Cast<UAC2CharacterMovement>(GetCharacterMovement());
    const uint32 Element=bSwim?AC2Motion::Water:bWade?AC2Motion::SeaFloor:M->IsFalling()?AC2Motion::Air:AC2Motion::Ground;
    auto Values=Model?Model->MotionData().Values(Element,bSwim?AC2Motion::Swim:NativeMode,0x80000001,GlobalModifier):FAC2MotionValues();
    auto Ground=Model?Model->MotionData().Values(bWade?AC2Motion::SeaFloor:AC2Motion::Ground,NativeMode,0x80000001,GlobalModifier):FAC2MotionValues();
    for(auto* V:{&Values,&Ground})
    {
        V->Forward*=VelocityScale;V->Backward*=VelocityScale;V->Strafe*=VelocityScale;V->Vertical*=VelocityScale;
        V->ForwardAccel*=AccelerationScale;V->BackwardAccel*=AccelerationScale;V->StrafeAccel*=AccelerationScale;V->VerticalAccel*=AccelerationScale;
        V->Jump*=JumpScale;
    }
    M->Native=Values;
    M->Pace=bFast?1.f:.55f;
    M->MaxWalkSpeed=Ground.Forward*100*(bFast?1.f:.55f);
    M->MaxCustomMovementSpeed=Values.Forward*100;
    M->MaxFlySpeed=bFast?12000:4000;
    M->MaxAcceleration=bFly?18000:FMath::Max(Values.ForwardAccel,Values.StrafeAccel)*100;
    M->BrakingDecelerationWalking=Ground.ForwardBrake*100;
    M->GroundFriction=0;
    M->AirControl=Values.ForwardAccel>0 ? FMath::Clamp(Values.ForwardAccel/FMath::Max(1.f,Ground.ForwardAccel),0.f,1.f):0;
    M->GravityScale=1;
    M->JumpZVelocity=Ground.Jump*100*FMath::Max(.5,Body->GetRelativeScale3D().Z);
    M->RotationRate.Yaw=GetVelocity().Size2D()>100?Ground.RunTurn:Ground.StillTurn;
}
bool AAC2Explorer::IsFlying() const
{
    return bFly;
}
void AAC2Explorer::SetFlying(bool Flying)
{
    bFly = Flying;
    UpdateSpeed();
    if (!bHeld)
        GetCharacterMovement()->SetMovementMode(Flying?MOVE_Flying:bSwim?MOVE_Custom:MOVE_Walking,bSwim?1:0);
}
void AAC2Explorer::SetHeld(bool Held)
{
    bHeld = Held || bDead;
    if (bHeld)
    {
        EndJump();
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
    }
    else
        GetCharacterMovement()->SetMovementMode(bFly?MOVE_Flying:bSwim?MOVE_Custom:MOVE_Walking,bSwim?1:0);
}
void AAC2Explorer::ToggleFly()
{
    if (auto *Mode = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        Mode->ToggleFlight();
}

void AAC2Explorer::BeginPlay()
{
    Super::BeginPlay();
    SetActorHiddenInGame(true);
    SetHeld(true);
}
bool AAC2Explorer::ApplyAvatar(uint32 Physics, float Height, float Build, const FAC2Appearance &Appearance)
{
    auto *Mode = GetWorld()->GetAuthGameMode<AAC2GameMode>();
    if (!Mode)
        return false;
    Model.Reset();
    for (auto &C : ModelComponents)
        if (C.IsValid())
            C->DestroyComponent();
    ModelComponents.Reset();
    Body->SetRelativeScale3D(FVector::OneVector);
    TInlineComponentArray<UActorComponent *> Before(this);
    FString Error;
    Model = MakeUnique<FAC2AssetLibrary>();
    bModel = Model->Open(AC2::ResolveDatDirectory(Mode->DatDirectory), Error) &&
             Model->DrawModel(Physics, this, Body, Appearance);
    if(MotionDescriptor)Model->SetMotionDescriptor(MotionDescriptor);
    TInlineComponentArray<UActorComponent *> After(this);
    for (auto *C : After)
        if (!Before.Contains(C))
            ModelComponents.Add(C);
    const FBox Bounds = Model->ModelBounds();
    const FBox Feet = Model->ModelBounds(0x81000001);
    const float FootZ = Feet.IsValid ? Feet.Min.Z : Bounds.Min.Z;
    if (Bounds.IsValid)
    {
        const float OldHalf = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        const float Radius =
            FMath::Clamp(float(FMath::Min(Bounds.GetSize().X, Bounds.GetSize().Y) * Build * .3), 24.f, 70.f);
        const float Half = FMath::Max(Radius, float((Bounds.Max.Z - FootZ) * Height * .5));
        GetCapsuleComponent()->SetCapsuleSize(Radius, Half);
        AddActorWorldOffset(FVector(0, 0, Half - OldHalf), false, nullptr, ETeleportType::TeleportPhysics);
        Body->SetRelativeScale3D(FVector(Build, Build, Height));
        Body->SetRelativeLocation(FVector(0, 0, -Half - FootZ * Height));
        Boom->SetRelativeLocation(FVector(0, 0, Half * .75));
    }
    UE_LOG(LogTemp, Display, TEXT("AC2 player native model: %s %s"), bModel ? TEXT("loaded") : TEXT("failed"),
           *Error);
    return bModel;
}
void AAC2Explorer::EndPlay(const EEndPlayReason::Type Reason)
{
    Model.Reset();
    Super::EndPlay(Reason);
}
void AAC2Explorer::Tick(float Delta)
{
    Super::Tick(Delta);
    AnimationTime += Delta;
    GroundTime=GetCharacterMovement()->IsMovingOnGround()?GroundTime+Delta:0;
    UpdateSpeed();
    if (Model && bModel)
    {
        const FVector LocalVelocity =
            Body->GetComponentTransform().InverseTransformVectorNoScale(GetVelocity());
        Model->SetAnimationState(bSwim?AC2Motion::Water:bWade?AC2Motion::SeaFloor:GetCharacterMovement()->IsFalling()?AC2Motion::Air:AC2Motion::Ground,
                                bDead?AC2Motion::Dead:bSwim?AC2Motion::Swim:NativeMode);
        Model->Animate(AnimationTime, GetVelocity().Size2D(), nullptr, &LocalVelocity);
    }
    if (bSwim && !bFly && !bHeld)
    {
        if(bSwimUp)AddMovementInput(FVector::UpVector,1);
        auto* M=Cast<UAC2CharacterMovement>(GetCharacterMovement());
        M->WaterLevel=WaterLevel;
        M->WaterDepthOffset=GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*(2*(Model?Model->MotionData().Water:.75f)-1);
    }
}
void AAC2Explorer::ToggleView()
{
    bThirdPerson = !bThirdPerson;
    Boom->TargetArmLength = bThirdPerson ? ThirdPersonDistance : 0;
    Body->SetVisibility(bThirdPerson, true);
}
void AAC2Explorer::ZoomCamera(float Delta)
{
    if(!FMath::IsFinite(Delta) || FMath::IsNearlyZero(Delta))return;
    // CameraManager zoom uses a bounded distance. Tab restores the saved shoulder distance.
    float Distance=bThirdPerson?ThirdPersonDistance:Delta<0?50:0;
    Distance=FMath::Clamp(Distance-Delta*50.f,0.f,1000.f);
    bThirdPerson=Distance>=100;
    if(bThirdPerson)ThirdPersonDistance=Distance;
    Boom->TargetArmLength=bThirdPerson?Distance:0;
    Body->SetVisibility(bThirdPerson,true);
}
void AAC2Explorer::SetCameraDistance(float Distance)
{
    ThirdPersonDistance=FMath::Clamp(Distance,100.f,1000.f);
    if(bThirdPerson)Boom->TargetArmLength=ThirdPersonDistance;
}
void AAC2Explorer::SetWater(bool InWater, double Level)
{
    WaterLevel = Level;
    if (bSwim == InWater)
        return;
    bSwim = InWater;
    if (!bHeld)
        GetCharacterMovement()->SetMovementMode(bFly?MOVE_Flying:bSwim?MOVE_Custom:MOVE_Walking,bSwim?1:0);
    UpdateSpeed();
}
void AAC2Explorer::UpdateWater(bool Present,double Level)
{
    const float Height=GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2;
    const double Depth=Level-(GetActorLocation().Z-Height*.5);
    const float WaterThreshold=Model?Model->MotionData().Water:.75f;
    const float WadeThreshold=Model?Model->MotionData().SeaFloor:.1f;
    bWade=Present && Depth>Height*WadeThreshold && Depth<Height*WaterThreshold && GetCharacterMovement()->IsMovingOnGround();
    // Small hysteresis keeps wave/sweep roundoff from alternating ground and swim each tick.
    SetWater(Present && Depth>Height*WaterThreshold-(bSwim?30:0),Level);
}
bool AAC2Explorer::PlayBehavior(uint32 Behavior,uint32 Mode,float Rate,bool Hold)
{
    if(Mode)NativeMode=Mode;
    // A mode packet and its first behavior often arrive in the same frame.
    // Use the pending native mode immediately, before the next animation tick.
    return Model && Model->PlayBehavior(Behavior,NativeMode,Rate,Hold);
}
void AAC2Explorer::StopBehavior(uint32 Behavior){if(Model)Model->StopBehavior(Behavior);}
void AAC2Explorer::SetNativeMode(uint32 Mode){NativeMode=Mode;}
void AAC2Explorer::ResetMotionParameters()
{
    GlobalModifier=MotionDescriptor=0;VelocityScale=AccelerationScale=JumpScale=1;
}
void AAC2Explorer::SetMotionParameter(const FString& Parameter,double Value)
{
    if(!FMath::IsFinite(Value)||Value<0)return;
    if(Parameter==TEXT("descriptor")){if(Model&&Model->SetMotionDescriptor(uint32(Value)))MotionDescriptor=uint32(Value);}
    else if(Parameter==TEXT("modifier"))GlobalModifier=uint32(Value);
    else if(Parameter==TEXT("velocity"))VelocityScale=FMath::Clamp(float(Value),0.f,16.f);
    else if(Parameter==TEXT("acceleration"))AccelerationScale=FMath::Clamp(float(Value),0.f,16.f);
    else if(Parameter==TEXT("jump"))JumpScale=FMath::Clamp(float(Value),0.f,16.f);
    UpdateSpeed();
}
void AAC2Explorer::SetDead(bool Dead)
{
    if(bDead==Dead)return;
    bDead=Dead;
    if(Dead)PlayBehavior(AC2Motion::Death,AC2Motion::Dead,1,true);
    else {StopBehavior();NativeMode=AC2Motion::Peace;}
    SetHeld(Dead);
}
void AAC2Explorer::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);GroundTime=0;PlayBehavior(AC2Motion::Land);
}
void AAC2Explorer::Hit(float Damage)
{
    auto* Mode=GetWorld()->GetAuthGameMode<AAC2GameMode>();
    if(!Mode||Mode->Network||bDead)return;
    if(auto* Profile=Mode->Characters.Active())
    {
        Profile->Health=FMath::Max(0.f,Profile->Health-Damage);
        if(Profile->Health<=0)SetDead(true);
        else PlayBehavior(AC2Motion::Hit);
        Mode->Notice=bDead?TEXT("You have fallen. Recall to return to safety."):FString::Printf(TEXT("Hit for %.0f damage"),Damage);
    }
}
