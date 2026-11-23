#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
uint32 T(const TCHAR* Name){return FAC2UILayout::Token(Name);}
uint32 Attribute(const FAC2UIElement* E,const TCHAR* Name)
{
    if(E)for(const auto& S:E->States)if(S.Id==T(TEXT("stateless")))return S.Attributes.FindRef(T(Name));
    return 0;
}
}
FString SAC2GameUI::ShortcutHeightLabel(int32 Rows)
{
    return Native.Localized(*FString::Printf(TEXT("ID_Option_ShortcutHeight%d"),FAC2ShortcutGrid::Rows(Rows)));
}
void SAC2GameUI::BuildOptionChoiceMenu()
{
    if(OpenOptionChoice==INDEX_NONE)return;
    const bool LootMenu=OpenOptionChoice==1000;
    const bool FriendMenu=OpenOptionChoice==FriendModeMenu;
    const int32 Rows=FriendMenu?3:5;
    const FIntRect Anchor=FriendMenu?FriendModeAnchor:LootMenu?FellowshipLootAnchor:OptionChoiceAnchor;
    if((!FriendMenu && (LootMenu?(!ShowSocial || !Mode->Network || !Mode->Network->Fellows.IsEmpty()):(!ShowOptions || OptionsTab!=2))) || Anchor.IsEmpty() || OptionConfirmationOpen()){
        CloseOptionChoice();return;
    }
    const auto& Layout=Native.Layouts.FindChecked(0x22000001);
    auto Desc=[&](uint32 Id){return Layout.Controls.FindByPredicate([&](const auto& E){return E.Id==Id;});};
    const auto* Menu=FriendMenu?Desc(0x7ff25ebe):Layout.Find(TEXT("DropDownMenu"));
    const uint32 ItemId=Attribute(Menu,TEXT("text_item")),PopupId=Attribute(Menu,TEXT("popup"));
    const auto* Popup=Desc(PopupId);const auto* Item=Desc(ItemId);
    const uint32 ListId=Attribute(Menu,TEXT("listbox"));
    const auto* List=Layout.Controls.FindByPredicate([&](const auto& E){return E.Id==ListId && E.Parent==PopupId;});
    if(!Popup || !Item || !List){CloseOptionChoice();return;}
    // UIElement_Menu::Open sizes its popup to the items plus the authored
    // listbox margins, then centers it under the closed menu.
    const FIntPoint ItemSize=Item->Rect.Size()+FIntPoint(1,1);
    const FIntPoint Extent=FIntPoint(FriendMenu?Anchor.Width():ItemSize.X,Rows*ItemSize.Y)+Popup->Rect.Size()-List->Rect.Size();
    FIntPoint Origin(Anchor.Min.X+(Anchor.Width()-Extent.X)/2,Anchor.Max.Y-1);
    if(Attribute(Menu,TEXT("open_up")))Origin.Y=Anchor.Min.Y-Extent.Y;
    Origin.X=FMath::Clamp(Origin.X,0,FMath::Max(0,int32(Size.X)-Extent.X));
    Origin.Y=FMath::Clamp(Origin.Y,0,FMath::Max(0,int32(Size.Y)-Extent.Y));
    OptionChoicePopup=FIntRect(Origin,Origin+Extent);
    OptionDialogNodesStart=Native.Nodes.Num();
    // UIElement_ListBox::UpdateControls disables scrolling when all entries
    // fit. This DAT scrollbar has hide_when_disabled=false: retain its track
    // and arrow artwork, but hide the disabled thumb (Scrollbar::UpdateLayout).
    const uint32 WidgetId=T(TEXT("widget"));
    const bool HadWidgetVisibility=Native.Visibility.Contains(WidgetId),WidgetVisibility=Native.Visibility.FindRef(WidgetId);
    Native.Visibility.Add(WidgetId,false);
    Native.Template(0x22000001,PopupId,OptionChoicePopup,FIntRect(0,0,int32(Size.X),int32(Size.Y)));
    if(HadWidgetVisibility)Native.Visibility.Add(WidgetId,WidgetVisibility);else Native.Visibility.Remove(WidgetId);
    const auto* ListNode=Native.Find(ListId);if(!ListNode){CloseOptionChoice();return;}
    const auto Bounds=ListNode->Rect;
    const uint32 Normal=ListNode->Attr(TEXT("item_normal_state"),T(TEXT("normal")));
    const uint32 Selected=ListNode->Attr(TEXT("item_selected_state"),T(TEXT("highlight")));
    for(int32 I=0;I<Rows;++I){
        const FIntRect Row(Bounds.Min+FIntPoint(0,I*ItemSize.Y),Bounds.Min+FIntPoint(Bounds.Width(),(I+1)*ItemSize.Y));
        OptionChoiceItems.Add(Row);
        Native.Text.Add(ItemId,FriendMenu?FString(FriendModeLabel(I)):LootMenu?FellowshipLootLabel(I):ShortcutHeightLabel(I+1));
        const bool Chosen=FriendMenu?Mode->Network->FriendMode==(1u<<I):LootMenu?FellowshipLootChoice==I:int32(OptionValues[33])==I+1;
        Native.States.Add(ItemId,Row.Contains(FIntPoint(Mouse.X,Mouse.Y)) || Chosen?Selected:Normal);
        Native.Template(0x22000001,ItemId,Row,Bounds);
    }
    Native.Text.Remove(ItemId);Native.States.Remove(ItemId);
}
void SAC2GameUI::CloseOptionChoice()
{
    if(OpenOptionChoice==INDEX_NONE)return;
    OpenOptionChoice=PressedOptionChoice=INDEX_NONE;PressedAction=NAME_None;Native.Pressed=0;
    OptionChoiceItems.Reset();OptionChoicePopup=FIntRect();
    auto& App=FSlateApplication::Get();const auto Focus=OptionChoicePreviousFocus.Pin();OptionChoicePreviousFocus.Reset();
    if(App.GetKeyboardFocusedWidget().Get()==this){
        if(Focus && Focus.Get()!=this && Focus->GetVisibility().IsVisible())App.SetKeyboardFocus(Focus);
        else App.SetAllUserFocusToGameViewport();
    }
}
bool SAC2GameUI::OptionChoiceMouseDown(FKey Key)
{
    const FIntPoint Point(Mouse.X,Mouse.Y);
    if(OpenOptionChoice!=INDEX_NONE){
        PressedOptionChoice=Key==EKeys::LeftMouseButton?OptionChoiceItems.IndexOfByPredicate([&](const auto& R){return R.Contains(Point);}):INDEX_NONE;
        if(Key==EKeys::LeftMouseButton && PressedOptionChoice==INDEX_NONE && OptionChoicePopup.Contains(Point))return true;
        if(PressedOptionChoice==INDEX_NONE){CloseOptionChoice();Rebuild();}
        return true;
    }
    const bool LootMenu=ShowSocial && FellowshipLootAnchor.Contains(Point) && WindowOwns(T(TEXT("SocialField")),Mouse);
    const bool FriendMenu=ShowSocial && SocialTab==2 && FriendModeAnchor.Contains(Point) && WindowOwns(T(TEXT("SocialField")),Mouse);
    if(Key!=EKeys::LeftMouseButton || (!FriendMenu && !LootMenu && (!ShowOptions || !OptionChoiceAnchor.Contains(Point) ||
       !AllowsPanelButton(TEXT("OptionChoice33"),Mouse))))return false;
    EndChat();CancelCameraGesture();PressedAction=NAME_None;Native.Pressed=0;
    OpenOptionChoice=FriendMenu?FriendModeMenu:LootMenu?1000:33;PressedOptionChoice=INDEX_NONE;
    auto& App=FSlateApplication::Get();OptionChoicePreviousFocus=App.GetKeyboardFocusedWidget();
    Rebuild();App.SetKeyboardFocus(AsShared());return true;
}
bool SAC2GameUI::OptionChoiceMouseUp(FKey Key)
{
    if(OpenOptionChoice==INDEX_NONE)return false;
    if(Key==EKeys::LeftMouseButton && OptionChoiceItems.IsValidIndex(PressedOptionChoice)){
        if(OptionChoiceItems[PressedOptionChoice].Contains(FIntPoint(Mouse.X,Mouse.Y))){
            if(OpenOptionChoice==1000)FellowshipLootChoice=PressedOptionChoice;
            else if(OpenOptionChoice==FriendModeMenu)Mode->Network->FriendAction(TEXT("mode"),{},1u<<PressedOptionChoice);
            else OptionValues[33]=float(PressedOptionChoice+1);
        }
        CloseOptionChoice();Rebuild();
    }
    return true;
}
