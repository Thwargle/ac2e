#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2ShortcutKeys.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/World.h"

void SAC2GameUI::LoadShortcuts()
{
    const auto* P=Mode->Characters.Active();if(!P)return;
    const auto* Net=Mode->Network.Get();
    const FString Legacy=TEXT("AC2.Shortcuts.")+P->Id;
    const FString Owner=Net && !Net->ShortcutScope.IsEmpty()?TEXT("AC2.Shortcuts.")+Net->ShortcutScope+TEXT(".")+P->Id:Legacy;
    if(ShortcutOwner==Owner){RefreshShortcutHeightOption();SyncGameOptions();SyncShortcuts();return;}
    ShortcutOwner=Owner;ShortcutSet=0;ShortcutRows=1;for(auto& Binding:Shortcuts)Binding={};
    LoadedShortcutSession.Invalidate();LoadedShortcutRevision=0;
    bool Imported=false;GConfig->GetBool(*Owner,TEXT("ServerImported"),Imported,GGameUserSettingsIni);
    FString Saved;
    FString Source=Owner;
    bool Found=GConfig->GetString(*Source,TEXT("SlotsV2"),Saved,GGameUserSettingsIni) || GConfig->GetString(*Source,TEXT("Slots"),Saved,GGameUserSettingsIni);
    if(!Found && !Imported && Owner!=Legacy){Source=Legacy;Found=GConfig->GetString(*Source,TEXT("SlotsV2"),Saved,GGameUserSettingsIni) || GConfig->GetString(*Source,TEXT("Slots"),Saved,GGameUserSettingsIni);}
    ImportLocalShortcuts=Found && !Imported;
    GConfig->GetInt(*Source,TEXT("Rows"),ShortcutRows,GGameUserSettingsIni);ShortcutRows=FAC2ShortcutGrid::Rows(ShortcutRows);
    if(Found){
        TArray<FString> Values;Saved.ParseIntoArray(Values,TEXT(","),false);
        for(int32 I=0;I<FMath::Min(100,Values.Num());++I)Shortcuts[I]=FAC2Shortcut::Decode(Values[I]);
        GConfig->GetInt(*Source,TEXT("Set"),ShortcutSet,GGameUserSettingsIni);ShortcutSet=FMath::Clamp(ShortcutSet,0,9);
    }else{
        const uint32 Human[]={0xb0,0x60,0xcf},Lugian[]={0x166,0x111,0x1a0},Tumerok[]={0x2cc,0x2a0,0x316};
        for(int32 I=0;I<3;++I)Shortcuts[I]=FAC2Shortcut::ForSkill((P->Race==2?Lugian:P->Race==4?Tumerok:Human)[I]);
    }
    ShortcutScroll.Snap(ShortcutSet*ShortcutRowHeight);ShortcutTopRow=ShortcutSet;
    RefreshShortcutHeightOption(true);SyncGameOptions();SyncShortcuts();
}

void SAC2GameUI::SyncShortcuts()
{
    if(TestingShortcuts)return;
    auto* Net=Mode->Network.Get();const auto* P=Mode->Characters.Active();
    if(!Net || !P || !Net->IsWorld() || Net->PlayerId!=P->Id || Net->ServerShortcuts.Num()!=100 ||
       (LoadedShortcutSession==Net->ShortcutSession && LoadedShortcutRevision==Net->ShortcutRevision))return;
    // Legacy local customizations are imported once. Future logins use the
    // authoritative profile, including an intentionally empty array.
    if(!ImportLocalShortcuts || !Net->SaveShortcuts(Shortcuts))
        for(int32 I=0;I<100;++I)Shortcuts[I]=Net->ServerShortcuts[I];
    ImportLocalShortcuts=false;LoadedShortcutSession=Net->ShortcutSession;LoadedShortcutRevision=Net->ShortcutRevision;
    GConfig->SetBool(*ShortcutOwner,TEXT("ServerImported"),true,GGameUserSettingsIni);
    SaveShortcuts();
}

