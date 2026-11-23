#include "AC2KeyBindings.h"
#include "Misc/ConfigCacheIni.h"
#include "String/LexFromString.h"

namespace
{
constexpr const TCHAR* Section = TEXT("AC2.KeyBindings");

FString ShortcutLabel(uint32 Action)
{
    if (Action >= 0x4100005f) return FString::Printf(TEXT("Second row, slot %u"), Action - 0x4100005f + 1);
    if (Action == 0x41000040) return TEXT("Previous shortcut page");
    if (Action == 0x4100003f) return TEXT("Next shortcut page");
    if (Action >= 0x41000035) return FString::Printf(TEXT("Select shortcut page %u"), Action - 0x41000035 + 1);
    return FString::Printf(TEXT("First row, slot %u"), Action - 0x4100002b + 1);
}
bool Valid(const FAC2KeyBinding& Row, const FInputChord& Chord)
{
    if (!Chord.Key.IsValid()) return Chord == FInputChord();
    const FKey Key = Chord.Key;
    if (Key.IsGamepadKey() || Key.IsTouch() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() ||
        Key == EKeys::AnyKey || (Key.IsMouseButton() && !FAC2KeyBindings::PointerKey(Key))) return false;
    if (Row.Hold && (Chord.HasAnyModifierKeys() || FAC2KeyBindings::PointerKey(Key))) return false;
    return true;
}
}

void FAC2KeyBindings::Initialize(TConstArrayView<FAC2ShortcutKey> Native)
{
    Rows.Reset();
    auto Add = [this](const TCHAR* Id, const TCHAR* Label, FKey Key, FName Axis = NAME_None, float Direction = 0, bool Hold = false)
    {
        Rows.Add({Id, Label, Key, Key, Axis, Direction, 0, Hold || !Axis.IsNone()});
    };
    Add(TEXT("Forward"), TEXT("Move forward"), EKeys::W, TEXT("AC2Forward"), 1);
    Add(TEXT("ForwardAlt"), TEXT("Move forward (alternate)"), EKeys::Up, TEXT("AC2Forward"), 1);
    Add(TEXT("Backward"), TEXT("Move backward"), EKeys::S, TEXT("AC2Forward"), -1);
    Add(TEXT("BackwardAlt"), TEXT("Move backward (alternate)"), EKeys::Down, TEXT("AC2Forward"), -1);
    Add(TEXT("StrafeLeft"), TEXT("Strafe left"), EKeys::A, TEXT("AC2Right"), -1);
    Add(TEXT("StrafeRight"), TEXT("Strafe right"), EKeys::D, TEXT("AC2Right"), 1);
    Add(TEXT("TurnLeft"), TEXT("Turn left"), EKeys::Left, TEXT("AC2KeyboardTurn"), -1);
    Add(TEXT("TurnRight"), TEXT("Turn right"), EKeys::Right, TEXT("AC2KeyboardTurn"), 1);
    Add(TEXT("Ascend"), TEXT("Swim or fly up"), EKeys::E, TEXT("AC2Up"), 1);
    Add(TEXT("Descend"), TEXT("Swim or fly down"), EKeys::Q, TEXT("AC2Up"), -1);
    Add(TEXT("Jump"), TEXT("Jump"), EKeys::SpaceBar, NAME_None, 0, true);
    Add(TEXT("Walk"), TEXT("Walk while held"), EKeys::LeftShift, NAME_None, 0, true);
    Add(TEXT("WalkAlt"), TEXT("Walk while held (alternate)"), EKeys::RightShift, NAME_None, 0, true);
    Add(TEXT("AutoRun"), TEXT("Toggle autorun"), EKeys::ThumbMouseButton);
    Add(TEXT("Fly"), TEXT("Toggle flight"), EKeys::F);
    Add(TEXT("View"), TEXT("Switch camera view"), EKeys::Tab);
    Add(TEXT("ZoomIn"), TEXT("Zoom in"), EKeys::MouseScrollUp);
    Add(TEXT("ZoomOut"), TEXT("Zoom out"), EKeys::MouseScrollDown);
    Add(TEXT("Cursor"), TEXT("Restore pointer"), EKeys::MiddleMouseButton);
    Add(TEXT("Target"), TEXT("Select target"), EKeys::T);
    Add(TEXT("Use"), TEXT("Use target"), EKeys::E);
    Add(TEXT("Combat"), TEXT("Toggle combat stance"), EKeys::Z);
    Add(TEXT("Recall"), TEXT("Lifestone recall"), EKeys::Home);
    Add(TEXT("Map"), TEXT("Map"), EKeys::M);
    Add(TEXT("Inventory"), TEXT("Inventory"), EKeys::I);
    Add(TEXT("Character"), TEXT("Character sheet"), EKeys::C);
    Add(TEXT("Chat"), TEXT("Open chat"), EKeys::Enter);
    Add(TEXT("Close"), TEXT("Close panel or open options"), EKeys::Escape);
    Add(TEXT("SelectCharacter"), TEXT("Character selection"), EKeys::F4);
    for (const FAC2ShortcutKey& Key : Native)
    {
        const FName Id(*FString::Printf(TEXT("Shortcut%08x"), Key.Action));
        Rows.Add({Id, ShortcutLabel(Key.Action), Key.Chord, Key.Chord, NAME_None, 0, Key.Action});
    }
}

