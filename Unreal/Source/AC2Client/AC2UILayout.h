#pragma once
#include "AC2AssetReader.h"

// LayoutDesc / ElementDesc / StateDesc media from local_English.dat.
// This catalogue preserves authored image identity and state, without executing UI messages.
struct FAC2UIArt
{
    uint32 Element = 0, State = 0, Image = 0;
    FIntRect Rect;
    FIntPoint Offset = FIntPoint::ZeroValue;
    bool Default = false;
    uint32 Kind = 3, DrawMode = 0;
    float Duration = 0;
};
struct FAC2UIState
{
    uint32 Id = 0;
    uint8 Flags = 0;
    FIntRect Rect;
    uint16 Z = 0xffff;
    TMap<uint32, uint32> Attributes;
    TMap<uint32, FString> Strings;
    TArray<int32> Media, Children;
};
struct FAC2UIElement
{
    uint32 Id = 0, Parent = 0, Reference = 0;
    FIntRect Rect;
    uint32 Scene = 0, Font = 0;
    uint32 Kind = 0, DefaultState = 0;
    uint16 Z = 0;
    uint8 Anchors[4] = {};
    TArray<FAC2UIState> States;
};
struct FAC2UILayout
{
    uint32 StringTable = 0;
    int32 Width = 0, Height = 0, Elements = 0;
    TArray<FAC2UIArt> Art;
    TArray<FAC2UIElement> Controls;
    TArray<int32> Roots;
    static uint32 Token(const FString &Name);
    const FAC2UIElement *Find(const FString &Name) const;
    FIntRect Bounds(const FString &Name) const;
    uint32 Font(const FString &Name, uint32 Default = 0x4200001d) const;
    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes);
};
