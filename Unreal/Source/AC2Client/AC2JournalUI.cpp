#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"

bool SAC2GameUI::AllowsPanelButton(FName Name,FVector2D Point) const
{
    if(CraftOwnsPointer(Point))return Name==TEXT("CraftClose") || Name==TEXT("CraftButton") || Name==TEXT("AutoPopulateButton") || Name==TEXT("CraftLevelDown") || Name==TEXT("CraftLevelUp") || Name.ToString().StartsWith(TEXT("CraftIngredient"));
    if(Native.Bounds(TEXT("ExternalContainerField")).Contains(FIntPoint(Point.X,Point.Y)))
        return Name.ToString().StartsWith(TEXT("Loot")) || ButtonActions.FindRef(Name).ToString().StartsWith(TEXT("Loot"));
    return !ShopOwnsPointer(Point) || ShopButton(Name);
}

void SAC2GameUI::BuildJournal()
{
    if(!ShowSkills && !ShowQuests)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    auto Button=[&](uint32 Id,FName Action){if(const auto* N=Native.Find(Id)){
        const FName Key(*FString::Printf(TEXT("Native%08X"),Id));
        Buttons.Add(Key,FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));ButtonActions.Add(Key,Action);}};
    auto TextRow=[&](FString Text,FIntRect Rect,FIntRect Clip){
        const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),Rect,Clip);
        if(Native.Nodes.IsValidIndex(First))Native.Nodes[First].Text=MoveTemp(Text);
    };
    auto* Net=Mode->Network.Get();
    if(ShowSkills){
        BuildSkills();
    }else{
        Button(0x92d81e44,TEXT("Quests"));
        Button(0x715bdff7,TEXT("QuestActive"));Button(0x70859237,TEXT("QuestDone"));Button(0x92e39d2f,TEXT("QuestStory"));
        const auto* Panel=Native.Find(0x87e7c1f5);if(!Panel)return;
        const FIntRect Box=Panel->Rect,Clip=Panel->Clip;
        const uint32 Pages[]={0x71758fff,0x70abc23f,0x93e398e5};
        Native.Template(0x22000001,Pages[QuestTab],FIntRect(Box.Min+FIntPoint(0,21),Box.Max-FIntPoint(0,18)),Clip,Panel->Id);
        const uint32 Lists[]={0x6a170865,0x6bc945a5,0x5f75e612};
        const auto* List=Native.Find(Lists[QuestTab]);if(!List)return;const FIntRect Area=List->Clip;
        int32 Y=Area.Min.Y-JournalScroll;
        // Story acts use a separate native repository, not the ordinary quest list.
        if(Net && QuestTab!=2)for(const auto& Q:Net->Quests){
            const bool Done=Q.Status==0x30000000;if((QuestTab==0&&Done)||(QuestTab==1&&!Done))continue;
            FString Body=Q.Name+TEXT("\n")+(Q.Journal.IsEmpty()?Q.Description:Q.Journal);
            const int32 Height=BodyFont?FMath::Max(40,int32(BodyFont->Lines(Body,Area.Width()).Num()*BodyFont->Height)+12):80;
            if(Y+Height>Area.Min.Y && Y<Area.Max.Y)TextRow(Body,FIntRect(Area.Min.X,Y,Area.Max.X,Y+Height),Area);
            Y+=Height;
        }
        if(Y==Area.Min.Y-JournalScroll)TextRow(TEXT("No quests in this category."),Area,Area);
        JournalScroll=FMath::Min(JournalScroll,FMath::Max(0,Y+JournalScroll-Area.Max.Y));
    }
}
bool SAC2GameUI::JournalAction(FName Name)
{
    const FString Key=Name.ToString();
    if(Name==TEXT("TrainButton") || Name==TEXT("RaiseButton") || Name==TEXT("UntrainButton") || Name==TEXT("CancelUntrainButton")){
        if(const auto* Button=Native.Find(*Key);!Button || Button->State==FAC2UILayout::Token(TEXT("ghosted")))return true;
        if(Mode->Network && SelectedSkill)Mode->Network->ChangeSkill(SelectedSkill,Name==TEXT("TrainButton")?TEXT("train"):Name==TEXT("RaiseButton")?TEXT("raise"):Mode->Network->UntrainingSkill==SelectedSkill?TEXT("cancel_untrain"):TEXT("untrain"));return true;
    }
    if(Key.StartsWith(TEXT("SkillPanel"))){const uint32 Id=FCString::Strtoui64(*Key.Mid(10),nullptr,10);
        if(SkillData.Panel(Id).Valid){SkillPanel=Id;SelectedSkill=0;JournalScroll=-1;}return true;}
    if(Key.StartsWith(TEXT("LearnedSkill"))){SelectedSkill=FCString::Atoi(*Key.Mid(12));return true;}
    if(Name==TEXT("MeleeButton")||Name==TEXT("MissileButton")||Name==TEXT("MagicButton")||Name==TEXT("NoncombatButton")||Name==TEXT("CraftTab")){
        SkillTab=Name==TEXT("MeleeButton")?0:Name==TEXT("MissileButton")?1:Name==TEXT("MagicButton")?2:Name==TEXT("NoncombatButton")?3:4;SkillPanel=0;SelectedSkill=0;JournalScroll=-1;return true;}
    if(Name==TEXT("QuestActive")||Name==TEXT("QuestDone")||Name==TEXT("QuestStory")){
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
        const FIntRect Before=Native.Bounds(TEXT("RightSidePanel"));
        const uint32 Root=FAC2UILayout::Token(TEXT("RightSidePanel"));
        const FIntPoint SavedOffset=Native.RootOffsets.FindRef(Root);
        const FVector2D Delta(Before.Min.X<80?80:-80,Before.Min.Y<40?40:-40);
        const FVector2D From=GetCachedGeometry().LocalToAbsolute(FVector2D(Before.Min.X+Before.Width()/2,Before.Min.Y+12)*Scale),To=From+Delta*Scale;
        auto Path=App.LocateWindowUnderMouse(From,{Window.ToSharedRef()},false);
        App.RoutePointerDownEvent(Path,FPointerEvent(0,From,From,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        const uint32 CapturedRoot=DragWindow;const FName CapturedAction=PressedAction;
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,To,From,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,To,From,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(Native.Bounds(TEXT("RightSidePanel")).Min==Before.Min){
            Error=FString::Printf(TEXT("Window header did not drag (root %08X, action %s, offset %s, start %s)"),CapturedRoot,*CapturedAction.ToString(),*SavedOffset.ToString(),*Before.Min.ToString());return false;}
        // The acceptance test must not gradually push the user's saved panel off screen.
        Native.RootOffsets.Add(Root,SavedOffset);
        GConfig->SetVector2D(TEXT("AC2.Windows"),TEXT("RightSidePanel"),FVector2D(SavedOffset),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);Rebuild();
        if(!SmokeShortcuts(Error))return false;
    }
    if(Step==3){if(!Click(TEXT("QuestButton")) || !ShowQuests)return Fail(TEXT("Native quest toolbar button failed"));}
    if(Step==4){
        const uint32 Tabs[]={0x715bdff7,0x70859237,0x92e39d2f};
        for(int32 Tab:{1,0,2}){
            if(!Click(*FString::Printf(TEXT("Native%08X"),Tabs[Tab])) || QuestTab!=Tab)return Fail(TEXT("Native quest tab click failed"));
            Rebuild();
            for(int32 I=0;I<3;++I){const auto* N=Native.Find(Tabs[I]);
                if(!N || N->State!=FAC2UILayout::Token(I==Tab?TEXT("open"):TEXT("closed")))return Fail(TEXT("Quest tab did not change native visual state"));}
        }
    }
    if(Step==5){
        if(!Click(TEXT("SkillButton")) || !ShowSkills)return Fail(TEXT("Skills did not reopen"));
        Rebuild();
        if(!Click(TEXT("MagicButton")))return Fail(TEXT("Magic tab did not reopen"));
        Rebuild();
        if(!Click(TEXT("LearnedSkill85")))return Fail(TEXT("Ease Suffering node is missing"));
        Rebuild();
        if(!Click(TEXT("TrainButton")))return Fail(TEXT("Native train button is missing"));
    }
    if(Step>=6){
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
