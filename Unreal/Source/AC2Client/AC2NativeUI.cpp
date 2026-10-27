#include "AC2NativeUI.h"
#include "AC2CharacterData.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"

namespace
{
uint32 T(const TCHAR *Name)
{
    return FAC2UILayout::Token(Name);
}
const FAC2UIState *State(const FAC2UIElement &E, uint32 Id)
{
    return E.States.FindByPredicate([Id](const auto &S) { return S.Id == Id; });
}
FIntRect Original(const FAC2UIElement &E, uint32 Id)
{
    for (uint32 S : {Id, T(TEXT("stateless"))})
        if (const auto *V = State(E, S); V && V->Rect.Min.X != -1)
            return V->Rect;
    return E.Rect;
}
int32 OriginalZ(const FAC2UIElement &E, uint32 Id)
{
    for (uint32 S : {Id, T(TEXT("stateless"))})
        if (const auto *V = State(E, S); V && V->Rect.Min.X != -1)
            return V->Z;
    return E.Z;
}
FIntRect Intersect(FIntRect A, FIntRect B)
{
    return {FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y), FMath::Min(A.Max.X, B.Max.X),
            FMath::Min(A.Max.Y, B.Max.Y)};
}
bool Empty(FIntRect R)
{
    return R.Width() <= 0 || R.Height() <= 0;
}
void PushClip(FSlateWindowElementList &Out, const FGeometry &G, FIntRect R)
{
    Out.PushClip(
        FSlateClippingZone(G.MakeChild(FVector2D(R.Size()), FSlateLayoutTransform(FVector2D(R.Min)))));
}
} // namespace
uint32 FAC2NativeNode::Attr(const TCHAR *Name, uint32 Default) const
{
    const auto *Value = Attributes.Find(T(Name));
    return Value ? *Value : Default;
}
float FAC2NativeNode::Float(const TCHAR *Name, float Default) const
{
    const auto *Value = Attributes.Find(T(Name));
    if (!Value)
        return Default;
    float Out;
    FMemory::Memcpy(&Out, Value, 4);
    return Out;
}
bool FAC2NativeUI::Open(const FString &Directory, FString &Error)
{
    if (!Local.Open(FPaths::Combine(Directory, TEXT("local_English.dat")), 3, Error))
        return false;
    for (uint32 Id :
         {0x22000001u, 0x22000002u, 0x22000005u, 0x2200002bu, 0x22000006u, 0x22000007u, 0x22000008u})
    {
        TArray<uint8> Bytes;
        FAC2UILayout Layout;
        if (!Local.Read(Id, Bytes, Error) || !Layout.Decode(Id, Bytes))
            return false;
        Layouts.Add(Id, MoveTemp(Layout));
    }
    return true;
}
FIntRect FAC2NativeUI::Place(const FAC2UIElement &E, FIntRect R, FIntPoint Old, FIntPoint Now)
{
    const FIntPoint W = R.Size() + FIntPoint(1, 1), Delta = Now - Old;
    if (E.Anchors[0] == 2)
        R.Min.X += Delta.X;
    if (E.Anchors[0] == 3)
        R.Min.X = Now.X / 2 - W.X / 2;
    if (E.Anchors[1] == 2)
        R.Min.Y += Delta.Y;
    if (E.Anchors[1] == 3)
        R.Min.Y = Now.Y / 2 - W.Y / 2;
    if (E.Anchors[2] == 1 && R.Max.X + Delta.X >= 1)
        R.Max.X += Delta.X;
    if (E.Anchors[2] == 3)
        R.Max.X = Now.X / 2 + W.X / 2 - 1;
    if (E.Anchors[3] == 1 && R.Max.Y + Delta.Y >= 1)
        R.Max.Y += Delta.Y;
    if (E.Anchors[3] == 3)
        R.Max.Y = Now.Y / 2 + W.Y / 2 - 1;
    // All runtime boxes have exclusive Max. Conversion happens exactly once here.
    R.Max += FIntPoint(1, 1);
    return R;
}
void FAC2NativeUI::Begin(FIntPoint Viewport)
{
    Display = Viewport;
    Nodes.Reset();
}
bool FAC2NativeUI::Root(uint32 Id, const TCHAR *Name)
{
    auto *L = Layouts.Find(Id);
    if (!L)
        return false;
    for (int32 I : L->Roots)
        if (L->Controls[I].Id == T(Name))
            return Add(*L, I, INDEX_NONE, FIntPoint(L->Width, L->Height), 0);
    return false;
}
bool FAC2NativeUI::Template(uint32 Id, const TCHAR *Name, FIntRect Rect, FIntRect Clip)
{
    auto *L = Layouts.Find(Id);
    if (!L)
        return false;
    for (int32 I : L->Roots)
        if (L->Controls[I].Id == T(Name))
            return Add(*L, I, INDEX_NONE, FIntPoint(L->Width, L->Height), 0, &Rect, &Clip);
    return false;
}
bool FAC2NativeUI::Add(const FAC2UILayout &L, int32 Index, int32 Parent, FIntPoint OldParent,
                       uint32 Inherited, const FIntRect *Override, const FIntRect *ExternalClip, int32 Depth)
{
    if (Depth >= 64 || Nodes.Num() >= 8192 || !L.Controls.IsValidIndex(Index))
        return false;
    const auto &Ref = L.Controls[Index];
    if (const bool *Show = Visibility.Find(Ref.Id); Show && !*Show)
        return true;
    const FAC2UIElement *Desc = &Ref;
    TSet<uint32> Seen;
    while (Desc->Kind == T(TEXT("reference")))
    {
        if (Seen.Contains(Desc->Id))
            return false;
        Seen.Add(Desc->Id);
        const auto *S = State(*Desc, T(TEXT("stateless")));
        const uint32 *Target = S ? S->Attributes.Find(T(TEXT("referent"))) : nullptr;
        if (!Target)
            return false;
        Desc = nullptr;
        for (int32 I : L.Roots)
            if (L.Controls[I].Id == *Target)
            {
                Desc = &L.Controls[I];
                break;
            }
        if (!Desc)
            return false;
    }
    FAC2NativeNode N;
    N.Id = Ref.Id;
    N.Kind = Desc->Kind;
    N.Parent = Parent;
    N.State = States.Contains(Ref.Id) ? States[Ref.Id] : Inherited ? Inherited : Ref.DefaultState;
    // Reference instances often say stateless while their button template defaults to normal.
    // Field states (open/closed, MacroMap/DetailMap) must not suppress a button's own artwork.
    if (N.Kind == T(TEXT("button")) && !States.Contains(Ref.Id) &&
        (N.State == T(TEXT("stateless")) || (!State(*Desc, N.State) && !State(Ref, N.State))))
        N.State = Desc->DefaultState;
    auto Merge = [&]() {
        N.Attributes.Reset();
        for (const FAC2UIElement *E : {&Ref, Desc})
            for (uint32 Id : {N.State, T(TEXT("stateless"))})
                if (const auto *S = State(*E, Id))
                    for (const auto &A : S->Attributes)
                        if (!N.Attributes.Contains(A.Key))
                            N.Attributes.Add(A.Key, A.Value);
    };
    Merge();
    if (N.Kind == T(TEXT("button")) && !States.Contains(Ref.Id))
    {
        N.State = N.Attr(TEXT("ghosted"))                         ? T(TEXT("ghosted"))
                  : Pressed == Ref.Id                             ? T(TEXT("normal_pressed"))
                  : Hovered == Ref.Id && N.Attr(TEXT("rollover")) ? T(TEXT("normal_rollover"))
                                                                  : N.State;
        Merge();
    }
    if (N.Attr(TEXT("hide")) && !Visibility.Contains(Ref.Id))
        return true;
    N.Z = OriginalZ(Ref, N.State);
    const FIntRect ParentBox =
        Parent == INDEX_NONE ? FIntRect(FIntPoint::ZeroValue, Display) : Nodes[Parent].Rect;
    N.Rect = Override ? *Override : Place(Ref, Original(Ref, N.State), OldParent, ParentBox.Size());
    for (auto Axis : {0, 1})
    {
        const TCHAR *MinName = Axis ? TEXT("min_height") : TEXT("min_width");
        const TCHAR *MaxName = Axis ? TEXT("max_height") : TEXT("max_width");
        int32 W = N.Rect.Max[Axis] - N.Rect.Min[Axis];
        W = FMath::Clamp(W, int32(N.Attr(MinName, 0)), int32(N.Attr(MaxName, 32768)));
        N.Rect.Max[Axis] = N.Rect.Min[Axis] + W;
    }
    if (!Override)
    {
        if (Parent != INDEX_NONE)
            N.Rect += ParentBox.Min;
        else
        {
            FIntPoint Origin(FMath::Clamp(N.Rect.Min.X, 0, FMath::Max(0, Display.X - N.Rect.Width())),
                             FMath::Clamp(N.Rect.Min.Y, 0, FMath::Max(0, Display.Y - N.Rect.Height())));
            N.Rect += Origin - N.Rect.Min;
        }
    }
    if (Parent != INDEX_NONE && Ref.Id == T(TEXT("widget")) && Nodes[Parent].Kind == T(TEXT("scrollbar")))
    {
        const auto &Bar = Nodes[Parent];
        const int32 Axis = Bar.Attr(TEXT("horizontal")) ? 0 : 1;
        const float Pos = Positions.Contains(Bar.Id)
                              ? Positions[Bar.Id]
                              : Bar.Float(Axis ? TEXT("y_position") : TEXT("x_position"));
        FIntPoint Shift = FIntPoint::ZeroValue;
        Shift[Axis] = Bar.Rect.Min[Axis] +
                      int32((Bar.Rect.Size()[Axis] - N.Rect.Size()[Axis]) * FMath::Clamp(Pos, 0.f, 1.f)) -
                      N.Rect.Min[Axis];
        N.Rect += Shift;
    }
    N.Clip = Intersect(N.Rect, ExternalClip           ? *ExternalClip
                               : Parent == INDEX_NONE ? ParentBox
                                                      : Nodes[Parent].Clip);
    // UIElement_Meter positions its fill inside the meter's original clip.
    if (Parent != INDEX_NONE && Nodes[Parent].Kind == T(TEXT("meter")))
    {
        const auto &M = Nodes[Parent];
        const float Value =
            FMath::Clamp(Meters.Contains(M.Id) ? Meters[M.Id] : M.Float(TEXT("goal_position")), 0.f, 1.f);
        if (Ref.Id == T(TEXT("left_fill")))
        {
            int32 Width = FMath::RoundToInt(M.Rect.Width() * Value);
            N.Clip.Max.X = FMath::Min(N.Clip.Max.X, M.Rect.Min.X + Width);
            if (M.Attr(TEXT("move_fill")))
                N.Rect += FIntPoint(Width - M.Rect.Width(), 0);
        }
        if (Ref.Id == T(TEXT("right_fill")))
            N.Clip.Min.X = FMath::Max(N.Clip.Min.X, M.Rect.Min.X + FMath::RoundToInt(M.Rect.Width() * Value));
    }
    if (Empty(N.Clip))
        return true;
    const FAC2UIState *Media = nullptr;
    for (const FAC2UIElement *E : {Desc, &Ref})
    {
        for (uint32 Id : {N.State, T(TEXT("stateless"))})
            if (const auto *S = State(*E, Id); S && !S->Media.IsEmpty())
            {
                Media = S;
                break;
            }
        if (Media)
            break;
    }
    if (Media)
        for (int32 I : Media->Media)
            if (L.Art.IsValidIndex(I))
                N.Media.Add(L.Art[I]);
    if (const auto *Value = Text.Find(Ref.Id))
        N.Text = *Value;
    else if (N.Kind == T(TEXT("text")))
    {
        const uint32 Key = N.Attr(TEXT("entry"));
        if (Key)
        {
            const uint64 CacheKey = (uint64(L.StringTable) << 32) | Key;
            if (!Strings.Contains(CacheKey))
                Strings.Add(CacheKey, AC2ReadUIString(Local, L.StringTable, Key));
            N.Text = Strings[CacheKey];
        }
    }
    const int32 Here = Nodes.Add(MoveTemp(N));
    // UpdateChild accepts the first description listed during an update. The current state precedes
    // stateless, and the dereferenced description precedes the instance's additional children.
    struct Child
    {
        int32 Index;
        const FAC2UIElement *Owner;
        uint32 State;
    };
    TArray<Child> Children;
    for (const FAC2UIElement *E : {Desc, &Ref})
        for (uint32 Id : {Nodes[Here].State, T(TEXT("stateless"))})
            if (const auto *S = State(*E, Id))
                for (int32 C : S->Children)
                {
                    if (!L.Controls.IsValidIndex(C))
                        continue;
                    if (!Children.ContainsByPredicate(
                            [&](const Child &V) { return L.Controls[V.Index].Id == L.Controls[C].Id; }))
                    {
                        const auto *Propagate = State(*Desc, Nodes[Here].State);
                        Children.Add({C, E, Propagate && (Propagate->Flags & 1) ? Nodes[Here].State : 0});
                    }
                }
    Children.StableSort(
        [&](const Child &A, const Child &B) { return L.Controls[A.Index].Z > L.Controls[B.Index].Z; });
    for (const auto &C : Children)
        Add(L, C.Index, Here, Original(*C.Owner, Nodes[Here].State).Size() + FIntPoint(1, 1), C.State,
            nullptr, nullptr, Depth + 1);
    return true;
}
const FAC2NativeNode *FAC2NativeUI::Find(uint32 Id) const
{
    return Nodes.FindByPredicate([Id](const auto &N) { return N.Id == Id; });
}
const FAC2NativeNode *FAC2NativeUI::Find(const TCHAR *Name) const
{
    return Find(T(Name));
}
const FAC2NativeNode *FAC2NativeUI::Descendant(const FAC2NativeNode &Parent, const TCHAR *Name) const
{
    const int32 Index = int32(&Parent - Nodes.GetData());
    for (int32 I = Index + 1; I < Nodes.Num(); ++I)
    {
        int32 P = Nodes[I].Parent;
        while (P != INDEX_NONE && P != Index)
            P = Nodes[P].Parent;
        if (P == Index && Nodes[I].Id == T(Name))
            return &Nodes[I];
    }
    return nullptr;
}
FIntRect FAC2NativeUI::Bounds(const TCHAR *Name) const
{
    const auto *N = Find(Name);
    return N ? N->Rect : FIntRect();
}
void FAC2NativeUI::ConstrainRoot(const TCHAR *Name, FIntRect Area)
{
    const auto *RootNode = Find(Name);
    if (!RootNode || RootNode->Parent != INDEX_NONE)
        return;
    const int32 Index = int32(RootNode - Nodes.GetData());
    FIntPoint Origin(FMath::Clamp(RootNode->Rect.Min.X, Area.Min.X,
                                  FMath::Max(Area.Min.X, Area.Max.X - RootNode->Rect.Width())),
                     FMath::Clamp(RootNode->Rect.Min.Y, Area.Min.Y,
                                  FMath::Max(Area.Min.Y, Area.Max.Y - RootNode->Rect.Height())));
    const FIntPoint Shift = Origin - RootNode->Rect.Min;
    for (int32 I = Index; I < Nodes.Num() && (I == Index || Nodes[I].Parent != INDEX_NONE); ++I)
    {
        Nodes[I].Rect += Shift;
        Nodes[I].Clip = Intersect(Nodes[I].Clip + Shift, Area);
    }
}
TArray<int32> FAC2NativeUI::PaintOrder() const
{
    // UIRegion paints siblings back to front (larger Z first), including top-level viewports.
    // Keep each subtree together so a background cannot cover its own children.
    TArray<TArray<int32>> Children;
    Children.SetNum(Nodes.Num() + 1);
    for (int32 I = 0; I < Nodes.Num(); ++I)
        Children[Nodes[I].Parent + 1].Add(I);
    for (auto &Siblings : Children)
        Siblings.StableSort([&](int32 A, int32 B) { return Nodes[A].Z > Nodes[B].Z; });
    TArray<int32> Order;
    TFunction<void(int32)> Visit = [&](int32 I) {
        Order.Add(I);
        for (int32 C : Children[I + 1])
            Visit(C);
    };
    for (int32 I : Children[0])
        Visit(I);
    return Order;
}
void FAC2NativeUI::LoadImage(uint32 Id, FAC2AssetLibrary &Assets)
{
    if (!Id || AttemptedImages.Contains(Id))
        return;
    AttemptedImages.Add(Id);
    if (auto *Texture = Assets.Texture(Id))
    {
        FSlateBrush B;
        B.SetResourceObject(Texture);
        B.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        B.DrawAs = ESlateBrushDrawType::Image;
        Images.Add(Id, B);
    }
    else
        ++MissingImages;
}
void FAC2NativeUI::Prepare(FAC2AssetLibrary &Assets, FAC2Fonts &Fonts)
{
    for (const auto &N : Nodes)
    {
        for (const auto &M : N.Media)
            if (M.Kind == 1 || M.Kind == 3)
                LoadImage(M.Image, Assets);
        if (N.Kind == T(TEXT("text")))
            Fonts.Get(N.Attr(TEXT("font"), 0x4200000c));
    }
}
int32 FAC2NativeUI::PaintText(const FAC2NativeNode &N, const FString &Value, FSlateWindowElementList &Out,
                              int32 Layer, const FGeometry &G, FAC2Fonts &Fonts) const
{
    if (Value.IsEmpty() || Empty(N.Clip))
        return Layer;
    auto Font = Fonts.Get(N.Attr(TEXT("font"), 0x4200000c));
    if (!Font)
        return Layer;
    const int32 Left = int32(N.Attr(TEXT("left_margin"))), Top = int32(N.Attr(TEXT("top_margin")));
    const int32 Width = N.Rect.Width() - Left - int32(N.Attr(TEXT("right_margin")));
    if (Width <= 0)
        return Layer;
    auto Lines = Font->Lines(Value, N.Attr(TEXT("one_line")) ? 0 : Width);
    float Y = N.Rect.Min.Y + Top;
    if (N.Attr(TEXT("center_vertically")))
        Y = N.Rect.Min.Y + (N.Rect.Height() - Lines.Num() * int32(Font->Height)) / 2.f;
    FLinearColor Color(N.Float(TEXT("font_color_red"), 1), N.Float(TEXT("font_color_green"), 1),
                       N.Float(TEXT("font_color_blue"), 1));
    PushClip(Out, G, N.Clip);
    for (const auto &Line : Lines)
    {
        float X = N.Rect.Min.X + Left;
        if (N.Attr(TEXT("center")))
            X += (Width - Font->Width(Line)) / 2;
        else if (N.Attr(TEXT("right_justify")))
            X += Width - Font->Width(Line);
        if (N.Attr(TEXT("font_outline")))
            for (FVector2D D : {FVector2D(-1, 0), FVector2D(1, 0), FVector2D(0, -1), FVector2D(0, 1)})
                Font->Draw(Out, ++Layer, G, Line, FVector2D(X, Y) + D, FLinearColor::Black);
        Font->Draw(Out, ++Layer, G, Line, FVector2D(X, Y), Color);
        Y += Font->Height;
    }
    Out.PopClip();
    return Layer;
}
int32 FAC2NativeUI::Paint(FSlateWindowElementList &Out, int32 Layer, const FGeometry &G, FAC2Fonts &Fonts,
                          TFunction<int32(const FAC2NativeNode &, int32)> Content) const
{
    for (int32 Index : PaintOrder())
    {
        const auto &N = Nodes[Index];
        PushClip(Out, G, N.Clip);
        // An animation replaces its previous image; never draw all frames on top of one another.
        const FAC2UIArt *Chosen = nullptr;
        for (const auto &M : N.Media)
            if (M.Kind == 1 || M.Kind == 3)
            {
                Chosen = &M;
                break;
            }
        if (Chosen)
            if (const auto *B = Images.Find(Chosen->Image))
            {
                const FVector2D Origin = FVector2D(N.Rect.Min + Chosen->Offset);
                const bool Stretch = N.Attr(TEXT("size_image_to_owner")) != 0;
                const FVector2D Step = Stretch ? FVector2D(N.Rect.Size()) : B->ImageSize;
                if (Step.X > 0 && Step.Y > 0)
                    for (float Y = Origin.Y; Y < N.Rect.Max.Y; Y += Step.Y)
                        for (float X = Origin.X; X < N.Rect.Max.X; X += Step.X)
                            FSlateDrawElement::MakeBox(
                                Out, ++Layer, G.ToPaintGeometry(Step, FSlateLayoutTransform(FVector2D(X, Y))),
                                B, ESlateDrawEffect::None,
                                FLinearColor(1, 1, 1, N.Float(TEXT("alpha_value"), 1)));
            }
        if (Content)
            Layer = Content(N, Layer);
        Out.PopClip();
        if (N.Kind == T(TEXT("text")))
            Layer = PaintText(N, N.Text, Out, Layer, G, Fonts);
    }
    return Layer;
}
