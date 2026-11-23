#include "AC2Aliases.h"
#include "AC2Chat.h"
#include "AC2ShortcutData.h"

FAC2AliasRequest FAC2AliasRequest::Parse(const FString& Input)
{
    FAC2AliasRequest R;
    FString Line=Input.TrimStartAndEnd(),Command,Args;
    if(!Line.Split(TEXT(" "),&Command,&Args))Command=Line;
    if(Command.Equals(TEXT("/help"),ESearchCase::IgnoreCase) && Args.TrimStartAndEnd().Equals(TEXT("alias"),ESearchCase::IgnoreCase)){
        R.Recognized=true;R.Command=EAC2AliasCommand::Help;return R;
    }
    if(!Command.Equals(TEXT("/alias"),ESearchCase::IgnoreCase))return R;
    R.Recognized=true;
    R.Error=TEXT("Usage: /alias [add] ;name <replacement> | remove ;name | list | clear | clearlist | shortcut ;name <1-10>");
    if(Input.Len()>1024)return R;
    for(TCHAR C:Input)if(C<32 || C==127)return R;
    TArray<FString> Words;
    if(!AC2ChatWords(Args,Words) || Words.IsEmpty())return R;
    const FString Verb=Words[0].ToLower();
    if(Verb==TEXT("list") || Verb==TEXT("clear") || Verb==TEXT("clearlist")){
        if(Words.Num()!=1)return R;
        R.Command=Verb==TEXT("list")?EAC2AliasCommand::List:EAC2AliasCommand::Clear;
    }else if(Verb==TEXT("remove") || Verb==TEXT("shortcut")){
        const bool Shortcut=Verb==TEXT("shortcut");
        if(Words.Num()!=(Shortcut?3:2))return R;
        R.Command=Shortcut?EAC2AliasCommand::Shortcut:EAC2AliasCommand::Remove;
        R.Name=Words[1].ToLower();
        if(!FAC2Aliases::ValidName(R.Name))return R;
        if(Shortcut){
            if(Words[2].Len()>2)return R;
            for(TCHAR C:Words[2])if(C<TEXT('0') || C>TEXT('9'))return R;
            const int32 Location=FCString::Atoi(*Words[2]);
            if(Location<1 || Location>10)return R;
            R.Slot=Location-1;
        }
    }else{
        if(Verb==TEXT("add"))Words.RemoveAt(0);
        if(Words.Num()<2)return R;
        R.Name=Words[0].ToLower();Words.RemoveAt(0);
        if(!FAC2Aliases::ValidName(R.Name)){
            R.Error=TEXT("Alias names must start with a semicolon and contain at least one more character.");return R;
        }
        R.Text=FString::Join(Words,TEXT(" "));
        if(FAC2Shortcut::ForText(R.Text).IsEmpty())return R;
    }
    if((R.Command==EAC2AliasCommand::Add || R.Command==EAC2AliasCommand::Remove) && FAC2Aliases::Reserved(R.Name)){
        R.Error=TEXT("You cannot change this predefined alias.");return R;
    }
    R.Error.Reset();return R;
}

bool FAC2Aliases::Reserved(const FString& Name)
{
    return Name.Equals(TEXT(";target"),ESearchCase::IgnoreCase) || Name.Equals(TEXT(";loc"),ESearchCase::IgnoreCase);
}
bool FAC2Aliases::ValidName(const FString& Name)
{
    // The script checks length and leading semicolon. The native expander,
    // separately, only consumes alphanumeric characters after that semicolon.
    return Name.Len()>=2 && Name.StartsWith(TEXT(";")) && !FAC2Shortcut::ForText(Name).IsEmpty();
}
bool FAC2Aliases::Set(const FString& Name,const FString& Text)
{
    if(!ValidName(Name) || Reserved(Name) || FAC2Shortcut::ForText(Text).IsEmpty())return false;
    const FString Key=Name.ToLower();
    if(Values.Num()>=MaxAliases && !Values.Contains(Key))return false;
    Values.Add(Key,Text);return true;
}
bool FAC2Aliases::Contains(const FString& Name) const
{
    return Reserved(Name) || Values.Contains(Name.ToLower());
}
FString FAC2Aliases::Encode() const
{
    TArray<FString> Keys,Rows;Values.GetKeys(Keys);Keys.Sort();
    for(const auto& Key:Keys)Rows.Add(FAC2Shortcut::ForText(Key).Encode()+TEXT("|")+FAC2Shortcut::ForText(Values[Key]).Encode());
    return FString::Join(Rows,TEXT(","));
}
void FAC2Aliases::Decode(const FString& Saved)
{
    Values.Reset();
    if(Saved.Len()>2*1024*1024)return;
    TArray<FString> Rows;Saved.ParseIntoArray(Rows,TEXT(","),true);
    for(const auto& Row:Rows){
        FString Key,Value;
        if(!Row.Split(TEXT("|"),&Key,&Value))continue;
        Set(FAC2Shortcut::Decode(Key).Text,FAC2Shortcut::Decode(Value).Text);
    }
}
bool FAC2Aliases::Expand(const FString& Input,const FString& Target,const FString& Location,FString& Output,FString& Error) const
{
    Output.Reset();Error.Reset();
    // Match the bridge's existing input bound. Never send a truncated expansion
    // that might turn into a different command; also bound exponential chains.
    constexpr int32 MaxText=2048,MaxExpansions=256,MaxDepth=32;
    if(Input.Len()>MaxText){Error=TEXT("Alias expansion is too long.");return false;}
    for(TCHAR C:Input)if(C<32 || C==127){Error=TEXT("Enter one line of text.");return false;}
    // CPlayerShell::ExpandableText exempts /alias so definitions retain tokens
    // and /alias shortcut stores the name rather than its current replacement.
    int32 Slash=INDEX_NONE;
    if(Input.FindChar(TEXT('/'),Slash)){
        const FString Tail=Input.Mid(Slash);
        FString Command,Rest;
        if(!Tail.Split(TEXT(" "),&Command,&Rest))Command=Tail;
        if(Command.Equals(TEXT("/alias"),ESearchCase::IgnoreCase)){Output=Input;return true;}
    }
    TSet<FString> Stack;
    int32 Expansions=0;
    TFunction<bool(const FString&,FString&)> Walk;
    Walk=[&](const FString& Text,FString& Result){
        for(int32 At=0;At<Text.Len();){
            if(Text[At]!=TEXT(';')){Result.AppendChar(Text[At++]);}
            else{
                const int32 Start=At++;
                while(At<Text.Len() && FChar::IsAlnum(Text[At]))++At;
                const FString Token=Text.Mid(Start,At-Start),Key=Token.ToLower();
                const FString* Value=Values.Find(Key);
                if(Key==TEXT(";target"))Value=Target.IsEmpty()?nullptr:&Target;
                else if(Key==TEXT(";loc"))Value=Location.IsEmpty()?nullptr:&Location;
                if(Value && !Stack.Contains(Key)){
                    if(Stack.Num()>=MaxDepth || ++Expansions>MaxExpansions)return false;
                    Stack.Add(Key);
                    if(!Walk(*Value,Result))return false;
                    Stack.Remove(Key);
                }else Result+=Token; // Missing and cyclic references remain literal, as in retail.
            }
            if(Result.Len()>MaxText)return false;
        }
        return true;
    };
    if(Walk(Input,Output))return true;
    Output.Reset();Error=TEXT("Alias expansion exceeds the text or nesting limit.");return false;
}
