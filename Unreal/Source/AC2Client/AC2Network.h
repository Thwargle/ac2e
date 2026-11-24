#pragma once
#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2GameTime.h"
#include "AC2RemoteMotion.h"
#include "AC2SkillCooldown.h"
#include "AC2ShortcutData.h"
#include "AC2GameOptions.h"
#include "AC2RadarData.h"
#include "AC2Look.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"

class AAC2GameMode;
struct FAC2FoliageContact;
struct FAC2ConnectionSettings
{
    FString Host=TEXT("127.0.0.1"),Account=TEXT("local"),Ticket,DirectPassword,Protocol,DatDirectory;
    FString ServerProfileId,ProfileFile;
    bool DirectLogin=false;
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
    TOptional<FAC2ConnectionSettings> DataReload;
    TOptional<double> ServerTime() const;
    void Send(TSharedRef<FJsonObject> Message);
    FString ShortcutScope;
    FGuid ShortcutSession=FGuid::NewGuid();
    uint64 ShortcutRevision=0;
    TArray<FAC2Shortcut> ServerShortcuts;
    bool SaveShortcuts(const FAC2Shortcut* Bindings);
    FAC2GameOptions ServerGameOptions;
    uint64 GameOptionsRevision=0;
    bool SaveGameOptions(const FAC2GameOptions& Options);
    void Enter(const FString &Id);
    void DeleteCharacter(const FString &Id);
    void Create(const FString &Name, const FAC2AvatarChoice &Avatar, float Height, float Build,
                const TMap<uint32, int32> &Choices);
    void SceneComplete();
    void FinishArrivalEffect();
    bool PlayingStoryScene() const { return ActiveStoryScene != 0; }
    void Logout();
    void Recall();
    void Chat(FString Text);
    void TargetNext();
    bool AutoTarget=true;
    struct FRadarContact
    {
        FString Id, Name;
        TWeakObjectPtr<AActor> Actor;
        uint32 Entity=0;
        TOptional<uint32> Shape;
        float Health=-1;
        TOptional<bool> Selectable;
        TOptional<uint32> Color;
    };
    TArray<FRadarContact> RadarContacts() const;
    void AppendFoliageContacts(TArray<FAC2FoliageContact>& Contacts,const FVector& Focus) const;
    struct FDamageText { FString Target, Attacker; int64 Damage=0; double Born=0; };
    TArray<FDamageText> DamageTexts;
    int32 MusicGroove() const;
    void SelectTarget(const FString &Id);
    void SelectFellow(const FString& Id);
    void ClearTarget();
    FVector PlayerLook(float Delta);
    void ReportPlayerLook(const FVector& Pose);
    FString PlayerLookTarget,LookSelection;
    AC2Look::FInterest LookInterest;
    AC2Look::FReporter LookReporter;
    AC2Look::FPose PlayerLookPose;
    FString ObjectName(const FString& Id) const;
    bool ObjectScreenBounds(const FString& Id,FBox2D& Bounds) const;
    void Examine(const FString& Id);
    void Fellowship(const FString& Action,const FString& Value=FString(),int32 Looting=0,bool SharedXp=false);
    void Allegiance(const FString& Action,const FString& Id=TEXT("0"));
    struct FFriend {
        FString Name,Location;
        uint32 Mode=0;
    };
    TArray<FFriend> Friends;
    bool FriendsEnabled=false,FriendsReceived=false;
    uint32 FriendMode=0;
    FString FriendError;
    FString FriendTarget();
    void FriendAction(const FString& Action,const FString& Name=FString(),uint32 Availability=0);
    struct FLiege {FString Id,Name,Title,Role;int32 Level=0,Rank=0;bool Online=false;};
    TArray<FLiege> Lieges;
    FString AllegianceName,AllegianceMotd,AllegiancePatron,AllegianceMonarch,AllegianceApplicant,AllegianceInvitation;
    int32 AllegianceSize=0;
    bool AllegianceReceived=false;
    double AllegianceInvitationUntil=0;
    int32 FellowshipLooting=0;
    bool FellowshipSharedXp=false;
    struct FFellow {FString Id,Name;int32 Level=0,Health=0,MaxHealth=0,Vigor=0,MaxVigor=0;uint32 Cell=0;};
    TArray<FFellow> Fellows;
    FString FellowshipName,FellowshipLeader,FellowshipInviter,FellowshipInvitation;
    double FellowshipInvitationUntil=0;
    // Called only while the examination panel is visible; retains the last
    // snapshot until the server supplies its replacement.
    void RefreshExamination(const FString& Id);
    // A native examination is an inline stream, not preformatted text rows.
    // Kind zero is a locally supplied complete line; wire nodes retain breaks,
    // tab counts and independent appearance templates for labels and values.
    struct FExaminationRow {
        FString Text; uint32 Appearance=0, Image=0, Kind=0, Tabs=0;
        TArray<FAC2IconLayer> Layers;
    };
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
    TOptional<uint32> QuestPoints;
    uint64 LevelXpFloor=0,LevelXpSpan=0;
    TOptional<uint64> ServerLevelXpFloor,ServerLevelXpCeiling;
    void StarterSkill(int32 Category);
    void UseTarget();
    void UseItem(int32 Index, uint32 EquipSlot=0, bool PickWorldTarget=false);
    bool AwaitingItemTarget() const { return !PendingTargetItem.IsEmpty(); }
    void CancelItemTarget() { PendingTargetItem.Reset(); }
    void UseItemOnWorldTarget(const FString& Target);
    // A complete native inventory snapshot may arrive before its item creates.
    bool InventoryKnown=false;
    bool OwnsInventoryItem(const FString& Id) const {return InventoryOwnership.Contains(Id);}
    TOptional<uint32> HeldImplement(uint32 Slot) const;
    void MoveItem(int32 Index, int32 Slot);
    void DropItem(int32 Index);
    void GiveItem(int32 Index,const FString& Recipient);
    struct FShopOffer { FString Id,Name; uint32 Entity=0,Icon=0,Currency=0,Stock=0; float Price=0; };
    FString ShopVendor;
    TArray<FShopOffer> ShopOffers;
    void BuyOffer(int32 Index);
    void SellItem(int32 Index);
    void CloseShop();
    // Recipe entitlement comes from WM_Craft; the client never grants recipes.
    TArray<uint32> CraftRecipes;
    uint32 OpenCraftRecipe=0;
    struct FCraftInput {int32 Ordinal=0,Quantity=0; FString Name; TArray<uint32> Choices;};
    TMap<uint32,TSharedPtr<FJsonObject>> CraftCatalog,CraftSkills,CraftProgress;
    TArray<float> CraftSuccess;
    uint32 CraftSpinner=0;
    FString CraftIngredients[16];
    bool CraftAccepted[16]={};
    TArray<FCraftInput> CraftInputs() const;
    void CraftLevel(int32 Delta);
    void CycleCraftIngredient(int32 Ordinal);
    void SetCraftIngredient(int32 Ordinal,const FString& Item);
    void RaiseCraft(uint32 Skill);
    bool CraftPending=false;
    FString CraftNotice;
    void OpenCraft(uint32 Recipe);
    void PopulateCraft();
    void ExecuteCraft();
    void CloseCraft();
    bool CanCraft() const;
    struct FSkill {uint32 Id=0;bool Trained=false;FString Xp;double ResetStarted=-1;};
    FAC2SkillCooldown SharedSkillReset;
    FAC2SkillCooldown SkillCooldown(uint32 Skill,double Duration,double ServerNow) const;
    // gmShortcutUI's pending table is keyed by attack counter, not skill ID:
    // a rejected repeat must not clear an earlier request for the same skill.
    TMap<uint32,uint32> PendingSkills;
    TSet<uint32> ToggledSkills;
    bool SkillPending(uint32 Skill) const;
    bool SkillActive(uint32 Skill) const;
    bool SkillBusy(uint32 Skill) const;
    TOptional<uint32> ActiveSkillRequest;
    uint32 ActiveSkillBehavior=0;
    uint32 ActiveSkillHoldCycles=1;
    uint64 ActiveSkillInstance=0;
    void StartSkillStory(uint32 Request,uint32 Behavior,uint64 Instance);
    void FinishSkillStory();
    void FinishSkillRequest(uint32 Request);
    void UpdateSkillStory(uint64 PlayingInstance);
    struct FQuest {
        uint32 Id=0,Icon=0,Status=0,Phase=0;
        int32 Level=-999;
        FString Name,Description,Journal;
        // Replaced with each native quest update; avoid reparsing the DAT
        // string table every frame while this quest's details are expanded.
        FString FormattedDetail;
        double Bestowal=-1,Received=0,Remaining=0,Retry=0;
        bool Expires=false,MaxedOut=false,Changed=false;
        double Seconds(double Now) const {return FMath::Max(0.,(Status==0x10000000?Remaining:Retry)-FMath::Max(0.,Now-Received));}
        uint32 DisplayStatus(double Now) const {return Status==0x10000000 && Expires && Seconds(Now)<=0?0x20000000:Status;}
    };
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
    bool IsGroundItem(const FString& Id) const;
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
    bool PortalSpaceRequested = false, PortalMovementLocked = false;
    uint32 Cell = 0;
    FVector Feet = FVector::ZeroVector;
    float Heading = 0, MaxHealth = 0, MaxVigor = 0;
    struct FStatusEffect {
        uint32 Id=0, Icon=0, Flags=0, Order=0;
        FString Name, Description;
        double Cast=0, Duration=0;
    };
    TMap<uint32,FStatusEffect> StatusEffects;
    float Vitae=100, CurrentFocus=0, MaxFocus=0, PKDamage=0, PKVigorLoss=0;
    TOptional<uint64> VitaeRecoveryRemaining;
    uint32 PKStatus=0, ServerPKStatus=0;
    bool IsHero=false, HaveLinkSample=false;
    double RetransmitPercent=0, SentBytesPerSecond=0, ReceivedBytesPerSecond=0, LinkSilence=0;
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
    TOptional<FAC2ConnectionSettings> HandshakeSettings;
    FString DataCacheRoot;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2DamageNetworkTest;
    friend class FAC2HeldRunningTest;
    friend class FAC2SwimmingRenderTest;
    friend class FAC2TargetPresentationTest;
    friend class FAC2AnimationContinuityTest;
    friend class FAC2NetworkPresentationTest;
    friend class FAC2StoryMissileRenderTest;
    friend class FAC2PrivatePlayerPresentationTest;
    friend class FAC2LiveExaminationTest;
    friend class FAC2RadarUpdatesTest;
    friend class FAC2ItemShortcutsTest;
    friend class FAC2TextShortcutsTest;
    friend class FAC2AliasesTest;
    friend class FAC2ShortcutSyncTest;
    friend class FAC2GameOptionsSyncTest;
    friend class FAC2AutoTargetTest;
    friend class FAC2SkillEquipmentTest;
    friend class FAC2RecipeShortcutsTest;
    friend class FAC2ConcurrentWindowsTest;
    friend class FAC2ShortcutKeysTest;
    friend class FAC2PlayerPositionTest;
    friend class FAC2NativeEquipmentTest;
    friend class FAC2InventoryOrderTest;
    friend class FAC2InventoryTransferTest;
    friend class FAC2PortalArrivalTest;
    friend class FAC2MenhirShardNetworkCameraTest;
    friend class FAC2InteractionAudioTest;
    friend class FAC2GeneralChatTest;
    friend class FAC2FriendsTest;
    friend class FAC2RememberCharacterTest;
    friend class FAC2LookLifecycleTest;
#endif
    TUniquePtr<class FAC2MapData> PortalMap;
    bool PortalMapAttempted = false;
    float ServerArrivalEffectStrength = 0;
    bool RevealingArrivalEffect = false;
    uint32 ActiveStoryScene = 0;
    FString PendingTargetItem, PendingShardUse;
    double ShardUseTime = 0;
    void ActivateShard(const FString& Id, const FString& Recipient);
    void PlayStoryScenes(const TSharedPtr<FJsonObject>& Message);
    void CompleteStoryScene(uint32 Id);
    bool SmokeWorldProps(FString& Error) const;
    void FoliageSmokeTick();
    int32 FoliageSmokeStep=0;
    int8 PlayerGroove=0;
    double FoliageSmokeNext=0;
    struct FObject
    {
        uint32 Visual = 0, Cell = 0, Entity = 0;
        int32 Instance = 0, Quantity = 1;
        uint16 PositionStamp=0, TeleportStamp=0, ForceStamp=0;
        int8 Groove=0;
        float EntityScale = 1;
        float Health=-1,MaxHealth=0,DamageFX=-1;
        TOptional<uint32> RadarShape;
        TOptional<bool> RadarSelectable;
        TOptional<uint32> RadarColor;
        TOptional<int32> UseTargets;
        FString Name, Parent, Summoner;
        uint32 Holding=0;
        FVector Scale = FVector::OneVector, Goal = FVector::ZeroVector;
        FAC2RemoteMotion PresentationMotion;
        float Yaw = 0;
        FQuat Rotation = FQuat::Identity;
        uint32 AnimMode=AC2Motion::Peace;
        // Death survives a replacement visual or duplicate create for this
        // incarnation. Otherwise the rebuilt corpse acquires a living idle.
        bool Dead=false;
        uint32 DeathMode=AC2Motion::Peace;
        float DeathRate=1;
        double DeathStarted=0;
        uint32 Element=AC2Motion::Ground;
        float BodyHeight=180;
        FAC2Appearance Aspects;
        TSharedPtr<FJsonObject> CachedVisual;
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<USceneComponent> Body;
        // Body-local bounds captured when the visual is built. Animation must
        // not move the targeting frame or the name above a living creature.
        FBox PresentationBounds = FBox(ForceInit);
        bool Content=false, GroundedBody=false, Solid=false, GroundItem=false;
        bool FollowsGround() const
        {
            // Icefire can send the summoner quality after the object itself.
            return GroundedBody && (Content || (!Summoner.IsEmpty() && Summoner!=TEXT("0")));
        }
        bool Door=false,DoorOpen=false,OpenableStateKnown=false,OpenableOpen=false;
        float FootZ=0;
        double NextGroundCheck=0;
        double NextAnimationFloorCheck=0;
        bool AnimationHasFloor=false;
        FVector AnimationFloor=FVector::UpVector;
        FString LookTarget;
        FVector LookDirection=FVector::ZeroVector;
        AC2Look::FPose LookPose;
        TUniquePtr<FAC2AssetLibrary> Model;
        bool Placed = false;
        bool Missile=false,MissileFinished=false,MissilePointKnown=false,MissileOffsetRelative=false,LocalMissile=false;
        FString MissileTarget;
        FVector MissileVelocity=FVector::ZeroVector,MissileOffset=FVector::ZeroVector,MissilePoint=FVector::ZeroVector;
        float MissileAge=0;
    };
    AAC2GameMode *Mode;
    FString CharacterSelectionSection, LastCharacterId;
    void SetAccountScope(const FString& Host, int32 Port, const FString& Account, bool Direct);
    void SaveLastCharacter(const FString& Id);
    FProcHandle Process;
    void *Read = nullptr;
    void *Write = nullptr;
    TArray<uint8> Incoming;
    TMap<FString, TUniquePtr<FObject>> Objects;
    TMap<FString, TPair<double,TSharedPtr<FJsonObject>>> PendingMissiles;
    struct FStoryMissile {
        FString Owner,Attacker,Target,Weapon;
        int32 Instance=0;
        uint32 Skill=0,Hook=0,Behavior=0;
        uint64 Animation=0;
        double Due=0;
    };
    TArray<FStoryMissile> StoryMissiles;
    uint64 NextLocalMissile=0;
    void ReceiveStoryMissiles(const TSharedPtr<FJsonObject>& Event);
    void LaunchStoryMissiles();
    TArray<FString> RecentActions;
    FString LastAttackerId;
    TSet<FString> PendingPickupSounds;
    TSet<FString> AcquisitionSounds;
    double LastPickupAnimation=-10,PickupFallbackTime=-10;
    void ConfirmPickup(const FString& Id,bool Requested);
    TUniquePtr<class FAC2SkillData> CombatSkillData;
    TUniquePtr<FAC2RadarData> SelectionData;
    AC2Radar::FLook SelectionLook(uint32 Entity);
    FString ReplacementTarget(const FString& Excluded);
    void SelectionUnavailable(const FString& Id,bool Deleted);
    void ReceiveStoryTarget(const TSharedPtr<FJsonObject>& Event);
    TMap<FString, TSharedPtr<FJsonObject>> CharacterVisuals;
    TUniquePtr<FAC2DatArchive> Locale;
    double Time = 0, LastMovement = 0, Started = 0;
    double LastSceneChange = 0;
    uint64 SceneRevision = 0;
    FAC2ServerClock Clock;
    double LastShopRefresh=0;
    double ExaminationStarted=-1;
    bool ExaminationQueued=false, ExaminationDirty=false;
    void InvalidateExamination(const FString& Id);
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
    TSet<FString> InventoryOwnership;
    TMap<FString, uint32> WornSlots;
    void RemoveBagSlot(const FString& Id);
    void InsertBagSlot(const FString& Id,int32 Slot);
    void SyncInventoryItems();
    void Handle(const TSharedPtr<FJsonObject> &Event);
    void ReceiveGameOptions(const TSharedPtr<FJsonObject>& Event);
    void ReceiveShortcuts(const TSharedPtr<FJsonObject>& Event);
    void ClearWorld();
    bool Targetable(const FString &Id, const FObject &Object) const;
    FBox ObjectPresentationBounds(const FString& Id) const;
    void SetStatus(const FString &State, const FString &Text);
    void ApplyVisual(FObject &Object, const TSharedPtr<FJsonObject> &Visual, bool Player);
    void AddCharacter(const TSharedPtr<FJsonObject> &Character);
    FString StringInfo(const TSharedPtr<FJsonObject> &Info) const;
};
