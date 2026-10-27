#include "AC2Chat.h"
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
