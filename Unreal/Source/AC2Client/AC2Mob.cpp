#include "AC2Mob.h"
#include "AC2Explorer.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProceduralMeshComponent.h"
AAC2Mob::AAC2Mob()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34, 103);
    Body = CreateDefaultSubobject<USceneComponent>(TEXT("NativeMob"));
    Body->SetupAttachment(GetCapsuleComponent());
    Body->SetRelativeLocation(FVector(0, 0, -103));
    Body->SetRelativeRotation(FRotator(0, -90, 0));
    auto *M = GetCharacterMovement();
    M->bRunPhysicsWithNoController = true;
    M->MaxWalkSpeed = 160;
    M->bOrientRotationToMovement = true;
    M->RotationRate = FRotator(0, 180, 0);
    M->MaxStepHeight = 60;
    M->SetWalkableFloorAngle(55);
}
bool AAC2Mob::Initialize(const FString &D, uint32 V)
{
    VisualId = V;
    FString Error;
    Model = MakeUnique<FAC2AssetLibrary>();
    bool OK = Model->Open(D, Error) && Model->DrawModel(V, this, Body);
    if (OK)
    {
        const FBox Bounds = Model->ModelBounds();
        if (!Bounds.IsValid)
            return false;
        // Ground different native bodies by their feet and fit a conservative
        // movement capsule instead of giving quadrupeds a human-sized collider.
        const FVector Size = Bounds.GetSize();
        const float Radius = FMath::Clamp(FMath::Max(Size.X, Size.Y) * .3, 18., 160.);
        const float HalfHeight = FMath::Max(Radius, float(FMath::Clamp(Size.Z * .5, 25., 300.)));
        GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
        Body->SetRelativeLocation(FVector(0, 0, -HalfHeight - Bounds.Min.Z));
        UE_LOG(LogTemp, Display, TEXT("AC2 creature %08X: bounds=%s capsule=%g/%g animated=%d"), V,
               *Size.ToString(), Radius, HalfHeight, HasAnimation());
    }
    UE_LOG(LogTemp, Display, TEXT("AC2 offline mob %08X %s"), V, OK ? TEXT("loaded") : TEXT("failed"));
    return OK;
}
void AAC2Mob::PlaceAt(const FVector &Position)
{
    Home = Position;
    SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    SetActorEnableCollision(true);
}
void AAC2Mob::SetViewerDistance(float Distance)
{
    VisibleToViewer = Distance < 18000;
    SetActorHiddenInGame(!VisibleToViewer);
    PrimaryActorTick.TickInterval = VisibleToViewer ? 0 : .25f;
}
void AAC2Mob::Tick(float Dt)
{
    Super::Tick(Dt);
    Time += Dt;
    if (Health <= 0)
    {
        Death += Dt;
        if(Model)Model->Animate(Time,0);
        if (Death > CorpseDuration)
            Destroy();
        return;
    }
    if (!VisibleToViewer)
        return;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* Player=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(Aggressive&&(!Player||Player->IsDead()))Aggressive=false;
    if(Model)Model->SetAnimationState(AC2Motion::Ground,Aggressive?AC2Motion::Monster:AC2Motion::Peace);
    if(Aggressive && Player && !Player->IsDead())
    {
        FVector Toward=Player->GetActorLocation()-GetActorLocation();
        float Range=GetCapsuleComponent()->GetScaledCapsuleRadius()+Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+130;
        if(Toward.Size2D()>1600 || FVector::DistSquared(GetActorLocation(),Home)>FMath::Square(1800.))Aggressive=false;
        else if(Toward.Size2D()<Range)
        {
            GetCharacterMovement()->StopMovementImmediately();
            SetActorRotation(FRotator(0,Toward.Rotation().Yaw,0));AttackTime-=Dt;
            if(AttackTime<=0)
            {
                FHitResult Block;FCollisionQueryParams Query;Query.AddIgnoredActor(this);Query.AddIgnoredActor(Player);
                if(!GetWorld()->LineTraceSingleByObjectType(Block,GetActorLocation(),Player->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Query))
                {if(Model)Model->PlayBehavior(AC2Motion::MeleeNormal,AC2Motion::Monster);Player->Hit(6);}
                AttackTime=1.8f;
            }
            if(Model)Model->Animate(Time,0);
            return;
        }
        else {Direction=Toward.GetSafeNormal2D();Decision=1;}
    }
    Decision -= Dt;
    if (Decision <= 0)
    {
        Direction = GetActorLocation() - Home;
        if (Direction.Size2D() > 650)
            Direction = -Direction.GetSafeNormal2D();
        else
        {
            float A = FMath::FRand() * 2 * PI;
            Direction = FVector(FMath::Cos(A), FMath::Sin(A), 0);
        }
        Decision = FMath::FRandRange(2.f, 5.f);
    }
    FHitResult Hit;
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Next = GetActorLocation() + Direction * (Radius + 90);
    FCollisionQueryParams Q;
    Q.AddIgnoredActor(this);
    const bool Floor = GetWorld()->LineTraceSingleByObjectType(
                           Hit, Next + FVector(0, 0, 30), Next - FVector(0, 0, HalfHeight + 80),
                           FCollisionObjectQueryParams(ECC_WorldStatic), Q) &&
                       Hit.ImpactNormal.Z >= .57357644f &&
                       FMath::Abs(Hit.ImpactPoint.Z - (GetActorLocation().Z - HalfHeight)) < 65;
    const bool Wall =
        GetWorld()->SweepTestByChannel(GetActorLocation(), Next, FQuat::Identity, ECC_Pawn,
                                       FCollisionShape::MakeCapsule(Radius - 2, HalfHeight - 2), Q);
    if (Floor && !Wall)
        AddMovementInput(Direction, 1);
    else
    {
        Direction = -Direction;
        Decision = 0;
    }
    if (GetActorLocation().Z < Home.Z - 1000)
    {
        SetActorLocation(Home);
        GetCharacterMovement()->StopMovementImmediately();
    }
    if (Model)
    {
        const FVector LocalVelocity =
            Body->GetComponentTransform().InverseTransformVectorNoScale(GetVelocity());
        Model->Animate(Time, GetVelocity().Size2D(), nullptr, &LocalVelocity);
    }
}
void AAC2Mob::EndPlay(const EEndPlayReason::Type Reason)
{
    Model.Reset();
    Super::EndPlay(Reason);
}
bool AAC2Mob::Hit(float Damage)
{
    if (Health <= 0)
        return false;
    Health = FMath::Max(0.f, Health - FMath::Max(0.f, Damage));
    if(Model)Model->SetHealth(Health,100);
    if (Health == 0)
    {
        GetCharacterMovement()->DisableMovement();
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(Model)
        {
            Model->PlayBehavior(AC2Motion::Death,AC2Motion::Dead,1,true);
            CorpseDuration=FMath::Max(5.f,Model->BehaviorDuration(AC2Motion::Death)+1.f);
        }
    }
    else {Aggressive=true;AttackTime=.7f;if(Model)Model->PlayBehavior(AC2Motion::Hit,AC2Motion::Monster);}
    return true;
}
