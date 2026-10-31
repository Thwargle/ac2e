#include "AC2UILayout.h"
#include "AC2CharacterData.h"
namespace
{
struct FLayoutReader
{
    FAC2AssetReader R;
    FAC2UILayout &Layout;
    int32 Budget = 16384;
    FLayoutReader(TConstArrayView<uint8> Bytes, FAC2UILayout &Out) : R(Bytes), Layout(Out)
    {
    }
    uint8 Byte()
    {
        int32 P = R.Pos;
        return R.Take(1) ? R.Data[P] : 0;
    }
    FIntRect Rect()
    {
        int32 X = int16(R.Short()), Y = int16(R.Short()), X1 = int16(R.Short()), Y1 = int16(R.Short());
        return FIntRect(X, Y, X1, Y1);
    }
    int32 Element(int32 Depth, uint32 Parent = 0)
    {
        if (Depth >= 64 || --Budget < 0)
        {
            R.Good = false;
            return INDEX_NONE;
        }
        ++Layout.Elements;
        uint32 Id = R.Word();
        uint32 ElementKind = R.Word();
        uint32 Default = R.Word();
        FIntRect Bounds = Rect();
        int32 Control = Layout.Controls.Add({Id, Parent, 0, Bounds});
        Layout.Controls[Control].Kind = ElementKind;
        Layout.Controls[Control].DefaultState = Default;
        Layout.Controls[Control].Z = R.Short();
        for (int32 Edge = 0; Edge < 4; ++Edge)
            Layout.Controls[Control].Anchors[Edge] = Byte();
        int32 NS = R.Short();
        R.Align();
        for (int32 S = 0; S < NS && R.Good; ++S)
        {
            uint32 State = R.Word();
            FAC2UIState NativeState;
            NativeState.Id = State;
            NativeState.Flags = Byte();
            NativeState.Rect = Rect();
            NativeState.Z = R.Short();
            R.Align();
            int32 NA = R.Count(5, 16384);
            for (int32 A = 0; A < NA && R.Good; ++A)
            {
                uint32 Attribute = R.Word();
                uint8 Kind = Byte();
                R.Align();
                if (Kind == 5)
                    NativeState.Strings.Add(Attribute, AC2ReadPackedString(R));
                else if (Kind >= 1 && Kind <= 7)
                {
                    uint32 Value = R.Word();
                    NativeState.Attributes.Add(Attribute, Value);
                    if (Attribute == FAC2UILayout::Token(TEXT("referent")))
                        Layout.Controls[Control].Reference = Value;
                    if (Attribute == FAC2UILayout::Token(TEXT("font")))
                        Layout.Controls[Control].Font = Value;
                    if (Attribute == FAC2UILayout::Token(TEXT("uiscene")))
                        Layout.Controls[Control].Scene = Value;
                }
                R.Align();
            }
            int32 NM = R.Count(8, 16384);
            for (int32 M = 0; M < NM && R.Good; ++M)
            {
                uint32 Kind = R.Word();
                if (Kind < 1 || Kind > 10 || R.Word() != Kind)
                {
                    R.Good = false;
                    break;
                }
                FAC2UIArt Image;
                Image.Element = Id;
                Image.State = State;
                Image.Rect = Bounds;
                Image.Default = State == Default;
                Image.Kind = Kind;
                if (Kind == 1)
                {
                    Image.Duration = R.Float();
                    Image.DrawMode = R.Word();
                    int32 X = int32(R.Word()), Y = int32(R.Word());
                    Image.Offset = FIntPoint(X, Y);
                    int32 NF = R.Count(4, 16384);
                    for (int32 F = 0; F < NF; ++F)
                    {
                        Image.Image = R.Word();
                        NativeState.Media.Add(Layout.Art.Add(Image));
                    }
                }
                else if (Kind == 3 || Kind == 4 || Kind == 10)
                {
                    Image.Image = R.Word();
                    if (Kind == 3)
                        Image.DrawMode = R.Word();
                    int32 X = int32(R.Word()), Y = int32(R.Word());
                    Image.Offset = FIntPoint(X, Y);
                    NativeState.Media.Add(Layout.Art.Add(Image));
                }
                else if (Kind == 2 || Kind == 6 || Kind == 7)
                    R.Take(8);
                else if (Kind == 5)
                {
                    Image.Image=R.Word(); // MD_Data_Sound::m_soundDID
                    NativeState.Media.Add(Layout.Art.Add(Image));
                }
                else if (Kind == 8)
                    R.Word();
                else if (Kind == 9)
                {
                    R.Take(8);
                    R.String();
                }
            }
            int32 NT = R.Count(12, 16384);
            for (int32 T = 0; T < NT && R.Good; ++T)
            {
                uint32 Kind = R.Word();
                R.Take(8);
                if (Kind == 1 || Kind == 2)
                    R.Take(28);
                if (Kind == 3)
                    R.Take(20);
                if (Kind == 4)
                    R.Take(24);
            }
            int32 NC = R.Count(28, 16384);
            for (int32 C = 0; C < NC && R.Good; ++C)
                NativeState.Children.Add(Element(Depth + 1, Id));
            R.Align();
            Layout.Controls[Control].States.Add(MoveTemp(NativeState));
        }
        R.Align();
        return Control;
    }
};
} // namespace
bool FAC2UILayout::Decode(uint32 Id, TConstArrayView<uint8> Bytes)
{
    *this = {};
    FLayoutReader Reader(Bytes, *this);
    auto &R = Reader.R;
    if (R.Word() != Id)
        return false;
    int32 N = R.Count(4, 4096);
    R.Take(int64(N) * 4);
    StringTable = R.Word();
    Width = R.Short();
    Height = R.Short();
    N = R.Short();
    R.Align();
    for (int32 I = 0; I < N && R.Good; ++I)
        Roots.Add(Reader.Element(0));
    return R.End();
}

