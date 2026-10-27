#include "AC2GameMode.h"
#include "AC2CreatureCatalog.h"
#include "AC2Explorer.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2Placement.h"
#include "AC2WorldGrid.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

AAC2GameMode::AAC2GameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AAC2Explorer::StaticClass();
    PlayerControllerClass = AAC2PlayerController::StaticClass();
    HUDClass = AAC2HUD::StaticClass();
    DatDirectory = TEXT("../Asheron's Call 2 - Legions");
    StartBlock = TEXT("5667");
}
void AAC2GameMode::BeginPlay()
{
    Super::BeginPlay();
    bSmoke = FParse::Param(FCommandLine::Get(), TEXT("AC2Smoke"));
    DatDirectory = AC2::ResolveDatDirectory(DatDirectory);
    bUISmoke = FParse::Param(FCommandLine::Get(), TEXT("AC2UISmoke"));
    bMapSmoke = FParse::Param(FCommandLine::Get(), TEXT("AC2MapSmoke"));
    bControllerSmoke = FParse::Param(FCommandLine::Get(), TEXT("AC2ControllerSmoke"));
    CharacterSavePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Characters/characters.json"));
    if (bSmoke || bUISmoke || bMapSmoke || bControllerSmoke)
        CharacterSavePath = FPaths::Combine(
            FPaths::ProjectSavedDir(), TEXT("Diagnostics"),
            TEXT("UI-test-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
    const bool Online = FParse::Param(FCommandLine::Get(), TEXT("AC2Online"));
    CharacterDataReady = CharacterCatalog.Open(DatDirectory, CharacterError) &&
                         (Online || Characters.Load(CharacterSavePath, CharacterCatalog, CharacterError));
    if (!CharacterDataReady && CharacterError.IsEmpty())
        CharacterError = TEXT("The installed DATs use an unsupported character-data format.");
    FParse::Value(FCommandLine::Get(), TEXT("AC2Block="), StartBlock);
    FParse::Value(FCommandLine::Get(), TEXT("AC2Radius="), ViewRadius);
    ViewRadius = FMath::Clamp(ViewRadius, 1, 6);
    const bool Automated=bSmoke||bUISmoke||bMapSmoke||FParse::Param(FCommandLine::Get(),TEXT("AC2NetworkSmoke"));
    if(!Automated)return; // No world actors, sky or default avatar before login/selection.
    FrontEnd=false;LoginStarted=true;
    if(!OpenWorld())return;
    if(auto* P=Cast<AAC2Explorer>(GetWorld()->GetFirstPlayerController()->GetPawn()))
    {P->ApplyAvatar(0x470000cd,1,1);P->SetActorHiddenInGame(false);}
    if (Online)
    {
        CharacterUIStarted = true;
        bWaiting = true;
        Network = MakeUnique<FAC2Network>(this);
        if (auto* H = Cast<AAC2HUD>(GetWorld()->GetFirstPlayerController()->GetHUD()))
        { H->InitializeCharacterUI(); if(H->CharacterUI) H->CharacterUI->Open(SAC2CharacterUI::EScreen::Select); }
        if (auto* P = Cast<AAC2Explorer>(GetWorld()->GetFirstPlayerController()->GetPawn())) P->SetHeld(true);
        Network->Start();
        return;
    }
    if (!AC2::ParseBlock(StartBlock, InitialBlock) || !LoadBlock(InitialBlock))
    {
        if (bSmoke)
            FinishSmoke(false, TEXT("Invalid start block"));
    }
}
bool AAC2GameMode::OpenWorld()
{
    if(bReady)return true;
    FString Error;
    Stream = MakeUnique<FAC2WorldStream>();
    Stream->Radius = ViewRadius;
    Sky = MakeUnique<FAC2SkyRenderer>();
    if (!Stream->Open(GetWorld(), DatDirectory, Error) || !Sky->Open(GetWorld(), DatDirectory, Error))
    {
        Status = Error;
        UE_LOG(LogTemp, Error, TEXT("AC2 startup: %s"), *Error);
        if (bSmoke)
            FinishSmoke(false, Error);
        return false;
    }
    float Hour = 12;
    FParse::Value(FCommandLine::Get(), TEXT("AC2Hour="), Hour);
    Sky->SetHour(Hour);
    if (bSmoke)
        Sky->TimeScale = 0;
    Far = MakeUnique<FAC2FarTerrain>();
    if (!Far->Open(GetWorld(), DatDirectory, Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("AC2 distant terrain: %s"), *Error);
        Far.Reset();
    }
    Effects = MakeUnique<FAC2Effects>();
    Effects->Open(GetWorld(), DatDirectory);
    bReady = true;
    return true;
}
void AAC2GameMode::LeaveWorld()
{
    FrontEnd=true;OfflineEntering=false;NetworkTravel=false;bWaiting=true;
    if(auto* PC=GetWorld()->GetFirstPlayerController())
        if(auto* P=Cast<AAC2Explorer>(PC->GetPawn())){P->SetHeld(true);P->SetActorHiddenInGame(true);}
    for(auto& M:Mobs)if(M.IsValid())M->Destroy();Mobs.Reset();Target=nullptr;
    if(Stream)Stream->Shutdown();
    Stream.Reset();Far.Reset();Effects.Reset();Sky.Reset();bReady=false;
    if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* H=Cast<AAC2HUD>(PC->GetHUD()))if(H->GameUI)H->GameUI->ClosePanels();
}
void AAC2GameMode::Recall()
{
    if(!InWorld())return;
    if(Network){Network->Recall();return;}
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(!P||bWaiting)return;
    // Offline has no server binding record; return to this session's safe start.
    uint32 Block=0;
    if(!AC2::ParseBlock(StartBlock,Block))return;
    P->SetDead(false);P->PlayBehavior(AC2Motion::Recall,AC2Motion::Melee);
    if(auto* C=Characters.Active()){C->Health=100;C->Vigor=100;}
    LoadBlock(Block);Notice=TEXT("Returning to offline start");
}
void AAC2GameMode::ShowLogin(const FString& Message)
{
    LeaveWorld();LoginStarted=true;CharacterUIStarted=true;
    if(auto* PC=GetWorld()->GetFirstPlayerController())
        if(auto* H=Cast<AAC2HUD>(PC->GetHUD()))
        {H->InitializeCharacterUI();if(H->CharacterUI)H->CharacterUI->Open(SAC2CharacterUI::EScreen::Login,Message);}
}
void AAC2GameMode::SignOut()
{
    Network.Reset();Characters=FAC2CharacterStore();ShowLogin();
}
void AAC2GameMode::Connect(const FAC2ConnectionSettings& Settings)
{
    LeaveWorld();Network.Reset();Characters=FAC2CharacterStore();
    Network=MakeUnique<FAC2Network>(this);
    if(auto* H=Cast<AAC2HUD>(GetWorld()->GetFirstPlayerController()->GetHUD()))
        if(H->CharacterUI)H->CharacterUI->Open(SAC2CharacterUI::EScreen::Select);
    Network->Start(&Settings);
}
void AAC2GameMode::Offline()
{
    Network.Reset();Characters=FAC2CharacterStore();
    if(!Characters.Load(CharacterSavePath,CharacterCatalog,CharacterError)){ShowLogin(CharacterError);return;}
    if(auto* H=Cast<AAC2HUD>(GetWorld()->GetFirstPlayerController()->GetHUD()))
        if(H->CharacterUI)H->CharacterUI->Open(Characters.Characters.IsEmpty()?SAC2CharacterUI::EScreen::Create:SAC2CharacterUI::EScreen::Select);
}
bool AAC2GameMode::EnterOfflineCharacter()
{
    if(!FrontEnd)return PlaceCharacter();
    if(!OpenWorld())return false;
    if(!AC2::ParseBlock(StartBlock,InitialBlock)||!LoadBlock(InitialBlock))return false;
    OfflineEntering=true;
    return true;
}
void AAC2GameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    Network.Reset();
    if (Stream)
        Stream->Shutdown();
    Far.Reset();
    Effects.Reset();
    Sky.Reset();
    Stream.Reset();
    Super::EndPlay(Reason);
}
bool AAC2GameMode::LoadBlock(uint32 Block)
{
    if (Network) { Notice=TEXT("Travel is controlled by the server."); return false; }
    if (!bReady)
        return false;
    FString Error;
    if (!Stream->Destination(Block, Destination, Error))
    {
        Status = Error;
        UE_LOG(LogTemp, Warning, TEXT("AC2 destination %04X: %s"), Block, *Error);
        return false;
    }
    PendingCell = 0;
    InteriorBlock = 0;
    Stream->SetInteriorCell(0);
    bTeleport = true;
    bWaiting = true;
    Notice = TEXT("Traveling...");
    StreamFocus = Destination;
    PositionViewer(GetWorld()->GetFirstPlayerController());
    UE_LOG(LogTemp, Display, TEXT("AC2 travel requested: %04X"), Block);
    return true;
}
bool AAC2GameMode::TravelToMap(FVector2D Land)
{
    if (!InWorld() || !bReady) { Notice = TEXT("Enter the world before using map travel."); return false; }
    FVector Point(Land.X * 2000., Land.Y * 2000., 0);
    uint32 Block;
    if (!AC2::WorldBlock(Point, Block)) { Notice = TEXT("That point is outside the world."); return false; }
    if (Network)
    {
        if (!Network->MapTravelEnabled) { Notice = TEXT("Map travel is available offline or on a local AC2E testing server."); return false; }
        Network->Chat(FString::Printf(TEXT("@maptravel %.5f %.5f"), Land.X, Land.Y));
        Notice = TEXT("Requesting map travel...");
        return true;
    }
    FString Error;
    if (!Stream->Destination(Block, Point, Error)) { Notice = Error; return false; }
    // Destination verifies the block and supplies a height hint. Preserve the clicked XY.
    Destination = FVector(Land.X * 2000., Land.Y * 2000., Point.Z);
    PendingCell = InteriorBlock = 0;
    Stream->SetInteriorCell(0);
    bTeleport = bWaiting = true;
    StreamFocus = Destination;
    Notice = TEXT("Traveling...");
    PositionViewer(GetWorld()->GetFirstPlayerController());
    if (auto* PC = Cast<AAC2PlayerController>(GetWorld()->GetFirstPlayerController()))
        PC->SetCursor(bMap || !PC->WantsMouseLook());
    return true;
}
void AAC2GameMode::PositionViewer(APlayerController *PC)
{
    if (!PC || !PC->GetPawn() || !bReady)
        return;
    if (auto *P = Cast<AAC2Explorer>(PC->GetPawn()))
    {
        P->SetHeld(true);
        P->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics);
        P->SetWater(false, 0);
        P->SetFlying(false);
    }
    PC->SetControlRotation(FRotator(-5, 45, 0));
    if(auto* Controller=Cast<AAC2PlayerController>(PC))Controller->RestoreCursor();
}
void AAC2GameMode::CompleteTravel(const FVector &Position)
{
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *P = PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
    if (!P)
        return;
    P->GetCharacterMovement()->StopMovementImmediately();
    P->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    P->SetHeld(false);
    LastSafe = Position;
    bWaiting = bTeleport = false;
    Notice = TEXT("Arrived");
    for (const auto &Mob : Mobs)
        if (Mob.IsValid())
            Mob->Destroy();
    Mobs.Reset();
    Target = nullptr;
    bSpawned = false;
    if (bSmoke)
        StageTime = 0;
    UE_LOG(LogTemp, Display, TEXT("AC2 clear landing: %s capsule=%g/%g"), *Position.ToString(),
           P->GetCapsuleComponent()->GetScaledCapsuleRadius(),
           P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}
bool AAC2GameMode::PlaceCharacter()
{
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *P = PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
    if (!P || !Stream || bWaiting)
        return false;
    const auto *Capsule = P->GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius(), Half = Capsule->GetScaledCapsuleHalfHeight();
    const FVector Center = P->GetActorLocation();
    if ((P->IsFlying() || P->IsSwimming()) && AC2::CapsuleClear(GetWorld(), Center, Radius, Half, P))
    {
        LastSafe = Center;
        return true;
    }
    // Resizing a Lugian/Empyrean must not embed the new capsule in a nearby wall or ceiling.
    for (int32 Ring = 0; Ring <= 4; ++Ring)
        for (int32 Angle = 0; Angle < (Ring ? 8 : 1); ++Angle)
        {
            FVector Candidate = Center + FRotator(0, Angle * 45, 0).Vector() * (Ring * 100);
            double Z;
            if (!Stream->GroundAt(Candidate, Z))
                continue;
            Candidate.Z = Z;
            FVector Landing;
            if (AC2::StandingPosition(GetWorld(), Candidate, Radius, Half, Landing, P))
            {
                P->SetActorLocation(Landing, false, nullptr, ETeleportType::TeleportPhysics);
                LastSafe = Landing;
                StreamFocus = Landing;
                return true;
            }
        }
    return false;
}
void AAC2GameMode::RestartPlayer(AController *Player)
{
    Super::RestartPlayer(Player);
    PositionViewer(Cast<APlayerController>(Player));
}
void AAC2GameMode::ToggleFlight()
{
    if (Network) return;
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *P = PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
    if (!P || !bReady)
        return;
    if (P->IsFlying())
    {
        double Z;
        if (!Stream->GroundAt(P->GetActorLocation(), Z))
        {
            Status = TEXT("No outdoor floor here. Stay in flight to cross this area.");
            return;
        }
        FVector Landing;
        const auto *Capsule = P->GetCapsuleComponent();
        if (!AC2::StandingPosition(GetWorld(), FVector(P->GetActorLocation().X, P->GetActorLocation().Y, Z),
                                   Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
                                   Landing, P))
        {
            Notice = TEXT("No clear standing space below. Move away from the obstruction before landing.");
            return;
        }
        P->SetActorLocation(Landing, false, nullptr, ETeleportType::TeleportPhysics);
        P->GetCharacterMovement()->StopMovementImmediately();
        P->SetFlying(false);
    }
    else
        P->SetFlying(true);
}
void AAC2GameMode::SetHour(float Hour)
{
    if (Sky && FMath::IsFinite(Hour))
        Sky->SetHour(Hour);
}
void AAC2GameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!LoginStarted){ShowLogin();return;}
    if(bControllerSmoke)ControllerSmokeTick(Delta);
    if(Network)Network->Tick(Delta);
    if(Network && Network->HasPlayerPose && !bReady)OpenWorld();
    if(!bReady)return;
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *P = PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
    if (!P)
        return;
    if (Network)
    {
        if(Network->IsWorld()&&!bWaiting&&!Network->NeedsPlacement)
        {
            uint32 Cell;
            if(Stream->InteriorCellAt(P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Cell))
            {Network->SetCell(Cell);Stream->SetInteriorCell(Cell);}
            else if(!InteriorBlock && AC2::WorldBlock(P->GetActorLocation(),Cell))Network->SetCell((Cell<<16)|1);
        }
        if (!Network->HasPlayerPose) return;
        if (Network->NeedsPlacement && (!NetworkTravel || !Network->Feet.Equals(Destination, .01)))
        {
            FString Error;
            if (!Stream->Destination(Network->Cell >> 16, Destination, Error))
            { Notice=Error; return; }
            // A second map request can replace an obstructed arrival while loading.
            Destination = Network->Feet;
            NetworkTravel=true; bWaiting=true; bTeleport=false; PendingCell=0;
            InteriorBlock=(Network->Cell & 65535)>=256 ? Network->Cell>>16 : 0;
            Stream->SetInteriorCell(Network->Cell);
            StreamFocus=AC2::BlockOrigin(Network->Cell>>16)+FVector(8000,8000,Network->Feet.Z);
            P->SetHeld(true); P->SetActorLocation(Network->Feet+FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        }
        if (NetworkTravel)
        {
            Stream->Tick(StreamFocus);
            if (!Stream->PlacementReadyAt(StreamFocus)) return;
            const auto* Capsule = P->GetCapsuleComponent();
            FVector Center;
            if (!Stream->NetworkLanding(Network->Feet, Network->Cell, Capsule->GetScaledCapsuleRadius(),
                                        Capsule->GetScaledCapsuleHalfHeight(), Center, P))
            { Notice=TEXT("No clear arrival space near the server position."); return; }
            const FVector LandedFeet = Center - FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
            UE_LOG(LogTemp, Display, TEXT("AC2 network landing: cell=%08X correction=%s capsule=%g/%g"),
                   Network->Cell, *(LandedFeet-Network->Feet).ToString(), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
            Network->Feet = LandedFeet;
            uint32 ArrivalCell;
            if (InteriorBlock && Stream->InteriorCellAt(LandedFeet, ArrivalCell)) Network->SetCell(ArrivalCell);
            P->GetCharacterMovement()->StopMovementImmediately();
            P->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
            P->SetActorRotation(FRotator(0,90-Network->Heading,0));
            PC->SetControlRotation(FRotator(-5,90-Network->Heading,0));
            P->SetHeld(false); LastSafe=Center; bWaiting=false; NetworkTravel=false; InteriorGrace=2;
            if(FrontEnd)if(auto* H=Cast<AAC2HUD>(PC->GetHUD()))if(H->GameUI)H->GameUI->RestoreOptions();
            FrontEnd=false;P->SetActorHiddenInGame(false);
            if (!Network->IsWorld()) Network->SceneComplete(); else Network->NeedsPlacement=false;
        }
    }
    FVector Here = P->GetActorLocation();
    if (!bWaiting && !CharacterUIStarted && !bSmoke)
    {
        if (auto *HUD = Cast<AAC2HUD>(PC->GetHUD()))
        {
            HUD->InitializeCharacterUI();
            if (HUD->CharacterUI)
            {
                CharacterUIStarted = true;
                HUD->CharacterUI->Open(Characters.Characters.IsEmpty() ? SAC2CharacterUI::EScreen::Create
                                                                       : SAC2CharacterUI::EScreen::Select);
            }
        }
    }
    if(OfflineEntering&&!bWaiting)
    {
        OfflineEntering=false;FrontEnd=false;P->SetActorHiddenInGame(false);P->SetDead(false);P->SetHeld(false);
        if(auto* H=Cast<AAC2HUD>(PC->GetHUD()))if(H->GameUI)H->GameUI->RestoreOptions();
        if(auto* H=Cast<AAC2HUD>(PC->GetHUD()))if(H->CharacterUI)H->CharacterUI->NetworkEntered();
    }
    if (bUISmoke)
        UISmokeTick(Delta);
    if (bMapSmoke)
        MapSmokeTick(Delta);
    AttackCooldown = FMath::Max(0.f, AttackCooldown - Delta);
    if (!bWaiting)
    {
        uint32 Inside;
        if (Stream->InteriorAt(Here, Inside))
        {
            InteriorBlock = Inside;
            InteriorGrace = 2;
            uint32 Cell;
            if (Stream->InteriorCellAt(Here - FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), Cell))
                Stream->SetInteriorCell(Cell);

            StreamFocus = AC2::BlockOrigin(Inside) + FVector(8000, 8000, Here.Z);
        }
        else if (InteriorBlock)
        {
            InteriorGrace -= Delta;
            if (InteriorGrace <= 0)
            { InteriorBlock = 0; Stream->SetInteriorCell(0); }
        }
        if (InteriorBlock)
        {
            double Floor;
            if (!P->IsFlying() && !Stream->GroundAt(Here, Floor))
            {
                P->SetActorLocation(LastSafe);
                P->GetCharacterMovement()->StopMovementImmediately();
            }
            else
                LastSafe = Here;
        }
    }
    if (!bWaiting && !InteriorBlock)
    {
        uint32 Block;
        if (!AC2::WorldBlock(Here, Block))
        {
            P->SetActorLocation(LastSafe);
            P->GetCharacterMovement()->StopMovementImmediately();
            Here = LastSafe;
        }
        else if (!Stream->ReadyAt(Here))
        {
            StreamFocus = Here;
            bWaiting = true;
            P->SetHeld(true);
            P->SetActorLocation(LastSafe);
        }
        else
        {
            double Ground;
            if (!P->IsFlying() && (!Stream->GroundAt(Here, Ground) || Here.Z < Ground - 150))
            {
                P->SetActorLocation(LastSafe);
                P->GetCharacterMovement()->StopMovementImmediately();
                Here = LastSafe;
            }
            else
                LastSafe = Here;
            StreamFocus = Here;
        }
    }
    Stream->Tick(StreamFocus);
    if (PendingCell && Stream->AllReady())
    {
        FVector Cell;
        if (Stream->CellPosition(PendingCell, Cell))
        {
            CompleteTravel(Cell);
            InteriorBlock = PendingCell >> 16;
            InteriorGrace = 2;
            float BestDistance = -1, BestYaw = 0;
            FVector Eye = Cell + FVector(0, 0, 78);
            FCollisionQueryParams Q;
            Q.AddIgnoredActor(P);
            for (int32 Angle = 0; Angle < 8; ++Angle)
            {
                FVector End = Eye + FRotator(0, Angle * 45, 0).Vector() * 2500;
                FHitResult Hit;
                float Distance = GetWorld()->LineTraceSingleByObjectType(
                                     Hit, Eye, End, FCollisionObjectQueryParams(ECC_WorldStatic), Q)
                                     ? Hit.Distance
                                     : 2500;
                if (Distance > BestDistance)
                {
                    BestDistance = Distance;
                    BestYaw = Angle * 45;
                }
            }
            PC->SetControlRotation(FRotator(0, BestYaw, 0));
            Notice = FString::Printf(TEXT("Interior %08X"), PendingCell);
        }
        else
            Notice = TEXT("Cell has no clear landing; choosing a safe outdoor position");
        PendingCell = 0;
    }
    if (bWaiting && !PendingCell && Stream->ReadyAt(StreamFocus))
    {
        if (bTeleport)
        {
            if (Stream->PlacementReadyAt(StreamFocus))
            {
                FVector Landing;
                const auto *Capsule = P->GetCapsuleComponent();
                if (Stream->LandingPosition(StreamFocus, Landing, Capsule->GetScaledCapsuleRadius(),
                                            Capsule->GetScaledCapsuleHalfHeight()))
                    CompleteTravel(Landing);
                else
                    Notice = TEXT("No clear landing in this block. Choose another destination.");
            }
        }
        else
        {
            double Z = 0;
            if ((P->IsFlying() || Stream->GroundAt(StreamFocus, Z)) &&
                AC2::CapsuleClear(GetWorld(), StreamFocus, P->GetCapsuleComponent()->GetScaledCapsuleRadius(),
                                  P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), P))
            {
                P->SetActorLocation(StreamFocus, false, nullptr, ETeleportType::TeleportPhysics);
                P->SetHeld(false);
                LastSafe = StreamFocus;
                bWaiting = false;
            }
        }
    }
    double Water = 0;
    const bool Wet = !InteriorBlock && Stream->WaterAt(P->GetActorLocation(), Water);
    P->UpdateWater(Wet, Water);
    Sky->Update(P->Camera->GetComponentLocation(), Delta);
    if (Far)
    {
        Far->Tick(StreamFocus, ViewRadius, Stream->AllReady());
        Far->SetVisible(Stream->ExteriorVisible());
        uint32 Center = 0;
        AC2::WorldBlock(StreamFocus, Center);
        Stream->SetDrawRadius(Far->Center() == Center ? ViewRadius : ViewRadius + 1);
    }
    RoofTime -= Delta;
    if (RoofTime <= 0)
    {
        RoofTime = .5f;
        FHitResult Hit;
        FCollisionQueryParams Q;
        Q.AddIgnoredActor(P);
        FVector Eye = P->Camera->GetComponentLocation();
        bIndoors = InteriorBlock ||
                   GetWorld()->LineTraceSingleByObjectType(Hit, Eye, Eye + FVector(0, 0, 10000),
                                                           FCollisionObjectQueryParams(ECC_WorldStatic), Q);
    }
    auto K = Sky->Lighting();
    EffectSync -= Delta;
    if (EffectSync <= 0)
    {
        EffectSync = .5f;
        Effects->SyncSources(Stream->ParticleSources(P->GetActorLocation()));
    }
    Effects->Update(P->Camera->GetComponentLocation(), P->Camera->GetRightVector(), P->Camera->GetUpVector(),
                    Delta, Sky->RainOverride >= 0 ? Sky->RainOverride : K.Rain, FVector(K.WindX, K.WindY, 0),
                    bIndoors);
    for (int32 I = Mobs.Num() - 1; I >= 0; --I)
        if (!Mobs[I].IsValid())
            Mobs.RemoveAtSwap(I);
        else if (FVector::Dist2D(Mobs[I]->GetActorLocation(), P->GetActorLocation()) > 22000)
        {
            Mobs[I]->Destroy();
            Mobs.RemoveAtSwap(I);
        }
        else
            Mobs[I]->SetViewerDistance(FVector::Dist(Mobs[I]->GetActorLocation(), P->GetActorLocation()));
    if (Target && (!IsValid(Target) || Target->Health <= 0))
        Target = nullptr;
    if (!Network && !bSpawned && Stream->AllReady() && !bWaiting)
    {
        bSpawned = true;
        CreatureIndex = 0;
        for (int32 I = 0; I < (InteriorBlock ? 3 : UE_ARRAY_COUNT(AC2CreaturePreviews)); ++I)
            SpawnMob();
    }

    const int32 Hour = int32(Sky->Hour()), Minute = int32((Sky->Hour() - Hour) * 60);
    Status = FString::Printf(TEXT("%s\n%s | %02d:%02d | native sky %08X (%d objects)%s"), *Stream->Status(),
                             P->IsFlying()     ? TEXT("FLY")
                             : P->IsSwimming() ? TEXT("SWIM")
                                               : TEXT("WALK"),
                             Hour, Minute, Sky->DayId(), Sky->ObjectCount(),
                             bWaiting ? (Stream->FailedAt(StreamFocus)
                                             ? TEXT(" | block unavailable; use AC2Block to travel")
                                             : TEXT(" | loading nearby ground"))
                                      : TEXT(""));
    Status +=
        FString::Printf(TEXT("\n%d water / %d interior cells / %d distant tiles / %d NPCs / %d particles%s"),
                        Stream->WaterCount(), Stream->InteriorCount(), Far ? Far->TileCount() : 0, Mobs.Num(),
                        Effects->Count(), bIndoors ? TEXT(" | indoors") : TEXT(""));
    Status += FString::Printf(TEXT(" / %d scenery FX systems / %d native particles"), Effects->SourceCount(),
                              Effects->NativeCount());
    if (bSmoke)
        SmokeTick(Delta);
}
void AAC2GameMode::SmokeTick(float Delta)
{
    SmokeTime += Delta;
    StageTime += Delta;
    auto *PC = GetWorld()->GetFirstPlayerController();
    auto *P = Cast<AAC2Explorer>(PC->GetPawn());
    if (SmokeTime > 150)
    {
        FinishSmoke(false, TEXT("Streaming smoke exceeded 150 seconds"));
        return;
    }
    if (SmokeStage == 0 && Stream->AllReady() && !bWaiting && StageTime > 2)
    {
        double Z;
        if (!Stream->GroundAt(P->GetActorLocation(), Z) || !P->GetCharacterMovement()->IsMovingOnGround() ||
            Sky->ObjectCount() == 0 || Stream->SceneryCount() == 0 || Stream->GrassBatchCount() == 0 ||
            !P->HasModel() || Mobs.IsEmpty() || !Stream->GrassDepthReady())
        {
            FinishSmoke(false, TEXT("Initial walking floor, scenery/grass or native sky missing"));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: walking floor, uploaded grass and native sky PASS"));
        auto *HUD = Cast<AAC2HUD>(PC->GetHUD());
        if (!HUD || !HUD->GameUI)
        {
            FinishSmoke(false, TEXT("Native HUD art unavailable"));
            return;
        }
        HUD->GameUI->ToggleMap();
        if (!bMap)
        {
            FinishSmoke(false, TEXT("HUD action dispatch failed"));
            return;
        }
        HUD->GameUI->ToggleMap();
        TargetNext();
        if (!Target)
        {
            FinishSmoke(false, TEXT("Offline target missing"));
            return;
        }
        // Exercise damage and a native rendered NPC without depending on random wander paths.
        Target->SetActorLocation(P->GetActorLocation() + FVector(170, 0, 0));
        Attack();
        if (Target->Health != 75)
        {
            FinishSmoke(false, TEXT("Offline hit failed"));
            return;
        }
        CastBurst();
        Rain(1);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2Player.png")), true, false);
        SmokeStart = P->GetActorLocation();
        SmokeStage = 8;
        StageTime = 0;
    }
    else if (SmokeStage == 8)
    {
        if (StageTime < 1)
            P->AddMovementInput(FVector::YAxisVector, 1);
        else if (StageTime > 1.5f)
        {
            // Native backward/strafe walking is slower than the old fixed 650 cm/s.
            if (FVector::Dist2D(SmokeStart, P->GetActorLocation()) < 90 ||
                !P->GetCharacterMovement()->IsMovingOnGround())
            {
                FinishSmoke(false, FString::Printf(TEXT("Ground walking: %.1f cm, supported=%d"),
                    FVector::Dist2D(SmokeStart,P->GetActorLocation()),P->GetCharacterMovement()->IsMovingOnGround()));
                return;
            }
            SmokeStart = P->GetActorLocation();
            P->Jump();
            SmokeStage = 9;
            StageTime = 0;
        }
    }
    else if (SmokeStage == 9)
    {
        bSmokeJumped |= P->GetCharacterMovement()->IsFalling() && P->GetActorLocation().Z > SmokeStart.Z + 30;
        if (StageTime > 2)
        {
            if (!bSmokeJumped || !P->GetCharacterMovement()->IsMovingOnGround())
            {
                FinishSmoke(false, TEXT("Jump and landing did not complete"));
                return;
            }
            if (Effects->Count() == 0 || !Far || Far->TileCount() == 0)
            {
                FinishSmoke(false, TEXT("Weather particles or distant terrain missing"));
                return;
            }
            Rain(-1);
            UE_LOG(LogTemp, Display,
                   TEXT("AC2 world smoke: ground movement, native body, local combat, weather particles, "
                        "distant LOD, "
                        "jump and landing PASS"));
            SmokeStart = P->GetActorLocation();
            P->SetFlying(true);
            P->SetActorLocation(SmokeStart + FVector(0, 0, 20000));
            SmokeStage = 1;
            StageTime = 0;
        }
    }
    else if (SmokeStage == 1)
    {
        if (P->GetActorLocation().X < SmokeStart.X + AC2::BlockCentimetres * 2.2)
            P->AddMovementInput(FVector::XAxisVector, 1);
        else
        {
            P->GetCharacterMovement()->StopMovementImmediately();
            if (Stream->AllReady() && !bWaiting)
            {
                if (Stream->UnloadedTotal == 0)
                {
                    FinishSmoke(false, TEXT("Distant blocks were not unloaded"));
                    return;
                }
                UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: continuous block crossing PASS; %s"),
                       *Stream->Status());
                LoadBlock(0x9882);
                SmokeStage = 2;
                StageTime = 0;
            }
        }
    }
    else if (SmokeStage == 2 && Stream->AllReady() && !bWaiting)
    {
        double Z;
        if (!Stream->GroundAt(P->GetActorLocation(), Z))
        {
            FinishSmoke(false, TEXT("Destination collision missing"));
            return;
        }
        if (Stream->ResidentCount() > FMath::Square(2 * (ViewRadius + 1) + 1))
        {
            FinishSmoke(false, TEXT("Residency exceeded bound"));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: distant travel and bounded residency PASS; %s"),
               *Stream->Status());
        LoadBlock(InitialBlock);
        SmokeStage = 3;
        StageTime = 0;
    }
    else if (SmokeStage == 3 && Stream->AllReady() && !bWaiting)
    {
        P->SetFlying(true);
        P->SetActorLocation(P->GetActorLocation() + FVector(-1500, -1500, 2200));
        PC->SetControlRotation(FRotator(-8, 45, 0));
        GEngine->ForceGarbageCollection(true);
        Rain(1);
        SmokeStage = 4;
        StageTime = 0;
    }
    else if (SmokeStage == 4 && StageTime > 3)
    {
        if (Effects->RainCount() <= 0)
        {
            FinishSmoke(false, TEXT("Outdoor rain emission failed"));
            return;
        }
        UE_LOG(LogTemp, Display,
               TEXT("AC2 world smoke: post-unload garbage collection and outdoor rain PASS (%d streaks)"),
               Effects->RainCount());
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2World.png")), true, false);
        SmokeStage = 5;
        StageTime = 0;
    }
    else if (SmokeStage == 5 && StageTime > 2)
    {
        Rain(-1);
        Sky->SetHour(0);
        SmokeStage = 6;
        StageTime = 0;
    }
    else if (SmokeStage == 6 && StageTime > 3)
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().AddWeakLambda(this, [this]() {
            if (SmokeStage == 7)
            {
                Sky->SetHour(12);
                LoadBlock(0x5561);
                SmokeStage = 10;
                StageTime = 0;
            }
        });
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2WorldNight.png")), true, false);
        SmokeStage = 7;
    }
    else if (SmokeStage == 10 && Stream->AllReady() && !bWaiting && StageTime > 3)
    {
        if (Stream->WaterCount() == 0 || !P->IsSwimming())
        {
            FinishSmoke(false, TEXT("Native water/swimming missing at 5561"));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: native water and buoyancy PASS"));
        PC->SetControlRotation(FRotator(-15, 180, 0));
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2Water.png")), true, false);
        SmokeStage = 11;
        StageTime = 0;
    }
    else if (SmokeStage == 11 && StageTime > 2)
    {
        LoadCell(0x58700100);
        SmokeStage = 12;
        StageTime = 0;
    }
    else if (SmokeStage == 12 && Stream->AllReady() && !bWaiting && StageTime > 3)
    {
        double Z;
        if (InteriorBlock != 0x5870 || Stream->InteriorCount() == 0 ||
            !Stream->GroundAt(P->GetActorLocation(), Z) || !P->GetCharacterMovement()->IsMovingOnGround())
        {
            FinishSmoke(false,
                        FString::Printf(TEXT("Interior render/collision/travel failed: owner=%04X cells=%d "
                                             "movement=%d notice=%s position=%s"),
                                        InteriorBlock, Stream->InteriorCount(),
                                        int32(P->GetCharacterMovement()->MovementMode), *Notice,
                                        *P->GetActorLocation().ToString()));
            return;
        }
        UE_LOG(LogTemp, Display,
               TEXT("AC2 world smoke: native interior render and walking floor PASS (%d cells)"),
               Stream->InteriorCount());
        P->ToggleView();
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2Interior.png")), true, false);
        SmokeStage = 13;
        StageTime = 0;
    }
    else if (SmokeStage == 13 && StageTime > 2)
    {
        LoadCell(0x59600110);
        Rain(1);
        SmokeStage = 15;
        StageTime = 0;
    }
    else if (SmokeStage == 15 && Stream->AllReady() && !bWaiting && StageTime > 3)
    {
        double Z;
        if (InteriorBlock != 0x5960 || !Stream->GroundAt(P->GetActorLocation(), Z) ||
            Stream->InteriorCount() < 20 || !P->GetCharacterMovement()->IsMovingOnGround())
        {
            FinishSmoke(false, TEXT("Dungeon cell population/floor missing"));
            return;
        }
        if (Mobs.IsEmpty())
        {
            FinishSmoke(false, TEXT("Dungeon offline NPC population missing"));
            return;
        }
        for (const auto &Mob : Mobs)
            if (!Mob.IsValid() || !Mob->GetCharacterMovement()->IsMovingOnGround() ||
                !AC2::CapsuleClear(GetWorld(), Mob->GetActorLocation() + FVector(0, 0, 1),
                                   Mob->GetCapsuleComponent()->GetScaledCapsuleRadius() - 1,
                                   Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1, Mob.Get()))
            {
                FinishSmoke(false, TEXT("Dungeon NPC did not settle on a clear floor"));
                return;
            }
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: %d grounded dungeon NPCs PASS"), Mobs.Num());
        if (Effects->RainCount() != 0 || !bIndoors)
        {
            FinishSmoke(false, TEXT("Dungeon precipitation suppression failed"));
            return;
        }
        Rain(-1);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2Dungeon.png")), true, false);
        SmokeStart = P->GetActorLocation();
        SmokeStage = 16;
        StageTime = 0;
    }
    else if (SmokeStage == 16 && StageTime < .7f)
        P->AddMovementInput(PC->GetControlRotation().Vector(), 1);
    else if (SmokeStage == 16 && StageTime > 2)
    {
        if (FVector::Dist2D(SmokeStart, P->GetActorLocation()) < 100 ||
            !P->GetCharacterMovement()->IsMovingOnGround())
        {
            FinishSmoke(false, TEXT("Dungeon walking did not remain supported"));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: settled dungeon travel and corridor walking PASS"));
        LoadBlock(0xc654);
        SmokeStage = 17;
        StageTime = 0;
    }
    else if (SmokeStage == 17 && Stream->AllReady() && !bWaiting && StageTime > 3)
    {
        auto Sources = Stream->ParticleSources(P->GetActorLocation());
        if (Sources.IsEmpty() || Effects->SourceCount() == 0)
        {
            FinishSmoke(false, TEXT("C654 native scenery startup FX missing"));
            return;
        }
        P->SetFlying(true);
        const FVector Fire = Sources[0].Frame.GetLocation();
        P->SetActorLocation(Fire + FVector(-600, -600, 250));
        PC->SetControlRotation((Fire - P->GetActorLocation()).Rotation());
        if (auto *Controller = Cast<AAC2PlayerController>(PC))
            Controller->SetCursor(true);
        SmokeStage = 18;
        StageTime = 0;
    }
    else if (SmokeStage == 18 && StageTime > 2)
    {
        if (Effects->NativeCount() == 0)
        {
            FinishSmoke(false, TEXT("Scenery emitters did not produce native particles"));
            return;
        }
        TSet<uint32> Species;
        for (const auto &Mob : Mobs)
            if (Mob.IsValid() && Mob->HasAnimation() && Mob->MovementAnimationUpdates() > 10)
            {
                Species.Add(Mob->VisualId);
                UE_LOG(LogTemp, Display, TEXT("AC2 walking creature %08X: %d moving pose updates"),
                       Mob->VisualId, Mob->MovementAnimationUpdates());
            }
        if (Species.Num() != UE_ARRAY_COUNT(AC2CreaturePreviews) || !Stream->GrassDepthReady())
        {
            FinishSmoke(false, TEXT("Native creature locomotion or grass depth pass missing"));
            return;
        }
        UE_LOG(LogTemp, Display,
               TEXT("AC2 world smoke: five native walking creature species and depth-writing grass PASS"));
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2SceneryFX.png")), true, false);
        UE_LOG(
            LogTemp, Display,
            TEXT("AC2 world smoke: native HUD actions, scenery startup FX PASS (%d systems / %d particles)"),
            Effects->SourceCount(), Effects->NativeCount());
        SmokeStage = 22;
        StageTime = 0;
    }
    else if (SmokeStage == 22 && StageTime > .5f)
    {
        SmokeStart = P->GetActorLocation();
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2GrassBefore.png")), true, false);
        SmokeStage = 23;
        StageTime = 0;
    }
    else if (SmokeStage == 23 && StageTime > .5f)
    {
        P->SetActorLocation(SmokeStart + FVector(2, 0, 0));
        SmokeStage = 24;
        StageTime = 0;
    }
    else if (SmokeStage == 24 && StageTime > .5f)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2GrassAfter.png")), true, false);
        UE_LOG(LogTemp, Display, TEXT("AC2 grass comparison captured after a 2 cm camera translation"));
        SmokeStage = 25;
        StageTime = 0;
    }
    else if (SmokeStage == 25 && StageTime > .5f)
    {
        FVector Center = FVector::ZeroVector;
        for (const auto &Mob : Mobs)
            Center += Mob->GetActorLocation();
        Center /= Mobs.Num();
        P->SetActorLocation(Center + FVector(-1100, -1100, 650));
        PC->SetControlRotation((Center - P->GetActorLocation()).Rotation());
        SmokeStage = 26;
        StageTime = 0;
    }
    else if (SmokeStage == 26 && StageTime > 1)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2Creatures.png")), true, false);
        SmokeStage = 27;
        StageTime = 0;
    }
    else if (SmokeStage == 27 && StageTime > .3f)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2CreaturesMoving.png")), true,
            false);
        SmokeStage = 19;
        StageTime = 0;
    }
    else if (SmokeStage == 19 && StageTime > 2)
    {
        PreviewEffect = 0x39000000;
        CastBurst(PreviewEffect);
        SmokeStage = 20;
        StageTime = 0;
    }
    else if (SmokeStage == 20 && StageTime > .5f)
    {
        if (Effects->ActiveEmitters(PreviewEffect) < 2 || Effects->Count() > 4096)
        {
            FinishSmoke(false, TEXT("Multi-emitter preview failed"));
            return;
        }
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/AC2NativeFXUI.png")), true, false);
        UE_LOG(LogTemp, Display, TEXT("AC2 world smoke: multi-emitter preview PASS (%d active emitters)"),
               Effects->ActiveEmitters(PreviewEffect));
        SmokeStage = 21;
        StageTime = 0;
    }
    else if (SmokeStage == 21 && StageTime > 2)
    {
        if (auto *Controller = Cast<AAC2PlayerController>(PC))
            Controller->CloseUI();
        LoadBlock(InitialBlock);
        SmokeStage = 14;
        StageTime = 0;
    }
    else if (SmokeStage == 14 && Stream->AllReady() && !bWaiting && StageTime > 2)
        FinishSmoke(
            true,
            TEXT("native HUD, streamed scenery FX, multiple emitters, rain/shelter, player, NPC combat, "
                 "water, interiors, LOD, streaming and sky"));
}
void AAC2GameMode::FinishSmoke(bool Success, const FString &Message)
{
    bSmoke = false;
    UE_LOG(LogTemp, Display, TEXT("AC2 world smoke %s: %s"), Success ? TEXT("PASS") : TEXT("FAIL"), *Message);
    if (Success)
    {
        FString Report;
        if (FParse::Value(FCommandLine::Get(), TEXT("AC2SmokeReport="), Report))
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report), true);
            FFileHelper::SaveStringToFile(TEXT("PASS"), *Report);
        }
    }
    FPlatformMisc::RequestExitWithStatus(false, Success ? 0 : 1);
}
void AAC2PlayerController::AC2Block(const FString &Text)
{
    uint32 B;
    auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>();
    if (M && AC2::ParseBlock(Text, B))
        M->LoadBlock(B);
}
void AAC2PlayerController::AC2Fly()
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->ToggleFlight();
}
void AAC2PlayerController::AC2Time(float Hour)
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->SetHour(Hour);
}

