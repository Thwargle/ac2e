#include "AC2LoginUI.h"
#include "AC2CharacterUI.h"
#include "AC2GameMode.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "AC2AccountService.h"
#include "Async/Async.h"
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
    Form->AddSlot().AutoHeight().Padding(0,0,0,24)[Text(TEXT("ASHERON'S CALL 2 - SIGN IN"))];
    Form->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(SAC2Text).Font(Font)
        .Text(FText::FromString(TEXT("First sign-in creates your AC2E account. Use the same account and password next time (12-128 characters)."))).AutoWrapText(true)];
    auto Field=[&](const TCHAR* Label,TSharedPtr<SAC2TextEntry>& Entry,FString Value,bool Secret=false){
        Form->AddSlot().AutoHeight().Padding(0,4)[Text(Label)];
        Form->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(SBorder).BorderBackgroundColor(FLinearColor(.06,.06,.04,1)).Padding(8)
            [SNew(SBox).HeightOverride(25)[SAssignNew(Entry,SAC2TextEntry).Font(Font).Text(Value).NameOnly(false).Password(Secret).MaxCharacters(Secret?128:256)
                .IsEnabled_Lambda([this]{return !Busy;}).OnEnter_Lambda([this]{Login();})]]];
    };
    Field(TEXT("Account"),Account,User);Field(TEXT("Password"),Password,TEXT(""),true);
    Field(TEXT("Account service URL"),Endpoint,Service);
    Field(TEXT("Game server"),Host,Server);Field(TEXT("Port"),Port,FString::FromInt(ServerPort));
    auto Button=[&](const TCHAR* Label,TFunction<void()> Fn){return SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").IsEnabled_Lambda([this]{return !Busy;})
        .OnClicked_Lambda([Fn]{Fn();return FReply::Handled();})[SNew(SBorder).BorderImage(&ButtonArt).Padding(FMargin(16,8))[Text(Label)]];};
    Form->AddSlot().AutoHeight().Padding(0,12)[Button(TEXT("Connect"),[this]{Login();})];
    Form->AddSlot().AutoHeight()[Button(TEXT("Offline characters"),[this]{Password->SetText(TEXT(""));Mode->Offline();})];
    Form->AddSlot().AutoHeight().Padding(0,8)[Button(TEXT("Quit"),[]{FPlatformMisc::RequestExit(false);})];
    Form->AddSlot().AutoHeight().Padding(0,12)[SNew(SAC2Text).Font(Font).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)];
    ChildSlot[SNew(SOverlay)+SOverlay::Slot()[SNew(SImage).Image(&Background)]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBorder).BorderBackgroundColor(FLinearColor(.025,.03,.025,.97)).Padding(24)[SNew(SBox).WidthOverride(430)[Form]]]];
}
SAC2LoginUI::~SAC2LoginUI(){}
void SAC2LoginUI::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if(!FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke"))||SmokeStarted)return;
    SmokeTime+=Delta;if(SmokeTime<2)return;
    SmokeStarted=true;
    FString Secret=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_TEST_PASSWORD"));
    FPlatformMisc::SetEnvironmentVar(TEXT("AC2_TEST_PASSWORD"),TEXT(""));
    if(Secret.IsEmpty()||!Mode.IsValid()||Mode->InWorld()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // Exercise the same focused DAT-font text inputs and Enter action as a user.
    auto& App=FSlateApplication::Get();
    const FString User=Account->Value();Account->SetText(TEXT(""));
    App.SetKeyboardFocus(Account,EFocusCause::SetDirectly);
    for(TCHAR C:User)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
    App.SetKeyboardFocus(Password,EFocusCause::SetDirectly);
    for(TCHAR C:Secret)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
    const bool Entered=Account->Value()==User && Password->Value()==Secret;
    Secret.Reset();
    if(!Entered){FPlatformMisc::RequestExitWithStatus(false,1);return;}
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-sign-in.png"),true,false);
    App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
}
void SAC2LoginUI::Login()
{
    if(Busy||!Mode.IsValid())return;
    FAC2ConnectionSettings Settings;Settings.Host=Host->Value().TrimStartAndEnd();Settings.Account=Account->Value().TrimStartAndEnd().ToLower();Settings.Port=FCString::Atoi(*Port->Value());
    if(!Matches(Settings.Account,TEXT("[a-z0-9_]{1,32}"))||!Matches(Settings.Host,TEXT("[a-zA-Z0-9.-]+"))||!Matches(Port->Value(),TEXT("[0-9]{1,5}"))||Settings.Port<1||Settings.Port>65534)
    {Status=TEXT("Enter a valid account, server and port (1-65534).");return;}
    FString URL=Endpoint->Value().TrimStartAndEnd();
    if(!PreauthenticatedTicket.IsEmpty() && Password->Value().IsEmpty() &&
       Settings.Account==PreauthenticatedAccount && Settings.Host==PreauthenticatedHost && Settings.Port==PreauthenticatedPort)
    {Settings.Ticket=MoveTemp(PreauthenticatedTicket);Mode->Connect(Settings);return;}
    if(URL.IsEmpty())
    {
        if(!Password->Value().IsEmpty()){Status=TEXT("Enter the account-service URL to sign in with a password.");return;}
        Mode->Connect(Settings);return;
    }
    if(!ValidEndpoint(URL)){Status=TEXT("Use an HTTPS account-service address. Local HTTP is supported on 127.0.0.1.");return;}
    if(Password->Value().IsEmpty()){Status=TEXT("Enter your account password.");return;}
    if(Password->Value().Len()<12){Status=TEXT("Use a password of 12-128 characters. First sign-in creates your account; later sign-ins need the same password.");return;}
    URL.RemoveFromEnd(TEXT("/"));URL.ReplaceInline(TEXT("http://localhost"),TEXT("http://127.0.0.1"));
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("account"),Settings.Account);Body->SetStringField(TEXT("password"),Password->Value());
    FString Payload;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Payload));Password->SetText(TEXT(""));
    Body->Values.Empty();Busy=true;Status=TEXT("Signing in...");
    TWeakPtr<SAC2LoginUI> Weak=SharedThis(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Settings,URL,Payload=MoveTemp(Payload)]() mutable {
        auto Response=AC2AccountLogin(URL+TEXT("/api/login"),Payload);Payload.Reset();
        AsyncTask(ENamedThreads::GameThread,[Weak,Settings,Response=MoveTemp(Response)]() mutable {
        auto Self=Weak.Pin();if(!Self)return;Self->Busy=false;
        if(Response.Status!=200){
            TSharedPtr<FJsonObject> Error;FString Detail;
            if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body),Error) && Error.IsValid())Error->TryGetStringField(TEXT("message"),Detail);
            Self->Status=Response.Status==0?TEXT("Could not reach the account service. Start AC2E and check the account-service address."):
                Response.Status==401?TEXT("Account or password is incorrect."):Detail.IsEmpty()?TEXT("Sign-in failed. Check the account-service address and try again."):Detail.Left(256);
            return;
        }
        TSharedPtr<FJsonObject> J;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body),J)){Self->Status=TEXT("Invalid account-service response.");return;}
        FString Account,Host,Ticket;double Port=0;
        if(!J->TryGetStringField(TEXT("account"),Account)||Account!=Settings.Account||!J->TryGetStringField(TEXT("game_host"),Host)||!Matches(Host,TEXT("[a-zA-Z0-9.-]+"))||!J->TryGetNumberField(TEXT("game_port"),Port)||Port<1||Port>65534||Port!=FMath::FloorToDouble(Port)||!J->TryGetStringField(TEXT("ticket"),Ticket)||!Matches(Ticket,TEXT("[a-f0-9]{64}")))
        {Self->Status=TEXT("Invalid account-service connection details.");return;}
        Settings.Host=Host;Settings.Port=int32(Port);Settings.Ticket=Ticket;
        if(Self->Mode.IsValid())Self->Mode->Connect(Settings);
        });
    });
}
TSharedRef<SWidget> SAC2CharacterUI::Login(){return SNew(SAC2LoginUI).Mode(Mode.Get()).Message(Message);}
