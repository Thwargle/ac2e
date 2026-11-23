#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"

bool SAC2GameUI::AllowsPanelButton(FName Name,FVector2D Point) const
{
    const uint32 Window=WindowAt(Point);
    return !Window || ButtonWindows.FindRef(Name)==Window;

}

void SAC2GameUI::BuildJournal()
{
    JournalBar=JournalThumb=FIntRect();JournalMaxScroll=0;
    if(!ShowQuests)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    auto Button=[&](uint32 Id,FName Action){if(const auto* N=Native.Find(Id)){
        const FName Key(*FString::Printf(TEXT("Native%08X"),Id));
        Buttons.Add(Key,FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));ButtonActions.Add(Key,Action);}};
    Button(0x92d81e44,TEXT("Quests"));
    Button(0xcd4a8d44,TEXT("QuestHelp"));
    Button(0x715bdff7,TEXT("QuestActive"));Button(0x70859237,TEXT("QuestDone"));Button(0x92e39d2f,TEXT("QuestStory"));
    const auto* Panel=Native.Find(0x87e7c1f5);if(!Panel)return;
    const FIntRect Box=Panel->Rect,Clip=Panel->Clip;const uint32 Parent=Panel->Id;
    const uint32 Pages[]={0x71758fff,0x70abc23f,0x93e398e5};
    // Native page rectangle is 250 x 315, at (0,21); leave the authored bottom inset.
    Native.Template(0x22000001,Pages[QuestTab],FIntRect(Box.Min+FIntPoint(0,21),Box.Max-FIntPoint(0,18)),Clip,Parent);
    const uint32 Lists[]={0x6a170865,0x6bc945a5,0x5f75e612};
    const uint32 Bars[]={0xb4ba025a,0xb5644f9a,0xed9a62f5};
    const uint32 Up[]={0x44ba0270,0x45644fb0,0x69fd2076},Down[]={0xdb55ba78,0xda8bf7b8,0x260afc72};
    const auto* List=Native.Find(Lists[QuestTab]);if(!List)return;const FIntRect Area=List->Clip;
    auto* Net=Mode->Network.Get();const double Now=FPlatformTime::Seconds();
    struct Row {FAC2Network::FQuest* Quest;FString Extra;int32 Height;};TArray<Row> Rows;
    // Resolve the actual QuestExtraText font/margins even when no rows are visible.
    const int32 Probe=Native.Nodes.Num();
    Native.Template(0x22000001,TEXT("QuestExtraText"),Area,Area,Lists[QuestTab]);
    FAC2NativeNode Style;if(Native.Nodes.IsValidIndex(Probe))Style=Native.Nodes[Probe];Native.Nodes.SetNum(Probe);
    const auto Font=Fonts.Get(Style.Attr(TEXT("font"),0x4200000c));
    const auto* RowDesc=Native.Layouts[0x22000001].Find(TEXT("QuestTemplate"));
    const int32 RowHeight=RowDesc?RowDesc->Rect.Height()+1:40;
    const int32 RowWidth=Area.Width();
    const int32 TextWidth=FMath::Max(1,RowWidth-int32(Style.Attr(TEXT("left_margin"))+Style.Attr(TEXT("right_margin"))));
    int32 Total=0;
    if(Net && QuestTab!=2)for(auto& Q:Net->Quests){
        const uint32 Status=Q.DisplayStatus(Now);
        const bool Done=Status==0x20000000 || Status==0x30000000;
        if((QuestTab==0 && Done)||(QuestTab==1 && !Done))continue;
        Row R{&Q,FString(),0};
        if(ExpandedQuests.Contains(Q.Id)){
            if(Done)R.Extra=Native.Localized(Status==0x30000000?TEXT("ID_Quest_Completed"):TEXT("ID_Quest_Failed"));
            else {
                if(Q.FormattedDetail.IsEmpty())Q.FormattedDetail=Native.Localized(TEXT("ID_Quest_Description"),{
                    {T(TEXT("DESC")),Q.Description},{T(TEXT("JOURNAL")),Q.Journal==Q.Description?FString():Q.Journal},
                    {T(TEXT("LEVEL")),Q.Level==-999?Native.Localized(TEXT("ID_Quest_ChallengeLevelAny")):FString::FromInt(Q.Level)}});
                R.Extra=Q.FormattedDetail;
            }
            R.Height=Font?Font->Lines(R.Extra,TextWidth).Num()*int32(Font->Height)+int32(Style.Attr(TEXT("top_margin"))+Style.Attr(TEXT("bottom_margin"))):0;
        }
        Total+=RowHeight+R.Height;Rows.Add(MoveTemp(R));
    }
    JournalMaxScroll=FMath::Max(0,Total-Area.Height());JournalScroll=FMath::Clamp(JournalScroll,0,JournalMaxScroll);
    int32 Y=Area.Min.Y-JournalScroll;
    for(auto& R:Rows){auto& Q=*R.Quest;const bool Open=ExpandedQuests.Contains(Q.Id);
        const FIntRect Rect(Area.Min.X,Y,Area.Min.X+RowWidth,Y+RowHeight);
        if(Rect.Max.Y>Area.Min.Y && Rect.Min.Y<Area.Max.Y){
            const FName Key(*FString::Printf(TEXT("QuestRow%u"),Q.Id));
            const bool Hover=Rect.Contains(FIntPoint(Mouse.X,Mouse.Y));
            Native.States.Add(T(TEXT("QuestTemplate")),T(Open?(Hover?TEXT("highlight_rollover"):TEXT("highlight")):(Hover?TEXT("normal_rollover"):TEXT("normal"))));
            for(const TCHAR* Name:{TEXT("QuestName"),TEXT("QuestTime")}){
                if(Q.Changed)Native.States.Add(T(Name),T(TEXT("changed")));else Native.States.Remove(T(Name));}
            Native.Text.Add(T(TEXT("QuestName")),Q.Name);
            const int64 Seconds=FMath::CeilToDouble(Q.Seconds(Now));
            FString Time=Native.Localized(Q.MaxedOut?TEXT("ID_Quest_QuestMaxed"):TEXT("ID_Quest_TimeNone"));
            if(!Q.MaxedOut && Seconds>0)Time=FString::Printf(TEXT("%lld:%02lld:%02lld"),Seconds/3600,(Seconds/60)%60,Seconds%60);
            Native.Text.Add(T(TEXT("QuestTime")),Time);
            const uint32 Hovered=Native.Hovered,Pressed=Native.Pressed;Native.Hovered=Native.Pressed=0;
            const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("QuestTemplate"),Rect,Area,Lists[QuestTab]);
            Native.Hovered=Hovered;Native.Pressed=Pressed;
            if(Native.Nodes.IsValidIndex(First)){auto Hit=Rect;Hit.Clip(Area);
                Buttons.Add(Key,FSlateRect(Hit.Min.X,Hit.Min.Y,Hit.Max.X,Hit.Max.Y));Hints.Add(Key,Q.Name);}
        }
        Y+=RowHeight;
        if(R.Height>0 && Y+R.Height>Area.Min.Y && Y<Area.Max.Y){
            const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("QuestExtraText"),FIntRect(Area.Min.X,Y,Area.Min.X+RowWidth,Y+R.Height),Area,Lists[QuestTab]);
            if(Native.Nodes.IsValidIndex(First))Native.Nodes[First].Text=R.Extra;
        }
        Y+=R.Height;
    }
    // Every row is a separate instance of the same DAT template; don't leak its
    // selection or text into other panels or the next rebuild.
    for(const TCHAR* Name:{TEXT("QuestName"),TEXT("QuestTime"),TEXT("QuestTemplate")}){Native.States.Remove(T(Name));Native.Text.Remove(T(Name));}
    Button(Up[QuestTab],TEXT("QuestScrollUp"));Button(Down[QuestTab],TEXT("QuestScrollDown"));
    for(int32 I=0;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==Bars[QuestTab]){
        JournalBar=Native.Nodes[I].Rect;
        for(int32 J=I+1;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Parent==I && Native.Nodes[J].Id==T(TEXT("widget"))){
            const FIntPoint Offset(0,JournalBar.Min.Y+FMath::RoundToInt((JournalBar.Height()-Native.Nodes[J].Rect.Height())*(JournalMaxScroll?float(JournalScroll)/JournalMaxScroll:0))-Native.Nodes[J].Rect.Min.Y);
            for(int32 K=J;K<Native.Nodes.Num();++K){int32 P=K;while(P!=J && Native.Nodes.IsValidIndex(P))P=Native.Nodes[P].Parent;
                if(P==J){Native.Nodes[K].Rect+=Offset;Native.Nodes[K].Clip=Native.Nodes[K].Rect;Native.Nodes[K].Clip.Clip(JournalBar);}}
            JournalThumb=Native.Nodes[J].Rect;break;
        }break;
    }
}
void SAC2GameUI::DragJournal(float Y)
{
    const int32 Travel=JournalBar.Height()-JournalThumb.Height();
    if(Travel>0)JournalScroll=FMath::RoundToInt(FMath::Clamp((Y-JournalGrab-JournalBar.Min.Y)/Travel,0.f,1.f)*JournalMaxScroll);
    if(JournalWindow==FAC2UILayout::Token(TEXT("SkillField")) && SkillTab==4)CraftPage=JournalScroll;
    Rebuild();
}
bool SAC2GameUI::JournalAction(FName Name)
{
    if(Name==TEXT("SkillsHelp")){Mode->Notice=TEXT("Select a skill to inspect its requirements. Train spends skill credits; Raise spends available XP. Untrain recovers a skill gradually as you earn XP; Cancel stops untraining. Double-click a trained active skill to add it to the current hotbar page, or drag it to a slot. Use its hotbar shortcut to cast. Hold Shift to move or remove locked shortcuts. Craft opens professions and recipes. Greyed controls need their requirements met or additional server rules.");return true;}
    if(Name==TEXT("QuestHelp")){Mode->Notice=TEXT("Speak to quest NPCs to receive tasks. Underway lists active quests; Finished lists completed or failed tasks. Click a quest to expand its instructions and timer. Use the scrollbar or mouse wheel for longer journals. Story entries are not implemented yet.");return true;}
    const FString Key=Name.ToString();
    if(Name==TEXT("QuestScrollUp") || Name==TEXT("QuestScrollDown") || Name==TEXT("SkillScrollUp") || Name==TEXT("SkillScrollDown") || Name==TEXT("SocialScrollUp") || Name==TEXT("SocialScrollDown")){
        const bool Up=Key.EndsWith(TEXT("Up"));
        SelectJournalWindow(FAC2UILayout::Token(Key.StartsWith(TEXT("Skill"))?TEXT("SkillField"):Key.StartsWith(TEXT("Social"))?TEXT("SocialField"):TEXT("QuestField")));
        if(JournalWindow==FAC2UILayout::Token(TEXT("SkillField")) && SkillTab==4)CraftPage=FMath::Clamp(CraftPage+(Up?-1:1),0,JournalMaxScroll);
        else JournalScroll=FMath::Clamp(JournalScroll+(Up?-40:40),0,JournalMaxScroll);return true;}
    if(Key.StartsWith(TEXT("QuestRow")) && Mode->Network){
        const uint32 Id=FCString::Strtoui64(*Key.Mid(8),nullptr,10);
        if(auto* Q=Mode->Network->Quests.FindByPredicate([&](const auto& V){return V.Id==Id;})){
            if(ExpandedQuests.Contains(Id))ExpandedQuests.Remove(Id);else ExpandedQuests.Add(Id);
            Q->Changed=false;SelectedQuest=Id;
        }return true;
    }
    if(Name==TEXT("TrainButton") || Name==TEXT("RaiseButton") || Name==TEXT("UntrainButton") || Name==TEXT("CancelUntrainButton")){
        if(const auto* Button=Native.Find(*Key);!Button || Button->State==FAC2UILayout::Token(TEXT("ghosted")))return true;
        if(Mode->Network && SelectedSkill)Mode->Network->ChangeSkill(SelectedSkill,Name==TEXT("TrainButton")?TEXT("train"):Name==TEXT("RaiseButton")?TEXT("raise"):Mode->Network->UntrainingSkill==SelectedSkill?TEXT("cancel_untrain"):TEXT("untrain"));return true;
    }
    if(Key.StartsWith(TEXT("SkillPanel"))){const uint32 Id=FCString::Strtoui64(*Key.Mid(10),nullptr,10);
        if(SkillData.Panel(Id).Valid){SelectJournalWindow(FAC2UILayout::Token(TEXT("SkillField")));SkillPanel=Id;SelectedSkill=0;JournalScroll=-1;}return true;}
    if(Key.StartsWith(TEXT("LearnedSkill"))){SelectedSkill=FCString::Atoi(*Key.Mid(12));ExaminedRecipe=0;ExaminedSkill=SelectedSkill;ShowStats=true;RaiseWindow(FAC2UILayout::Token(TEXT("ExaminationField")));ExamineScroll=0;InspectedObject.Reset();return true;}
    if(Name==TEXT("MeleeButton")||Name==TEXT("MissileButton")||Name==TEXT("MagicButton")||Name==TEXT("NoncombatButton")||Name==TEXT("CraftTab")){
        SelectJournalWindow(FAC2UILayout::Token(TEXT("SkillField")));
        if(Name==TEXT("CraftTab")){CraftProfessions=true;CraftPage=0;}
        SkillTab=Name==TEXT("MeleeButton")?0:Name==TEXT("MissileButton")?1:Name==TEXT("MagicButton")?2:Name==TEXT("NoncombatButton")?3:4;SkillPanel=0;SelectedSkill=0;JournalScroll=-1;return true;}
    if(Name==TEXT("QuestActive")||Name==TEXT("QuestDone")||Name==TEXT("QuestStory")){
        SelectJournalWindow(FAC2UILayout::Token(TEXT("QuestField")));
        QuestTab=Name==TEXT("QuestActive")?0:Name==TEXT("QuestDone")?1:2;JournalScroll=0;return true;}
    if(!Mode->Network)return false;
    if(Name==TEXT("LootAll")){Mode->Network->LootAll();return true;}
    if(Name==TEXT("LootTransmute")){Mode->Network->TransmuteLoot();return true;}
    if(Name==TEXT("LootClose")){Mode->Network->CloseLoot();return true;}
    return Key.StartsWith(TEXT("LootItem"));
}
void SAC2GameUI::BuildLoot()
{
    auto* Net=Mode->Network.Get();if(!Net || Net->LootContainer.IsEmpty())return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    Native.Visibility.Add(T(TEXT("ExternalContainerField")),true);
    Native.Visibility.Add(T(TEXT("LootAllButton")),true);
    Native.Visibility.Add(T(TEXT("TransmuteAllButton")),true);
    Native.Text.Add(T(TEXT("ExternalContainerLabel")),Net->ObjectName(Net->LootContainer));
    const FString Title=Net->ObjectName(Net->LootContainer);
    const int32 Width=FMath::Clamp(FMath::CeilToInt(BodyFont->Width(Title))+66,244,FMath::Min(480,int32(Size.X)));
    const FIntPoint Origin(FMath::Max(0,int32(Size.X/2)-Width/2),FMath::Max(0,int32(Size.Y/2)-82));
    Native.Template(0x22000005,TEXT("ExternalContainerField"),FIntRect(Origin,Origin+FIntPoint(Width,165)),FIntRect(0,0,Size.X,Size.Y));
    // Template applies the saved drag offset and viewport constraints. Lay out
    // the heading against that final window, not its original centered origin.
    const FIntRect Window=Native.Bounds(TEXT("ExternalContainerField"));
    for(auto& Node:Native.Nodes){
        // The DAT supplies one complete 139-pixel frame, not a repeating tile.
        // Keep its curved end caps when accommodating a longer corpse name.
        if(Node.Id==T(TEXT("ExternalContainerField")))Node.ImageBorderX=34;
        if(Node.Id==T(TEXT("ExternalContainerLabel"))){Node.Rect.Max.X=Window.Max.X-36;Node.Clip=Node.Rect;Node.Clip.Clip(Window);}
        if(Node.Id==T(TEXT("ExternalContainerCloseButton"))){
            // Reference rectangles were stretched with the wider root. Preserve
            // the original close glyph size, then place it inside the end cap.
            Node.Rect=FIntRect(Window.Max.X-34,Window.Min.Y+5,Window.Max.X-17,Window.Min.Y+22);Node.Clip=Node.Rect;
        }
        if(Node.Id==T(TEXT("TransmuteAllButton"))){Node.Rect=FIntRect(Window.Max.X-54,Window.Max.Y-24,Window.Max.X-8,Window.Max.Y-2);Node.Clip=Node.Rect;}
        if(Node.Id==T(TEXT("LootAllButton"))){Node.Rect=FIntRect(Window.Min.X+6,Window.Max.Y-24,Window.Min.X+54,Window.Max.Y-2);Node.Clip=Node.Rect;}
    }
    Bind(TEXT("ExternalContainerCloseButton"),TEXT("LootClose"));Bind(TEXT("LootAllButton"),TEXT("LootAll"));
    Bind(TEXT("TransmuteAllButton"),TEXT("LootTransmute"));
    Hints.Add(TEXT("TransmuteAllButton"),TEXT("Convert supported remaining loot to gold. Quest items remain in the corpse."));
    const auto* Container=Native.Find(TEXT("ExternalContainer"));
    const auto* List=Container?Native.Descendant(*Container,TEXT("ContainerListBox")):nullptr;if(!List)return;
    const FIntRect Area=List->Clip;const int32 Columns=FMath::Max(1,Area.Width()/36);
    LootRow=FMath::Clamp(LootRow,0,FMath::Max(0,FMath::DivideAndRoundUp(Net->LootItems.Num(),Columns)-Area.Height()/36));
    for(int32 I=0;I<Net->LootItems.Num();++I){
        const FIntPoint XY=Area.Min+FIntPoint((I%Columns)*36,(I/Columns-LootRow)*36);
        if(XY.Y<Area.Min.Y)continue;
        if(XY.Y+36>Area.Max.Y)break;
        const FIntRect R(XY,XY+FIntPoint(36,36));Native.Template(0x22000001,TEXT("ItemBackground"),R,Area);
        const FString& Item=Net->LootItems[I];
        const FIntRect IconRect(XY+FIntPoint(2,2),XY+FIntPoint(34,34));
        // Equipment artwork lives in the native visual's ordered icon layers.
        // The entity descriptor can contain only the generic unknown-item icon.
        const auto Layers=Net->ItemIconLayers(Item,*Assets);
        if(Layers.IsEmpty())Icons.Add({IconRect,Area,Net->ItemIcon(Item)});
        else for(const auto& Layer:Layers)
            Icons.Add({IconRect,Area,Layer.Image,1,FLinearColor(FColor(Layer.Color))});
        const FName Key(*FString::Printf(TEXT("LootItem%d"),I));Buttons.Add(Key,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));
        Hints.Add(Key,Net->ObjectName(Net->LootItems[I])+TEXT("\nDouble-click to take."));
    }
}

