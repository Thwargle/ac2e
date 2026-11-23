#pragma once
#include "AC2ShortcutKeys.h"

struct FAC2KeyBinding
{
    FName Id;
    FString Label;
    FInputChord Default, Chord;
    FName Axis;
    float Direction = 0;
    uint32 Shortcut = 0;
    bool Hold = false;
};

struct FAC2KeyBindings
{
    TArray<FAC2KeyBinding> Rows;
    void Initialize(TConstArrayView<FAC2ShortcutKey> Native);
    void Load(const FString& Ini);
    void Save(const FString& Ini) const;
    void Reset();
    bool Assign(int32 Index, const FInputChord& Chord, FString& Error);
    static bool PointerKey(FKey Key);
};
