#pragma once
#include "CoreMinimal.h"

// ShellCmdTable quoted-word parsing shared by local chat commands.
bool AC2ChatWords(const FString& Args,TArray<FString>& Words);

enum class EAC2ChatType : uint8
{
    System,
    Local,
    Combat,
    Error
};
enum class EAC2ChatCommand : uint8
{
    Say,
    Emote,
    Help,
    Clear,
    Location,
    Unavailable,
    Invalid
};
struct FAC2ChatLine
{
    EAC2ChatType Type;
    FString Text;
    uint32 NativeType=0;
};
// GameplayOptionsProfile::FlushChatFontOptions / ChatWindow::GetChatMessageInfo.
uint32 AC2ChatColorIndex(uint32 NativeType,EAC2ChatType Fallback);
struct FAC2ChatRequest
{
    EAC2ChatCommand Command = EAC2ChatCommand::Invalid;
    FString Text;
    static FAC2ChatRequest Parse(FString Line);
};
// CPlayer's /shortcut command controls number labels or binds text at 1..10.
struct FAC2ShortcutRequest
{
    bool Recognized=false;
    int32 Slot=INDEX_NONE;
    FString Text,Error;
    TOptional<bool> ShowNumbers;
    static FAC2ShortcutRequest Parse(const FString& Line);
};
struct FAC2ChatLog
{
    TArray<FAC2ChatLine> Lines;
    TArray<FString> History;
    int32 HistoryIndex = 0;
    FString Draft;
    void Add(EAC2ChatType Type, const FString &Text);
    void Remember(const FString &Text);
    FString Recall(int32 Direction, const FString &Current);
};