void AAC2GameMode::LoadCell(uint32 Id)
{
    if ((Id & 65535) < 256 || !LoadBlock(Id >> 16))
        return;
    PendingCell = Id;
    Stream->SetInteriorCell(Id);
}
void AAC2GameMode::Weather(int32 Index)
{
    if (Sky)
    {
        Sky->SetWeather(Index < 0 ? Sky->WeatherIndex + 1 : Index);
        Notice = FString::Printf(TEXT("Native weather day %08X"), Sky->DayId());
    }
}
void AAC2GameMode::SpawnMob(uint32 Visual)
{
    if(Network) { Notice=TEXT("Population is controlled by the server."); return; }
    auto *PC = GetWorld()->GetFirstPlayerController();
    APawn *P = PC ? PC->GetPawn() : nullptr;
    if (!P || !Stream)
        return;
    if (Mobs.Num() >= 12)
    {
        Notice = TEXT("Test population limit reached (12)");
        return;
    }
    if (bWaiting)
        return;
    const float A = Mobs.Num() * 2.39996f;
    const FVector Preferred = P->GetActorLocation() + FVector(FMath::Cos(A), FMath::Sin(A), 0) * 600;
    if (!Visual)
        Visual = AC2CreaturePreviews[CreatureIndex++ % UE_ARRAY_COUNT(AC2CreaturePreviews)].Visual;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto *Mob =
        GetWorld()->SpawnActor<AAC2Mob>(Preferred + FVector(0, 0, 1000), FRotator::ZeroRotator, Params);
    if (!Mob)
        return;
    Mob->SetActorEnableCollision(false);
    if (!Mob->Initialize(DatDirectory, Visual))
    {
        Mob->Destroy();
        Notice = TEXT("Visual could not be decoded");
        return;
    }
    FVector Pos;
    if (!Stream->SpawnPosition(Preferred, InteriorBlock, Pos,
                               Mob->GetCapsuleComponent()->GetScaledCapsuleRadius(),
                               Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Mob))
    {
        Mob->Destroy();
        Notice = TEXT("No clear floor space nearby for this creature");
        return;
    }
    Mob->PlaceAt(Pos);
    Mob->DisplayName = FString::Printf(TEXT("Test creature %08X"), Visual);
    for (const auto &Creature : AC2CreaturePreviews)
        if (Creature.Visual == Visual)
            Mob->DisplayName = FString(Creature.Name) + TEXT(" (test)");
    Mobs.Add(Mob);
    Notice = TEXT("Spawned ") + Mob->DisplayName;
    UE_LOG(LogTemp, Display, TEXT("AC2 population: %s at %s owner=%04X"), *Mob->DisplayName, *Pos.ToString(),
           InteriorBlock);
}
void AAC2GameMode::TargetNext()
{
    if(Network) { Network->TargetNext(); return; }
    if (Mobs.IsEmpty())
    {
        Target = nullptr;
        return;
    }
    int32 I = Mobs.IndexOfByPredicate([&](const auto &M) { return M.Get() == Target; });
    for (int32 N = 0; N < Mobs.Num(); ++N)
    {
        auto *M = Mobs[(I + 1 + N) % Mobs.Num()].Get();
        if (M && M->Health > 0)
        {
            Target = M;
            return;
        }
    }
    Target = nullptr;
}
void AAC2GameMode::Attack()
{
    if(Network) { Network->Attack(); return; }
    auto *PC = GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->GetPawn() || AttackCooldown > 0)
        return;
    auto* Player=Cast<AAC2Explorer>(PC->GetPawn());
    if(!Player||Player->IsDead()||Player->IsHeld())return;
    if (!Target)
        TargetNext();
    if (!Target)
        return;
    if (FVector::Distance(Target->GetActorLocation(), PC->GetPawn()->GetActorLocation()) > 350)
    {
        Notice = TEXT("Move within 3.5 m of the target");
        return;
    }
    FHitResult Hit;
    FCollisionQueryParams Q;
    Q.AddIgnoredActor(PC->GetPawn());
    if (GetWorld()->LineTraceSingleByObjectType(Hit, PC->GetPawn()->GetActorLocation(),
                                                Target->GetActorLocation(),
                                                FCollisionObjectQueryParams(ECC_WorldStatic), Q))
    {
        Notice = TEXT("Target is behind scenery");
        return;
    }
    const int32 Damage = Characters.Active() ? Characters.Active()->Damage(CharacterCatalog) : 25;
    if (Target->Hit(Damage))
    {
        Player->SetActorRotation(FRotator(0,(Target->GetActorLocation()-Player->GetActorLocation()).Rotation().Yaw,0));
        Player->PlayBehavior(AC2Motion::Attack,AC2Motion::Unarmed);
        AttackCooldown = .5f;
        Effects->Burst(Target->GetActorLocation(), FLinearColor(1, .55f, .1f));
        Notice = Target->Health > 0 ? FString::Printf(TEXT("Hit: %d damage (offline)"), Damage)
                                    : TEXT("Target defeated (offline)");
    }
}
void AAC2GameMode::CastBurst(uint32 Id)
{
    if(Network) { Network->StarterSkill(2); return; }
    auto *PC = GetWorld()->GetFirstPlayerController();
    if (Effects && PC && PC->GetPawn())
        Notice = Effects->NativeBurst(Id, PC->GetPawn()->GetActorLocation() +
                                              PC->GetControlRotation().Vector() * 150)
                     ? TEXT("Native particle burst preview")
                     : TEXT("Unsupported particle record");
}
void AAC2PlayerController::AC2Cell(const FString &Text)
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
    {
        FString V = Text;
        V.RemoveFromStart(TEXT("0x"));
        bool Valid = V.Len() == 8;
        for (TCHAR C : V)
            Valid &= FChar::IsHexDigit(C);
        if (Valid)
            M->LoadCell(FCString::Strtoui64(*V, nullptr, 16));
    }
}
void AAC2PlayerController::AC2Weather(int32 I)
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->Weather(I);
}
void AAC2PlayerController::AC2Rain(float A)
{
    if (FMath::IsFinite(A))
        if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
            M->Rain(A);
}
void AAC2PlayerController::AC2Effect(const FString &Id)
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->CastBurst(FCString::Strtoui64(*Id, nullptr, 16));
}
void AAC2PlayerController::AC2Spawn(const FString &V)
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
    {
        bool Hex = V.Len() == 8;
        for (TCHAR C : V)
            Hex &= FChar::IsHexDigit(C);
        for (const auto &Creature : AC2CreaturePreviews)
            if (V.Equals(Creature.Name, ESearchCase::IgnoreCase))
            {
                M->SpawnMob(Creature.Visual);
                return;
            }
        if (V.IsEmpty())
            M->SpawnMob();
        else if (Hex)
            M->SpawnMob(FCString::Strtoui64(*V, nullptr, 16));
        else
            M->Notice = TEXT("AC2Spawn: Drudge, Shreth, Reedshark, Beetle, Banderling or 8-digit visual ID");
    }
}
void AAC2PlayerController::AC2Recall(){if(auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>())M->Recall();}
void AAC2PlayerController::Map()
{
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
        if (H->GameUI) H->GameUI->ToggleMap();
}
void AAC2PlayerController::Select()
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->TargetNext();
}
void AAC2PlayerController::Attack()
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->Attack();
}
void AAC2PlayerController::Burst()
{
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->CastBurst(M->PreviewEffect);
}
void AAC2PlayerController::CombatMode()
{
    if(auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>()) M->SetCombat(!M->InCombat());
}
bool AAC2GameMode::InCombat() const
{
    if(Network) return Network->Combat;
    auto* PC=GetWorld()->GetFirstPlayerController();
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    return P && !P->IsDead() && P->GetNativeMode()!=AC2Motion::Peace;
}
void AAC2GameMode::SetCombat(bool Enabled)
{
    if(!InWorld())return;
    if(Network){Network->SetCombat(Enabled);return;}
    auto* PC=GetWorld()->GetFirstPlayerController();
    if(auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr; P && !P->IsDead())
    {
        P->StopBehavior();
        P->SetNativeMode(Enabled?AC2Motion::Unarmed:AC2Motion::Peace);
        Notice=Enabled?TEXT("Combat stance"):TEXT("Peace stance");
    }
}
void AAC2PlayerController::SelectUnderCursor()
{
    auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>();
    if(!M || !M->InWorld())return;
    if(!bShowMouseCursor)
    {if(auto* H=Cast<AAC2HUD>(GetHUD());H && H->GameUI)H->GameUI->ClickWorldCenter(EKeys::LeftMouseButton);return;}
    FVector Origin, Direction;
    if(M->Network && DeprojectMousePositionToWorld(Origin,Direction))
        M->Network->SelectWorldRay(Origin,Direction);
}
void AAC2PlayerController::InspectUnderCursor()
{
    if(bShowMouseCursor)return; // Visible-pointer events belong to the Slate panel under it.
    if(auto* H=Cast<AAC2HUD>(GetHUD());H && H->GameUI)H->GameUI->ClickWorldCenter(EKeys::RightMouseButton);
}
void AAC2PlayerController::UseUnderCursor()
{
    if(bShowMouseCursor)return;
    if(auto* H=Cast<AAC2HUD>(GetHUD());H && H->GameUI)H->GameUI->ClickWorldCenter(EKeys::LeftMouseButton,true);
}
void AAC2PlayerController::UseTarget()
{
    if(auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>()) if(M->Network) M->Network->UseTarget();
}
void AAC2PlayerController::MissileSkill()
{
    if(auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>()) { if(M->Network) M->Network->StarterSkill(1); }
}
void AAC2PlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAC2PlayerController::Map);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AAC2PlayerController::Select);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAC2PlayerController::SelectUnderCursor);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_DoubleClick, this, &AAC2PlayerController::UseUnderCursor);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAC2PlayerController::InspectUnderCursor);
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AAC2PlayerController::Attack);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AAC2PlayerController::Burst);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AAC2PlayerController::MissileSkill);
    InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AAC2PlayerController::CombatMode);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AAC2PlayerController::UseTarget);
    InputComponent->BindKey(EKeys::Home, IE_Pressed, this, &AAC2PlayerController::AC2Recall);
    InputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AAC2PlayerController::Cursor);
    InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &AAC2PlayerController::Cursor);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAC2PlayerController::CloseUI);
    InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AAC2PlayerController::Inventory);
    InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AAC2PlayerController::CharacterStats);
    InputComponent->BindKey(EKeys::F4, IE_Pressed, this, &AAC2PlayerController::CharacterSelect);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AAC2PlayerController::Chat);
}

