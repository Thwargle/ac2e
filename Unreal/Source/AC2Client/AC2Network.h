#pragma once
#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2GameTime.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"

class AAC2GameMode;
struct FAC2ConnectionSettings
{
    FString Host=TEXT("127.0.0.1"),Account=TEXT("local"),Ticket;
    int32 Port=9000;
};
// Native AC2 transport is isolated in an AGPL bridge process. No Unreal NetDriver/RPC wire format.
class FAC2Network
{
  public:
    explicit FAC2Network(AAC2GameMode *Owner);
    ~FAC2Network();
    bool Start(const FAC2ConnectionSettings* Settings=nullptr);
    void Tick(float Delta);
    TOptional<double> ServerTime() const;
    void Send(TSharedRef<FJsonObject> Message);
    void Enter(const FString &Id);
    void DeleteCharacter(const FString &Id);
    void Create(const FString &Name, const FAC2AvatarChoice &Avatar, float Height, float Build,
                const TMap<uint32, int32> &Choices);
    void SceneComplete();
    void Logout();
    void Recall();
    void Chat(FString Text);
    void TargetNext();
    struct FRadarContact
    {
        FString Id, Name;
        TWeakObjectPtr<AActor> Actor;
    };
    TArray<FRadarContact> RadarContacts() const;
    struct FDamageText { FString Target, Attacker; int64 Damage=0; double Born=0; };
    TArray<FDamageText> DamageTexts;
    int32 MusicGroove() const;
    void SelectTarget(const FString &Id);
    void ClearTarget();
    FString ObjectName(const FString& Id) const;
    bool ObjectScreenBounds(const FString& Id,FBox2D& Bounds) const;
    void Examine(const FString& Id);
    // Called only while the examination panel is visible; retains the last
    // snapshot until the server supplies its replacement.
    void RefreshExamination(const FString& Id);
    struct FExaminationRow { FString Text; uint32 Appearance=0, Image=0; };
    FString ExaminationId, ExaminationError;
    TArray<FExaminationRow> ExaminationRows;
    bool ExaminationPending=false;
    bool SelectWorldRay(const FVector &Origin, const FVector &Direction);
    FString WorldTargetAtRay(const FVector &Origin, const FVector &Direction) const;
    void Attack();
    void SetCombat(bool Enabled);
    void CastSkill(uint32 Skill);
    void ChangeSkill(uint32 Skill,FString Action);
    uint64 AvailableXp=0;
    void StarterSkill(int32 Category);
    void UseTarget();
    void UseItem(int32 Index, uint32 EquipSlot=0);
    void MoveItem(int32 Index, int32 Slot);
    void DropItem(int32 Index);
    struct FShopOffer { FString Id,Name; uint32 Entity=0,Icon=0,Currency=0,Stock=0; float Price=0; };
    FString ShopVendor;
    TArray<FShopOffer> ShopOffers;
    void BuyOffer(int32 Index);
    void SellItem(int32 Index);
    void CloseShop();
    struct FSkill {uint32 Id=0;bool Trained=false;FString Xp;};
    struct FQuest {uint32 Id=0,Icon=0,Status=0,Phase=0;FString Name,Description,Journal;};
    TArray<FSkill> Skills;
    TArray<FQuest> Quests;
    int32 SkillCredits=0;
    uint32 UntrainingSkill=0;
    FString LootContainer;
    TArray<FString> LootItems;
    void LootAll();
    void TransmuteLoot();
    void TakeLoot(int32 Index);
    void CloseLoot();
    uint32 ItemIcon(const FString& Id) const;
    TArray<FAC2IconLayer> ItemIconLayers(const FString& Id,FAC2AssetLibrary& Assets) const;
    bool ArrivalCollisionReady() const;
    uint64 ArrivalRevision() const { return SceneRevision; }
    uint32 Gold = 0;
    int32 AllowedCharacters = 8;
    bool Dead = false, Combat = false;
    bool MapTravelEnabled = false;
    bool IsWorld() const
    {
        return Phase == TEXT("world");
    }
    bool IsSelection() const
    {
        return Phase == TEXT("selection");
    }
    bool HasPlayerPose = false, NeedsPlacement = false;
    uint32 Cell = 0;
    FVector Feet = FVector::ZeroVector;
    float Heading = 0, MaxHealth = 0, MaxVigor = 0;
    FString Phase = TEXT("connecting"), Message, PlayerId, TargetId;
    uint32 Visual(const FString &Id) const;
    FAC2Appearance Appearance(const FString &Id) const;
    void SetCell(uint32 NewCell)
    {
        Cell = NewCell;
    }
    int32 ActorCount() const
    {
        return Objects.Num();
    }