bool SAC2GameUI::SmokeJournalInput(int32 Step,FString& Error)
{
    if(Step>=24)return SmokeOptions(Step-21,Error);
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));
    if(!Window)return Fail(TEXT("No Slate window"));
    auto Click=[&](FName Key){const auto* R=Buttons.Find(Key);if(!R)return false;
        auto P=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
        auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
    if(Step==0){if(!Mode->Network || Mode->Network->Skills.IsEmpty())return Fail(TEXT("Server did not deliver learned skills"));
        if(!Click(TEXT("SkillButton")) || !ShowSkills)return Fail(TEXT("Native skills toolbar button did not open its panel"));}
    if(Step==1){if(!Click(TEXT("MagicButton")) || SkillTab!=2)return Fail(TEXT("Native magic tab failed"));}
    if(Step==2){
        if(!Buttons.Contains(TEXT("LearnedSkill96")))return Fail(TEXT("Mind Lash icon missing from the magic repertoire"));
        const FIntRect Before=Native.Bounds(RightPanelRoot());
        const uint32 Root=FAC2UILayout::Token(RightPanelRoot());
        const FIntPoint SavedOffset=Native.RootOffsets.FindRef(Root);
        const FVector2D Delta(Before.Min.X<80?80:-80,Before.Min.Y<40?40:-40);
        const FVector2D From=GetCachedGeometry().LocalToAbsolute(FVector2D(Before.Min.X+Before.Width()/2,Before.Min.Y+12)*Scale),To=From+Delta*Scale;
        auto Path=App.LocateWindowUnderMouse(From,{Window.ToSharedRef()},false);
        App.RoutePointerDownEvent(Path,FPointerEvent(0,From,From,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        const uint32 CapturedRoot=DragWindow;const FName CapturedAction=PressedAction;
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,To,From,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,To,From,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(Native.Bounds(RightPanelRoot()).Min==Before.Min){
            Error=FString::Printf(TEXT("Window header did not drag (root %08X, action %s, offset %s, start %s)"),CapturedRoot,*CapturedAction.ToString(),*SavedOffset.ToString(),*Before.Min.ToString());return false;}
        // The acceptance test must not gradually push the user's saved panel off screen.
        Native.RootOffsets.Add(Root,SavedOffset);
        GConfig->SetVector2D(TEXT("AC2.Windows"),RightPanelRoot(),FVector2D(SavedOffset),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);Rebuild();
        if(!SmokeShortcuts(Error))return false;
        if(JournalMaxScroll>0){
            const int32 Saved=JournalScroll;const auto Thumb=JournalThumb;
            if(!Click(TEXT("Native4E1E5A87")))return Fail(TEXT("Skill tree scrollbar up button missing"));Rebuild();
            if(JournalScroll>=Saved || JournalThumb.Min.Y>=Thumb.Min.Y)return Fail(TEXT("Skill scrollbar did not move with tree"));
            JournalScroll=Saved;Rebuild();
        }
        const uint32 Chat=FAC2UILayout::Token(TEXT("ChatWindow1"));
        const FIntPoint ChatOffset=Native.RootOffsets.FindRef(Chat),ChatSize=Native.RootSizes.FindRef(Chat);
        const bool HadSize=Native.RootSizes.Contains(Chat);
        Native.RootOffsets.Add(Chat,FIntPoint(-170,-486));Native.RootSizes.Add(Chat,FIntPoint(300,160));Rebuild();
        const auto Frame=Native.Bounds(TEXT("ChatWindow1")),LogBefore=Native.Bounds(TEXT("ChatLog"));
        const auto ResizeFrom=GetCachedGeometry().LocalToAbsolute(FVector2D(Frame.Max.X-6,Frame.Min.Y+70)*Scale);
        const auto ResizeTo=GetCachedGeometry().LocalToAbsolute(FVector2D(Frame.Max.X+94,Frame.Min.Y+70)*Scale);
        uint32 CandidateRoot=0;uint8 CandidateEdges=0;
        const bool Candidate=WindowResizeAt(FVector2D(Frame.Max.X-6,Frame.Min.Y+70),CandidateRoot,CandidateEdges);
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,ResizeFrom,ResizeFrom,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        const uint32 CapturedResize=ResizeWindow;const uint8 CapturedEdges=ResizeEdges;
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,ResizeTo,ResizeFrom,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,ResizeTo,ResizeTo,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        const bool Resized=Native.Bounds(TEXT("ChatWindow1")).Width()==400 && Native.Bounds(TEXT("ChatLog")).Width()==LogBefore.Width()+100;
        const FString ResizeDiagnostic=FString::Printf(TEXT("Native chat resize failed (candidate %d %08X/%u, root %08X, edges %u, frame %d to %d, log %d to %d, origin %s, mouse %s)"),
            Candidate,CandidateRoot,CandidateEdges,CapturedResize,CapturedEdges,Frame.Width(),Native.Bounds(TEXT("ChatWindow1")).Width(),LogBefore.Width(),Native.Bounds(TEXT("ChatLog")).Width(),*Frame.Min.ToString(),*Mouse.ToString());
        Native.RootOffsets.Add(Chat,ChatOffset);
        if(HadSize){Native.RootSizes.Add(Chat,ChatSize);GConfig->SetVector2D(TEXT("AC2.WindowSizes"),TEXT("ChatWindow1"),FVector2D(ChatSize),GGameUserSettingsIni);}
        else{Native.RootSizes.Remove(Chat);GConfig->RemoveKey(TEXT("AC2.WindowSizes"),TEXT("ChatWindow1"),GGameUserSettingsIni);}
        GConfig->SetVector2D(TEXT("AC2.Windows"),TEXT("ChatWindow1"),FVector2D(ChatOffset),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);Rebuild();
        if(!Resized){Error=ResizeDiagnostic;return false;}
    }
    if(Step==3){if(!Click(TEXT("QuestButton")) || !ShowQuests)return Fail(TEXT("Native quest toolbar button failed"));
        if(Mode->Network->Quests.Num()<5)return Fail(TEXT("Server did not restore the NPC quest fixture"));}
    if(Step==4){
        const uint32 Tabs[]={0x715bdff7,0x70859237,0x92e39d2f};
        for(int32 Tab:{1,0,2}){
            if(!Click(*FString::Printf(TEXT("Native%08X"),Tabs[Tab])) || QuestTab!=Tab)return Fail(TEXT("Native quest tab click failed"));
            Rebuild();
            for(int32 I=0;I<3;++I){const auto* N=Native.Find(Tabs[I]);
                if(!N || N->State!=FAC2UILayout::Token(I==Tab?TEXT("open"):TEXT("closed")))return Fail(TEXT("Quest tab did not change native visual state"));}
        }
        Click(TEXT("Native715BDFF7"));Rebuild();
        for(auto& Q:Mode->Network->Quests)if(Q.Status==0x10000000){
            JournalScroll=0;Rebuild();const FName Key(*FString::Printf(TEXT("QuestRow%u"),Q.Id));
            if(Buttons.Contains(Key)){if(!Click(Key) || !ExpandedQuests.Contains(Q.Id))return Fail(TEXT("Quest row did not expand"));Rebuild();}
        }
        if(JournalMaxScroll<=0 || JournalThumb.IsEmpty())return Fail(TEXT("Expanded quest details did not create a scrollable list"));
        if(!Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text.Contains(TEXT("Intended Level:"));}))return Fail(TEXT("Native quest detail template was not formatted"));
        const auto StartThumb=JournalThumb;const auto BeforeScroll=JournalScroll;
        const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D(JournalThumb.Min+JournalThumb.Size()/2)*Scale);
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        const auto End=GetCachedGeometry().LocalToAbsolute(FVector2D(JournalBar.Min.X+5,JournalBar.Max.Y)*Scale);
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,End,P,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,End,End,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(JournalScroll!=JournalMaxScroll || JournalThumb.Min.Y<=StartThumb.Min.Y || JournalScroll<=BeforeScroll)return Fail(TEXT("Quest scrollbar drag did not move list and thumb together"));
        if(!Click(TEXT("Native44BA0270")) || JournalScroll==JournalMaxScroll)return Fail(TEXT("Quest scrollbar up arrow did not scroll"));
        JournalScroll=0;Rebuild();
    }
    if(Step==5){
        // The two journals can stay open together. Close the frontmost
        // quest window to get back to the skill book behind it.
        if(!ShowSkills || !Click(TEXT("Native92D81E44")) || ShowQuests || !ShowSkills)
            return Fail(TEXT("Closing the quest journal did not restore access to the open skill book"));
        Rebuild();
        if(!Click(TEXT("MagicButton")))return Fail(TEXT("Magic tab did not reopen"));
        Rebuild();
        if(!Click(TEXT("LearnedSkill85")))return Fail(TEXT("Ease Suffering node is missing"));
        Rebuild();
        if(ExaminedSkill!=85 || !ShowStats || !Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text.Contains(TEXT("must be trained"));}))return Fail(TEXT("Selected skill requirements were not inspected"));
        if(!Click(TEXT("TrainButton")))return Fail(TEXT("Native train button is missing"));
    }
    if(Step>=6 && Step<=9){
        const auto* Skill=Mode->Network->Skills.FindByPredicate([](const auto& S){return S.Id==85 && S.Trained;});
        if(!Skill)return Fail(TEXT("Native skill training was not acknowledged"));
        if(Step==6 && !Click(TEXT("RaiseButton")))return Fail(TEXT("Native advancement button is missing"));
        if(Step==7){
            if(FCString::Strtoui64(*Skill->Xp,nullptr,10)==0)return Fail(TEXT("Skill advancement was not acknowledged"));
            if(!Click(TEXT("UntrainButton")))return Fail(TEXT("Native untrain button is missing"));
        }
        if(Step==8){
            if(Mode->Network->UntrainingSkill!=85)return Fail(TEXT("Skill untraining state was not acknowledged"));
            if(!Click(TEXT("CancelUntrainButton")))return Fail(TEXT("Native cancel untraining button is missing"));
        }
        if(Step==9 && Mode->Network->UntrainingSkill!=0)return Fail(TEXT("Untraining cancellation was not acknowledged"));
    }
    if(Step==10){
        if(!Click(TEXT("SocialButton")) || !ShowSocial)return Fail(TEXT("Flag button did not open social panel"));Rebuild();
        if(!Native.Find(TEXT("NoFellowshipNameEntryText")) || !FellowshipEntry)return Fail(TEXT("Fellowship name input missing"));
        App.SetKeyboardFocus(FellowshipEntry,EFocusCause::SetDirectly);
        if(!FellowshipEntry->HasKeyboardFocus())return Fail(TEXT("Fellowship input could not receive keyboard focus"));
        const int32 Before=ShortcutSet;
        SmokeShortcutKey(EKeys::Zero,false,true);SmokeShortcutKey(EKeys::Hyphen);
        if(ShortcutSet!=Before)return Fail(TEXT("Typing a fellowship name changed hotbar pages"));
        App.SetAllUserFocusToGameViewport();
        FellowshipEntry->SetText(TEXT("Rendered Fellowship"));
        if(!Click(TEXT("FellowshipCreateButton")))return Fail(TEXT("Native fellowship create control missing"));
    }
    if(Step==11){
        auto& Net=*Mode->Network;
        if(Net.FellowshipName!=TEXT("Rendered Fellowship") || Net.Fellows.Num()!=1)return Fail(TEXT("Fellowship creation was not acknowledged"));
        if(!Native.Find(TEXT("FellowTemplateHealthText")))return Fail(TEXT("Native fellow vitals not displayed"));
    }
    if(Step==12 && !Click(TEXT("FellowshipDisbandButton")))return Fail(TEXT("Fellowship disband button missing"));
    if(Step==13){
        if(!Mode->Network->Fellows.IsEmpty())return Fail(TEXT("Fellowship disband did not clear members"));
        if(!Click(TEXT("Native69DD00FE")) || ShowSocial)return Fail(TEXT("Social close button failed"));
    }
    if(Step==14){
        // Use the real fixture's starter weapon and server acknowledgement. The
        // smoke client exits afterward; never persist its temporary shortcuts.
        TestingShortcuts=true;ShortcutSet=9;Shortcuts[99]={};
        Shortcuts[90]=FAC2Shortcut::ForSkill(0x60);Shortcuts[91]=FAC2Shortcut::ForSkill(0xcf);
        ShowSkills=false;ShowQuests=false;ShowStats=false;ShowInventory=true;PackRow=0;Rebuild();
        const auto* P=Mode->Characters.Active();
        const auto* Weapon=P?P->Items.FindByPredicate([](const auto& I){return I.Definition==0x470007ed && !I.Equipped;}):nullptr;
        if(!Weapon)return Fail(TEXT("Starter bow missing from shortcut fixture"));
        const FString Id=Weapon->NetworkId;
        const auto* From=Buttons.Find(*FString::Printf(TEXT("Cell%d"),Weapon->Cell));
        const auto* To=Buttons.Find(TEXT("ShortcutSlot9"));
        if(!From || !To)return Fail(TEXT("Inventory or hotbar drag target missing"));
        const auto A=GetCachedGeometry().LocalToAbsolute(FVector2D((From->Left+From->Right)*.5,(From->Top+From->Bottom)*.5)*Scale);
        const auto B=GetCachedGeometry().LocalToAbsolute(FVector2D((To->Left+To->Right)*.5,(To->Top+To->Bottom)*.5)*Scale);
        auto Path=App.LocateWindowUnderMouse(A,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return Fail(TEXT("Inventory drag path missing"));
        App.RoutePointerDownEvent(Path,FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(Shortcuts[99].Item!=Id)return Fail(TEXT("Rendered inventory drag did not bind starter bow"));
        const auto Layers=ShortcutLayers(Shortcuts[99]);
        if(Layers.IsEmpty())return Fail(TEXT("Item shortcut has no native icon layers"));
        for(const auto& Layer:Layers)if(!Native.Brush(Layer.Image))return Fail(TEXT("Item shortcut icon did not resolve from DAT"));
        Path=App.LocateWindowUnderMouse(B,{Window.ToSharedRef()},false);
        App.RoutePointerDownEvent(Path,FPointerEvent(0,B,B,{EKeys::RightMouseButton},EKeys::RightMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,B,B,{},EKeys::RightMouseButton,0,FModifierKeysState()));
    }
    if(Step==15){
        if(Mode->Network->ExaminationId!=Shortcuts[99].Item || Mode->Network->ExaminationRows.IsEmpty())
            return Fail(TEXT("Hotbar right-click did not receive server examination"));
        ShowInventory=false;Rebuild();
        if(!Click(TEXT("ShortcutSlot9")))return Fail(TEXT("Item shortcut click failed with inventory closed"));
    }
    if(Step==16 || Step==17){
        const int32 Index=ShortcutItemIndex(Shortcuts[99].Item);
        if(Index==INDEX_NONE)return Fail(TEXT("Equipment transaction lost item shortcut identity"));
        const bool Equipped=Mode->Characters.Active()->Items[Index].Equipped!=0;
        if(Equipped!=(Step==16))return Fail(TEXT("Item shortcut equip/unequip was not acknowledged by the server"));
        bool MagicBlocked=false,MissileBlocked=false;
        SkillEquipmentHint(0x60,MagicBlocked);const FString Missile=SkillEquipmentHint(0xcf,MissileBlocked);
        if(!MagicBlocked || MissileBlocked==Equipped || !Missile.Contains(Equipped?TEXT("Bow (equipped)"):TEXT("Bow (not equipped)")))
            return Fail(TEXT("Skill requirements did not follow acknowledged weapon changes"));
        ShowInventory=true;Rebuild();
        const auto* Slot=Buttons.Find(TEXT("ShortcutSlot1"));
        const bool Shade=Slot && Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Id==FAC2UILayout::Token(TEXT("bottom_fill")) &&
            N.Clip.Min.X>=Slot->Left && N.Clip.Max.X<=Slot->Right && N.Clip.Min.Y>=Slot->Top && N.Clip.Max.Y<=Slot->Bottom &&
            N.Media.ContainsByPredicate([](const auto& Art){return Art.Image==0x410016eau;});});
        if(Shade==Equipped)return Fail(TEXT("Native hotbar equipment shade did not update"));
        if(Step==16 && !Click(TEXT("ShortcutSlot9")))return Fail(TEXT("Equipped shortcut could not be used again"));
    }
    if(Step==18){
        ShowInventory=false;ShowStats=false;ShortcutRows=5;ShortcutSet=5;
        Shortcuts[59]=FAC2Shortcut::ForSkill(96);Shortcuts[69]=FAC2Shortcut::ForSkill(0xb0);
        Rebuild();
        if(!Buttons.Contains(TEXT("ShortcutSlot49")))return Fail(TEXT("Expanded hotbar lost its fifth visible row"));
    }
    if(Step==19){
        ShortcutSet=9;Rebuild();
        if(Buttons.Contains(TEXT("ShortcutSlot10")))return Fail(TEXT("Trailing hotbar padding became interactive"));
    }
    if(Step==20){
        App.SetAllUserFocusToGameViewport();
        if(!SmokeShortcutKey(EKeys::Nine,false,true) || ShortcutSet!=8)
            return Fail(TEXT("Ctrl+9 did not expose the bow on the second row"));
        if(!SmokeShortcutKey(EKeys::Zero,true))return Fail(TEXT("Second-row keyboard activation failed"));
    }
    if(Step==21 || Step==23){
        const int32 Index=ShortcutItemIndex(Shortcuts[99].Item);
        if(Index==INDEX_NONE || (Mode->Characters.Active()->Items[Index].Equipped!=0)!=(Step==21))
            return Fail(TEXT("Shift+0 item equip/unequip did not receive server acknowledgement"));
    }
    if(Step==22 && !SmokeShortcutKey(EKeys::Zero,true))return Fail(TEXT("Second-row keyboard unequip failed"));
    Rebuild();return true;
}

