#pragma once
#include "AC2Effects.h"
#include "AC2CharacterData.h"
#include "AC2FarTerrain.h"
#include "AC2Mob.h"
#include "AC2Sky.h"
#include "AC2WorldStream.h"
#include "AC2Network.h"
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "AC2GameMode.generated.h"

UCLASS(Config = Engine)
class AAC2GameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AAC2GameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void RestartPlayer(AController *NewPlayer) override;
    bool LoadBlock(uint32 Block);
    void ToggleFlight();
    void SetHour(float Hour);
    void LoadCell(uint32 Id);
    void Weather(int32 Index);
    void Rain(float Amount)
    {
        if (Sky)
            Sky->RainOverride = FMath::Clamp(Amount, -1.f, 1.f);
    }
    void SpawnMob(uint32 Visual = 0);
    FString NextCreatureName() const;
    void TargetNext();
    void Attack();
    bool InCombat() const;
    void SetCombat(bool Enabled);
    void CastBurst(uint32 Id = 0x39000003);
    bool bHelp = false, bMap = false, bIndoors = false, bWorldPanel = false, bFXPanel = false;
    uint32 PreviewEffect = 0x39000003;
    FString WeatherStatus() const;
    float MapHour() const { return Sky ? Sky->Hour() : 12.f; }
    void UIAction(FName Action);
    bool PlaceCharacter();
    bool InWorld() const { return !FrontEnd; }
    bool CellVisible(uint32 Cell) const { return Stream && Stream->CellVisible(Cell); }
    bool OpenWorld();
    void LeaveWorld();
    void ShowLogin(const FString& Message=FString());
    void SignOut();
    void Connect(const FAC2ConnectionSettings& Settings);
    void Offline();
    bool EnterOfflineCharacter();
    void Recall();
    TUniquePtr<FAC2Network> Network;
    FAC2CharacterCatalog CharacterCatalog;
    FAC2CharacterStore Characters;
    FString CharacterSavePath, CharacterError;
    bool CharacterDataReady = false;
    float StrikeCooldown() const
    {
        return FMath::Clamp(AttackCooldown / .5f, 0.f, 1.f);
    }
    FString Notice = TEXT("Offline sandbox / local NPCs");
    UPROPERTY() TObjectPtr<AAC2Mob> Target;
    const TArray<TWeakObjectPtr<AAC2Mob>> &MobList() const
    {
        return Mobs;
    }
    FString Status;
    UPROPERTY(Config) FString DatDirectory;
    UPROPERTY(Config) FString StartBlock;
    UPROPERTY(Config) int32 ViewRadius = 2;

  private:
    TUniquePtr<FAC2WorldStream> Stream;
    TUniquePtr<FAC2SkyRenderer> Sky;
    TUniquePtr<FAC2FarTerrain> Far;
    TUniquePtr<FAC2Effects> Effects;
    TArray<TWeakObjectPtr<AAC2Mob>> Mobs;
    uint32 PendingCell = 0, InteriorBlock = 0;
    bool bSpawned = false;
    int32 CreatureIndex = 0;
    float EffectSync = 0;
    float AttackCooldown = 0, RoofTime = 0, InteriorGrace = 0;
    FVector Destination = FVector::ZeroVector, LastSafe = FVector::ZeroVector,
            StreamFocus = FVector::ZeroVector;
    bool bReady = false, bTeleport = false, bWaiting = false, bSmoke = false, bSmokeJumped = false;
    bool FrontEnd=true, LoginStarted=false, OfflineEntering=false;
    int32 SmokeStage = 0;
    float SmokeTime = 0, StageTime = 0;
    FVector SmokeStart = FVector::ZeroVector;
    uint32 InitialBlock = 0;
    bool CharacterUIStarted = false, bUISmoke = false;
    bool NetworkTravel = false;
    int32 UIStage = 0;
    float UITime = 0;
    void UISmokeTick(float DeltaTime);
    bool bControllerSmoke=false;
    int32 ControllerStage=0;
    float ControllerTime=0,ControllerNext=2;
    void ControllerSmokeTick(float DeltaTime);
    bool bMapSmoke = false;
    int32 MapStage = 0;
    float MapTime = 0, MapNext = 3;
    void MapSmokeTick(float DeltaTime);
    void PositionViewer(APlayerController *PC);
    void CompleteTravel(const FVector &Position);
    void SmokeTick(float DeltaTime);
    void FinishSmoke(bool Success, const FString &Message);
};
UCLASS()
class AAC2PlayerController : public APlayerController
{
    GENERATED_BODY()
  public:
    UFUNCTION(Exec) void AC2Block(const FString &HexBlock);
    UFUNCTION(Exec) void AC2Fly();
    UFUNCTION(Exec) void AC2Time(float Hour);
    UFUNCTION(Exec) void AC2Cell(const FString &HexCell);
    UFUNCTION(Exec) void AC2Weather(int32 Index);
    UFUNCTION(Exec) void AC2Rain(float Amount);
    UFUNCTION(Exec) void AC2Effect(const FString &Id);
    UFUNCTION(Exec) void AC2Spawn(const FString &Visual);
    UFUNCTION(Exec) void AC2Recall();
    virtual void SetupInputComponent() override;
    void Help();
    void Map();
    void Select();
    void SelectUnderCursor();
    void Attack();
    void Burst();
    void CombatMode();
    void UseTarget();
    void MissileSkill();
    void Weather();
    void Spawn();
    void Cursor();
    void CloseUI();
    void Chat();
    void WorldPanel();
    void FXPanel();
    void Inventory();
    void CharacterStats();
    void CharacterSelect();
    void SetCursor(bool Enabled);
};
UCLASS()
class AAC2HUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
    virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
    virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;
    void InitializeCharacterUI();
    TSharedPtr<class SAC2CharacterUI> CharacterUI;
    TSharedPtr<class SAC2GameUI> GameUI;
    bool HasNativeArt() const
    {
        return ArtReady;
    }

  private:
    TUniquePtr<FAC2AssetLibrary> UIAssets;
    TSharedPtr<class FAC2Fonts> Fonts;
    TMap<uint32, class UTexture2D *> Art;
    FName Hover;
    bool ArtReady = false;
    float UIScale = 1;
    void Text(const FString &Value, float X, float Y, FLinearColor Color, float Size = 1);
    void Rect(FLinearColor Color, float X, float Y, float W, float H);
    void Picture(uint32 Id, float X, float Y, float W, float H, FLinearColor Tint = FLinearColor::White);
    void Panel(const FString &Title, float X, float Y, float W, float H);
    void Button(FName Action, const FString &Label, float X, float Y, float W, float H, uint32 Icon = 0);
};