bool SAC2GameUI::SmokeShortcutKey(FKey Key,bool Shift,bool Control)
{
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(!PC || !PC->PlayerInput || !PC->InputComponent)return false;
    auto& App=FSlateApplication::Get();
    const FModifierKeysState Modifiers(Shift,false,Control,false,false,false,false,false,false);
    // Route through the focused Slate widget and the real controller bindings.
    // Pump before releasing modifiers, just as separate input frames do.
    if(Shift)App.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftShift,Modifiers,0,false,0,0));
    if(Control)App.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftControl,Modifiers,0,false,0,0));
    App.ProcessKeyDownEvent(FKeyEvent(Key,Modifiers,0,false,0,0));
    PC->PlayerInput->ProcessInputStack({PC->InputComponent},.016f,false);
    App.ProcessKeyUpEvent(FKeyEvent(Key,Modifiers,0,false,0,0));
    if(Shift)App.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftShift,FModifierKeysState(),0,false,0,0));
    if(Control)App.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftControl,FModifierKeysState(),0,false,0,0));
    PC->PlayerInput->ProcessInputStack({PC->InputComponent},.016f,false);
    return true;
}
bool SAC2GameUI::SmokeShortcuts(FString& Error)
{
    LoadShortcuts();
    const FString OriginalOwner=ShortcutOwner;const int32 OriginalSet=ShortcutSet,OriginalRows=ShortcutRows;
    const bool OriginalLock=ShortcutLocked;ShortcutLocked=true;
    const bool OriginalInstant=ShortcutInstantScroll;ShortcutInstantScroll=true;
    const bool OriginalNumbers=ShowShortcutNumbers;ShowShortcutNumbers=true;
    TArray<FAC2Shortcut> Original;Original.Append(Shortcuts,100);
    TestingShortcuts=true;
    ON_SCOPE_EXIT{for(int32 I=0;I<100;++I)Shortcuts[I]=Original[I];ShortcutSet=OriginalSet;ShortcutRows=OriginalRows;ShortcutOwner=OriginalOwner;
        ShortcutLocked=OriginalLock;ShortcutInstantScroll=OriginalInstant;ShowShortcutNumbers=OriginalNumbers;TestingShortcuts=false;Rebuild();};
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto Position=[&](FName Key,FVector2D& P){const auto* R=Buttons.Find(Key);if(!R)return false;
        P=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);return true;};
    auto Click=[&](FName Key){FVector2D P;if(!Position(Key,P))return false;
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
    // Rebuild normally loads the active character. Keep that load idempotent
    // while using an otherwise empty page, and restore the original on exit.
    ShortcutOwner=OriginalOwner;ShortcutSet=1;ShortcutRows=1;for(int32 I=10;I<60;++I)Shortcuts[I]={};Rebuild();
    auto Drag=[&](FName From,FName To,bool Shift=true){FVector2D A,B;if(!Position(From,A)||!Position(To,B))return false;
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState(Shift,false,false,false,false,false,false,false,false)));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
    if(!Drag(TEXT("LearnedSkill96"),TEXT("ShortcutSlot9"),false) || Shortcuts[19].Skill!=96)return Fail(TEXT("Learned active skill did not drag onto an empty hotbar slot"));
    if(!Drag(TEXT("ShortcutSlot9"),TEXT("ShortcutSlot7"),false) || Shortcuts[19].Skill!=96 || !Shortcuts[17].IsEmpty())
        return Fail(TEXT("Locked hotbar allowed a move without Shift"));
    if(!Drag(TEXT("ShortcutSlot9"),TEXT("ShortcutSlot7")) || Shortcuts[17].Skill!=96 || !Shortcuts[19].IsEmpty())return Fail(TEXT("Hotbar slot drag did not move the binding"));
    FVector2D SkillPoint;if(!Position(TEXT("LearnedSkill96"),SkillPoint))return Fail(TEXT("Skill book quick-add target missing"));
    OnMouseButtonDoubleClick(GetCachedGeometry(),FPointerEvent(0,SkillPoint,SkillPoint,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
    if(Shortcuts[10].Skill!=96)return Fail(TEXT("Skill double-click did not fill the current page's first slot"));
    Shortcuts[10]={};Rebuild();
    if(!Click(TEXT("ShortcutUp")) || ShortcutSet!=0)return Fail(TEXT("Hotbar up arrow did not select the previous set"));
    if(!Click(TEXT("ShortcutDown")) || ShortcutSet!=1 || Shortcuts[17].Skill!=96)return Fail(TEXT("Hotbar down arrow lost its bindings"));
    const auto* Number=Native.Find(TEXT("SetNumberDisplay"));
    if(!Number || Number->Text!=TEXT("2"))return Fail(TEXT("Hotbar page number did not update"));
    const auto OneRow=Native.Bounds(TEXT("ShortcutField"));
    auto Resize=[&](int32 Delta){const auto R=Native.Bounds(TEXT("ShortcutField"));
        const FVector2D A=GetCachedGeometry().LocalToAbsolute(FVector2D(R.Min.X+R.Width()/2,R.Min.Y+5)*Scale),B=A+FVector2D(0,Delta)*Scale;
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    Resize(-128);
    if(ShortcutRows!=5 || Native.Bounds(TEXT("ShortcutField")).Max!=OneRow.Max || Native.Bounds(TEXT("ShortcutListBox")).Height()!=160)
        return Fail(TEXT("Native hotbar grip did not expand to five rows with a fixed bottom edge"));
    if(const auto* Label=Native.Find(TEXT("NumberDisplay"));!Label || Label->Text!=TEXT("1"))
        return Fail(TEXT("Empty shortcut does not show its native corner number"));
    if(!Drag(TEXT("ShortcutSlot7"),TEXT("ShortcutSlot49")) || Shortcuts[59].Skill!=96 || !Shortcuts[17].IsEmpty())
        return Fail(TEXT("Expanded hotbar failed to move a skill to its fifth row"));
    if(!Click(TEXT("ShortcutDown")) || ShortcutSet!=2 || !Buttons.Contains(TEXT("ShortcutSlot39")))
        return Fail(TEXT("Expanded hotbar page did not advance one row"));
    Resize(128);
    if(ShortcutRows!=1 || Buttons.Contains(TEXT("ShortcutSlot10")) || Shortcuts[59].Skill!=96 || Native.Bounds(TEXT("ShortcutField"))!=OneRow)
        return Fail(TEXT("Shrinking hotbar lost hidden bindings or shifted its frame"));
    Resize(-128);ShortcutSet=9;Rebuild();
    if(!Buttons.Contains(TEXT("ShortcutSlot9")) || Buttons.Contains(TEXT("ShortcutSlot10")))
        return Fail(TEXT("Last hotbar page did not disable padding after binding 100"));
    if(!Click(TEXT("ShortcutDown")) || ShortcutSet!=0 || !Click(TEXT("ShortcutUp")) || ShortcutSet!=9)
        return Fail(TEXT("Expanded hotbar arrows did not wrap between pages ten and one"));
    FSlateApplication::Get().SetAllUserFocusToGameViewport();
    if(!SmokeShortcutKey(EKeys::Eight,false,true) || ShortcutSet!=7)
        return Fail(TEXT("Ctrl+8 did not select page eight through the viewport"));
    if(!SmokeShortcutKey(EKeys::Hyphen) || ShortcutSet!=8 || !SmokeShortcutKey(EKeys::Hyphen,true) || ShortcutSet!=7)
        return Fail(TEXT("Minus/Shift+minus did not change hotbar pages through the viewport"));
    BeginChat();
    if(!HasChatFocus())return Fail(TEXT("Shortcut test could not focus chat"));
    SmokeShortcutKey(EKeys::One,false,true);SmokeShortcutKey(EKeys::Hyphen);
    const bool Protected=ShortcutSet==7;EndChat();
    if(!Protected)return Fail(TEXT("Typing in chat changed hotbar pages"));
    return true;
}
void SAC2GameUI::SaveShortcuts()
{
    if(ShortcutOwner.IsEmpty() || TestingShortcuts)return;
    SaveGameOptions();
    if(auto* Net=Mode->Network.Get();Net && Net->IsWorld() && Net->ServerShortcuts.Num()==100 &&
       LoadedShortcutSession==Net->ShortcutSession && LoadedShortcutRevision==Net->ShortcutRevision){
        if(!Net->SaveShortcuts(Shortcuts))for(int32 I=0;I<100;++I)Shortcuts[I]=Net->ServerShortcuts[I];
    }
    FString Saved;for(int32 I=0;I<100;++I){if(I)Saved+=TEXT(",");Saved+=Shortcuts[I].Encode();}
    GConfig->SetString(*ShortcutOwner,TEXT("SlotsV2"),*Saved,GGameUserSettingsIni);
    GConfig->SetInt(*ShortcutOwner,TEXT("Set"),ShortcutSet,GGameUserSettingsIni);
    GConfig->SetInt(*ShortcutOwner,TEXT("Rows"),ShortcutRows,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
}
bool SAC2GameUI::ShortcutAllowed(uint32 Skill) const
{
    const auto* Net=Mode->Network.Get();
    return Skill && Net && Net->UntrainingSkill!=Skill && Net->Skills.ContainsByPredicate([&](const auto& S){return S.Id==Skill&&S.Trained;});
}
bool SAC2GameUI::RecipeShortcutAllowed(uint32 Recipe) const
{
    const auto* Net=Mode->Network.Get();const auto* P=Mode->Characters.Active();
    return Recipe && P && Net && Net->IsWorld() && Net->PlayerId==P->Id &&
        Net->CraftRecipes.Contains(Recipe) && Net->CraftCatalog.Contains(Recipe);
}
bool SAC2GameUI::ShortcutInputBlocked() const
{
    return Frontend || !Mode.IsValid() || !Mode->InWorld() || Editing || OptionConfirmationOpen() || OpenOptionChoice!=INDEX_NONE ||
        (Entry && Entry->HasKeyboardFocus()) || (FellowshipEntry && FellowshipEntry->HasKeyboardFocus());
}
void SAC2GameUI::SelectShortcutSet(int32 Set)
{
    LoadShortcuts();
    if(ShortcutScroll.Goal!=ShortcutSet*ShortcutRowHeight)ShortcutScroll.Snap(ShortcutSet*ShortcutRowHeight);
    ShortcutSet=(Set%10+10)%10;
    const auto* List=Native.Find(TEXT("ShortcutListBox"));
    const double Duration=List?List->Float(TEXT("smooth_movement_duration"),.25f):.25;
    if(ShortcutInstantScroll)ShortcutScroll.Snap(ShortcutSet*ShortcutRowHeight);
    else ShortcutScroll.Start(ShortcutSet*ShortcutRowHeight,ShortcutTime,Duration);
    SaveShortcuts();Rebuild();
}
bool SAC2GameUI::HandleShortcutAction(uint32 Action)
{
    if(!FAC2ShortcutKeys::IsAction(Action))return false;
    if(ShortcutInputBlocked())return true;
    LoadShortcuts();
    // Legions 7160 gmShortcutUI::ListenToMessage (00438AC0). The second
    // row requires two visible rows and cannot extend beyond binding 100.
    if(Action<=0x41000034)ActivateShortcut(int32(Action-0x4100002b));
    else if(Action<=0x4100003e)SelectShortcutSet(int32(Action-0x41000035));
    else if(Action==0x4100003f)SelectShortcutSet(ShortcutTopRow+1);
    else if(Action==0x41000040)SelectShortcutSet(ShortcutTopRow-1);
    else ActivateShortcut(10+int32(Action-0x4100005f));
    return true;
}
void SAC2GameUI::ActivateShortcut(int32 Slot)
{
    if(ShortcutInputBlocked())return;
    LoadShortcuts();UseShortcutBinding(FAC2ShortcutGrid::Index(ShortcutTopRow,Slot,ShortcutRows));
}
int32 SAC2GameUI::ShortcutHitIndex(FName Key) const
{
    const auto* Index=ShortcutHitIndices.Find(Key);return Index?*Index:INDEX_NONE;
}
void SAC2GameUI::UseShortcutBinding(int32 Index)
{
    if(ShortcutInputBlocked() || Index<0 || Index>=100)return;
    const auto Binding=Shortcuts[Index];if(Binding.IsEmpty())return;
    if(Binding.IsEnumeration)return; // Preserve native enum commands until their execution is implemented.
    // Native ShortcutInfo type 40000003 is passed to CWInterface::ProcessText.
    // Use the same path as Enter, so chat/command validation stays authoritative.
    if(!Binding.Text.IsEmpty()){Submit(Binding.Text);return;}
    if(Binding.Recipe){
        if(RecipeShortcutAllowed(Binding.Recipe))Mode->Network->OpenCraft(Binding.Recipe);
        return;
    }
    if(!Binding.Item.IsEmpty()){
        const int32 ItemIndex=ShortcutItemIndex(Binding.Item);
        if(Mode->Network && ItemIndex!=INDEX_NONE)Mode->Network->UseItem(ItemIndex);
        return;
    }
    const uint32 Skill=Binding.Skill;
    if(!Mode->Network){if(Index%10==0)Mode->Attack();else if(Index%10==1)Mode->CastBurst(Mode->PreviewEffect);return;}
    if(!ShortcutAllowed(Skill))return;
    if(Skill==0xb0 || Skill==0x166 || Skill==0x2cc)Mode->Network->Attack();
    else if(Skill==0x343)Mode->Network->Recall();else Mode->Network->CastSkill(Skill);
}
int32 SAC2GameUI::ShortcutItemIndex(const FString& Id) const
{
    if(Id.IsEmpty() || !Mode.IsValid())return INDEX_NONE;
    const auto* P=Mode->Characters.Active();const auto* Net=Mode->Network.Get();
    if(!P || !Net || Net->PlayerId!=P->Id || !Net->InventoryKnown || !Net->OwnsInventoryItem(Id))return INDEX_NONE;
    return P->Items.IndexOfByPredicate([&](const auto& I){return I.NetworkId==Id;});
}
TArray<FAC2IconLayer> SAC2GameUI::ShortcutLayers(const FAC2Shortcut& Binding) const
{
    TArray<FAC2IconLayer> Layers;
    if(!Binding.Text.IsEmpty()){
        if(const auto* Label=Native.Find(TEXT("AliasText")))
            if(const uint32 Icon=Label->Attr(TEXT("icon")))Layers.Add({0,Icon,0xffffffff});
        return Layers;
    }
    if(Binding.Recipe){
        if(RecipeShortcutAllowed(Binding.Recipe)){
            if(const uint32 Icon=RecipeIcon(Binding.Recipe))Layers.Add({0,Icon,0xffffffff});
        }return Layers;
    }
    if(Binding.Skill){Layers.Add({0,const_cast<FAC2SkillData&>(SkillData).Get(Binding.Skill).Icon,0xffffffff});return Layers;}
    const int32 Index=ShortcutItemIndex(Binding.Item);
    if(Index!=INDEX_NONE){
        const auto& Item=Mode->Characters.Active()->Items[Index];
        if(Item.Visual)Layers=Assets->IconLayers(Item.Visual,Item.Aspects);
        if(Layers.IsEmpty())if(const auto* Definition=Mode->CharacterCatalog.Items.Find(Item.Definition))Layers.Add({0,Definition->Icon,0xffffffff});
    }
    return Layers;
}
void SAC2GameUI::AddSkillShortcut(uint32 Skill)
{
    if(ShortcutInputBlocked() || !ShortcutAllowed(Skill))return;
    const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==Skill;});
    if(!Node || Node->Passive)return;
    LoadShortcuts();
    // gmShortcutUI::ListenToMessage: double-clicking a draggable skill fills
    // the first empty slot in this ten-slot set, even with more rows visible.
    for(int32 I=ShortcutTopRow*10;I<FMath::Min(100,ShortcutTopRow*10+10);++I)if(Shortcuts[I].IsEmpty()){
        Shortcuts[I]=FAC2Shortcut::ForSkill(Skill);SaveShortcuts();Rebuild();return;
    }
}
void SAC2GameUI::UpdateShortcutDrag(bool ShiftDown)
{
    if(DragShortcut==INDEX_NONE || DragBinding.IsEmpty() || FVector2D::Distance(Mouse,ItemPressPoint)<=6)return;
    ShortcutDragAttempted=true;
    if(DragBinding.Skill && Mode->Network && Mode->Network->SkillBusy(DragBinding.Skill)){
        ShortcutDragStarted=false;return;
    }
    // Admin 7148 gmShortcutUI_DOCB (00435E70) checks Shift when the
    // outbound drag starts. DDCB deliberately permits incoming assignments.
    if(!ShortcutLocked || ShiftDown)ShortcutDragStarted=true;
}
bool SAC2GameUI::FinishShortcutDrag()
{
    if(!ShortcutDragAttempted && FVector2D::Distance(Mouse,ItemPressPoint)<=6)return false;
    const bool FromInventory=DragBinding.IsEmpty() && !DragItem.IsEmpty();
    const auto Binding=FromInventory?FAC2Shortcut::ForItem(DragItem):DragBinding;
    if(Binding.IsEmpty())return false;
    int32 Destination=INDEX_NONE;
    for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("ShortcutSlot")) && AllowsPanelButton(B.Key,Mouse) && B.Value.ContainsPoint(Mouse)){
        Destination=ShortcutHitIndex(B.Key);break;
    }
    if(FromInventory && Destination==INDEX_NONE)return false;
    const bool SourceValid=DragShortcut==INDEX_NONE || (Shortcuts[DragShortcut]==Binding &&
        !(Binding.Skill && Mode->Network && Mode->Network->SkillBusy(Binding.Skill)));
    const bool ItemValid=Binding.Item.IsEmpty() || ShortcutItemIndex(Binding.Item)!=INDEX_NONE;
    const bool RecipeValid=!Binding.Recipe || RecipeShortcutAllowed(Binding.Recipe);
    if((DragShortcut==INDEX_NONE || ShortcutDragStarted) && SourceValid && ItemValid && (RecipeValid || DragShortcut!=INDEX_NONE)){
        if(Destination!=INDEX_NONE){
            // Retail moves a shortcut and replaces an occupied destination;
            // dragging an inventory object creates a reference, never a transfer.
            if(DragShortcut!=INDEX_NONE && DragShortcut!=Destination)Shortcuts[DragShortcut]={};
            Shortcuts[Destination]=Binding;SaveShortcuts();
        }else if(DragShortcut!=INDEX_NONE && Mouse.X>=0 && Mouse.Y>=0 && Mouse.X<Size.X && Mouse.Y<Size.Y && !OwnsPointer(Mouse)){
            Shortcuts[DragShortcut]={};SaveShortcuts();
        }
    }
    DragBinding={};DragShortcut=INDEX_NONE;ShortcutDragStarted=ShortcutDragAttempted=false;
    DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;Rebuild();
    return true;
}
void SAC2GameUI::InspectShortcut(int32 Slot)
{
    const int32 Index=ShortcutHitIndex(FName(*FString::Printf(TEXT("ShortcutSlot%d"),Slot)));if(Index==INDEX_NONE)return;
    const auto Binding=Shortcuts[Index];
    if(!Binding.Text.IsEmpty())return; // Retail has no examination request for text.
    if(Binding.Recipe){InspectRecipe(Binding.Recipe);return;}
    if(Binding.Skill){
        ExaminedRecipe=0;ExaminedSkill=Binding.Skill;ShowStats=true;ShowOptions=false;ExamineScroll=0;SelectedItem=INDEX_NONE;
        SelectedNetworkItem.Reset();InspectedObject.Reset();InspectedMob.Reset();
    }else if(const int32 ItemIndex=ShortcutItemIndex(Binding.Item);ItemIndex!=INDEX_NONE)SelectItem(ItemIndex);
    Rebuild();
}
void SAC2GameUI::BuildShortcuts()
{
    LoadShortcuts();const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    ShortcutHitIndices.Reset();
    const auto* Net=Mode->Network.Get();const auto* P=Mode->Characters.Active();
    // Do not erase saved bindings while login, travel or item creation is still
    // in flight. The complete inventory list establishes ownership separately
    // from the visual object records, as in retail's pending-create list.
    if(Net && P && Net->IsWorld() && Net->InventoryKnown && Net->PlayerId==P->Id &&
       (Net->ShortcutScope.IsEmpty() || (LoadedShortcutSession==Net->ShortcutSession && LoadedShortcutRevision==Net->ShortcutRevision))){
        bool Changed=false;
        for(auto& Binding:Shortcuts)if(!Binding.Item.IsEmpty() && !Net->OwnsInventoryItem(Binding.Item)){Binding={};Changed=true;}
        if(Changed)SaveShortcuts();
    }
    const FIntRect Area=Native.Bounds(TEXT("ShortcutListBox"));
    const auto* Frame=Native.Find(TEXT("ShortcutField"));
    const int32 RowHeight=FMath::Max(1,Frame?int32(Frame->Attr(TEXT("row_height"),32)):32);
    ShortcutRowHeight=RowHeight;
    // Loads, explicit page restores and enabling instant scroll settle the view.
    if(ShortcutInstantScroll || ShortcutScroll.Goal!=ShortcutSet*RowHeight)ShortcutScroll.Snap(ShortcutSet*RowHeight);
    const int32 View=FMath::Clamp(ShortcutScroll.View(ShortcutTime),0,9*RowHeight);
    ShortcutTopRow=View/RowHeight;
    for(auto& N:Native.Nodes)if(N.Id==T(TEXT("SetNumberDisplay")))N.Text=ShowShortcutNumbers?FString::FromInt(ShortcutTopRow+1):FString();
    const int32 ColumnWidth=Area.Width()/FAC2ShortcutGrid::Columns;
    const int32 VisibleRows=FMath::DivideAndRoundUp(View%RowHeight+Area.Height(),RowHeight);
    const uint32 SavedHover=Native.Hovered,SavedPress=Native.Pressed;
    for(int32 I=0;I<VisibleRows*10 && ColumnWidth>0;++I){
        const FIntPoint Origin=Area.Min+FIntPoint((I%10)*ColumnWidth,(ShortcutTopRow+I/10)*RowHeight-View);
        const FIntRect Rect(Origin,Origin+FIntPoint(ColumnWidth,RowHeight));
        const int32 Absolute=ShortcutTopRow*10+I,Index=Absolute<100?Absolute:INDEX_NONE;
        const FIntRect Clip(FIntPoint(FMath::Max(Rect.Min.X,Area.Min.X),FMath::Max(Rect.Min.Y,Area.Min.Y)),
            FIntPoint(FMath::Min(Rect.Max.X,Area.Max.X),FMath::Min(Rect.Max.Y,Area.Max.Y)));
        if(Clip.IsEmpty())continue;
        const FName Key(*FString::Printf(TEXT("ShortcutSlot%d"),I));
        if(Index!=INDEX_NONE){Buttons.Add(Key,FSlateRect(Clip.Min.X,Clip.Min.Y,Clip.Max.X,Clip.Max.Y));ButtonWindows.Add(Key,T(TEXT("ShortcutField")));ShortcutHitIndices.Add(Key,Index);}
        const auto Binding=Index==INDEX_NONE?FAC2Shortcut():Shortcuts[Index];
        const int32 ItemIndex=ShortcutItemIndex(Binding.Item);
        const bool Available=Index!=INDEX_NONE && (Binding.IsEmpty() || !Binding.Text.IsEmpty() || (Binding.Recipe?RecipeShortcutAllowed(Binding.Recipe):Binding.Skill?(!Net||ShortcutAllowed(Binding.Skill)):ItemIndex!=INDEX_NONE));
        bool EquipmentBlocked=false;
        const FString Equipment=Binding.Skill?SkillEquipmentHint(Binding.Skill,EquipmentBlocked):FString();
        const auto ServerNow=Net?Net->ServerTime():TOptional<double>();
        const auto Reset=Binding.Skill && ServerNow.IsSet() && SkillData.Get(Binding.Skill).HasImplements?
            Net->SkillCooldown(Binding.Skill,SkillData.Get(Binding.Skill).ResetSeconds,ServerNow.GetValue()):FAC2SkillCooldown();
        const double Remaining=ServerNow.IsSet()?Reset.Remaining(ServerNow.GetValue()):0.;
        // gmShortcutUI::CheckSkill/UpdateTimeLeft: equipment failure covers the
        // icon completely; a live server reset moves the same native fill down.
        Native.Visibility.Add(T(TEXT("TimeLeftMeter")),Available && (EquipmentBlocked || Remaining>0));
        Native.Meters.Add(T(TEXT("TimeLeftMeter")),EquipmentBlocked?0.f:ServerNow.IsSet()?Reset.Position(ServerNow.GetValue()):1.f);
        // gmShortcutUI::MakeElement labels empty slots when enabled. These
        // small corner labels are separate from the fixed digits in 41008D29.
        Native.Text.Add(T(TEXT("NumberDisplay")),ShowShortcutNumbers && Index!=INDEX_NONE && Binding.IsEmpty()?FString::FromInt((I+1)%10):FString());
        Native.Text.Add(T(TEXT("AliasText")),Binding.Text);
        const bool Pending=Binding.Skill && Net && Net->SkillPending(Binding.Skill);
        const bool Active=Binding.Skill && Net && Net->SkillActive(Binding.Skill);
        const bool Toggled=Binding.Skill && Net && Net->IsWorld() && !Net->Dead && Net->ToggledSkills.Contains(Binding.Skill);
        Native.States.Add(T(TEXT("ShortcutTemplate")),T(!Available?TEXT("ghosted"):Active?TEXT("active"):Pending?TEXT("pending"):Toggled?TEXT("toggled"):TEXT("stateless")));
        // Template children share IDs, but pointer feedback belongs to one slot.
        const bool UnderPointer=Index!=INDEX_NONE && Clip.Contains(FIntPoint(Mouse.X,Mouse.Y)) && AllowsPanelButton(Key,Mouse);
        Native.Hovered=UnderPointer?T(TEXT("ShortcutButton")):0;
        Native.Pressed=UnderPointer && PressedAction==Key && (DragShortcut==INDEX_NONE || DragShortcut==Index)?T(TEXT("ShortcutButton")):0;
        const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ShortcutTemplate"),Rect,Area,T(TEXT("ShortcutListBox")));
        for(int32 J=First;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Id==T(TEXT("ShortcutIcon"))){
            const auto& N=Native.Nodes[J];
            for(const auto& Layer:ShortcutLayers(Binding))Icons.Add({N.Rect,N.Clip,Layer.Image,1,FLinearColor(FColor(Layer.Color))*(Available?1.f:.45f),J});
        }
        if(Binding.IsEmpty())continue;
        FString Name=!Binding.Text.IsEmpty()?TEXT("Alias: ")+Binding.Text:Binding.Recipe?TEXT("Recipe unavailable"):Binding.Skill?SkillData.Get(Binding.Skill).Name:TEXT("Waiting for item data...");
        if(Binding.Recipe && Available)Mode->Network->CraftCatalog.FindRef(Binding.Recipe)->TryGetStringField(TEXT("name"),Name);
        if(ItemIndex!=INDEX_NONE){const auto& Item=P->Items[ItemIndex];
            if(const auto* Definition=Mode->CharacterCatalog.Items.Find(Item.Definition))Name=Definition->Name;
            if(Item.Quantity>1)Name+=FString::Printf(TEXT(" (%d)"),Item.Quantity);
        }
        Hints.Add(Key,Name+(Remaining>0?FString::Printf(TEXT("\nReady in %.1f seconds"),Remaining):FString())+(Equipment.IsEmpty()?FString():TEXT("\n")+Equipment)+(!Binding.Text.IsEmpty()?TEXT("\nClick to submit text."):Binding.Recipe?TEXT("\nClick to open recipe; right-click to examine."):TEXT("\nClick to use; right-click to examine."))+
            (ShortcutLocked?TEXT("\nHold Shift and drag to move or remove this shortcut."):TEXT("\nDrag to move; drag into the world to remove the shortcut.")));
    }
    Native.Hovered=SavedHover;Native.Pressed=SavedPress;
    Native.Visibility.Add(T(TEXT("TimeLeftMeter")),false);Native.Meters.Remove(T(TEXT("TimeLeftMeter")));
    Native.States.Remove(T(TEXT("ShortcutTemplate")));Native.Text.Remove(T(TEXT("NumberDisplay")));Native.Text.Remove(T(TEXT("AliasText")));
}