uint32 FAC2UILayout::Token(const FString &Name)
{
    uint32 H = 0, Shift = 0;
    for (TCHAR C : Name)
    {
        uint32 V = uint32(C) << 25;
        H ^= V >> Shift;
        if (Shift > 25)
            H ^= V << (32 - Shift);
        Shift = (Shift + 7) & 31;
    }
    return H;
}
const FAC2UIElement *FAC2UILayout::Find(const FString &Name) const
{
    uint32 Id = Token(Name);
    return Controls.FindByPredicate([&](const auto &E) { return E.Id == Id; });
}
FIntRect FAC2UILayout::Bounds(const FString &Name) const
{
    const auto *E = Find(Name);
    if (!E)
        return FIntRect();
    FIntRect Result = E->Rect;
    uint32 Parent = E->Parent;
    for (int32 Depth = 0; Parent && Depth < 64; ++Depth)
    {
        const auto *P = Controls.FindByPredicate([&](const auto &V) { return V.Id == Parent; });
        if (!P)
            break;
        Result += P->Rect.Min;
        Parent = P->Parent;
    }
    return Result;
}

uint32 FAC2UILayout::Font(const FString &Name, uint32 Default) const
{
    const auto *Start = Find(Name);
    TSet<uint32> Visited;
    TFunction<uint32(const FAC2UIElement *, int32)> Resolve = [&](const FAC2UIElement *E,
                                                                  int32 Depth) -> uint32 {
        if (!E || Depth > 32 || Visited.Contains(E->Id))
            return 0;
        Visited.Add(E->Id);
        if (E->Font)
            return E->Font;
        if (E->Reference)
            if (uint32 F = Resolve(
                    Controls.FindByPredicate([&](const auto &V) { return V.Id == E->Reference; }), Depth + 1))
                return F;
        for (const auto &Child : Controls)
            if (Child.Parent == E->Id)
                if (uint32 F = Resolve(&Child, Depth + 1))
                    return F;
        return 0;
    };
    uint32 Result = Resolve(Start, 0);
    return Result ? Result : Default;
}
