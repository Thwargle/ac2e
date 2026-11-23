#include "AC2GameUI.h"

// gmExaminationUI::ProcessNode / DoBreak: each scalar owns its DAT appearance;
// images and text share a line, explicit breaks reset the 16-pixel tab indent.
void SAC2GameUI::LayoutExamination(const TArray<FAC2Network::FExaminationRow>& Stream,FIntRect List)
{
    if(List.Width()<=0 || List.Height()<=0)return;
    const int32 FirstNode=Native.Nodes.Num(),FirstIcon=Icons.Num(),PreviousScroll=ExamineScroll;
    const uint32 Parent=FAC2UILayout::Token(TEXT("ExaminationListBox"));
    int32 X=0,Y=0,Bottom=0,Indent=0;
    int32 LineStart=Native.Nodes.Num();
    auto AlignLine=[&](){
        // The title shares the icon's row. Center text in that row instead of
        // pinning its baseline to the icon's top border.
        for(int32 I=LineStart;I<Native.Nodes.Num();++I){auto& N=Native.Nodes[I];if(N.Text.IsEmpty())continue;
            N.Rect+=FIntPoint(0,FMath::Max(0,(Bottom-Y-N.Rect.Height())/2));N.Clip=N.Rect;N.Clip.Clip(List);
        }
        LineStart=Native.Nodes.Num();
    };
    auto Break=[&](bool Wrapped){
        AlignLine();
        Y=Bottom=FMath::Max(Bottom,Y+16);
        if(!Wrapped)Indent=0;
        X=FMath::Min(Indent*16,List.Width()-1);
    };
    // Templates are measured while visible: offscreen template creation is
    // culled before font lookup and would otherwise collapse the scroll range.
    const FIntRect Probe(List.Min,List.Min+FIntPoint(List.Width(),32));
    for(const auto& Item:Stream)
    {
        if(Item.Kind==0x40000006){Break(false);continue;}
        if(Item.Kind==0x40000007){
            const int32 Count=FMath::Clamp(int32(Item.Tabs),1,256);
            Indent=FMath::Min(Indent+Count,FMath::Max(0,(List.Width()-1)/16));
            X=FMath::Min(X+Count*16,List.Width()-1);continue;
        }
        if(Item.Image || Item.Kind==0x40000009)
        {
            auto Layers=Item.Layers;
            if(Item.Image)Layers.Add({0,Item.Image,0xffffffff});
            FIntPoint Extent=FIntPoint::ZeroValue;
            for(const auto& L:Layers){
                Native.LoadImage(L.Image,*Assets);
                if(const auto* B=Native.Brush(L.Image)){
                    Extent.X=FMath::Max(Extent.X,FMath::CeilToInt(B->ImageSize.X));
                    Extent.Y=FMath::Max(Extent.Y,FMath::CeilToInt(B->ImageSize.Y));
                }
            }
            if(Extent.X<=0 || Extent.Y<=0)continue;
            if(X>=List.Width()){Break(true);X=FMath::Min(X+16,List.Width()-1);++Indent;}
            Extent.X=FMath::Min(Extent.X,List.Width()-X);
            const int32 Index=Native.Nodes.Num();
            Native.Template(0x22000001,TEXT("ExaminationImageTemplate"),Probe,List,Parent);
            if(!Native.Nodes.IsValidIndex(Index))continue;
            auto& N=Native.Nodes[Index];
            N.Rect=FIntRect(List.Min+FIntPoint(X,Y-ExamineScroll),List.Min+FIntPoint(X+Extent.X,Y+Extent.Y-ExamineScroll));
            N.Clip=N.Rect;N.Clip.Clip(List);
            for(const auto& L:Layers)Icons.Add({N.Rect,List,L.Image,1,FLinearColor(FColor(L.Color)),Index});
            X+=Extent.X+4;Bottom=FMath::Max(Bottom,Y+Extent.Y);continue;
        }
        // EffectID is a request to the retail script interpreter to expand an
        // effect subprofile, not a text appearance. Never treat its depth as one.
        if(Item.Kind==0x4000000c)continue;
        if(Item.Text.IsEmpty()){if(!Item.Kind)Break(false);continue;}
        const int32 Index=Native.Nodes.Num();
        if(!Item.Appearance || !Native.Template(0x22000001,Item.Appearance,Probe,List,Parent))
            Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),Probe,List,Parent);
        if(!Native.Nodes.IsValidIndex(Index))continue;
        auto& N=Native.Nodes[Index];N.Text=Item.Text;
        const auto Font=Fonts.Get(N.Attr(TEXT("font"),0x4200000c));
        const int32 Horizontal=N.Attr(TEXT("left_margin"))+N.Attr(TEXT("right_margin"));
        const int32 Vertical=N.Attr(TEXT("top_margin"))+N.Attr(TEXT("bottom_margin"));
        int32 Width=Font?FMath::CeilToInt(Font->Width(Item.Text))+Horizontal:List.Width();
        if(X+Width>=List.Width() && X>Indent*16){Break(true);LineStart=Index;}
        Width=FMath::Clamp(Width,1,List.Width()-X);
        const int32 Height=Font?Font->Lines(Item.Text,FMath::Max(1,Width-Horizontal)).Num()*Font->Height+Vertical:16;
        N.Rect=FIntRect(List.Min+FIntPoint(X,Y-ExamineScroll),List.Min+FIntPoint(X+Width,Y+Height-ExamineScroll));
        N.Clip=N.Rect;N.Clip.Clip(List);
        X+=Width;Bottom=FMath::Max(Bottom,Y+Height);
        if(!Item.Kind)Break(false);
    }
    AlignLine();
    ExamineMaxScroll=FMath::Max(0,Bottom-List.Height());
    ExamineScroll=FMath::Clamp(ExamineScroll,0,ExamineMaxScroll);
    const FIntPoint Correction(0,PreviousScroll-ExamineScroll);
    for(int32 I=FirstNode;I<Native.Nodes.Num();++I){auto& N=Native.Nodes[I];N.Rect+=Correction;N.Clip=N.Rect;N.Clip.Clip(List);}
    for(int32 I=FirstIcon;I<Icons.Num();++I)Icons[I].Rect+=Correction;
    UpdateScrollbar(0xb9d70117,ExamineScroll,ExamineMaxScroll,ExamineBar,ExamineThumb,TEXT("ExamineUp"),TEXT("ExamineDown"));
}
