#pragma once
#include "CoreMinimal.h"

enum class EAC2AliasCommand : uint8 { Add, Remove, List, Clear, Shortcut, Help };
struct FAC2AliasRequest
{
    bool Recognized=false;
    EAC2AliasCommand Command=EAC2AliasCommand::Add;
    FString Name,Text,Error;
    int32 Slot=INDEX_NONE;
    static FAC2AliasRequest Parse(const FString& Line);
};

// GameplayOptionsProfile's user aliases, separate from literal text shortcuts.
// StrictAliasControl owns ;target and ;loc; callers supply their live values.
struct FAC2Aliases
{
    static constexpr int32 MaxAliases=256;
    TMap<FString,FString> Values;
    static bool Reserved(const FString& Name);
    static bool ValidName(const FString& Name);
    bool Set(const FString& Name,const FString& Text);
    bool Contains(const FString& Name) const;
    FString Encode() const;
    void Decode(const FString& Saved);
    bool Expand(const FString& Input,const FString& Target,const FString& Location,FString& Output,FString& Error) const;
};
