#pragma once
#include "AC2DatArchive.h"
#include "Framework/Commands/InputChord.h"

// Default gmClient keymaps, restricted to gmShortcutUI actions. Movement and
// camera controls keep their existing user-selected behavior.
struct FAC2ShortcutKey
{
    FInputChord Chord;
    uint32 Action=0;
};

struct FAC2ShortcutKeys
{
    static bool IsAction(uint32 Action);
    static bool Decode(TConstArrayView<uint8> Bytes,uint32 Id,bool Shift,bool Control,TArray<FAC2ShortcutKey>& Out);
    static bool Load(const FString& Directory,TArray<FAC2ShortcutKey>& Out,FString& Error);
};
