#include "AC2GameUI.h"
#include "AC2Audio.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/SpringArmComponent.h"
#include "Framework/Application/SlateApplication.h"
namespace
{
struct FSetting {const TCHAR* Label;float Default,Min,Max,Step;bool Check;};
const FSetting Settings[]={
    {TEXT("Field of view"),90,60,110,5,false},
    {TEXT("Mouse sensitivity"),1,.25,3,.25,false},
    {TEXT("Invert mouse"),0,0,1,1,true},
    {TEXT("Camera distance"),380,200,800,50,false},
    {TEXT("Vertical sync"),0,0,1,1,true},
    {TEXT("Fullscreen"),1,0,1,1,true},
    {TEXT("Frame rate limit"),60,30,240,30,false},
    {TEXT("Shadow quality"),2,0,3,1,false},
    {TEXT("Effects quality"),2,0,3,1,false},
    {TEXT("View distance quality"),2,0,3,1,false},
    {TEXT("Health numbers"),1,0,1,1,true},
    {TEXT("Vigor numbers"),1,0,1,1,true},
    {TEXT("Chat font size"),14,12,18,2,false},
    {TEXT("Sound volume"),1,0,1,.1f,false},
    {TEXT("Music volume"),1,0,1,.1f,false},
    {TEXT("Player music volume"),1,0,1,.1f,false},
    {TEXT("Ambient volume"),1,0,1,.1f,false},
    {TEXT("Blood effects"),4,1,4,1,false},
    {TEXT("Green blood"),0,0,1,1,true},
    {TEXT("Damage numbers"),1,0,1,1,true},
    {TEXT("Third-party damage"),0,0,1,1,true},
    {TEXT("Third-party damage range"),1,0,1,.1f,false},
    {TEXT("Object names"),1,0,1,1,true},
    {TEXT("Radar monsters"),1,0,1,1,true},
    {TEXT("Radar NPCs"),1,0,1,1,true},
    {TEXT("Radar players"),1,0,1,1,true},
    {TEXT("Radar portals"),1,0,1,1,true},
    {TEXT("Radar lifestones"),1,0,1,1,true},
    {TEXT("Radar items"),0,0,1,1,true},
    {TEXT("Radar coordinates"),1,0,1,1,true},
    {TEXT("Lock shortcuts"),1,0,1,1,true},
    {TEXT("Instant shortcut scrolling"),1,0,1,1,true},
    {TEXT("Shortcut numbers"),1,0,1,1,true},
    {TEXT("Shortcut bar height"),1,1,5,1,false},
    {TEXT("Auto target"),1,0,1,1,true}
};
// Native GameplayOptionsProfile bits (gmCharacterUI / ggmCWInterface).
struct FGameOptionBit {int32 Row;uint32 Bit;};
const FGameOptionBit GameOptionBits[]={{32,1},{19,4},{20,8},{29,0x10},{30,0x800},{31,0x2000},{34,0x20}};
bool IsServerOption(int32 Row)
{
    if(Row==21 || Row==33 || (Row>=23 && Row<=28))return true;
    for(const auto& B:GameOptionBits)if(Row==B.Row)return true;
    return false;
}
uint32 T(const TCHAR* V){return FAC2UILayout::Token(V);}
TArray<int32> OptionRows(int32 Tab)
{
    if(Tab==1)return {0,1,2,3,4,5,6,7,8,9,13,14,15,16,17,18};
    if(Tab==2)return {10,11,34,19,20,21,22,23,24,25,26,27,28,29,32,30,31,33};
    if(Tab==3)return {12};
    return {};
}
uint32 Attribute(const FAC2UIElement* E,const TCHAR* Name)
{
    if(E)for(const auto& S:E->States)if(S.Id==T(TEXT("stateless")))return S.Attributes.FindRef(T(Name));
    return 0;
}
}
bool SAC2GameUI::ServerOptionsActive() const
{
    const auto* Net=Mode->Network.Get();const auto* P=Mode->Characters.Active();
    return !Frontend && Net && P && Net->IsWorld() && Net->PlayerId==P->Id && Net->GameOptionsRevision &&
        LoadedGameOptionsSession==Net->ShortcutSession && GameOptionsOwner==Net->ShortcutScope+TEXT(".")+P->Id;
}
void SAC2GameUI::SyncGameOptions()
{
    if(TestingOptions || TestingShortcuts || Frontend)return;
    const auto* Net=Mode->Network.Get();const auto* P=Mode->Characters.Active();
    if(!Net || !P || !Net->IsWorld() || Net->PlayerId!=P->Id || !Net->GameOptionsRevision)return;
    const FString Owner=Net->ShortcutScope+TEXT(".")+P->Id;
    const bool Fresh=LoadedGameOptionsSession!=Net->ShortcutSession || GameOptionsOwner!=Owner;
    if(!Fresh && LoadedGameOptionsRevision==Net->GameOptionsRevision)return;
    if(OptionValues.IsEmpty())RestoreOptions();
    auto Adopt=[&](int32 Row,float Value){
        // A server refresh must not commit or discard an unrelated UI draft.
        if(Fresh || OptionValues[Row]==AppliedOptions[Row])OptionValues[Row]=Value;
        AppliedOptions[Row]=Value;
    };
    if(Fresh)for(int32 I=0;I<UE_ARRAY_COUNT(Settings);++I)if(IsServerOption(I)){
        float V=I==33?float(ShortcutRows):Settings[I].Default;
        if(I!=33)GConfig->GetFloat(TEXT("AC2.Options"),Settings[I].Label,V,GGameUserSettingsIni);
        Adopt(I,FMath::IsFinite(V)?FMath::Clamp(V,Settings[I].Min,Settings[I].Max):Settings[I].Default);
    }
    const auto& Prefs=Net->ServerGameOptions;
    if(Prefs.Fields & 0x80)for(const auto& B:GameOptionBits)Adopt(B.Row,(Prefs.Bits & B.Bit)?1.f:0.f);
    if(Prefs.Fields & 0x20)for(int32 I=0;I<6;++I)Adopt(23+I,(Prefs.Radar & (1u<<I))?1.f:0.f);
    if(Prefs.Fields & 8)Adopt(21,Prefs.Range<0?Settings[21].Default:FMath::Clamp(Prefs.Range,0.f,1.f));
    if(Prefs.Fields & 0x1000){ShortcutRows=int32(Prefs.Height);Adopt(33,float(ShortcutRows));}
    GameOptionsOwner=Owner;LoadedGameOptionsSession=Net->ShortcutSession;LoadedGameOptionsRevision=Net->GameOptionsRevision;
    SyncedGameOptions=AppliedOptions;SyncedShortcutRows=ShortcutRows;
    RestoreOptions();
}
bool SAC2GameUI::SaveGameOptions()
{
    if(TestingOptions || TestingShortcuts || !ServerOptionsActive() || SyncedGameOptions.Num()!=AppliedOptions.Num())return true;
    bool Changed=ShortcutRows!=SyncedShortcutRows;
    for(int32 I=0;I<AppliedOptions.Num();++I)if(IsServerOption(I) && AppliedOptions[I]!=SyncedGameOptions[I])Changed=true;
    if(!Changed)return true;
    auto* Net=Mode->Network.Get();auto Next=Net->ServerGameOptions;
    for(const auto& B:GameOptionBits)if(!(Next.Fields & 0x80) || AppliedOptions[B.Row]!=SyncedGameOptions[B.Row])
        Next.Bits=AppliedOptions[B.Row]>.5?Next.Bits|B.Bit:Next.Bits&~B.Bit;
    for(int32 I=0;I<6;++I)if(!(Next.Fields & 0x20) || AppliedOptions[23+I]!=SyncedGameOptions[23+I])
        Next.Radar=AppliedOptions[23+I]>.5?Next.Radar|(1u<<I):Next.Radar&~(1u<<I);
    // Retain a native unset sentinel or a range outside this slider's domain
    // unless the user actually changes the range. Do not normalize on resize.
    if(!(Next.Fields & 8) || AppliedOptions[21]!=SyncedGameOptions[21])Next.Range=AppliedOptions[21];
    if(!(Next.Fields & 0x1000) || ShortcutRows!=SyncedShortcutRows)Next.Height=uint32(ShortcutRows);
    Next.Fields=FAC2GameOptions::All;
    if(!Net->SaveGameOptions(Next)){Mode->Notice=TEXT("Game options could not be saved to the server.");return false;}
    SyncedGameOptions=AppliedOptions;SyncedShortcutRows=ShortcutRows;return true;
}
void SAC2GameUI::ToggleOptions()
{
    if(OptionConfirmationOpen())return;
    if(ShowOptions){CloseOptions();return;}
    EndChat();ShowOptions=true;OptionsTab=0;OptionsScroll=0;
    if(ShowOptions)RaiseWindow(FAC2UILayout::Token(TEXT("OptionField")));
    if(OptionValues.IsEmpty())RestoreOptions();
    // Another left-side panel may have temporarily covered options. Keep its
    // draft until Apply, Revert or an explicit close resolves it.
    if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))PC->SetCursor(!PC->WantsMouseLook()||ShowOptions||ShowInventory);
    Rebuild();
}
void SAC2GameUI::RefreshShortcutHeightOption(bool Force)
{
    if(OptionValues.IsEmpty())RestoreOptions();
    if(Force || OptionValues[33]==AppliedOptions[33])OptionValues[33]=float(ShortcutRows);
    AppliedOptions[33]=float(ShortcutRows);
}
void SAC2GameUI::RestoreOptions()
{
    if(OptionValues.IsEmpty())
    {
        for(int32 I=0;I<UE_ARRAY_COUNT(Settings);++I)
        {
            float V=Settings[I].Default;
            if(I!=33)GConfig->GetFloat(TEXT("AC2.Options"),Settings[I].Label,V,GGameUserSettingsIni);
            if(I==5 && GEngine && GEngine->GameViewport){
                const auto Window=GEngine->GameViewport->GetWindow();
                if(Window)V=Window->GetWindowMode()==EWindowMode::Windowed?0.f:1.f;
            }
            if(!FMath::IsFinite(V))V=Settings[I].Default;
            OptionValues.Add(FMath::Clamp(V,Settings[I].Min,Settings[I].Max));
        }
        AppliedOptions=OptionValues;
    }
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr)
    {P->Camera->SetFieldOfView(AppliedOptions[0]);P->MouseSensitivity=AppliedOptions[1];P->InvertMouse=AppliedOptions[2]>.5;P->SetCameraDistance(AppliedOptions[3]);}
    HealthNumbers=AppliedOptions[10]>.5;VigorNumbers=AppliedOptions[11]>.5;
    DamageNumbers=AppliedOptions[19]>.5;ThirdPartyDamage=AppliedOptions[20]>.5;
    ThirdPartyDamageRange=AppliedOptions[21];ShowObjectNames=AppliedOptions[22]>.5;
    uint32 RadarMask=0;for(int32 I=0;I<6;++I)if(AppliedOptions[23+I]>.5)RadarMask|=1u<<I;
    MapUI.SetRadarOptions(RadarMask,AppliedOptions[29]>.5);
    ShortcutLocked=AppliedOptions[30]>.5;
    ShortcutInstantScroll=AppliedOptions[31]>.5;
    ShowShortcutNumbers=AppliedOptions[32]>.5;
    if(Mode->Network)Mode->Network->AutoTarget=AppliedOptions[34]>.5;
    if(ShortcutInstantScroll){ShortcutScroll.Snap(ShortcutSet*ShortcutRowHeight);ShortcutTopRow=ShortcutSet;}
    ChatFont=Fonts.Body(AppliedOptions[12]);if(Entry)Entry->SetFont(ChatFont);
    if(auto* Audio=FAC2Audio::Get(Mode->GetWorld()))for(uint32 I=0;I<4;++I)Audio->SetVolume(I,AppliedOptions[13+I]);
    FAC2AssetLibrary::SetEffectChannel(0x40000001,uint32(AppliedOptions[17]));
    FAC2AssetLibrary::SetEffectChannel(0x40000007,AppliedOptions[18]>.5?1:2);
}
void SAC2GameUI::SetRadarCoordinates(bool Show)
{
    SyncGameOptions();
    if(OptionValues.IsEmpty())RestoreOptions();
    // The radar button is immediate. Keep unrelated pending option edits intact.
    OptionValues[29]=AppliedOptions[29]=Show?1.f:0.f;
    MapUI.SetRadarOptions(MapUI.GetRadarMask(),Show);
    if(!TestingOptions){
        if(!ServerOptionsActive())GConfig->SetFloat(TEXT("AC2.Options"),Settings[29].Label,AppliedOptions[29],GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
        SaveGameOptions();
    }
}
void SAC2GameUI::SetShortcutNumbers(bool Show)
{
    SyncGameOptions();
    if(OptionValues.IsEmpty())RestoreOptions();
    // The shell command changes this option immediately; unrelated drafts,
    // bindings and an in-flight page scroll must remain untouched.
    OptionValues[32]=AppliedOptions[32]=Show?1.f:0.f;
    ShowShortcutNumbers=Show;
    if(!TestingOptions){
        if(!ServerOptionsActive())GConfig->SetFloat(TEXT("AC2.Options"),Settings[32].Label,AppliedOptions[32],GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
        SaveGameOptions();
    }
}
void SAC2GameUI::BuildOptions()
{
    OptionsList=OptionsBar=OptionsThumb=FIntRect();OptionsMaxScroll=0;
    Bind(TEXT("OptionCloseButton"),TEXT("Options"));
    const TCHAR* Tabs[]={TEXT("CommandTab"),TEXT("ClientTab"),TEXT("CharacterTab"),TEXT("ChatTab")};
    for(int32 I=0;I<4;++I)
    {
        Bind(Tabs[I],*FString::Printf(TEXT("OptionsTab%d"),I));
    }
    const auto* Panel=Native.Find(0xf30bc740);
    if(!Panel)return;
    const auto& Layout=Native.Layouts.FindChecked(0x22000001);
    const TCHAR* PageName=OptionsTab==0?TEXT("CommandPage"):OptionsTab==1?TEXT("ClientPage"):OptionsTab==2?TEXT("CharacterPage"):TEXT("ChatPage");
    const auto* PageDesc=Layout.Find(PageName);if(!PageDesc)return;
    const FIntRect Page(Panel->Rect.Min+PageDesc->Rect.Min,Panel->Rect.Min+PageDesc->Rect.Max+FIntPoint(1,1));
    const TCHAR* ListName=OptionsTab==1?TEXT("ClientList"):OptionsTab==2?TEXT("CharacterList"):TEXT("ChatOptionsList");
    const auto Rows=OptionRows(OptionsTab);const auto* ListDesc=Layout.Find(ListName);
    const auto* RowDesc=Layout.Find(TEXT("OptionSlider"));
    OptionRowHeight=RowDesc?RowDesc->Rect.Height()+1:33;
    OptionsMaxScroll=OptionsTab && ListDesc?FMath::Max(0,Rows.Num()*OptionRowHeight-(ListDesc->Rect.Height()+1)):0;
    OptionsScroll=FMath::Clamp(OptionsScroll,0,OptionsMaxScroll);
    const uint32 BarId=OptionsTab?Attribute(ListDesc,TEXT("vertical_scrollbar")):0;
    const auto* BarDesc=Layout.Controls.FindByPredicate([&](const auto& E){return E.Id==BarId;});
    const uint32 Up=Attribute(BarDesc,TEXT("up_button")),Down=Attribute(BarDesc,TEXT("down_button"));
    Native.Positions.Add(BarId,OptionsMaxScroll?float(OptionsScroll)/OptionsMaxScroll:0);
    Native.States.Add(Up,T(OptionsScroll>0?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(Down,T(OptionsScroll<OptionsMaxScroll?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("ApplyButton")),T(OptionsChanged(OptionsTab)?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("RecallButton")),T(OptionsChanged(OptionsTab)?TEXT("normal"):TEXT("ghosted")));
    Native.Template(0x22000001,PageName,Page,Page);
    if(OptionsTab==0)
    {
        if (const auto* Help = Native.Find(TEXT("AssistanceButton")))
        {
            // Add a fourth command using the DAT button and caption templates.
            const auto* Leave = Native.Find(TEXT("LeaveButton"));
            const auto* Quit = Native.Find(TEXT("QuitButton"));
            if (Leave && Quit)
            {
                const FIntRect Rect = Help->Rect + FIntPoint(0, Quit->Rect.Min.Y - Leave->Rect.Min.Y);
                constexpr uint32 ButtonTemplate = 0x7e781723, CaptionTemplate = 0x2f6e2a3f;
                const uint32 Id = T(TEXT("KeyBindingsButton"));
                TGuardValue<uint32> Hover(Native.Hovered, Native.Hovered == Id ? ButtonTemplate : Native.Hovered);
                TGuardValue<uint32> Press(Native.Pressed, Native.Pressed == Id ? ButtonTemplate : Native.Pressed);
                const int32 Start = Native.Nodes.Num();
                if (Native.Template(0x22000001, ButtonTemplate, Rect, Page) && Native.Nodes.IsValidIndex(Start))
                {
                    Native.Nodes[Start].Id = Id;
                    const int32 Caption = Native.Nodes.Num();
                    if (Native.Template(0x22000001, CaptionTemplate, Rect, Page, Id) && Native.Nodes.IsValidIndex(Caption))
                        Native.Nodes[Caption].Text = TEXT("Key Bindings");
                    Bind(TEXT("KeyBindingsButton"), TEXT("OptionsKeys"));
                }
            }
        }
        Bind(TEXT("LeaveButton"),TEXT("Logout"));Bind(TEXT("QuitButton"),TEXT("Quit"));Bind(TEXT("AssistanceButton"),TEXT("Help"));
        return;
    }
    Bind(TEXT("ApplyButton"),TEXT("OptionsApply"));Bind(TEXT("RecallButton"),TEXT("OptionsRecall"));Bind(TEXT("DefaultButton"),TEXT("OptionsDefault"));
    const FIntRect List=OptionsList=Native.Bounds(ListName);
    UpdateScrollbar(BarId,OptionsScroll,OptionsMaxScroll,OptionsBar,OptionsThumb,TEXT("OptionsScrollUp"),TEXT("OptionsScrollDown"));
    for(int32 R=0;R<Rows.Num();++R)
    {
        const FIntRect Row(List.Min+FIntPoint(0,R*OptionRowHeight-OptionsScroll),List.Min+FIntPoint(List.Width(),(R+1)*OptionRowHeight-OptionsScroll));
        if(Row.Max.Y<=List.Min.Y || Row.Min.Y>=List.Max.Y)continue;
        const int32 I=Rows[R];
        const auto& S=Settings[I];
        Native.Text.Add(T(TEXT("OptionText")),S.Check?FString(S.Label):FString::Printf(TEXT("%s: %g"),S.Label,OptionValues[I]));
        if(I>=23 && I<=29){
            const TCHAR* Keys[]={TEXT("ID_Option_RadarShowMonsters"),TEXT("ID_Option_RadarShowNPCs"),TEXT("ID_Option_RadarShowPCs"),
                TEXT("ID_Option_RadarShowPortals"),TEXT("ID_Option_RadarShowLifestones"),TEXT("ID_Option_RadarShowItems"),TEXT("ID_Option_RadarShowCoordinates")};
            const FString Label=Native.Localized(Keys[I-23]);
            if(!Label.IsEmpty())Native.Text.Add(T(TEXT("OptionText")),Label);
        }
        if(I==30)Native.Text.Add(T(TEXT("OptionText")),Native.Localized(TEXT("ID_Option_ShortcutLocked")));
        if(I==31)Native.Text.Add(T(TEXT("OptionText")),Native.Localized(TEXT("ID_Option_ShortcutInstaScroll")));
        if(I==32)Native.Text.Add(T(TEXT("OptionText")),Native.Localized(TEXT("ID_Option_ShortcutNumbersText")));
        if(I==34)Native.Text.Add(T(TEXT("OptionText")),Native.Localized(TEXT("ID_Option_AutoTargetText")));
        if(I==33){
            Native.Text.Add(T(TEXT("OptionText")),Native.Localized(TEXT("ID_Option_ShortcutHeight")));
            const auto* Menu=Layout.Find(TEXT("DropDownMenu"));
            Native.Text.Add(Attribute(Menu,TEXT("selection_display")),ShortcutHeightLabel(int32(OptionValues[I])));
            Native.States.Add(T(TEXT("DropDownMenu")),T(OpenOptionChoice==33?TEXT("open"):TEXT("closed")));
        }
        if(I==17){const TCHAR* Names[]={TEXT("Fun"),TEXT("None"),TEXT("Low"),TEXT("Normal")};
            Native.Text.Add(T(TEXT("OptionText")),FString::Printf(TEXT("Blood effects: %s"),Names[FMath::Clamp(int32(OptionValues[I])-1,0,3)]));}
        Native.States.Add(T(TEXT("CheckboxButton")),T(OptionValues[I]>.5?TEXT("highlight"):TEXT("normal")));
        Native.Positions.Add(T(TEXT("SliderBar")),(OptionValues[I]-S.Min)/(S.Max-S.Min));
        int32 Before=Native.Nodes.Num();
        const uint32 SavedHover=Native.Hovered,SavedPress=Native.Pressed;
        Native.Hovered=Native.Pressed=0;
        // These controls share a template ID. Feedback belongs to the hovered
        // row and action, never every visible copy of SliderRight/CheckboxButton.
        for(const TCHAR* Control:{TEXT("CheckboxButton"),TEXT("SliderLeft"),TEXT("SliderRight")}){
            const auto* D=Layout.Find(Control);if(!D)continue;
            const bool Minus=FCString::Strcmp(Control,TEXT("SliderLeft"))==0;
            const FName Action(*FString::Printf(TEXT("%s%d"),Minus?TEXT("OptionMinus"):TEXT("OptionPlus"),I));
            if(PressedAction==Action)Native.Pressed=T(Control);
            // The previous frame's matching action has a clipped hit rectangle.
            // OnMouseMove already resolves the shared native ID; restrict it by row.
            if(SavedHover==T(Control) && Row.Contains(FIntPoint(Mouse.X,Mouse.Y)))Native.Hovered=T(Control);
        }
        Native.Template(0x22000001,I==33?TEXT("OptionDropDown"):S.Check?TEXT("OptionCheckbox"):TEXT("OptionSlider"),Row,List);
        Native.Hovered=SavedHover;Native.Pressed=SavedPress;
        for(int32 J=Before;J<Native.Nodes.Num();++J)
        {
            const auto& N=Native.Nodes[J];
            if(N.Clip.IsEmpty())continue;
            if(I==33 && N.Id==T(TEXT("DropDownMenu"))){
                OptionChoiceAnchor=N.Clip;
                const FName Key(TEXT("OptionChoice33"));Buttons.Add(Key,FSlateRect(N.Clip.Min.X,N.Clip.Min.Y,N.Clip.Max.X,N.Clip.Max.Y));ButtonActions.Add(Key,Key);
                Hints.Add(Key,Native.Localized(TEXT("ID_Option_ShortcutHeight_Help")));
            }
            if(N.Id==T(TEXT("SliderBar")))OptionSliders.Add(I,FSlateRect(N.Clip.Min.X,N.Clip.Min.Y,N.Clip.Max.X,N.Clip.Max.Y));
            if(N.Id==T(TEXT("CheckboxButton"))||N.Id==T(TEXT("SliderRight"))||N.Id==T(TEXT("SliderLeft")))
            {
                FName Action(*(FString(N.Id==T(TEXT("SliderLeft"))?TEXT("OptionMinus"):TEXT("OptionPlus"))+FString::FromInt(I)));
                Buttons.Add(Action,FSlateRect(N.Clip.Min.X,N.Clip.Min.Y,N.Clip.Max.X,N.Clip.Max.Y));ButtonActions.Add(Action,Action);
                if(I==30)Hints.Add(Action,TEXT("Hold Shift while dragging to move or remove a locked shortcut. New shortcuts can still be added."));
                if(I==31)Hints.Add(Action,TEXT("When off, the hotbar scrolls smoothly between pages."));
                if(I==32)Hints.Add(Action,Native.Localized(TEXT("ID_Option_ShortcutNumbersText_Help")));
                if(I==34)Hints.Add(Action,Native.Localized(TEXT("ID_Option_AutoTargetText_Help")));
            }
        }
    }
    if(const auto* Menu=Layout.Find(TEXT("DropDownMenu")))Native.Text.Remove(Attribute(Menu,TEXT("selection_display")));
    Native.States.Remove(T(TEXT("DropDownMenu")));
    for(const TCHAR* Name:{TEXT("OptionText"),TEXT("CheckboxButton")}){Native.Text.Remove(T(Name));Native.States.Remove(T(Name));}
}
void SAC2GameUI::ScrollOptions(int32 Pixels)
{
    if(!ShowOptions || OptionConfirmationOpen())return;
    CloseOptionChoice();
    OptionsScroll=FMath::Clamp(OptionsScroll+Pixels,0,OptionsMaxScroll);Rebuild();
}
void SAC2GameUI::DragOptionsScroll(float Y)
{
    const int32 Travel=OptionsBar.Height()-OptionsThumb.Height();
    if(Travel>0)OptionsScroll=FMath::RoundToInt(FMath::Clamp((Y-OptionsGrab-OptionsBar.Min.Y)/Travel,0.f,1.f)*OptionsMaxScroll);
    Rebuild();
}
void SAC2GameUI::DragOption(float X)
{
    const auto* R=OptionSliders.Find(DraggingOption);
    if(!ShowOptions||!R||!OptionValues.IsValidIndex(DraggingOption))return;
    const auto& S=Settings[DraggingOption];
    const float Fraction=FMath::Clamp((X-R->Left)/FMath::Max(1.f,R->Right-R->Left),0.f,1.f);
    OptionValues[DraggingOption]=FMath::Clamp(S.Min+FMath::RoundToFloat(Fraction*(S.Max-S.Min)/S.Step)*S.Step,S.Min,S.Max);
    Rebuild();
}
bool SAC2GameUI::OptionsChanged(int32 Tab) const
{
    if(OptionValues.Num()!=UE_ARRAY_COUNT(Settings) || AppliedOptions.Num()!=OptionValues.Num())return false;
    for(int32 I:OptionRows(Tab))if(!FMath::IsNearlyEqual(OptionValues[I],AppliedOptions[I]))return true;
    return false;
}
void SAC2GameUI::ApplyOptions(int32 Tab)
{
    SyncGameOptions();
    if(OptionValues.Num()!=UE_ARRAY_COUNT(Settings))return;
    for(int32 I:OptionRows(Tab)){
        AppliedOptions[I]=OptionValues[I];
        if(I!=33 && !TestingOptions && (!IsServerOption(I) || !ServerOptionsActive()))GConfig->SetFloat(TEXT("AC2.Options"),Settings[I].Label,OptionValues[I],GGameUserSettingsIni);
    }
    if(Tab==2)ShortcutRows=FAC2ShortcutGrid::Rows(FMath::RoundToInt(AppliedOptions[33]));
    if(Tab==1 && !TestingOptions)if(auto* U=GEngine->GetGameUserSettings())
    {
        U->SetVSyncEnabled(OptionValues[4]>.5);U->SetFullscreenMode(OptionValues[5]>.5?EWindowMode::WindowedFullscreen:EWindowMode::Windowed);
        U->SetFrameRateLimit(OptionValues[6]);U->SetShadowQuality(OptionValues[7]);U->SetVisualEffectQuality(OptionValues[8]);U->SetViewDistanceQuality(OptionValues[9]);
        U->ApplySettings(false);
    }
    RestoreOptions(); // Runtime values always come from applied tabs, not other tabs' drafts.
    if(!TestingOptions)GConfig->Flush(false,GGameUserSettingsIni);
    Mode->Notice=TEXT("Options applied");
    if(Tab==2){SaveGameOptions();if(!TestingOptions)SaveShortcuts();}
    Rebuild();
}
void SAC2GameUI::CloseOptions()
{
    CloseOptionChoice();
    if(OptionsChanged(1)||OptionsChanged(2)||OptionsChanged(3)){
        AskOptionConfirmation(EOptionConfirmation::Close);return;
    }
    ShowOptions=false;DraggingOption=INDEX_NONE;DraggingOptionsScroll=false;
    Rebuild();
    if(Frontend)OnClose.ExecuteIfBound();
}
void SAC2GameUI::AskOptionConfirmation(EOptionConfirmation Question)
{
    if(OptionConfirmationOpen())return;
    CloseOptionChoice();
    EndChat();CancelCameraGesture();
    DraggingOption=INDEX_NONE;DraggingOptionsScroll=false;
    DragWindow=ResizeWindow=0;PressedAction=NAME_None;Native.Pressed=0;
    auto& App=FSlateApplication::Get();OptionPreviousFocus=App.GetKeyboardFocusedWidget();
    OptionConfirmation=Question;OptionConfirmationTab=OptionsTab;
    if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController())){
        PC->FlushPressedKeys();PC->SetCursor(true);
        FInputModeUIOnly Input;Input.SetWidgetToFocus(AsShared());
        Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Input);
    }
    Rebuild();App.SetKeyboardFocus(AsShared());
}
void SAC2GameUI::AnswerOptionConfirmation(bool Yes)
{
    if(!OptionConfirmationOpen())return;
    const auto Question=OptionConfirmation;const int32 Tab=OptionConfirmationTab;
    OptionConfirmation=EOptionConfirmation::None;PressedAction=NAME_None;Native.Pressed=0;
    // gmOptionUI::DoConfirm applies or recalls every page when closing;
    // each page's own buttons act only on that page. Defaults are applied.
    if(Question==EOptionConfirmation::Close){
        if(Yes)for(int32 Page=1;Page<=3;++Page)ApplyOptions(Page);
        else OptionValues=AppliedOptions;
    }else if(Yes){
        if(Question==EOptionConfirmation::Recall)for(int32 I:OptionRows(Tab))OptionValues[I]=AppliedOptions[I];
        if(Question==EOptionConfirmation::Default)for(int32 I:OptionRows(Tab))OptionValues[I]=Settings[I].Default;
        if(Question==EOptionConfirmation::Apply || Question==EOptionConfirmation::Default)ApplyOptions(Tab);
    }
    if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController())){PC->FlushPressedKeys();PC->SetCursor(true);}
    auto& App=FSlateApplication::Get();
    const auto Focus=OptionPreviousFocus.Pin();OptionPreviousFocus.Reset();
    if(Focus && Focus.Get()!=this && Focus->GetVisibility().IsVisible())App.SetKeyboardFocus(Focus);
    else App.SetAllUserFocusToGameViewport();
    if(Question==EOptionConfirmation::Close)CloseOptions();else Rebuild();
}
void SAC2GameUI::BuildOptionConfirmation()
{
    if(!OptionConfirmationOpen())return;
    OptionDialogNodesStart=Native.Nodes.Num();
    const TCHAR* Key=OptionConfirmation==EOptionConfirmation::Recall?TEXT("ID_Option_RecallText"):
        OptionConfirmation==EOptionConfirmation::Default?TEXT("ID_Option_DefaultText"):TEXT("ID_Option_ApplyText");
    Native.Text.Add(T(TEXT("QuestionText")),Native.Localized(Key));
    Native.Root(0x22000005,TEXT("ConfirmationField"));
    Native.Text.Remove(T(TEXT("QuestionText")));
    Buttons.Reset();ButtonActions.Reset();Hints.Reset();
    Bind(TEXT("YesButton"),TEXT("OptionsYes"));Bind(TEXT("NoButton"),TEXT("OptionsNo"));
}
// Kept with the native page so settings bounds and actions cannot drift apart.
bool SAC2GameUI::OptionAction(FName Name)
{
    if(OptionConfirmationOpen()){
        if(Name==TEXT("OptionsYes") || Name==TEXT("OptionsNo"))AnswerOptionConfirmation(Name==TEXT("OptionsYes"));
        return true;
    }
    const FString Key=Name.ToString();
    if(Name==TEXT("Options")){ToggleOptions();return true;}
    if(Name==TEXT("OptionsKeys")){OpenKeyBindings();return true;}
    if(Key.StartsWith(TEXT("OptionsTab"))){CloseOptionChoice();DraggingOption=INDEX_NONE;DraggingOptionsScroll=false;OptionsTab=FMath::Clamp(FCString::Atoi(*Key.Mid(10)),0,3);OptionsScroll=0;Rebuild();return true;}
    if(Name==TEXT("OptionsScrollUp") || Name==TEXT("OptionsScrollDown")){ScrollOptions(Name==TEXT("OptionsScrollUp")?-OptionRowHeight:OptionRowHeight);return true;}
    if(Name==TEXT("OptionsApply")){if(OptionsChanged(OptionsTab)){if(OptionsTab==3)AskOptionConfirmation(EOptionConfirmation::Apply);else ApplyOptions(OptionsTab);}return true;}
    if(Name==TEXT("OptionsRecall")){if(OptionsChanged(OptionsTab))AskOptionConfirmation(EOptionConfirmation::Recall);return true;}
    if(Name==TEXT("OptionsDefault")){AskOptionConfirmation(EOptionConfirmation::Default);return true;}
    if(Name==TEXT("Logout")){if(Frontend){Mode->SignOut();return true;}OptionValues=AppliedOptions;ShowOptions=false;ClosePanels();if(Mode->Network)Mode->Network->Logout();else{Mode->LeaveWorld();Mode->Offline();}return true;}
    if(Name==TEXT("Quit")){if(Mode->Network)Mode->Network->Logout();FPlatformMisc::RequestExit(false);return true;}
    const bool Plus=Key.StartsWith(TEXT("OptionPlus")),Minus=Key.StartsWith(TEXT("OptionMinus"));
    if(Plus||Minus)
    {
        const int32 I=FCString::Atoi(*Key.Mid(Plus?10:11));
        if(OptionValues.IsValidIndex(I) && OptionRows(OptionsTab).Contains(I)){const auto& S=Settings[I];OptionValues[I]=S.Check?1-OptionValues[I]:FMath::Clamp(OptionValues[I]+(Plus?S.Step:-S.Step),S.Min,S.Max);Rebuild();}
        return true;
    }
    return false;
}
bool SAC2GameUI::SmokeShortcutNumbers(int32 Step,FString& Error)
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(Step==0){
        TestingOptions=TestingShortcuts=true;
        RestoreOptions();
        ShowOptions=true;OptionsTab=2;OptionValues[32]=AppliedOptions[32]=1;
        RestoreOptions();ShortcutRows=2;ShortcutSet=0;Rebuild();ScrollOptions(MAX_int32/2);
        if(!Buttons.Contains(TEXT("OptionPlus32")) || !ShowShortcutNumbers || !Native.Find(TEXT("SetNumberDisplay")))
            return Fail(TEXT("Shortcut numbers option or page label is missing"));
    }else if(Step==1){
        auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));
        if(!Window)return Fail(TEXT("No native window for shortcut options"));
        auto Click=[&](FName Key){const auto* R=Buttons.Find(Key);if(!R)return false;
            const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
            const auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
            App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
        };
        if(!Click(TEXT("OptionPlus32")) || !ShowShortcutNumbers || !Click(TEXT("ApplyButton")) || ShowShortcutNumbers)
            return Fail(TEXT("Shortcut numbers checkbox did not respect draft and Apply"));
    }else{
        ShowOptions=false;ShortcutRows=1;OptionValues.Reset();RestoreOptions();TestingOptions=false;Rebuild();return true;
    }
    const bool Visible=Step==0;
    const auto* PageLabel=Native.Find(TEXT("SetNumberDisplay"));
    if(Native.MissingImages || !PageLabel || PageLabel->Text.IsEmpty()==Visible ||
        Native.Nodes.ContainsByPredicate([](const auto& N){return N.Id==T(TEXT("NumberDisplay")) && !N.Text.IsEmpty();})!=Visible)
        return Fail(TEXT("Shortcut labels did not follow the applied option"));
    return true;
}
bool SAC2GameUI::SmokeOptions(int32 Step,FString& Error)
{
    if(Step>=3){
        auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
        auto& App=FSlateApplication::Get();const auto Window=App.FindWidgetWindow(SharedThis(this));
        if(!Window)return Fail(TEXT("Options smoke requires a Slate window"));
        auto Click=[&](FName Key){const auto* R=Buttons.Find(Key);if(!R)return false;
            const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
            const auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
            App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;};
        if(Step==3){
            ClosePanels();TestingOptions=true;ToggleOptions();OptionAction(TEXT("OptionsTab1"));
            if(!OptionSliders.Contains(0) || OptionsMaxScroll<=0 || Buttons.Contains(TEXT("ApplyButton")))return Fail(TEXT("Native options initial layout/state failed"));
        }
        if(Step==4){
            const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D(OptionsList.Min+FIntPoint(20,20))*Scale);
            OnMouseWheel(GetCachedGeometry(),FPointerEvent(0,P,P,{},EKeys::Invalid,-30,FModifierKeysState()));
            if(OptionsScroll!=OptionsMaxScroll || OptionsThumb.Max.Y!=OptionsBar.Max.Y || !Buttons.Contains(TEXT("OptionPlus18")))return Fail(TEXT("Options wheel did not reach last native row and scrollbar endpoint"));
        }
        if(Step==5){
            ScrollOptions(-OptionsMaxScroll);OptionAction(OptionValues[0]<110?TEXT("OptionPlus0"):TEXT("OptionMinus0"));
            if(!Click(TEXT("RecallButton")) || !OptionConfirmationOpen())return Fail(TEXT("Recall did not open native confirmation"));
            const auto* Text=Native.Find(TEXT("QuestionText"));
            if(!Text || Text->Text.IsEmpty() || Buttons.Num()!=2 || Native.MissingImages)return Fail(TEXT("Native confirmation text/art/input incomplete"));
        }
        if(Step==6){
            if(!Click(TEXT("NoButton")) || OptionConfirmationOpen() || !OptionsChanged(1))return Fail(TEXT("No failed to retain options draft"));
            if(!Click(TEXT("DefaultButton")) || !OptionConfirmationOpen())return Fail(TEXT("Defaults did not confirm"));
        }
        if(Step==7){
            if(!Click(TEXT("YesButton")) || OptionsChanged(1) || AppliedOptions[0]!=90)return Fail(TEXT("Confirmed Defaults did not apply"));
            OptionAction(TEXT("OptionPlus0"));OptionAction(TEXT("OptionsTab2"));OptionAction(TEXT("OptionPlus10"));
            if(!Click(TEXT("OptionCloseButton")) || !OptionConfirmationOpen())return Fail(TEXT("Close did not confirm pending changes"));
        }
        if(Step==8){
            if(!Click(TEXT("NoButton")) || ShowOptions || OptionValues!=AppliedOptions)return Fail(TEXT("Close No failed to discard drafts"));
            OptionValues.Reset();RestoreOptions();TestingOptions=false;
        }
        if(Step==9){
            TestingOptions=true;ToggleOptions();OptionAction(TEXT("OptionsTab2"));
            for(int32 I=23;I<=29;++I)AppliedOptions[I]=OptionValues[I]=(I==28?0.f:1.f);
            RestoreOptions();Rebuild();ScrollOptions(6*OptionRowHeight);
            if(!MapUI.Contacts.ContainsByPredicate([](const auto& C){return C.Look.Category==AC2Radar::NPCs;}))
                return Fail(TEXT("Fixture NPCs did not receive native radar classification"));
            if(!Buttons.Contains(TEXT("OptionPlus29")) || Native.MissingImages)return Fail(TEXT("Radar options list is not reachable"));
        }
        if(Step==10){
            const auto* Contact=MapUI.Contacts.FindByPredicate([](const auto& C){return C.Look.Category==AC2Radar::NPCs;});
            if(!Contact || !Mode->Network)return Fail(TEXT("No NPC available for radar filtering test"));
            const FString Id=Contact->Id;Mode->Network->SelectTarget(Id);
            if(!Click(TEXT("OptionPlus24")) || !(MapUI.GetRadarMask()&AC2Radar::NPCs))return Fail(TEXT("Draft radar filter applied prematurely"));
            if(!Click(TEXT("ApplyButton")) || MapUI.Contacts.ContainsByPredicate([](const auto& C){return C.Look.Category==AC2Radar::NPCs;}))
                return Fail(TEXT("Applied NPC filter did not remove radar contacts and their hit targets"));
            if(Mode->Network->TargetId!=Id)return Fail(TEXT("Radar filter incorrectly cleared world target"));
        }
        if(Step==11){
            if(!Click(TEXT("OptionPlus24")) || !Click(TEXT("OptionPlus29")) || !Click(TEXT("ApplyButton")))return Fail(TEXT("Radar checkboxes cannot be applied through native controls"));
            if(MapUI.ShowCoordinates() || !MapUI.Contacts.ContainsByPredicate([](const auto& C){return C.Look.Category==AC2Radar::NPCs;}))
                return Fail(TEXT("Radar coordinate/filter settings were not reflected live"));
        }
        if(Step==12){
            ShowOptions=false;Rebuild();
            const auto R=MapUI.Native.Bounds(TEXT("CoordBoxButton"));
            const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D(R.Min+R.Size()/2)*Scale);
            const auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);
            if(!Path.IsValid())return Fail(TEXT("Radar coordinate button is not hittable"));
            App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
            if(!MapUI.ShowCoordinates() || AppliedOptions[29]!=1 || OptionValues[29]!=1)return Fail(TEXT("Radar button did not synchronize its option"));
            OptionValues.Reset();RestoreOptions();TestingOptions=false;Rebuild();
        }
        if(Step==13){
            TestingOptions=true;ToggleOptions();OptionAction(TEXT("OptionsTab2"));
            AppliedOptions[30]=OptionValues[30]=1;RestoreOptions();ScrollOptions(MAX_int32/2);
            if(!ShortcutLocked || !Buttons.Contains(TEXT("OptionPlus30")))return Fail(TEXT("Native shortcut lock option is not reachable"));
        }
        if(Step==14){
            if(!Click(TEXT("OptionPlus30")) || !ShortcutLocked)return Fail(TEXT("Shortcut lock draft applied prematurely"));
            if(!Click(TEXT("ApplyButton")) || ShortcutLocked)return Fail(TEXT("Native checkbox did not unlock shortcuts"));
        }
        if(Step==15){
            if(!Click(TEXT("DefaultButton")) || !Click(TEXT("YesButton")) || !ShortcutLocked)return Fail(TEXT("Native defaults did not relock shortcuts"));
            ShowOptions=false;OptionValues.Reset();RestoreOptions();TestingOptions=false;Rebuild();
        }
        if(Step==16){
            TestingOptions=TestingShortcuts=true;ToggleOptions();OptionAction(TEXT("OptionsTab2"));
            AppliedOptions[31]=OptionValues[31]=1;RestoreOptions();ScrollOptions(MAX_int32/2);
            ShortcutRows=2;ShortcutSet=8;Shortcuts[80]=Shortcuts[0];Shortcuts[90]=Shortcuts[1];Rebuild();
            if(!ShortcutInstantScroll || !Buttons.Contains(TEXT("OptionPlus31")))return Fail(TEXT("Native instant-scroll option is not reachable"));
        }
        if(Step==17){
            if(!Click(TEXT("OptionPlus31")) || !ShortcutInstantScroll || !Click(TEXT("ApplyButton")) || ShortcutInstantScroll)
                return Fail(TEXT("Native instant-scroll option did not respect draft and Apply"));
            ShowOptions=false;Rebuild();SelectShortcutSet(9);
            if(!ShortcutScroll.Moving(ShortcutTime) || ShortcutScroll.Duration!=.25)return Fail(TEXT("Smooth hotbar paging did not use native duration"));
            // Sample mid-transition; subsequent frames continue advancing normally.
            ShortcutScroll.Started-=.125;Rebuild();
            const auto* Hit=Buttons.Find(TEXT("ShortcutSlot0"));
            if(ShortcutTopRow!=8 || !Hit || Hit->Bottom-Hit->Top!=16 || ShortcutHitIndex(TEXT("ShortcutSlot19"))!=99)
                return Fail(TEXT("Animated hotbar rows or clipped hit targets differ from displayed bindings"));
        }
        if(Step==18){
            if(ShortcutTopRow!=9 || ShortcutScroll.Moving(ShortcutTime) || Buttons.Contains(TEXT("ShortcutSlot10")))
                return Fail(TEXT("Completed page transition retained outgoing rows or enabled padding"));
            if(!Click(TEXT("ShortcutUp")))return Fail(TEXT("Animated page arrow is not clickable"));
            ShortcutScroll.Started-=.125;Rebuild();const int32 Before=ShortcutScroll.View(ShortcutTime);
            if(!Click(TEXT("ShortcutDown")) || ShortcutScroll.View(ShortcutTime)!=Before)
                return Fail(TEXT("Reversing an animated page caused a view jump"));
        }
        if(Step==19){
            ToggleOptions();OptionAction(TEXT("OptionsTab2"));ScrollOptions(MAX_int32/2);
            if(!Click(TEXT("OptionPlus31")) || !Click(TEXT("ApplyButton")) || !ShortcutInstantScroll || ShortcutScroll.Moving(ShortcutTime))
                return Fail(TEXT("Reenabling instant scroll did not settle the hotbar"));
        }
        if(Step==20){
            ShowOptions=false;TestingShortcuts=false;ShortcutOwner.Reset();LoadShortcuts();
            OptionValues.Reset();RestoreOptions();TestingOptions=false;Rebuild();
        }
        if(Step==21){
            TestingOptions=TestingShortcuts=true;ToggleOptions();OptionAction(TEXT("OptionsTab2"));
            ShortcutRows=1;RefreshShortcutHeightOption(true);ScrollOptions(MAX_int32/2);
            if(!Click(TEXT("OptionChoice33")) || OpenOptionChoice!=33 || OptionChoiceItems.Num()!=5 || Native.MissingImages)
                return Fail(TEXT("Native hotbar height menu did not open with five choices"));
        }
        if(Step==22){
            if(OptionChoiceItems.Num()!=5)return Fail(TEXT("Native menu lost its choices"));
            const auto R=OptionChoiceItems[4];
            const auto P=GetCachedGeometry().LocalToAbsolute(FVector2D(R.Min+R.Size()/2)*Scale);
            const auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return Fail(TEXT("Menu choice is not hittable"));
            App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
            if(OpenOptionChoice!=INDEX_NONE || OptionValues[33]!=5 || ShortcutRows!=1 || !Buttons.Contains(TEXT("ApplyButton")))
                return Fail(TEXT("Height selection must remain a draft until Apply"));
        }
        if(Step==23){
            if(!Click(TEXT("ApplyButton")) || ShortcutRows!=5 || OptionsChanged(2) || Native.Bounds(TEXT("ShortcutField")).Height()!=172)
                return Fail(TEXT("Native height Apply did not resize the hotbar to five rows"));
        }
        if(Step==24){
            ShowOptions=false;TestingShortcuts=false;ShortcutOwner.Reset();LoadShortcuts();
            OptionValues.Reset();RestoreOptions();TestingOptions=false;Rebuild();
        }
        return true;
    }
    if(Step==0)
    {
        Rebuild();
        if(!Buttons.Contains(TEXT("LeaveButton"))||!Native.Find(T(TEXT("QuitButton")))||Native.MissingImages)
        {Error=TEXT("Native command page controls missing");return false;}
        if (!SmokeKeyBindingEditor(Error)) return false;
    }
    if(Step==1)
    {
        OptionAction(TEXT("OptionsTab1"));
        const auto* Slider=OptionSliders.Find(0);
        if(!Slider){Error=TEXT("Native FOV slider missing");return false;}
        DraggingOption=0;DragOption((Slider->Left+Slider->Right)*.5f);DraggingOption=INDEX_NONE;
        if(OptionValues[0]!=85){Error=TEXT("Native option slider interaction failed");return false;}
        OptionsScroll=OptionsMaxScroll;Rebuild();
        if(!OptionSliders.Contains(13)||!OptionSliders.Contains(14)||!OptionSliders.Contains(16))
        {Error=TEXT("Native sound, music and ambient volume controls missing");return false;}
    }
    if(Step==2)OptionAction(TEXT("Logout"));
    return true;
}
bool SAC2GameUI::OptionsClosePoint(FVector2D &Point) const
{
    const auto *R=Buttons.Find(TEXT("OptionCloseButton"));
    if(!R)return false;
    Point=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)/2,(R->Top+R->Bottom)/2)*Scale);
    return true;
}
