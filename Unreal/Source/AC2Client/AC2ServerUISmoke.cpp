#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"

// Explicit live-server acceptance: existing character, private self-tell and
// read-only panels. No purchases, attacks, quest hand-ins or inventory edits.
bool SAC2GameUI::SmokeServerUI(int32 Step,FString& Error)
{
    auto Fail=[&](const FString& Why){Error=Why;return false;};
    if(!Mode.IsValid() || !Mode->Network || !Mode->Characters.Active())return Fail(TEXT("No server character"));
    auto& App=FSlateApplication::Get();
    auto Window=App.FindWidgetWindow(AsShared());if(!Window)return Fail(TEXT("No UI window"));
    auto Click=[&](const TCHAR* Name){
        const auto* R=Buttons.Find(Name);if(!R)return false;
        const auto At=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
        auto Path=App.LocateWindowUnderMouse(At,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,At,At,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,At,At,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    if(Step==0){BeginChat();Rebuild();return Editing && Native.Bounds(TEXT("ChatEntry")).Area()>0?true:Fail(TEXT("Chat entry missing"));}
    if(Step==1){
        FString Line=TEXT("/tell ")+Mode->Characters.Active()->Name+TEXT(", AC2 private UI compatibility check.");
        FParse::Value(FCommandLine::Get(),TEXT("AC2UISmokeChat="),Line);
        for(TCHAR C:Line)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        if(Entry->Value()!=Line)return Fail(TEXT("Typing did not reach the focused chat entry"));
        App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
        return !Editing && Entry->Value().IsEmpty()?true:Fail(TEXT("Enter did not submit chat"));
    }
    if(Step==2){
        FString Expected=TEXT("AC2 private UI compatibility check.");
        FParse::Value(FCommandLine::Get(),TEXT("AC2UISmokeReply="),Expected);
        for(const auto& Line:Log.Lines)if(Line.Text.Contains(Expected))return true;
        return Fail(TEXT("Private tell not received from server"));
    }
    const TCHAR* ButtonsToClick[]={TEXT("InventoryButton"),TEXT("SkillButton"),TEXT("ExaminationButton"),TEXT("QuestButton"),TEXT("SocialButton"),TEXT("OptionButton"),TEXT("NavigationButton")};
    const TCHAR* Fields[]={TEXT("InventoryField"),TEXT("SkillField"),TEXT("ExaminationField"),TEXT("QuestField"),TEXT("SocialField"),TEXT("OptionField")};
    const int32 Index=Step-3;
    if(Index<0 || Index>=UE_ARRAY_COUNT(ButtonsToClick))return true;
    if(!Click(ButtonsToClick[Index]))return Fail(FString(TEXT("Toolbar hit target missing: "))+ButtonsToClick[Index]);
    Rebuild();
    if(Index==6)return Mode->bMap?true:Fail(TEXT("Map button failed"));
    const auto* Panel=Native.Find(Fields[Index]);
    if(!Panel || Panel->Clip.Area()<=0)return Fail(FString(TEXT("Panel did not resolve: "))+Fields[Index]);
    if(!OwnsPointer(FVector2D(Panel->Clip.Min)+FVector2D(Panel->Clip.Size())*.5))return Fail(TEXT("Panel does not own its hit area"));
    const uint32 Root=FAC2UILayout::Token(Native.PanelRoot(Fields[Index]));
    const FIntPoint Saved=Native.RootOffsets.FindRef(Root),Before=Panel->Rect.Min;
    const auto R=Native.Bounds(Native.PanelRoot(Fields[Index]));
    const FIntPoint Delta(R.Width()+24>Size.X?0:(R.Min.X+R.Max.X>Size.X?-12:12),
        R.Height()+24>Size.Y?0:(R.Min.Y+R.Max.Y>Size.Y?-12:12));
    Native.RootOffsets.Add(Root,Saved+Delta);Rebuild();
    const auto* Moved=Native.Find(Fields[Index]);const bool Aligned=Moved && Moved->Rect.Min==Before+Delta;
    Native.RootOffsets.Add(Root,Saved);Rebuild();
    return Aligned?true:Fail(TEXT("Panel contents did not follow root position"));
}
