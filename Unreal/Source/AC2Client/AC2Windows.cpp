#include "AC2GameUI.h"

void SAC2GameUI::BuildWindowContent(uint32 Window,TFunction<void()> Build)
{
    const int32 First=Native.Nodes.Num(),FirstIcon=Icons.Num();
    const auto PreviousButtons=Buttons;
    Native.ContentWindow=Window;Build();Native.ContentWindow=0;
    for(int32 I=First;I<Native.Nodes.Num();++I)Native.Nodes[I].Window=Window;
    for(int32 I=FirstIcon;I<Icons.Num();++I)Icons[I].Window=Window;
    for(const auto& B:Buttons)if(!PreviousButtons.Contains(B.Key) || PreviousButtons[B.Key]!=B.Value)ButtonWindows.Add(B.Key,Window);
}
uint32 SAC2GameUI::WindowAt(FVector2D Point) const
{
    const FIntPoint Pixel(Point.X,Point.Y);
    const uint32 Status=FAC2UILayout::Token(TEXT("StatusbarField"));
    for(int32 I=WindowStack.Num()-1;I>=0;--I)
    {
        const auto* N=Native.Find(WindowStack[I]);
        if(!N || !N->Clip.Contains(Pixel))continue;
        // The DAT reserves space below the vitals for several rows of buffs.
        // Empty slots in that strip must still accept clicks in the world.
        if(N->Id==Status){
            for(const auto& B:Buttons)
                if(ButtonWindows.FindRef(B.Key)==Status && B.Value.ContainsPoint(Point))return Status;
            if(!Native.Nodes.ContainsByPredicate([&](const FAC2NativeNode& Child){
                return Child.Window==Status && !Child.Media.IsEmpty() && Child.Clip.Contains(Pixel);
            }))continue;
        }
        return N->Id;
    }
    return 0;
}
void SAC2GameUI::RaiseWindow(uint32 Window)
{
    if(!Window)return;
    // The toolbar's opaque center is the backing for the separate shortcut
    // field. Clicking a toolbar button must never raise it over those slots.
    if(WindowStack.Contains(Window) && (Window==FAC2UILayout::Token(TEXT("ToolbarField")) ||
        Window==FAC2UILayout::Token(TEXT("StatusbarField"))))return;
    WindowStack.Remove(Window);WindowStack.Add(Window);
}
void SAC2GameUI::SaveJournalWindow()
{
    if(JournalWindow)JournalViews.Add(JournalWindow,{JournalScroll,JournalMaxScroll,JournalBar,JournalThumb});
}
void SAC2GameUI::SelectJournalWindow(uint32 Window)
{
    if(Window!=FAC2UILayout::Token(TEXT("SkillField")) && Window!=FAC2UILayout::Token(TEXT("QuestField")) && Window!=FAC2UILayout::Token(TEXT("SocialField")))return;
    if(Window==JournalWindow)return;
    SaveJournalWindow();JournalWindow=Window;
    auto& V=JournalViews.FindOrAdd(Window);JournalScroll=V.Scroll;JournalMaxScroll=V.Maximum;JournalBar=V.Bar;JournalThumb=V.Thumb;
}
