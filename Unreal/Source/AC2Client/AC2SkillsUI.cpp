#include "AC2GameUI.h"
#include "AC2GameMode.h"

FString SAC2GameUI::SkillEquipmentHint(uint32 Skill,bool& Blocked)
{
    Blocked=false;const auto Info=SkillData.Get(Skill);
    if(!Info.Valid || !Info.HasImplements)return {};
    TArray<FString> Lines;
    for(int32 Hand=0;Hand<2;++Hand){
        const uint32 Required=Hand?Info.LeftWeapons:Info.Weapons;
        if(Required==MAX_uint32)continue;
        const auto Held=Mode->Network?Mode->Network->HeldImplement(Hand?0x80:0x40):TOptional<uint32>();
        const auto Fits=FAC2SkillData::Holds(Required,Held);
        const bool Unmet=Fits.IsSet() && !Fits.GetValue();Blocked|=Unmet;
        Lines.Add(FString(Hand?TEXT("Left hand: "):TEXT("Right hand: "))+FAC2SkillData::ImplementName(Required)+
            (!Fits.IsSet()?TEXT(" (equipment data pending)"):Unmet?TEXT(" (not equipped)"):TEXT(" (equipped)")));
    }
    return FString::Join(Lines,TEXT("\n"));
}

FAC2SkillContext SAC2GameUI::CurrentSkills() const
{
    FAC2SkillContext C;
    if(!Mode.IsValid() || !Mode->Network)return C;
    const auto& Net=*Mode->Network;
    if(const auto* P=Mode->Characters.Active()){C.Race=P->Race;C.Level=P->Level;}
    C.Credits=Net.SkillCredits;C.AvailableXp=Net.AvailableXp;C.Untraining=Net.UntrainingSkill;C.Dead=Net.Dead;
    for(const auto& S:Net.Skills)if(S.Trained)C.Known.Add(S.Id,FCString::Strtoui64(*S.Xp,nullptr,10));
    return C;
}
void SAC2GameUI::BuildSkills()
{
    BindNative(0xc932ecb7,TEXT("SkillsHelp"));
    auto* Net=Mode->Network.Get();const auto* Player=Mode->Characters.Active();
    if(!Net || !Player)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};
    if(const auto* N=Native.Find(0x96a07fb7)){
        Buttons.Add(TEXT("Native96A07FB7"),FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));
        ButtonActions.Add(TEXT("Native96A07FB7"),TEXT("Skills"));}
    for(const TCHAR* Tab:{TEXT("MeleeButton"),TEXT("MissileButton"),TEXT("MagicButton"),TEXT("NoncombatButton"),TEXT("CraftTab")})Bind(Tab,FName(Tab));
    if(SkillTab==4){BuildCrafting();return;}
    if(!SkillPanel)SkillPanel=FAC2SkillData::RootPanel(Player->Race,SkillTab);
    const auto& Data=SkillData.Panel(SkillPanel);if(!Data.Valid)return;
    const auto Context=CurrentSkills();const auto& Known=Context.Known;
    auto* Panel=Native.Find(0x839fa006);if(!Panel)return;const auto Box=Panel->Rect,Clip=Panel->Clip;const auto PanelId=Panel->Id;
    const auto Selected=SkillData.Get(SelectedSkill);const uint64 Spent=Known.FindRef(SelectedSkill),Cost=SkillData.NextCost(SelectedSkill,Spent);
    const bool Trained=Known.Contains(SelectedSkill);
    const bool Untraining=SelectedSkill && Net->UntrainingSkill==SelectedSkill;
    const auto* SelectedNode=Data.Nodes.FindByPredicate([&](const auto& N){return N.Id==SelectedSkill;});
    const auto Actions=SkillData.Actions(SelectedSkill,Context,!SelectedNode||SelectedNode->Untrainable);
    Native.States.Add(T(TEXT("ModifyField")),T(Untraining?TEXT("untraining"):Trained?TEXT("trained"):SelectedSkill?TEXT("untrained"):TEXT("panel")));
    Native.States.Add(T(TEXT("ButtonField")),T(Untraining?TEXT("untraining"):Trained?TEXT("trained"):SelectedSkill?TEXT("untrained"):TEXT("panel")));
    Native.States.Add(T(TEXT("TrainButton")),T(Actions.Train.IsEmpty()?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("RaiseButton")),T(Actions.Raise.IsEmpty()?TEXT("normal"):TEXT("ghosted")));
    // Native 'untrainable' means that the skill CAN be untrained; permanent
    // choices use a confirmation before training in the original client.
    Native.States.Add(T(TEXT("UntrainButton")),T(Actions.Untrain.IsEmpty()?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("CancelUntrainButton")),T(Actions.Cancel.IsEmpty()?TEXT("normal"):TEXT("ghosted")));
    Native.Text.Add(T(TEXT("SelectedName")),SelectedSkill?Selected.Name:Data.Name);
    Native.Text.Add(T(TEXT("MiscText")),!SelectedSkill?FString():Untraining?FString::Printf(TEXT("Untraining: earn %llu more XP"),Spent):Trained?(Cost?FString::Printf(TEXT("XP: %llu / Next level: %llu"),Net->AvailableXp,Cost):Actions.Raise):
        FString::Printf(TEXT("Skill credits: %d / Cost: %u"),Net->SkillCredits,SelectedSkill?Selected.Credits:0));
    Native.Template(0x22000001,0xaf472f02,FIntRect(Box.Min+FIntPoint(0,21),Box.Max),Clip,PanelId);
    for(const TCHAR* B:{TEXT("TrainButton"),TEXT("RaiseButton"),TEXT("UntrainButton"),TEXT("CancelUntrainButton")})Bind(B,FName(B));
    Hints.Add(TEXT("TrainButton"),Actions.Train.IsEmpty()?TEXT("Train the selected skill."):Actions.Train);
    Hints.Add(TEXT("RaiseButton"),Actions.Raise.IsEmpty()?FString::Printf(TEXT("Spend %llu XP to raise this skill."),Cost):Actions.Raise);
    Hints.Add(TEXT("UntrainButton"),Actions.Untrain.IsEmpty()?TEXT("Untrain this skill. New earned XP refunds its invested XP before returning its skill credits."):Actions.Untrain);
    Hints.Add(TEXT("CancelUntrainButton"),Actions.Cancel.IsEmpty()?TEXT("Stop untraining and keep the remaining invested XP."):Actions.Cancel);
    if(const auto* Icon=Native.Find(TEXT("SelectedIcon")))Icons.Add({Icon->Rect,Icon->Clip,SelectedSkill?Selected.Icon:Data.Icon});
    const auto* Tree=Native.Find(TEXT("SkillTree"));if(!Tree)return;
    SkillClip=Tree->Clip;const auto Grid=Tree->Rect;
    const int32 NW=Tree->Attr(TEXT("node_width"),38),NH=Tree->Attr(TEXT("node_height"),52);
    if(NW<=0||NH<=0)return;
    const bool Horizontal=Tree->Attr(TEXT("horizontal"))!=0,Orient=Tree->Attr(TEXT("orient_high"))!=0;
    const bool Straight=Tree->Attr(TEXT("straight_connect"))!=0,Lines=Tree->Attr(TEXT("no_lines"))==0;
    const int32 Away=Tree->Attr(TEXT("straight_away_pix"));
    SkillLineColor=FLinearColor(Tree->Float(TEXT("line_color_red")),Tree->Float(TEXT("line_color_green")),Tree->Float(TEXT("line_color_blue")),1);
    int32 PaperW=Grid.Width(),PaperH=Grid.Height();
    for(const auto& N:Data.Nodes){PaperW=FMath::Max(PaperW,(N.Cell.X+1)*NW);PaperH=FMath::Max(PaperH,(N.Cell.Y+1)*NH);}
    const int32 MaximumTreeScroll=PaperH-Grid.Height();JournalMaxScroll=MaximumTreeScroll;
    JournalScroll=JournalScroll<0?MaximumTreeScroll:FMath::Clamp(JournalScroll,0,MaximumTreeScroll);
    UpdateScrollbar(0x62245fc3,JournalScroll,JournalMaxScroll,JournalBar,JournalThumb,TEXT("SkillScrollUp"),TEXT("SkillScrollDown"));
    TMap<uint32,FIntPoint> Points;
    for(const auto& N:Data.Nodes){
        int32 X=N.Cell.X*NW,Y=N.Cell.Y*NH;
        if(Horizontal&&Orient)X=PaperW-NW-X;
        if(!Horizontal&&!Orient)Y=PaperH-NH-Y;
        Points.Add(N.Id,Grid.Min+FIntPoint(X-FMath::Max(0,PaperW-Grid.Width()),Y-JournalScroll));
    }
    const auto* NodeTemplate=Native.Layouts[0x22000001].Find(TEXT("SkillNodeTemplate"));
    const FIntPoint NodeSize=NodeTemplate?NodeTemplate->Rect.Size()+FIntPoint(1,1):FIntPoint(38,52);
    for(const auto& N:Data.Nodes){
        const bool IsSkill=N.Kind==0x40000001;const auto Info=SkillData.Get(N.Id);const auto XY=Points[N.Id];
        if(Lines)for(uint32 Parent:N.Parents)if(const auto* P=Points.Find(Parent)){
            const FVector2D From=FVector2D(*P)+FVector2D(NodeSize.X*.5,Orient?NodeSize.Y:0);
            const FVector2D To=FVector2D(XY)+FVector2D(NodeSize.X*.5,Orient?0:NodeSize.Y);
            if(Straight){const FVector2D Bend(From.X,From.Y+(Orient?Away:-Away)),Corner(To.X,Bend.Y);
                SkillLinks.Add({From,Bend});SkillLinks.Add({Bend,Corner});SkillLinks.Add({Corner,To});}
            else SkillLinks.Add({From,To});
        }
        if(XY.Y+NodeSize.Y<=SkillClip.Min.Y||XY.Y>=SkillClip.Max.Y)continue;
        const FIntRect Rect(XY,XY+NodeSize);
        Native.States.Add(T(TEXT("SkillNodeTemplate")),T(IsSkill?TEXT("skill"):N.Kind==0x40000003?TEXT("minus"):TEXT("plus")));
        Native.States.Add(T(TEXT("Background")),T(N.Id==SelectedSkill?TEXT("selected"):TEXT("stateless")));
        const auto NodeActions=IsSkill?SkillData.Actions(N.Id,Context,N.Untrainable):FAC2SkillActions();
        Native.States.Add(T(TEXT("IconArea")),T(!IsSkill?TEXT("stateless"):Known.Contains(N.Id)?(NodeActions.Raise.IsEmpty()?TEXT("canraise"):TEXT("stateless")):NodeActions.Train.IsEmpty()?TEXT("cantrain"):TEXT("greyed")));
        Native.Text.Add(T(TEXT("SkillNodeLevelText")),IsSkill&&Known.Contains(N.Id)?FString::FromInt(SkillData.Rank(N.Id,Known[N.Id])):FString());
        const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("SkillNodeTemplate"),Rect,SkillClip);
        for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==T(TEXT("SkillNodeIcon")))
            Icons.Add({Native.Nodes[I].Rect,Native.Nodes[I].Clip,N.Icon,1,FLinearColor::White,I});
        const FName Key(*FString::Printf(TEXT("%s%u"),IsSkill?TEXT("LearnedSkill"):TEXT("SkillPanel"),N.Id));
        Buttons.Add(Key,FSlateRect(FMath::Max(Rect.Min.X,SkillClip.Min.X),FMath::Max(Rect.Min.Y,SkillClip.Min.Y),FMath::Min(Rect.Max.X,SkillClip.Max.X),FMath::Min(Rect.Max.Y,SkillClip.Max.Y)));
        FString Hint=N.Name+TEXT("\n")+N.Description;
        if(IsSkill){Hint+=FString::Printf(TEXT("\nLevel %u; %u skill credits"),Info.Level,Info.Credits);
            if(!N.Passive){bool Blocked=false;const FString Equipment=SkillEquipmentHint(N.Id,Blocked);
                if(!Equipment.IsEmpty())Hint+=TEXT("\n")+Equipment;}
            for(const auto& R:Info.Required)Hint+=TEXT("\nRequires ")+SkillData.Get(R.Key).Name+FString::Printf(TEXT(" (%u)"),R.Value);
            if(Known.Contains(N.Id)){
                Hint+=TEXT("\n")+(NodeActions.Raise.IsEmpty()?TEXT("Select to raise this skill."):NodeActions.Raise);
                Hint+=Net->UntrainingSkill==N.Id?TEXT("\nUnavailable while untraining."):N.Passive?TEXT("\nPassive skill; no activation required."):TEXT("\nDouble-click to add to this hotbar page, or drag to a slot.");
            }else Hint+=TEXT("\n")+(NodeActions.Train.IsEmpty()?TEXT("Available to train. Select, then press Train."):NodeActions.Train);
        }
        Hints.Add(Key,Hint);
    }
    for(const TCHAR* Key:{TEXT("SkillNodeTemplate"),TEXT("Background"),TEXT("IconArea")})Native.States.Remove(T(Key));
}
