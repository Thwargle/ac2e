#include "AC2Chat.h"
#include "AC2ShortcutData.h"

uint32 AC2ChatColorIndex(uint32 Type,EAC2ChatType Fallback)
{
    if(!Type)Type=Fallback==EAC2ChatType::Error?1:Fallback==EAC2ChatType::Combat?2:Fallback==EAC2ChatType::Local?16:8;
    // ChatWindow::GetChatMessageInfo uses the first matching TextType in
    // FlushChatFontOptions order (a qualified tell is still tell-colored).
    if(Type&1)return 0;
    if(Type&2)return 1;
    if(Type&4)return 2;
    if(Type&8)return 3;
    if(Type&0x10)return 4;
    if(Type&0x20)return 5;
    if(Type&0x40)return 6;
    if(Type&0x80)return 4;
    if(Type&0x10000)return 7;
    if(Type&0x20000)return 8;
    if(Type&0x7c0000)return 4;
    if(Type&0x100)return 9;
    return 4;
}

bool AC2ChatWords(const FString& Args,TArray<FString>& Words)
{
    Words.Reset();
    for(int32 At=0;At<Args.Len();){
        if(Args[At]==TEXT(' ')){++At;continue;}
        FString Word;
        if(Args[At]==TEXT('"')){
            const int32 Start=++At;
            while(At<Args.Len() && Args[At]!=TEXT('"'))++At;
            if(At==Args.Len())return false;
            Word=Args.Mid(Start,At-Start);++At;
            if(At<Args.Len() && Args[At]!=TEXT(' '))return false;
        }else{
            const int32 Start=At;
            while(At<Args.Len() && Args[At]!=TEXT(' '))++At;
            Word=Args.Mid(Start,At-Start);
        }
        if(!Word.IsEmpty())Words.Add(MoveTemp(Word));
    }
    return true;
}

FAC2ShortcutRequest FAC2ShortcutRequest::Parse(const FString& Input)
{
    FAC2ShortcutRequest Result;
    FString Line=Input.TrimStartAndEnd(),Command,Args;
    if(!Line.Split(TEXT(" "),&Command,&Args))Command=Line;
    if(!Command.Equals(TEXT("/shortcut"),ESearchCase::IgnoreCase))return Result;
    Result.Recognized=true;
    Result.Error=TEXT("Usage: /shortcut numbers <on|off> or /shortcut text <1-10> <message or command>");
    if(Input.Len()>1024)return Result;
    for(TCHAR C:Input)if(C<32 || C==127)return Result;
    // Native CPlayer joins the remaining text tokens with spaces. It also
    // accepts /shortcut <location> <text>.
    TArray<FString> Words;
    if(!AC2ChatWords(Args,Words))return Result;
    if(!Words.IsEmpty() && Words[0].Equals(TEXT("numbers"),ESearchCase::IgnoreCase)){
        if(Words.Num()!=2)return Result;
        if(Words[1].Equals(TEXT("on"),ESearchCase::IgnoreCase))Result.ShowNumbers=true;
        else if(Words[1].Equals(TEXT("off"),ESearchCase::IgnoreCase))Result.ShowNumbers=false;
        else return Result;
        Result.Error.Reset();return Result;
    }
    if(!Words.IsEmpty() && Words[0].Equals(TEXT("text"),ESearchCase::IgnoreCase))Words.RemoveAt(0);
    if(Words.Num()<2 || Words[0].Len()>2)return Result;
    for(TCHAR C:Words[0])if(C<TEXT('0') || C>TEXT('9'))return Result;
    const int32 Location=FCString::Atoi(*Words[0]);
    if(Location<1 || Location>10)return Result;
    Words.RemoveAt(0);
    const auto Binding=FAC2Shortcut::ForText(FString::Join(Words,TEXT(" ")));
    if(Binding.IsEmpty())return Result;
    Result.Slot=Location-1;Result.Text=Binding.Text;Result.Error.Reset();
    return Result;
}

FAC2ChatRequest FAC2ChatRequest::Parse(FString Line)
{
    if (Line.Len() > 512)
        return {EAC2ChatCommand::Invalid, TEXT("Message is too long (512 characters).")};
    for (TCHAR C : Line)
        if (C < 32)
            return {EAC2ChatCommand::Invalid, TEXT("Enter one line of text.")};
    Line.TrimStartAndEndInline();
    if (Line.IsEmpty())
        return {};
    if (!Line.StartsWith(TEXT("/")))
        return {EAC2ChatCommand::Say, Line};
    FString Command, Text;
    if (!Line.Split(TEXT(" "), &Command, &Text))
        Command = Line;
    Command.ToLowerInline();
    Text.TrimStartAndEndInline();
    if (Command == TEXT("/say") || Command == TEXT("/s") || Command == TEXT("/me") ||
        Command == TEXT("/emote"))
    {
        if (Text.IsEmpty())
            return {EAC2ChatCommand::Invalid, TEXT("Add a message after the command.")};
        return {Command == TEXT("/say") || Command == TEXT("/s") ? EAC2ChatCommand::Say
                                                                 : EAC2ChatCommand::Emote,
                Text};
    }
    if (Command == TEXT("/help"))
        return {EAC2ChatCommand::Help, {}};
    if (Command == TEXT("/clear"))
        return {EAC2ChatCommand::Clear, {}};
    if (Command == TEXT("/loc"))
        return {EAC2ChatCommand::Location, {}};
    if (Command == TEXT("/tell") || Command == TEXT("/t") || Command == TEXT("/general") ||
        Command == TEXT("/trade") || Command == TEXT("/fellowship") || Command == TEXT("/allegiance"))
        return {EAC2ChatCommand::Unavailable,
                TEXT("This offline world has no server chat connection. Use /say for local chat.")};
    return {EAC2ChatCommand::Invalid, TEXT("Unknown command. Type /help for local chat commands.")};
}
void FAC2ChatLog::Add(EAC2ChatType Type, const FString &Text)
{
    if (Text.IsEmpty())
        return;
    Lines.Add({Type, Text.Left(1024)});
    if (Lines.Num() > 200)
        Lines.RemoveAt(0, Lines.Num() - 200);
}
void FAC2ChatLog::Remember(const FString &Text)
{
    if (!Text.IsEmpty() && (History.IsEmpty() || History.Last() != Text))
        History.Add(Text.Left(512));
    if (History.Num() > 50)
        History.RemoveAt(0, History.Num() - 50);
    HistoryIndex = History.Num();
    Draft.Empty();
}
FString FAC2ChatLog::Recall(int32 Direction, const FString &Current)
{
    if (HistoryIndex == History.Num())
        Draft = Current;
    HistoryIndex = FMath::Clamp(HistoryIndex + Direction, 0, History.Num());
    return History.IsValidIndex(HistoryIndex) ? History[HistoryIndex] : Draft;
}
