#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2Explorer.h"
#include "AC2WorldGrid.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace
{
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
bool FAC2Network::Start(const FAC2ConnectionSettings* Settings)
{
    FString Executable = FPaths::ConvertRelativePathToFull(
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
    if(Settings){Host=Settings->Host;Port=Settings->Port;Account=Settings->Account;Ticket=Settings->Ticket;}
    auto J = Command(TEXT("connect"));
    J->SetStringField(TEXT("host"), Host);
    J->SetNumberField(TEXT("port"), Port);
    J->SetStringField(TEXT("account"), Account);
    J->SetStringField(TEXT("version"), Version);
    J->SetStringField(TEXT("ticket"), Ticket);
    Send(J);
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
    FString Text;
    FJsonSerializer::Serialize(J, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    Text += TEXT("\n");
    FTCHARToUTF8 Bytes(*Text);
    if (Bytes.Length() < 65536)
        FPlatformProcess::WritePipe(Write, reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
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
        ClearWorld();
        if (auto P = Pawn(Mode))
            P->SetHeld(true);
        Mode->ShowLogin(Text);
    }
    UE_LOG(LogTemp, Display, TEXT("AC2 network %s: %s"), *State, *Text);
}
void FAC2Network::ClearWorld()
{
    for (auto &Pair : Objects)
        if (Pair.Value->Actor.IsValid())
            Pair.Value->Actor->Destroy();
    Objects.Reset();
    BagSlots.Reset();
    WornSlots.Reset();
    PlayerId.Reset();
    TargetId.Reset();
    HasPlayerPose = NeedsPlacement = false;
    Dead = false;
    if(auto P=Pawn(Mode)){P->ResetMotionParameters();P->SetDead(false);P->SetHeld(true);}
}
uint32 FAC2Network::Visual(const FString &Id) const
{
    auto P = CharacterVisuals.Find(Id);
    return P ? uint32(N(*P, TEXT("did"))) : 0;
}
FAC2Appearance FAC2Network::Appearance(const FString &Id) const
{
    auto P = CharacterVisuals.Find(Id);
    return P ? Aspects(*P) : FAC2Appearance();
}
FString FAC2Network::StringInfo(const TSharedPtr<FJsonObject> &J) const
{
    FString Text = S(J, TEXT("literal"));
    if (Text.IsEmpty() && J && Locale)
        Text = AC2ReadUIString(*Locale, uint32(N(J, TEXT("table"))), uint32(N(J, TEXT("key"))));
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
            P->ApplyAvatar(Obj.Visual, Obj.Scale.Z, Obj.Scale.X, Obj.Aspects);
        return;
    }
    if (Obj.Actor.IsValid())
        Obj.Actor->Destroy();
    Obj.Model.Reset();
    if (!Obj.Visual || !Obj.Placed)
        return;
    auto A = Mode->GetWorld()->SpawnActor<AActor>();
    Obj.Actor = A;
    auto Root = NewObject<USceneComponent>(A);
    A->SetRootComponent(Root);
    Root->RegisterComponent();
    A->SetActorEnableCollision(false);
    A->SetActorLocation(Obj.Goal);
    A->SetActorRotation(FRotator(0, Obj.Yaw - 90, 0));
    A->SetActorScale3D(Obj.Scale * Obj.EntityScale);
    Obj.Model = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    if (!Obj.Model->Open(Mode->DatDirectory, Error) ||
        !Obj.Model->DrawModel(Obj.Visual, A, Root, Obj.Aspects))
    {
        UE_LOG(LogTemp, Warning, TEXT("AC2 server visual unavailable: %08X"), Obj.Visual);
        A->Destroy();
        Obj.Model.Reset();
    }
    else if (Smoke)
    {
        const FBox Bounds = A->GetComponentsBoundingBox(true);
        UE_LOG(LogTemp, Display, TEXT("AC2 network model %08X size=%s"), Obj.Visual,
               *Bounds.GetSize().ToString());
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
    J->SetNumberField(TEXT("start_area"), A.Location);
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
    NeedsPlacement = false;
    Send(Command(TEXT("ready")));
    SetStatus(TEXT("world"), TEXT("Connected to server world"));
    if (auto H = HUD(Mode))
        if (H->CharacterUI)
            H->CharacterUI->NetworkEntered();
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
    if (Text.StartsWith(TEXT("/me ")))
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
        Mode->Notice = TEXT("Network chat: /say, /me, /tell Name, message");
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
        if (E.Key != PlayerId && E.Value->Actor.IsValid() &&
            FVector::Dist(E.Value->Goal, P->GetActorLocation()) < 15000)
            Ids.Add(E.Key);
    Ids.Sort();
    if (Ids.IsEmpty())
        return;
    SelectTarget(Ids[(Ids.Find(TargetId) + 1) % Ids.Num()]);
}
TArray<FAC2Network::FRadarContact> FAC2Network::RadarContacts() const
{
    TArray<FRadarContact> Result;
    if (!IsWorld()) return Result;
    const bool Indoors = (Cell & 0xffff) >= 0x100;
    for (const auto &E : Objects)
    {
        const auto &O = *E.Value;
        if (E.Key == PlayerId || !O.Placed || !O.Actor.IsValid() || !O.Cell) continue;
        if (((O.Cell & 0xffff) >= 0x100) != Indoors || (Indoors && (O.Cell >> 16) != (Cell >> 16))) continue;
        Result.Add({E.Key, O.Name, O.Actor});
    }
    Result.Sort([](const auto &A, const auto &B) { return A.Id < B.Id; });
    return Result;
}
void FAC2Network::SelectTarget(const FString &Id)
{
    if (!IsWorld() || Id == PlayerId) return;
    const auto *Object = Objects.Find(Id);
    auto *Player = Pawn(Mode);
    if (!Object || !(*Object)->Actor.IsValid() || !Player ||
        FVector::DistSquared((*Object)->Actor->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(15000.)) return;
    TargetId = Id;
    auto J = Command(TEXT("target"));
    J->SetStringField(TEXT("id"), TargetId);
    Send(J);
    Mode->Notice = TEXT("Target: ") + Objects[TargetId]->Name;
}
void FAC2Network::Attack()
{
    if (IsWorld() && !TargetId.IsEmpty())
    {
        auto J = Command(TEXT("attack"));
        J->SetStringField(TEXT("id"), TargetId);
        Send(J);
    }
}
void FAC2Network::UseTarget()
{
    if (IsWorld() && !TargetId.IsEmpty())
    {
        auto J = Command(TEXT("use"));
        J->SetStringField(TEXT("target"), TEXT("0")); // Native self-use of a selected world object.
        J->SetStringField(TEXT("item"), TargetId);
        Send(J);
    }
}
void FAC2Network::UseItem(int32 Index)
{
    auto P = Mode->Characters.Active();
    if (!IsWorld() || !P || !P->Items.IsValidIndex(Index))
        return;
    auto &I = P->Items[Index];
    auto D = Mode->CharacterCatalog.Items.Find(I.Definition);
    if (!D)
        return;
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
        J->SetNumberField(TEXT("location"), D->Preferred);
        J->SetNumberField(TEXT("slot"), I.Cell);
        Send(J);
    }
    else
    {
        auto J = Command(TEXT("use"));
        J->SetStringField(TEXT("item"), I.NetworkId);
        J->SetStringField(TEXT("target"), TEXT("0")); // Owner is UsageDesc.user; native self-use has no target.
        Send(J);
    }
}
void FAC2Network::MoveItem(int32 Index, int32 Slot)
{
    auto P = Mode->Characters.Active();
    if (!IsWorld() || !P || !P->Items.IsValidIndex(Index) || Slot < 0 || Slot >= 78)
        return;
    auto &I = P->Items[Index];
    if (I.Equipped)
        return;
    auto J = Command(TEXT("move_item"));
    J->SetStringField(TEXT("id"), I.NetworkId);
    J->SetNumberField(TEXT("from"), I.Cell);
    J->SetNumberField(TEXT("to"), Slot);
    J->SetNumberField(TEXT("quantity"), I.Quantity);
    Send(J);
}
void FAC2Network::Handle(const TSharedPtr<FJsonObject> &J)
{
    FString Event = S(J, TEXT("event")), Id = S(J, TEXT("id"));
    if(Event==TEXT("behavior")||Event==TEXT("mode")||Event==TEXT("stop_behavior")||Event==TEXT("hit")||Event==TEXT("impulse")||Event==TEXT("motion"))
    {
        auto* Found=Objects.Find(Id);
        if(!Found || (J->HasField(TEXT("instance")) && int32(N(J,TEXT("instance")))!=(*Found)->Instance))return;
        auto& Obj=**Found;
        auto* Player=Id==PlayerId?Pawn(Mode):nullptr;
        const uint32 B=uint32(N(J,TEXT("behavior"))), M=uint32(N(J,TEXT("mode")));
        if(M){Obj.AnimMode=M;if(Player)Player->SetNativeMode(M);}
        if(Obj.Model)Obj.Model->SetAnimationState(AC2Motion::Ground,Obj.AnimMode);
        if(Event==TEXT("behavior"))
        {
            bool Hold=false;J->TryGetBoolField(TEXT("hold"),Hold);Hold|=B==AC2Motion::Death;
            const bool Played=Player?Player->PlayBehavior(B,M,float(N(J,TEXT("rate"),1)),Hold):Obj.Model&&Obj.Model->PlayBehavior(B,M,float(N(J,TEXT("rate"),1)),Hold);
            if(Played)
            {
                ++PlayedBehaviors;if(B==AC2Motion::Death)++DeathBehaviors;
                if(B==AC2Motion::Attack||B==AC2Motion::MeleeNormal){if(Player)++PlayerAttacks;else ++MobAttacks;}
            }
            else if(B==AC2Motion::Attack||B==AC2Motion::MeleeNormal)++FailedAttacks;
            if(Smoke)UE_LOG(LogTemp,Display,TEXT("AC2 server behavior %08X mode=%08X played=%d player=%d"),B,M,Played,Player!=nullptr);
        }
        if(Event==TEXT("stop_behavior")){if(Player)Player->StopBehavior(B);else if(Obj.Model)Obj.Model->StopBehavior(B);}
        if(Event==TEXT("hit"))
        {
            const bool Played=Player&&!Dead?Player->PlayBehavior(AC2Motion::Hit):Obj.Model&&Obj.AnimMode!=AC2Motion::Dead&&Obj.Model->PlayBehavior(AC2Motion::Hit);
            if(Played)++HitEvents;
        }
        if(Event==TEXT("motion")&&Player)Player->SetMotionParameter(S(J,TEXT("parameter")),N(J,TEXT("value")));
        if(Event==TEXT("impulse")&&Player)Player->GetCharacterMovement()->AddImpulse(V(J,TEXT("direction"))*100,true);
        return;
    }
    if (Event == TEXT("status"))
    {
        SetStatus(S(J, TEXT("phase")), S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("disconnected"))
    {
        SetStatus(TEXT("disconnected"), S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("error") || Event == TEXT("character_error"))
    {
        SetStatus(HasPlayerPose ? Phase : TEXT("selection"),
                  S(J, TEXT("message")).IsEmpty()
                      ? FString::Printf(TEXT("Server character error %d"), int32(N(J, TEXT("code"))))
                      : S(J, TEXT("message")));
        return;
    }
    if (Event == TEXT("characters"))
    {
        ClearWorld();
        Mode->Characters = FAC2CharacterStore();
        CharacterVisuals.Reset();
        const TArray<TSharedPtr<FJsonValue>> *List;
        if (J->TryGetArrayField(TEXT("characters"), List))
            for (auto &E : *List)
                AddCharacter(E->AsObject());
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
        else
            SetStatus(TEXT("selection"), FString::Printf(TEXT("Character creation rejected (%d)"),
                                                         int32(N(J, TEXT("response")))));
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
        if (!Speaker.IsEmpty())
            Text = Speaker + TEXT(": ") + Text;
        if (auto H = HUD(Mode))
            if (H->GameUI)
                H->GameUI->ReceiveNetworkChat(Text);
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
            Mode->Notice = TEXT("You have fallen. Use /resurrect to request resurrection.");
        return;
    }
    if (Event == TEXT("inventory") || Event == TEXT("bag") || Event == TEXT("equipment"))
    {
        const TArray<TSharedPtr<FJsonValue>> *List;
        if (Event == TEXT("inventory"))
            Gold = N(J, TEXT("gold"));
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
        if (auto P = Mode->Characters.Active())
            for (auto &I : P->Items)
            {
                if (auto Slot = BagSlots.Find(I.NetworkId))
                {
                    I.Cell = *Slot;
                    I.Equipped = 0;
                }
                if (auto Slot = WornSlots.Find(I.NetworkId))
                    I.Equipped = *Slot;
            }
        return;
    }
    if (Event == TEXT("inventory_result"))
    {
        if (N(J, TEXT("status")) != 0)
        {
            Mode->Notice =
                FString::Printf(TEXT("Server rejected inventory change (%d)"), int32(N(J, TEXT("status"))));
            return;
        }
        if (auto P = Mode->Characters.Active())
            for (auto &I : P->Items)
                if (I.NetworkId == Id && J->HasField(TEXT("slot")))
                {
                    I.Cell = N(J, TEXT("slot"));
                    I.Equipped = 0;
                    BagSlots.Add(Id, I.Cell);
                }
        return;
    }
    if (Event == TEXT("quality") && (Id.IsEmpty() || Id == PlayerId))
    {
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
                    P->Level = Value;
                else if (Key == 0xc1c)
                    Gold = Value;
            }
            if (Table == 12 && Key == 0x12c)
                P->Experience = FMath::Clamp<int64>(FCString::Atoi64(*S(J, TEXT("value"))), 0, MAX_int32);
        }
        return;
    }
    if (Event == TEXT("remove"))
    {
        if (auto E = Objects.Find(Id))
        {
            if ((*E)->Actor.IsValid())
                (*E)->Actor->Destroy();
            Objects.Remove(Id);
        }
        if (auto P = Mode->Characters.Active())
            P->Items.RemoveAll([&](const FAC2InventoryItem &I) { return I.NetworkId == Id; });
        return;
    }
    if (Event != TEXT("object") && Event != TEXT("position") && Event != TEXT("visual"))
        return;
    if (Id.IsEmpty() || (Objects.Num() >= 2048 && !Objects.Contains(Id)))
        return;
    if (!Objects.Contains(Id))
        Objects.Add(Id, MakeUnique<FObject>());
    auto &Obj = *Objects[Id];
    const bool IsPlayer = Id == PlayerId;
    if (Event == TEXT("object"))
    {
        // Behavior/mode stamps are instance stamps. Retain the create stamp so
        // valid server actions are not compared against an uninitialized zero.
        Obj.Instance=int32(N(J,TEXT("instance")));
        Obj.Name = StringInfo(O(J, TEXT("name")));
        Obj.Entity = N(J, TEXT("entity"));
        Obj.EntityScale = FMath::Clamp(float(N(J, TEXT("scale"), 1)), .01f, 100.f);
        if (Obj.Name.IsEmpty() && Obj.Entity)
        {
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory, Obj.Entity);
            if (auto Definition = Mode->CharacterCatalog.Items.Find(Obj.Entity))
                Obj.Name = Definition->Name;
        }
    }
    if (Event == TEXT("object") && !PlayerId.IsEmpty() &&
        (S(J, TEXT("container")) == PlayerId || S(J, TEXT("wielder")) == PlayerId))
    {
        if (auto P = Mode->Characters.Active())
        {
            uint32 Did = N(J, TEXT("entity"));
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory, Did);
            FAC2InventoryItem I;
            I.Definition = Did;
            I.NetworkId = Id;
            I.Quantity = FMath::Max(1, int32(N(J, TEXT("quantity"), 1)));
            I.Cell = BagSlots.Contains(Id) ? BagSlots[Id] : P->Items.Num();
            I.Equipped = WornSlots.FindRef(Id);
            P->Items.RemoveAll([&](const FAC2InventoryItem &Existing) { return Existing.NetworkId == Id; });
            P->Items.Add(I);
        }
        return;
    }
    auto Pose = Event == TEXT("object") ? O(J, TEXT("placement")) : (Event == TEXT("position") ? J : nullptr);
    if(Event==TEXT("position") && Obj.Instance && int32(N(J,TEXT("instance")))!=Obj.Instance)return;
    if (Pose)
    {
        Obj.Cell = uint32(N(Pose, TEXT("cell")));
        Obj.Goal = AC2::BlockOrigin(Obj.Cell >> 16) + V(Pose, TEXT("position")) * 100;
        Obj.Yaw = 90 - N(Pose, TEXT("heading"));
        Obj.Placed = true;
        if (IsPlayer)
        {
            Cell = Obj.Cell;
            Feet = Obj.Goal;
            Heading = N(Pose, TEXT("heading"));
            // The server only sends our own position for entry or authoritative correction.
            HasPlayerPose = true;
            NeedsPlacement = true;
        }
    }
    if (auto Vis = O(J, TEXT("visual")))
        ApplyVisual(Obj, Vis, IsPlayer);
}
void FAC2Network::Tick(float Delta)
{
    Time += Delta;
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
        if (!FPlatformProcess::IsProcRunning(Process) && Phase != TEXT("disconnected"))
            SetStatus(TEXT("disconnected"), TEXT("Network bridge stopped"));
    }
    auto P = Pawn(Mode);
    for (auto &E : Objects)
        if (E.Value->Actor.IsValid())
        {
            auto &Obj = *E.Value;
            auto A = Obj.Actor.Get();
            FVector Old = A->GetActorLocation();
            FVector Next = FVector::DistSquared(Old, Obj.Goal) > FMath::Square(1000.)
                               ? Obj.Goal
                               : FMath::VInterpTo(Old, Obj.Goal, Delta, 12);
            A->SetActorLocation(Next);
            A->SetActorRotation(
                FMath::RInterpTo(A->GetActorRotation(), FRotator(0, Obj.Yaw - 90, 0), Delta, 12));
            const bool Visible =
                P && FVector::DistSquared(Next, P->GetActorLocation()) < FMath::Square(30000.);
            A->SetActorHiddenInGame(!Visible);
            if (Visible && Obj.Model)
            {
                FVector Velocity = Delta > 0 ? (Next - Old) / Delta : FVector::ZeroVector;
                FVector Local = A->GetActorTransform().InverseTransformVectorNoScale(Velocity);
                Obj.Model->Animate(Time, Velocity.Size2D(), nullptr, &Local);
            }
        }
    if (IsWorld() && P && !Dead && !NeedsPlacement && Time - LastMovement >= .1)
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
        if (IsSelection() && !SmokeEntered && !Mode->Characters.Characters.IsEmpty())
        {
            if(FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke")))
            {
                if(Mode->InWorld()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
                if(!SmokeSelection){SmokeSelection=true;Started=0;FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-selection.png"),true,false);}
                if(Started<2)return;
            }
            SmokeEntered = true;
            Enter(Mode->Characters.Characters[0].Id);
        }
        if (IsWorld() && !SmokeChat)
        {
            Chat(TEXT("AC2 network smoke"));
            SmokeChat = true;
            Started = 0;
        }
        if (IsWorld() && SmokeChat && ReceivedChats > 0 && Started > 2 && !SmokeAttacked)
        {
            for (auto &E : Objects)
                if (E.Value->Entity == 0x470007d1 && FVector::Dist(E.Value->Goal, Feet) < 300)
                {
                    FString RadarError;
                    if (auto H = HUD(Mode); H && H->GameUI)
                        SmokeRadar = H->GameUI->SmokeNetworkRadar(E.Key, RadarError);
                    if (!SmokeRadar) { UE_LOG(LogTemp, Error, TEXT("AC2 network radar smoke: %s"), *RadarError); continue; }
                    Attack();
                    SmokeAttacked = true;
                    FScreenshotRequest::RequestScreenshot(
                        FPaths::ProjectSavedDir() / TEXT("Screenshots/AC2Network-world.png"), true, false);
                    break;
                }
        }
        const auto Profile = Mode->Characters.Active();
        const bool HasLoot = Profile && !Profile->Items.IsEmpty() && Profile->Experience > 0;
        if ((IsWorld() && SmokeAttacked && HasLoot && Started > 6) || Started > 120)
        {
            bool OK = IsWorld() && ReceivedChats > 0 && P && P->HasModel() && Objects.Num() > 1 && HasLoot && SmokeRadar && PlayerAttacks>0 && MobAttacks>0 && HitEvents>0 && DeathBehaviors>0 && FailedAttacks==0;
            FString Report = FPaths::ProjectSavedDir() / TEXT("Diagnostics/AC2Network-smoke.txt");
            FFileHelper::SaveStringToFile(OK ? TEXT("PASS") : TEXT("FAIL"), *Report);
            if (auto H = HUD(Mode))
                if (H->GameUI)
                {
                    H->GameUI->ToggleInventory();
                    H->GameUI->ToggleStats();
                }
            FScreenshotRequest::RequestScreenshot(
                FPaths::ProjectSavedDir() / TEXT("Screenshots/AC2Network-loot.png"), true, false);
            UE_LOG(LogTemp, Display, TEXT("AC2 network smoke %s: objects=%d chat=%d loot=%d xp=%d"),
                   OK ? TEXT("PASS") : TEXT("FAIL"), Objects.Num(), ReceivedChats,
                   Profile ? Profile->Items.Num() : 0, Profile ? Profile->Experience : 0);
            UE_LOG(LogTemp,Display,TEXT("AC2 server animation verification: played=%d player-attacks=%d mob-attacks=%d hit=%d death=%d failed-attacks=%d"),PlayedBehaviors,PlayerAttacks,MobAttacks,HitEvents,DeathBehaviors,FailedAttacks);
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
