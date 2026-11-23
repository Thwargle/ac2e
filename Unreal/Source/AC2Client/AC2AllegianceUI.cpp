#include "AC2GameUI.h"
#include "AC2GameMode.h"

void SAC2GameUI::BuildAllegiance(const FIntRect& Box,const FIntRect& Clip,uint32 Parent)
{
    auto* Net=Mode->Network.Get();if(!Net)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    Native.States.Add(T(TEXT("AllegiancePage")),T(Net->Lieges.IsEmpty()?TEXT("noallegiance"):TEXT("allegiance")));
    Native.Text.Add(T(TEXT("AllegianceName")),Net->AllegianceName);
    Native.Text.Add(T(TEXT("AllegianceSize")),Net->AllegianceSize?FString::Printf(TEXT("Members: %d"),Net->AllegianceSize):FString());
    const auto* Monarch=Net->Lieges.FindByPredicate([&](const auto& M){return M.Id==Net->AllegianceMonarch;});
    Native.Text.Add(T(TEXT("MonarchName")),Monarch?Monarch->Name:FString());
    const FString BreakTarget=SelectedLiege.IsEmpty()?Net->AllegiancePatron:SelectedLiege;
    const bool CanBreak=BreakTarget==Net->AllegiancePatron&&!BreakTarget.IsEmpty()||Net->Lieges.ContainsByPredicate([&](const auto& M){return M.Id==BreakTarget&&M.Role==TEXT("Vassal");});
    Native.States.Add(T(TEXT("BreakButton")),T(CanBreak?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("SwearButton")),T(Net->AllegiancePatron.IsEmpty()&&!Net->TargetId.IsEmpty()&&Net->TargetId!=Net->PlayerId?TEXT("normal"):TEXT("ghosted")));
    Native.Template(0x22000001,TEXT("AllegiancePage"),FIntRect(Box.Min+FIntPoint(0,20),Box.Max),Clip,Parent);
    BindNative(T(TEXT("SwearButton")),TEXT("SwearButton"));BindNative(T(TEXT("BreakButton")),TEXT("BreakButton"));
    auto Area=Native.Bounds(TEXT("MemberListBox"));if(Area.Width()<=0)Area=FIntRect(Box.Min+FIntPoint(8,60),Box.Max-FIntPoint(24,50));
    auto Label=[&](const FIntRect& R,const FString& Text){const int32 Start=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),R,Area,Parent);if(Native.Nodes.IsValidIndex(Start))Native.Nodes[Start].Text=Text;};
    if(!Net->AllegianceApplicant.IsEmpty()&&FPlatformTime::Seconds()<Net->AllegianceInvitationUntil){
        Label(FIntRect(Area.Min,Area.Min+FIntPoint(Area.Width(),100)),Net->AllegianceInvitation);
        const int32 Width=FMath::Max(60,(Area.Width()-8)/2);
        for(int32 I=0;I<2;++I){
            const FIntRect Rect(Area.Min+FIntPoint(I*(Width+8),106),Area.Min+FIntPoint(I*(Width+8)+Width,132));
            const FName Key=I?TEXT("AllegianceDecline"):TEXT("AllegianceAccept");const int32 First=Native.Nodes.Num();
            Native.Template(0x22000001,0xad558692,Rect,Area,Parent);
            if(Native.Nodes.IsValidIndex(First)){Native.Nodes[First].Id=T(*Key.ToString());Buttons.Add(Key,FSlateRect(Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y));ButtonWindows.Add(Key,T(TEXT("SocialField")));}
            Label(Rect,I?TEXT("Decline"):TEXT("Accept"));
        }return;
    }
    if(Net->Lieges.IsEmpty()){
        Label(Area,Net->AllegianceReceived?TEXT("You are not in an allegiance.\n\nSelect another player and press Swear. That player must accept your request."):TEXT("Waiting for allegiance information."));return;
    }
    JournalMaxScroll=FMath::Max(0,Net->Lieges.Num()*55-Area.Height());JournalScroll=FMath::Clamp(JournalScroll,0,JournalMaxScroll);
    int32 Y=Area.Min.Y-JournalScroll;
    for(const auto& M:Net->Lieges){
        FIntRect R(Area.Min.X,Y,Area.Max.X,Y+51);Y+=55;if(R.Max.Y<=Area.Min.Y||R.Min.Y>=Area.Max.Y)continue;
        Native.Text.Add(T(TEXT("MemberNameText")),M.Name+(M.Id==SelectedLiege?TEXT(" *"):TEXT("")));
        Native.Text.Add(T(TEXT("MemberMiscText")),M.Role+FString::Printf(TEXT("  Level %d\n%s"),M.Level,M.Online?TEXT("Online"):TEXT("Offline"))+(M.Title.IsEmpty()?FString():TEXT("  ")+M.Title));
        Native.Template(0x22000001,TEXT("VassalElement"),R,Area,T(TEXT("MemberListBox")));R.Clip(Area);
        const FName Key(*(TEXT("Liege:")+M.Id));Buttons.Add(Key,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));Hints.Add(Key,M.Name+TEXT("\n")+M.Role);ButtonWindows.Add(Key,T(TEXT("SocialField")));
    }
    UpdateScrollbar(0xb9d70117,JournalScroll,JournalMaxScroll,JournalBar,JournalThumb,TEXT("SocialScrollUp"),TEXT("SocialScrollDown"));
}