  private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2DamageNetworkTest;
    friend class FAC2SwimmingRenderTest;
    friend class FAC2TargetPresentationTest;
    friend class FAC2LiveExaminationTest;
#endif
    TUniquePtr<class FAC2MapData> PortalMap;
    bool PortalMapAttempted = false;
    bool SmokeWorldProps(FString& Error) const;
    void FoliageSmokeTick();
    int32 FoliageSmokeStep=0;
    int8 PlayerGroove=0;
    double FoliageSmokeNext=0;
    struct FObject
    {
        uint32 Visual = 0, Cell = 0, Entity = 0;
        int32 Instance = 0;
        int8 Groove=0;
        float EntityScale = 1;
        float Health=-1,MaxHealth=0,DamageFX=-1;
        FString Name, Parent;
        uint32 Holding=0;
        FVector Scale = FVector::OneVector, Goal = FVector::ZeroVector;
        float Yaw = 0;
        FQuat Rotation = FQuat::Identity;
        uint32 AnimMode=AC2Motion::Peace;
        uint32 Element=AC2Motion::Ground;
        float BodyHeight=180;
        FAC2Appearance Aspects;
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<USceneComponent> Body;
        // Body-local bounds captured when the visual is built. Animation must
        // not move the targeting frame or the name above a living creature.
        FBox PresentationBounds = FBox(ForceInit);
        bool Content=false, GroundedContent=false, Solid=false, GroundItem=false;
        bool Door=false,DoorOpen=false,OpenableStateKnown=false,OpenableOpen=false;
        float FootZ=0;
        double NextGroundCheck=0;
        TUniquePtr<FAC2AssetLibrary> Model;
        bool Placed = false;
        bool Missile=false,MissileFinished=false,MissilePointKnown=false;
        FString MissileTarget;
        FVector MissileVelocity=FVector::ZeroVector,MissileOffset=FVector::ZeroVector,MissilePoint=FVector::ZeroVector;
        float MissileAge=0;
    };
    AAC2GameMode *Mode;
    FProcHandle Process;
    void *Read = nullptr;
    void *Write = nullptr;
    TArray<uint8> Incoming;
    TMap<FString, TUniquePtr<FObject>> Objects;
    TMap<FString, TSharedPtr<FJsonObject>> CharacterVisuals;
    TUniquePtr<FAC2DatArchive> Locale;
    double Time = 0, LastMovement = 0, Started = 0;
    double LastSceneChange = 0;
    uint64 SceneRevision = 0;
    FAC2ServerClock Clock;
    double LastShopRefresh=0;
    double ExaminationStarted=-1;
    bool ExaminationQueued=false;
    void RequestExamination();
    bool Smoke = false, SmokeEntered = false, SmokeChat = false;
    int32 ReceivedChats = 0;
    double SmokeFinish = 0;
    bool SmokeAttacked = false;
    int32 LootSmokeStep=0,LootSmokeCount=0;
    double LootSmokeNext=0;
    bool SmokeEquipped=false, SmokeRepeated=false, SmokeHeld=false;
    int32 MagicAttacks=0;
    bool SmokeRadar = false;
    bool SmokeSelection=false;
    int32 SelectionSmokeStep=0;
    int32 CombatSmokeStep=0;
    int32 MouseSmokeStep=0;
    int32 InventorySmokeStep=0;
    int32 ShopSmokeStep=0;
    double ShopSmokeNext=0;
    FBox ShopClosedDoorBounds=FBox(ForceInit);
    FBox ShopClosedChestBounds=FBox(ForceInit);
    double InventorySmokeNext=0;
    int32 MapTravelSmokeStep=0;
    double MapTravelSmokeNext=0;
    int32 PlayedBehaviors=0,HitEvents=0,DeathBehaviors=0,PlayerAttacks=0,MobAttacks=0,FailedAttacks=0;
    int32 PlayedEffects=0;
    int32 SmokeParticles=0,SmokeCastParticles=0;
    int32 SmokeMissiles=0,SmokeMissileUpdates=0,SmokeMissileParticles=0;
    int32 SmokeArmorStage=0;uint32 SmokeClothedHash=0;bool SmokeArmor=false;
    TMap<FString, int32> BagSlots;
    TMap<FString, uint32> WornSlots;
    void Handle(const TSharedPtr<FJsonObject> &Event);
    void ClearWorld();
    bool Targetable(const FString &Id, const FObject &Object) const;
    FBox ObjectPresentationBounds(const FString& Id) const;
    void SetStatus(const FString &State, const FString &Text);
    void ApplyVisual(FObject &Object, const TSharedPtr<FJsonObject> &Visual, bool Player);
    void AddCharacter(const TSharedPtr<FJsonObject> &Character);
    FString StringInfo(const TSharedPtr<FJsonObject> &Info) const;
};
