#include "AC2Network.h"
#include "AC2GameMode.h"

namespace
{
bool Word(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,uint32& Out)
{
    double Value=0;if(!J || !J->TryGetNumberField(Key,Value) || !FMath::IsFinite(Value) || Value<0 || Value>MAX_uint32 || FMath::FloorToDouble(Value)!=Value)return false;
    Out=uint32(Value);return true;
}
bool ReadBinding(const TSharedPtr<FJsonObject>& J,FAC2Shortcut& Out)
{
    FString Kind,Text;uint32 Value=0;
    if(!J || !J->TryGetStringField(TEXT("kind"),Kind))return false;
    if(Kind==TEXT("empty")){Out={};return true;}
    if(Kind==TEXT("text")){
        if(!J->TryGetStringField(TEXT("text"),Text))return false;
        Out=FAC2Shortcut::ForText(Text);return !Out.IsEmpty();
    }
    if(Kind==TEXT("item")){
        if(!J->TryGetStringField(TEXT("id"),Text))return false;
        Out=FAC2Shortcut::Decode(TEXT("i:")+Text);return !Out.Item.IsEmpty();
    }
    if(!Word(J,TEXT("value"),Value))return false;
    if(Kind==TEXT("enum")){Out=FAC2Shortcut::ForEnumeration(Value);return true;}
    if(Kind==TEXT("skill") && Value){Out=FAC2Shortcut::ForSkill(Value);return true;}
    if(Kind==TEXT("recipe") && Value>>24==0x6f){Out=FAC2Shortcut::ForRecipe(Value);return true;}
    return false;
}
TSharedRef<FJsonObject> WriteBinding(const FAC2Shortcut& B)
{
    auto J=MakeShared<FJsonObject>();
    if(B.IsEnumeration){J->SetStringField(TEXT("kind"),TEXT("enum"));J->SetNumberField(TEXT("value"),B.Enumeration);}
    else if(!B.Text.IsEmpty()){J->SetStringField(TEXT("kind"),TEXT("text"));J->SetStringField(TEXT("text"),B.Text);}
    else if(!B.Item.IsEmpty()){J->SetStringField(TEXT("kind"),TEXT("item"));J->SetStringField(TEXT("id"),B.Item);}
    else if(B.Recipe){J->SetStringField(TEXT("kind"),TEXT("recipe"));J->SetNumberField(TEXT("value"),B.Recipe);}
    else if(B.Skill){J->SetStringField(TEXT("kind"),TEXT("skill"));J->SetNumberField(TEXT("value"),B.Skill);}
    else J->SetStringField(TEXT("kind"),TEXT("empty"));
    return J;
}
int32 Bytes(const FAC2Shortcut& B)
{
    return !B.Text.IsEmpty()?4+Align(2+B.Text.Len()*2,4):!B.Item.IsEmpty()?12:B.IsEmpty()?4:8;
}
}

void FAC2Network::ReceiveShortcuts(const TSharedPtr<FJsonObject>& J)
{
    if(Phase!=TEXT("entering") && !IsWorld())return;
    const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
    TArray<FAC2Shortcut> Next;Next.SetNum(100);
    bool Valid=J->TryGetArrayField(TEXT("items"),Items) && Items->Num()<=100;
    if(Valid)for(int32 I=0;I<Items->Num();++I){
        if(!(*Items)[I].IsValid() || !ReadBinding((*Items)[I]->AsObject(),Next[I])){Valid=false;break;}
    }
    if(!Valid){Mode->Notice=TEXT("Could not restore the server hotbar: unsupported or invalid shortcut data.");return;}
    ServerShortcuts=MoveTemp(Next);++ShortcutRevision;
}
bool FAC2Network::SaveShortcuts(const FAC2Shortcut* Bindings)
{
    const auto* P=Mode->Characters.Active();
    if(!Write || !IsWorld() || !P || PlayerId!=P->Id || ServerShortcuts.Num()!=100)return false;
    int32 Size=4;
    for(int32 I=0;I<100;++I){
        FAC2Shortcut Check;
        if(!ReadBinding(WriteBinding(Bindings[I]),Check) || !(Check==Bindings[I]))return false;
        Size+=Bytes(Bindings[I]);
    }
    if(Size>16384){Mode->Notice=TEXT("Hotbar not saved: the combined shortcut text exceeds the server's 16 KiB limit.");return false;}
    // Shrinking writes precede growing writes, so swaps cannot temporarily
    // exceed the server's bound even when the final bar fits.
    for(int32 Pass=0;Pass<2;++Pass)for(int32 I=0;I<100;++I){
        const auto& B=Bindings[I];
        if(B==ServerShortcuts[I] || (Bytes(B)>Bytes(ServerShortcuts[I]))!=(Pass==1))continue;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("command"),TEXT("shortcut"));
        J->SetNumberField(TEXT("slot"),I);J->SetObjectField(TEXT("binding"),WriteBinding(B));Send(J);
        ServerShortcuts[I]=B;
    }
    return true;
}
