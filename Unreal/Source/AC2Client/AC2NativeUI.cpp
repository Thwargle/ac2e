#include "AC2NativeUI.h"
#include "AC2Audio.h"
#include "Engine/World.h"
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
         {0x22000001u, 0x22000002u, 0x22000005u, 0x2200002bu, 0x22000006u, 0x22000007u, 0x22000008u, 0x22000022u})
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
    DisplayedRootOffsets.Reset();
    Nodes.Reset();
}
FString FAC2NativeUI::Localized(const TCHAR* Key,const TMap<uint32,FString>& Arguments)
{
    if(!Arguments.IsEmpty())return AC2FormatUIString(Local,0x25000012,T(Key),Arguments);
    return Localized(T(Key));
}
FString FAC2NativeUI::Localized(uint32 Key)
{
    const uint64 CacheKey=(uint64(0x25000012)<<32)|Key;
    if(!Strings.Contains(CacheKey))Strings.Add(CacheKey,AC2ReadUIString(Local,0x25000012,Key));
    return Strings[CacheKey];
}
const TCHAR* FAC2NativeUI::PanelRoot(const TCHAR* Field) const
{
    return Field;
}
bool FAC2NativeUI::Root(uint32 Id, const TCHAR *Name)
{
    auto *L = Layouts.Find(Id);
    if (!L)
        return false;
    for (int32 I : L->Roots)
        if (L->Controls[I].Id == T(Name))
        {
            return Add(*L, I, INDEX_NONE, FIntPoint(L->Width, L->Height), 0);
        }
    // Promote the legacy side-panel reference into its own window. Resolve
    // its authored parent geometry once; user offsets then belong to this field.
    for(int32 I=0;I<L->Controls.Num();++I)if(L->Controls[I].Id==T(Name)){
        const auto& E=L->Controls[I];const auto* Parent=L->Controls.FindByPredicate([&](const auto& C){return C.Id==E.Parent;});if(!Parent)break;
        const auto ParentRect=Place(*Parent,Original(*Parent,Parent->DefaultState),FIntPoint(L->Width,L->Height),Display);
        auto Rect=Place(E,Original(E,E.DefaultState),Original(*Parent,Parent->DefaultState).Size()+FIntPoint(1,1),ParentRect.Size());
        Rect+=ParentRect.Min;
        return Add(*L,I,INDEX_NONE,FIntPoint(L->Width,L->Height),0,&Rect);
    }

    return false;
}
bool FAC2NativeUI::Template(uint32 Id, const TCHAR *Name, FIntRect Rect, FIntRect Clip, uint32 ParentId)
{
    return Template(Id,T(Name),Rect,Clip,ParentId);
}
bool FAC2NativeUI::Template(uint32 Id, uint32 ElementId, FIntRect Rect, FIntRect Clip, uint32 ParentId)
{
    auto *L = Layouts.Find(Id);
    if (!L)
        return false;
    const auto *ParentNode = ParentId ? Find(ParentId) : nullptr;
    const int32 Parent = ParentNode ? int32(ParentNode - Nodes.GetData()) : INDEX_NONE;
    for (int32 I : L->Roots)
        if (L->Controls[I].Id == ElementId)
            return Add(*L, I, Parent, FIntPoint(L->Width, L->Height), 0, &Rect, &Clip);
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
    N.Window=ContentWindow?ContentWindow:Parent==INDEX_NONE?Ref.Id:Nodes[Parent].Window;
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
    if (N.Kind == T(TEXT("button")))
    {
        // An explicit base state controls availability/selection, not pointer feedback.
        // Preserve disabled controls and use the template's selected variants when present.
        if (N.State == T(TEXT("ghosted")) || N.Attr(TEXT("ghosted")))
            N.State = T(TEXT("ghosted"));
        else if (Pressed == Ref.Id || (Hovered == Ref.Id && N.Attr(TEXT("rollover"))))
        {
            const bool Selected = N.State == T(TEXT("highlight"));
            uint32 Pointer = T(Pressed == Ref.Id ? (Selected ? TEXT("highlight_pressed") : TEXT("normal_pressed"))
                                               : (Selected ? TEXT("highlight_rollover") : TEXT("normal_rollover")));
            if (!State(*Desc, Pointer) && !State(Ref, Pointer))
                Pointer = T(Pressed == Ref.Id ? TEXT("normal_pressed") : TEXT("normal_rollover"));
            if (State(*Desc, Pointer) || State(Ref, Pointer))
                N.State = Pointer;
        }
        Merge();
    }
    if (N.Attr(TEXT("hide")) && !Visibility.Contains(Ref.Id))
        return true;
    N.Z = OriginalZ(Ref, N.State);
    const FIntRect ParentBox =
        Parent == INDEX_NONE ? FIntRect(FIntPoint::ZeroValue, Display) : Nodes[Parent].Rect;
    if(Parent==INDEX_NONE)if(const auto* Minimum=WindowMinimums.Find(Ref.Id)){
        N.Attributes.Add(T(TEXT("allow_resize")),1);
        N.Attributes.Add(T(TEXT("min_width")),Minimum->X);N.Attributes.Add(T(TEXT("min_height")),Minimum->Y);
        N.Attributes.Add(T(TEXT("max_width")),32768);N.Attributes.Add(T(TEXT("max_height")),32768);
    }
    auto Placement=Ref;
    const bool SkillsWindow=N.Window==T(TEXT("SkillField"));
    const bool QuestWindow=N.Window==T(TEXT("QuestField"));
    const bool BottomTrim=(SkillsWindow && Ref.Id==0xb6645e9bu) || (QuestWindow && Ref.Id==0xb21c3f68u);
    if(QuestWindow && (Ref.Id==0x85413ff5u || Ref.Id==0x87e7c1f5u ||
        Ref.Id==0x6a170865u || Ref.Id==0x6bc945a5u || Ref.Id==0x5f75e612u)){
        Placement.Anchors[2]=1;
        Placement.Anchors[3]=1;
        N.Attributes.Add(T(TEXT("max_width")),32768);
        N.Attributes.Add(T(TEXT("max_height")),32768);
    }
    if(SkillsWindow && Ref.Id==T(TEXT("ModifyField"))){
        Placement.Anchors[1]=2;
        Placement.Anchors[2]=1;
        Placement.Anchors[3]=1;
    }
    if((SkillsWindow || QuestWindow) && N.Attr(TEXT("referent"))==0xe104e194u)
        Placement.Anchors[3]=2; // Header text stays in its authored caption band.
    if(N.Window==T(TEXT("InventoryField"))){
        // The old fixed inventory used stretch anchors even for the portrait
        // and bag buttons. Only its bag grid should gain vertical space.
        if(Ref.Id==0x27ed4815u)Placement.Anchors[3]=2;
        if(Ref.Id==0x22d1bc45u) {Placement.Anchors[0]=2;Placement.Anchors[2]=1;}
        if(Ref.Id==0x523d41c6u)Placement.Anchors[1]=2;
        if(Ref.Id==0x4e54bdc7u || Ref.Id==0x89932192u || Ref.Id==0xefd62191u || Ref.Id==0xc71135eau){
            Placement.Anchors[0]=2;Placement.Anchors[2]=1;Placement.Anchors[3]=2;
        }
    }
    if(Ref.Id==T(TEXT("AutoPopulateButton")))Placement.Anchors[2]=2;
    if(N.Window==0x0e036a6cu && (Ref.Id==0x82155597u || Ref.Id==0x9ba1f88fu || Ref.Id==0xbff888f4u)){
        Placement.Anchors[0]=2;Placement.Anchors[2]=1;Placement.Anchors[3]=2;
    }
    N.Rect = Override ? *Override : Place(Placement, Original(Ref, N.State), OldParent, ParentBox.Size());
    if(BottomTrim && Parent!=INDEX_NONE){
        const int32 Height=Original(Ref,N.State).Height()+1;
        N.Rect=FIntRect(0,ParentBox.Height()-Height,ParentBox.Width(),ParentBox.Height());
        N.Attributes.Add(T(TEXT("max_width")),32768);
    }
    // These panels were capped to the fixed side bar in retail's XML. Once
    // detached as independent windows their content must follow the frame,
    // while tab buttons, icons and the 80-pixel footer retain native sizes.
    if(N.Window==T(TEXT("SkillField")) && Parent!=INDEX_NONE){
        const int32 W=ParentBox.Width(),H=ParentBox.Height();
        if(Ref.Id==0x839fa006u)N.Rect=FIntRect(2,30,W-2,H-2);
        if(Ref.Id==T(TEXT("CraftPage")) || Ref.Id==0xaf472f02u || Ref.Id==0x839fa006u){
            N.Attributes.Add(T(TEXT("max_width")),32768);N.Attributes.Add(T(TEXT("max_height")),32768);
        }
        if(Nodes[Parent].Id==T(TEXT("CraftPage"))){
            if(Ref.Id==0xe44235ffu)N.Rect=FIntRect(0,H-80,W,H);
            if(Ref.Id==T(TEXT("RecipeList")))N.Rect=FIntRect(4,24,W-19,H-86);
            if(Ref.Id==0x7a983523u)N.Rect=FIntRect(W-15,24,W,H-101);
            if(Ref.Id==0x039837c7u)N.Rect=FIntRect(W-15,9,W,24);
            if(Ref.Id==0xf87635e0u)N.Rect=FIntRect(W-15,H-101,W,H-86);
            N.Attributes.Add(T(TEXT("max_width")),32768);N.Attributes.Add(T(TEXT("max_height")),32768);
        }
    }
    // Resize the owning viewport before resolving anchored descendants. Fixed
    // retail panels ignore saved sizes; only authored allow_resize roots use them.
    if(Parent==INDEX_NONE && N.Attr(TEXT("allow_resize")))
        if(const auto* Size=RootSizes.Find(Ref.Id))N.Rect.Max=N.Rect.Min+*Size;
    if(Parent==INDEX_NONE && N.Attr(TEXT("allow_resize")) && N.Attr(TEXT("row_height")))
        if(const auto* Rows=RootBottomRows.Find(Ref.Id))
            N.Rect.Min.Y=N.Rect.Max.Y-(*Rows*int32(N.Attr(TEXT("row_height")))+int32(N.Attr(TEXT("border_size"))));
    for (auto Axis : {0, 1})
    {
        const TCHAR *MinName = Axis ? TEXT("min_height") : TEXT("min_width");
        const TCHAR *MaxName = Axis ? TEXT("max_height") : TEXT("max_width");
        int32 W = N.Rect.Max[Axis] - N.Rect.Min[Axis];
        W = FMath::Clamp(W, int32(N.Attr(MinName, 0)), int32(N.Attr(MaxName, 32768)));
        // Saved detached sizes may come from a larger monitor. Clamp the size
        // before laying out children, otherwise bottom-anchored actions remain
        // below the viewport even though the window origin is clamped.
        if(Parent==INDEX_NONE && WindowMinimums.Contains(Ref.Id))W=FMath::Min(W,FMath::Max(1,Display[Axis]));
        N.Rect.Max[Axis] = N.Rect.Min[Axis] + W;
    }
    if (!Override)
    {
        if (Parent != INDEX_NONE)
            N.Rect += ParentBox.Min;
        else if(!WindowMinimums.Contains(Ref.Id))
        {
            FIntPoint Origin(FMath::Clamp(N.Rect.Min.X, 0, FMath::Max(0, Display.X - N.Rect.Width())),
                             FMath::Clamp(N.Rect.Min.Y, 0, FMath::Max(0, Display.Y - N.Rect.Height())));
            N.Rect += Origin - N.Rect.Min;
        }
    }
    if (Parent==INDEX_NONE && (RootOffsets.Contains(Ref.Id) || WindowMinimums.Contains(Ref.Id)))
        {
            const FIntPoint Offset=RootOffsets.FindRef(Ref.Id);
            N.Rect+=Offset;
            const FIntPoint Origin(FMath::Clamp(N.Rect.Min.X,0,FMath::Max(0,Display.X-N.Rect.Width())),
                                  FMath::Clamp(N.Rect.Min.Y,0,FMath::Max(0,Display.Y-N.Rect.Height())));
            // Login can lay out at a provisional viewport before the actual window exists.
            // Start a drag at the visible position without modifying the user's preference.
            DisplayedRootOffsets.Add(Ref.Id,Offset+Origin-N.Rect.Min);
            N.Rect+=Origin-N.Rect.Min;
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
        // Legions UIElement_Meter::UpdateLayout (005B5780). The shortcut
        // recovery shade uses bottom_fill with move_fill, disappearing at 1.
        if (Ref.Id == T(TEXT("top_fill")))
        {
            const int32 Height=FMath::RoundToInt(M.Rect.Height()*Value);
            N.Clip.Max.Y=FMath::Min(N.Clip.Max.Y,M.Rect.Min.Y+Height);
            if(M.Attr(TEXT("move_fill")))N.Rect+=FIntPoint(0,Height-M.Rect.Height());
        }
        if (Ref.Id == T(TEXT("bottom_fill")))
        {
            const int32 Height=FMath::RoundToInt(M.Rect.Height()*Value);
            N.Clip.Min.Y=FMath::Max(N.Clip.Min.Y,M.Rect.Min.Y+Height);
            if(M.Attr(TEXT("move_fill")))N.Rect+=FIntPoint(0,Height);
        }
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
    N.ResizableFrame=WindowMinimums.Contains(N.Window) && N.Kind==T(TEXT("field"));
    if(N.Kind==T(TEXT("button")) && N.State!=T(TEXT("ghosted")) && !N.Attr(TEXT("ghosted")))
    {
        N.PressState=T(N.State==T(TEXT("highlight")) || N.State==T(TEXT("highlight_rollover")) ||
            N.State==T(TEXT("highlight_pressed"))?TEXT("highlight_pressed"):TEXT("normal_pressed"));
        if(!State(Ref,N.PressState) && !State(*Desc,N.PressState))N.PressState=T(TEXT("normal_pressed"));
        for(const auto* E:{Desc,&Ref})if(const auto* S=State(*E,N.PressState);S && !S->Media.IsEmpty())
        {
            for(int32 I:S->Media)if(L.Art.IsValidIndex(I) && L.Art[I].Kind==5)N.PressSounds.Add(L.Art[I].Image);
            break;
        }
    }
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
    for (const auto &C : Children) {
        const int32 First=Nodes.Num();
        Add(L, C.Index, Here, Original(*C.Owner, Nodes[Here].State).Size() + FIntPoint(1, 1), C.State,
            nullptr, nullptr, Depth + 1);
        if(!Nodes.IsValidIndex(First))continue;
        const uint32 Referent=Nodes[First].Attr(TEXT("referent"));
        if((Referent==4254656600u || Referent==4229490776u) && WindowMinimums.Contains(Nodes[Here].Window)){
            // Header controls stay at native size. Some layouts incorrectly
            // stretch them vertically when their formerly fixed parent grows.
            const auto Authored=Original(L.Controls[C.Index],Nodes[First].State);
            const auto Owner=Original(*C.Owner,Nodes[Here].State);
            if(Authored.Min.Y<=10){
                const FIntPoint Origin(Nodes[Here].Rect.Max.X-(Owner.Width()-Authored.Max.X)-17,
                                       Nodes[Here].Rect.Min.Y+Authored.Min.Y);
                const auto Shift=Origin-Nodes[First].Rect.Min;
                for(int32 I=First;I<Nodes.Num();++I){Nodes[I].Rect+=Shift;Nodes[I].Clip+=Shift;}
                Nodes[First].Rect=FIntRect(Origin,Origin+FIntPoint(17,17));
                Nodes[First].Clip=Intersect(Nodes[First].Rect,Nodes[Here].Clip);
            }
        }
    }
    // Keep native title text inside the header's usable area. The DAT title
    // templates span the entire frame, including the help/close controls;
    // long localized titles otherwise paint over those controls in Slate.
    int32 HeaderEnd=Nodes[Here].Rect.Max.X;
    for(int32 I=Here+1;I<Nodes.Num();++I)if(Nodes[I].Parent==Here) {
        const auto& C=Nodes[I];const uint32 RefId=C.Attr(TEXT("referent"));
        if(RefId==4254656600u || RefId==4229490776u)
            HeaderEnd=FMath::Min(HeaderEnd,C.Rect.Min.X-4);
    }
    if(HeaderEnd<Nodes[Here].Rect.Max.X)
        for(int32 I=Here+1;I<Nodes.Num();++I)if(Nodes[I].Parent==Here) {
            auto& C=Nodes[I];
            if(C.Kind==T(TEXT("text")) && (C.Attr(TEXT("referent"))==0xe104e194u || C.Rect.Height()<=32) &&
               C.Rect.Min.Y<=Nodes[Here].Rect.Min.Y+8) {
                // Later DATs anchor the standard title template to the bottom
                // as well. Its text still belongs in the 30-pixel caption,
                // even when the resizable content below it becomes taller.
                C.Rect.Max.Y=FMath::Min(C.Rect.Max.Y,C.Rect.Min.Y+30);
                C.Clip.Max.Y=FMath::Min(C.Clip.Max.Y,C.Rect.Max.Y);
                C.Clip.Min.X=FMath::Max(C.Clip.Min.X,Nodes[Here].Rect.Min.X+12);
                C.Clip.Max.X=FMath::Min(C.Clip.Max.X,HeaderEnd);
                C.Attributes.Add(T(TEXT("one_line")),1);
                C.FitHeader=true;
            }
        }
    // The native chat background's nine tiles share their boundary texels.
    // Slate alpha-composites each tile separately; drawing those shared pixels
    // twice creates dark horizontal/vertical seams through the text area.
    // Clip the overlaps without changing the DAT artwork's sampling positions.
    if(Desc->Id==0xdc5f0f48u)
    {
        FIntRect Center;
        for(int32 I=Here+1;I<Nodes.Num();++I)
            if(Nodes[I].Parent==Here && Nodes[I].Id==0xcb281c73u)Center=Nodes[I].Rect;
        if(!Empty(Center))for(int32 I=Here+1;I<Nodes.Num();++I)
            if(Nodes[I].Parent==Here){auto& Child=Nodes[I];
                if(Child.Rect.Min.X<Center.Min.X)Child.Clip.Max.X=FMath::Min(Child.Clip.Max.X,Center.Min.X);
                if(Child.Rect.Max.X>Center.Max.X)Child.Clip.Min.X=FMath::Max(Child.Clip.Min.X,Center.Max.X);
                if(Child.Rect.Min.Y<Center.Min.Y)Child.Clip.Max.Y=FMath::Min(Child.Clip.Max.Y,Center.Min.Y);
                if(Child.Rect.Max.Y>Center.Max.Y)Child.Clip.Min.Y=FMath::Max(Child.Clip.Min.Y,Center.Max.Y);
            }
    }
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
void FAC2NativeUI::Press(uint32 Id)
{
    if(Pressed==Id)return;
    Pressed=Id;
    const auto* N=Find(Id);
    if(!N || !N->PressState)return;
    // ResetMedia happens on the input event in retail. A click can begin and
    // end between two paints, so frame polling alone loses its sound entirely.
    if(auto* Audio=FAC2Audio::Get(AudioWorld.Get()))for(uint32 Sound:N->PressSounds)Audio->Play(Sound);
    uint32 Root=N->Id;
    for(int32 P=N->Parent;Nodes.IsValidIndex(P);P=Nodes[P].Parent)Root=HashCombineFast(Root,Nodes[P].Id);
    AudioStates.Add((uint64(Root)<<32)|N->Id,N->PressState);
}
void FAC2NativeUI::Prepare(FAC2AssetLibrary &Assets, FAC2Fonts &Fonts)
{
    auto* Audio=FAC2Audio::Get(AudioWorld.Get());
    TMap<uint64,uint32> NextStates;
    for (const auto &N : Nodes)
    {
        uint32 Root=N.Id;
        for(int32 P=N.Parent;Nodes.IsValidIndex(P);P=Nodes[P].Parent)Root=HashCombineFast(Root,Nodes[P].Id);
        const uint64 Key=(uint64(Root)<<32)|N.Id;
        if(Audio)
        {
            if(N.Parent==INDEX_NONE)if(uint32 Music=N.Attr(TEXT("music")))Audio->Music(Music);
            const auto* Before=AudioStates.Find(Key);
            if(!Before || *Before!=N.State)
                for(const auto& M:N.Media)if(M.Kind==5)Audio->Play(M.Image);
        }
        NextStates.Add(Key,N.State);
        for (const auto &M : N.Media)
            if (M.Kind == 1 || M.Kind == 3)
                LoadImage(M.Image, Assets);
        if (N.Kind == T(TEXT("text")))
            Fonts.Get(N.Attr(TEXT("font"), 0x4200000c));
    }
    AudioStates=MoveTemp(NextStates);
}
bool FAC2NativeUI::CursorAt(FVector2D Point,uint32& Image,FIntPoint& Hotspot) const
{
    auto Order=PaintOrder();
    for(int32 I=Order.Num()-1;I>=0;--I)
    {
        int32 Index=Order[I];
        if(!Nodes[Index].Clip.Contains(FIntPoint(FMath::FloorToInt(Point.X),FMath::FloorToInt(Point.Y))))continue;
        // Children inherit the pointer from their owning field. Never use a covered sibling's cursor.
        for(int32 Depth=0;Nodes.IsValidIndex(Index)&&Depth<64;++Depth)
        {
            for(const auto& Art:Nodes[Index].Media)
                if(Art.Kind==10 && Art.Image){Image=Art.Image;Hotspot=Art.Offset;return true;}
            Index=Nodes[Index].Parent;
        }
        return false;
    }
    return false;
}
void FAC2NativeNode::SetTextColor(const FLinearColor& Color)
{
    const TCHAR* Names[]={TEXT("font_color_red"),TEXT("font_color_green"),TEXT("font_color_blue")};
    for(int32 I=0;I<3;++I){uint32 Bits;const float Channel=Color.Component(I);FMemory::Memcpy(&Bits,&Channel,4);
        Attributes.Add(FAC2UILayout::Token(Names[I]),Bits);}
}
FLinearColor FAC2NativeNode::TextColor(uint32 Palette) const
{
    // Retail UIElement_Text packs RGBAColor directly into 8-bit glyph colors.
    // Those are display RGB, not linear lighting values. Slate expects linear
    // tint and encodes it for display; feeding the DAT floats directly brightens
    // cream, blue and orange a second time.
    const FString Prefix=Palette?FString::Printf(TEXT("font_color%u_"),Palette):TEXT("font_color_");
    auto Byte=[&](const TCHAR* Channel){return uint8(FMath::Clamp(Float(*(Prefix+Channel),1.f)*255.f,0.f,255.f));};
    auto Color=FLinearColor::FromSRGBColor(FColor(Byte(TEXT("red")),Byte(TEXT("green")),Byte(TEXT("blue"))));
    Color.A=Opacity;return Color;
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
    FString Fitted=Value;
    if(N.FitHeader) {
        const int32 Available=FMath::Min(Width,N.Clip.Width());
        if(Font->Width(Fitted)>Available) {
            while(!Fitted.IsEmpty() && Font->Width(Fitted+TEXT("..."))>Available)Fitted.LeftChopInline(1);
            Fitted+=TEXT("...");
        }
    }
    auto Lines = Font->Lines(Fitted, N.Attr(TEXT("one_line")) ? 0 : Width);
    float Y = N.Rect.Min.Y + Top;
    if (N.Attr(TEXT("center_vertically")))
        Y = N.Rect.Min.Y + (N.Rect.Height() - Lines.Num() * int32(Font->Height)) / 2.f;
    const FLinearColor Color=N.TextColor();
    PushClip(Out, G, N.Clip);
    for (const auto &Line : Lines)
    {
        float X = N.Rect.Min.X + Left;
        if (N.Attr(TEXT("center")))
            X += (Width - Font->Width(Line)) / 2;
        else if (N.Attr(TEXT("right_justify")))
            X += Width - Font->Width(Line);
        if(N.FitHeader)
            X=FMath::Clamp(X,float(N.Clip.Min.X),FMath::Max(float(N.Clip.Min.X),N.Clip.Max.X-Font->Width(Line)));
        // DAT fonts are bitmap glyphs; half-pixel centering blurs the face and outline.
        X=FMath::RoundToFloat(X);Y=FMath::RoundToFloat(Y);
        if (N.Attr(TEXT("font_outline")))
            for (FVector2D D : {FVector2D(-1, 0), FVector2D(1, 0), FVector2D(0, -1), FVector2D(0, 1)})
                Font->Draw(Out, ++Layer, G, Line, FVector2D(X, Y) + D, FLinearColor(0,0,0,N.Opacity));
        Font->Draw(Out, ++Layer, G, Line, FVector2D(X, Y), Color);
        Y += Font->Height;
    }
    Out.PopClip();
    return Layer;
}
int32 FAC2NativeUI::Paint(FSlateWindowElementList &Out, int32 Layer, const FGeometry &G, FAC2Fonts &Fonts,
                          TFunction<int32(const FAC2NativeNode &, int32)> Content, int32 FirstNode, int32 EndNode, uint32 Window) const
{
    for (int32 Index : PaintOrder())
    {
        if(Window && Nodes[Index].Window!=Window)continue;
        if(Index<FirstNode || Index>=EndNode)continue;
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
                const bool PanelFrame=N.ResizableFrame && B->ImageSize.X>=180 && B->ImageSize.Y>=24 && !Stretch;
                const float BorderX=N.ImageBorderX>0?N.ImageBorderX:PanelFrame?32.f:0.f;
                const float BorderY=N.ImageBorderY>0?N.ImageBorderY:PanelFrame?FMath::Min(30.f,B->ImageSize.Y*.25f):0.f;
                if(BorderX>0 && B->ImageSize.X>2*BorderX)
                {
                    FSlateBrush Frame=*B;
                    Frame.DrawAs=ESlateBrushDrawType::Box;
                    Frame.Margin=FMargin(BorderX/B->ImageSize.X,BorderY/B->ImageSize.Y,BorderX/B->ImageSize.X,BorderY/B->ImageSize.Y);
                    FSlateDrawElement::MakeBox(Out,++Layer,
                        G.ToPaintGeometry(FVector2D(N.Rect.Size()),FSlateLayoutTransform(Origin)),
                        &Frame,ESlateDrawEffect::None,FLinearColor(1,1,1,N.Float(TEXT("alpha_value"),1)));
                }
                else if (Step.X > 0 && Step.Y > 0)
                    for (float Y = Origin.Y; Y < N.Rect.Max.Y; Y += Step.Y)
                        for (float X = Origin.X; X < N.Rect.Max.X; X += Step.X)
                        {
                            if(!FMath::IsNearlyZero(N.Rotation))
                                FSlateDrawElement::MakeRotatedBox(Out,++Layer,
                                    G.ToPaintGeometry(Step,FSlateLayoutTransform(FVector2D(X,Y))),B,
                                    ESlateDrawEffect::None,N.Rotation,Step*.5f,FSlateDrawElement::RelativeToElement);
                            else
                            FSlateDrawElement::MakeBox(
                                Out, ++Layer, G.ToPaintGeometry(Step, FSlateLayoutTransform(FVector2D(X, Y))),
                                B, ESlateDrawEffect::None,
                                FLinearColor(1, 1, 1, N.Float(TEXT("alpha_value"), 1)));
                        }
            }
        if (Content)
            Layer = Content(N, Layer);
        Out.PopClip();
        if (N.Kind == T(TEXT("text")))
            Layer = PaintText(N, N.Text, Out, Layer, G, Fonts);
    }
    return Layer;
}
