#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2CharacterUI.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Framework/Application/SlateApplication.h"

FString SAC2GameUI::FellowshipLootLabel(int32 Method) const
{
    static const TCHAR* Labels[]={TEXT("Not shared"),TEXT("Shared"),TEXT("Rotate"),TEXT("Rotate group"),TEXT("Random")};
    return Labels[FMath::Clamp(Method,0,4)];
}

void SAC2GameUI::BindNative(uint32 Id,FName Action)
{
    if(const auto* N=Native.Find(Id)){
        if(N->State==FAC2UILayout::Token(TEXT("ghosted")))return;
        const FName Key(*FString::Printf(TEXT("Native%08X"),Id));
        Buttons.Add(Key,FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));ButtonActions.Add(Key,Action);ButtonWindows.Add(Key,N->Window);
    }
}
void SAC2GameUI::UpdateScrollbar(uint32 Id,int32 Offset,int32 Maximum,FIntRect& Bar,FIntRect& Thumb,FName Up,FName Down)
{
    Bar=Thumb=FIntRect();
    for(int32 I=0;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==Id){
        Bar=Native.Nodes[I].Rect;
        BindNative(Native.Nodes[I].Attr(TEXT("up_button")),Up);BindNative(Native.Nodes[I].Attr(TEXT("down_button")),Down);
        for(int32 J=I+1;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Parent==I && Native.Nodes[J].Id==FAC2UILayout::Token(TEXT("widget"))){
            const FIntPoint Delta(0,Bar.Min.Y+FMath::RoundToInt(FMath::Max(0,Bar.Height()-Native.Nodes[J].Rect.Height())*(Maximum?float(Offset)/Maximum:0))-Native.Nodes[J].Rect.Min.Y);
            for(int32 K=J;K<Native.Nodes.Num();++K){int32 P=K;while(P!=J && Native.Nodes.IsValidIndex(P))P=Native.Nodes[P].Parent;
                if(P==J){Native.Nodes[K].Rect+=Delta;Native.Nodes[K].Clip=Native.Nodes[K].Rect;Native.Nodes[K].Clip.Clip(Bar);}}
            Thumb=Native.Nodes[J].Rect;break;
        }break;
    }
}
void SAC2GameUI::DragExamine(float Y)
{
    const int32 Travel=ExamineBar.Height()-ExamineThumb.Height();
    if(Travel>0)ExamineScroll=FMath::RoundToInt(FMath::Clamp((Y-ExamineGrab-ExamineBar.Min.Y)/Travel,0.f,1.f)*ExamineMaxScroll);
    Rebuild();
}
void SAC2GameUI::PrepareSocial()
{
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    Native.Visibility.Add(T(TEXT("SocialField")),ShowSocial);
    Native.States.Add(0xb2c8925e,T(SocialTab==0?TEXT("open"):TEXT("closed")));
    Native.States.Add(T(TEXT("AllegianceTab")),T(SocialTab==1?TEXT("open"):TEXT("closed")));Native.States.Add(0xdcf29a9e,T(SocialTab==2?TEXT("open"):TEXT("closed")));
    const auto* Net=Mode->Network.Get();const bool Joined=Net&&!Net->Fellows.IsEmpty(),Leader=Joined&&Net->FellowshipLeader==Net->PlayerId;
    Native.States.Add(T(TEXT("FellowshipPage")),T(Joined?TEXT("fellowship"):TEXT("nofellowship")));
    Native.States.Add(0x6257ec14,T(Joined?TEXT("fellowship"):TEXT("nofellowship")));
    for(const TCHAR* Key:{TEXT("FellowshipDisbandButton"),TEXT("FellowshipDismissButton"),TEXT("FellowshipRecruitButton")})
        Native.States.Add(T(Key),T(Leader?TEXT("normal"):TEXT("ghosted")));
    const bool SharedXp=Joined?Net->FellowshipSharedXp:FellowshipShareXpChoice;
    Native.States.Add(T(TEXT("NoFellowshipXPToggleButton")),T(TEXT("normal")));
    Native.Text.Add(0x22c6774f,FellowshipLootLabel(Joined?Net->FellowshipLooting:FellowshipLootChoice));
    Native.Text.Add(0x9df39c4d,SharedXp?TEXT("Shared combat XP."):TEXT("Individual experience."));
    Native.Text.Add(T(TEXT("FellowshipNameText")),Joined?Net->FellowshipName:FString());
    Native.Text.Add(T(TEXT("NoFellowshipNameEntryText")),FString());
    for(const TCHAR* Key:{TEXT("FellowTemplatePKDamageText"),TEXT("FellowTemplatePKDamageMeter"),TEXT("FellowTemplatePKVigorlossText"),TEXT("FellowTemplatePKVigorlossMeter"),TEXT("FellowTemplatePercentText")})Native.Visibility.Add(T(Key),false);
}
void SAC2GameUI::BuildSocial()
{
    FellowshipLootAnchor=FriendModeAnchor=FIntRect();
    if(!ShowSocial)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    auto* Net=Mode->Network.Get();if(!Net)return;
    BindNative(0x69dd00fe,TEXT("Social"));BindNative(0x696225d8,TEXT("SocialHelp"));
    BindNative(0xb2c8925e,TEXT("SocialFellowship"));BindNative(T(TEXT("AllegianceTab")),TEXT("SocialAllegiance"));BindNative(0xdcf29a9e,TEXT("SocialFriends"));
    const auto* Panel=Native.Find(0x0bf77f41);if(!Panel)return;
    const auto Box=Panel->Rect,Clip=Panel->Clip;const uint32 Parent=Panel->Id;
    if(SocialTab==1){BuildAllegiance(Box,Clip,Parent);return;}
    if(SocialTab==2){BuildFriends(Box,Clip,Parent);return;}
    Native.Template(0x22000001,TEXT("FellowshipPage"),FIntRect(Box.Min+FIntPoint(0,20),Box.Max),Clip,Parent);
    for(const TCHAR* B:{TEXT("FellowshipCreateButton"),TEXT("FellowshipLeaveButton"),TEXT("FellowshipDisbandButton"),TEXT("FellowshipRecruitButton"),TEXT("FellowshipDismissButton")})Bind(B,FName(B));
    Bind(TEXT("NoFellowshipNameEntryText"),TEXT("FellowshipName"));
    Bind(TEXT("FellowshipXPButton"),TEXT("SocialHelp"));Bind(TEXT("FellowshipNumberButton"),TEXT("SocialHelp"));
    if(Net->Fellows.IsEmpty()){
        Bind(TEXT("NoFellowshipXPToggleButton"),TEXT("FellowshipXpChoice"));
        Hints.Add(TEXT("NoFellowshipXPToggleButton"),TEXT("Toggle XP sharing for this fellowship."));
    }
    if(Net->Fellows.IsEmpty())if(const auto* Loot=Native.Find(0x03a4a415)){
        FellowshipLootAnchor=Loot->Clip;BindNative(0x03a4a415,TEXT("FellowshipLootChoice"));
    }
    // Single-line native text fields clip instead of wrapping. Keep the full
    // name in the tooltip, and fit the displayed name to its authored bounds.
    auto Fit=[&](FAC2NativeNode& N){
        if(auto Font=Fonts.Get(N.Attr(TEXT("font"),0x4200000c))){
            const int32 Width=N.Rect.Width()-N.Attr(TEXT("left_margin"))-N.Attr(TEXT("right_margin"));
            if(Font->Width(N.Text)>Width){while(!N.Text.IsEmpty() && Font->Width(N.Text+TEXT("..."))>Width)N.Text.LeftChopInline(1);N.Text+=TEXT("...");}
        }
    };
    for(auto& N:Native.Nodes)if(N.Id==T(TEXT("FellowshipNameText")))Fit(N);
    Bind(TEXT("FellowshipNameText"),TEXT("SocialHelp"));
    if(!Net->Fellows.IsEmpty()){
        const FString XpMode=Net->FellowshipSharedXp?TEXT("Shared combat XP"):TEXT("Individual experience");
        Hints.Add(TEXT("FellowshipXPButton"),XpMode);
        Hints.Add(TEXT("FellowshipNameText"),Net->FellowshipName+TEXT("\n")+XpMode);
    }
    if(!Net->Fellows.IsEmpty()){
        const auto Area=Native.Bounds(TEXT("FellowshipListBox"));
        JournalMaxScroll=FMath::Max(0,Net->Fellows.Num()*53-Area.Height());JournalScroll=FMath::Clamp(JournalScroll,0,JournalMaxScroll);
        int32 Y=Area.Min.Y-JournalScroll;
        for(const auto& M:Net->Fellows){
            const FIntRect Rect(Area.Min.X,Y,Area.Max.X,Y+53);Y+=53;
            if(Rect.Max.Y<=Area.Min.Y||Rect.Min.Y>=Area.Max.Y)continue;
            Native.Text.Add(T(TEXT("FellowTemplateNameText")),M.Name);Native.Text.Add(T(TEXT("FellowTemplateLevelText")),FString::Printf(TEXT("Level %d"),M.Level));
            Native.States.Add(T(TEXT("FellowTemplateNameText")),T(M.Id==Net->FellowshipLeader?TEXT("leader"):TEXT("notleader")));
            Native.Text.Add(T(TEXT("FellowTemplateHealthText")),FString::Printf(TEXT("%d / %d"),M.Health,M.MaxHealth));Native.Text.Add(T(TEXT("FellowTemplateVigorText")),FString::Printf(TEXT("%d / %d"),M.Vigor,M.MaxVigor));
            Native.Meters.Add(T(TEXT("FellowTemplateHealthMeter")),M.MaxHealth?float(M.Health)/M.MaxHealth:0);Native.Meters.Add(T(TEXT("FellowTemplateVigorMeter")),M.MaxVigor?float(M.Vigor)/M.MaxVigor:0);
            const int32 First=Native.Nodes.Num();
            Native.Template(0x22000001,TEXT("FellowTemplate"),Rect,Area,T(TEXT("FellowshipListBox")));
            int32 NameEnd=Rect.Max.X;
            for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==T(TEXT("FellowTemplateLevelText")))NameEnd=Native.Nodes[I].Rect.Min.X-4;
            for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==T(TEXT("FellowTemplateNameText"))){auto& N=Native.Nodes[I];N.Rect.Max.X=FMath::Min(N.Rect.Max.X,NameEnd);N.Clip=N.Rect;N.Clip.Clip(Area);Fit(N);}
            const FName Key(*(TEXT("Fellow:")+M.Id));auto Hit=Rect;Hit.Clip(Area);
            Buttons.Add(Key,FSlateRect(Hit.Min.X,Hit.Min.Y,Hit.Max.X,Hit.Max.Y));Hints.Add(Key,M.Name+(M.Id==SelectedFellow?TEXT(" (selected)"):FString())+TEXT("\nClick to target this member for helpful skills."));
        }
        UpdateScrollbar(0xee89d21a,JournalScroll,JournalMaxScroll,JournalBar,JournalThumb,TEXT("SocialScrollUp"),TEXT("SocialScrollDown"));
    }
    // The native invitation is time limited; acceptance always goes back to the server.
    if(!Net->FellowshipInviter.IsEmpty() && FPlatformTime::Seconds()<Net->FellowshipInvitationUntil){
        // Replace the creation form while an invitation is pending. Binding by
        // template name here would hit the original Create button instead.
        Buttons.Remove(TEXT("FellowshipCreateButton"));
        const int32 FirstForm=Native.Nodes.IndexOfByPredicate([&](const auto& N){return N.Id==T(TEXT("FellowshipPage"));});
        if(FirstForm!=INDEX_NONE)Native.Nodes.SetNum(FirstForm);
        const FIntRect R(Box.Min.X+8,Box.Min.Y+55,Box.Max.X-8,Box.Min.Y+180);
        const int32 Start=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),R,Clip);
        if(Native.Nodes.IsValidIndex(Start))Native.Nodes[Start].Text=Net->FellowshipInvitation;
        for(int32 I=0;I<2;++I){
            const FIntRect Rect(Box.Min.X+10+I*118,Box.Min.Y+200,Box.Min.X+120+I*118,Box.Min.Y+225);
            const FName Key=I?TEXT("FellowshipDecline"):TEXT("FellowshipAccept");
            Native.States.Add(0xad558692,T(Rect.Contains(FIntPoint(Mouse.X,Mouse.Y))?TEXT("normal_rollover"):TEXT("normal")));
            const int32 First=Native.Nodes.Num();Native.Template(0x22000001,0xad558692,Rect,Clip,Parent);
            if(Native.Nodes.IsValidIndex(First)){
                Native.Nodes[First].Id=T(*Key.ToString());const auto Hit=Native.Nodes[First].Clip;
                Buttons.Add(Key,FSlateRect(Hit.Min.X,Hit.Min.Y,Hit.Max.X,Hit.Max.Y));
                const int32 Label=Native.Nodes.Num();Native.Template(0x22000001,0x27774131,Rect,Clip,T(*Key.ToString()));
                if(Native.Nodes.IsValidIndex(Label))Native.Nodes[Label].Text=I?TEXT("Decline"):TEXT("Accept");
            }
        }
        Native.States.Remove(0xad558692);
    }
}
bool SAC2GameUI::SocialAction(FName Name)
{
    auto* Net=Mode->Network.Get();if(!Net)return false;
    if(Name==TEXT("Social")){
        ShowSocial=!ShowSocial;RaiseWindow(FAC2UILayout::Token(TEXT("SocialField")));SelectJournalWindow(FAC2UILayout::Token(TEXT("SocialField")));JournalScroll=0;SelectedFellow.Reset();EndChat();
        if(Portrait.IsValid()){Portrait->SetActorTickEnabled(ShowInventory);Portrait->Capture->bCaptureEveryFrame=ShowInventory;}
        if(ShowSocial && SocialTab==2)Net->FriendAction(TEXT("list"));
        if(ShowSocial && FellowshipEntry && FellowshipEntry->Value().IsEmpty())if(const auto* P=Mode->Characters.Active())FellowshipEntry->SetText(P->Name.Left(18)+TEXT("'s fellowship"));
        Rebuild();return true;}
    if(Name==TEXT("SocialFellowship")||Name==TEXT("SocialAllegiance")||Name==TEXT("SocialFriends")){
        SelectJournalWindow(FAC2UILayout::Token(TEXT("SocialField")));JournalScroll=0;SocialTab=Name==TEXT("SocialFellowship")?0:Name==TEXT("SocialAllegiance")?1:2;
        if(SocialTab==1)Net->Allegiance(TEXT("query"));
        if(SocialTab==2)Net->FriendAction(TEXT("list"));Rebuild();return true;}
    if(Name==TEXT("SocialHelp") && SocialTab==2){
        Mode->Notice=Net->FriendsEnabled?TEXT("Select a player and press Add, or use /friend add Name. Select a friend in the list and press Remove to remove them."):
            TEXT("You can message players with /tell Name, message.");return true;}
    if(Name==TEXT("SocialHelp")){Mode->Notice=TEXT("Fellowships support up to eight players. Choose loot and XP sharing before creating a fellowship. With shared XP, nearby living members split base kill experience. Select a player and Recruit, or select a member to Dismiss. Use /f for fellowship chat.");return true;}
    if(Name==TEXT("FellowshipName")){if(FellowshipEntry)FSlateApplication::Get().SetKeyboardFocus(FellowshipEntry);return true;}
    const FString Key=Name.ToString();
    if(Key.StartsWith(TEXT("Friend:"))){
        SelectedFriend=Key.Mid(7);Rebuild();return true;}
    if(Name==TEXT("FriendAdd")){
        const FString NameToAdd=Net->FriendTarget();
        if(!NameToAdd.IsEmpty())Net->FriendAction(TEXT("add"),NameToAdd);return true;}
    if(Name==TEXT("FriendRemove")){
        if(!SelectedFriend.IsEmpty())Net->FriendAction(TEXT("remove"),SelectedFriend);return true;}
    if(Key.StartsWith(TEXT("Liege:"))){SelectedLiege=Key.Mid(6);return true;}
    if(Name==TEXT("SwearButton")){Net->Allegiance(TEXT("swear"),Net->TargetId);return true;}
    if(Name==TEXT("BreakButton")){Net->Allegiance(TEXT("break"),SelectedLiege.IsEmpty()?Net->AllegiancePatron:SelectedLiege);return true;}
    if(Name==TEXT("AllegianceAccept")||Name==TEXT("AllegianceDecline")){
        Net->Allegiance(Name==TEXT("AllegianceAccept")?TEXT("accept"):TEXT("decline"),Net->AllegianceApplicant);Net->AllegianceApplicant.Reset();return true;}
    if(Key.StartsWith(TEXT("Fellow:"))){SelectedFellow=Key.Mid(7);Net->SelectFellow(SelectedFellow);return true;}
    if(Name==TEXT("FellowshipXpChoice")){
        if(Net->Fellows.IsEmpty()){
            FellowshipShareXpChoice=!FellowshipShareXpChoice;
            Rebuild();
        }
        return true;
    }
    if(Name==TEXT("FellowshipCreateButton")){
        if(FellowshipEntry)Net->Fellowship(TEXT("create"),FellowshipEntry->Value().TrimStartAndEnd(),
            FellowshipLootChoice,FellowshipShareXpChoice);
        return true;
    }
    if(Name==TEXT("FellowshipRecruitButton")){Net->Fellowship(TEXT("invite"),Net->TargetId);return true;}
    if(Name==TEXT("FellowshipDismissButton")){Net->Fellowship(TEXT("dismiss"),SelectedFellow);return true;}
    if(Name==TEXT("FellowshipLeaveButton")){Net->Fellowship(TEXT("leave"));return true;}
    if(Name==TEXT("FellowshipDisbandButton")){Net->Fellowship(TEXT("disband"));return true;}
    if(Name==TEXT("FellowshipAccept")||Name==TEXT("FellowshipDecline")){
        Net->Fellowship(Name==TEXT("FellowshipAccept")?TEXT("accept"):TEXT("decline"),Net->FellowshipInviter);Net->FellowshipInviter.Reset();return true;}
    return false;
}