void AAC2PlayerController::SetCursor(bool Enabled)
{
    const bool Changed=bShowMouseCursor!=Enabled;
    if(Changed)RotationInput=FRotator::ZeroRotator;
    if(Changed && !Enabled)
    {float X=0,Y=0;if(GetMousePosition(X,Y))SavedPointer=FIntPoint(FMath::RoundToInt(X),FMath::RoundToInt(Y));}
    bShowMouseCursor = Enabled;
    bEnableClickEvents = Enabled;
    bEnableMouseOverEvents = Enabled;
    ResetIgnoreLookInput();
    SetIgnoreLookInput(Enabled);
    if (Enabled)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
        if(Changed && SavedPointer.IsSet())SetMouseLocation(SavedPointer->X,SavedPointer->Y);
    }
    else
        SetInputMode(FInputModeGameOnly());
}
void AAC2PlayerController::Cursor()
{
    auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>();
    auto* H=Cast<AAC2HUD>(GetHUD());
    if(!M || !M->InWorld() || (H && ((H->CharacterUI && H->CharacterUI->IsOpen()) ||
        (H->GameUI && H->GameUI->ChatActive()))))return;
    MouseLook=bShowMouseCursor;
    SetCursor(!MouseLook);
}
void AAC2PlayerController::CloseUI()
{
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
        if (H->GameUI && H->GameUI->ChatActive())
        {
            H->GameUI->EndChat();
            return;
        }
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
        if (H->GameUI && H->GameUI->ClosePanels())
            return;
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
        if (H->CharacterUI && H->CharacterUI->IsOpen())
        {
            H->CharacterUI->Close();
            return;
        }
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->bMap = false;
    if(auto* H=Cast<AAC2HUD>(GetHUD()))if(H->GameUI)H->GameUI->ToggleOptions();
}
void AAC2PlayerController::Chat()
{
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
        if (H->GameUI)
            H->GameUI->BeginChat();
}
void AAC2PlayerController::Inventory()
{
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
    {
        H->InitializeCharacterUI();
        if (H->GameUI)
            H->GameUI->ToggleInventory();
    }
}
void AAC2PlayerController::CharacterStats()
{
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
    {
        H->InitializeCharacterUI();
        if (H->GameUI)
            H->GameUI->ToggleStats();
    }
}
void AAC2PlayerController::CharacterSelect()
{
    if (auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>()) if(M->Network) {M->Network->Logout();return;}
    if(auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>())M->LeaveWorld();
    if (auto *H = Cast<AAC2HUD>(GetHUD()))
    {
        H->InitializeCharacterUI();
        if (H->CharacterUI)
            H->CharacterUI->Open(SAC2CharacterUI::EScreen::Select);
    }
}
void AAC2GameMode::UIAction(FName Action)
{
    if(Action==TEXT("Recall")){Recall();return;}
    if (Action == TEXT("Strike"))
        Attack();
    else if (Action == TEXT("Target"))
        TargetNext();
    else if (Action == TEXT("Cast"))
        CastBurst(PreviewEffect);
    else if (Action == TEXT("Inventory") || Action == TEXT("Stats") || Action == TEXT("Characters"))
    {
        if (auto *PC = Cast<AAC2PlayerController>(GetWorld()->GetFirstPlayerController()))
        {
            if (Action == TEXT("Inventory"))
                PC->Inventory();
            else if (Action == TEXT("Stats"))
                PC->CharacterStats();
            else
                PC->CharacterSelect();
        }
    }
    else if (Action == TEXT("Map"))
    {
        if (auto *PC = Cast<AAC2PlayerController>(GetWorld()->GetFirstPlayerController())) PC->Map();
    }
}