bool SAC2GameUI::SmokeLootInput(bool Take,FString& Error)
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!Mode->Network || Mode->Network->LootContainer.IsEmpty() || Mode->Network->LootItems.IsEmpty())
        return Fail(TEXT("No native container contents received"));
    if(!Native.Find(TEXT("ExternalContainerField")))return Fail(TEXT("Native loot window is missing"));
    if(!Take){
        const uint32 Root=FAC2UILayout::Token(TEXT("ExternalContainerField"));
        const FIntPoint Saved=Native.RootOffsets.FindRef(Root);
        Native.RootOffsets.Add(Root,Saved+FIntPoint(-120,-80));Rebuild();
        const auto Window=Native.Bounds(TEXT("ExternalContainerField"));
        const auto Heading=Native.Bounds(TEXT("ExternalContainerLabel"));
        const auto Close=Native.Bounds(TEXT("ExternalContainerCloseButton"));
        const bool Fits=Heading.Min.X>=Window.Min.X && Heading.Max.X<=Close.Min.X && Close.Max.X<=Window.Max.X;
        const bool Wide=Heading.Width()>=FMath::CeilToInt(BodyFont->Width(Mode->Network->ObjectName(Mode->Network->LootContainer)));
        Native.RootOffsets.Add(Root,Saved);Rebuild();
        if(!Fits || !Wide)return Fail(TEXT("Dragged loot window clips its heading or close button"));
    }
    const auto* R=Buttons.Find(TEXT("LootItem0"));if(!R)return Fail(TEXT("No clickable loot item"));
    const auto Art=Icons.FilterByPredicate([&](const FIcon& Icon){
        return Icon.Rect.Min.X>=R->Left && Icon.Rect.Max.X<=R->Right &&
            Icon.Rect.Min.Y>=R->Top && Icon.Rect.Max.Y<=R->Bottom;
    });
    if(Art.IsEmpty() || !Art.ContainsByPredicate([](const FIcon& Icon){return Icon.Image && Icon.Image!=0x4100034b;}))
        return Fail(TEXT("Loot item has only a missing/placeholder icon"));
    for(const auto& Icon:Art)if(!Native.Brush(Icon.Image))return Fail(TEXT("Loot icon layer did not resolve from the DAT"));
    const FVector2D Local((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5);
    if(!AllowsPanelButton(TEXT("LootItem0"),Local))return Fail(TEXT("Loot item is obscured by another panel"));
    if(!Take){
        FSlateApplication::Get().SetCursorPos(GetCachedGeometry().LocalToAbsolute(Local*Scale));Mouse=Local;Rebuild();
        const auto* Tip=Native.Find(0xd5a32d4d);
        if(!Tip || !Tip->Text.Contains(TEXT("Double-click to take.")))return Fail(TEXT("Native loot tooltip is missing"));
        const auto Font=Fonts.Get(Tip->Attr(TEXT("font"),0x4200000c));
        const int32 Width=Tip->Rect.Width()-Tip->Attr(TEXT("left_margin"))-Tip->Attr(TEXT("right_margin"));
        if(!Font || Tip->Rect.Height()<Font->Lines(Tip->Text,Width).Num()*int32(Font->Height))return Fail(TEXT("Loot tooltip clips wrapped text"));
        if(Tip->Clip.Min.X<0 || Tip->Clip.Max.X>Size.X || Tip->Clip.Min.Y<0 || Tip->Clip.Max.Y>Size.Y)return Fail(TEXT("Loot tooltip leaves viewport"));
    }
    if(Take){
        const auto P=GetCachedGeometry().LocalToAbsolute(Local*Scale);
        if(!OnMouseButtonDoubleClick(GetCachedGeometry(),FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState())).IsEventHandled())
            return Fail(TEXT("Native item double-click was not handled"));
    }
    return true;
}