bool FAC2KeyBindings::PointerKey(FKey Key)
{
    return Key == EKeys::MiddleMouseButton || Key == EKeys::ThumbMouseButton || Key == EKeys::ThumbMouseButton2 ||
        Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown;
}

bool FAC2KeyBindings::Assign(int32 Index, const FInputChord& Chord, FString& Error)
{
    if (!Rows.IsValidIndex(Index)) return false;
    const auto& Row = Rows[Index];
    if (!Valid(Row, Chord))
    {
        Error = Row.Hold ? TEXT("Choose a single keyboard key for this held action.") : TEXT("Choose a keyboard key, wheel direction, or side mouse button.");
        return false;
    }
    // Ascend and Use share E in the shipped controls. Preserve that intentional pair.
    for (int32 I = 0; Chord.Key.IsValid() && I < Rows.Num(); ++I)
    {
        const auto& Other = Rows[I];
        if (I == Index || Other.Chord != Chord || (Row.Default == Chord && Other.Default == Chord)) continue;
        Error = FString::Printf(TEXT("Already assigned to %s. Clear its binding first."), *Other.Label);
        return false;
    }
    Rows[Index].Chord = Chord;
    Error.Reset();
    return true;
}

void FAC2KeyBindings::Reset()
{
    for (auto& Row : Rows) Row.Chord = Row.Default;
}

void FAC2KeyBindings::Load(const FString& Ini)
{
    Reset();
    for (auto& Row : Rows)
    {
        FString Saved;
        if (!GConfig->GetString(Section, *Row.Id.ToString(), Saved, Ini)) continue;
        if (Saved == TEXT("Unbound")) { Row.Chord = {}; continue; }
        FString Key, Modifiers;
        int32 Flags = 0;
        if (!Saved.Split(TEXT("|"), &Key, &Modifiers) || !LexTryParseString(Flags, *Modifiers) || Flags < 0 || Flags > 15) continue;
        const FInputChord Chord(FKey(FName(*Key)), Flags & 1, Flags & 2, Flags & 4, Flags & 8);
        if (Chord.Key.IsValid() && Valid(Row, Chord)) Row.Chord = Chord;
    }
}

void FAC2KeyBindings::Save(const FString& Ini) const
{
    for (const auto& Row : Rows)
    {
        const FString Id = Row.Id.ToString();
        const auto& Chord = Row.Chord;
        if (Chord == Row.Default) { GConfig->RemoveKey(Section, *Id, Ini); continue; }
        const int32 Flags = Chord.bShift | (Chord.bCtrl << 1) | (Chord.bAlt << 2) | (Chord.bCmd << 3);
        const FString Saved = Chord.Key.IsValid() ? FString::Printf(TEXT("%s|%d"), *Chord.Key.ToString(), Flags) : TEXT("Unbound");
        GConfig->SetString(Section, *Id, *Saved, Ini);
    }
    GConfig->Flush(false, Ini);
}
