#include "AC2GameUI.h"
#include "AC2GameMode.h"

FString SAC2GameUI::FriendModeLabel(int32 Row)
{
    static constexpr uint32 Labels[]={0x9d4e1790,0x30eebdf8,0x5a0ec326};
    return Native.Localized(Labels[FMath::Clamp(Row,0,2)]);
}

void SAC2GameUI::BuildFriends(const FIntRect& Box,const FIntRect& Clip,uint32 Parent)
{
    auto& Net=*Mode->Network;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    auto Label=[&](const FIntRect& Rect,const FString& Text){
        const int32 Index=Native.Nodes.Num();
        Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),Rect,Clip,Parent);
        if(Native.Nodes.IsValidIndex(Index))Native.Nodes[Index].Text=Text;
    };
    if(!Net.FriendsEnabled){
        Label(FIntRect(Box.Min+FIntPoint(8,35),Box.Max-FIntPoint(8,8)),
            TEXT("This connection does not support friend-list synchronization. You can message players with /tell Name, message."));
        return;
    }
    constexpr uint32 AddButton=0xf10bd109,RemoveButton=0x29791999;
    const auto Listed=[&](const FString& Name){return Net.Friends.ContainsByPredicate(
        [&](const auto& F){return F.Name.Equals(Name,ESearchCase::IgnoreCase);});};
    const FString Target=Net.FriendTarget();
    if(!Listed(SelectedFriend))SelectedFriend.Reset();
    Native.States.Add(AddButton,T(!Target.IsEmpty() && !Listed(Target)?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(RemoveButton,T(SelectedFriend.IsEmpty()?TEXT("ghosted"):TEXT("normal")));
    Native.States.Add(0x7ff25ebe,T(OpenOptionChoice==FriendModeMenu?TEXT("open"):TEXT("closed")));
    Native.Text.Add(0x6fbf610a,Net.FriendMode?FriendModeLabel(Net.FriendMode==4?2:Net.FriendMode==2?1:0):TEXT(""));
    Native.Template(0x22000001,TEXT("FriendsPage"),FIntRect(Box.Min+FIntPoint(0,20),Box.Max),Clip,Parent);
    BindNative(AddButton,TEXT("FriendAdd"));BindNative(RemoveButton,TEXT("FriendRemove"));
    if(const auto* Menu=Native.Find(0x7ff25ebe))FriendModeAnchor=Menu->Clip;
    const auto Area=Native.Bounds(TEXT("FriendsListBox"));
    if(Net.Friends.IsEmpty()){
        const FString Info=!Net.FriendError.IsEmpty()?Net.FriendError:Net.FriendsReceived?TEXT("Select a player to add a friend."):TEXT("Loading friends…");
        Label(FIntRect(Area.Min+FIntPoint(8,4),Area.Max),Info);
    }
    const uint32 List=T(TEXT("FriendsListBox"));
    int32 Online=0;
    for(const auto& Friend:Net.Friends)if(Friend.Mode)++Online;
    const int32 GroupCount=(Online>0)+(Online<Net.Friends.Num());
    JournalMaxScroll=FMath::Max(0,Net.Friends.Num()*20+GroupCount*18-Area.Height());
    JournalScroll=FMath::Clamp(JournalScroll,0,JournalMaxScroll);
    int32 Y=Area.Min.Y-JournalScroll;
    for(bool IsOnline:{true,false}){
        if((IsOnline && Online==0) || (!IsOnline && Online==Net.Friends.Num()))continue;
        const FIntRect Marker(Area.Min.X,Y,Area.Max.X,Y+18);
        Native.Template(0x22000001,IsOnline?TEXT("OnlineFriendsMarker"):TEXT("OfflineFriendsMarker"),Marker,Area,List);
        Y+=18;
        for(const auto& Friend:Net.Friends){
            if(bool(Friend.Mode)!=IsOnline)continue;
            FIntRect Rect(Area.Min.X,Y,Area.Max.X,Y+20);Y+=20;
            if(Rect.Max.Y<=Area.Min.Y || Rect.Min.Y>=Area.Max.Y)continue;
            const auto Status=T(Friend.Mode&2?TEXT("awayfromkeyboard"):Friend.Mode&4?TEXT("donotdisturb"):IsOnline?TEXT("online"):TEXT("offline"));
            Native.States.Add(T(TEXT("FriendTemplate")),Friend.Name==SelectedFriend?T(TEXT("selected")):Status);
            Native.States.Add(T(TEXT("FriendName")),Status);
            Native.Text.Add(T(TEXT("FriendName")),Friend.Name);
            Native.Template(0x22000001,TEXT("FriendTemplate"),Rect,Area,List);
            Rect.Clip(Area);
            const FName Key(*(TEXT("Friend:")+Friend.Name));
            Buttons.Add(Key,FSlateRect(Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y));
            ButtonWindows.Add(Key,T(TEXT("SocialField")));
            FString Hint=Friend.Name+TEXT(" — ")+(IsOnline?FriendModeLabel(Friend.Mode&2?1:Friend.Mode&4?2:0):TEXT("Offline"));
            if(!Friend.Location.IsEmpty())Hint+=TEXT(" — ")+Friend.Location;
            Hints.Add(Key,Hint);
        }
    }
    UpdateScrollbar(0xd47912b6,JournalScroll,JournalMaxScroll,JournalBar,JournalThumb,TEXT("SocialScrollUp"),TEXT("SocialScrollDown"));
}
