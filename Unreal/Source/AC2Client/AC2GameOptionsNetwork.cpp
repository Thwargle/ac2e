#include "AC2Network.h"
#include "AC2GameMode.h"

namespace
{
bool OptionWord(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,uint32& Out)
{
    double V=0;
    if(!J->TryGetNumberField(Key,V) || !FMath::IsFinite(V) || V<0 || V>MAX_uint32 || FMath::FloorToDouble(V)!=V)return false;
    Out=uint32(V);return true;
}
}
void FAC2Network::ReceiveGameOptions(const TSharedPtr<FJsonObject>& J)
{
    if(Phase!=TEXT("entering") && !IsWorld())return;
    auto Next=ServerGameOptions;uint32 Fields=0;double Range=0;
    if(!OptionWord(J,TEXT("fields"),Fields) || (Fields & ~FAC2GameOptions::All) ||
       ((Fields & 0x80) && !OptionWord(J,TEXT("bits"),Next.Bits)) ||
       ((Fields & 0x20) && !OptionWord(J,TEXT("radar"),Next.Radar)) ||
       ((Fields & 0x1000) && (!OptionWord(J,TEXT("height"),Next.Height) || Next.Height<1 || Next.Height>5)) ||
       ((Fields & 8) && (!J->TryGetNumberField(TEXT("range"),Range) || !FMath::IsFinite(Range) || Range>MAX_flt || (Range<0 && Range!=-1)))){
        Mode->Notice=TEXT("Could not restore invalid server game options.");return;
    }
    if(Fields & 8)Next.Range=float(Range);
    Next.Fields|=Fields;ServerGameOptions=Next;++GameOptionsRevision;
    // Apply before the next HUD tick: a combat story can follow this profile in
    // the same receive batch. An absent field must not overwrite local defaults.
    if(Fields & 0x80)AutoTarget=(Next.Bits & 0x20)!=0;
}
bool FAC2Network::SaveGameOptions(const FAC2GameOptions& Next)
{
    const auto* P=Mode->Characters.Active();
    if(!Write || !IsWorld() || !P || PlayerId!=P->Id || !GameOptionsRevision || Next.Fields!=FAC2GameOptions::All ||
       Next.Height<1 || Next.Height>5 || !FMath::IsFinite(Next.Range) || (Next.Range<0 && Next.Range!=-1))return false;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("command"),TEXT("game_options"));
    J->SetNumberField(TEXT("bits"),Next.Bits);J->SetNumberField(TEXT("radar"),Next.Radar);
    J->SetNumberField(TEXT("height"),Next.Height);J->SetNumberField(TEXT("range"),Next.Range);
    Send(J);ServerGameOptions=Next;AutoTarget=(Next.Bits & 0x20)!=0;return true;
}
