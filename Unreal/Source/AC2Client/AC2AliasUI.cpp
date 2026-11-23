#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2MapData.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

void SAC2GameUI::LoadAliases()
{
    const auto* Player=Mode.IsValid()?Mode->Characters.Active():nullptr;
    const FString Owner=Player?TEXT("AC2.Aliases.")+Player->Id:FString();
    if(AliasOwner==Owner)return;
    AliasOwner=Owner;Aliases.Values.Reset();
    FString Saved;
    if(!Owner.IsEmpty() && GConfig->GetString(*Owner,TEXT("EntriesV1"),Saved,GGameUserSettingsIni))Aliases.Decode(Saved);
}
void SAC2GameUI::SaveAliases()
{
    if(AliasOwner.IsEmpty() || TestingAliases)return;
    GConfig->SetString(*AliasOwner,TEXT("EntriesV1"),*Aliases.Encode(),GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
bool SAC2GameUI::HandleAliasCommand(const FAC2AliasRequest& R)
{
    if(!R.Recognized)return false;
    if(!R.Error.IsEmpty()){Log.Add(EAC2ChatType::Error,R.Error);return true;}
    if(Frontend || !Mode.IsValid() || !Mode->InWorld() || !Mode->Characters.Active())return true;
    LoadAliases();
    switch(R.Command){
    case EAC2AliasCommand::Help:{
        FAC2DatArchive Local;FString Help,Error;
        if(Local.Open(FPaths::Combine(Mode->DatDirectory,TEXT("local_English.dat")),3,Error))
            Help=AC2ReadUIString(Local,0x250007c8,0x09620f79);
        if(Help.IsEmpty())Help=TEXT("/alias [add] ;name <replacement>\n/alias remove ;name\n/alias shortcut ;name <1-10>\n/alias list\n/alias clear\n/alias clearlist: same as clear.");
        TArray<FString> Lines;Help.ParseIntoArrayLines(Lines);
        for(const auto& Line:Lines)Log.Add(EAC2ChatType::System,Line);
        break;}
    case EAC2AliasCommand::Add:
        if(Aliases.Set(R.Name,R.Text)){
            SaveAliases();Log.Add(EAC2ChatType::System,TEXT("Alias added: ")+R.Name+TEXT(" = ")+R.Text);
        }else Log.Add(EAC2ChatType::Error,TEXT("Cannot add alias. Remove an unused alias before adding another (limit 256)."));
        break;
    case EAC2AliasCommand::Remove:
        if(Aliases.Values.Remove(R.Name)){
            SaveAliases();Log.Add(EAC2ChatType::System,TEXT("Alias removed: ")+R.Name);
        }else Log.Add(EAC2ChatType::Error,TEXT("There is no such alias as ")+R.Name+TEXT("."));
        break;
    case EAC2AliasCommand::List:{
        Log.Add(EAC2ChatType::System,TEXT("Predefined aliases:"));
        Log.Add(EAC2ChatType::System,TEXT(";target = The name of your current selection"));
        Log.Add(EAC2ChatType::System,TEXT(";loc = Your Location"));
        Log.Add(EAC2ChatType::System,TEXT("Your aliases:"));
        TArray<FString> Keys;Aliases.Values.GetKeys(Keys);Keys.Sort();
        for(const auto& Key:Keys)Log.Add(EAC2ChatType::System,Key+TEXT(" = ")+Aliases.Values[Key]);
        if(Keys.IsEmpty())Log.Add(EAC2ChatType::System,TEXT("No user aliases defined."));
        break;}
    case EAC2AliasCommand::Clear:
        Aliases.Values.Reset();SaveAliases();Log.Add(EAC2ChatType::System,TEXT("Alias list cleared."));break;
    case EAC2AliasCommand::Shortcut:
        if(!Aliases.Contains(R.Name)){Log.Add(EAC2ChatType::Error,TEXT("Cannot add shortcut: ")+R.Name+TEXT(" is not an alias."));break;}
        LoadShortcuts();
        if(const int32 Index=FAC2ShortcutGrid::Index(ShortcutTopRow,R.Slot,1);Index!=INDEX_NONE){
            // CPlayer sets ShortcutInfo::Alias to the original name (local 12),
            // not the InqAlias result (local 10). Resolve again on each click.
            Shortcuts[Index]=FAC2Shortcut::ForText(R.Name);SaveShortcuts();Rebuild();
            Log.Add(EAC2ChatType::System,FString::Printf(TEXT("Shortcut to %s added in location %d on bar %d."),*R.Name,R.Slot+1,ShortcutTopRow+1));
        }
        break;
    }
    return true;
}
bool SAC2GameUI::ExpandAliases(const FString& Text,FString& Expanded)
{
    LoadAliases();
    FString Target,Location,Error;
    if(Mode.IsValid() && Mode->InWorld()){
        if(Mode->Network){
            const auto& Net=*Mode->Network;
            Target=Net.ObjectName(Net.TargetId);
            if(Target.IsEmpty() && !Net.TargetId.IsEmpty() && Net.TargetId==Net.PlayerId)
                if(const auto* Player=Mode->Characters.Active())Target=Player->Name;
        }else if(IsValid(Mode->Target))Target=Mode->Target->DisplayName;
        if(const auto* PC=Mode->GetWorld()->GetFirstPlayerController())
            if(const APawn* Pawn=PC->GetPawn())Location=FAC2MapData::Coordinates(FAC2MapData::Landscape(Pawn->GetActorLocation()));
    }
    if(Aliases.Expand(Text,Target,Location,Expanded,Error))return true;
    Log.Add(EAC2ChatType::Error,Error);return false;
}
