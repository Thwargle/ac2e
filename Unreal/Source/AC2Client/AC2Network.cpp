#include "AC2Network.h"
#include "AC2NativeMaterial.h"
#include "AC2Look.h"
#include "AC2Audio.h"
#include "AC2LoginProfiles.h"
#include "AC2HeldItem.h"
#include "AC2Performance.h"
#include "AC2Locomotion.h"
#include "AC2Projectile.h"
#include "AC2SkillData.h"
#include "AC2GameMode.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2PortalSpace.h"
#include "AC2Explorer.h"
#include "AC2StoryScene.h"
#include "AC2WorldGrid.h"
#include "AC2MapData.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr uint32 EnterWorldEffect = 0x41000010;
constexpr uint32 MenhirActivateEffect = 0x41000044;
constexpr uint32 ShardCameraBehavior = 0x410000a5;
constexpr uint32 CameraOriginMount = 0x40000004;
constexpr uint32 CameraTargetMount = 0x40000005;
FString S(const TSharedPtr<FJsonObject> &J, const TCHAR *K)
{
    FString V;
    if (J)
        J->TryGetStringField(K, V);
    return V;
}
double N(const TSharedPtr<FJsonObject> &J, const TCHAR *K, double D = 0)
{
    double V;
    return J && J->TryGetNumberField(K, V) && FMath::IsFinite(V) ? V : D;
}
TSharedPtr<FJsonObject> O(const TSharedPtr<FJsonObject> &J, const TCHAR *K)
{
    const TSharedPtr<FJsonObject> *V;
    return J && J->TryGetObjectField(K, V) ? *V : nullptr;
}
FVector V(const TSharedPtr<FJsonObject> &J, const TCHAR *K, FVector Default = FVector::ZeroVector)
{
    const TArray<TSharedPtr<FJsonValue>> *A;
    if (!J || !J->TryGetArrayField(K, A) || A->Num() != 3)
        return Default;
    FVector R((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
    return R.ContainsNaN() || R.GetAbsMax() > 1.e8 ? Default : R;
}
TArray<TSharedPtr<FJsonValue>> Array(FVector V)
{
    return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y),
            MakeShared<FJsonValueNumber>(V.Z)};
}
TSharedRef<FJsonObject> Command(const TCHAR *Name)
{
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("command"), Name);
    return J;
}
AAC2Explorer *Pawn(AAC2GameMode *M)
{
    auto PC = M->GetWorld()->GetFirstPlayerController();
    return PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
}
AAC2HUD *HUD(AAC2GameMode *M)
{
    auto PC = M->GetWorld()->GetFirstPlayerController();
    return PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
}
FAC2Appearance Aspects(const TSharedPtr<FJsonObject> &J)
{
    FAC2Appearance Out;
    const TArray<TSharedPtr<FJsonValue>> *A;
    if (J && J->TryGetArrayField(TEXT("aspects"), A))
        for (auto &E : *A)
        {
            auto P = E->AsObject();
            if (P && Out.Num() < 128)
                Out.Add({uint32(N(P, TEXT("table"))), uint32(N(P, TEXT("key"))), float(N(P, TEXT("value")))});
        }
    return Out;
}
} // namespace
FAC2Network::FAC2Network(AAC2GameMode *Owner) : Mode(Owner)
{
}
void FAC2Network::RemoveBagSlot(const FString& Id)
{
    if(const int32* Previous=BagSlots.Find(Id))
    {
        const int32 Slot=*Previous;BagSlots.Remove(Id);
        for(auto& Entry:BagSlots)if(Entry.Value>Slot)--Entry.Value;
    }
}
void FAC2Network::InsertBagSlot(const FString& Id,int32 Slot)
{
    if(Slot<0)return; // Native -1 can indicate a merge, not a visible bag cell.
    if(const int32* Previous=BagSlots.Find(Id);Previous && *Previous==Slot)return;
    RemoveBagSlot(Id);Slot=FMath::Clamp(Slot,0,BagSlots.Num());
    for(auto& Entry:BagSlots)if(Entry.Value>=Slot)++Entry.Value;
    BagSlots.Add(Id,Slot);
}
void FAC2Network::SyncInventoryItems()
{
    auto* Profile=Mode->Characters.Active();if(!Profile)return;
    for(const FString& Id:InventoryOwnership)
    {
        const auto* Found=Objects.Find(Id);if(!Found || !(*Found)->Entity)continue;
        const auto& Obj=**Found;
        auto* Item=Profile->Items.FindByPredicate([&](const auto& I){return I.NetworkId==Id;});
        if(!Item){Item=&Profile->Items.AddDefaulted_GetRef();Item->NetworkId=Id;}
        Item->Definition=Obj.Entity;Item->Quantity=Obj.Quantity;
        Item->Visual=uint32(N(Obj.CachedVisual,TEXT("did")));Item->Aspects=Aspects(Obj.CachedVisual);
        Item->Equipped=WornSlots.FindRef(Id);
        // Pending metadata must never steal another item's cell. Only the
        // server's bag order assigns cells; equipment does not count as bag space.
        Item->Cell=BagSlots.Contains(Id)?BagSlots[Id]:INDEX_NONE;
    }
    for(auto& Item:Profile->Items)
    {
        Item.Equipped=WornSlots.FindRef(Item.NetworkId);
        Item.Cell=BagSlots.Contains(Item.NetworkId)?BagSlots[Item.NetworkId]:INDEX_NONE;
    }
}
void FAC2Network::ConfirmPickup(const FString& Id,bool Requested)
{
    if(AcquisitionSounds.Contains(Id))return;
    AcquisitionSounds.Add(Id);
    if(auto* Audio=FAC2Audio::Get(Mode->GetWorld()))Audio->Play(0x2a0000ee);
    // Some servers acknowledge transfers without a physics pickup. Reuse the
    // DAT reach only for a requested, confirmed pickup, coalescing Loot All.
    // Explicit server behavior wins when it has already arrived.
    if(Requested && Time-LastPickupAnimation>2)
        if(auto* Player=Pawn(Mode);Player && Player->PlayPickup())
            LastPickupAnimation=PickupFallbackTime=Time;
}
FAC2Network::~FAC2Network()
{
    Send(Command(TEXT("quit")));
    FPlatformProcess::ClosePipe(nullptr, Write);
    Write = nullptr;
    if (Process.IsValid())
    {
        const double Deadline = FPlatformTime::Seconds() + .5;
        while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline)
            FPlatformProcess::Sleep(.01f);
        if (FPlatformProcess::IsProcRunning(Process))
            FPlatformProcess::TerminateProc(Process, true);
        FPlatformProcess::CloseProc(Process);
    }
    FPlatformProcess::ClosePipe(Read, nullptr);
    Read = nullptr;
    ClearWorld();
}
TOptional<uint32> FAC2Network::HeldImplement(uint32 Slot) const
{
    const auto* P=Mode->Characters.Active();
    if(!P || P->Id!=PlayerId || !IsWorld() || !InventoryKnown)return {};
    // Consult the inventory snapshot even if the worn object's create is still
    // in flight. Missing metadata must not turn a pending item into an empty hand.
    for(const auto& W:WornSlots)if(W.Value==Slot){
        const auto* Item=P->Items.FindByPredicate([&](const auto& I){return I.NetworkId==W.Key;});
        const auto* Def=Item?Mode->CharacterCatalog.Items.Find(Item->Definition):nullptr;
        return Def?Def->Implement:TOptional<uint32>();
    }
    return 1u;
}
void FAC2Network::SetAccountScope(const FString& Host, int32 Port, const FString& Account, bool Direct)
{
    const FString AccountKey = Direct ? Account : Account.ToLower();
    const FString Endpoint = Host.ToLower() + TEXT(":") + FString::FromInt(Port);
    ShortcutScope = FMD5::HashAnsiString(*(Endpoint + TEXT("/") + AccountKey));
    CharacterSelectionSection = FString::Printf(TEXT("AC2.CharacterSelection.%s.%s"),
        Direct ? TEXT("Icefire") : TEXT("AC2E"), *ShortcutScope);
    LastCharacterId.Reset();
    GConfig->GetString(*CharacterSelectionSection, TEXT("LastCharacter"), LastCharacterId, GGameUserSettingsIni);
}

