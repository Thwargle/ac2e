#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/SpringArmComponent.h"
namespace
{
struct FSetting {const TCHAR* Label;float Default,Min,Max,Step;bool Check;};
const FSetting Settings[]={
    {TEXT("Field of view"),90,60,110,5,false},
    {TEXT("Mouse sensitivity"),1,.25,3,.25,false},
    {TEXT("Invert mouse"),0,0,1,1,true},
    {TEXT("Camera distance"),380,200,800,50,false},
    {TEXT("Vertical sync"),0,0,1,1,true},
    {TEXT("Fullscreen"),0,0,1,1,true},
    {TEXT("Frame rate limit"),60,30,240,30,false},
    {TEXT("Shadow quality"),2,0,3,1,false},
    {TEXT("Effects quality"),2,0,3,1,false},
    {TEXT("View distance quality"),2,0,3,1,false},
    {TEXT("Health numbers"),1,0,1,1,true},
    {TEXT("Vigor numbers"),1,0,1,1,true},
    {TEXT("Chat font size"),14,12,18,2,false}
};
uint32 T(const TCHAR* V){return FAC2UILayout::Token(V);}
}
void SAC2GameUI::ToggleOptions()
{
    EndChat();ShowOptions=!ShowOptions;OptionsTab=0;OptionsScroll=0;
    if(ShowOptions){ShowStats=false;Mode->bMap=false;MapUI.Close();}
    if(OptionValues.IsEmpty())RestoreOptions();
    OptionValues=AppliedOptions;
    if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))PC->SetCursor(ShowOptions||ShowInventory);
    Rebuild();
}
void SAC2GameUI::RestoreOptions()
{
    if(OptionValues.IsEmpty())
    {
        for(int32 I=0;I<UE_ARRAY_COUNT(Settings);++I)
        {
            float V=Settings[I].Default;
            GConfig->GetFloat(TEXT("AC2.Options"),Settings[I].Label,V,GGameUserSettingsIni);
            OptionValues.Add(FMath::Clamp(V,Settings[I].Min,Settings[I].Max));
        }
        AppliedOptions=OptionValues;
    }
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr)
    {P->Camera->SetFieldOfView(AppliedOptions[0]);P->MouseSensitivity=AppliedOptions[1];P->InvertMouse=AppliedOptions[2]>.5;P->Boom->TargetArmLength=AppliedOptions[3];}
    HealthNumbers=AppliedOptions[10]>.5;VigorNumbers=AppliedOptions[11]>.5;
    ChatFont=Fonts.Body(AppliedOptions[12]);if(Entry)Entry->SetFont(ChatFont);
}
void SAC2GameUI::BuildOptions()
{
    Bind(TEXT("OptionCloseButton"),TEXT("Options"));
    const TCHAR* Tabs[]={TEXT("CommandTab"),TEXT("ClientTab"),TEXT("CharacterTab"),TEXT("ChatTab")};
    for(int32 I=0;I<4;++I)
    {
        Bind(Tabs[I],*FString::Printf(TEXT("OptionsTab%d"),I));
        Native.States.Add(T(Tabs[I]),T(I==OptionsTab?TEXT("open"):TEXT("closed")));
    }
    const auto* Panel=Native.Find(0xf30bc740);
    if(!Panel)return;
    const FIntPoint Origin=Panel->Rect.Min+FIntPoint(0,21);
    const FIntRect Page(Origin,Origin+FIntPoint(250,333));
    Native.Template(0x22000001,OptionsTab==0?TEXT("CommandPage"):OptionsTab==1?TEXT("ClientPage"):OptionsTab==2?TEXT("CharacterPage"):TEXT("ChatPage"),Page,Page);
    if(OptionsTab==0)
    {
        Bind(TEXT("LeaveButton"),TEXT("Logout"));Bind(TEXT("QuitButton"),TEXT("Quit"));Bind(TEXT("AssistanceButton"),TEXT("Help"));
        return;
    }
    Bind(TEXT("ApplyButton"),TEXT("OptionsApply"));Bind(TEXT("RecallButton"),TEXT("OptionsRecall"));Bind(TEXT("DefaultButton"),TEXT("OptionsDefault"));
    const FIntRect List=Native.Bounds(OptionsTab==1?TEXT("ClientList"):OptionsTab==2?TEXT("CharacterList"):TEXT("ChatOptionsList"));
    const int32 First=OptionsTab==2?10:OptionsTab==3?12:OptionsScroll;
    const int32 Last=OptionsTab==1?10:OptionsTab==2?12:13;
    for(int32 I=First;I<FMath::Min(First+9,Last)&&OptionValues.IsValidIndex(I);++I)
    {
        const auto& S=Settings[I];
        Native.Text.Add(T(TEXT("OptionText")),S.Check?FString(S.Label):FString::Printf(TEXT("%s: %g"),S.Label,OptionValues[I]));
        Native.States.Add(T(TEXT("CheckboxButton")),T(OptionValues[I]>.5?TEXT("highlight"):TEXT("normal")));
        Native.Positions.Add(T(TEXT("SliderBar")),(OptionValues[I]-S.Min)/(S.Max-S.Min));
        FIntRect Row(List.Min+FIntPoint(0,(I-First)*33),List.Min+FIntPoint(233,(I-First+1)*33));
        int32 Before=Native.Nodes.Num();
        Native.Template(0x22000001,S.Check?TEXT("OptionCheckbox"):TEXT("OptionSlider"),Row,List);
        for(int32 J=Before;J<Native.Nodes.Num();++J)
        {
            const auto& N=Native.Nodes[J];
            if(N.Id==T(TEXT("SliderBar")))OptionSliders.Add(I,FSlateRect(N.Clip.Min.X,N.Clip.Min.Y,N.Clip.Max.X,N.Clip.Max.Y));
            if(N.Id==T(TEXT("CheckboxButton"))||N.Id==T(TEXT("SliderRight"))||N.Id==T(TEXT("SliderLeft")))
            {
                FName Action(*(FString(N.Id==T(TEXT("SliderLeft"))?TEXT("OptionMinus"):TEXT("OptionPlus"))+FString::FromInt(I)));
                Buttons.Add(Action,FSlateRect(N.Clip.Min.X,N.Clip.Min.Y,N.Clip.Max.X,N.Clip.Max.Y));ButtonActions.Add(Action,Action);
            }
        }
    }
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
void SAC2GameUI::ApplyOptions()
{
    if(OptionValues.Num()!=UE_ARRAY_COUNT(Settings))return;
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr)
    {P->Camera->SetFieldOfView(OptionValues[0]);P->MouseSensitivity=OptionValues[1];P->InvertMouse=OptionValues[2]>.5;P->Boom->TargetArmLength=OptionValues[3];}
    if(auto* U=GEngine->GetGameUserSettings())
    {
        U->SetVSyncEnabled(OptionValues[4]>.5);U->SetFullscreenMode(OptionValues[5]>.5?EWindowMode::WindowedFullscreen:EWindowMode::Windowed);
        U->SetFrameRateLimit(OptionValues[6]);U->SetShadowQuality(OptionValues[7]);U->SetVisualEffectQuality(OptionValues[8]);U->SetViewDistanceQuality(OptionValues[9]);
        U->ApplySettings(false);
    }
    HealthNumbers=OptionValues[10]>.5;VigorNumbers=OptionValues[11]>.5;
    ChatFont=Fonts.Body(OptionValues[12]);if(Entry)Entry->SetFont(ChatFont);
    for(int32 I=0;I<OptionValues.Num();++I)GConfig->SetFloat(TEXT("AC2.Options"),Settings[I].Label,OptionValues[I],GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);AppliedOptions=OptionValues;
    Mode->Notice=TEXT("Options applied");
}
// Kept with the native page so settings bounds and actions cannot drift apart.
bool SAC2GameUI::OptionAction(FName Name)
{
    const FString Key=Name.ToString();
    if(Name==TEXT("Options")){ToggleOptions();return true;}
    if(Key.StartsWith(TEXT("OptionsTab"))){OptionsTab=FMath::Clamp(FCString::Atoi(*Key.Mid(10)),0,3);OptionsScroll=0;Rebuild();return true;}
    if(Name==TEXT("OptionsApply")){ApplyOptions();return true;}
    if(Name==TEXT("OptionsRecall")){OptionValues=AppliedOptions;Rebuild();return true;}
    if(Name==TEXT("OptionsDefault")){OptionValues.Reset();for(const auto& S:Settings)OptionValues.Add(S.Default);Rebuild();return true;}
    if(Name==TEXT("Logout")){ClosePanels();if(Mode->Network)Mode->Network->Logout();else{Mode->LeaveWorld();Mode->Offline();}return true;}
    if(Name==TEXT("Quit")){if(Mode->Network)Mode->Network->Logout();FPlatformMisc::RequestExit(false);return true;}
    const bool Plus=Key.StartsWith(TEXT("OptionPlus")),Minus=Key.StartsWith(TEXT("OptionMinus"));
    if(Plus||Minus)
    {
        const int32 I=FCString::Atoi(*Key.Mid(Plus?10:11));
        if(OptionValues.IsValidIndex(I)){const auto& S=Settings[I];OptionValues[I]=S.Check?1-OptionValues[I]:FMath::Clamp(OptionValues[I]+(Plus?S.Step:-S.Step),S.Min,S.Max);Rebuild();}
        return true;
    }
    return false;
}
bool SAC2GameUI::SmokeOptions(int32 Step,FString& Error)
{
    if(Step==0)
    {
        Rebuild();
        if(!Buttons.Contains(TEXT("LeaveButton"))||!Native.Find(T(TEXT("QuitButton")))||Native.MissingImages)
        {Error=TEXT("Native command page controls missing");return false;}
    }
    if(Step==1)
    {
        OptionAction(TEXT("OptionsTab1"));
        const auto* Slider=OptionSliders.Find(0);
        if(!Slider){Error=TEXT("Native FOV slider missing");return false;}
        DraggingOption=0;DragOption((Slider->Left+Slider->Right)*.5f);DraggingOption=INDEX_NONE;
        if(OptionValues[0]!=85){Error=TEXT("Native option slider interaction failed");return false;}
    }
    if(Step==2)OptionAction(TEXT("Logout"));
    return true;
}
