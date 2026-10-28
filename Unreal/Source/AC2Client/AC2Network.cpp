#include "AC2Network.h"
#include "AC2Projectile.h"
#include "AC2GameMode.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2Explorer.h"
#include "AC2WorldGrid.h"
#include "AC2MapData.h"
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
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

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
    ShopVendor.Reset();ShopOffers.Reset();
    BagSlots.Reset();
    WornSlots.Reset();
    PlayerId.Reset();
    TargetId.Reset();
    ExaminationId.Reset();ExaminationRows.Reset();ExaminationError.Reset();ExaminationPending=false;
    HasPlayerPose = NeedsPlacement = false;
    Dead = Combat = false;
    MapTravelEnabled = false;
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
        if(auto H=HUD(Mode);H && H->GameUI)H->GameUI->RefreshInventoryAppearance();
        return;
    }
    if (Obj.Actor.IsValid())
        Obj.Actor->Destroy();
    Obj.Model.Reset();
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
        const bool GroundItem=Obj.GroundItem;
        const FBox FeetBounds=Obj.Model->ModelBounds(GroundItem?0:0x81000001);
        const auto Motion=Obj.Model->MotionData().Values(AC2Motion::Ground);
        // Default motion values include gravity even for visuals with no native
        // motion descriptor. Static props must retain their authored pivot.
        const bool GroundedBody=FeetBounds.IsValid && (GroundItem ||
            (!Obj.Model->MotionData().Lists.IsEmpty() && Motion.Gravity && FMath::IsNearlyZero(Motion.Hover)));
        Obj.GroundedContent=Obj.Content && Obj.Parent.IsEmpty() && GroundedBody;
        // Feet are a mesh-type in the DAT. Do not use an effect's bounding box
        // or a tail tip as the humanoid's standing plane.
        Obj.FootZ=GroundedBody?FeetBounds.Min.Z:0;
        if(GroundedBody && Obj.Parent.IsEmpty())Body->SetRelativeLocation(FVector(0,0,-Obj.FootZ));
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
        Result.Add({E.Key, O.Name, O.Actor});
    }
    Result.Sort([](const auto &A, const auto &B) { return A.Id < B.Id; });
    return Result;
}
bool FAC2Network::Targetable(const FString &Id, const FObject &O) const
{
    const auto* P=Pawn(Mode);
    return IsWorld() && Id!=PlayerId && O.Placed && O.Parent.IsEmpty() && !O.Missile &&
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
        if(Obj.GroundedContent && Obj.Body.IsValid())
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
    Mode->Notice = TEXT("Target: ") + Objects[TargetId]->Name;
}
void FAC2Network::ClearTarget()
{
    if(!IsWorld() || TargetId.IsEmpty())return;
    TargetId.Reset();auto J=Command(TEXT("target"));J->SetStringField(TEXT("id"),TEXT("0"));Send(J);
}
FString FAC2Network::ObjectName(const FString& Id) const
{
    const auto* O=Objects.Find(Id);
    return O?(*O)->Name:FString();
}
bool FAC2Network::ObjectScreenBounds(const FString& Id,FBox2D& Bounds) const
{
    const auto* O=Objects.Find(Id);auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(!O || !Targetable(Id,**O) || !PC)return false;
    const auto Box=(*O)->Actor->GetComponentsBoundingBox(true);
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
    ExaminationId=Id;ExaminationRows.Reset();ExaminationError.Reset();ExaminationPending=true;
    ExaminationStarted=FPlatformTime::Seconds();
    auto J=Command(TEXT("examine"));J->SetStringField(TEXT("id"),Id);Send(J);
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
    J->SetStringField(TEXT("target"), Skill == 0x55 || Skill == 0x343 ? PlayerId : (TargetId.IsEmpty() ? TEXT("0") : TargetId));
    Send(J);
}
void FAC2Network::StarterSkill(int32 Category)
{
    auto P = Mode->Characters.Active(); if (!P || Category < 0 || Category > 2) return;
    const uint32 Human[] = {0xb0,0xcf,0x60}, Lugian[] = {0x166,0x1a0,0x111}, Tumerok[] = {0x2cc,0x316,0x2a0};
    CastSkill((P->Race == 2 ? Lugian : P->Race == 4 ? Tumerok : Human)[Category]);
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
void FAC2Network::UseItem(int32 Index, uint32 EquipSlot)
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
        J->SetNumberField(TEXT("location"), EquipSlot?EquipSlot:D->Preferred);
        J->SetNumberField(TEXT("slot"), I.Cell);
        Send(J);
    }
    else
    {
        auto J = Command(TEXT("use"));
        if(I.Definition==0x47001582 && (TargetId.IsEmpty() || TargetId==TEXT("0")))
        {Mode->Notice=TEXT("Select the Menhir Portal, then use the glyph from your backpack.");return;}
        J->SetStringField(TEXT("item"), I.NetworkId);
        J->SetStringField(TEXT("target"), I.Definition==0x47001582 ? TargetId : TEXT("0"));
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
void FAC2Network::DropItem(int32 Index)
{
    const auto* P=Mode->Characters.Active();
    if(!IsWorld() || !P || !P->Items.IsValidIndex(Index))return;
    const auto& I=P->Items[Index];
    if(I.Equipped){Mode->Notice=TEXT("Move equipped items to your backpack before dropping them.");return;}
    auto J=Command(TEXT("drop_item"));J->SetStringField(TEXT("id"),I.NetworkId);
    J->SetNumberField(TEXT("from"),I.Cell);J->SetNumberField(TEXT("quantity"),I.Quantity);
    J->SetNumberField(TEXT("landblock"),Cell & 0xffff0000);Send(J);
}
bool FAC2Network::ArrivalCollisionReady() const
{
    for(const auto& Pair:Objects)
    {
        const auto& O=*Pair.Value;
        if(O.Content && O.Solid && O.Model && O.Placed && Mode->CellVisible(O.Cell) &&
            !O.Model->CollisionReady())return false;
    }
    return true;
}
void FAC2Network::Handle(const TSharedPtr<FJsonObject> &J)
{
    FString Event = S(J, TEXT("event")), Id = S(J, TEXT("id"));
    if(Event==TEXT("examination_error"))
    {ExaminationPending=false;ExaminationError=S(J,TEXT("message"));return;}
    if(Event==TEXT("examination"))
    {
        if(Id!=ExaminationId)return; // A slower reply must never replace a newer inspection.
        ExaminationPending=false;ExaminationRows.Reset();ExaminationError.Reset();
        FExaminationRow Row;
        auto Flush=[&](){if(!Row.Text.IsEmpty() || Row.Image)ExaminationRows.Add(Row);Row={};};
        const TArray<TSharedPtr<FJsonValue>>* Nodes=nullptr;
        if(J->TryGetArrayField(TEXT("nodes"),Nodes))for(const auto& Value:*Nodes)
        {
            const auto Node=Value->AsObject();if(!Node)continue;
            const uint32 Kind=N(Node,TEXT("kind"));
            if(Kind==0x40000006){Flush();continue;}
            if(Kind==0x40000005){Flush();Row.Image=N(Node,TEXT("image"));Flush();continue;}
            if(!Row.Appearance)Row.Appearance=N(Node,TEXT("appearance"));
            if(Kind==0x40000004)
            {
                const auto Info=Node->GetObjectField(TEXT("text"));
                FString Text=StringInfo(Info);
                // Localized noun annotations are grammar metadata, not part of the display name.
                if(Row.Appearance==0xbe5b6513 && N(Info,TEXT("table")))while(Text.EndsWith(TEXT("]")))
                {int32 Start;if(!Text.FindLastChar('[',Start) || Text.Len()-Start>8)break;Text.LeftInline(Start);}
                Row.Text+=Text;
            }
            else if(Kind==0x40000007)Row.Text+=TEXT("  ");
            else if(Kind==0x40000003)Row.Text+=Node->GetBoolField(TEXT("value"))?TEXT("Yes"):TEXT("No");
            else if(Kind==0x4000000a)Row.Text+=S(Node,TEXT("value"));
            else if(Kind==0x40000001 || Kind==0x40000002 || Kind==0x40000008)
                Row.Text+=FString::SanitizeFloat(N(Node,TEXT("value")));
        }
        Flush();return;
    }
    if (Event == TEXT("combat"))
    {
        Combat = J->GetBoolField(TEXT("enabled"));
        Mode->Notice=Combat?TEXT("Combat stance"):TEXT("Peace stance");
        return;
    }
    if(Event==TEXT("missile"))
    {
        auto Found=Objects.Find(Id);if(!Found || (*Found)->Instance!=int32(N(J,TEXT("instance"))) || Id==PlayerId)return;
        auto& O=**Found;auto Pose=J->GetObjectField(TEXT("placement"));auto Offset=J->GetObjectField(TEXT("offset"));
        const uint32 CellId=N(Pose,TEXT("cell"));const FVector Goal=AC2::BlockOrigin(CellId>>16)+V(Pose,TEXT("position"))*100;
        const FVector Velocity=V(J,TEXT("velocity"))*100,TargetOffset=V(Offset,TEXT("position"))*100;
        if(!CellId || !AC2Projectile::Valid(Goal,Velocity,TargetOffset))return;
        O.Cell=CellId;O.Goal=Goal;O.MissileVelocity=Velocity;O.MissileTarget=S(J,TEXT("target"));O.MissileOffset=TargetOffset;
        O.MissilePoint=AC2::BlockOrigin(uint32(N(Offset,TEXT("cell")))>>16)+O.MissileOffset;
        O.MissilePointKnown=J->GetBoolField(TEXT("initialized")) && O.MissileTarget==TEXT("0") && N(Offset,TEXT("cell"))!=0;
        O.Missile=true;O.MissileFinished=false;O.MissileAge=0;O.Solid=false;O.GroundedContent=false;
        if(Smoke)++SmokeMissiles;
        if(O.Actor.IsValid()){O.Actor->SetActorLocation(O.Goal);O.Actor->SetActorEnableCollision(false);}return;
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
        auto Found=Objects.Find(Id);if(!Found || (J->HasField(TEXT("instance")) && (*Found)->Instance!=int32(N(J,TEXT("instance")))))return;
        auto Player=Id==PlayerId?Pawn(Mode):nullptr;const uint32 Fx=N(J,TEXT("fx"));
        if(Event==TEXT("stop_fx"))
        {
            if(Player)Player->StopEffect(Fx);else if((*Found)->Model)(*Found)->Model->StopEffect(Fx);
            for(auto& Entry:Objects)if(Entry.Value->Parent==Id && Entry.Value->Model)Entry.Value->Model->StopEffect(Fx);
            return;
        }
        USceneComponent* Target=nullptr;const FString TargetIdValue=S(J,TEXT("target"));
        if(TargetIdValue==PlayerId){if(auto P=Pawn(Mode))Target=P->GetRootComponent();}
        else if(auto T=Objects.Find(TargetIdValue))if((*T)->Actor.IsValid())Target=(*T)->Actor->GetRootComponent();
        bool Played=Player?Player->PlayEffect(Fx,N(J,TEXT("strength"),1),Target):(*Found)->Model && (*Found)->Model->PlayEffect(Fx,N(J,TEXT("strength"),1),Target);
        if(!Played)for(auto& Entry:Objects)if(Entry.Value->Parent==Id && Entry.Value->Model)
            Played|=Entry.Value->Model->PlayEffect(Fx,N(J,TEXT("strength"),1),Target);
        if(Smoke)UE_LOG(LogTemp,Display,TEXT("AC2 server FX %08X played=%d player=%d"),Fx,Played,Player!=nullptr);
        if(Played)++PlayedEffects;return;
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
    if(Event==TEXT("behavior")||Event==TEXT("mode")||Event==TEXT("stop_behavior")||Event==TEXT("hit")||Event==TEXT("impulse")||Event==TEXT("motion"))
    {
        auto* Found=Objects.Find(Id);
        if(!Found || (J->HasField(TEXT("instance")) && int32(N(J,TEXT("instance")))!=(*Found)->Instance))return;
        auto& Obj=**Found;
        auto* Player=Id==PlayerId?Pawn(Mode):nullptr;
        const uint32 B=uint32(N(J,TEXT("behavior"))), M=uint32(N(J,TEXT("mode")));
        if(Event==TEXT("mode") && (M==0x41000002 || M==0x41000003))
        {
            const bool WasDoor=Obj.Door,WasOpen=Obj.DoorOpen;Obj.Door=true;Obj.DoorOpen=M==0x41000002;
            if(Obj.Model)
            {
                Obj.Model->SetAnimationState(AC2Motion::Ground,M);
                if(WasDoor && WasOpen!=Obj.DoorOpen)Obj.Model->PlayBehavior(Obj.DoorOpen?0x41000008:0x41000007,M,1,false);
            }
            if(Obj.Actor.IsValid())Obj.Actor->SetActorEnableCollision(!Obj.DoorOpen && Obj.Solid);
        }
        if(M){Obj.AnimMode=M;if(Player)Player->SetNativeMode(M);}
        if(Obj.Model)Obj.Model->SetAnimationState(AC2Motion::Ground,Obj.AnimMode);
        if(Event==TEXT("behavior"))
        {
            const bool IsAttack=B==AC2Motion::Attack||B==AC2Motion::MeleeNormal||B==0x410000b0||B==0x410000b9||B==0x410000bc||B==0x410000e2;
            bool Hold=false;J->TryGetBoolField(TEXT("hold"),Hold);Hold|=B==AC2Motion::Death;
            const bool Played=Player?Player->PlayBehavior(B,M,float(N(J,TEXT("rate"),1)),Hold):Obj.Model&&Obj.Model->PlayBehavior(B,M,float(N(J,TEXT("rate"),1)),Hold);
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
        AllowedCharacters=FMath::Clamp(int32(N(J,TEXT("allowed"))),1,32);
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
        else {
            SetStatus(TEXT("selection"), FString::Printf(TEXT("Character creation rejected (%d)"),
                                                         int32(N(J, TEXT("response")))));
            if(Smoke){UE_LOG(LogTemp,Error,TEXT("AC2 network smoke: server rejected creation"));FPlatformMisc::RequestExitWithStatus(false,1);}
        }
        return;
    }
    if (Event == TEXT("deleted"))
    {
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
        if (Speaker.IsEmpty() && Text == TEXT("[AC2E:map-travel-v1]"))
        {
            MapTravelEnabled = true;
            return;
        }
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
    if(Event==TEXT("inventory_remove"))
    {
        if(const int32* CellIndex=BagSlots.Find(Id))
        {
            const int32 Removed=*CellIndex;BagSlots.Remove(Id);
            for(auto& Entry:BagSlots)if(Entry.Value>Removed)--Entry.Value;
        }
        WornSlots.Remove(Id);
        if(auto P=Mode->Characters.Active())
        {
            P->Items.RemoveAll([&](const auto& I){return I.NetworkId==Id;});
            for(auto& I:P->Items)if(auto* Slot=BagSlots.Find(I.NetworkId))I.Cell=*Slot;
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
        if (J->HasField(TEXT("equipped")))
        {
            const uint32 Location = N(J,TEXT("equipped"));
            const int32 Slot = Location ? BagSlots.FindRef(Id) : int32(N(J,TEXT("slot")));
            if (Location)
            {
                BagSlots.Remove(Id); WornSlots.Add(Id,Location);
                for (auto& Entry:BagSlots) if(Entry.Value>Slot) --Entry.Value;
            }
            else
            {
                WornSlots.Remove(Id);
                for(auto& Entry:BagSlots) if(Entry.Key!=Id && Entry.Value>=Slot) ++Entry.Value;
                BagSlots.Add(Id,Slot);
            }
            if(auto P=Mode->Characters.Active()) for(auto& I:P->Items)
            { I.Equipped=WornSlots.FindRef(I.NetworkId); if(auto BagCell=BagSlots.Find(I.NetworkId)) I.Cell=*BagCell; }
            return;
        }
        if(J->HasField(TEXT("slot")))
        {
            const int32 Destination=N(J,TEXT("slot"));
            if(const auto* Previous=BagSlots.Find(Id))
            {
                const int32 Source=*Previous;
                for(auto& Entry:BagSlots)if(Entry.Key!=Id)
                {
                    if(Source<Destination && Entry.Value>Source && Entry.Value<=Destination)--Entry.Value;
                    else if(Source>Destination && Entry.Value>=Destination && Entry.Value<Source)++Entry.Value;
                }
            }
            BagSlots.Add(Id,Destination);
            if(auto P=Mode->Characters.Active())for(auto& I:P->Items)
                if(auto* Slot=BagSlots.Find(I.NetworkId)){I.Cell=*Slot;I.Equipped=WornSlots.FindRef(I.NetworkId);}
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
            if(J->HasField(TEXT("instance")) && (*E)->Instance!=int32(N(J,TEXT("instance"))))return;
            if ((*E)->Actor.IsValid())
                (*E)->Actor->Destroy();
            Objects.Remove(Id);
            if(TargetId==Id)TargetId.Reset();
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
        Obj.Door=Obj.DoorOpen=false;
        Obj.Missile=Obj.MissileFinished=Obj.MissilePointKnown=false;Obj.MissileAge=0;
        Obj.Parent=S(J,TEXT("parent")); Obj.Holding=uint32(N(J,TEXT("holding")));
        Obj.Name = StringInfo(O(J, TEXT("name")));
        Obj.Entity = N(J, TEXT("entity"));
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
    if (Event == TEXT("object") && !PlayerId.IsEmpty() &&
        (S(J, TEXT("container")) == PlayerId || S(J, TEXT("wielder")) == PlayerId))
    {
        if (auto P = Mode->Characters.Active())
        {
            uint32 Did = N(J, TEXT("entity"));
            Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory, Did);
            FAC2InventoryItem I;
            I.Definition = Did;
            const auto ItemVisual=O(J,TEXT("visual"));
            I.Visual=uint32(N(ItemVisual,TEXT("did")));I.Aspects=Aspects(ItemVisual);
            I.NetworkId = Id;
            I.Quantity = FMath::Max(1, int32(N(J, TEXT("quantity"), 1)));
            I.Cell = BagSlots.Contains(Id) ? BagSlots[Id] : P->Items.Num();
            I.Equipped = WornSlots.FindRef(Id);
            P->Items.RemoveAll([&](const FAC2InventoryItem &Existing) { return Existing.NetworkId == Id; });
            P->Items.Add(I);
        }
        if(Obj.Parent.IsEmpty()) return;
    }
    auto Pose = Event == TEXT("object") ? O(J, TEXT("placement")) : (Event == TEXT("position") ? J : nullptr);
    if(Event==TEXT("position") && Obj.Instance && int32(N(J,TEXT("instance")))!=Obj.Instance)return;
    if (Pose)
    {
        Obj.Cell = uint32(N(Pose, TEXT("cell")));
        Obj.Goal = AC2::BlockOrigin(Obj.Cell >> 16) + V(Pose, TEXT("position")) * 100;
        Obj.Yaw = 90 - N(Pose, TEXT("heading"));
        Obj.Rotation=FRotator(0,Obj.Yaw-90,0).Quaternion();
        const TArray<TSharedPtr<FJsonValue>>* Rotation;
        if(Pose->TryGetArrayField(TEXT("rotation"),Rotation) && Rotation->Num()==4)
        {
            const FQuat Native((*Rotation)[1]->AsNumber(),(*Rotation)[2]->AsNumber(),
                (*Rotation)[3]->AsNumber(),(*Rotation)[0]->AsNumber());
            if(!Native.ContainsNaN() && Native.SizeSquared()>SMALL_NUMBER)Obj.Rotation=Native.GetNormalized();
        }
        Obj.Placed = true;
        if (Event == TEXT("object") && (Obj.Entity == 0x4700084d || Obj.Entity == 0x47001454) &&
            (Obj.Name.IsEmpty() || Obj.Name == TEXT("Portal") || Obj.Name.StartsWith(TEXT("Item "))))
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
    if(IsWorld() && !ShopVendor.IsEmpty() && Time-LastShopRefresh>10)
    {auto J=Command(TEXT("shop_refresh"));J->SetStringField(TEXT("vendor"),ShopVendor);Send(J);LastShopRefresh=Time;}

    if(ExaminationPending && FPlatformTime::Seconds()-ExaminationStarted>8)
    {ExaminationPending=false;ExaminationError=TEXT("The server did not provide examination information. Right-click to retry.");}
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
        if (E.Value->Actor.IsValid() && E.Value->Parent.IsEmpty())
        {
            auto &Obj = *E.Value;
            auto A = Obj.Actor.Get();
            FVector Old = A->GetActorLocation();
            if(Obj.Missile && !Obj.MissileFinished)
            {
                Obj.MissileAge+=Delta;FVector Destination=Obj.MissilePoint;bool TargetKnown=Obj.MissilePointKnown;
                if(Obj.MissileTarget==PlayerId && P)
                {Destination=P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())+Obj.MissileOffset;TargetKnown=true;}
                else if(auto Target=Objects.Find(Obj.MissileTarget))if((*Target)->Actor.IsValid())
                {Destination=(*Target)->Actor->GetActorLocation()+Obj.MissileOffset;TargetKnown=true;}
                bool Arrived=false;
                const FVector Direction=TargetKnown?(Destination-Old).GetSafeNormal():Obj.MissileVelocity.GetSafeNormal();
                Obj.Goal=AC2Projectile::Advance(Old,Obj.MissileVelocity,TargetKnown?&Destination:nullptr,Delta,Arrived);
                if(!Direction.IsNearlyZero())A->SetActorRotation(AC2Projectile::Facing(Direction));
                if(Arrived || Obj.MissileAge>15 || (Obj.MissileTarget!=TEXT("0") && !TargetKnown))Obj.MissileFinished=true;
                if(Obj.MissileFinished)Obj.Model.Reset(); // Destroy detached trail emitters as well as hiding the mesh.
            }
            FVector Next = FVector::DistSquared(Old, Obj.Goal) > FMath::Square(1000.)
                               ? Obj.Goal
                               : FMath::VInterpTo(Old, Obj.Goal, Delta, 12);
            if(Obj.Missile)Next=Obj.Goal;
            A->SetActorLocation(Next);
            if(!Obj.Missile)A->SetActorRotation(
                FMath::QInterpTo(A->GetActorQuat(), Obj.Rotation, Delta, 12));
            const bool Visible =
                P && !Obj.MissileFinished && Mode->CellVisible(Obj.Cell ? Obj.Cell : Cell) &&
                FVector::DistSquared(Next, P->GetActorLocation()) < FMath::Square(30000.);
            A->SetActorHiddenInGame(!Visible);
            A->SetActorEnableCollision(Visible && Obj.Solid && !Obj.DoorOpen);
            if(Visible && Obj.GroundedContent && Obj.Body.IsValid() && Time>=Obj.NextGroundCheck)
            {
                Obj.NextGroundCheck=Time+.25;
                FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2NPCFloor),true,A);
                if(Mode->GetWorld()->LineTraceSingleByObjectType(Floor,Next+FVector(0,0,100),Next-FVector(0,0,200),
                    FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Floor.ImpactNormal.Z>.57f)
                {
                    // Only correct the presentation offset, never the network pose.
                    const double ScaleZ=A->GetActorScale3D().Z;
                    Obj.Body->SetRelativeLocation(FVector(0,0,(Floor.ImpactPoint.Z-Next.Z+2)/ScaleZ-Obj.FootZ));
                }
            }
            if (Visible && Obj.Model)
            {
                FVector Velocity = Delta > 0 ? (Next - Old) / Delta : FVector::ZeroVector;
                FVector Local = A->GetActorTransform().InverseTransformVectorNoScale(Velocity);
                Obj.Model->Animate(Time, Velocity.Size2D(), nullptr, &Local);
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
        if(Obj.Parent==PlayerId && P) Attached=P->HoldingTransform(Obj.Holding,Socket);
        else if(auto Parent=Objects.Find(Obj.Parent))
            if((*Parent)->Actor.IsValid() && !(*Parent)->Actor->IsHidden() && (*Parent)->Model && (*Parent)->Model->AnimatedHoldingLocation(Obj.Holding,Socket))
            {Socket=Socket*((*Parent)->Body.IsValid()?(*Parent)->Body->GetComponentTransform():(*Parent)->Actor->GetActorTransform());Attached=true;}
        Obj.Actor->SetActorHiddenInGame(!Attached);
        if(Attached)
        {
            if(Obj.Parent==PlayerId)SmokeHeld=true;
            Obj.Actor->SetActorTransform(FTransform(FQuat::Identity,FVector::ZeroVector,Obj.Scale*Obj.EntityScale)*Socket);
            if(Obj.Model)Obj.Model->Animate(Time,0);
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
                if(!H || !H->CharacterUI || !H->CharacterUI->SmokeSelection(SelectionSmokeStep,Error))
                {UE_LOG(LogTemp,Error,TEXT("Selection pointer smoke step %d: %s"),SelectionSmokeStep,*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2Login-selection-%02d.png"),SelectionSmokeStep),true,false);
                Started=0;
                if(++SelectionSmokeStep<=18)return;
                SmokeEntered=true;
                return;
            }
            SmokeEntered = true;
            Enter(Mode->Characters.Characters[0].Id);
        }
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_FOLIAGE_SMOKE"))==TEXT("1"))
        {
            FoliageSmokeTick();return;
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
            const FString Vendor=LexToString(0x8140000000030900ull),Door=LexToString(0x8140000000030901ull);
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
                Finish(true,TEXT("DAT shop pointer sale/purchase/close and native door animation/collision"));return;
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
        const bool HasLoot = Profile && !Profile->Items.IsEmpty() && Profile->Experience > 0;
        if ((IsWorld() && SmokeAttacked && HasLoot && Started > 6) || Started > 120)
        {
            bool OK = IsWorld() && ReceivedChats > 0 && P && P->HasModel() && Objects.Num() > 1 && HasLoot && SmokeRadar && PlayerAttacks>0 && MobAttacks>0 && HitEvents>0 && DeathBehaviors>0 && FailedAttacks==0 && (!VerifyEquipment || (SmokeHeld && MagicAttacks>0 && SmokeArmor && PlayedEffects>0 && SmokeParticles>0 && SmokeCastParticles>0));
            if(VerifyEquipment)OK &= SmokeMissiles>0 && SmokeMissileUpdates>0 && SmokeMissileParticles>0;
            FString Report = FPaths::ProjectSavedDir() / TEXT("Diagnostics/AC2Network-smoke.txt");
            if(OK && VerifyEquipment && InventorySmokeStep<14)
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
            UE_LOG(LogTemp, Display, TEXT("AC2 network smoke %s: objects=%d chat=%d loot=%d xp=%d"),
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