void FAC2Network::SaveLastCharacter(const FString& Id)
{
    if (CharacterSelectionSection.IsEmpty() || LastCharacterId == Id)
        return;
    LastCharacterId = Id;
    if (Id.IsEmpty())
        GConfig->RemoveKey(*CharacterSelectionSection, TEXT("LastCharacter"), GGameUserSettingsIni);
    else
        GConfig->SetString(*CharacterSelectionSection, TEXT("LastCharacter"), *Id, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

bool FAC2Network::Start(const FAC2ConnectionSettings* Settings)
{
    Clock = {};
    FString Executable = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ac2-network-bridge.exe"));
    if (!FPaths::FileExists(Executable)) Executable = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("../Network/Bridge/target/release/ac2-network-bridge.exe"));
    FParse::Value(FCommandLine::Get(), TEXT("AC2Bridge="), Executable);
    FString Ticket = FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_TICKET"));
    FPlatformMisc::SetEnvironmentVar(TEXT("AC2_LOGIN_TICKET"), TEXT(""));
    void *ChildOut = nullptr;
    void *ChildIn = nullptr;
    if (!FPaths::FileExists(Executable) || !FPlatformProcess::CreatePipe(Read, ChildOut) ||
        !FPlatformProcess::CreatePipe(ChildIn, Write, true))
    {
        FPlatformProcess::ClosePipe(ChildIn, ChildOut);
        SetStatus(TEXT("disconnected"), TEXT("Network bridge unavailable. Run Build-AC2Network.ps1."));
        return false;
    }
    Process = FPlatformProcess::CreateProc(*Executable, TEXT(""), false, true, true, nullptr, 0, nullptr,
                                           ChildOut, ChildIn, ChildOut);
    FPlatformProcess::ClosePipe(ChildIn, ChildOut);
    if (!Process.IsValid())
    {
        SetStatus(TEXT("disconnected"), TEXT("Could not start network bridge."));
        return false;
    }
    FString Host = TEXT("127.0.0.1"), Account = TEXT("local"), Version = TEXT("7148");
    int32 Port = 9000;
    FParse::Value(FCommandLine::Get(), TEXT("AC2Server="), Host);
    FParse::Value(FCommandLine::Get(), TEXT("AC2Port="), Port);
    FParse::Value(FCommandLine::Get(), TEXT("AC2Account="), Account);
    FParse::Value(FCommandLine::Get(), TEXT("AC2Protocol="), Version);
    if(Settings){
        Host=Settings->Host;Port=Settings->Port;Account=Settings->Account;Ticket=Settings->Ticket;
        if(!Settings->Protocol.IsEmpty())Version=Settings->Protocol;
    }
    const bool Direct=Settings && Settings->DirectLogin;
    FriendsEnabled=!Direct;
    if(Direct){
        HandshakeSettings=*Settings;
        DataCacheRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ServerData")/
            FMD5::HashAnsiString(*(Host.ToLower()+TEXT(":")+FString::FromInt(Port))));
    }

    SetAccountScope(Host, Port, Account, Direct);
    auto J = Command(TEXT("connect"));
    J->SetStringField(TEXT("host"), Host);
    J->SetNumberField(TEXT("port"), Port);
    J->SetStringField(TEXT("account"), Account);
    J->SetStringField(TEXT("version"), Version);
    J->SetStringField(TEXT("dat_directory"),FPaths::ConvertRelativePathToFull(Mode->DatDirectory));
    J->SetStringField(TEXT("auth"),Direct?TEXT("gls_direct"):TEXT("account_service"));
    if(Direct){J->SetStringField(TEXT("password"),Settings->DirectPassword);J->SetStringField(TEXT("dat_cache_root"),DataCacheRoot);}
    else J->SetStringField(TEXT("ticket"), Ticket);
    Send(J);
    J->Values.Empty();
    Ticket.Reset();
    Locale = MakeUnique<FAC2DatArchive>();
    FString Error;
    Locale->Open(Mode->DatDirectory / TEXT("local_English.dat"), 3, Error);
    Smoke = FParse::Param(FCommandLine::Get(), TEXT("AC2NetworkSmoke"))||FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke"));
    SetStatus(TEXT("connecting"), TEXT("Connecting to ") + Host + TEXT(":") + FString::FromInt(Port));
    return true;
}
void FAC2Network::Send(TSharedRef<FJsonObject> J)
{
    if (!Write)
        return;
    const FString Action=S(J,TEXT("command"));
    if(Action==TEXT("use") || Action==TEXT("take_item") || Action==TEXT("loot") || Action==TEXT("transmute_loot") ||
        Action==TEXT("open_container") || Action==TEXT("close_container") || Action==TEXT("equip") ||
        Action==TEXT("unequip") || Action==TEXT("drop_item") || Action==TEXT("give_item") ||
        Action==TEXT("examine") || Action==TEXT("attack") || Action==TEXT("skill") || Action==TEXT("combat"))
    {
        // Whitelist numeric object identities only; never retain credentials,
        // chat, names or arbitrary serialized request contents in this trace.
        FString Line=FString::Printf(TEXT("%.3f %s"),Time,*Action);
        for(const TCHAR* Key:{TEXT("id"),TEXT("item"),TEXT("target"),TEXT("container")}){
            const FString Value=S(J,Key);
            if(!Value.IsEmpty() && Value.Len()<=20 && Value.IsNumeric())Line+=FString::Printf(TEXT(" %s=%s"),Key,*Value);
        }
        RecentActions.Add(Line);if(RecentActions.Num()>12)RecentActions.RemoveAt(0);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2ServerUISmoke")) && S(J,TEXT("command"))!=TEXT("move"))
        UE_LOG(LogTemp,Display,TEXT("AC2 UI probe send: %s"),*S(J,TEXT("command")));
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2ServerUISmoke")) && S(J,TEXT("command"))==TEXT("connect"))
        UE_LOG(LogTemp,Display,TEXT("AC2 UI probe profile: version=%s auth=%s"),*S(J,TEXT("version")),*S(J,TEXT("auth")));
    FString Text;
    FJsonSerializer::Serialize(J, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    Text += TEXT("\n");
    FTCHARToUTF8 Bytes(*Text);
    if (Bytes.Length() < 65536)
        FPlatformProcess::WritePipe(Write, reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
}
void FAC2Network::DeleteCharacter(const FString &Id)
{
    if (!IsSelection() || !Mode->Characters.Characters.ContainsByPredicate([&](const auto &C){return C.Id==Id;})) return;
    auto J=Command(TEXT("delete"));J->SetStringField(TEXT("id"),Id);Send(J);
}
void FAC2Network::SetStatus(const FString &State, const FString &Text)
{
    Phase = State;
    Message = Text;
    Mode->Notice = Text;
    if (auto H = HUD(Mode))
        if (H->CharacterUI)
            H->CharacterUI->NetworkMessage(Text);
    if (State == TEXT("disconnected"))
    {
        HandshakeSettings.Reset();
        Clock = {};
        ClearWorld();
        if (auto P = Pawn(Mode))
            P->SetHeld(true);
        Mode->ShowLogin(Text);
    }
    UE_LOG(LogTemp, Display, TEXT("AC2 network %s: %s"), *State, *Text);
}
void FAC2Network::LootAll()
{
    if(!LootContainer.IsEmpty()){
        for(const auto& Item:LootItems)PendingPickupSounds.Add(Item);
        auto J=Command(TEXT("loot"));J->SetStringField(TEXT("id"),LootContainer);Send(J);}
}
void FAC2Network::TransmuteLoot()
{
    if(!LootContainer.IsEmpty()){auto J=Command(TEXT("transmute_loot"));J->SetStringField(TEXT("id"),LootContainer);Send(J);}
}
void FAC2Network::TakeLoot(int32 Index)
{
    if(!LootItems.IsValidIndex(Index))return;
    auto J=Command(TEXT("take_item"));J->SetStringField(TEXT("id"),LootItems[Index]);J->SetStringField(TEXT("container"),LootContainer);
    PendingPickupSounds.Add(LootItems[Index]);
    J->SetNumberField(TEXT("from"),Index);Send(J);
}
void FAC2Network::CloseLoot(){
    if(!LootContainer.IsEmpty()){auto J=Command(TEXT("close_container"));J->SetStringField(TEXT("id"),LootContainer);Send(J);}
    LootContainer.Reset();LootItems.Reset();
}
uint32 FAC2Network::ItemIcon(const FString& Id) const
{
    if(const auto* Object=Objects.Find(Id)){
        Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,(*Object)->Entity);
        if(const auto* D=Mode->CharacterCatalog.Items.Find((*Object)->Entity))return D->Icon;
    }return 0;
}
void FAC2Network::ClearWorld()
{
    ServerArrivalEffectStrength = 0;
    RevealingArrivalEffect = false;
    ActiveStoryScene = 0;
    PendingShardUse.Reset();
    if (auto* Player = Pawn(Mode))
    {
        Player->CancelShardPresentation();
        Player->StopEffect(EnterWorldEffect);
    }
    ServerShortcuts.Reset();ShortcutSession=FGuid::NewGuid();ShortcutRevision=0;
    ServerGameOptions={};GameOptionsRevision=0;
    for (auto &Pair : Objects)
        if (Pair.Value->Actor.IsValid())
            Pair.Value->Actor->Destroy();
    Objects.Reset();PendingMissiles.Reset();PendingPickupSounds.Reset();AcquisitionSounds.Reset();StoryMissiles.Reset();RecentActions.Reset();
    LastPickupAnimation=PickupFallbackTime=-10;
    DamageTexts.Reset();
    PlayerGroove=0;
    ShopVendor.Reset();ShopOffers.Reset();
    Skills.Reset();SharedSkillReset={};FinishSkillStory();PendingSkills.Reset();ToggledSkills.Reset();Quests.Reset();SkillCredits=0;UntrainingSkill=0;AvailableXp=0;QuestPoints.Reset();LevelXpFloor=0;LevelXpSpan=0;ServerLevelXpFloor.Reset();ServerLevelXpCeiling.Reset();LootContainer.Reset();LootItems.Reset();
    Fellows.Reset();FellowshipName.Reset();FellowshipLeader.Reset();FellowshipInviter.Reset();FellowshipInvitation.Reset();FellowshipInvitationUntil=0;
    FellowshipLooting=0;FellowshipSharedXp=false;
    Friends.Reset();FriendsReceived=false;FriendError.Reset();FriendMode=0;
    Lieges.Reset();AllegianceName.Reset();AllegianceMotd.Reset();AllegiancePatron.Reset();AllegianceMonarch.Reset();AllegianceApplicant.Reset();AllegianceInvitation.Reset();AllegianceSize=0;AllegianceReceived=false;AllegianceInvitationUntil=0;
    CraftRecipes.Reset();CloseCraft();
    BagSlots.Reset();
    WornSlots.Reset();
    InventoryOwnership.Reset();InventoryKnown=false;
    PlayerId.Reset();
    TargetId.Reset();
    PlayerLookTarget.Reset();LookSelection.Reset();LookInterest={};LookReporter={};PlayerLookPose={};
    LastAttackerId.Reset();AutoTarget=true;
    ExaminationId.Reset();ExaminationRows.Reset();ExaminationError.Reset();ExaminationPending=false;
    ExaminationQueued=ExaminationDirty=false;ExaminationStarted=-1;
    HasPlayerPose = NeedsPlacement = false;
    PortalSpaceRequested = PortalMovementLocked = false;
    Dead = Combat = false;
    MaxHealth=MaxVigor=0;
    StatusEffects.Reset();
    Vitae=100; CurrentFocus=MaxFocus=PKDamage=PKVigorLoss=0; IsHero=false;
    VitaeRecoveryRemaining.Reset();
    PKStatus=ServerPKStatus=0;
    HaveLinkSample=false; RetransmitPercent=SentBytesPerSecond=ReceivedBytesPerSecond=LinkSilence=0;
    MapTravelEnabled = false;
    if(auto P=Pawn(Mode)){P->ResetMotionParameters();P->SetDead(false);P->SetHealth(100,100);P->SetHeld(true);P->SetServerMovementLocked(false);}
}
uint32 FAC2Network::Visual(const FString &Id) const
{
    auto P = CharacterVisuals.Find(Id);
    return P ? uint32(N(*P, TEXT("did"))) : 0;
}
bool FAC2Network::IsGroundItem(const FString& Id) const
{
    const auto* Object=Objects.Find(Id);
    return Object && (*Object)->GroundItem;
}
TArray<FAC2IconLayer> FAC2Network::ItemIconLayers(const FString& Id,FAC2AssetLibrary& Assets) const
{
    // Backpack creates keep their appearance in the inventory record and return
    // before world-actor visual setup. Use that authoritative visual for owned
    // items as the inventory panel does, rather than an empty world proxy.
    if(const auto* Player=Mode->Characters.Active())
        if(const auto* Item=Player->Items.FindByPredicate([&](const auto& I){return I.NetworkId==Id;}))
            return Item->Visual?Assets.IconLayers(Item->Visual,Item->Aspects):TArray<FAC2IconLayer>();
    const auto* Object=Objects.Find(Id);
    return Object?Assets.IconLayers((*Object)->Visual,(*Object)->Aspects):TArray<FAC2IconLayer>();
}
FAC2Appearance FAC2Network::Appearance(const FString &Id) const
{
    auto P = CharacterVisuals.Find(Id);
    return P ? Aspects(*P) : FAC2Appearance();
}
FString FAC2Network::StringInfo(const TSharedPtr<FJsonObject> &J) const
{
    FString Resolved;
    if(J && J->TryGetStringField(TEXT("resolved"),Resolved))return Resolved.Left(4096);
    FString Text = S(J, TEXT("literal"));
    if (Text.IsEmpty() && J && Locale)
        Text = AC2ReadUIString(*Locale, uint32(N(J, TEXT("table"))), uint32(N(J, TEXT("key"))));
    const TArray<TSharedPtr<FJsonValue>>* Variables=nullptr;
    if(J && J->TryGetArrayField(TEXT("variables"),Variables))for(const auto& Value:*Variables)
    {
        const auto Var=Value->AsObject();if(!Var)continue;
        const FString Replacement=StringInfo(O(Var,TEXT("text")));
        // The ordinary corpse branch of the native grammatical alternative.
        // Destroyed objects use a different script state; do not rename living NPCs.
        if(uint32(N(J,TEXT("key")))==0x509a8ff6 && uint32(N(Var,TEXT("key")))==0xe4ea98e6)
            Text=TEXT("Corpse of ")+Replacement;
        else Text.ReplaceInline(*FString::Printf(TEXT("%%%08X%%"),uint32(N(Var,TEXT("key")))),*Replacement);
    }
    return Text.Left(4096);
}
void FAC2Network::AddCharacter(const TSharedPtr<FJsonObject> &J)
{
    FString Id = S(J, TEXT("id"));
    if (Id.IsEmpty() || Mode->Characters.Characters.Num() >= 32)
        return;
    auto Visual = O(J, TEXT("visual"));
    CharacterVisuals.Add(Id, Visual);
    FAC2CharacterProfile P;
    P.Id = Id;
    P.Name = S(J, TEXT("name"));
    const uint32 Did = N(Visual, TEXT("did"));
    switch (Did)
    {
    case 0x1f000024:
        P.Sex = 0x2000;
        break;
    case 0x1f000025:
        P.Race = 2;
        break;
    case 0x1f000079:
        P.Race = 2;
        P.Sex = 0x2000;
        break;
    case 0x1f000026:
        P.Race = 4;
        break;
    case 0x1f00007a:
        P.Race = 4;
        P.Sex = 0x2000;
        break;
    default:
        break;
    }
    FVector Scale = V(Visual, TEXT("scale"), FVector::OneVector);
    P.Height = Scale.Z;
    P.Build = Scale.X;
    P.Health = P.Vigor = 0;
    Mode->Characters.Characters.RemoveAll([&](const FAC2CharacterProfile &C) { return C.Id == Id; });
    Mode->Characters.Characters.Add(P);
    if (Mode->Characters.ActiveId.IsEmpty())
        Mode->Characters.ActiveId = Id;
}
void FAC2Network::ApplyVisual(FObject &Obj, const TSharedPtr<FJsonObject> &J, bool IsPlayer)
{
    if (!J)
        return;
    Obj.CachedVisual=J;
    Obj.Visual = uint32(N(J, TEXT("did")));
    Obj.Scale = V(J, TEXT("scale"), FVector::OneVector);
    Obj.Aspects = Aspects(J);
    Obj.Scale.X = FMath::Clamp(Obj.Scale.X, .05, 10.);
    Obj.Scale.Y = FMath::Clamp(Obj.Scale.Y, .05, 10.);
    Obj.Scale.Z = FMath::Clamp(Obj.Scale.Z, .05, 10.);
    if (IsPlayer)
    {
        CharacterVisuals.Add(PlayerId, J);
        if (auto P = Pawn(Mode))
        {
            P->ApplyAvatar(Obj.Visual, Obj.Scale.Z, Obj.Scale.X, Obj.Aspects);
            if (ServerArrivalEffectStrength > 0 || RevealingArrivalEffect)
                P->PlayEffect(EnterWorldEffect, ServerArrivalEffectStrength > 0 ? ServerArrivalEffectStrength : 1.f);
            if(Obj.DamageFX>=0)P->PlayEffect(0x41000002,Obj.DamageFX);
        }
        if(auto H=HUD(Mode);H && H->GameUI)H->GameUI->RefreshInventoryAppearance();
        return;
    }
    if (Obj.Actor.IsValid())
        Obj.Actor->Destroy();
    Obj.Model.Reset();
    Obj.PresentationBounds=FBox(ForceInit);
    if (!Obj.Visual || (!Obj.Placed && Obj.Parent.IsEmpty()))
        return;
    auto A = Mode->GetWorld()->SpawnActor<AActor>();
    Obj.Actor = A;
    auto Root = NewObject<USceneComponent>(A);
    A->SetRootComponent(Root);
    Root->RegisterComponent();
    A->SetActorEnableCollision(false);
    A->SetActorLocation(Obj.Goal);
    A->SetActorRotation(Obj.Rotation);
    A->SetActorScale3D(Obj.Scale * Obj.EntityScale);
    auto Body=NewObject<USceneComponent>(A);
    Body->SetupAttachment(Root);Body->RegisterComponent();Obj.Body=Body;
    Obj.Model = MakeUnique<FAC2AssetLibrary>();
    if(Obj.Entity && (SelectionLook(Obj.Entity).Category&AC2Radar::Monsters))
        Obj.Model->EnableCreatureIdleAudio();
    FString Error;
    if (!Obj.Model->Open(Mode->DatDirectory, Error) ||
        !Obj.Model->DrawModel(Obj.Visual, A, Body, Obj.Aspects))
    {
        UE_LOG(LogTemp, Warning, TEXT("AC2 server visual unavailable: %08X"), Obj.Visual);
        A->Destroy();
        Obj.Model.Reset();
    }
    else
    {
        Obj.Model->SetHealth(Obj.Health,Obj.MaxHealth);
        Obj.Model->SetAnimationState(Obj.Element,AC2Locomotion::AnimationMode(Obj.Element,Obj.AnimMode));
        if(Obj.Dead && Obj.Model->PlayBehavior(AC2Motion::Death,Obj.DeathMode,Obj.DeathRate,true))
        {
            Obj.Model->SetAnimationState(Obj.Element,AC2Motion::Dead);
            Obj.Model->SeekBehavior(AC2Motion::Death,FMath::Max(0.,Time-Obj.DeathStarted));
            Obj.Model->Animate(Time,0);
        }
        if(Obj.DamageFX>=0)Obj.Model->PlayEffect(0x41000002,Obj.DamageFX);
        const bool GroundItem=Obj.GroundItem;
        const FBox FeetBounds=Obj.Model->ModelBounds(GroundItem?0:0x81000001);
        const FBox ModelBounds=Obj.Model->ModelBounds();
        const auto Motion=Obj.Model->MotionData().Values(AC2Motion::Ground);
        // Default motion values include gravity even for visuals with no native
        // motion descriptor. Static props must retain their authored pivot.
        const bool GroundedBody=ModelBounds.IsValid && (GroundItem ||
            (Obj.Model->HasLocomotion() && Motion.Gravity && FMath::IsNearlyZero(Motion.Hover)));
        Obj.GroundedBody=Obj.Parent.IsEmpty() && GroundedBody;
        // Feet are a mesh-type in the DAT. Do not use an effect's bounding box
        // or a tail tip as the humanoid's standing plane.
        // Creature meshes often have no humanoid feet part. Their native root
        // is the contact plane; a tail/wing bound must not become the feet.
        Obj.FootZ=GroundedBody && FeetBounds.IsValid?FeetBounds.Min.Z:0;
        if(GroundedBody)Obj.BodyHeight=FMath::Max(1.,ModelBounds.Max.Z-Obj.FootZ);
        if(GroundedBody && Obj.Parent.IsEmpty())Body->SetRelativeLocation(FVector(0,0,-Obj.FootZ));
        Obj.PresentationBounds=Obj.Model->ModelBounds();
        // Effect-only portals can have invisible interaction hulls rather than
        // visible model triangles. Preserve their original display extent too.
        if(!Obj.PresentationBounds.IsValid)
            Obj.PresentationBounds=A->GetComponentsBoundingBox(true).TransformBy(Body->GetComponentTransform().Inverse());
        Obj.Solid=!GroundItem && Obj.Content && Obj.Parent.IsEmpty() && Obj.Model->EnableWorldObjectCollision()>0;
        A->SetActorEnableCollision(Obj.Solid);
        if(Smoke)
        {
        const FBox Bounds = A->GetComponentsBoundingBox(true);
        UE_LOG(LogTemp, Display, TEXT("AC2 network model %08X size=%s"), Obj.Visual,
               *Bounds.GetSize().ToString());
        }
    }
}
void FAC2Network::Enter(const FString &Id)
{
    if (!IsSelection())
        return;
    ClearWorld();
    Mode->Characters.ActiveId = Id;
    auto J = Command(TEXT("enter"));
    J->SetStringField(TEXT("id"), Id);
    Send(J);
    SetStatus(TEXT("entering"), TEXT("Entering server world..."));
}
void FAC2Network::Create(const FString &Name, const FAC2AvatarChoice &A, float Height, float Build,
                         const TMap<uint32, int32> &Choices)
{
    if (!IsSelection())
        return;
    auto J = Command(TEXT("create"));
    J->SetStringField(TEXT("name"), Name);
    J->SetNumberField(TEXT("race"), A.Race);
    J->SetNumberField(TEXT("sex"), A.Sex);
    // gmCharGenResult::Clear / GetStartingPosition: zero asks the server for the
    // race/class default. HoldingLocation belongs only to the preview UIScene.
    J->SetNumberField(TEXT("start_area"), 0);
    TArray<TSharedPtr<FJsonValue>> Values;
    auto Add = [&](uint32 Key, float Value) {
        Values.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueNumber>(Key), MakeShared<FJsonValueNumber>(Value)}));
    };
    Add(1, Build);
    Add(2, Height);
    for (auto &C : Choices)
        if (auto Options = A.Choices.Find(C.Key))
            if (Options->IsValidIndex(C.Value))
                Add(C.Key, (*Options)[C.Value].Value);
    J->SetArrayField(TEXT("physical"), Values);
    Send(J);
    SetStatus(TEXT("creating"), TEXT("Creating character on server..."));
}
void FAC2Network::SceneComplete()
{
    // The server can stop Enter_World as soon as we acknowledge the scene.
    // The destination is still covered by the tunnel and its exit flash.
    RevealingArrivalEffect = true;
    if (ServerArrivalEffectStrength <= 0)
        if (auto* Player = Pawn(Mode)) Player->PlayEffect(EnterWorldEffect, 1.f);
    NeedsPlacement = false;
    Send(Command(TEXT("ready")));
    if (!PlayerId.IsEmpty() && CharacterVisuals.Contains(PlayerId))
        SaveLastCharacter(PlayerId);
    SetStatus(TEXT("world"), TEXT("Connected to server world"));
    if (auto H = HUD(Mode))
        if (H->CharacterUI)
            H->CharacterUI->NetworkEntered();
}
void FAC2Network::FinishArrivalEffect()
{
    if (!RevealingArrivalEffect) return;
    RevealingArrivalEffect = false;
    if (ServerArrivalEffectStrength <= 0)
        if (auto* Player = Pawn(Mode)) Player->StopEffect(EnterWorldEffect);
}
void FAC2Network::PlayStoryScenes(const TSharedPtr<FJsonObject>& MessageObject)
{
    const TArray<TSharedPtr<FJsonValue>>* Scenes = nullptr;
    if (!MessageObject->TryGetArrayField(TEXT("scenes"), Scenes) || Scenes->IsEmpty()) return;
    const uint32 FirstId = uint32(N((*Scenes)[0]->AsObject(), TEXT("id")));
    if (!FirstId || FirstId == ActiveStoryScene) return;
    if (ActiveStoryScene)
        if (auto* Player = Pawn(Mode)) Player->CancelShardPresentation();

    TArray<FAC2StoryFrame> Frames;
    for (const auto& SceneValue : *Scenes)
    {
        const auto Scene = SceneValue->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
        if (!Scene || !Scene->TryGetArrayField(TEXT("frames"), Keyframes)) continue;
        for (const auto& FrameValue : *Keyframes)
        {
            const auto Frame = FrameValue->AsObject();
            if (!Frame) continue;
            auto& Out = Frames.AddDefaulted_GetRef();
            Out.Duration = N(Frame, TEXT("duration"), -1);
            Out.Tracks = uint32(N(Frame, TEXT("tracks")));
            Out.Video = uint32(N(Frame, TEXT("video")));
            Out.Audio = uint32(N(Frame, TEXT("audio")));
            Out.Voice = uint32(N(Frame, TEXT("voice")));
            Out.Subtitle = StringInfo(O(Frame, TEXT("text")));
        }
    }
    ActiveStoryScene = FirstId;
    const TWeakObjectPtr<AAC2GameMode> Owner(Mode);
    if (auto* Player = Pawn(Mode); Player && Player->PlayShardScene(Frames, [Owner, FirstId]()
        {
            if (auto* Game = Owner.Get(); Game && Game->Network) Game->Network->CompleteStoryScene(FirstId);
        })) return;
    UE_LOG(LogTemp, Warning, TEXT("AC2 could not present story scene %08X"), FirstId);
    CompleteStoryScene(FirstId);
}
void FAC2Network::CompleteStoryScene(uint32 Id)
{
    if (!Id || ActiveStoryScene != Id) return;
    ActiveStoryScene = 0;
    auto Reply = Command(TEXT("story_scene_complete"));
    Reply->SetNumberField(TEXT("id"), Id);
    Send(Reply);
}
void FAC2Network::Logout()
{
    if (!IsWorld())
        return;
    auto J = Command(TEXT("logout"));
    J->SetStringField(TEXT("id"), PlayerId);
    Send(J);
    SetStatus(TEXT("leaving"), TEXT("Leaving server world..."));
    if (auto P = Pawn(Mode))
        P->SetHeld(true);
}
void FAC2Network::Chat(FString Text)
{
    if(Text.Equals(TEXT("/recall"),ESearchCase::IgnoreCase)){Recall();return;}
    if(Text.Equals(TEXT("/combat"),ESearchCase::IgnoreCase)){SetCombat(!Combat);return;}
    if(Text.StartsWith(TEXT("/cast "))){CastSkill(FCString::Strtoui64(*Text.Mid(6),nullptr,16));return;}
    if(Text.Equals(TEXT("/use"),ESearchCase::IgnoreCase)){UseTarget();return;}
    Text = Text.TrimStartAndEnd().Left(2048);
    if (Text.IsEmpty() || !IsWorld())
        return;
    if (Text.Equals(TEXT("/resurrect")))
    {
        Send(Command(TEXT("resurrect")));
        return;
    }
    if (Text.Equals(TEXT("/stop")))
    {
        Send(Command(TEXT("stop_attack")));
        return;
    }
    if (Text.Equals(TEXT("/loot")) && !TargetId.IsEmpty())
    {
        auto J = Command(TEXT("loot"));
        J->SetStringField(TEXT("id"), TargetId);
        Send(J);
        return;
    }
    auto J = Command(TEXT("chat"));
    FString Channel = TEXT("say");
    if(Text.Equals(TEXT("/source"),ESearchCase::IgnoreCase))
        Text=TEXT("@license");
    if (Text.Equals(TEXT("/cg"), ESearchCase::IgnoreCase))
    {
        Mode->Notice = TEXT("Use /cg message to speak in General chat.");
        return;
    }
    if (Text.StartsWith(TEXT("/cg "), ESearchCase::IgnoreCase))
    {
        Channel = TEXT("general");
        Text.RightChopInline(4);
        Text.TrimStartAndEndInline();
    }
    else if (Text.StartsWith(TEXT("/f "),ESearchCase::IgnoreCase) || Text.StartsWith(TEXT("/g "),ESearchCase::IgnoreCase) || Text.StartsWith(TEXT("/group "),ESearchCase::IgnoreCase))
    {
        Channel=TEXT("fellowship");Text.RightChopInline(Text.StartsWith(TEXT("/group "),ESearchCase::IgnoreCase)?7:3);
    }
    else if (Text.StartsWith(TEXT("/a "),ESearchCase::IgnoreCase) || Text.StartsWith(TEXT("/gu "),ESearchCase::IgnoreCase) || Text.StartsWith(TEXT("/guild "),ESearchCase::IgnoreCase))
    {
        Channel=TEXT("allegiance");int32 Space=0;Text.FindChar(TEXT(' '),Space);Text.RightChopInline(Space+1);
    }
    else if (Text.StartsWith(TEXT("/me ")))
    {
        Channel = TEXT("emote");
        Text.RightChopInline(4);
    }
    else if (Text.StartsWith(TEXT("/say ")))
        Text.RightChopInline(5);
    else if (Text.StartsWith(TEXT("/s ")))
        Text.RightChopInline(3);
    else if (Text.StartsWith(TEXT("/tell ")) || Text.StartsWith(TEXT("/t ")))
    {
        Text.RightChopInline(Text.StartsWith(TEXT("/tell ")) ? 6 : 3);
        FString Recipient;
        if (!Text.Split(TEXT(","), &Recipient, &Text))
        {
            Mode->Notice = TEXT("Use /tell Character Name, message");
            return;
        }
        Channel = TEXT("tell");
        J->SetStringField(TEXT("recipient"), Recipient.TrimStartAndEnd());
        Text.TrimStartAndEndInline();
    }
    else if (Text.StartsWith(TEXT("/")))
    {
        Mode->Notice = TEXT("Chat: /say, /me, /tell Name, message, /cg (General), /f (fellowship), /a (allegiance)");
        return;
    }
    J->SetStringField(TEXT("text"), Text);
    J->SetStringField(TEXT("channel"), Channel);
    Send(J);
}
void FAC2Network::Recall()
{
    if(!IsWorld())return;
    Send(Command(Dead?TEXT("resurrect"):TEXT("recall")));
    Mode->Notice=Dead?TEXT("Resurrection requested"):TEXT("Lifestone recall requested");
}
void FAC2Network::TargetNext()
{
    if (!IsWorld())
        return;
    TArray<FString> Ids;
    auto P = Pawn(Mode);
    if (!P)
        return;
    for (auto &E : Objects)
        if (Targetable(E.Key, *E.Value))
            Ids.Add(E.Key);
    Ids.Sort([&](const FString& A,const FString& B){
        const double DA=FVector::DistSquared(Objects[A]->Actor->GetActorLocation(),P->GetActorLocation());
        const double DB=FVector::DistSquared(Objects[B]->Actor->GetActorLocation(),P->GetActorLocation());
        return DA==DB?A<B:DA<DB;
    });
    if (Ids.IsEmpty())
    {
        Mode->Notice=TEXT("No visible targets nearby.");
        return;
    }
    SelectTarget(Ids[(Ids.Find(TargetId) + 1) % Ids.Num()]);
}
int32 FAC2Network::MusicGroove() const
{
    const auto* P=Pawn(Mode);if(!IsWorld()||!P)return 0;
    // gmRadarUI::UpdateGroove sums signed bytes within the radar's XY range.
    uint8 Sum=uint8(PlayerGroove);
    const double Range=(Cell&0xffff)>=0x100?5000.:7500.;
    for(const auto& Pair:Objects){const auto& O=*Pair.Value;
        if(Pair.Key!=PlayerId && O.Parent.IsEmpty() && O.Actor.IsValid() && O.Placed &&
            FVector::DistSquared2D(O.Actor->GetActorLocation(),P->GetActorLocation())<=Range*Range)
            Sum=uint8(Sum+uint8(O.Groove));
    }
    return int8(Sum);
}
void FAC2Network::AppendFoliageContacts(TArray<FAC2FoliageContact>& Contacts,const FVector& Focus) const
{
    TArray<FAC2FoliageContact> Nearby;
    for(const auto& Pair:Objects){
        const auto& O=*Pair.Value;auto* Actor=O.Actor.Get();
        if(Pair.Key==PlayerId || !Actor || Actor->IsHidden() || !O.Placed || !O.Parent.IsEmpty() || O.Dead || O.Missile ||
            !Mode->CellVisible(O.Cell) || !O.Model || (!O.Model->HasLocomotion() &&
                (!O.PresentationMotion.Active || O.PresentationMotion.From.Equals(O.PresentationMotion.To,1.))))continue;
        const FVector ContactFeet=O.Body.IsValid()?O.Body->GetComponentTransform().TransformPosition(FVector(0,0,O.FootZ)):Actor->GetActorLocation();
        if(FVector::DistSquared(ContactFeet,Focus)>FMath::Square(5000.))continue;
        const FVector Scale=Actor->GetActorScale3D();
        const FVector Extent=O.PresentationBounds.IsValid?O.PresentationBounds.GetExtent()*Scale:FVector(45);
        Nearby.Add({Actor->GetUniqueID(),ContactFeet,float(FMath::Clamp(FMath::Min(Extent.X,Extent.Y),20.,150.)),O.BodyHeight*float(Scale.Z)});
    }
    Nearby.Sort([&](const auto& A,const auto& B){
        const auto DA=FVector::DistSquared(A.Feet,Focus),DB=FVector::DistSquared(B.Feet,Focus);
        return DA==DB?A.Id<B.Id:DA<DB;
    });
    for(const auto& C:Nearby){if(Contacts.Num()>=AC2FoliageContactLimit)break;Contacts.Add(C);}
}
TArray<FAC2Network::FRadarContact> FAC2Network::RadarContacts() const
{
    TArray<FRadarContact> Result;
    if (!IsWorld()) return Result;
    const bool Indoors = (Cell & 0xffff) >= 0x100;
    for (const auto &E : Objects)
    {
        const auto &O = *E.Value;
        if (!Targetable(E.Key,O)) continue;
        if (((O.Cell & 0xffff) >= 0x100) != Indoors || (Indoors && (O.Cell >> 16) != (Cell >> 16))) continue;
        Result.Add({E.Key, O.Name, O.Actor, O.Entity, O.RadarShape, O.Health, O.RadarSelectable, O.RadarColor});
    }
    Result.Sort([](const auto &A, const auto &B) { return A.Id < B.Id; });
    return Result;
}
bool FAC2Network::Targetable(const FString &Id, const FObject &O) const
{
    const auto* P=Pawn(Mode);
    return IsWorld() && Id!=PlayerId && O.Placed && O.Parent.IsEmpty() && !O.Missile && O.RadarSelectable.Get(true) &&
        O.Actor.IsValid() && !O.Actor->IsHidden() && O.Cell && Mode->CellVisible(O.Cell) && P &&
        FVector::DistSquared(O.Actor->GetActorLocation(),P->GetActorLocation())<=FMath::Square(15000.);
}
bool FAC2Network::SelectWorldRay(const FVector &Origin, const FVector &Direction)
{
    const FString Id=WorldTargetAtRay(Origin,Direction);
    if(Id.IsEmpty()){ClearTarget();return false;}
    SelectTarget(Id);return true;
}
FString FAC2Network::WorldTargetAtRay(const FVector &Origin, const FVector &Direction) const
{
    if(!IsWorld())return FString();
    const FVector End=Origin+Direction.GetSafeNormal()*15000.;
    // Bounds are only a broad phase; creature visuals have no triangle collision:
    // large lifestones and animated limbs overlap other actors' screen rectangles.
    // Pick the current rendered triangles, bounded by dungeon/scenery collision.
    FHitResult Wall;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2Target),true,Pawn(Mode));
    double Limit=1;
    if(Mode->GetWorld()->LineTraceSingleByObjectType(Wall,Origin,End,
        FCollisionObjectQueryParams(ECC_WorldStatic),Query)) Limit=Wall.Time;
    FString Best;
    for(const auto& E:Objects)
    {
        if(!Targetable(E.Key,*E.Value))continue;
        const FBox Bounds=E.Value->Actor->GetComponentsBoundingBox(true);
        FVector Hit,Normal;float HitTime=0;
        if(!Bounds.IsValid || !FMath::LineExtentBoxIntersection(Bounds,Origin,End,FVector::ZeroVector,Hit,Normal,HitTime) || HitTime>=Limit)continue;
        TInlineComponentArray<UProceduralMeshComponent*> Meshes(E.Value->Actor.Get());
        bool HasSolidSurface=false,HasVisibleSurface=false;
        for(auto* Mesh:Meshes)if(Mesh->IsVisible() && !Mesh->bHiddenInGame)
            for(int32 Section=0;Section<Mesh->GetNumSections();++Section)
                if(const auto* Part=Mesh->GetProcMeshSection(Section);Part && Part->bSectionVisible && !Part->ProcIndexBuffer.IsEmpty())
                {
                    HasVisibleSurface=true;
                    if(const auto* Mat=Mesh->GetMaterial(Section);Mat && (Mat->GetBlendMode()==BLEND_Opaque || Mat->GetBlendMode()==BLEND_Masked))HasSolidSurface=true;
                }
        // Effect-only portals retain an alpha-zero native mesh as their
        // interaction hull. Keep it invisible/nonblocking, but selectable.
        const bool EffectHull=!HasVisibleSurface && E.Value->Model && E.Value->Model->HasStartupEffects();
        for(auto* Mesh:Meshes)
        {
            if(!Mesh->IsVisible() || Mesh->bHiddenInGame)continue;
            const FTransform Transform=Mesh->GetComponentTransform();
            const FVector LocalStart=Transform.InverseTransformPosition(Origin);
            const FVector LocalEnd=Transform.InverseTransformPosition(End);
            const FVector Delta=LocalEnd-LocalStart;
            if(Delta.IsNearlyZero())continue;
            for(int32 Section=0;Section<Mesh->GetNumSections();++Section)
            {
                const auto* Part=Mesh->GetProcMeshSection(Section);
                if(!Part || (!Part->bSectionVisible && !EffectHull))continue;
                // Glows, halos and spell billboards can span neighbouring actors;
                // they are visual effects, not the object's selectable surface.
                const auto* Material=Mesh->GetMaterial(Section);
                // Portals can consist entirely of translucent/additive native
                // mesh. Prefer solid surfaces when present so a character's
                // halo cannot steal clicks from a neighbour. Particle actors
                // are separate and are never included in this ray test.
                if(HasSolidSurface && Material && Material->GetBlendMode()!=BLEND_Opaque && Material->GetBlendMode()!=BLEND_Masked)continue;
                for(int32 I=0;I+2<Part->ProcIndexBuffer.Num();I+=3)
                    if(FMath::SegmentTriangleIntersection(LocalStart,LocalStart+Delta*Limit,
                        Part->ProcVertexBuffer[Part->ProcIndexBuffer[I]].Position,
                        Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+1]].Position,
                        Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+2]].Position,Hit,Normal))
                    {Limit=FVector::DotProduct(Hit-LocalStart,Delta)/Delta.SizeSquared();Best=E.Key;}
            }
        }
    }
    return Best;
}
bool FAC2Network::SmokeWorldProps(FString& Error) const
{
    int32 Portals=0,Grounded=0;
    for(const auto& Entry:Objects)
    {
        const auto& Obj=*Entry.Value;
        if(!Obj.Actor.IsValid() || Obj.Actor->IsHidden())continue;
        if(Obj.FollowsGround() && Obj.Body.IsValid())
        {
            FHitResult Hit;const FVector Foot=Obj.Body->GetComponentTransform().TransformPosition(FVector(0,0,Obj.FootZ));
            if(Mode->GetWorld()->LineTraceSingleByObjectType(Hit,Foot+FVector(0,0,50),Foot-FVector(0,0,200),FCollisionObjectQueryParams(ECC_WorldStatic)) && Hit.ImpactNormal.Z>.57f)
            {
                if(FMath::Abs(Foot.Z-Hit.ImpactPoint.Z-2)>3){Error=TEXT("Grounded NPC does not meet floor");return false;}
                ++Grounded;
            }
        }
        if(Obj.Visual!=0x1f00031e || !Targetable(Entry.Key,Obj))continue;
        bool Picked=false;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Obj.Actor.Get());
        for(auto* Mesh:Meshes)if(Mesh->IsVisible())
            for(int32 S=0;S<Mesh->GetNumSections() && !Picked;++S)
                if(const auto* Section=Mesh->GetProcMeshSection(S))
                    for(int32 I=0;I+2<Section->ProcIndexBuffer.Num() && !Picked;I+=3)
                    {
                        FVector Points[3];
                        for(int32 V=0;V<3;++V)Points[V]=Mesh->GetComponentTransform().TransformPosition(FVector(Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+V]].Position));
                        const FVector Normal=FVector::CrossProduct(Points[1]-Points[0],Points[2]-Points[0]).GetSafeNormal();
                        if(!Normal.IsNearlyZero())
                            for(double Side:{-1.,1.})Picked|=WorldTargetAtRay((Points[0]+Points[1]+Points[2])/3+Normal*(Side*20),-Normal*Side)==Entry.Key;
                    }
        if(!Picked)
        {
            for(auto* Mesh:Meshes)
                for(int32 S=0;S<Mesh->GetNumSections();++S)if(const auto* Part=Mesh->GetProcMeshSection(S))
                    UE_LOG(LogTemp,Display,TEXT("AC2 portal pick mesh visible=%d hidden=%d section=%d triangles=%d blend=%d"),Mesh->IsVisible(),Mesh->bHiddenInGame,Part->bSectionVisible,Part->ProcIndexBuffer.Num()/3,Mesh->GetMaterial(S)?int32(Mesh->GetMaterial(S)->GetBlendMode()):-1);
            Error=FString::Printf(TEXT("Native portal %s cell=%08X meshes=%d bounds=%s cannot be selected"),*Entry.Key,Obj.Cell,Meshes.Num(),*Obj.Actor->GetComponentsBoundingBox(true).ToString());return false;
        }
        ++Portals;
    }
    if(!Portals){Error=TEXT("No native portal present for picking regression");return false;}
    UE_LOG(LogTemp,Display,TEXT("AC2 world props passed: selectable portals=%d grounded NPCs=%d"),Portals,Grounded);
    return true;
}
void FAC2Network::SelectTarget(const FString &Id)
{
    if (!IsWorld() || Id == PlayerId) return;
    const auto *Object = Objects.Find(Id);
    if (!Object || !Targetable(Id,**Object)) return;
    TargetId = Id;
    auto J = Command(TEXT("target"));
    J->SetStringField(TEXT("id"), TargetId);
    Send(J);
}
void FAC2Network::ClearTarget()
{
    if(!IsWorld() || TargetId.IsEmpty())return;
    TargetId.Reset();auto J=Command(TEXT("target"));J->SetStringField(TEXT("id"),TEXT("0"));Send(J);
}
FVector FAC2Network::PlayerLook(float Delta)
{
    const auto* Controller=Mode->GetWorld()->GetFirstPlayerController();
    const auto* P=Controller?Cast<AAC2Explorer>(Controller->GetPawn()):nullptr;
    if(!P || !P->Body || !IsWorld())return FVector::ZeroVector;
    auto TargetLook=[&](const FString& Target)
    {
        PlayerLookTarget=Target;LookInterest={};
        auto J=Command(TEXT("look_target"));J->SetStringField(TEXT("id"),Target.IsEmpty()?TEXT("0"):Target);Send(J);
    };
    if(LookSelection!=TargetId)
    {
        LookSelection=TargetId;
        TargetLook(TargetId==PlayerId?FString():TargetId);
    }
    const auto* Found=Objects.Find(PlayerLookTarget);
    if(Found && (*Found)->Actor.IsValid() && !(*Found)->Actor->IsHidden())
    {
        const auto& Obj=**Found;
        const bool Creature=(SelectionLook(Obj.Entity).Category&(AC2Radar::Monsters|AC2Radar::NPCs|AC2Radar::Players))!=0;
        const FVector Point=AC2Look::TargetPoint(Obj.Actor->GetActorLocation(),Obj.BodyHeight*Obj.Actor->GetActorScale3D().Z,Creature);
        const FVector Eye=P->GetActorLocation()+FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FTransform Frame=P->Body->GetComponentTransform();
        if(!LookInterest.Update(Frame.InverseTransformVectorNoScale(Point-Eye),Delta))
            return PlayerLookPose.Aim(PlayerLookTarget,AC2Look::At(Frame,Eye,Point),Delta);
        TargetLook(FString());
    }
    else if(!PlayerLookTarget.IsEmpty())TargetLook(FString());
    const FVector CameraToFocus=P->Camera && P->Boom?
        P->Camera->GetForwardVector()*(P->Boom->TargetArmLength*.01):FVector::ZeroVector;
    return PlayerLookPose.Direction(AC2Look::Camera(P->Body->GetComponentTransform(),CameraToFocus),Delta,true);
}
void FAC2Network::ReportPlayerLook(const FVector& Pose)
{
    if(!IsWorld() || !LookReporter.Update(Time,!PlayerLookTarget.IsEmpty(),Pose))return;
    auto J=Command(TEXT("look_direction"));J->SetNumberField(TEXT("x"),Pose.X);J->SetNumberField(TEXT("y"),Pose.Z);Send(J);
}
void FAC2Network::SelectFellow(const FString& Id)
{
    if(!IsWorld() || !Fellows.ContainsByPredicate([&](const auto& F){return F.Id==Id;}))return;
    // A roster click selects the member (FellowshipHelper::ListenToMessage),
    // including self. It need not hit their rendered mesh. Remote scene presence
    // and authoritative cast range/visibility still apply.
    const auto* Object=Objects.Find(Id);
    if(Id!=PlayerId && (!Object || !(*Object)->Placed || !(*Object)->Parent.IsEmpty() ||
        !(*Object)->Cell || !Mode->CellVisible((*Object)->Cell))){
        Mode->Notice=TEXT("That fellowship member is not nearby.");return;}
    TargetId=Id;auto J=Command(TEXT("target"));J->SetStringField(TEXT("id"),Id);Send(J);
    const auto* Fellow=Fellows.FindByPredicate([&](const auto& F){return F.Id==Id;});
}
FString FAC2Network::ObjectName(const FString& Id) const
{
    const auto* O=Objects.Find(Id);
    return O?(*O)->Name:FString();
}
FBox FAC2Network::ObjectPresentationBounds(const FString& Id) const
{
    const auto* Entry=Objects.Find(Id);
    if(!Entry || !(*Entry)->Actor.IsValid())return FBox(ForceInit);
    const auto& Obj=**Entry;
    // Follow the object/body placement, not animated limbs, wing tips or idle
    // hover. Death is a one-time posture change: let the frame settle around
    // the collapsed corpse instead of keeping its old standing/flying extent.
    if(Obj.AnimMode!=AC2Motion::Dead && Obj.PresentationBounds.IsValid && Obj.Body.IsValid())
        return Obj.PresentationBounds.TransformBy(Obj.Body->GetComponentTransform());
    return Obj.Actor->GetComponentsBoundingBox(true);
}
bool FAC2Network::ObjectScreenBounds(const FString& Id,FBox2D& Bounds) const
{
    const auto* O=Objects.Find(Id);auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(!O || !Targetable(Id,**O) || !PC)return false;
    const auto Box=ObjectPresentationBounds(Id);
    Bounds=FBox2D(ForceInit);
    if(!Box.IsValid)return false;
    for(int32 I=0;I<8;++I)
    {
        FVector2D Point;
        if(!PC->ProjectWorldLocationToScreen(FVector(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z),Point))return false;
        Bounds+=Point;
    }
    return Bounds.bIsValid;
}
void FAC2Network::Examine(const FString& Id)
{
    if(!IsWorld() || Id.IsEmpty() || !Objects.Contains(Id))return;
    if(Id==ExaminationId && ExaminationPending)return;
    if(Id!=ExaminationId)ExaminationRows.Reset();
    ExaminationId=Id;ExaminationError.Reset();ExaminationPending=true;ExaminationQueued=true;ExaminationDirty=false;
    RefreshExamination(Id);
}
void FAC2Network::InvalidateExamination(const FString& Id)
{
    if(!Id.IsEmpty() && Id==ExaminationId)ExaminationDirty=true;
}
void FAC2Network::RefreshExamination(const FString& Id)
{
    if(!IsWorld() || Id.IsEmpty() || Id!=ExaminationId)return;
    if(!Objects.Contains(Id))
    {
        ExaminationRows.Reset();ExaminationPending=ExaminationQueued=false;
        ExaminationError=TEXT("That object is no longer available to examine.");
        return;
    }
    if(!ExaminationError.IsEmpty())return;
    const double Elapsed=FPlatformTime::Seconds()-ExaminationStarted;
    // A target change can land immediately after an automatic refresh. Queue
    // it past the server's 100 ms cooldown instead of losing the new request.
    if(ExaminationQueued)
    {
        if(Elapsed>=.125)RequestExamination();
        return;
    }
    // Refresh after server state changes, not continuously for an unchanged
    // object. Keep at most one request outstanding and coalesce combat updates.
    // A change during an outstanding request remains dirty after its reply.
    if(!ExaminationDirty || ExaminationPending || Elapsed<.5)return;
    RequestExamination();
}
void FAC2Network::RequestExamination()
{
    ExaminationError.Reset();ExaminationPending=true;ExaminationQueued=ExaminationDirty=false;
    ExaminationStarted=FPlatformTime::Seconds();
    auto J=Command(TEXT("examine"));J->SetStringField(TEXT("id"),ExaminationId);Send(J);
}
void FAC2Network::Attack()
{
    if(!IsWorld())return;
    if(Dead){Mode->Notice=TEXT("You cannot attack while dead. Home recalls to safety.");return;}
    const auto* Target=Objects.Find(TargetId);
    if(!Target || !Targetable(TargetId,**Target))
    {
        Mode->Notice=TEXT("Select a creature first: release the mouse buttons, click a creature or press T, then press 1 to attack.");
        return;
    }
    {
        auto J = Command(TEXT("attack"));
        J->SetStringField(TEXT("id"), TargetId);
        Send(J);
    }
}
void FAC2Network::SetCombat(bool Enabled)
{
    if (!IsWorld()) return;
    auto J = Command(TEXT("combat")); J->SetBoolField(TEXT("enabled"), Enabled); Send(J);
}
void FAC2Network::CastSkill(uint32 Skill)
{
    if (!IsWorld() || Dead) return;
    auto J = Command(TEXT("skill")); J->SetNumberField(TEXT("skill"), Skill);
    // The server resolves helpful casts to self for no target, enemies or
    // scenery, while preserving a deliberately selected player for healing.
    J->SetStringField(TEXT("target"), Skill == 0x343 ? PlayerId : (TargetId.IsEmpty() ? TEXT("0") : TargetId));
    Send(J);
}
void FAC2Network::ChangeSkill(uint32 Skill,FString Action)
{
    if(!IsWorld() || Dead || !Skill || Skill>65535 ||
       (Action!=TEXT("train") && Action!=TEXT("raise") && Action!=TEXT("untrain") && Action!=TEXT("cancel_untrain")))return;
    const FString Name=TEXT("skill_")+Action;
    auto J=Command(*Name);J->SetNumberField(TEXT("skill"),Skill);Send(J);
}
void FAC2Network::StarterSkill(int32 Category)
{
    auto P = Mode->Characters.Active(); if (!P || Category < 0 || Category > 2) return;
    const uint32 Human[] = {0xb0,0xcf,0x60}, Lugian[] = {0x166,0x1a0,0x111}, Tumerok[] = {0x2cc,0x316,0x2a0};
    CastSkill((P->Race == 2 ? Lugian : P->Race == 4 ? Tumerok : Human)[Category]);
}
void FAC2Network::ReceiveStoryMissiles(const TSharedPtr<FJsonObject>& J)
{
    const FString Owner=S(J,TEXT("id")),Attacker=S(J,TEXT("attacker"));
    const auto* Source=Objects.Find(Attacker);const auto* Stamp=Objects.Find(Owner);
    if(!Source || !Stamp || (*Stamp)->Instance!=int32(N(J,TEXT("instance"))))return;
    const uint32 Behavior=N(J,TEXT("behavior"));
    auto* Player=Attacker==PlayerId?Pawn(Mode):nullptr;
    const uint64 Animation=Player?Player->BehaviorInstance(Behavior):(*Source)->Model?(*Source)->Model->BehaviorInstance(Behavior):0;
    const auto Triggers=Player?Player->StoryHookDelays(Behavior):(*Source)->Model?(*Source)->Model->StoryHookDelays(Behavior):TArray<float>();
    const TArray<TSharedPtr<FJsonValue>>* Hooks=nullptr;
    if(!J->TryGetArrayField(TEXT("hooks"),Hooks))return;
    // The kind-2 hook prepares hand visuals; equipment already supplies that
    // visual here. Each kind-3 hook launches one independent transient missile.
    for(const auto& Value:*Hooks)if(auto H=Value->AsObject();H && N(H,TEXT("kind"))==3 && StoryMissiles.Num()<128){
        FStoryMissile M;M.Owner=Owner;M.Attacker=Attacker;M.Instance=(*Stamp)->Instance;
        M.Target=S(H,TEXT("target"));M.Weapon=S(H,TEXT("weapon"));M.Skill=N(J,TEXT("skill"));M.Hook=N(H,TEXT("attack_hook"));
        M.Behavior=Behavior;M.Animation=Animation;
        const uint32 Index=N(H,TEXT("anim_hook"));
        // StoryManager starts at hook 0; matching animation hooks increment it.
        // Missing animation hooks are flushed at behavior completion in retail.
        float Delay=0;
        if(Index && Animation){
            if(Triggers.IsValidIndex(Index-1))Delay=Triggers[Index-1];
            else if((*Source)->Model)Delay=(*Source)->Model->BehaviorDuration(Behavior);
            else if(Player)Delay=Player->StoryBehaviorDuration(Behavior);
        }
        M.Due=Time+Delay;StoryMissiles.Add(M);
    }
}
void FAC2Network::LaunchStoryMissiles()
{
    for(int32 I=StoryMissiles.Num()-1;I>=0;--I){
        const auto M=StoryMissiles[I];if(M.Due>Time)continue;
        StoryMissiles.RemoveAt(I);
        const auto* Stamp=Objects.Find(M.Owner);const auto* Source=Objects.Find(M.Attacker);const auto* Target=Objects.Find(M.Target);
        if(!Stamp || (*Stamp)->Instance!=M.Instance || !Source || !Target)continue;
        auto* Player=M.Attacker==PlayerId?Pawn(Mode):nullptr;
        const auto& A=**Source;const auto& T=**Target;
        const uint64 Active=Player?Player->BehaviorInstance(M.Behavior):A.Model?A.Model->BehaviorInstance(M.Behavior):0;
        if(M.Animation && Active && M.Animation!=Active)continue; // Replaced/cancelled attack.
        if(!CombatSkillData){CombatSkillData=MakeUnique<FAC2SkillData>();CombatSkillData->Open(Mode->DatDirectory);}
        const auto Data=CombatSkillData->Missile(M.Skill,M.Hook);if(!Data.IsSet())continue;
        const auto& D=Data.GetValue();
        FVector Origin=A.Goal,Destination=T.Goal;float SourceHeight=A.BodyHeight*A.Scale.Z*A.EntityScale,TargetHeight=T.BodyHeight*T.Scale.Z*T.EntityScale;
        if(Player){SourceHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2;Origin=Player->GetActorLocation()-FVector(0,0,SourceHeight*.5f);}
        else if(A.Actor.IsValid())Origin=A.Actor->GetActorLocation();else continue;
        if(M.Target==PlayerId){auto* P=Pawn(Mode);if(!P)continue;TargetHeight=P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2;Destination=P->GetActorLocation()-FVector(0,0,TargetHeight*.5f);}
        else if(T.Actor.IsValid())Destination=T.Actor->GetActorLocation();else continue;
        Origin.Z+=SourceHeight*D.SourceHeight;Destination.Z+=TargetHeight*D.TargetHeight;
        FTransform Socket;bool Held=false;
        if(D.Holding && Player)Held=Player->HoldingTransform(D.Holding,Socket);
        else if(D.Holding && A.Model && A.Body.IsValid()){
            Held=A.Model->AnimatedHoldingLocation(D.Holding,Socket);
            if(Held)Socket=Socket*A.Body->GetComponentTransform();
        }
        if(Held)Origin=Socket.GetLocation();
        TSharedPtr<FJsonObject> Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),D.Visual);
        Visual->SetArrayField(TEXT("scale"),Array(FVector::OneVector));
        float EntityScale=1;
        if(D.Clone)if(auto Weapon=Objects.Find(M.Weapon);Weapon && (*Weapon)->CachedVisual){Visual=(*Weapon)->CachedVisual;EntityScale=(*Weapon)->EntityScale;}
        if(!N(Visual,TEXT("did")))continue;
        auto New=MakeUnique<FObject>();auto& O=*New;
        O.Missile=O.LocalMissile=O.Placed=O.MissilePointKnown=O.MissileOffsetRelative=true;
        O.Cell=A.Cell;O.Entity=D.Entity;O.EntityScale=EntityScale;O.Goal=Origin;
        O.MissileTarget=M.Target;O.MissilePoint=Destination;O.MissileOffset=FVector(0,0,TargetHeight*D.TargetHeight);
        O.MissileVelocity=(Destination-Origin).GetSafeNormal()*D.Speed*100;
        if(!AC2Projectile::Valid(Origin,O.MissileVelocity,O.MissileOffset))continue;
        O.Rotation=AC2Projectile::Facing(O.MissileVelocity);
        ApplyVisual(O,Visual,false);
        if(!O.Actor.IsValid() || !O.Model)continue;
        if(O.Body.IsValid())O.Body->SetRelativeLocation(FVector::ZeroVector);
        O.Actor->SetActorEnableCollision(false);
        Objects.Add(FString::Printf(TEXT("story:%llu"),++NextLocalMissile),MoveTemp(New));
        ++SmokeMissiles;
    }
}
void FAC2Network::UseTarget()
{
    if (IsWorld() && !TargetId.IsEmpty())
    {
        PendingShardUse = TargetId;
        ShardUseTime = Time;
        auto J = Command(TEXT("use"));
        J->SetStringField(TEXT("target"), TEXT("0")); // Native self-use of a selected world object.
        J->SetStringField(TEXT("item"), TargetId);
        Send(J);
    }
}
void FAC2Network::ActivateShard(const FString& Id, const FString& Recipient)
{
    const auto* Obj = Objects.Find(Id);
    if (!Obj || !(*Obj)->Model) return;
    auto& Model = *(*Obj)->Model;
    FTransform EyeMount, FocusMount;
    // Select by the authored rig because content DIDs can vary by server.
    if (!Model.AnimatedHoldingLocation(CameraOriginMount, EyeMount) ||
        !Model.AnimatedHoldingLocation(CameraTargetMount, FocusMount)) return;
    if (!Model.BehaviorInstance(ShardCameraBehavior) && !Model.PlayBehavior(ShardCameraBehavior)) return;
    if (!Recipient.IsEmpty() && Recipient != TEXT("0"))
    {
        if (Recipient != PlayerId) return;
    }
    else if (PendingShardUse != Id || Time - ShardUseTime > 15.0) return;
    PendingShardUse.Reset();
    if (auto* Player = Pawn(Mode))
    {
        const TWeakObjectPtr<AAC2GameMode> Owner(Mode);
        const int32 Instance = (*Obj)->Instance;
        Player->BeginShardOrbit([Owner, Id, Instance](FVector& Eye, FVector& Focus)
        {
            const auto* Game = Owner.Get();
            if (!Game || !Game->Network) return false;
            const auto* Shard = Game->Network->Objects.Find(Id);
            if (!Shard || (*Shard)->Instance != Instance || !(*Shard)->Model || !(*Shard)->Body.IsValid()) return false;
            FTransform Origin, Target;
            if (!(*Shard)->Model->AnimatedHoldingLocation(CameraOriginMount, Origin) ||
                !(*Shard)->Model->AnimatedHoldingLocation(CameraTargetMount, Target)) return false;
            const FTransform World = (*Shard)->Body->GetComponentTransform();
            Eye = World.TransformPosition(Origin.GetLocation());
            Focus = World.TransformPosition(Target.GetLocation());
            return true;
        }, Model.BehaviorDuration(ShardCameraBehavior));
    }
}
void FAC2Network::UseItem(int32 Index, uint32 EquipSlot)
{
    auto P = Mode->Characters.Active();
    if (!IsWorld() || !P || !P->Items.IsValidIndex(Index))
        return;
    auto &I = P->Items[Index];
    auto D = Mode->CharacterCatalog.Items.Find(I.Definition);
    if (!D)
        return;
    // Material hand-ins use the existing targeted-use request. The server owns
    // the recipient, range, quantity and reward checks. Some quest conversations
    // also offer a hand-in when the required item is already in the backpack.
    uint32 ExchangeNpc = 0;
    switch (I.Definition)
    {
    case 0x470010f4: ExchangeNpc = 0x470015c4; break; // Stone -> Miller of Kehan
    case 0x470015b8: ExchangeNpc = 0x470015c2; break; // Drudge Cadre Bone -> Esper Warder
    case 0x470015b1: ExchangeNpc = 0x470015cc; break; // Warder Scout's Notebook -> Warder Scout
    case 0x470015b0: ExchangeNpc = 0x470015ce; break; // Massive Gear -> Windmill
    case 0x470022f3: case 0x47002355: ExchangeNpc = 0x4700230b; break; // Blossom/garland -> Black Mother Earth
    case 0x470022f8: ExchangeNpc = 0x47002308; break; // Rabbit Meat -> Feral Pet Lasher
    case 0x470022f9: ExchangeNpc = 0x47002309; break; // Scented Blossom -> Outcast Wasp Queen
    case 0x470022fd: ExchangeNpc = 0x470022fe; break; // Eggs -> Digger 'Dillo Mama
    case 0x470022fc: ExchangeNpc = 0x47002305; break; // Truffle -> Grutt Gourmand
    }
    if (ExchangeNpc)
    {
        const auto* Target = Objects.Find(TargetId);
        if (Target && (*Target)->Entity == ExchangeNpc && !I.Equipped)
        {
            auto J = Command(TEXT("use"));
            J->SetStringField(TEXT("item"), I.NetworkId);
            J->SetStringField(TEXT("target"), TargetId);
            Send(J);
            return;
        }
    }
    if(I.Definition==0x47002de4||I.Definition==0x47002de7||I.Definition==0x47002de8||I.Definition==0x47002dea||I.Definition==0x47002deb||I.Definition==0x47002dec){
        if(I.Equipped){Mode->Notice=TEXT("Move the gathering tool to your backpack before using it.");return;}
        if(TargetId.IsEmpty()||TargetId==TEXT("0")){Mode->Notice=TEXT("Select a deposit or your defeated creature, then use the gathering tool.");return;}
        auto J=Command(TEXT("use"));J->SetStringField(TEXT("item"),I.NetworkId);J->SetStringField(TEXT("target"),TargetId);Send(J);return;
    }
    if (I.Equipped)
    {
        auto J = Command(TEXT("unequip"));
        J->SetStringField(TEXT("id"), I.NetworkId);
        J->SetNumberField(TEXT("location"), I.Equipped);
        int32 Slot = 0;
        while (P->Items.ContainsByPredicate(
            [&](const FAC2InventoryItem &V) { return !V.Equipped && V.Cell == Slot; }))
            ++Slot;
        J->SetNumberField(TEXT("slot"), Slot);
        Send(J);
    }
    else if (D->Preferred)
    {
        auto J = Command(TEXT("equip"));
        J->SetStringField(TEXT("id"), I.NetworkId);
        J->SetNumberField(TEXT("location"), EquipSlot?EquipSlot:D->Preferred);
        J->SetNumberField(TEXT("slot"), I.Cell);
        Send(J);
    }
    else
    {
        auto J = Command(TEXT("use"));
        const auto* Object=Objects.Find(I.NetworkId);
        const int32 Targets=Object?(*Object)->UseTargets.Get(D->UseTargets):D->UseTargets;
        if(Targets>1){Mode->Notice=TEXT("This item requires multiple targets.");return;}
        if(Targets==1 && (TargetId.IsEmpty() || TargetId==TEXT("0") || TargetId==I.NetworkId))
        {Mode->Notice=TEXT("Select the object to use this item on, then use the item from your backpack.");return;}
        J->SetStringField(TEXT("item"), I.NetworkId);
        J->SetStringField(TEXT("target"), Targets==1 ? TargetId : TEXT("0"));
        Send(J);
    }
}
void FAC2Network::MoveItem(int32 Index, int32 Slot)
{
    auto P = Mode->Characters.Active();
    if (!IsWorld() || !P || !P->Items.IsValidIndex(Index) || Slot < 0 || Slot >= 78)
        return;
    auto &I = P->Items[Index];
    int32 Count=0;for(const auto& Item:P->Items)if(!Item.Equipped)++Count;
    Slot=FMath::Min(Slot,Count);
    if (I.Equipped)
    {
        auto J=Command(TEXT("unequip"));J->SetStringField(TEXT("id"),I.NetworkId);
        J->SetNumberField(TEXT("location"),I.Equipped);J->SetNumberField(TEXT("slot"),Slot);Send(J);return;
    }
    auto J = Command(TEXT("move_item"));
    J->SetStringField(TEXT("id"), I.NetworkId);
    J->SetNumberField(TEXT("from"), I.Cell);
    // The UI names the final cell; native sorting uses a boundary in the list
    // before removing the source. Otherwise dragging to the next cell is a no-op.
    J->SetNumberField(TEXT("to"), FMath::Min(Slot + (Slot > I.Cell ? 1 : 0), Count));
    J->SetNumberField(TEXT("quantity"), I.Quantity);
    Send(J);
}
void FAC2Network::BuyOffer(int32 Index)
{
    if(!IsWorld() || ShopVendor.IsEmpty() || !ShopOffers.IsValidIndex(Index))return;
    const auto& Offer=ShopOffers[Index];
    if(Offer.Currency!=0x47001108 || !FMath::IsFinite(Offer.Price) || Offer.Price<0 || Offer.Price>Gold){Mode->Notice=TEXT("You cannot afford that item.");return;}
    auto J=Command(TEXT("shop_buy"));J->SetStringField(TEXT("vendor"),ShopVendor);J->SetStringField(TEXT("offer"),Offer.Id);Send(J);
}
void FAC2Network::SellItem(int32 Index)
{
    const auto* P=Mode->Characters.Active();if(!IsWorld() || ShopVendor.IsEmpty() || !P || !P->Items.IsValidIndex(Index))return;
    const auto& Item=P->Items[Index];if(Item.Equipped){Mode->Notice=TEXT("Unequip the item before selling it.");return;}
    auto J=Command(TEXT("shop_sell"));J->SetStringField(TEXT("item"),Item.NetworkId);J->SetNumberField(TEXT("slot"),Item.Cell);J->SetNumberField(TEXT("quantity"),Item.Quantity);Send(J);
}
void FAC2Network::CloseShop()
{
    if(ShopVendor.IsEmpty())return;
    auto J=Command(TEXT("shop_close"));J->SetStringField(TEXT("vendor"),ShopVendor);Send(J);ShopVendor.Reset();ShopOffers.Reset();
}
namespace {
int64 CraftCurve(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,uint32 Level){
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!J||!J->TryGetArrayField(Key,A)||A->IsEmpty())return 0;
    const int32 Index=A->Num()==1?0:int32(Level);return A->IsValidIndex(Index)?int64((*A)[Index]->AsNumber()):0;
}
}
TArray<FAC2Network::FCraftInput> FAC2Network::CraftInputs() const
{
    TArray<FCraftInput> Result;const auto* R=CraftCatalog.Find(OpenCraftRecipe);if(!R)return Result;
    for(const auto& V:(*R)->GetArrayField(TEXT("ingredients"))){auto I=V->AsObject();if(!I||CraftSpinner<uint32(N(I,TEXT("minimum"))))continue;
        FCraftInput F;F.Ordinal=N(I,TEXT("ordinal"));F.Quantity=CraftCurve(I,TEXT("quantities"),CraftSpinner);F.Name=S(I,TEXT("name"));
        for(const auto& D:I->GetArrayField(TEXT("choices")))F.Choices.Add(uint32(D->AsNumber()));
        if(F.Ordinal>=1&&F.Ordinal<=16&&F.Quantity>0)Result.Add(F);
    }return Result;
}
void FAC2Network::CloseCraft()
{
    OpenCraftRecipe=0;CraftPending=false;CraftNotice.Reset();
    for(int32 I=0;I<16;++I){CraftIngredients[I].Reset();CraftAccepted[I]=false;}
}
void FAC2Network::OpenCraft(uint32 Recipe)
{
    // Reusing a recipe shortcut must not clear an in-flight craft or reset a
    // level already chosen in the ingredient window.
    if(!IsWorld() || Dead || CraftPending || OpenCraftRecipe==Recipe)return;
    const auto* R=CraftCatalog.Find(Recipe);if(!CraftRecipes.Contains(Recipe)||!R)return;
    CloseCraft();OpenCraftRecipe=Recipe;CraftSpinner=N(*R,TEXT("minimum"));PopulateCraft();
}
void FAC2Network::CraftLevel(int32 Delta)
{
    const auto* R=CraftCatalog.Find(OpenCraftRecipe);if(!R||CraftPending)return;
    CraftSpinner=FMath::Clamp(int32(CraftSpinner)+Delta,int32(N(*R,TEXT("minimum"))),int32(N(*R,TEXT("maximum"))));PopulateCraft();
}
void FAC2Network::RaiseCraft(uint32 Skill)
{
    if(!IsWorld()||Dead||Combat||!CraftProgress.Contains(Skill))return;
    auto J=Command(TEXT("craft_raise"));J->SetNumberField(TEXT("skill"),Skill);Send(J);
}
void FAC2Network::SetCraftIngredient(int32 Ordinal,const FString& Item)
{
    const auto* P=Mode->Characters.Active();
    if(!P||!IsWorld()||Dead||Combat||CraftPending||Ordinal<1||Ordinal>16)return;
    const auto Inputs=CraftInputs();const auto* Input=Inputs.FindByPredicate([&](const auto& F){return int32(F.Ordinal)==Ordinal;});
    const auto* Owned=P->Items.FindByPredicate([&](const auto& I){return I.NetworkId==Item&&!I.Equipped;});
    if(!Input||!Owned)return;
    CraftIngredients[Ordinal-1]=Item;CraftAccepted[Ordinal-1]=false;CraftNotice.Reset();
    auto J=Command(TEXT("craft_validate"));J->SetNumberField(TEXT("recipe"),OpenCraftRecipe);J->SetNumberField(TEXT("spinner"),CraftSpinner);
    J->SetNumberField(TEXT("ordinal"),Ordinal);J->SetStringField(TEXT("item"),Item);Send(J);
}
void FAC2Network::CycleCraftIngredient(int32 Ordinal)
{
    const auto* P=Mode->Characters.Active();if(!P||Ordinal<1||Ordinal>16)return;
    for(const auto& F:CraftInputs())if(int32(F.Ordinal)==Ordinal){
        TArray<FString> Choices;for(const auto& I:P->Items)if(!I.Equipped&&(F.Choices.IsEmpty()||F.Choices.Contains(I.Definition)))Choices.Add(I.NetworkId);
        if(Choices.IsEmpty())return;
        SetCraftIngredient(Ordinal,Choices[(Choices.IndexOfByKey(CraftIngredients[Ordinal-1])+1)%Choices.Num()]);return;
    }
}
void FAC2Network::PopulateCraft()
{
    const auto* P=Mode->Characters.Active();if(!P||!IsWorld()||Dead||Combat||!OpenCraftRecipe||CraftPending)return;
    CraftNotice.Reset();for(int32 I=0;I<16;++I){CraftIngredients[I].Reset();CraftAccepted[I]=false;}
    TMap<uint32,int64> Reserved;
    bool Missing=false;
    for(const auto& F:CraftInputs()){
        const auto* Item=P->Items.FindByPredicate([&](const auto& E){
            if(E.Equipped||!F.Choices.Contains(E.Definition))return false;
            int64 Total=0;for(const auto& Stack:P->Items)if(!Stack.Equipped&&Stack.Definition==E.Definition)Total+=Stack.Quantity;
            return Total-Reserved.FindRef(E.Definition)>=F.Quantity;
        });
        if(!Item){Missing=true;continue;}
        Reserved.FindOrAdd(Item->Definition)+=F.Quantity;SetCraftIngredient(F.Ordinal,Item->NetworkId);
    }
    if(Missing)CraftNotice=TEXT("Choose ingredients from your backpack.");
}
bool FAC2Network::CanCraft() const
{
    const auto* P=Mode->Characters.Active();const auto R=CraftCatalog.FindRef(OpenCraftRecipe);
    if(!P||!R||!IsWorld()||Dead||Combat||CraftPending||!CraftRecipes.Contains(OpenCraftRecipe))return false;
    if(CraftCurve(R,TEXT("cost"),CraftSpinner)>Gold)return false;
    TMap<uint32,int64> Reserved;
    for(const auto& F:CraftInputs()){
        const int32 I=F.Ordinal-1;
        const auto* Owned=P->Items.FindByPredicate([&](const auto& E){return E.NetworkId==CraftIngredients[I]&&!E.Equipped;});
        if(!CraftAccepted[I]||!Owned)return false;
        // Abstract category predicates are evaluated by the server. Explicit
        // material slots can share a stack only when their combined amount fits.
        if(!F.Choices.IsEmpty()){
            if(!F.Choices.Contains(Owned->Definition))return false;
            int64 Total=0;for(const auto& Stack:P->Items)if(!Stack.Equipped&&Stack.Definition==Owned->Definition)Total+=Stack.Quantity;
            Reserved.FindOrAdd(Owned->Definition)+=F.Quantity;
            if(Total<Reserved[Owned->Definition])return false;
        }
    }
    return true;
}
void FAC2Network::ExecuteCraft()
{
    if(!CanCraft())return;
    auto J=Command(TEXT("craft_execute"));J->SetNumberField(TEXT("recipe"),OpenCraftRecipe);J->SetNumberField(TEXT("spinner"),CraftSpinner);
    TArray<TSharedPtr<FJsonValue>> Items;for(const auto& F:CraftInputs()){auto I=MakeShared<FJsonObject>();I->SetNumberField(TEXT("ordinal"),F.Ordinal);I->SetStringField(TEXT("item"),CraftIngredients[F.Ordinal-1]);Items.Add(MakeShared<FJsonValueObject>(I));}
    J->SetArrayField(TEXT("ingredients"),Items);J->SetStringField(TEXT("helper"),PlayerId);Send(J);CraftPending=true;CraftNotice=TEXT("Crafting...");
}
void FAC2Network::DropItem(int32 Index)
{
    const auto* P=Mode->Characters.Active();
    if(!IsWorld() || Dead || !P || !P->Items.IsValidIndex(Index))return;
    const auto& I=P->Items[Index];
    if(I.Equipped){Mode->Notice=TEXT("Move equipped items to your backpack before dropping them.");return;}
    if(I.NetworkId.IsEmpty() || I.Quantity<=0 || I.Cell<0)return;
    auto J=Command(TEXT("drop_item"));J->SetStringField(TEXT("id"),I.NetworkId);
    J->SetNumberField(TEXT("from"),I.Cell);J->SetNumberField(TEXT("quantity"),I.Quantity);
    J->SetNumberField(TEXT("landblock"),Cell & 0xffff0000);Send(J);
}
void FAC2Network::GiveItem(int32 Index,const FString& Recipient)
{
    const auto* P=Mode->Characters.Active();
    if(!IsWorld() || Dead || !P || !P->Items.IsValidIndex(Index))return;
    const auto& I=P->Items[Index];
    if(I.Equipped){Mode->Notice=TEXT("Move equipped items to your backpack before giving them.");return;}
    if(I.NetworkId.IsEmpty() || I.Quantity<=0 || I.Cell<0)return;
    const auto* Found=Objects.Find(Recipient);
    if(Recipient==PlayerId || Recipient==I.NetworkId || !Found || !(*Found)->Placed ||
        !(*Found)->Parent.IsEmpty() || !(*Found)->Actor.IsValid() || (*Found)->Actor->IsHidden() ||
        (*Found)->Dead || (*Found)->Health==0 ||
        !(SelectionLook((*Found)->Entity).Category&(AC2Radar::Monsters|AC2Radar::NPCs|AC2Radar::Players)))
    {Mode->Notice=TEXT("That object cannot receive items.");return;}
    auto J=Command(TEXT("give_item"));J->SetStringField(TEXT("id"),I.NetworkId);
    J->SetStringField(TEXT("target"),Recipient);J->SetNumberField(TEXT("from"),I.Cell);
    J->SetNumberField(TEXT("quantity"),I.Quantity);Send(J);
    // Contents/removal notices own the inventory. A refused gift must not
    // disappear locally, and dropping on a non-recipient must not drop it nearby.
}
bool FAC2Network::ArrivalCollisionReady() const
{
    if(!Incoming.IsEmpty() || Time-LastSceneChange<.25)return false;
    for(const auto& Pair:Objects)
    {
        const auto& O=*Pair.Value;
        if(O.Content && O.Solid && O.Model && O.Placed && Mode->CellVisible(O.Cell) &&
            !O.Model->CollisionReady())return false;
    }
    return true;
}
void FAC2Network::Fellowship(const FString& Action,const FString& Value,int32 Looting,bool SharedXp)
{
    auto J=Command(TEXT("fellowship"));J->SetStringField(TEXT("action"),Action);
    J->SetStringField(Action==TEXT("create")?TEXT("name"):TEXT("id"),Value);
    if(Action==TEXT("create")){
        J->SetNumberField(TEXT("looting"),Looting);
        J->SetBoolField(TEXT("shared_xp"),SharedXp);
    }
    Send(J);
}
void FAC2Network::Allegiance(const FString& Action,const FString& Id)
{
    auto J=Command(TEXT("allegiance"));J->SetStringField(TEXT("action"),Action);J->SetStringField(TEXT("id"),Id);Send(J);
}
FString FAC2Network::FriendTarget()
{
    const auto* Target=Objects.Find(TargetId);
    if(TargetId!=PlayerId && Target && (SelectionLook((*Target)->Entity).Category&AC2Radar::Players))
        return (*Target)->Name;
    return {};
}
void FAC2Network::FriendAction(const FString& Action,const FString& Name,uint32 Availability)
{
    if(!FriendsEnabled || !IsWorld())return;
    auto J=Command(TEXT("friends"));
    J->SetStringField(TEXT("action"),Action);
    if(!Name.IsEmpty())J->SetStringField(TEXT("name"),Name);
    if(Availability)J->SetNumberField(TEXT("mode"),Availability);
    FriendError.Reset();
    Send(J);
}
void FAC2Network::Handle(const TSharedPtr<FJsonObject> &J)
{
    FString Event = S(J, TEXT("event")), Id = S(J, TEXT("id"));
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2ServerUISmoke")) && (Event==TEXT("error") || Event==TEXT("chat") || Event==TEXT("status")))
        UE_LOG(LogTemp,Display,TEXT("AC2 UI probe receive: %s %s"),*Event,*(Event==TEXT("chat")?TEXT("<received>"):S(J,TEXT("message"))));
    if (Event == TEXT("story_scenes"))
    {
        PlayStoryScenes(J);
        return;
    }
    if (Event == TEXT("portal_movement_lock"))
    {
        bool Locked;
        if (J->TryGetBoolField(TEXT("locked"), Locked))
        {
            PortalMovementLocked = Locked;
            if (!Locked) PortalSpaceRequested = false;
            if (auto* Player = Pawn(Mode)) Player->SetServerMovementLocked(Locked);
        }
        return;
    }
    if (Event == TEXT("portal_space"))
    {
        bool Enter;
        if (J->TryGetBoolField(TEXT("active"), Enter))
        {
            if (Enter && IsWorld() && !Mode->WantsPortalSpace())
                if (auto* H = HUD(Mode); H && H->PortalEffect) H->PortalEffect->HoldDeparture();
            // Exit precedes the stamped destination. Keep the tunnel up across packet boundaries.
            if (Enter || !PortalMovementLocked) PortalSpaceRequested = Enter;
        }
        return;
    }
    if(Event==TEXT("shortcuts")){ReceiveShortcuts(J);return;}
    if(Event==TEXT("game_options")){ReceiveGameOptions(J);return;}
    if(Event==TEXT("logout_cancelled")){
        if(Phase==TEXT("leaving")){SetStatus(TEXT("world"),S(J,TEXT("message")));if(auto P=Pawn(Mode))P->SetHeld(false);}
        return;
    }
    if(Event==TEXT("friends") || Event==TEXT("friend_status") || Event==TEXT("friend_error") || Event==TEXT("friend_result") || Event==TEXT("friend_mode")){
        if(!FriendsEnabled || !IsWorld())return;
        if(Event==TEXT("friend_error")){
            Mode->Notice=FriendError=S(J,TEXT("message"));return;
        }
        if(Event==TEXT("friend_result")){FriendError.Reset();return;}
        if(Event==TEXT("friend_mode")){
            const uint32 Flags=uint32(N(J,TEXT("mode")));
            if(Flags==1 || Flags==2 || Flags==4)FriendMode=Flags;
            return;
        }
        if(Event==TEXT("friend_status")){
            const FString Name=S(J,TEXT("name"));
            if(auto* Friend=Friends.FindByPredicate([&](const auto& F){return F.Name.Equals(Name,ESearchCase::IgnoreCase);})){
                Friend->Mode=uint32(N(J,TEXT("mode")));
                const FString Location=S(J,TEXT("location"));
                if(!Friend->Mode || !Location.IsEmpty())Friend->Location=Location;
            }
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Members=nullptr;
        if(!J->TryGetArrayField(TEXT("members"),Members))return;
        Friends.Reset(Members->Num());
        for(const auto& Member:*Members){
            const auto M=Member->AsObject();if(!M)continue;
            Friends.Add({S(M,TEXT("name")),S(M,TEXT("location")),uint32(N(M,TEXT("mode")))});
        }
        Friends.Sort([](const auto& A,const auto& B){return A.Name.Compare(B.Name,ESearchCase::IgnoreCase)<0;});
        FriendsReceived=true;FriendError.Reset();return;
    }
    if(Event==TEXT("allegiance")){
        Lieges.Reset();AllegianceReceived=true;AllegianceName=StringInfo(O(J,TEXT("name")));AllegianceMotd=StringInfo(O(J,TEXT("motd")));AllegianceSize=N(J,TEXT("total"));
        AllegiancePatron=S(O(J,TEXT("patron")),TEXT("id"));AllegianceMonarch=S(O(J,TEXT("monarch")),TEXT("id"));
        auto Add=[&](const TSharedPtr<FJsonObject>& M,const TCHAR* Role){if(!M)return;const FString Id=S(M,TEXT("id"));if(Id.IsEmpty())return;
            if(Lieges.ContainsByPredicate([&](const auto& V){return V.Id==Id;}))return;
            bool Online=false;M->TryGetBoolField(TEXT("online"),Online);
            Lieges.Add({Id,StringInfo(O(M,TEXT("name"))),StringInfo(O(M,TEXT("title"))),Role,int32(N(M,TEXT("level"))),int32(N(M,TEXT("rank"))),Online});};
        Add(O(J,TEXT("patron")),TEXT("Patron"));Add(O(J,TEXT("monarch")),TEXT("Monarch"));Add(O(J,TEXT("me")),TEXT("You"));
        const TArray<TSharedPtr<FJsonValue>>* Vassals=nullptr;if(J->TryGetArrayField(TEXT("vassals"),Vassals))for(const auto& V:*Vassals)Add(V->AsObject(),TEXT("Vassal"));return;
    }
    if(Event==TEXT("allegiance_invitation")){
        AllegianceApplicant=Id;AllegianceInvitation=StringInfo(O(J,TEXT("name")))+TEXT(" asks to swear allegiance to you.");AllegianceInvitationUntil=FPlatformTime::Seconds()+60;
        Mode->Notice=AllegianceInvitation+TEXT(" Open Social > Allegiance to accept or decline.");return;
    }
    if(Event==TEXT("allegiance_result")){
        Mode->Notice=N(J,TEXT("result"))==0?TEXT("Allegiance updated with ")+StringInfo(O(J,TEXT("name")))+TEXT("."):TEXT("The allegiance request was declined or could not be completed.");
        if(N(J,TEXT("result"))==0)Allegiance(TEXT("query"));return;
    }
    if(Event==TEXT("fellowship_clear")){Fellows.Reset();FellowshipName.Reset();FellowshipLeader.Reset();FellowshipLooting=0;FellowshipSharedXp=false;return;}
    if(Event==TEXT("fellowship")){
        FellowshipLooting=FMath::Clamp(int32(N(J,TEXT("looting"))),0,4);
        FellowshipSharedXp=(uint32(N(J,TEXT("flags")))&1u)!=0;
        Fellows.Reset();FellowshipName=S(J,TEXT("name"));FellowshipLeader=S(J,TEXT("leader"));FellowshipInviter.Reset();
        const TArray<TSharedPtr<FJsonValue>>* Members=nullptr;
        if(J->TryGetArrayField(TEXT("members"),Members))for(const auto& V:*Members){auto M=V->AsObject();if(!M)continue;
            Fellows.Add({S(M,TEXT("id")),StringInfo(O(M,TEXT("name"))),int32(N(M,TEXT("level"))),int32(N(M,TEXT("health"))),int32(N(M,TEXT("max_health"))),int32(N(M,TEXT("vigor"))),int32(N(M,TEXT("max_vigor"))),uint32(N(M,TEXT("cell")))});}
        Fellows.Sort([&](const auto& A,const auto& B){if((A.Id==FellowshipLeader)!=(B.Id==FellowshipLeader))return A.Id==FellowshipLeader;return A.Name<B.Name;});return;
    }
    if(Event==TEXT("fellowship_invitation")){
        FellowshipInviter=Id;FellowshipInvitation=StringInfo(O(J,TEXT("inviter")))+TEXT(" invites you to ")+StringInfo(O(J,TEXT("name")));
        FellowshipInvitationUntil=FPlatformTime::Seconds()+30;Mode->Notice=FellowshipInvitation+TEXT(". Open the flag panel to accept or decline.");return;
    }
    if(Event==TEXT("fellowship_vital")){
        if(auto* M=Fellows.FindByPredicate([&](const auto& F){return F.Id==Id;})){
            const FString Field=S(J,TEXT("field"));const int32 Value=N(J,TEXT("value"));
            if(Field==TEXT("health"))M->Health=Value;else if(Field==TEXT("vigor"))M->Vigor=Value;
            else if(Field==TEXT("max_health"))M->MaxHealth=Value;else if(Field==TEXT("max_vigor"))M->MaxVigor=Value;else if(Field==TEXT("cell"))M->Cell=Value;
        }return;
    }
    if(Event==TEXT("object") || Event==TEXT("visual") || Event==TEXT("remove") || Event==TEXT("world_leave") || Event==TEXT("contain") || Event==TEXT("parent"))
    { LastSceneChange=Time; ++SceneRevision; }
    if (Event == TEXT("pvp_status")) {
        if(J->HasField(TEXT("server")))ServerPKStatus=uint32(N(J,TEXT("server")));
        PKStatus=ServerPKStatus ? ServerPKStatus : uint32(N(J,TEXT("region")));
        return;
    }
    if (Event == TEXT("link_status")) {
        RetransmitPercent=N(J,TEXT("retransmit_percent"));
        SentBytesPerSecond=N(J,TEXT("sent_bps"));
        ReceivedBytesPerSecond=N(J,TEXT("received_bps"));
        LinkSilence=N(J,TEXT("silence_seconds"));
        HaveLinkSample=true;
        return;
    }
    if (Event == TEXT("status_effects")) {
        bool Replace=false;
        J->TryGetBoolField(TEXT("replace"),Replace);
        if(Replace)StatusEffects.Reset();
        if(J->HasField(TEXT("remove")))StatusEffects.Remove(uint32(N(J,TEXT("remove"))));
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(J->TryGetArrayField(TEXT("effects"),Rows))for(const auto& Value:*Rows) {
            const auto Row=Value->AsObject();
            if(!Row)continue;
            const uint32 EffectId=N(Row,TEXT("eid")), Flags=N(Row,TEXT("flags"));
            if(Flags&0x800){StatusEffects.Remove(EffectId);continue;}
            auto& Effect=StatusEffects.FindOrAdd(EffectId);
            Effect.Id=EffectId;
            Effect.Flags=Flags;
            Effect.Order=N(Row,TEXT("order"),Effect.Order);
            Effect.Icon=N(Row,TEXT("icon"));
            Effect.Name=S(Row,TEXT("name"));
            Effect.Description=S(Row,TEXT("description"));
            Effect.Cast=N(Row,TEXT("cast"));
            Effect.Duration=N(Row,TEXT("duration"));
        }
        return;
    }
    if (Event == TEXT("clock"))
    {
        double Seconds;
        if (J->TryGetNumberField(TEXT("server_time"), Seconds)) Clock.Adopt(Seconds, FPlatformTime::Seconds());
        return;
    }
    if(Event==TEXT("open_examination"))
    {
        // WM_Player::26 / CPlayer::E9 selects the object and opens Examine.
        // Never issue follow-up requests for an absent or removed object.
        if(!IsWorld() || Id.IsEmpty() || !Objects.Contains(Id))return;
        TargetId=Id;
        auto Target=Command(TEXT("target"));Target->SetStringField(TEXT("id"),Id);Send(Target);
        if(auto H=HUD(Mode))if(H->GameUI)H->GameUI->InspectTarget(Id);
        return;
    }
    if(Event==TEXT("examination_error"))
    {
        if(ExaminationPending && !ExaminationQueued && (Id.IsEmpty() || Id==ExaminationId))
        {ExaminationPending=false;ExaminationError=S(J,TEXT("message"));}
        return;
    }
    if(Event==TEXT("examination"))
    {
        if(Id!=ExaminationId || !Objects.Contains(Id))return; // Ignore old targets and removed objects.
        ExaminationPending=ExaminationQueued=false;ExaminationRows.Reset();ExaminationError.Reset();
        const TArray<TSharedPtr<FJsonValue>>* Nodes=nullptr;
        if(J->TryGetArrayField(TEXT("nodes"),Nodes))for(const auto& Value:*Nodes)
        {
            const auto Node=Value->AsObject();if(!Node)continue;
            FExaminationRow Row;
            const uint32 Kind=Row.Kind=N(Node,TEXT("kind"));
            Row.Appearance=N(Node,TEXT("appearance"));
            if(Kind==0x40000005)Row.Image=N(Node,TEXT("image"));
            if(Kind==0x40000009){
                const TArray<TSharedPtr<FJsonValue>>* Layers=nullptr;
                if(Node->TryGetArrayField(TEXT("layers"),Layers))for(const auto& L:*Layers)
                    if(const auto O=L->AsObject())Row.Layers.Add({uint32(N(O,TEXT("layer"))),uint32(N(O,TEXT("image"))),uint32(N(O,TEXT("color")))});
                Row.Layers.StableSort([](const auto& A,const auto& B){return A.Layer<B.Layer;});
            }
            if(Kind==0x40000004)
            {
                const auto Info=Node->GetObjectField(TEXT("text"));
                FString Text=StringInfo(Info);
                // Localized noun annotations are grammar metadata, not part of the display name.
                if(N(Info,TEXT("table")))while(Text.EndsWith(TEXT("]")))
                {int32 Start;if(!Text.FindLastChar('[',Start) || Text.Len()-Start>8)break;Text.LeftInline(Start);}
                Row.Text+=Text;
            }
            else if(Kind==0x40000007)Row.Tabs=FMath::Clamp(int32(N(Node,TEXT("value"))),1,256);
            else if(Kind==0x40000003)Row.Text+=Node->GetBoolField(TEXT("value"))?TEXT("Yes"):TEXT("No");
            else if(Kind==0x4000000a)Row.Text+=S(Node,TEXT("value"));
            else if(Kind==0x40000001)Row.Text=FString::FromInt(int32(N(Node,TEXT("value"))));
            else if(Kind==0x40000002)Row.Text=FString::Printf(TEXT("%.2f"),N(Node,TEXT("value")));
            else if(Kind==0x40000008)Row.Text=FString::SanitizeFloat(N(Node,TEXT("value")));
            ExaminationRows.Add(MoveTemp(Row));
        }
        // The server supplies instance state and authorization. Descriptions
        // come from the matching native item definition, as with inventory.
        if(const auto* Object=Objects.Find(Id)){
            const auto& Obj=**Object;
            const uint64 Namespace=FCString::Strtoui64(*Id,nullptr,10)>>48;
            if(Obj.GroundItem || OwnsInventoryItem(Id) || LootItems.Contains(Id) || Namespace==0x8191 || Namespace==0x8130){
                Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Obj.Entity);
                if(const auto* D=Mode->CharacterCatalog.Items.Find(Obj.Entity)){
                    FString Existing;for(const auto& Line:ExaminationRows){
                        Existing+=Line.Text;
                        if(!Line.Kind || Line.Kind==0x40000006)Existing+=TEXT("\n");
                    }
                    auto Normalized=[](const FString& Text){
                        TArray<FString> Words;
                        Text.ParseIntoArrayWS(Words);
                        return FString::Join(Words,TEXT(" "));
                    };
                    auto AppendMissing=[&](const FString& Text){if(!Text.IsEmpty() && !Normalized(Existing).Contains(Normalized(Text))){
                        if(!ExaminationRows.IsEmpty() && ExaminationRows.Last().Kind && ExaminationRows.Last().Kind!=0x40000006){
                            FExaminationRow Break;Break.Kind=0x40000006;ExaminationRows.Add(Break);
                        }
                        ExaminationRows.Add({Text});Existing+=Text+TEXT("\n");
                    }};
                    AppendMissing(D->Description);
                    if(D->MinLevel>1 && !Existing.Contains(TEXT("Required level:")))AppendMissing(FString::Printf(TEXT("Required level: %d"),D->MinLevel));
                    if(D->Race && !Existing.Contains(TEXT("Race:")))AppendMissing(TEXT("Race: ")+FAC2CharacterCatalog::RaceName(D->Race));
                }
            }
        }
        return;
    }
    if(Event==TEXT("skill_requested"))
    {
        const uint32 Skill=N(J,TEXT("skill")),Request=N(J,TEXT("request"));
        if(IsWorld() && !Dead && Skill>0 && Skill<=65535 && !PendingSkills.Contains(Request))PendingSkills.Add(Request,Skill);
        return;
    }
    if(Event==TEXT("skill_finished")){FinishSkillRequest(N(J,TEXT("request")));return;}
    if(Event==TEXT("story_target")){ReceiveStoryTarget(J);return;}
    if(Event==TEXT("story_missiles")){ReceiveStoryMissiles(J);return;}
    if(Event==TEXT("story"))
    {
        const auto* Obj=Objects.Find(Id);
        if(!Obj || int32(N(J,TEXT("instance")))!=(*Obj)->Instance)return;
        // FinishStory flushes any unplayed hooks before replacing a story.
        // Do this before the bridge's following behavior starts a new action.
        for(auto& Pending:StoryMissiles)if(Pending.Owner==Id)Pending.Due=Time;
        LaunchStoryMissiles();
        if(Id!=PlayerId)return;
        // Legions StoryManager::DoStory finishes the preceding owner story,
        // even when this story has only hooks and no animation.
        FinishSkillStory();bool Animated=false;double Request=0;
        J->TryGetBoolField(TEXT("animated"),Animated);
        if(!Animated && J->TryGetNumberField(TEXT("request"),Request))FinishSkillRequest(uint32(Request));
        return;
    }
    if(Event==TEXT("skill_toggled"))
    {
        const uint32 Skill=N(J,TEXT("skill"));bool Enabled=false;
        if(IsWorld() && Skill>0 && Skill<=65535 && J->TryGetBoolField(TEXT("enabled"),Enabled)){
            if(Enabled)ToggledSkills.Add(Skill);else ToggledSkills.Remove(Skill);
        }
        return;
    }
    if (Event == TEXT("combat"))
    {
        Combat = J->GetBoolField(TEXT("enabled"));
        return;
    }
    if(Event==TEXT("look") || Event==TEXT("look_direction"))
    {
        auto* Found=Objects.Find(Id);
        if(!Found || int32(N(J,TEXT("instance")))!=(*Found)->Instance)return;
        auto& Obj=**Found;
        if(Event==TEXT("look"))
        {
            Obj.LookTarget=S(J,TEXT("target"));
            if(Obj.LookTarget==TEXT("0") || Obj.LookTarget==TEXT("18446744073709551615"))Obj.LookTarget.Reset();
            if(Id==PlayerId){PlayerLookTarget=Obj.LookTarget;LookSelection=TargetId;LookInterest={};}
        }
        else
        {
            const FVector Direction(N(J,TEXT("x")),0,N(J,TEXT("y")));
            if(Direction.ContainsNaN())return;
            Obj.LookDirection=Direction;
        }
        return;
    }
    if(Event==TEXT("missile"))
    {
        if(Id.IsEmpty() || Id==PlayerId)return;
        auto Found=Objects.Find(Id);
        if(!Found){
            // Retail GetObjectOrQueueEvent retains InitializeMissile until
            // CreateObject arrives. Physics events may overtake world creation.
            if(PendingMissiles.Num()<128 || PendingMissiles.Contains(Id))PendingMissiles.Add(Id,{Time,J});
            return;
        }
        if((*Found)->Instance!=int32(N(J,TEXT("instance"))))return;
        auto& O=**Found;auto Pose=J->GetObjectField(TEXT("placement"));auto Offset=J->GetObjectField(TEXT("offset"));
        const uint32 CellId=N(Pose,TEXT("cell"));const FVector Goal=AC2::BlockOrigin(CellId>>16)+V(Pose,TEXT("position"))*100;
        const FVector Velocity=V(J,TEXT("velocity"))*100,TargetOffset=V(Offset,TEXT("position"))*100;
        if(!CellId || !AC2Projectile::Valid(Goal,Velocity,TargetOffset))return;
        O.Cell=CellId;O.Goal=Goal;O.MissileVelocity=Velocity;O.MissileTarget=S(J,TEXT("target"));O.MissileOffset=TargetOffset;
        O.MissilePoint=AC2::BlockOrigin(uint32(N(Offset,TEXT("cell")))>>16)+O.MissileOffset;
        // TargetPosition::m_offset_pos is a Position, not a vector added to
        // the target's world location. Retain its world point if the target
        // has not streamed in yet, and derive the relative tracking offset.
        O.MissilePointKnown=J->GetBoolField(TEXT("initialized")) && N(Offset,TEXT("cell"))!=0;
        O.MissileOffsetRelative=!O.MissilePointKnown;
        O.Missile=true;O.MissileFinished=false;O.MissileAge=0;O.Solid=false;O.GroundedBody=false;
        O.Parent.Reset();O.Holding=0;O.Placed=true;O.PresentationMotion={};
        if(!O.Actor.IsValid() && O.CachedVisual)ApplyVisual(O,O.CachedVisual,false);
        if(O.Body.IsValid())O.Body->SetRelativeLocation(FVector::ZeroVector);
        if(Smoke)++SmokeMissiles;
        if(O.Actor.IsValid()){O.Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);O.Actor->SetActorLocation(O.Goal);O.Actor->SetActorEnableCollision(false);}return;
    }
    if(Event==TEXT("fx_script"))
    {
        auto Found=Objects.Find(Id);if(!Found || (*Found)->Instance!=int32(N(J,TEXT("instance"))))return;
        if(auto* Player=Id==PlayerId?Pawn(Mode):nullptr)Player->PlayEffectScript(N(J,TEXT("script")));
        else if((*Found)->Model)(*Found)->Model->PlayEffectScript(N(J,TEXT("script")));
        return;
    }
    if(Event==TEXT("fx") || Event==TEXT("stop_fx"))
    {
        // Private FX has no explicit IID: retail resolves it to the current player.
        if(Id.IsEmpty())Id=PlayerId;
        auto Found=Objects.Find(Id);if(!Found || (J->HasField(TEXT("instance")) && (*Found)->Instance!=int32(N(J,TEXT("instance")))))return;
        auto Player=Id==PlayerId?Pawn(Mode):nullptr;const uint32 Fx=N(J,TEXT("fx"));
        if (Id == PlayerId && Fx == EnterWorldEffect)
        {
            ServerArrivalEffectStrength = Event == TEXT("stop_fx") ? 0.f : float(N(J,TEXT("strength"),1));
            if (RevealingArrivalEffect && ServerArrivalEffectStrength <= 0) return;
        }
        if(Fx==0x41000002)(*Found)->DamageFX=Event==TEXT("stop_fx")?0:float(N(J,TEXT("strength"),1));
        if(Event==TEXT("stop_fx"))
        {
            if(Player)Player->StopEffect(Fx);else if((*Found)->Model)(*Found)->Model->StopEffect(Fx);
            for(auto& Entry:Objects)if(Entry.Value->Parent==Id && Entry.Value->Model)Entry.Value->Model->StopEffect(Fx);
            return;
        }
        USceneComponent* Target=nullptr;const FString TargetIdValue=S(J,TEXT("target"));
        if (Fx == MenhirActivateEffect && N(J,TEXT("strength"),1) > 0)
            ActivateShard(Id, TargetIdValue);
        if(TargetIdValue==PlayerId){if(auto P=Pawn(Mode))Target=P->GetRootComponent();}
        else if(auto T=Objects.Find(TargetIdValue))if((*T)->Actor.IsValid())Target=(*T)->Actor->GetRootComponent();
        bool Played=Player?Player->PlayEffect(Fx,N(J,TEXT("strength"),1),Target):(*Found)->Model && (*Found)->Model->PlayEffect(Fx,N(J,TEXT("strength"),1),Target);
        if(!Played)for(auto& Entry:Objects)if(Entry.Value->Parent==Id && Entry.Value->Model)
            Played|=Entry.Value->Model->PlayEffect(Fx,N(J,TEXT("strength"),1),Target);
        if(Smoke)UE_LOG(LogTemp,Display,TEXT("AC2 server FX %08X played=%d player=%d"),Fx,Played,Player!=nullptr);
        if(Played)++PlayedEffects;return;
    }
    if(Event==TEXT("craft_registry")||Event==TEXT("craft_skills"))
    {
        if(Event==TEXT("craft_registry")){
            if(J->GetBoolField(TEXT("replace"))){CraftRecipes.Reset();CraftCatalog.Reset();CraftSkills.Reset();CraftProgress.Reset();}
            const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
            if(J->TryGetArrayField(TEXT("recipes"),List))for(const auto& V:*List){const uint32 Recipe=V->AsNumber();if(Recipe>>24==0x6f)CraftRecipes.AddUnique(Recipe);}
            if(J->TryGetArrayField(TEXT("catalog"),List))for(const auto& V:*List)if(auto R=V->AsObject())CraftCatalog.Add(N(R,TEXT("id")),R);
            if(J->TryGetArrayField(TEXT("professions"),List))for(const auto& V:*List)if(auto R=V->AsObject())CraftSkills.Add(N(R,TEXT("id")),R);
            if(J->TryGetArrayField(TEXT("success"),List)){CraftSuccess.Reset();for(const auto& V:*List)CraftSuccess.Add(V->AsNumber());}
            if(OpenCraftRecipe&&!CraftRecipes.Contains(OpenCraftRecipe))CloseCraft();
        }
        const TArray<TSharedPtr<FJsonValue>>* Professions=nullptr;
        if(J->TryGetArrayField(TEXT("professions"),Professions))for(const auto& V:*Professions)if(auto R=V->AsObject())CraftSkills.Add(N(R,TEXT("id")),R);
        const TArray<TSharedPtr<FJsonValue>>* Progress=nullptr;
        if(J->TryGetArrayField(TEXT("progress"),Progress))for(const auto& V:*Progress)if(auto R=V->AsObject())CraftProgress.Add(N(R,TEXT("id")),R);
        return;
    }
    if(Event==TEXT("craft_ingredient"))
    {
        const int32 Index=int32(N(J,TEXT("ordinal")))-1;
        if(uint32(N(J,TEXT("recipe")))==OpenCraftRecipe && uint32(N(J,TEXT("spinner")))==CraftSpinner && Index>=0&&Index<16&&CraftIngredients[Index]==S(J,TEXT("item"))){
            CraftAccepted[Index]=N(J,TEXT("error"))==0;
            if(!CraftAccepted[Index])CraftNotice=FString::Printf(TEXT("Ingredient rejected (%d). Choose another item."),int32(N(J,TEXT("error"))));}
        return;
    }
    if(Event==TEXT("craft_complete"))
    {
        if(uint32(N(J,TEXT("recipe")))==OpenCraftRecipe && N(J,TEXT("notify"))!=0){
            CraftPending=false;const bool Success=N(J,TEXT("error"))==0;
            for(int32 I=0;I<16;++I){CraftIngredients[I].Reset();CraftAccepted[I]=false;}
            CraftNotice=Success?TEXT("Crafted! Item in backpack."):TEXT("Craft failed; see chat.");
        }
        return;
    }
    if(Event==TEXT("shop"))
    {
        const TArray<TSharedPtr<FJsonValue>>* Offers=nullptr;
        if(!J->TryGetArrayField(TEXT("offers"),Offers) || Offers->Num()>512)return;
        ShopVendor=S(J,TEXT("vendor"));ShopOffers.Reset();LastShopRefresh=Time;
        for(const auto& Value:*Offers)if(auto O=Value->AsObject())
        {
            FShopOffer Offer;Offer.Id=S(O,TEXT("id"));Offer.Entity=N(O,TEXT("entity"));Offer.Icon=N(O,TEXT("icon"));
            Offer.Currency=N(O,TEXT("currency"));Offer.Price=N(O,TEXT("price"));Offer.Stock=N(O,TEXT("stock"));
            Offer.Name=StringInfo(O->GetObjectField(TEXT("name")));
            if(!Offer.Id.IsEmpty() && FMath::IsFinite(Offer.Price) && Offer.Price>=0)ShopOffers.Add(Offer);
        }
        if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))PC->SetCursor(true);
        return;
    }
    if(Event==TEXT("shop_close")) {if(ShopVendor==S(J,TEXT("vendor"))){ShopVendor.Reset();ShopOffers.Reset();}return;}
    if(Event==TEXT("shop_stock")) {if(ShopVendor==S(J,TEXT("vendor")))for(auto& O:ShopOffers)if(O.Id==Id)O.Stock=N(J,TEXT("stock"));return;}
    if(Event==TEXT("release_behavior") || Event==TEXT("stop_layer"))
    {
        auto* Found=Objects.Find(Id);
        if(!Found || (*Found)->Instance!=int32(N(J,TEXT("instance"))))return;
        auto* Player=Id==PlayerId?Pawn(Mode):nullptr;
        if(Event==TEXT("release_behavior")){
            const uint32 Behavior=N(J,TEXT("behavior"));
            if(Player)Player->ReleaseBehavior(Behavior);else if((*Found)->Model)(*Found)->Model->ReleaseBehavior(Behavior);
        }else{
            const uint32 Layer=N(J,TEXT("layer"));
            if(Player)Player->StopAnimationLayer(Layer);else if((*Found)->Model)(*Found)->Model->StopAnimationLayer(Layer);
            StoryMissiles.RemoveAll([&](const auto& Pending){
                if(Pending.Attacker!=Id || !Pending.Animation)return false;
                const uint64 Active=Player?Player->BehaviorInstance(Pending.Behavior):
                    (*Found)->Model?(*Found)->Model->BehaviorInstance(Pending.Behavior):0;
                return Active!=Pending.Animation;
            });
        }
        return;
    }
    if(Event==TEXT("behavior")||Event==TEXT("mode")||Event==TEXT("stop_behavior")||Event==TEXT("hit")||Event==TEXT("impulse")||Event==TEXT("motion"))
    {
        auto* Found=Objects.Find(Id);
        if(!Found || (J->HasField(TEXT("instance")) && int32(N(J,TEXT("instance")))!=(*Found)->Instance))return;
        auto& Obj=**Found;
        auto* Player=Id==PlayerId?Pawn(Mode):nullptr;
        const uint32 B=uint32(N(J,TEXT("behavior"))), M=uint32(N(J,TEXT("mode")));
        if(Event==TEXT("mode") && (M==0x41000002 || M==0x41000003))
        {
            const bool HadState=Obj.OpenableStateKnown,WasOpen=Obj.OpenableOpen;
            Obj.OpenableStateKnown=true;Obj.OpenableOpen=M==0x41000002;
            Obj.DoorOpen=Obj.Door && Obj.OpenableOpen;
            if(Obj.Model)
            {
                if(HadState && WasOpen!=Obj.OpenableOpen)
                    Obj.Model->StopBehavior(WasOpen?0x41000008:0x41000007);
                Obj.Model->SetAnimationState(AC2Motion::Ground,M);
                if(HadState && WasOpen!=Obj.OpenableOpen)Obj.Model->PlayBehavior(Obj.OpenableOpen?0x41000008:0x41000007,M,1,false);
            }
            if(Obj.Actor.IsValid())Obj.Actor->SetActorEnableCollision(!Obj.DoorOpen && Obj.Solid);
        }
        if(Event==TEXT("behavior") && B==AC2Motion::Death && !Obj.Dead)
        {
            InvalidateExamination(Id);
            Obj.Dead=true;Obj.DeathMode=M?M:Obj.AnimMode;
            Obj.DeathRate=FMath::Clamp(float(N(J,TEXT("rate"),1)),.05f,10.f);Obj.DeathStarted=Time;
        }
        if(M){Obj.AnimMode=M;if(Player)Player->SetNativeMode(M);}
        if(Obj.Dead && !Player)Obj.AnimMode=AC2Motion::Dead;
        if(Id==PlayerId && Event==TEXT("mode"))
        {
            if(M==AC2Motion::Peace || M==AC2Motion::Dead)Combat=false;
            else if((M>=0x4100000a && M<=0x41000011) || M==0x41000015)Combat=true;
        }
        if(Obj.Model)Obj.Model->SetAnimationState(Obj.Element,AC2Locomotion::AnimationMode(Obj.Element,Obj.AnimMode));
        if(Event==TEXT("behavior"))
        {
            if(Obj.Dead && !Player && B!=AC2Motion::Death)return;
            if(Id==PlayerId && B==AC2Motion::Pickup)
            {
                LastPickupAnimation=Time;
                if(Time-PickupFallbackTime<2)return; // Same confirmed reach, already playing.
            }
            const bool IsAttack=B==AC2Motion::Attack||B==AC2Motion::MeleeNormal||B==0x410000b0||B==0x410000b9||B==0x410000bc||B==0x410000e2;
            bool Hold=false;J->TryGetBoolField(TEXT("hold"),Hold);Hold|=B==AC2Motion::Death;
            const bool WaterAllows=Obj.Element!=AC2Motion::Water || B==AC2Motion::Death || B==AC2Motion::Hit;
            const uint32 BehaviorMode=B==AC2Motion::Death?Obj.DeathMode:M?M:Obj.AnimMode;
            const bool Played=Player?Player->PlayBehavior(B,M,float(N(J,TEXT("rate"),1)),Hold):Obj.Model&&WaterAllows&&Obj.Model->PlayBehavior(B,AC2Locomotion::AnimationMode(Obj.Element,BehaviorMode),float(N(J,TEXT("rate"),1)),Hold);
            double StoryRequest=0;
            if(Id==PlayerId && J->TryGetNumberField(TEXT("story_request"),StoryRequest))
            {
                StartSkillStory(uint32(StoryRequest),B,Played && Player?Player->BehaviorInstance(B):0);
                ActiveSkillHoldCycles=uint32(FMath::Clamp(N(J,TEXT("hold_cycles"),1),0.,double(MAX_uint32)));
            }
            bool Propagate=false;J->TryGetBoolField(TEXT("propagate"),Propagate);
            if(Propagate)for(auto& Entry:Objects)
                if(Entry.Value->Parent==Id && Entry.Value->Model)
                    Entry.Value->Model->PlayBehavior(B,M?M:Obj.AnimMode,float(N(J,TEXT("rate"),1)),Hold);
            if(Played)
            {
                ++PlayedBehaviors;if(B==AC2Motion::Death)++DeathBehaviors;
                if(IsAttack){if(Player)++PlayerAttacks;else ++MobAttacks;}
                if(Player && (B==0x410000b0||B==0x410000b9||B==0x410000bc)) {++MagicAttacks;if(Smoke)FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-cast.png"),true,false);}
            }
            else if(IsAttack)++FailedAttacks;
            if(Smoke)UE_LOG(LogTemp,Display,TEXT("AC2 server behavior %08X mode=%08X played=%d player=%d"),B,M,Played,Player!=nullptr);
        }
        if(Event==TEXT("stop_behavior")){
            StoryMissiles.RemoveAll([&](const auto& Pending){return Pending.Attacker==Id && (!B || Pending.Behavior==B);});
            if(Player)Player->StopBehavior(B);
            else if(Obj.Model){
                Obj.Model->StopBehavior(B);
                // StopAll cancels actions, but a dead object must retain the
                // last death pose until a new instance replaces it.
                if(Obj.Dead && (!B || B==AC2Motion::Death) && Obj.Model->PlayBehavior(AC2Motion::Death,Obj.DeathMode,Obj.DeathRate,true))
                    Obj.Model->SeekBehavior(AC2Motion::Death,FMath::Max(0.,Time-Obj.DeathStarted));
            }
        }
        if(Event==TEXT("hit"))
        {
            const int64 Damage=int64(N(J,TEXT("damage")));
            if(Damage>0 && Damage<=MAX_int32)
            {
                const double Now=Mode->GetWorld()->GetTimeSeconds();
                DamageTexts.RemoveAll([&](const auto& Text){return Now-Text.Born>=2.;});
                if(DamageTexts.Num()>=128)DamageTexts.RemoveAt(0);
                DamageTexts.Add({Id,S(J,TEXT("attacker")),Damage,Now});
                Mode->DamageBlood(Player?static_cast<AActor*>(Player):Obj.Actor.Get());
            }
            const bool Played=Player&&!Dead?Player->PlayBehavior(AC2Motion::Hit):Obj.Model&&Obj.AnimMode!=AC2Motion::Dead&&Obj.Model->PlayBehavior(AC2Motion::Hit);
            if(Played)++HitEvents;
        }
        if(Event==TEXT("motion")&&Player)Player->SetMotionParameter(S(J,TEXT("parameter")),N(J,TEXT("value")));
        if(Event==TEXT("impulse")&&Player)Player->GetCharacterMovement()->AddImpulse(V(J,TEXT("direction"))*100,true);
        return;
    }
    if (Event == TEXT("data_updated"))
    {
        FString Directory=S(J,TEXT("directory")),Error;
        FPaths::NormalizeDirectoryName(Directory);
        if(!HandshakeSettings.IsSet() || DataCacheRoot.IsEmpty() ||
           !FPaths::IsUnderDirectory(Directory,DataCacheRoot) || !FAC2LoginProfiles::ValidateData(Directory,Error))
        {HandshakeSettings.Reset();SetStatus(TEXT("disconnected"),TEXT("The updated game-data folder could not be verified."));return;}
        auto Settings=MoveTemp(HandshakeSettings.GetValue());HandshakeSettings.Reset();
        if(!Settings.ProfileFile.IsEmpty() && !Settings.ServerProfileId.IsEmpty()){
            FAC2LoginProfiles Profiles;
            if(Profiles.Load(Settings.ProfileFile,Error)){
                if(auto* Server=Profiles.Servers.FindByPredicate([&](const auto& P){return P.Id==Settings.ServerProfileId;})){
                    if(Server->Icefire && Server->Host.Equals(Settings.Host,ESearchCase::IgnoreCase) && Server->Port==Settings.Port &&
                       FPaths::IsSamePath(Server->DatDirectory,Mode->DatDirectory)){
                        Server->DatDirectory=Directory;
                        if(!Profiles.Save(Error))UE_LOG(LogTemp,Warning,TEXT("Could not save updated server data profile: %s"),*Error);
                    }
                }
            }
        }
        Settings.DatDirectory=Directory;DataReload=MoveTemp(Settings);
        SetStatus(TEXT("updating_data"),TEXT("Server game data updated. Reloading and reconnecting..."));
        UE_LOG(LogTemp,Display,TEXT("AC2 DAT update verified; scheduling data reload"));
        return;
    }
    if (Event == TEXT("status"))
    {
        SetStatus(S(J, TEXT("phase")), S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("disconnected"))
    {
        HandshakeSettings.Reset();
        SetStatus(TEXT("disconnected"), S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("error") || Event == TEXT("character_error"))
    {
        const FString Localized=StringInfo(O(J,TEXT("text")));
        // A profile/decoder warning is not a rejected character entry. In
        // particular, inventory can arrive before the player's world create.
        SetStatus(Event==TEXT("character_error") && !HasPlayerPose ? TEXT("selection") : Phase,
                  !Localized.IsEmpty()?Localized:S(J, TEXT("message")).IsEmpty()
                      ? FString::Printf(TEXT("Server character error %d"), int32(N(J, TEXT("code"))))
                      : S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("characters"))
    {
        HandshakeSettings.Reset();
        AllowedCharacters=FMath::Clamp(int32(N(J,TEXT("allowed"))),1,32);
        ClearWorld();
        Mode->Characters = FAC2CharacterStore();
        CharacterVisuals.Reset();
        const TArray<TSharedPtr<FJsonValue>> *List;
        if (J->TryGetArrayField(TEXT("characters"), List))
            for (auto &E : *List)
                AddCharacter(E->AsObject());
        if (CharacterVisuals.Contains(LastCharacterId))
            Mode->Characters.ActiveId = LastCharacterId;
        SetStatus(TEXT("selection"), TEXT("Select or create a server character"));
        if (auto H = HUD(Mode))
        {
            H->InitializeCharacterUI();
            if (H->CharacterUI)
                H->CharacterUI->Open(Mode->Characters.Characters.IsEmpty()
                                         ? SAC2CharacterUI::EScreen::Create
                                         : SAC2CharacterUI::EScreen::Select);
        }
        return;
    }
    if (Event == TEXT("created"))
    {
        auto C = O(J, TEXT("character"));
        if (C && !S(C, TEXT("id")).IsEmpty() && S(C, TEXT("id")) != TEXT("0"))
        {
            AddCharacter(C);
            Phase = TEXT("selection");
            Enter(S(C, TEXT("id")));
        }
        else {
            SetStatus(TEXT("selection"), FString::Printf(TEXT("Character creation rejected (%d)"),
                                                         int32(N(J, TEXT("response")))));
            if(Smoke){UE_LOG(LogTemp,Error,TEXT("AC2 network smoke: server rejected creation"));FPlatformMisc::RequestExitWithStatus(false,1);}
        }
        return;
    }
    if (Event == TEXT("deleted"))
    {
        if (Id == LastCharacterId)
            SaveLastCharacter(FString());
        Mode->Characters.Characters.RemoveAll([&](const auto &C){return C.Id==Id;});
        CharacterVisuals.Remove(Id);
        if (Mode->Characters.ActiveId==Id) Mode->Characters.ActiveId=Mode->Characters.Characters.IsEmpty()?FString():Mode->Characters.Characters[0].Id;
        SetStatus(TEXT("selection"),TEXT("Character deleted"));
        if (auto H=HUD(Mode);H && H->CharacterUI) H->CharacterUI->Open(SAC2CharacterUI::EScreen::Select);
        return;
    }
    if (Event == TEXT("player"))
    {
        PlayerId = Id;
        Mode->Characters.ActiveId = Id;
        return;
    }
    if (Event == TEXT("selection"))
    {
        Mode->LeaveWorld();
        ClearWorld();
        SetStatus(TEXT("selection"), TEXT("Select a server character"));
        if (auto H = HUD(Mode))
            if (H->CharacterUI)
                H->CharacterUI->Open(SAC2CharacterUI::EScreen::Select);
        return;
    }
    if (Event == TEXT("chat"))
    {
        auto Text = StringInfo(O(J, TEXT("text"))), Speaker = StringInfo(O(J, TEXT("speaker")));
        // Capabilities are wire literals, not localized display text. Native
        // StringInfo rendering strips bracketed grammatical metadata.
        if (!J->HasField(TEXT("speaker")) && !J->HasField(TEXT("channel")) &&
            S(O(J,TEXT("text")),TEXT("literal")) == TEXT("[AC2E:map-travel-v1]"))
        {
            MapTravelEnabled = true;
            return;
        }
        if(Speaker.IsEmpty() && Text.StartsWith(TEXT("Server Error:"))){
            UE_LOG(LogTemp,Warning,TEXT("AC2 server interaction error: %s [target=%s examine=%s]"),
                *Text.Left(512),*TargetId,*ExaminationId);
            for(const FString& Action:RecentActions)UE_LOG(LogTemp,Warning,TEXT("AC2 preceding action: %s"),*Action);
        }
        const uint32 Kind=J->HasField(TEXT("kind"))?uint32(N(J,TEXT("kind"))):0x10;
        const FString Form=S(J,TEXT("format"));
        if(Form==TEXT("tell_sent"))Text=FString::Printf(TEXT("You tell %s, \"%s\""),*Speaker,*Text);
        else if(Form==TEXT("tell"))Text=FString::Printf(TEXT("%s tells you, \"%s\""),*Speaker,*Text);
        else if(Form==TEXT("speech"))Text=S(J,TEXT("speaker_id"))==PlayerId
            ?FString::Printf(TEXT("You say, \"%s\""),*Text)
            :FString::Printf(TEXT("%s says, \"%s\""),*Speaker,*Text);
        else if (!Speaker.IsEmpty())Text = Speaker + TEXT(": ") + Text;
        const FString Channel = S(J, TEXT("channel"));
        if (!Channel.IsEmpty())
            Text = TEXT("[") + Channel + TEXT("] ") + Text;
        if (auto H = HUD(Mode))
            if (H->GameUI)
                H->GameUI->ReceiveNetworkChat(Text,Kind);
        if (Text.Contains(TEXT("AC2 network smoke")))
            ++ReceivedChats;
        return;
    }
    if (Event == TEXT("death"))
    {
        Dead = N(J, TEXT("dead")) != 0;
        if (auto P = Pawn(Mode))
            P->SetDead(Dead);
        if (Dead)
        {
            FinishSkillStory();PendingSkills.Reset();ToggledSkills.Reset();
            Mode->Notice = TEXT("You have fallen. Use /resurrect to request resurrection.");
        }
        return;
    }
    if(Event==TEXT("skills"))
    {
        if(J->GetBoolField(TEXT("replace")))Skills.Reset();
        if(J->HasField(TEXT("credits")))SkillCredits=int32(N(J,TEXT("credits")));
        if(J->HasField(TEXT("untraining")))UntrainingSkill=N(J,TEXT("untraining"));
        const TArray<TSharedPtr<FJsonValue>>* List;
        if(J->TryGetArrayField(TEXT("items"),List))for(const auto& V:*List){const auto A=V->AsObject();if(!A)continue;
            const uint32 Key=N(A,TEXT("id"));Skills.RemoveAll([&](const auto& E){return E.Id==Key;});
            double Reset=-1;A->TryGetNumberField(TEXT("reset_started"),Reset);
            if(!FMath::IsFinite(Reset) || Reset<0 || Reset>1.e12)Reset=-1;
            Skills.Add({Key,A->GetBoolField(TEXT("trained")),S(A,TEXT("xp")),Reset});}
        Skills.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});return;
    }
    if(Event==TEXT("skill_remove")){const uint32 Key=N(J,TEXT("id"));Skills.RemoveAll([&](const auto& E){return E.Id==Key;});return;}
    if(Event==TEXT("quests"))
    {
        const bool Replace=J->GetBoolField(TEXT("replace"));
        if(Replace)Quests.Reset();
        const TArray<TSharedPtr<FJsonValue>>* List;
        if(J->TryGetArrayField(TEXT("items"),List))for(const auto& V:*List){const auto A=V->AsObject();if(!A)continue;
            FQuest Q;Q.Id=N(A,TEXT("id"));Q.Icon=N(A,TEXT("icon"));Q.Status=N(A,TEXT("status"));Q.Phase=N(A,TEXT("phase"));
            Q.Name=StringInfo(O(A,TEXT("name")));Q.Description=StringInfo(O(A,TEXT("description")));Q.Journal=StringInfo(O(A,TEXT("journal")));
            Q.Level=int32(N(A,TEXT("level")));Q.Bestowal=N(A,TEXT("bestowal"));Q.Received=FPlatformTime::Seconds();
            Q.Remaining=FMath::Max(0.,N(A,TEXT("remaining")));Q.Retry=FMath::Max(0.,N(A,TEXT("retry")));
            A->TryGetBoolField(TEXT("expires"),Q.Expires);A->TryGetBoolField(TEXT("maxed_out"),Q.MaxedOut);
            if(!Replace)A->TryGetBoolField(TEXT("changed"),Q.Changed);
            const auto* Previous=Quests.FindByPredicate([&](const auto& E){return E.Id==Q.Id;});
            if(Q.Changed && (!Previous || Previous->Status!=Q.Status || Previous->Phase!=Q.Phase || Previous->Journal!=Q.Journal || Previous->Bestowal!=Q.Bestowal))
            {
                // CQuestSystem::UpdateQuest_Done uses the update reason, not
                // the journal's status (an advancing quest remains underway).
                uint32 Update=N(J,TEXT("update"),Q.Status),Effect=0;
                if(Update==0x10000000 && Previous && Previous->Bestowal==Q.Bestowal && Previous->Status==Q.Status)Update=0x20000000;
                switch(Update){case 0x10000000:Effect=0x410004de;break;case 0x20000000:Effect=0x410004e0;break;
                    case 0x30000000:Effect=0x410004e1;break;case 0x40000000:Effect=0x410004df;break;}
                if(Effect)if(auto P=Pawn(Mode))P->PlayEffect(Effect);
            }
            Quests.RemoveAll([&](const auto& E){return E.Id==Q.Id;});Quests.Add(MoveTemp(Q));}
        Quests.Sort([](const FQuest& A,const FQuest& B){return A.Bestowal==B.Bestowal?A.Id<B.Id:A.Bestowal>B.Bestowal;});
        return;
    }
    if(Event==TEXT("container_state")){
        if(N(J,TEXT("status"))!=0){Mode->Notice=TEXT("The container could not be opened or closed.");return;}
        if(J->GetBoolField(TEXT("open"))){LootContainer=Id;LootItems.Reset();
            const TArray<TSharedPtr<FJsonValue>>* List;
            if(J->TryGetArrayField(TEXT("items"),List))for(const auto& Item:*List)LootItems.Add(Item->AsString());
        }else if(LootContainer==Id){LootContainer.Reset();LootItems.Reset();}
        return;
    }
    if (Event == TEXT("inventory") || Event == TEXT("bag") || Event == TEXT("equipment"))
    {
        const TArray<TSharedPtr<FJsonValue>> *OwnedBag,*OwnedEquipment;
        if(Event==TEXT("inventory") && J->TryGetArrayField(TEXT("items"),OwnedBag) && J->TryGetArrayField(TEXT("equipment"),OwnedEquipment)){
            InventoryOwnership.Reset();
            for(const auto& Item:*OwnedBag)InventoryOwnership.Add(Item->AsString());
            for(const auto& Item:*OwnedEquipment)InventoryOwnership.Add(S(Item->AsObject(),TEXT("id")));
            InventoryKnown=true;
        }
        const TArray<TSharedPtr<FJsonValue>> *List;
        if (Event == TEXT("inventory"))
            Gold = N(J, TEXT("gold"));
        if(Event==TEXT("bag") && !Id.IsEmpty() && Id!=PlayerId && J->TryGetArrayField(TEXT("items"),List))
        {
            if(LootContainer==Id){LootItems.Reset();for(const auto& Item:*List)LootItems.Add(Item->AsString());}return;
        }
        if ((Event == TEXT("inventory") || Id.IsEmpty() || Id == PlayerId) &&
            J->TryGetArrayField(TEXT("items"), List))
        {
            BagSlots.Reset();
            for (int32 I = 0; I < List->Num(); ++I)
                BagSlots.Add((*List)[I]->AsString(), I);
        }
        if (J->TryGetArrayField(Event == TEXT("equipment") ? TEXT("slots") : TEXT("equipment"), List))
        {
            WornSlots.Reset();
            for (auto &E : *List)
            {
                auto A = E->AsObject();
                WornSlots.Add(S(A, TEXT("id")), uint32(N(A, TEXT("slot"))));
            }
        }
        InventoryOwnership.Reset();
        for(const auto& Item:BagSlots)InventoryOwnership.Add(Item.Key);
        for(const auto& Item:WornSlots)InventoryOwnership.Add(Item.Key);
        if(auto* P=Mode->Characters.Active())
            P->Items.RemoveAll([&](const auto& I){return !InventoryOwnership.Contains(I.NetworkId);});
        SyncInventoryItems();
        InvalidateExamination(PlayerId);
        if(InventoryOwnership.Contains(ExaminationId))InvalidateExamination(ExaminationId);
        return;
    }
    if(Event==TEXT("inventory_acquired"))
    {
        if(N(J,TEXT("status"))!=0)
        {
            PendingPickupSounds.Remove(Id);PendingPickupSounds.Remove(S(J,TEXT("source_item")));
            Mode->Notice=FString::Printf(TEXT("Server rejected inventory change (%d)"),int32(N(J,TEXT("status"))));
            return;
        }
        InvalidateExamination(Id);InvalidateExamination(PlayerId);
        if(!PlayerId.IsEmpty() && S(J,TEXT("owner"))==PlayerId && !Id.IsEmpty() && Id!=TEXT("0"))
        {
            const int32 Slot=int32(N(J,TEXT("slot"),-1));
            if(Slot>=0){InventoryOwnership.Add(Id);InsertBagSlot(Id,Slot);WornSlots.Remove(Id);}
            LootItems.Remove(Id);
            SyncInventoryItems();
        }
        if(!PlayerId.IsEmpty() && S(J,TEXT("from"))==PlayerId && J->HasField(TEXT("owner")) &&
            S(J,TEXT("owner"))!=PlayerId && !Id.IsEmpty() && Id==S(J,TEXT("source_item")))
        {
            // A successful whole-stack give/drop transfers ownership away.
            // Split transfers leave the original stack for quantity updates;
            // a later RemoveItem notice is harmless and must not shift twice.
            auto Removed=MakeShared<FJsonObject>();Removed->SetStringField(TEXT("event"),TEXT("inventory_remove"));
            Removed->SetStringField(TEXT("id"),Id);Handle(Removed);
        }
        // Only confirmed changes of ownership make a pickup cue. Initial bag
        // snapshots, rearranging our bag and another player's grants stay silent.
        if(N(J,TEXT("status"))==0 && !PlayerId.IsEmpty() && S(J,TEXT("owner"))==PlayerId &&
            S(J,TEXT("from"))!=PlayerId && !Id.IsEmpty() && Id!=TEXT("0") && !AcquisitionSounds.Contains(Id))
        {
            const bool Requested=PendingPickupSounds.Remove(Id)>0;
            const bool SplitRequested=PendingPickupSounds.Remove(S(J,TEXT("source_item")))>0;
            ConfirmPickup(Id,Requested || SplitRequested);
        }
        return;
    }
    if(Event==TEXT("inventory_remove"))
    {
        InvalidateExamination(Id);InvalidateExamination(PlayerId);
        AcquisitionSounds.Remove(Id);
        InventoryOwnership.Remove(Id);
        RemoveBagSlot(Id);
        WornSlots.Remove(Id);
        if(auto P=Mode->Characters.Active())
        {
            P->Items.RemoveAll([&](const auto& I){return I.NetworkId==Id;});
            for(auto& I:P->Items)if(auto* Slot=BagSlots.Find(I.NetworkId))I.Cell=*Slot;
        }
        return;
    }
    if (Event == TEXT("inventory_bulk"))
    {
        if(S(J,TEXT("owner"))!=PlayerId)return;
        const bool Transmute=J->GetBoolField(TEXT("transmute"));
        const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
        if(J->TryGetArrayField(TEXT("items"),Items))for(const auto& Value:*Items)
        {
            const FString Item=Value->AsString();
            const bool Requested=PendingPickupSounds.Remove(Item)>0;
            LootItems.Remove(Item);
            if(!Transmute)ConfirmPickup(Item,Requested);
        }
        if(J->TryGetArrayField(TEXT("remaining"),Items))for(const auto& Value:*Items)
            PendingPickupSounds.Remove(Value->AsString());
        // Contents/equipment notices own bag ordinals. Bulk completion gives
        // successful and rejected IDs, not insertion slots; don't invent them.
        if(N(J,TEXT("status"))!=0)
            Mode->Notice=FString::Printf(TEXT("Server rejected inventory change (%d)"),int32(N(J,TEXT("status"))));
        return;
    }
    if (Event == TEXT("inventory_result"))
    {
        const bool Pickup=PendingPickupSounds.Remove(Id)>0;
        if (N(J, TEXT("status")) != 0)
        {
            Mode->Notice =
                FString::Printf(TEXT("Server rejected inventory change (%d)"), int32(N(J, TEXT("status"))));
            return;
        }
        InvalidateExamination(Id);InvalidateExamination(PlayerId);
        // The custom single-item loot control uses the DAT loot-button cue.
        // Wait for success; failures and duplicate replies must stay silent.
        if(Pickup && J->HasField(TEXT("slot")) && !J->HasField(TEXT("equipped")) && !AcquisitionSounds.Contains(Id))
        {
            ConfirmPickup(Id,true);
        }
        if (J->HasField(TEXT("equipped")))
        {
            const uint32 Location = N(J,TEXT("equipped"));
            if (Location)
            {
                RemoveBagSlot(Id);WornSlots.Add(Id,Location);
            }
            else
            {
                WornSlots.Remove(Id);
                InsertBagSlot(Id,int32(N(J,TEXT("slot"),-1)));
            }
            InventoryOwnership.Add(Id);SyncInventoryItems();
            return;
        }
        if(J->HasField(TEXT("slot")))
        {
            LootItems.Remove(Id);
            InsertBagSlot(Id,int32(N(J,TEXT("slot"),-1)));
            InventoryOwnership.Add(Id);SyncInventoryItems();
        }
        return;
    }
    if(Event==TEXT("quality"))
    {
        // Both PlayerDesc and private quality events address the local player.
        // Resolve before object-specific name, radar and summoner handling too.
        if(Id.IsEmpty())Id=PlayerId;
        // A respawn may reuse an object ID. Queued qualities from the previous
        // instance must not turn its replacement into a dead/invalid target.
        if(auto* Object=Objects.Find(Id);Object &&
            int32(N(J,TEXT("instance"),(*Object)->Instance))!=(*Object)->Instance)return;
        InvalidateExamination(Id);
        const uint32 Table=N(J,TEXT("table")),Key=N(J,TEXT("key"));
        if(Table==4 && Key==0x2d0){
            const int32 Count=N(J,TEXT("value"));
            if(auto* Object=Objects.Find(Id);Object && Count>=0 && Count<=16)(*Object)->UseTargets=Count;
            return;
        }
        if((Table==1 && Key==0x270)||(Table==4 && Key==0x268)||(Table==12 && Key==0x259)){
            if(auto* Object=Objects.Find(Id);Object && int32(N(J,TEXT("instance"),(*Object)->Instance))==(*Object)->Instance){
                if(Table==1)(*Object)->RadarSelectable=N(J,TEXT("value"))!=0;
                if(Table==4)(*Object)->RadarShape=uint32(N(J,TEXT("value")));
                if(Table==12)(*Object)->RadarColor=uint32(FCString::Strtoui64(*S(J,TEXT("value")),nullptr,10));
                if(Table==1 && !(*Object)->RadarSelectable.Get(true))SelectionUnavailable(Id,false);
            }
            return;
        }
        if(Table==5 && Key==0x1f9){
            if(auto* Object=Objects.Find(Id);Object && int32(N(J,TEXT("instance"),(*Object)->Instance))==(*Object)->Instance)
                (*Object)->Summoner=S(J,TEXT("value"));
            return;
        }
    }
    if(Event==TEXT("quality") && N(J,TEXT("table"))==10 && N(J,TEXT("key"))==0x100)
    {
        if(auto* Object=Objects.Find(Id);Object && int32(N(J,TEXT("instance"),(*Object)->Instance))==(*Object)->Instance)
        {(*Object)->Name=StringInfo(O(J,TEXT("value")));}
        return;
    }
    if(Event==TEXT("quality") && N(J,TEXT("table"))==4 && N(J,TEXT("key"))==0x3b6){
        const int8 Groove=int8(int32(N(J,TEXT("value"))));
        if(Id.IsEmpty() || Id==PlayerId)PlayerGroove=Groove;
        else if(auto* Object=Objects.Find(Id))(*Object)->Groove=Groove;
    }
    if (Event == TEXT("quality") && (Id.IsEmpty() || Id == PlayerId))
    {
        const uint32 TimerTable=N(J,TEXT("table")),TimerKey=N(J,TEXT("key"));
        if(TimerTable==12 && TimerKey==0x13c){
            uint64 Remaining;
            if(LexTryParseString(Remaining,*S(J,TEXT("value"))))VitaeRecoveryRemaining=Remaining;
        }
        double TimerValue=0;
        if(J->TryGetNumberField(TEXT("value"),TimerValue) && FMath::IsFinite(TimerValue)){
            if(TimerTable==3 && TimerKey==0x11e)Vitae=FMath::Clamp(TimerValue,0.,100.);
            if(TimerTable==1 && TimerKey==0xfa0)IsHero=TimerValue!=0;
            if(TimerTable==4)switch(TimerKey) {
                case 0x10a: CurrentFocus=TimerValue;break;
                case 0x10b: MaxFocus=TimerValue;break;
                case 0x10d: PKDamage=TimerValue;break;
                case 0x10e: PKVigorLoss=TimerValue;break;
            }
            if(TimerTable==8 && TimerKey==0x12f)SharedSkillReset.Started=TimerValue>=0 && TimerValue<=1.e12?TimerValue:-1;
            if(TimerTable==3 && TimerKey==0xbb8)SharedSkillReset.Duration=TimerValue>0 && TimerValue<=86400?TimerValue:0;
        }
        if (auto P = Mode->Characters.Active())
        {
            const uint32 Key = N(J, TEXT("key")), Table = N(J, TEXT("table"));
            const float Value = N(J, TEXT("value"));
            if (Table == 4)
            {
                if (Key == 0x102)
                    P->Health = Value;
                else if (Key == 0x107)
                    MaxHealth = Value;
                else if (Key == 0x103)
                    P->Vigor = Value;
                else if (Key == 0x108)
                    MaxVigor = Value;
                else if (Key == 0x12e)
                {
                    P->Level = Value;
                    LevelXpFloor=FCString::Strtoui64(*S(J,TEXT("level_xp_floor")),nullptr,10);
                    LevelXpSpan=FCString::Strtoui64(*S(J,TEXT("level_xp_span")),nullptr,10);
                }
                else if (Key == 0xc1c)
                    Gold = Value;
                else if (Key == 0x7ac20003)
                {
                    QuestPoints = uint32(int64(N(J, TEXT("value"))));
                    InvalidateExamination(PlayerId);
                }
            }
            if (Table==12 && (Key==0x7ac20001 || Key==0x7ac20002))
            {
                uint64 Bound;
                if(LexTryParseString(Bound,*S(J,TEXT("value"))))
                    (Key==0x7ac20001 ? ServerLevelXpFloor : ServerLevelXpCeiling)=Bound;
            }
            if (Table == 12 && Key == 0x12d)AvailableXp=FCString::Strtoui64(*S(J,TEXT("value")),nullptr,10);
            if (Table == 12 && Key == 0x12c)
                P->Experience = FMath::Max<int64>(0,FCString::Atoi64(*S(J,TEXT("value"))));
            if(Table==4 && (Key==0x102 || Key==0x107))
            {
                if(auto Player=Pawn(Mode))Player->SetHealth(P->Health,MaxHealth);
                if(auto Object=Objects.Find(PlayerId)){(*Object)->Health=P->Health;(*Object)->MaxHealth=MaxHealth;(*Object)->DamageFX=-1;}
            }
        }
        return;
    }
    if(Event==TEXT("quality") && N(J,TEXT("table"))==4)
    {
        const uint32 Key=N(J,TEXT("key"));
        if((Key==0x102 || Key==0x107) && Objects.Contains(Id))
        {
            auto& Object=*Objects[Id];
            if(Key==0x102)Object.Health=N(J,TEXT("value"));else Object.MaxHealth=N(J,TEXT("value"));
            Object.DamageFX=-1;
            if(Object.Model && Object.Health>=0)Object.Model->SetHealth(Object.Health,Object.MaxHealth);
        }
        return;
    }
    if(Event==TEXT("parent") || Event==TEXT("deparent"))
    {
        auto* Item=Objects.Find(Id);const FString ParentId=S(J,TEXT("parent"));auto* Parent=Objects.Find(ParentId);
        if(!Item || !Parent || (*Item)->Instance!=int32(N(J,TEXT("instance"))) ||
            (*Parent)->Instance!=int32(N(J,TEXT("parent_instance"))))return;
        auto& Obj=**Item;const uint16 Stamp=uint16(N(J,TEXT("position_stamp")));
        const uint16 Advance=uint16(Stamp-Obj.PositionStamp);
        if(Advance==0 || Advance>0x7fff)return;
        Obj.PositionStamp=Stamp;Obj.Placed=false;
        Obj.Parent=Event==TEXT("parent")?ParentId:FString();
        Obj.Holding=Event==TEXT("parent")?uint32(N(J,TEXT("holding"))):0;
        if(Obj.Parent.IsEmpty())
        {if(Obj.Actor.IsValid())Obj.Actor->Destroy();Obj.Model.Reset();}
        else ApplyVisual(Obj,Obj.CachedVisual,false);
        return;
    }
    if (Event == TEXT("remove") || Event==TEXT("world_leave") || Event==TEXT("contain"))
    {
        if(Event==TEXT("remove"))PendingMissiles.Remove(Id);
        if (auto E = Objects.Find(Id))
        {
            if(J->HasField(TEXT("instance")) && (*E)->Instance!=int32(N(J,TEXT("instance"))))return;
            if(Event!=TEXT("remove"))
            {
                const uint16 Stamp=uint16(N(J,TEXT("position_stamp")));
                const uint16 Advance=uint16(Stamp-(*E)->PositionStamp);
                if(Advance>0x7fff || (Advance==0 && Event==TEXT("world_leave")))return;
                (*E)->PositionStamp=Stamp;
                if(Event==TEXT("contain") || InventoryOwnership.Contains(Id) || LootItems.Contains(Id) || PendingPickupSounds.Contains(Id))
                {
                    // A contained item can leave the 3D world while its inventory
                    // or open corpse still references its visual and examine profile.
                    auto& Obj=**E;if(Obj.Actor.IsValid())Obj.Actor->Destroy();Obj.Model.Reset();
                    Obj.Placed=false;Obj.Parent.Reset();Obj.Holding=0;return;
                }
            }
            if(LootContainer==Id){LootContainer.Reset();LootItems.Reset();}
            LootItems.Remove(Id);
            if ((*E)->Actor.IsValid())
                (*E)->Actor->Destroy();
            Objects.Remove(Id);
            SelectionUnavailable(Id,true);
            if(ExaminationId==Id)
            {
                ExaminationRows.Reset();ExaminationPending=ExaminationQueued=false;
                ExaminationError=TEXT("That object is no longer available to examine.");
            }
        }
        InventoryOwnership.Remove(Id);
        RemoveBagSlot(Id);WornSlots.Remove(Id);
        if (auto P = Mode->Characters.Active())
            P->Items.RemoveAll([&](const FAC2InventoryItem &I) { return I.NetworkId == Id; });
        SyncInventoryItems();
        return;
    }
    if (Event != TEXT("object") && Event != TEXT("position") && Event != TEXT("visual"))
        return;
    if (Id.IsEmpty() || (Objects.Num() >= 2048 && !Objects.Contains(Id)))
        return;
    // Incremental physics/appearance updates cannot create an object. A late
    // update after DestroyObject used to resurrect an entity-less selectable
    // shell and consume the object budget, eventually hiding real creatures.
    if(Event!=TEXT("object") && !Objects.Contains(Id))return;
    if (!Objects.Contains(Id))
        Objects.Add(Id, MakeUnique<FObject>());
    auto &Obj = *Objects[Id];
    if(Event==TEXT("visual") && int32(N(J,TEXT("instance"),Obj.Instance))!=Obj.Instance)return;
    const bool IsPlayer = Id == PlayerId;
    const bool NewInstance=Event==TEXT("object") && Obj.Instance!=int32(N(J,TEXT("instance")));
    if(NewInstance){Obj.Dead=false;Obj.DeathStarted=0;Obj.AnimMode=AC2Motion::Peace;Obj.Element=AC2Motion::Ground;}
    if (Event == TEXT("object"))
    {
        InvalidateExamination(Id);
        Obj.Placed=false;
        Obj.Health=-1;Obj.MaxHealth=0;Obj.DamageFX=-1;
        Obj.RadarShape.Reset();Obj.RadarSelectable.Reset();Obj.RadarColor.Reset();
        // Behavior/mode stamps are instance stamps. Retain the create stamp so
        // valid server actions are not compared against an uninitialized zero.
        Obj.Instance=int32(N(J,TEXT("instance")));
        Obj.PositionStamp=uint16(N(J,TEXT("position_stamp")));
        Obj.TeleportStamp=uint16(N(J,TEXT("teleport")));
        Obj.ForceStamp=uint16(N(J,TEXT("force")));
        Obj.Door=Obj.DoorOpen=false;
        Obj.OpenableStateKnown=Obj.OpenableOpen=false;
        Obj.Missile=Obj.MissileFinished=Obj.MissilePointKnown=false;Obj.MissileAge=0;
        Obj.Parent=S(J,TEXT("parent")); Obj.Holding=uint32(N(J,TEXT("holding")));
        Obj.Name = StringInfo(O(J, TEXT("name")));
        Obj.Entity = N(J, TEXT("entity"));
        Obj.Quantity=FMath::Max(1,int32(N(J,TEXT("quantity"),1)));
        Obj.CachedVisual=O(J,TEXT("visual"));
        Obj.Summoner=S(J,TEXT("summoner"));
        bool Selectable=SelectionLook(Obj.Entity).Selectable;
        J->TryGetBoolField(TEXT("selectable"),Selectable);Obj.RadarSelectable=Selectable;
        if(Obj.Entity)
        {
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Obj.Entity);
            if(const auto Definition=Mode->CharacterCatalog.Items.Find(Obj.Entity))
                Obj.Door=(Definition->NativeClass>=3756 && Definition->NativeClass<=3764) || Definition->NativeClass==3922;
        }
        const uint64 Namespace=FCString::Strtoui64(*Id,nullptr,10)>>48;
        Obj.Content=Namespace==0x8140 || Namespace==0x8180;
        Obj.GroundItem=Namespace==0x8180;
        Obj.EntityScale = FMath::Clamp(float(N(J, TEXT("scale"), 1)), .01f, 100.f);
        if (Obj.Name.IsEmpty() && Obj.Entity)
        {
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory, Obj.Entity);
            if (auto Definition = Mode->CharacterCatalog.Items.Find(Obj.Entity))
                Obj.Name = Definition->Name;
        }
    }
    auto Pose = Event == TEXT("object") ? O(J, TEXT("placement")) : (Event == TEXT("position") ? J : nullptr);
    if (Event == TEXT("object") && !PendingMissiles.Contains(Id) && !PlayerId.IsEmpty() &&
        ((!Pose && InventoryOwnership.Contains(Id)) || S(J, TEXT("container")) == PlayerId || S(J, TEXT("wielder")) == PlayerId))
    {
        InventoryOwnership.Add(Id);
        SyncInventoryItems();
        if(Obj.Parent.IsEmpty()){ApplyVisual(Obj,O(J,TEXT("visual")),false);return;}
    }
    if(Event==TEXT("position") && int32(N(J,TEXT("instance"),Obj.Instance))!=Obj.Instance)return;
    bool Teleported=false, Forced=false, Impulse=false;
    if(Event==TEXT("position") && (!IsPlayer || HasPlayerPose))
    {
        // CPhysicsObj::RecvEvt_Position stops interpolation for observers on a
        // new teleport stamp. Ordinary position echoes only move a remote body.
        auto Newer=[](uint16 Old,uint16 Next){const uint16 Advance=Next-Old;return Advance!=0 && Advance<=0x7fff;};
        const uint16 Stamp=uint16(N(J,TEXT("position_stamp"),Obj.PositionStamp));
        if(J->HasField(TEXT("position_stamp")) && !Newer(Obj.PositionStamp,Stamp))return;
        Obj.PositionStamp=Stamp;
        const uint16 Teleport=uint16(N(J,TEXT("teleport"),Obj.TeleportStamp));
        Teleported=Newer(Obj.TeleportStamp,Teleport);
        if(Teleported)Obj.TeleportStamp=Teleport;
        if(IsPlayer)
        {
            const uint16 Force=uint16(N(J,TEXT("force"),Obj.ForceStamp));
            Forced=Newer(Obj.ForceStamp,Force);
            if(Forced)Obj.ForceStamp=Force;
            J->TryGetBoolField(TEXT("impulse"),Impulse);
            if(!Teleported && !Forced && !Impulse)return;
        }
    }
    const bool RestoreWorldVisual=Pose && (!Obj.Placed || !Obj.Parent.IsEmpty());
    if (Pose)
    {
        // A world placement is authoritative even if the bag removal arrives
        // later. Reuse the cached inventory visual and detach held items.
        if(!IsPlayer && Event==TEXT("position")){Obj.Parent.Reset();Obj.Holding=0;}
        if(!IsPlayer && Obj.Parent.IsEmpty() && InventoryOwnership.Contains(Id))
        {
            auto Removed=MakeShared<FJsonObject>();Removed->SetStringField(TEXT("event"),TEXT("inventory_remove"));
            Removed->SetStringField(TEXT("id"),Id);Handle(Removed);
        }
        Obj.Cell = uint32(N(Pose, TEXT("cell")));
        Obj.Goal = AC2::BlockOrigin(Obj.Cell >> 16) + V(Pose, TEXT("position")) * 100;
        if(Event==TEXT("position") && !Teleported && Obj.Actor.IsValid())
            Obj.PresentationMotion.Push(Obj.Actor->GetActorLocation(),Obj.Goal,Time);
        else Obj.PresentationMotion={};
        Obj.Yaw = 90 - N(Pose, TEXT("heading"));
        Obj.Rotation=FRotator(0,Obj.Yaw-90,0).Quaternion();
        const TArray<TSharedPtr<FJsonValue>>* Rotation;
        if(Pose->TryGetArrayField(TEXT("rotation"),Rotation) && Rotation->Num()==4)
        {
            const FQuat Native((*Rotation)[1]->AsNumber(),(*Rotation)[2]->AsNumber(),
                (*Rotation)[3]->AsNumber(),(*Rotation)[0]->AsNumber());
            if(!Native.ContainsNaN() && Native.SizeSquared()>SMALL_NUMBER)Obj.Rotation=Native.GetNormalized();
        }
        if(!IsPlayer && Teleported && Obj.Actor.IsValid())
        {
            Obj.Actor->SetActorLocationAndRotation(Obj.Goal,Obj.Rotation,false,nullptr,ETeleportType::TeleportPhysics);
            Obj.NextGroundCheck=Obj.NextAnimationFloorCheck=0;
        }
        Obj.Placed = true;
        if (Event == TEXT("object") &&
            (Obj.Name.IsEmpty() || Obj.Name == TEXT("Portal") || Obj.Name.StartsWith(TEXT("Item "))) &&
            SelectionLook(Obj.Entity).Category == AC2Radar::Portals)
        {
            if (!PortalMapAttempted)
            {
                PortalMapAttempted = true; PortalMap = MakeUnique<FAC2MapData>(); FString Error;
                if (!PortalMap->Open(Mode->DatDirectory, Error)) PortalMap.Reset();
            }
            Obj.Name = (Obj.Cell & 0xffff) < 0x100 && PortalMap ? PortalMap->PortalLabel(FAC2MapData::Landscape(Obj.Goal)) : FString();
            if (Obj.Name.IsEmpty()) Obj.Name = TEXT("Portal");
        }
        if (IsPlayer)
        {
            if (NewInstance || Teleported || !HasPlayerPose) PortalSpaceRequested = false;
            // Once the pawn lands, force corrections update that pawn even while
            // the renderer finishes loading or the story still covers the world.
            const bool Arriving=!HasPlayerPose || NewInstance || Teleported || NeedsPlacement;
            Cell = Obj.Cell;
            Feet = Obj.Goal;
            if(Arriving)
            {
                Heading=N(Pose,TEXT("heading"));
                HasPlayerPose=NeedsPlacement=true;
            }
            else if(Forced || Impulse)
            {
                // SyncPositionSimple preserves the player's facing on a force
                // correction. It does not reopen portal space or reset input.
                if(auto* Player=Pawn(Mode))
                {
                    Player->SetActorLocation(Feet+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false,nullptr,ETeleportType::TeleportPhysics);
                    if(Impulse)Player->LaunchCharacter(V(J,TEXT("velocity"))*100,true,true);
                }
            }
        }
    }
    if (auto Vis = O(J, TEXT("visual")))
        ApplyVisual(Obj, Vis, IsPlayer);
    else if(!IsPlayer && Pose && (RestoreWorldVisual || !Obj.Actor.IsValid()) && Obj.CachedVisual)
        ApplyVisual(Obj,Obj.CachedVisual,false);
    if(Event==TEXT("object") || Event==TEXT("visual"))InvalidateExamination(Id);
    if(Event==TEXT("object"))if(auto Pending=PendingMissiles.Find(Id)){
        auto Launch=Pending->Value;PendingMissiles.Remove(Id);Handle(Launch);
    }
}
bool FAC2Network::SkillPending(uint32 Skill) const
{
    if(!IsWorld() || Dead || !Skill)return false;
    for(const auto& Request:PendingSkills)if(Request.Value==Skill && (!ActiveSkillRequest.IsSet() || ActiveSkillRequest.GetValue()!=Request.Key))return true;
    return false;
}
bool FAC2Network::SkillActive(uint32 Skill) const
{
    const auto* Value=ActiveSkillRequest.IsSet()?PendingSkills.Find(ActiveSkillRequest.GetValue()):nullptr;
    return IsWorld() && !Dead && Skill && Value && *Value==Skill;
}
bool FAC2Network::SkillBusy(uint32 Skill) const{return SkillPending(Skill)||SkillActive(Skill);}
void FAC2Network::FinishSkillStory()
{
    if(ActiveSkillRequest.IsSet())PendingSkills.Remove(ActiveSkillRequest.GetValue());
    ActiveSkillRequest.Reset();ActiveSkillBehavior=0;ActiveSkillInstance=0;ActiveSkillHoldCycles=1;
}
void FAC2Network::FinishSkillRequest(uint32 Request)
{
    if(ActiveSkillRequest.IsSet() && ActiveSkillRequest.GetValue()==Request)FinishSkillStory();
    else PendingSkills.Remove(Request);
}
void FAC2Network::StartSkillStory(uint32 Request,uint32 Behavior,uint64 Instance)
{
    FinishSkillStory();
    if(!IsWorld() || Dead || !Instance){PendingSkills.Remove(Request);return;}
    if(!PendingSkills.Contains(Request))return; // Other players/unknown counters cannot activate our slots.
    ActiveSkillRequest=Request;ActiveSkillBehavior=Behavior;ActiveSkillInstance=Instance;
}
void FAC2Network::UpdateSkillStory(uint64 PlayingInstance)
{
    // Exact action identity catches natural completion, stop, death, water and
    // same-behavior replacement. A reaction on another native layer is harmless.
    if(ActiveSkillRequest.IsSet() && PlayingInstance!=ActiveSkillInstance)FinishSkillStory();
}
FAC2SkillCooldown FAC2Network::SkillCooldown(uint32 Skill,double Duration,double ServerNow) const
{
    const auto* Known=Skills.FindByPredicate([&](const auto& S){return S.Id==Skill;});
    if(!IsWorld() || !Known || !Known->Trained)return {};
    return FAC2SkillCooldown::Longest({Known->ResetStarted,Duration},SharedSkillReset,ServerNow);
}
TOptional<double> FAC2Network::ServerTime() const
{
    return Clock.Valid ? TOptional<double>(Clock.At(FPlatformTime::Seconds())) : TOptional<double>();
}
void FAC2Network::Tick(float Delta)
{
    AC2Performance::FScope Measurement(TEXT("network_tick"));
    if(IsWorld() && !ShopVendor.IsEmpty() && Time-LastShopRefresh>10)
    {auto J=Command(TEXT("shop_refresh"));J->SetStringField(TEXT("vendor"),ShopVendor);Send(J);LastShopRefresh=Time;}

    if(ExaminationPending && !ExaminationQueued && FPlatformTime::Seconds()-ExaminationStarted>8)
    {ExaminationPending=false;ExaminationError=TEXT("The server did not provide examination information. Right-click to retry.");}
    Started += Delta;
    if (Process.IsValid())
    {
        TArray<uint8> Bytes;
        if (FPlatformProcess::ReadPipeToArray(Read, Bytes))
            Incoming.Append(Bytes);
        if (Incoming.Num() > 4 * 1024 * 1024)
        {
            SetStatus(TEXT("disconnected"), TEXT("Network event buffer exceeded limit"));
            FPlatformProcess::TerminateProc(Process, true);
            Incoming.Reset();
        }
        int32 Start = 0, Processed = 0;
        for (int32 I = 0; I < Incoming.Num() && Processed < 256; ++I)
            if (Incoming[I] == '\n')
            {
                FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR *>(Incoming.GetData() + Start), I - Start);
                TSharedPtr<FJsonObject> J;
                if (FJsonSerializer::Deserialize(
                        TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), J))
                    Handle(J);
                Start = I + 1;
                ++Processed;
            }
        if (Start)
            Incoming.RemoveAt(0, Start, EAllowShrinking::No);
        if (!FPlatformProcess::IsProcRunning(Process) && Phase != TEXT("disconnected") && !DataReload.IsSet())
            SetStatus(TEXT("disconnected"), TEXT("Network bridge stopped"));
    }
    // Incoming snapshots start at the previous presentation time. Advance only
    // after consuming them, so the arrival frame also moves toward the sample
    // instead of pausing for one frame on every 100 ms server update.
    Time += Delta;
    LaunchStoryMissiles();
    for(auto It=Objects.CreateIterator();It;++It)if(It.Value()->LocalMissile && It.Value()->MissileFinished){
        if(It.Value()->Actor.IsValid())It.Value()->Actor->Destroy();It.RemoveCurrent();
    }
    for(auto It=PendingMissiles.CreateIterator();It;++It)if(Time-It.Value().Key>5)It.RemoveCurrent();
    auto P = Pawn(Mode);
    if(ActiveSkillRequest.IsSet())
    {
        // BehaviorTracker::Think completes the hold context at the requested
        // repeat count. The physical track continues until ReleaseBehavior or Stop.
        UpdateSkillStory(P && !P->CompletedHoldCycles(ActiveSkillBehavior,ActiveSkillHoldCycles)?
            P->BehaviorInstance(ActiveSkillBehavior):0);
    }
    for (auto &E : Objects)
        if (E.Value->Actor.IsValid() && E.Value->Parent.IsEmpty())
        {
            auto &Obj = *E.Value;
            auto A = Obj.Actor.Get();
            FVector Old = A->GetActorLocation();
            if(Obj.Missile && !Obj.MissileFinished)
            {
                Obj.MissileAge+=Delta;FVector Destination=Obj.MissilePoint;bool TargetKnown=Obj.MissilePointKnown;
                FVector TargetOrigin;bool HasTarget=false;
                if(Obj.MissileTarget==PlayerId && P)
                {TargetOrigin=P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());HasTarget=true;}
                else if(auto Target=Objects.Find(Obj.MissileTarget))if((*Target)->Actor.IsValid())
                {TargetOrigin=(*Target)->Actor->GetActorLocation();HasTarget=true;}
                if(HasTarget){
                    if(!Obj.MissileOffsetRelative){Obj.MissileOffset=Obj.MissilePoint-TargetOrigin;Obj.MissileOffsetRelative=true;}
                    Destination=TargetOrigin+Obj.MissileOffset;TargetKnown=true;
                    Obj.MissilePoint=Destination;Obj.MissilePointKnown=true;
                }
                bool Arrived=false;
                const FVector Direction=TargetKnown?(Destination-Old).GetSafeNormal():Obj.MissileVelocity.GetSafeNormal();
                Obj.Goal=AC2Projectile::Advance(Old,Obj.MissileVelocity,TargetKnown?&Destination:nullptr,Delta,Arrived);
                if(!Direction.IsNearlyZero())A->SetActorRotation(AC2Projectile::Facing(Direction));
                if(Arrived || Obj.MissileAge>15)Obj.MissileFinished=true;
                if(Obj.MissileFinished)Obj.Model.Reset(); // Destroy detached trail emitters as well as hiding the mesh.
            }
            FVector Next = Obj.PresentationMotion.Sample(Time,Obj.Goal);
            if(Obj.Missile)Next=Obj.Goal;
            A->SetActorLocation(Next);
            if(!Obj.Missile)A->SetActorRotation(
                FMath::QInterpTo(A->GetActorQuat(), Obj.Rotation, Delta, 12));
            const bool Visible =
                P && !Obj.MissileFinished && (Obj.LocalMissile || Mode->CellVisible(Obj.Cell ? Obj.Cell : Cell)) &&
                FVector::DistSquared(Next, P->GetActorLocation()) < FMath::Square(30000.);
            A->SetActorHiddenInGame(!Visible);
            A->SetActorEnableCollision(Visible && Obj.Solid && !Obj.DoorOpen);
            const bool GroundFollower=Obj.FollowsGround();
            if(Visible && GroundFollower && Obj.Body.IsValid() && Time>=Obj.NextGroundCheck)
            {
                Obj.NextGroundCheck=Time+.25;
                double Water=0;
                const bool Swimming=(Obj.Cell&65535)<256 && Mode->WaterAt(Next,Water) &&
                    Water-Next.Z>Obj.BodyHeight*A->GetActorScale3D().Z*Obj.Model->MotionData().Water;
                // Discard the previous floor's correction when a query misses
                // (streaming, a ledge, or swimming). Otherwise it follows the
                // actor indefinitely and leaves it floating/buried elsewhere.
                Obj.Body->SetRelativeLocation(FVector(0,0,-Obj.FootZ));
                FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2NPCFloor),true,A);
                auto Probe=[&](double Up,double Down)
                {
                    return Mode->GetWorld()->LineTraceSingleByObjectType(Floor,Next+FVector(0,0,Up),Next-FVector(0,0,Down),
                        FCollisionObjectQueryParams(ECC_WorldStatic),Query) && !Floor.bStartPenetrating && Floor.Distance>.1 && Floor.ImpactNormal.Z>.57f;
                };
                // Prefer the nearby floor before extending above a low roof or
                // bridge. Only the visual body is adjusted, never network pose.
                if(!Swimming && (Probe(30,35) || Probe(100,200)))
                {
                    // Only correct the presentation offset, never the network pose.
                    const double ScaleZ=A->GetActorScale3D().Z;
                    Obj.Body->SetRelativeLocation(FVector(0,0,(Floor.ImpactPoint.Z-Next.Z+2)/ScaleZ-Obj.FootZ));
                }
            }
            if (Visible && Obj.Model)
            {
                FVector Velocity = Delta > 0 && FVector::DistSquared(Next,Old)<FMath::Square(1000.)
                    ? (Next - Old) / Delta : FVector::ZeroVector;
                if(!Obj.Missile && !Obj.OpenableStateKnown && !Obj.Door && !Obj.Solid && !Obj.Model->MotionData().Lists.IsEmpty())
                {
                    double Water=0;
                    const bool Wet=(Obj.Cell&65535)<256 && Mode->WaterAt(Next,Water);
                    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2RemoteFloor),true,A);
                    const FVector RemoteFeet=Obj.Body.IsValid()?Obj.Body->GetComponentTransform().TransformPosition(FVector(0,0,Obj.FootZ)):Next;
                    // Native servers use their own object-ID namespaces. A
                    // stationary remote actor is not falling merely because
                    // streamed collision or an authored floor offset is absent.
                    const bool StableGround=Obj.Element==AC2Motion::Ground && FMath::Abs(Velocity.Z)<1;
                    // Stationary actors can reuse their contact normal; moving
                    // actors must detect ledges and changes of support immediately.
                    if(Time>=Obj.NextAnimationFloorCheck || !Velocity.IsNearlyZero())
                    {
                        Obj.NextAnimationFloorCheck=Time+.25;
                        Obj.AnimationHasFloor=Mode->GetWorld()->LineTraceSingleByObjectType(Floor,RemoteFeet+FVector(0,0,30),RemoteFeet-FVector(0,0,35),
                            FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Floor.ImpactNormal.Z>.57;
                        Obj.AnimationFloor=Obj.AnimationHasFloor?Floor.ImpactNormal:FVector::UpVector;
                    }
                    const bool HasFloor=Obj.AnimationHasFloor;
                    const bool Grounded=Obj.Content || GroundFollower || StableGround || HasFloor;
                    // Content movement is server-owned ground navigation (also
                    // for hovering creatures). Missing streamed collision or an
                    // authored placement offset must not put NPCs into freefall.
                    Obj.Element=AC2Locomotion::Element(Wet,Water-Next.Z,Obj.BodyHeight*A->GetActorScale3D().Z,
                        Obj.Model->MotionData(),Grounded,Obj.Element);
                    Obj.Model->SetAnimationState(Obj.Element,AC2Locomotion::AnimationMode(Obj.Element,Obj.AnimMode));
                    const FTransform BodyFrame=Obj.Body.IsValid()?Obj.Body->GetComponentTransform():A->GetActorTransform();
                    FVector Point;bool HasTarget=false;
                    if(Obj.LookTarget==PlayerId && P)
                    {Point=P->GetActorLocation()+FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.5);HasTarget=true;}
                    else if(auto* Target=Objects.Find(Obj.LookTarget);Target && (*Target)->Actor.IsValid())
                    {
                        const auto& T=**Target;
                        const bool Creature=(SelectionLook(T.Entity).Category&(AC2Radar::Monsters|AC2Radar::NPCs|AC2Radar::Players))!=0;
                        Point=AC2Look::TargetPoint(T.Actor->GetActorLocation(),T.BodyHeight*T.Actor->GetActorScale3D().Z,Creature);HasTarget=true;
                    }
                    if(Obj.AnimMode==AC2Motion::Dead)Obj.LookPose={};
                    else if(HasTarget)Obj.LookPose.Aim(Obj.LookTarget,AC2Look::At(BodyFrame,
                        RemoteFeet+FVector(0,0,Obj.BodyHeight*A->GetActorScale3D().Z),Point),Delta);
                    else Obj.LookPose.Direction(Obj.LookDirection,Delta,false);
                    Obj.Model->SetAnimationEnvironment(Grounded,HasFloor?
                        BodyFrame.InverseTransformVectorNoScale(Obj.AnimationFloor):FVector::UpVector,Obj.LookPose.Value());
                }
                FVector Local = A->GetActorTransform().InverseTransformVector(Velocity);
                Obj.Model->SetHeldStreakActive(false);
                Obj.Model->Animate(Time, Velocity.Size2D(), &Local);
                if(Smoke && Obj.Missile)
                {
                    if(FVector::DistSquared(Old,Obj.Goal)>1)++SmokeMissileUpdates;
                    const int32 Particles=Obj.Model->EffectParticleCount();
                    if(Particles>0 && SmokeMissileParticles==0)
                        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Combat-projectile.png"),true,false);
                    SmokeMissileParticles=FMath::Max(SmokeMissileParticles,Particles);
                }
            }
        }
    // Held objects follow the current animated skeleton, including remote players.
    // Resolve after every actor has animated so map iteration order cannot add a frame of lag.
    for(auto& Entry:Objects)
    {
        auto& Obj=*Entry.Value;
        if(Obj.Parent.IsEmpty() || !Obj.Actor.IsValid()) continue;
        FTransform Socket; bool Attached=false;
        bool Handling=Obj.Parent==PlayerId && P && (P->HandlingItem() || P->ReleasedHeldItem(Obj.Holding));
        bool DrawStreak=false;
        if(Obj.Parent==PlayerId && P)
        {
            Attached=P->HoldingTransform(Obj.Holding,Socket);
            DrawStreak=P->HoldingStreakActive(Obj.Holding);
        }
        else if(auto Parent=Objects.Find(Obj.Parent))
            if((*Parent)->Actor.IsValid() && !(*Parent)->Actor->IsHidden() && (*Parent)->Model && (*Parent)->Model->AnimatedHoldingLocation(Obj.Holding,Socket))
            {
                Socket=Socket*((*Parent)->Body.IsValid()?(*Parent)->Body->GetComponentTransform():(*Parent)->Actor->GetActorTransform());
                Attached=true;
                Handling=(*Parent)->Model->HandlingItem() || (*Parent)->Model->ReleasedHeldItem(Obj.Holding);
                DrawStreak=(*Parent)->Model->HoldingStreakActive(Obj.Holding);
            }
        Obj.Actor->SetActorHiddenInGame(!Attached || Handling);
        if(Obj.Model)Obj.Model->SetHeldStreakActive(Attached && !Handling && DrawStreak);
        if(Attached)
        {
            if(Obj.Parent==PlayerId)SmokeHeld=true;
            Obj.Actor->SetActorTransform(AC2HeldItemTransform(Socket,Obj.Scale*Obj.EntityScale));
            if(Obj.Model)Obj.Model->Animate(Time,0);
        }
    }
    if (IsWorld() && P && !P->IsHeld() && !Dead && !NeedsPlacement && Time - LastMovement >= .1)
    {
        FVector Position =
            P->GetActorLocation() - FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        uint32 Block = 0;
        if ((Cell & 65535) < 256 && AC2::WorldBlock(Position, Block))
        {
            FVector Local = Position - AC2::BlockOrigin(Block);
            Cell = (Block << 16) | (FMath::Clamp(int(Local.X / 2000), 0, 7) * 8 +
                                    FMath::Clamp(int(Local.Y / 2000), 0, 7) + 1);
        }
        auto J = Command(TEXT("move"));
        J->SetNumberField(TEXT("cell"), Cell);
        J->SetArrayField(TEXT("position"), Array((Position - AC2::BlockOrigin(Cell >> 16)) / 100));
        J->SetNumberField(TEXT("heading"), FMath::Fmod(450 - P->GetActorRotation().Yaw, 360));
        FVector Local = P->GetActorTransform().InverseTransformVectorNoScale(P->GetVelocity()) / 100;
        J->SetArrayField(TEXT("motion"), Array(FVector(Local.Y, Local.X, Local.Z)));
        J->SetBoolField(TEXT("cancel_moveto"),P->HasManualMovementInput());
        Send(J);
        LastMovement = Time;
    }
    if (SmokeFinish > 0 && Time >= SmokeFinish)
    {
        FPlatformMisc::RequestExit(false);
        SmokeFinish = 0;
    }
    if (Smoke)
    {
        const bool VerifyEquipment=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_GAMEPLAY_SMOKE"))==TEXT("1");
        if(MagicAttacks>0 && P && SmokeCastParticles==0 && P->EffectParticleCount()>0)
        {
            SmokeCastParticles=P->EffectParticleCount();
            UE_LOG(LogTemp,Display,TEXT("AC2 native hand cast particles emitted: %d"),SmokeCastParticles);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-spell-cast.png"),true,false);
        }
        if(PlayedEffects>0 && SmokeParticles==0)
        {
            for(const auto& Entry:Objects)if(Entry.Value->Model)SmokeParticles+=Entry.Value->Model->EffectParticleCount();
            if(SmokeParticles>0)
            {
                UE_LOG(LogTemp,Display,TEXT("AC2 network spell particles emitted: %d"),SmokeParticles);
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-spell-impact.png"),true,false);
            }
        }
        if(IsSelection() && FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke")) &&
            !FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_REGISTER_SMOKE")).IsEmpty() && Started>2)
        {
            if(Mode->InWorld()||!Mode->Characters.Characters.IsEmpty()){
                UE_LOG(LogTemp,Error,TEXT("AC2 first-login smoke: expected an empty character selection before the world"));
                FPlatformMisc::RequestExitWithStatus(false,1);return;
            }
            UE_LOG(LogTemp,Display,TEXT("AC2 first-login mouse smoke: account created and authenticated character selection received"));
            FFileHelper::SaveStringToFile(TEXT("PASS"),*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
            FPlatformMisc::RequestExit(false);return;
        }
        if (IsSelection() && !SmokeEntered && !Mode->Characters.Characters.IsEmpty())
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke")))
            {
                if(Mode->InWorld()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
                if(!SmokeSelection){SmokeSelection=true;Started=0;}
                if(Started<2)return;
                FString Error;
                auto H=HUD(Mode);
                if(SelectionSmokeStep==15 && H && H->CharacterUI && H->CharacterUI->WaitingForAvatarAnimation())
                {
                    if(Started>15){UE_LOG(LogTemp,Error,TEXT("Avatar selection animation did not finish"));FPlatformMisc::RequestExitWithStatus(false,1);}
                    return;
                }
                if(!H || !H->CharacterUI || !H->CharacterUI->SmokeSelection(SelectionSmokeStep,Error))
                {UE_LOG(LogTemp,Error,TEXT("Selection pointer smoke step %d: %s"),SelectionSmokeStep,*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2Login-selection-%02d.png"),SelectionSmokeStep),true,false);
                Started=0;
                const bool SelectionOnly=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_SELECTION_SMOKE"))==TEXT("1");
                if(++SelectionSmokeStep<=(SelectionOnly?12:18))return;
                SmokeEntered=true;
                return;
            }
            SmokeEntered = true;
            Enter(Mode->Characters.Characters[0].Id);
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_SELECTION_SMOKE"))==TEXT("1") && SmokeEntered)
        {
            if(!IsWorld() || !P || NeedsPlacement || P->IsHeld() || Started<2)return;
            const auto* Active=Mode->Characters.Active();
            const bool Correct=Active && Active->Id==PlayerId && Active->Name==TEXT("Controller Hero");
            UE_LOG(LogTemp,Display,TEXT("AC2 character selection enter %s: %s"),Correct?TEXT("PASS"):TEXT("FAIL"),Active?*Active->Name:TEXT("missing"));
            FFileHelper::SaveStringToFile(Correct?TEXT("PASS"):TEXT("FAIL: wrong selected character entered"),
                *(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
            Smoke=false;FPlatformMisc::RequestExitWithStatus(false,Correct?0:1);return;
        }
        if(AC2Performance::Enabled())return; // The stationary profiler owns completion.
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_FOLIAGE_SMOKE"))==TEXT("1"))
        {
            FoliageSmokeTick();return;
        }
        const FString PopulationTarget=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_POPULATION_SMOKE"));
        if(!PopulationTarget.IsEmpty())
        {
            auto Finish=[&](bool OK,const FString& Error){
                UE_LOG(LogTemp,Display,TEXT("AC2 outdoor render smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
                FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);};
            // The placement-only check is already complete after its screenshot.
            // Allow its capture frame to finish even if the injured UDP fixture
            // dies during the delay; combat/loot validation has a separate mode.
            if(ShopSmokeStep==2 && Time>=ShopSmokeNext && FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_POPULATION_COMBAT_SMOKE"))!=TEXT("1"))
            {Finish(true,TEXT("Native model, selection bounds and rendered floor contact verified after portal loading"));return;}
            if(Started>100){Finish(false,FString::Printf(TEXT("Timed out at stage %d (health=%d held=%d target=%s notice=%s)"),
                ShopSmokeStep,Mode->Characters.Active()?int32(Mode->Characters.Active()->Health):-1,
                P?P->IsHeld():false,*TargetId,*Mode->Notice));return;}
            if(!IsWorld() || NeedsPlacement || Started<12 || Time<ShopSmokeNext || !P || P->IsHeld())return;
            const auto* Found=Objects.Find(PopulationTarget);if(!Found)return;
            const auto& O=**Found;
            if(!O.Actor.IsValid() || O.Actor->IsHidden() || !O.Model || O.MaxHealth<=0 || !O.Model->ModelBounds().IsValid)return;
            if(!ShopSmokeStep){
                SelectTarget(PopulationTarget);
                auto* PC=Mode->GetWorld()->GetFirstPlayerController();
                const FRotator View=(O.Actor->GetActorLocation()-P->GetActorLocation()).Rotation();
                // Offset the view so the local player cannot occlude the beetle
                // in the rendered acceptance screenshot.
                PC->SetControlRotation(FRotator(-12,View.Yaw+25,0));
                P->SetActorRotation(FRotator(0,View.Yaw,0));
                UE_LOG(LogTemp,Display,TEXT("AC2 outdoor creature entity=%08X name=%s cell=%08X health=%.0f visual=%08X"),O.Entity,*O.Name,O.Cell,O.MaxHealth,O.Visual);
                ++ShopSmokeStep;ShopSmokeNext=Time+3;return;
            }
            if(ShopSmokeStep==1){
                FBox2D Bounds;if(!ObjectScreenBounds(PopulationTarget,Bounds)){Finish(false,TEXT("Creature has no projected mesh"));return;}
                if(!O.FollowsGround() || !O.Body.IsValid()){Finish(false,TEXT("Terrestrial population actor has no ground presentation"));return;}
                const FVector RenderedFeet=O.Body->GetComponentTransform().TransformPosition(FVector(0,0,O.FootZ));
                FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2PopulationFloor),true,O.Actor.Get());
                if(!Mode->GetWorld()->LineTraceSingleByObjectType(Floor,RenderedFeet+FVector(0,0,30),RenderedFeet-FVector(0,0,35),
                    FCollisionObjectQueryParams(ECC_WorldStatic),Query) || FMath::Abs(RenderedFeet.Z-Floor.ImpactPoint.Z)>5)
                {Finish(false,TEXT("Rendered creature is not supported by its visible floor"));return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Population-outdoor.png"),true,false);
                if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_POPULATION_COMBAT_SMOKE"))==TEXT("1")){
                    const auto* Profile=Mode->Characters.Active();if(!Profile)return;
                    const int32 Staff=Profile->Items.IndexOfByPredicate([](const auto& I){return I.Definition==0x470007e0;});
                    if(Staff<0){Finish(false,TEXT("Outdoor combat observer lacks the native starter staff"));return;}
                    UE_LOG(LogTemp,Display,TEXT("AC2 outdoor combat equip: staff=%s equipped=%u catalog=%d combat=%d"),
                        *Profile->Items[Staff].NetworkId,Profile->Items[Staff].Equipped,
                        Mode->CharacterCatalog.Items.Contains(Profile->Items[Staff].Definition),Combat);
                    if(!Profile->Items[Staff].Equipped)UseItem(Staff);
                }
                ++ShopSmokeStep;ShopSmokeNext=Time+2;return;
            }
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_POPULATION_COMBAT_SMOKE"))==TEXT("1")){
                if(ShopSmokeStep==2){
                    const auto* Profile=Mode->Characters.Active();if(!Profile)return;
                    if(!Profile->Items.ContainsByPredicate([](const auto& I){return I.Definition==0x470007e0 && I.Equipped!=0;}))return;
                    StarterSkill(2);++ShopSmokeStep;ShopSmokeNext=Time+.5;return;
                }
                if(ShopSmokeStep==3){
                    if(O.Health<=0){Finish(false,FString::Printf(TEXT("Creature died before pursuit checks: moving=%d hits=%d retaliation=%d selected=%d"),
                        O.Model->MovementAnimationUpdates(),HitEvents,MobAttacks,TargetId==PopulationTarget));return;}
                    if(O.Model->MovementAnimationUpdates()<5 || O.Health>=O.MaxHealth || MobAttacks<1 || HitEvents<1)return;
                    if(TargetId!=PopulationTarget){Finish(false,TEXT("First attack cleared the selected creature"));return;}
                    auto H=HUD(Mode);FString Error;
                    if(!H || !H->GameUI || !H->GameUI->SmokeDamageText(Error)){Finish(false,Error);return;}
                    if(Mode->GroundDecalCount()==0){Finish(false,TEXT("Authoritative damage did not project blood onto the ground"));return;}
                    UE_LOG(LogTemp,Display,TEXT("AC2 outdoor pursuit animation frames=%d health=%.0f/%.0f retaliation=%d"),
                        O.Model->MovementAnimationUpdates(),O.Health,O.MaxHealth,MobAttacks);
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Population-pursuit.png"),true,false);
                    ++ShopSmokeStep;ShopSmokeNext=Time+1;return;
                }
                if(ShopSmokeStep==4){Attack();++ShopSmokeStep;ShopSmokeNext=Time+.5;return;}
                if(ShopSmokeStep==5){
                    if(O.Health>0)return;
                    if(TargetId!=PopulationTarget){Finish(false,TEXT("Death cleared the selected corpse"));return;}
                    if(!O.Name.StartsWith(TEXT("Corpse of "))){Finish(false,TEXT("Dead creature did not receive its corpse name"));return;}
                    SetCombat(false);++ShopSmokeStep;ShopSmokeNext=Time+.5;return;
                }
                const auto* Profile=Mode->Characters.Active();if(!Profile)return;
                if(ShopSmokeStep==6){
                    if(Combat)return;LootSmokeCount=Profile->Items.Num();UseTarget();++ShopSmokeStep;ShopSmokeNext=Time+.5;return;
                }
                if(ShopSmokeStep==7 || ShopSmokeStep==8){
                    if(LootItems.IsEmpty())return;
                    auto H=HUD(Mode);FString Error;
                    if(!H || !H->GameUI || !H->GameUI->SmokeLootInput(ShopSmokeStep==8,Error)){Finish(false,Error);return;}
                    if(ShopSmokeStep==7)FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Population-loot.png"),true,false);
                    ++ShopSmokeStep;ShopSmokeNext=Time+1;return;
                }
                if(!LootContainer.IsEmpty() || Profile->Items.Num()<=LootSmokeCount)return;
                Finish(true,TEXT("Captured outdoor creature remains selected after a ranged hit, pursues with native animated poses, retaliates and supplies a native loot window with item pickup"));return;
            }
            Finish(true,TEXT("Captured outdoor actor has native model, server health, selection and visible projected bounds"));return;
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_CRAFT_SMOKE"))==TEXT("1"))
        {
            auto Finish=[&](bool OK,const FString& Error){
                UE_LOG(LogTemp,Display,TEXT("AC2 crafting render smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
                FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);};
            if(Started>95){Finish(false,TEXT("Timed out waiting for crafting UI"));return;}
            if(!IsWorld() || NeedsPlacement || Started<10 || Time<ShopSmokeNext)return;
            auto H=HUD(Mode);if(!H || !H->GameUI)return;
            if(ShopSmokeStep==10){Finish(true,TEXT("Native recipe shortcut drag/examine/open, ingredient approval, pointer craft, inventory update, profession browser and recipe level controls"));return;}
            FString Error;if(!H->GameUI->SmokeCraftInput(ShopSmokeStep,Error)){Finish(false,Error);return;}
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2Craft-%d.png"),ShopSmokeStep),true,false);
            ++ShopSmokeStep;ShopSmokeNext=Time+1.5;return;
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_COOLDOWN_SMOKE"))==TEXT("1"))
        {
            auto Finish=[&](bool OK,const FString& Error){
                UE_LOG(LogTemp,Display,TEXT("AC2 cooldown render smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
                FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);};
            if(Started>60){Finish(false,TEXT("Timed out waiting for cooldown UI"));return;}
            if(!IsWorld() || NeedsPlacement || Started<10 || Time<ShopSmokeNext)return;
            auto H=HUD(Mode);if(!H || !H->GameUI)return;
            if(ShopSmokeStep==10){Finish(true,TEXT("Real pending requests, accepted/rejected completion, moving native meter, duplicate shortcuts, paging and expiry"));return;}
            FString Error;if(!H->GameUI->SmokeCooldownInput(ShopSmokeStep,Error)){Finish(false,Error);return;}
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2Cooldown-%d.png"),ShopSmokeStep),true,false);
            const double Delay[]={1.5,.15,.85,2.,1.,4.5,1.,.15,.85,1.};
            ShopSmokeNext=Time+Delay[ShopSmokeStep++];return;
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_JOURNAL_SMOKE"))==TEXT("1"))
        {
            auto Finish=[&](bool OK,const FString& Error){
                UE_LOG(LogTemp,Display,TEXT("AC2 journal render smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
                FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);};
            if(Started>95){Finish(false,TEXT("Timed out waiting for journal"));return;}
            if(!IsWorld() || NeedsPlacement || Started<10 || Time<ShopSmokeNext)return;
            auto H=HUD(Mode);if(!H || !H->GameUI)return;
            if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_QUEST_WINDOW_SMOKE"))==TEXT("1")){
                if(ShopSmokeStep<3)ShopSmokeStep=3;
                if(ShopSmokeStep==5){Finish(true,TEXT("Quest journal opened with server state, switched tabs, expanded quests and scrolled their details"));return;}
            }
            if(ShopSmokeStep==46){Finish(true,TEXT("Native skill training, advancement, untraining/cancellation, window resize, quest journal, fellowship controls, item shortcuts, five-row hotbar resizing, retail keyboard paging, second-row activation, text focus guards, options scrolling, confirmation dialogs, radar category filters, coordinate preference, shortcut locking/Shift override, skill-book quick-add, smooth/instant hotbar scrolling and native height choices"));return;}
            FString Error;if(!H->GameUI->SmokeJournalInput(ShopSmokeStep,Error)){Finish(false,Error);return;}
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2Journal-%d.png"),ShopSmokeStep),true,false);
            ++ShopSmokeStep;ShopSmokeNext=Time+1.5;return;
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_SHOP_SMOKE"))==TEXT("1"))
        {
            auto Finish=[&](bool OK,const FString& Error){
                UE_LOG(LogTemp,Display,TEXT("AC2 services render smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
                FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);
            };
            if(Started>95){Finish(false,TEXT("Timed out waiting for shop/door acknowledgement"));return;}
            if(!IsWorld() || NeedsPlacement || Started<10 || Time<ShopSmokeNext)return;
            auto H=HUD(Mode);if(!H || !H->GameUI)return;
            const FString Vendor=LexToString(0x8140000000030900ull),Door=LexToString(0x8140000000030901ull),Chest=LexToString(0x8140000000030903ull);
            FString Error;auto* Found=Objects.Find(Door);
            auto DoorBlocksPassage=[&]()
            {
                if(!Found || !(*Found)->Body.IsValid())return false;
                const auto& O=**Found;
                const auto Transform=O.Body->GetComponentTransform();
                const auto Center=ShopClosedDoorBounds.GetCenter();
                const FVector Across=ShopClosedDoorBounds.GetSize().X>ShopClosedDoorBounds.GetSize().Y?FVector(0,100,0):FVector(100,0,0);
                TArray<FHitResult> Hits;
                Mode->GetWorld()->LineTraceMultiByObjectType(Hits,Transform.TransformPosition(Center-Across),Transform.TransformPosition(Center+Across),
                    FCollisionObjectQueryParams(ECC_WorldDynamic),FCollisionQueryParams(SCENE_QUERY_STAT(AC2DoorSmoke),true));
                return Hits.ContainsByPredicate([&](const auto& Hit){return Hit.GetActor()==O.Actor.Get();});
            };
            if(ShopSmokeStep==0)
            {
                if(P && Found && (*Found)->Actor.IsValid())if(auto PC=P->GetController())
                {
                    P->SetCameraDistance(700);
                    const auto Forward=(*Found)->Body->GetComponentTransform().TransformVectorNoScale(FVector(0,1,0)).Rotation();
                    PC->SetControlRotation(FRotator(-8,Forward.Yaw,0));
                }
                SelectTarget(Vendor);UseTarget();
            }
            else if(ShopSmokeStep==1)
            {
                if(ShopVendor.IsEmpty())return;
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Shop-native.png"),true,false);
                if(!H->GameUI->SmokeShopInput(0,Error)){Finish(false,Error);return;}
            }
            else if(ShopSmokeStep==2){if(Gold!=3)return;if(!H->GameUI->SmokeShopInput(1,Error)){Finish(false,Error);return;}}
            else if(ShopSmokeStep==3)
            {
                if(Gold!=1)return;
                if(!H->GameUI->SmokeShopInput(2,Error)){Finish(false,Error);return;}
                if(!Found || !(*Found)->Model){Finish(false,TEXT("Door model unavailable"));return;}
                ShopClosedDoorBounds=(*Found)->Model->ModelBounds();
                if(!DoorBlocksPassage()){Finish(false,TEXT("Closed door lacks a blocking collision surface"));return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Door-closed.png"),true,false);
                SelectTarget(Door);UseTarget();
            }
            else if(ShopSmokeStep==4)
            {
                if(!Found || !(*Found)->DoorOpen)return;
                auto& O=**Found;const auto Bounds=O.Model->ModelBounds();
                UE_LOG(LogTemp,Display,TEXT("AC2 native door closed=%s open=%s collision=%d"),*ShopClosedDoorBounds.ToString(),*Bounds.ToString(),O.Actor->GetActorEnableCollision());
                if(O.Actor->GetActorEnableCollision() || DoorBlocksPassage() || Bounds.Equals(ShopClosedDoorBounds,1.0)) {Finish(false,TEXT("Door did not animate and release collision"));return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Door-open.png"),true,false);SelectTarget(Door);UseTarget();
            }
            else if(ShopSmokeStep==5)
            {
                if(!Found || (*Found)->DoorOpen)return;
                if(!(*Found)->Actor->GetActorEnableCollision() || !DoorBlocksPassage()){Finish(false,TEXT("Closed door collision missing"));return;}
                auto C=Objects.Find(Chest);
                if(!C || !(*C)->Model || !(*C)->Actor.IsValid()){Finish(false,TEXT("Chest model unavailable"));return;}
                if((*C)->Door || !(*C)->OpenableStateKnown || (*C)->OpenableOpen){Finish(false,TEXT("Incorrect native chest class/state"));return;}
                ShopClosedChestBounds=(*C)->Model->ModelBounds();SelectTarget(Chest);UseTarget();
            }
            else if(ShopSmokeStep==6)
            {
                auto C=Objects.Find(Chest);if(!C || !(*C)->OpenableOpen || LootContainer!=Chest)return;
                auto& O=**C;const auto Bounds=O.Model->ModelBounds();
                UE_LOG(LogTemp,Display,TEXT("AC2 native chest closed=%s open=%s collision=%d solid=%d"),*ShopClosedChestBounds.ToString(),*Bounds.ToString(),O.Actor->GetActorEnableCollision(),O.Solid);
                if(O.Door || O.DoorOpen || O.Actor->GetActorEnableCollision()!=O.Solid || Bounds.Equals(ShopClosedChestBounds,1.0)) {Finish(false,TEXT("Chest failed to animate or changed collision"));return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Chest-open.png"),true,false);CloseLoot();
            }
            else if(ShopSmokeStep==7)
            {
                auto C=Objects.Find(Chest);if(!C || (*C)->OpenableOpen || !LootContainer.IsEmpty())return;
                // This cache's native closed idle floats/bobs; frame bounds are
                // not identical. Closing must interrupt the 4-second open action
                // even though this asset does not supply a close behavior.
                if((*C)->AnimMode!=0x41000003 || (*C)->Model->BehaviorDuration(0x41000008)>0 || (*C)->Actor->GetActorEnableCollision()!=(*C)->Solid)
                    {Finish(false,TEXT("Chest retained its opening action or changed collision"));return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Chest-closed.png"),true,false);
                Finish(true,TEXT("DAT shop pointer sale/purchase/close, native door and chest animation/collision"));return;
            }
            ++ShopSmokeStep;ShopSmokeNext=Time+2;return;
        }
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_MAP_TRAVEL_SMOKE")) == TEXT("1"))
        {
            auto Finish = [&](bool OK, const FString& Error) {
                UE_LOG(LogTemp, Display, TEXT("AC2 map network smoke %s: %s"), OK ? TEXT("PASS") : TEXT("FAIL"), *Error);
                FFileHelper::SaveStringToFile(OK ? TEXT("PASS") : TEXT("FAIL: ") + Error,
                    *(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                Smoke = false;
                FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1);
            };
            if (Time > 140) { Finish(false, TEXT("Timed out waiting for map travel")); return; }
            if (!IsWorld() || !P || !MapTravelEnabled || NeedsPlacement || P->IsHeld() || Time < MapTravelSmokeNext) return;
            auto H = HUD(Mode);
            if (!H || !H->GameUI) return;
            if(MapTravelSmokeStep==4 && H->PortalEffect && H->PortalEffect->Active())return;
            FString Error;
            bool OK = true;
            switch (MapTravelSmokeStep)
            {
            case 0: H->GameUI->ToggleMap(); break;
            case 1: OK = H->GameUI->MapSmokeStep(9, Error); break;
            case 2: OK = H->GameUI->MapSmokeStep(10, Error); break;
            case 3: OK = H->GameUI->MapSmokeStep(11, Error); break;
            case 4:
                OK = (Cell >> 16) == 0x5667 && FVector::DistSquared2D(P->GetActorLocation(), FVector(1384500,1657000,0)) < FMath::Square(500.) &&
                     P->GetCharacterMovement()->IsMovingOnGround();
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Map-network-travel.png"), true, false);
                if (!OK) Error = TEXT("Server map travel did not release the pawn on the clicked terrain");
                break;
            case 5: Finish(true, TEXT("Native map input, server destination, streamed collision and grounded player")); return;
            }
            if (!OK) { Finish(false, Error); return; }
            ++MapTravelSmokeStep;
            MapTravelSmokeNext = Time + (MapTravelSmokeStep == 3 ? .1 : 2.);
            return;
        }
        if (IsWorld() && !SmokeChat)
        {
            Chat(TEXT("AC2 network smoke"));
            SmokeChat = true;
            Started = 0;
        }
        if (IsWorld() && SmokeChat && ReceivedChats > 0 && Started > (MouseSmokeStep==8?.1f:2.f) && !SmokeAttacked)
        {
            if(VerifyEquipment)
            {
                auto Profile=Mode->Characters.Active();
                if(!Profile)return;
                if(SmokeArmorStage<4)
                {
                    const int32 Shirt=Profile->Items.IndexOfByPredicate([](const auto& I){return I.Definition==0x47000ef8;});
                    if(Shirt<0)return;
                    if(SmokeArmorStage==0){SmokeClothedHash=P->AvatarAppearanceFingerprint();UseItem(Shirt);SmokeArmorStage=1;Started=0;return;}
                    if(SmokeArmorStage==1 && !Profile->Items[Shirt].Equipped && P->AvatarAppearanceFingerprint()!=SmokeClothedHash)
                    {FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-armor-off.png"),true,false);SmokeArmorStage=2;Started=0;return;}
                    if(SmokeArmorStage==2){UseItem(Shirt);SmokeArmorStage=3;Started=0;return;}
                    if(SmokeArmorStage==3 && Profile->Items[Shirt].Equipped && P->AvatarAppearanceFingerprint()==SmokeClothedHash)
                    {FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-armor-on.png"),true,false);SmokeArmorStage=4;SmokeArmor=true;Started=0;return;}
                    return;
                }
                const int32 Staff=Profile->Items.IndexOfByPredicate([](const auto& I){return I.Definition==0x470007e0;});
                if(Staff<0)return;
                if(!SmokeEquipped){UseItem(Staff);SmokeEquipped=true;Started=0;return;}
                if(!Profile->Items[Staff].Equipped || !SmokeHeld || Appearance(PlayerId).IsEmpty())return;
            }
            if(CombatSmokeStep<8)
            {
                FString Error;auto H=HUD(Mode);
                if(VerifyEquipment && CombatSmokeStep==0 && !SmokeWorldProps(Error))
                {UE_LOG(LogTemp,Error,TEXT("AC2 world props smoke: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                if(!H || !H->GameUI || !H->GameUI->SmokeCombatInput(CombatSmokeStep,Error))
                {UE_LOG(LogTemp,Error,TEXT("AC2 combat input smoke: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                ++CombatSmokeStep;Started=0;return;
            }
            for (auto &E : Objects)
                if (E.Value->Entity == 0x470007d1 && FVector::Dist(E.Value->Goal, Feet) < 300)
                {
                    if(CombatSmokeStep==8)
                    {
                        if(auto* PC=Cast<AAC2PlayerController>(P->GetController()))
                        {
                            PC->SetCursor(true);
                            // Frame above the foreground lifestone; clicking through its
                            // opaque base would correctly select the stone instead.
                            P->Boom->SetRelativeLocation(P->Boom->GetRelativeLocation()+FVector(0,0,300));
                            PC->SetControlRotation((E.Value->Actor->GetComponentsBoundingBox(true).GetCenter()-P->Boom->GetComponentLocation()).Rotation());
                        }
                        ++CombatSmokeStep;Started=0;return;
                    }
                    FString RadarError;
                    if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_MOUSE_SMOKE"))==TEXT("1") && MouseSmokeStep<10)
                    {
                        auto H=HUD(Mode);
                        if(!H || !H->GameUI || !H->GameUI->SmokeMouseControls(MouseSmokeStep,E.Key,RadarError))
                        {
                            UE_LOG(LogTemp,Error,TEXT("AC2 mouse controls smoke: %s"),*RadarError);
                            FFileHelper::SaveStringToFile(TEXT("FAIL: ")+RadarError,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                            FPlatformMisc::RequestExitWithStatus(false,1);return;
                        }
                        ++MouseSmokeStep;Started=0;return;
                    }
                    if(auto H=HUD(Mode); !H || !H->GameUI || !H->GameUI->SmokeWorldTarget(E.Key,RadarError))
                    {
                        UE_LOG(LogTemp,Error,TEXT("AC2 combat pointer smoke: %s"),*RadarError);
                        FFileHelper::SaveStringToFile(TEXT("FAIL: ")+RadarError,*(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
                        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Combat-pointer-failed.png"),true,false);
                        Smoke=false;SmokeFinish=Time+2;return;
                    }
                    if (auto H = HUD(Mode); H && H->GameUI)
                        SmokeRadar = H->GameUI->SmokeNetworkRadar(E.Key, RadarError);
                    if (!SmokeRadar) { UE_LOG(LogTemp, Error, TEXT("AC2 network radar smoke: %s"), *RadarError); continue; }
                    if(VerifyEquipment)StarterSkill(2);
                    else if(auto H=HUD(Mode);H && H->GameUI)H->GameUI->SmokeCombatInput(8,RadarError);
                    SmokeAttacked = true;
                    FScreenshotRequest::RequestScreenshot(
                        FPaths::ProjectSavedDir() / TEXT("Screenshots/AC2Network-world.png"), true, false);
                    break;
                }
        }
        if(VerifyEquipment && IsWorld() && SmokeAttacked && !SmokeRepeated && Started>3)
        {Attack();SmokeRepeated=true;}
        const auto Profile = Mode->Characters.Active();
        if(IsWorld() && Profile && SmokeAttacked && DeathBehaviors>0 && Combat && LootSmokeStep==0 && Started>6 && Time>LootSmokeNext)
        {SetCombat(false);LootSmokeNext=Time+.5;}
        if(IsWorld() && Profile && SmokeAttacked && DeathBehaviors>0 && !Combat && Started>6 && Time>LootSmokeNext)
        {
            if(LootSmokeStep==0){LootSmokeCount=Profile->Items.Num();UseTarget();LootSmokeStep=1;LootSmokeNext=Time+.5;}
            else if((LootSmokeStep==1 || LootSmokeStep==2) && !LootItems.IsEmpty())
            {
                auto H=HUD(Mode);FString Error;
                if(!H || !H->GameUI || !H->GameUI->SmokeLootInput(LootSmokeStep==2,Error))
                {UE_LOG(LogTemp,Error,TEXT("AC2 loot UI smoke: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                if(LootSmokeStep==1)FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Network-corpse-loot.png"),true,false);
                ++LootSmokeStep;LootSmokeNext=Time+1;
            }
            else if(LootSmokeStep==3 && LootContainer.IsEmpty() && Profile->Items.Num()>LootSmokeCount)
            {LootSmokeStep=4;UE_LOG(LogTemp,Display,TEXT("AC2 native corpse loot window and item double-click passed"));}
            else if(LootSmokeStep==1 && !LootContainer.IsEmpty() && LootItems.IsEmpty())
            {LootSmokeStep=4;UE_LOG(LogTemp,Display,TEXT("AC2 native empty corpse window passed: no unverified replacement loot"));}
        }
        const bool HasLoot = Profile && LootSmokeStep==4 && Profile->Experience > 0;
        if ((IsWorld() && SmokeAttacked && HasLoot && Started > 6) || Started > 120)
        {
            bool OK = IsWorld() && ReceivedChats > 0 && P && P->HasModel() && Objects.Num() > 1 && HasLoot && SmokeRadar && PlayerAttacks>0 && MobAttacks>0 && HitEvents>0 && DeathBehaviors>0 && FailedAttacks==0 && (!VerifyEquipment || (SmokeHeld && MagicAttacks>0 && SmokeArmor && PlayedEffects>0 && SmokeParticles>0 && SmokeCastParticles>0));
            if(VerifyEquipment)OK &= SmokeMissiles>0 && SmokeMissileUpdates>0 && SmokeMissileParticles>0;
            FString Report = FPaths::ProjectSavedDir() / TEXT("Diagnostics/AC2Network-smoke.txt");
            if(OK && VerifyEquipment && InventorySmokeStep<16)
            {
                if(Time<InventorySmokeNext)return;
                FString Error;auto H=HUD(Mode);
                if(H && H->GameUI && H->GameUI->SmokeInventoryInput(InventorySmokeStep,Error))
                {++InventorySmokeStep;InventorySmokeNext=Time+.7;return;}
                UE_LOG(LogTemp,Error,TEXT("AC2 inventory smoke step %d: %s"),InventorySmokeStep,*Error);OK=false;
            }
            FFileHelper::SaveStringToFile(OK ? TEXT("PASS") : TEXT("FAIL"), *Report);
            if (auto H = HUD(Mode))
                if (H->GameUI)
                {
                    H->GameUI->ToggleInventory();
                    H->GameUI->ToggleStats();
                }
            FScreenshotRequest::RequestScreenshot(
                FPaths::ProjectSavedDir() / TEXT("Screenshots/AC2Network-loot.png"), true, false);
            UE_LOG(LogTemp, Display, TEXT("AC2 network smoke %s: objects=%d chat=%d loot=%d xp=%lld"),
                   OK ? TEXT("PASS") : TEXT("FAIL"), Objects.Num(), ReceivedChats,
                   Profile ? Profile->Items.Num() : 0, Profile ? Profile->Experience : 0);
            UE_LOG(LogTemp,Display,TEXT("AC2 server animation verification: played=%d player-attacks=%d mob-attacks=%d hit=%d death=%d failed-attacks=%d"),PlayedBehaviors,PlayerAttacks,MobAttacks,HitEvents,DeathBehaviors,FailedAttacks);
            UE_LOG(LogTemp,Display,TEXT("AC2 projectile verification: missiles=%d moving-frames=%d trail-particles=%d"),SmokeMissiles,SmokeMissileUpdates,SmokeMissileParticles);
            for (const auto &Entry : Objects)
                if (Entry.Value->Actor.IsValid())
                    UE_LOG(LogTemp, Display, TEXT("AC2 animated server model %08X size=%s"),
                           Entry.Value->Visual,
                           *Entry.Value->Actor->GetComponentsBoundingBox(true).GetSize().ToString());
            Smoke = false;
            SmokeFinish = Time + 2;
        }
    }
}
