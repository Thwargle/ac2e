#include "AC2LoginUI.h"
#include "AC2CharacterUI.h"
#include "AC2GameMode.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "AC2AccountService.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
namespace
{
bool Matches(const FString& Value,const TCHAR* Pattern){FRegexMatcher M(FRegexPattern(Pattern),Value);return M.FindNext()&&M.GetMatchBeginning()==0&&M.GetMatchEnding()==Value.Len();}
}
bool SAC2LoginUI::ValidEndpoint(const FString& URL)
{
    return Matches(URL,TEXT("https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/?")) || Matches(URL,TEXT("http://(127\\.0\\.0\\.1|localhost)(:[0-9]{1,5})?/?"));
}
void SAC2LoginUI::Construct(const FArguments& Args)
{
    Mode=Args._Mode;Status=Args._Message;
    FString Error;Fonts.Open(Mode->DatDirectory,Error);Assets=MakeUnique<FAC2AssetLibrary>();Assets->Open(Mode->DatDirectory,Error);
    Background.SetResourceObject(Assets->Texture(0x410002ec));Background.DrawAs=ESlateBrushDrawType::Image;
    ButtonArt.SetResourceObject(Assets->Texture(0x41006321));ButtonArt.DrawAs=ESlateBrushDrawType::Image;
    auto Normal=ButtonArt,Hover=ButtonArt,Pressed=ButtonArt,Disabled=ButtonArt;
    Normal.TintColor=FLinearColor(.45,.45,.4,1);Hover.TintColor=FLinearColor(.75,.75,.65,1);
    Pressed.TintColor=FLinearColor(.3,.3,.25,1);Disabled.TintColor=FLinearColor(.2,.2,.2,1);
    ButtonStyle.SetNormal(Normal).SetHovered(Hover).SetPressed(Pressed).SetDisabled(Disabled)
        .SetNormalPadding(FMargin(16,8)).SetPressedPadding(FMargin(16,8));
    auto Font=Fonts.Get(0x42000024);
    auto Text=[Font](FString V){return SNew(SAC2Text).Font(Font).Text(FText::FromString(V));};
    FString Server=TEXT("127.0.0.1"),User,Service=TEXT("http://127.0.0.1:19400");int32 ServerPort=9000;
    FParse::Value(FCommandLine::Get(),TEXT("AC2Server="),Server);FParse::Value(FCommandLine::Get(),TEXT("AC2Port="),ServerPort);
    FParse::Value(FCommandLine::Get(),TEXT("AC2Account="),User);FParse::Value(FCommandLine::Get(),TEXT("AC2AccountUrl="),Service);
    PreauthenticatedTicket=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_TICKET"));
    FPlatformMisc::SetEnvironmentVar(TEXT("AC2_LOGIN_TICKET"),TEXT(""));
    PreauthenticatedAccount=User.ToLower();PreauthenticatedHost=Server;PreauthenticatedPort=ServerPort;
    if(!PreauthenticatedTicket.IsEmpty())Status=TEXT("Account authenticated. Select Connect to continue.");
    auto Form=SNew(SVerticalBox);
    Form->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,16)[Text(TEXT("ASHERON'S CALL 2 - SIGN IN"))];
    Form->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(SAC2Text).Font(Font)
        .Text(FText::FromString(TEXT("First sign-in creates your AC2E account. Use the same account and password next time (12-128 characters)."))).AutoWrapText(true)];
    auto Field=[&](const TCHAR* Label,TSharedPtr<SAC2TextEntry>& Entry,FString Value,bool Secret=false){
        auto Column=SNew(SVerticalBox);
        Column->AddSlot().AutoHeight().Padding(0,2)[Text(Label)];
        Column->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.06,.06,.04,1)).Padding(6)
            [SNew(SBox).HeightOverride(25)[SAssignNew(Entry,SAC2TextEntry).Font(Font).Text(Value).NameOnly(false).Password(Secret).MaxCharacters(Secret?128:256)
                .IsEnabled_Lambda([this]{return !Busy;}).OnEnter_Lambda([this]{Login();})]]];
        return Column;
    };
    Form->AddSlot().AutoHeight()[Field(TEXT("Account"),Account,User)];
    Form->AddSlot().AutoHeight()[Field(TEXT("Password"),Password,TEXT(""),true)];
    Form->AddSlot().AutoHeight()[Field(TEXT("Account service URL"),Endpoint,Service)];
    Form->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(3).Padding(0,0,12,0)[Field(TEXT("Game server"),Host,Server)]
        +SHorizontalBox::Slot().FillWidth(1)[Field(TEXT("Port"),Port,FString::FromInt(ServerPort))]];
    auto Button=[&](const TCHAR* Label,TFunction<void()> Fn){
        auto Caption=Text(Label);
        auto Control=SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .IsEnabled_Lambda([this]{return !Busy;}).OnClicked_Lambda([Fn]{Fn();return FReply::Handled();})[Caption];
        Buttons.Add(Control);ButtonLabels.Add(Caption);return Control;
    };
    // Keep validation next to Connect, with room to wrap without moving the form.
    Form->AddSlot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(88)
        [SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(StatusText,SAC2Text).Font(Font)
            .Text_Lambda([this]{return FText::FromString(Status);}).ColorAndOpacity(FLinearColor(1,.85,.5)).AutoWrapText(true)]]];
    ConnectButton=Button(TEXT("Connect"),[this]{Login();});
    Form->AddSlot().AutoHeight().Padding(0,0,0,8)[ConnectButton.ToSharedRef()];
    Form->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[Button(TEXT("Offline characters"),[this]{Password->SetText(TEXT(""));Mode->Offline();})]
        +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("Quit"),[]{FPlatformMisc::RequestExit(false);})]];
    ChildSlot[SNew(SOverlay)+SOverlay::Slot()[SNew(SImage).Image(&Background)]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.025,.03,.025,.94)).Padding(24)[SNew(SBox).WidthOverride(460)[Form]]]];
}
SAC2LoginUI::~SAC2LoginUI(){}
void SAC2LoginUI::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if(!FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke"))||SmokeStarted)return;
    SmokeTime+=Delta;if(SmokeTime<2)return;
    SmokeStarted=true;
    // Widget Tick runs during paint, before descendants enter the hit-test grid.
    // Route input on the next core tick using the completed previous frame.
    TWeakPtr<SAC2LoginUI> Weak=SharedThis(this);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){
        if(auto Self=Weak.Pin())return Self->SmokeLogin();return false;
    }),1.f);
}
bool SAC2LoginUI::SmokeLogin()
{
    FString Secret=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_TEST_PASSWORD"));
    if(Secret.IsEmpty()||!Mode.IsValid()||Mode->InWorld()){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("AC2 login smoke: %s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);return false;};
    const bool Register=!FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_REGISTER_SMOKE")).IsEmpty();
    auto Center=[](const TSharedPtr<SWidget>& W){const auto& G=W->GetCachedGeometry();return G.LocalToAbsolute(G.GetLocalSize()*.5);};
    if(SmokeStep==2){
        if(Busy){if(++SmokeWait<20)return true;return Fail(TEXT("account response timed out"));}
        if(!Status.Contains(TEXT("incorrect"))||!ConnectButton->IsEnabled())return Fail(TEXT("wrong-password feedback or retry missing"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-wrong-password.png"),true,false);
        SmokeStep=3;return true;
    }
    for(int32 I=0;I<Buttons.Num();++I)
        if(!Center(Buttons[I]).Equals(Center(ButtonLabels[I]),1.))return Fail(TEXT("button caption is not centered"));
    if(SmokeStep==0)SmokeButtonCenter=Center(ConnectButton);
    else {
        if(!SmokeButtonCenter.Equals(Center(ConnectButton),1.))return Fail(TEXT("validation moved Connect"));
        if(StatusText->GetCachedGeometry().GetLocalSize().Y+1 < StatusText->ComputeDesiredSize(1).Y)
            return Fail(TEXT("validation text is clipped"));
    }
    // Resolve mouse input through the rendered hit-test grid. Directly focusing
    // fields and submitting Enter does not exercise Connect-button routing.
    auto& App=FSlateApplication::Get();
    auto Click=[&](const TSharedRef<SWidget>& Widget){
        auto Window=App.FindWidgetWindow(AsShared());if(!Window)return false;
        const auto& G=Widget->GetCachedGeometry();const FVector2D At=G.LocalToAbsolute(G.GetLocalSize()*.5);
        auto Path=App.LocateWindowUnderMouse(At,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return false;
        bool Found=false;for(int32 I=0;I<Path.Widgets.Num();++I)Found|=Path.Widgets[I].Widget==Widget;
        if(!Found){
            FString Route;for(int32 I=0;I<Path.Widgets.Num();++I)Route+=Path.Widgets[I].Widget->GetTypeAsString()+TEXT(" > ");
            UE_LOG(LogTemp,Error,TEXT("AC2 login mouse target blocked: %s at %s; route=%s"),*Widget->GetTypeAsString(),*At.ToString(),*Route);
            return false;
        }
        const TSet<FKey> None,Down={EKeys::LeftMouseButton};
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,At,At,None,FKey(),0,FModifierKeysState()),false);
        if(Widget==ConnectButton&&!ConnectButton->IsHovered())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(Widget==ConnectButton&&!ConnectButton->IsPressed())return false;
        App.RoutePointerUpEvent(Path,FPointerEvent(0,At,At,None,EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    const FString User=Account->Value();Account->SetText(TEXT(""));
    if(!Click(Account.ToSharedRef())||!Account->HasKeyboardFocus()){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    for(TCHAR C:User)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
    Password->SetText(TEXT(""));
    if(!Click(Password.ToSharedRef())||!Password->HasKeyboardFocus()){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    if(SmokeStep==0){
        for(TCHAR C:FString(TEXT("short")))App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        if(!Click(ConnectButton.ToSharedRef())||Busy||!Status.Contains(TEXT("12-128"))){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-validation.png"),true,false);
        SmokeStep=1;
        return true;
    }
    if(SmokeStep==1&&!Register){
        for(TCHAR C:Secret+TEXT("!"))App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
        if(!Click(ConnectButton.ToSharedRef())||!Busy)return Fail(TEXT("wrong-password request was not submitted"));
        SmokeStep=2;return true;
    }
    FPlatformMisc::SetEnvironmentVar(TEXT("AC2_TEST_PASSWORD"),TEXT(""));
    for(TCHAR C:Secret)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
    const bool Entered=Account->Value()==User && Password->Value()==Secret;
    Secret.Reset();
    if(!Entered){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-sign-in.png"),true,false);
    if(!Click(ConnectButton.ToSharedRef())||!Busy)
    {return Fail(TEXT("mouse click did not submit"));}
    UE_LOG(LogTemp,Display,TEXT("AC2 login: centered captions, stable feedback, mouse focus and Connect click passed"));
    return false;
}
void SAC2LoginUI::SetStatus(FString Message)
{
    Status=MoveTemp(Message);
    if(StatusText)StatusText->Invalidate(EInvalidateWidgetReason::Layout);
}
void SAC2LoginUI::Login()
{
    if(Busy||!Mode.IsValid())return;
    UE_LOG(LogTemp,Display,TEXT("AC2 login: Connect submitted"));
    FAC2ConnectionSettings Settings;Settings.Host=Host->Value().TrimStartAndEnd();Settings.Account=Account->Value().TrimStartAndEnd().ToLower();Settings.Port=FCString::Atoi(*Port->Value());
    if(!Matches(Settings.Account,TEXT("[a-z0-9_]{1,32}")))
    {SetStatus(TEXT("Account must be 1-32 letters, numbers or underscores. Spaces are not allowed."));return;}
    if(!Matches(Settings.Host,TEXT("[a-zA-Z0-9.-]+"))||!Matches(Port->Value(),TEXT("[0-9]{1,5}"))||Settings.Port<1||Settings.Port>65534)
    {SetStatus(TEXT("Enter a valid game server and port (1-65534)."));return;}
    FString URL=Endpoint->Value().TrimStartAndEnd();
    if(!PreauthenticatedTicket.IsEmpty() && Password->Value().IsEmpty() &&
       Settings.Account==PreauthenticatedAccount && Settings.Host==PreauthenticatedHost && Settings.Port==PreauthenticatedPort)
    {Settings.Ticket=MoveTemp(PreauthenticatedTicket);Mode->Connect(Settings);return;}
    if(URL.IsEmpty())
    {
        if(!Password->Value().IsEmpty()){SetStatus(TEXT("Enter the account-service URL to sign in with a password."));return;}
        Mode->Connect(Settings);return;
    }
    if(!ValidEndpoint(URL)){SetStatus(TEXT("Use an HTTPS account-service address. Local HTTP is supported on 127.0.0.1."));return;}
    if(Password->Value().IsEmpty()){SetStatus(TEXT("Enter your account password."));return;}
    if(Password->Value().Len()<12){SetStatus(TEXT("Use a password of 12-128 characters. First sign-in creates your account; later sign-ins need the same password."));return;}
    URL.RemoveFromEnd(TEXT("/"));URL.ReplaceInline(TEXT("http://localhost"),TEXT("http://127.0.0.1"));
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("account"),Settings.Account);Body->SetStringField(TEXT("password"),Password->Value());
    FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));Password->SetText(TEXT(""));
    Body->Values.Empty();Busy=true;SetStatus(TEXT("Signing in..."));
    UE_LOG(LogTemp,Display,TEXT("AC2 login: requesting account authentication"));
    TWeakPtr<SAC2LoginUI> Weak=SharedThis(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Settings,URL,Payload=MoveTemp(Payload)]() mutable {
        auto Response=AC2AccountLogin(URL+TEXT("/api/login"),Payload);Payload.Reset();
        AsyncTask(ENamedThreads::GameThread,[Weak,Settings,Response=MoveTemp(Response)]() mutable {
        auto Self=Weak.Pin();if(!Self)return;Self->Busy=false;
        UE_LOG(LogTemp,Display,TEXT("AC2 login: account service returned HTTP %d"),Response.Status);
        if(Response.Status!=200){
            TSharedPtr<FJsonObject> Error;FString Detail;
            if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body),Error) && Error.IsValid())Error->TryGetStringField(TEXT("message"),Detail);
            Self->SetStatus(Response.Status==0?TEXT("Could not reach the account service. Start AC2E and check the account-service address."):
                Response.Status==401?TEXT("Account or password is incorrect."):Detail.IsEmpty()?TEXT("Sign-in failed. Check the account-service address and try again."):Detail.Left(256));
            return;
        }
        TSharedPtr<FJsonObject> J;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body),J)){Self->SetStatus(TEXT("Invalid account-service response."));return;}
        FString Account,Host,Ticket;double Port=0;
        if(!J->TryGetStringField(TEXT("account"),Account)||Account!=Settings.Account||!J->TryGetStringField(TEXT("game_host"),Host)||!Matches(Host,TEXT("[a-zA-Z0-9.-]+"))||!J->TryGetNumberField(TEXT("game_port"),Port)||Port<1||Port>65534||Port!=FMath::FloorToDouble(Port)||!J->TryGetStringField(TEXT("ticket"),Ticket)||!Matches(Ticket,TEXT("[a-f0-9]{64}")))
        {Self->SetStatus(TEXT("Invalid account-service connection details."));return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke"))&&!FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_REGISTER_SMOKE")).IsEmpty()){
            bool Created=false;
            if(!J->TryGetBoolField(TEXT("created"),Created)||!Created){UE_LOG(LogTemp,Error,TEXT("AC2 login smoke: first sign-in did not create an account"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        }
        Settings.Host=Host;Settings.Port=int32(Port);Settings.Ticket=Ticket;
        if(Self->Mode.IsValid())Self->Mode->Connect(Settings);
        });
    });
}
TSharedRef<SWidget> SAC2CharacterUI::Login(){return SNew(SAC2LoginUI).Mode(Mode.Get()).Message(Message);}
