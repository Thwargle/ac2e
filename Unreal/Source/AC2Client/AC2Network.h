#pragma once
#include "AC2Assets.h"
#include "AC2CharacterData.h"
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
    void SelectTarget(const FString &Id);
    void Attack();
    void UseTarget();
    void UseItem(int32 Index);
    void MoveItem(int32 Index, int32 Slot);
    uint32 Gold = 0;
    int32 AllowedCharacters = 8;
    bool Dead = false;
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
    struct FObject
    {
        uint32 Visual = 0, Cell = 0, Entity = 0;
        int32 Instance = 0;
        float EntityScale = 1;
        FString Name;
        FVector Scale = FVector::OneVector, Goal = FVector::ZeroVector;
        float Yaw = 0;
        uint32 AnimMode=AC2Motion::Peace;
        FAC2Appearance Aspects;
        TWeakObjectPtr<AActor> Actor;
        TUniquePtr<FAC2AssetLibrary> Model;
        bool Placed = false;
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
    bool Smoke = false, SmokeEntered = false, SmokeChat = false;
    int32 ReceivedChats = 0;
    double SmokeFinish = 0;
    bool SmokeAttacked = false;
    bool SmokeRadar = false;
    bool SmokeSelection=false;
    int32 SelectionSmokeStep=0;
    int32 PlayedBehaviors=0,HitEvents=0,DeathBehaviors=0,PlayerAttacks=0,MobAttacks=0,FailedAttacks=0;
    TMap<FString, int32> BagSlots;
    TMap<FString, uint32> WornSlots;
    void Handle(const TSharedPtr<FJsonObject> &Event);
    void ClearWorld();
    void SetStatus(const FString &State, const FString &Text);
    void ApplyVisual(FObject &Object, const TSharedPtr<FJsonObject> &Visual, bool Player);
    void AddCharacter(const TSharedPtr<FJsonObject> &Character);
    FString StringInfo(const TSharedPtr<FJsonObject> &Info) const;
};
