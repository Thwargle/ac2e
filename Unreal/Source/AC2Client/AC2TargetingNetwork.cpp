#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2SkillData.h"
#include "GameFramework/PlayerController.h"

AC2Radar::FLook FAC2Network::SelectionLook(uint32 Entity)
{
    if(!SelectionData){
        auto Data=MakeUnique<FAC2RadarData>();FString Error;
        if(!Data->Open(Mode->DatDirectory,Error))return {};
        SelectionData=MoveTemp(Data);
    }
    return SelectionData->Look(Entity);
}

FString FAC2Network::ReplacementTarget(const FString& Excluded)
{
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();APawn* Player=PC?PC->GetPawn():nullptr;
    if(!IsWorld() || Dead || !Player)return {};
    const bool Indoors=(Cell&0xffff)>64;
    const double Range=Indoors?5000.:7500.;
    FString Best;double BestDistance=MAX_dbl;uint64 BestId=MAX_uint64;
    for(const auto& Entry:Objects){
        const auto& O=*Entry.Value;
        if(Entry.Key==Excluded || !Targetable(Entry.Key,O) || O.Health==0 ||
           (!O.Summoner.IsEmpty() && O.Summoner!=TEXT("0")) ||
           Fellows.ContainsByPredicate([&](const auto& F){return F.Id==Entry.Key;}))continue;
        if(((O.Cell&0xffff)>64)!=Indoors || (Indoors && (O.Cell>>16)!=(Cell>>16)))continue;
        // Current server is PvE: use native class inheritance, excluding NPCs,
        // players and summoned companions. Full retail PK/faction rules remain separate.
        const auto Look=SelectionLook(O.Entity);
        if(Look.Category!=AC2Radar::Monsters || !O.RadarSelectable.Get(Look.Selectable))continue;
        const FVector Delta=O.Actor->GetActorLocation()-Player->GetActorLocation();
        if(Delta.SizeSquared2D()>Range*Range)continue;
        // Legions selection 00423B70: XY range, XY distance + 1.2 * abs(Z).
        // gmSelectionUI::Farther (004236C0) breaks ties by unsigned IID.
        const double Distance=Delta.Size2D()+1.2*FMath::Abs(Delta.Z);
        const uint64 ObjectId=FCString::Strtoui64(*Entry.Key,nullptr,10);
        if(Distance<BestDistance || (Distance==BestDistance && ObjectId<BestId)){
            Best=Entry.Key;BestDistance=Distance;BestId=ObjectId;
        }
    }
    return Best;
}

void FAC2Network::SelectionUnavailable(const FString& Id,bool Deleted)
{
    if(!IsWorld() || TargetId!=Id || Id==PlayerId)return;
    // Admin gmSelectionUI::RecvNotice_UpdateSelectability (00424670) and
    // RecvNotice_BeingDeleted (00424720); no replacement on health/death alone.
    if(!Deleted)ClearTarget();
    if(!Deleted || AutoTarget){
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("command"),TEXT("stop_target"));
        J->SetStringField(TEXT("id"),Id);Send(J);
    }
    if(AutoTarget){const FString Next=ReplacementTarget(Id);if(!Next.IsEmpty())SelectTarget(Next);}
    if(TargetId==Id)ClearTarget();
}

void FAC2Network::ReceiveStoryTarget(const TSharedPtr<FJsonObject>& Event)
{
    // Legions StoryManager::DoStoryHook (00503000), followed by
    // gmSelectionUI::RecvNotice_E_LastAttacker (004235A0). This is independent
    // of floating damage text, health changes, and the combat stance.
    FString Target,Owner,Attacker;double Stamp=0,Skill=0;
    if(!IsWorld() || Dead || !Event->TryGetStringField(TEXT("id"),Target) || Target!=PlayerId ||
       !Event->TryGetStringField(TEXT("owner"),Owner) || !Event->TryGetStringField(TEXT("attacker"),Attacker) ||
       Attacker.IsEmpty() || Attacker==TEXT("0") || Attacker==PlayerId ||
       !Event->TryGetNumberField(TEXT("instance"),Stamp) || !FMath::IsFinite(Stamp) || Stamp<0 || Stamp>MAX_uint16 || FMath::FloorToDouble(Stamp)!=Stamp ||
       !Event->TryGetNumberField(TEXT("skill"),Skill) || !FMath::IsFinite(Skill) || Skill<1 || Skill>MAX_uint32 || FMath::FloorToDouble(Skill)!=Skill)return;
    const auto* Source=Objects.Find(Owner);
    if(!Source || (*Source)->Instance!=int32(Stamp))return;
    if(!CombatSkillData){
        CombatSkillData=MakeUnique<FAC2SkillData>();
        if(!CombatSkillData->Open(Mode->DatDirectory)){CombatSkillData.Reset();return;}
    }
    const auto& Info=CombatSkillData->Get(uint32(Skill));
    if(!Info.Valid || !Info.Harmful)return;
    LastAttackerId=Attacker;
    const auto* Object=Objects.Find(Attacker);
    if(AutoTarget && TargetId.IsEmpty() && Object && (*Object)->RadarSelectable.Get(true))
        SelectTarget(Attacker);
}
