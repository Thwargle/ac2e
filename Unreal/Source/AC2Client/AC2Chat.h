#pragma once
#include "CoreMinimal.h"

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
};
struct FAC2ChatRequest
{
    EAC2ChatCommand Command = EAC2ChatCommand::Invalid;
    FString Text;
    static FAC2ChatRequest Parse(FString Line);
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
