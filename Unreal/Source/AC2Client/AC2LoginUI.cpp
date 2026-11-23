#include "AC2LoginUI.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2GameInstance.h"
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
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
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
#include "Widgets/Input/SCheckBox.h"
#include "AC2Explorer.h"
#include "AC2PortalSpace.h"
#include "EngineUtils.h"
namespace
{
TWeakObjectPtr<UAC2GameInstance> ProfileReloadSmoke,DatUpdateSmoke;
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
    auto LoadButtonStyle=[this](uint32 NormalId,uint32 PressedId,uint32 HoverId,uint32 GhostedId,FVector2D Size){
        auto Brush=[&](uint32 Id){
            FSlateBrush Art;
            Art.SetResourceObject(Assets->Texture(Id));
            Art.DrawAs=ESlateBrushDrawType::Image;
            Art.ImageSize=Size;
            return Art;
        };
        FButtonStyle Style;
        Style.SetNormal(Brush(NormalId)).SetPressed(Brush(PressedId)).SetHovered(Brush(HoverId)).SetDisabled(Brush(GhostedId))
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return Style;
    };
    // The DAT defines both widths of this button with their own painted end caps.
    ProfileButtonStyle=LoadButtonStyle(0x41001a8a,0x41001a8b,0x41001a8c,0x41001a8d,{142,35});
    LoginButtonStyle=LoadButtonStyle(0x41006321,0x41006320,0x4100631f,0x4100631e,{192,35});
    auto Font=Fonts.Get(0x42000024);
    auto Text=[Font](FString V){return SNew(SAC2Text).Font(Font).Text(FText::FromString(V));};
    DirectLogin=FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLogin"));
    FString Server=TEXT("127.0.0.1"),User,Service=TEXT("http://127.0.0.1:19400"),Version=DirectLogin?TEXT("7160"):TEXT("7148");int32 ServerPort=9000;
    FParse::Value(FCommandLine::Get(),TEXT("AC2Server="),Server);FParse::Value(FCommandLine::Get(),TEXT("AC2Port="),ServerPort);
    FParse::Value(FCommandLine::Get(),TEXT("AC2Account="),User);FParse::Value(FCommandLine::Get(),TEXT("AC2AccountUrl="),Service);
    FParse::Value(FCommandLine::Get(),TEXT("AC2Protocol="),Version);
    if(Mode->HasLastLogin){
        const auto& Last=Mode->LastLogin;Server=Last.Host;ServerPort=Last.Port;User=Last.Account;
        DirectLogin=Last.DirectLogin;Version=Last.Protocol;Service=Mode->LastAccountEndpoint;
    }
    PreauthenticatedTicket=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_TICKET"));
    FPlatformMisc::SetEnvironmentVar(TEXT("AC2_LOGIN_TICKET"),TEXT(""));
    PreauthenticatedAccount=User.ToLower();PreauthenticatedHost=Server;PreauthenticatedPort=ServerPort;
    if(!PreauthenticatedTicket.IsEmpty())Status=TEXT("Account authenticated. Select Connect to continue.");
    FString ProfileError;
    FString ProfilePath=FPaths::ProjectSavedDir()/TEXT("Login/profiles.json");
    // Automation uses a separate profile store and credential namespace.
    FParse::Value(FCommandLine::Get(),TEXT("AC2LoginProfiles="),ProfilePath);
    if(!Profiles.Load(ProfilePath,ProfileError))Status=ProfileError;
    Profiles.Defaults(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")));
    auto Form=SNew(SVerticalBox),ServerForm=SNew(SVerticalBox),AccountForm=SNew(SVerticalBox);
    Form->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,16)[Text(TEXT("ASHERON'S CALL 2 - SIGN IN"))];
    auto Field=[&](const TCHAR* Label,TSharedPtr<SAC2TextEntry>& Entry,FString Value,bool Secret=false){
        auto Column=SNew(SVerticalBox);
        Column->AddSlot().AutoHeight().Padding(0,2)[Text(Label)];
        Column->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.06f,.06f,.04f,1)).Padding(6)
            [SNew(SBox).HeightOverride(25)[SAssignNew(Entry,SAC2TextEntry).Font(Font).Text(Value).NameOnly(false).Password(Secret).MaxCharacters(Secret?128:256)
                .IsEnabled_Lambda([this]{return !Busy;}).OnEnter_Lambda([this]{Login();})]]];
        return Column;
    };
    auto Button=[&](const FButtonStyle& Style,const TCHAR* Label,TFunction<void()> Fn){
        auto Caption=Text(Label);
        auto Control=SNew(SButton).ButtonStyle(&Style).ContentPadding(0)
            .IsEnabled_Lambda([this]{return !Busy;}).OnClicked_Lambda([Fn]{Fn();return FReply::Handled();})
            [SNew(SBox).WidthOverride(Style.Normal.ImageSize.X).HeightOverride(Style.Normal.ImageSize.Y)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)[Caption]];
        Buttons.Add(Control);ButtonLabels.Add(Caption);return Control;
    };
    auto ServerLabel=[this](const FString& Id){const auto* S=Profiles.Servers.FindByPredicate([&](const auto& P){return P.Id==Id;});return S?S->Name:TEXT("New server");};
    auto AccountLabel=[this](const FString& Id){const auto* A=Profiles.Accounts.FindByPredicate([&](const auto& P){return P.Id==Id;});return A?A->Username:TEXT("New account");};
    ServerForm->AddSlot().AutoHeight().Padding(0,0,0,6)[Text(TEXT("SAVED SERVERS"))];
    ServerForm->AddSlot().AutoHeight()[SAssignNew(ServerList,SComboBox<TSharedPtr<FString>>).OptionsSource(&ServerOptions)
        .IsEnabled_Lambda([this]{return !Busy;})
        .OnGenerateWidget_Lambda([Text,ServerLabel](TSharedPtr<FString> Id)->TSharedRef<SWidget>{return Text(ServerLabel(*Id));})
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Id,ESelectInfo::Type){if(Id&&!SelectingProfile)SelectServer(*Id);})
        [SNew(SAC2Text).Font(Font).Text_Lambda([this,ServerLabel]{return FText::FromString(ServerLabel(Profiles.SelectedServer));})]];
    ServerForm->AddSlot().AutoHeight()[Field(TEXT("Server name"),ServerName,TEXT(""))];
    auto SetType=[this](bool Icefire){DirectLogin=Icefire;PreauthenticatedTicket.Reset();Password->SetText(TEXT(""));LoadedCredentialIdentity.Reset();
        Protocol->SetText(Icefire?TEXT("7160"):TEXT("7148"));SetStatus(TEXT(""));};
    ServerForm->AddSlot().AutoHeight().Padding(0,5,0,8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,20,0)[SNew(SCheckBox).IsEnabled_Lambda([this]{return !Busy;})
            .IsChecked_Lambda([this]{return DirectLogin?ECheckBoxState::Unchecked:ECheckBoxState::Checked;})
            .OnCheckStateChanged_Lambda([SetType](ECheckBoxState){SetType(false);})[Text(TEXT("AC2E"))]]
        +SHorizontalBox::Slot().AutoWidth()[SAssignNew(DirectControl,SCheckBox).IsEnabled_Lambda([this]{return !Busy;})
            .IsChecked_Lambda([this]{return DirectLogin?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([SetType](ECheckBoxState){SetType(true);})[Text(TEXT("Icefire"))]]];
    ServerForm->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(3).Padding(0,0,12,0)[Field(TEXT("Host / IP"),Host,Server)]
        +SHorizontalBox::Slot().FillWidth(1)[Field(TEXT("Game port"),Port,FString::FromInt(ServerPort))]];
    auto ServiceField=Field(TEXT("Account service URL"),Endpoint,Service);
    AccountServiceField=ServiceField;
    ServiceField->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return DirectLogin?EVisibility::Collapsed:EVisibility::Visible;}));
    // Reserve the service row in either mode so choosing a type doesn't move buttons.
    ServerForm->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(64)[ServiceField]];
    Endpoint->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !Busy && !DirectLogin;}));
    ServerForm->AddSlot().AutoHeight()[Field(TEXT("Client version (7148 / 7160 / 7230)"),Protocol,Version)];
    ServerForm->AddSlot().AutoHeight()[Field(TEXT("DAT folder"),DatFolder,Mode->DatDirectory)];
    ServerForm->AddSlot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[Button(ProfileButtonStyle,TEXT("New"),[this]{NewServer();})]
        +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Button(ProfileButtonStyle,TEXT("Save"),[this]{SaveServer();})]
        +SHorizontalBox::Slot().AutoWidth()[Button(ProfileButtonStyle,TEXT("Remove"),[this]{RemoveServer();})]];
    AccountForm->AddSlot().AutoHeight().Padding(0,0,0,6)[Text(TEXT("ACCOUNTS FOR THIS SERVER"))];
    AccountForm->AddSlot().AutoHeight()[SAssignNew(AccountList,SComboBox<TSharedPtr<FString>>).OptionsSource(&AccountOptions)
        .IsEnabled_Lambda([this]{return !Busy;})
        .OnGenerateWidget_Lambda([Text,AccountLabel](TSharedPtr<FString> Id)->TSharedRef<SWidget>{return Text(AccountLabel(*Id));})
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Id,ESelectInfo::Type){if(Id&&!SelectingProfile)SelectAccount(*Id);})
        [SNew(SAC2Text).Font(Font).Text_Lambda([this,AccountLabel]{return FText::FromString(AccountLabel(Profiles.SelectedAccount));})]];
    AccountForm->AddSlot().AutoHeight()[Field(TEXT("Username"),Account,User)];
    AccountForm->AddSlot().AutoHeight()[Field(TEXT("Password"),Password,TEXT(""),true)];
    AccountForm->AddSlot().AutoHeight().Padding(0,5,0,12)[SNew(SCheckBox).IsEnabled_Lambda([this]{return !Busy;})
        .IsChecked_Lambda([this]{return RememberPassword?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([this](ECheckBoxState V){RememberPassword=V==ECheckBoxState::Checked;})[Text(TEXT("Remember password on this Windows account"))]];
    AccountForm->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[Button(ProfileButtonStyle,TEXT("New"),[this]{NewAccount();})]
        +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[Button(ProfileButtonStyle,TEXT("Save"),[this]{SaveAccount();})]
        +SHorizontalBox::Slot().AutoWidth()[Button(ProfileButtonStyle,TEXT("Remove"),[this]{RemoveAccount();})]];
    AccountForm->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(96)[SNew(SAC2Text).Font(Font)
        .Text_Lambda([this]{return FText::FromString(DirectLogin?
            TEXT("Icefire creates accounts on login. It uses the legacy GLS connection, without HTTPS. Select Icefire's DAT folder for its item and character data."):
            TEXT("First sign-in creates your AC2E account. Use the same password next time (12-128 characters). Saved passwords are protected by Windows Credential Manager."));}).AutoWrapText(true)]];
    Form->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,24,0)[ServerForm]
        +SHorizontalBox::Slot().FillWidth(1)[AccountForm]];
    // Keep validation next to Connect, with room to wrap without moving the form.
    Form->AddSlot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(88)
        [SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(StatusText,SAC2Text).Font(Font)
            .Text_Lambda([this]{return FText::FromString(Status);}).ColorAndOpacity(FLinearColor(1,.85f,.5f)).AutoWrapText(true)]]];
    ConnectButton=Button(LoginButtonStyle,TEXT("Connect"),[this]{Login();});
    Form->AddSlot().AutoHeight().HAlign(HAlign_Center)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[ConnectButton.ToSharedRef()]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[Button(LoginButtonStyle,TEXT("Offline characters"),[this]{Password->SetText(TEXT(""));Mode->Offline();})]
        +SHorizontalBox::Slot().AutoWidth()[Button(LoginButtonStyle,TEXT("Quit"),[]{FPlatformMisc::RequestExit(false);})]];
    ChildSlot[SNew(SOverlay)+SOverlay::Slot()[SNew(SImage).Image(&Background)]
        +SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.025f,.03f,.025f,.94f)).Padding(24)[SNew(SBox).WidthOverride(900)[Form]]]]];
    const FString SavedAccount=Profiles.SelectedAccount;
    const FString LauncherTicket=MoveTemp(PreauthenticatedTicket);
    RefreshProfiles();SelectServer(Profiles.SelectedServer);SelectAccount(SavedAccount);
    // Explicit launcher defaults take precedence over saved selections, but a
    // saved password is never carried across to an overridden endpoint.
    if(!LauncherTicket.IsEmpty()||Mode->HasLastLogin||FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLogin"))||FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke"))||
       FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLoginSmoke"))||FString(FCommandLine::Get()).Contains(TEXT("AC2Server="))) {
        Host->SetText(Server);Port->SetText(FString::FromInt(ServerPort));Endpoint->SetText(Service);Protocol->SetText(Version);Account->SetText(User);
        DirectLogin=Mode->HasLastLogin?Mode->LastLogin.DirectLogin:FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLogin"));
        DatFolder->SetText(Mode->DatDirectory);Password->SetText(TEXT(""));LoadedCredentialIdentity.Reset();
        PreauthenticatedTicket=LauncherTicket;
    }
}
SAC2LoginUI::~SAC2LoginUI(){}
FAC2ServerProfile SAC2LoginUI::ServerFields() const
{
    FAC2ServerProfile S;S.Id=Profiles.SelectedServer;S.Name=ServerName->Value().TrimStartAndEnd();S.Host=Host->Value().TrimStartAndEnd();
    S.Port=FCString::Atoi(*Port->Value().TrimStartAndEnd());S.Endpoint=Endpoint->Value().TrimStartAndEnd();S.Protocol=Protocol->Value().TrimStartAndEnd();
    S.Icefire=DirectLogin;S.DatDirectory=DatFolder->Value().TrimStartAndEnd();
    if(!S.DatDirectory.IsEmpty())S.DatDirectory=FPaths::ConvertRelativePathToFull(FPaths::IsRelative(S.DatDirectory)?FPaths::ProjectDir()/S.DatDirectory:S.DatDirectory);
    return S;
}
void SAC2LoginUI::RefreshProfiles()
{
    SelectingProfile=true;ServerOptions.Reset();AccountOptions.Reset();
    for(const auto& S:Profiles.Servers)ServerOptions.Add(MakeShared<FString>(S.Id));
    for(const auto& A:Profiles.Accounts)if(A.ServerId==Profiles.SelectedServer)AccountOptions.Add(MakeShared<FString>(A.Id));
    ServerList->RefreshOptions();AccountList->RefreshOptions();ServerList->ClearSelection();AccountList->ClearSelection();
    for(auto Id:ServerOptions)if(*Id==Profiles.SelectedServer)ServerList->SetSelectedItem(Id);
    for(auto Id:AccountOptions)if(*Id==Profiles.SelectedAccount)AccountList->SetSelectedItem(Id);
    SelectingProfile=false;
}
void SAC2LoginUI::NewAccount()
{
    Profiles.SelectedAccount.Reset();LoadedCredentialIdentity.Reset();Account->SetText(TEXT(""));Password->SetText(TEXT(""));RememberPassword=false;RefreshProfiles();
}
void SAC2LoginUI::NewServer()
{
    Profiles.SelectedServer.Reset();NewAccount();ServerName->SetText(TEXT(""));Host->SetText(TEXT(""));Port->SetText(TEXT("9000"));
    Endpoint->SetText(TEXT(""));DirectLogin=false;Protocol->SetText(TEXT("7148"));DatFolder->SetText(Mode->DatDirectory);PreauthenticatedTicket.Reset();
    SetStatus(TEXT("Enter the server settings, then Save."));
}
void SAC2LoginUI::SelectServer(const FString& Id)
{
    const auto* S=Profiles.Servers.FindByPredicate([&](const auto& P){return P.Id==Id;});if(!S)return;
    Profiles.SelectedServer=S->Id;ServerName->SetText(S->Name);Host->SetText(S->Host);Port->SetText(FString::FromInt(S->Port));
    Endpoint->SetText(S->Endpoint);Protocol->SetText(S->Protocol);DatFolder->SetText(S->DatDirectory);DirectLogin=S->Icefire;
    PreauthenticatedTicket.Reset();NewAccount();
    if(!AccountOptions.IsEmpty())SelectAccount(*AccountOptions[0]);
}
void SAC2LoginUI::SelectAccount(const FString& Id)
{
    const auto* A=Profiles.Accounts.FindByPredicate([&](const auto& P){return P.Id==Id&&P.ServerId==Profiles.SelectedServer;});if(!A)return;
    Profiles.SelectedAccount=A->Id;Account->SetText(A->Username);FString Secret;
    RememberPassword=Profiles.ReadPassword(*A,ServerFields(),Secret);Password->SetText(Secret);Secret.Reset();
    LoadedCredentialIdentity=RememberPassword?ServerFields().Scope()+TEXT("|")+A->Username:TEXT("");RefreshProfiles();
}
bool SAC2LoginUI::SaveServer()
{
    FString Error;auto S=ServerFields();
    if(!Matches(Port->Value().TrimStartAndEnd(),TEXT("[0-9]{1,5}"))||!FAC2LoginProfiles::Validate(S,Error))
    {SetStatus(Error.IsEmpty()?TEXT("Enter a numeric game port."):Error);return false;}
    if(S.Id.IsEmpty())S.Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Existing=Profiles.Servers.FindByPredicate([&](const auto& P){return P.Id==S.Id;});
    const auto Before=Profiles.Servers;const FString Selected=Profiles.SelectedServer;
    if(Existing)*Existing=S;else Profiles.Servers.Add(S);Profiles.SelectedServer=S.Id;
    if(!Profiles.Save(Error)){Profiles.Servers=Before;Profiles.SelectedServer=Selected;SetStatus(Error);return false;}
    RefreshProfiles();SetStatus(TEXT("Server saved."));return true;
}
bool SAC2LoginUI::SaveSelection()
{
    const FString Username=DirectLogin?Account->Value().TrimStartAndEnd():Account->Value().TrimStartAndEnd().ToLower();
    if(!Matches(Username,TEXT("[a-zA-Z0-9_]{1,32}"))){SetStatus(TEXT("Username must be 1-32 letters, numbers or underscores."));return false;}
    if(!SaveServer())return false;
    const auto* S=Profiles.Servers.FindByPredicate([&](const auto& P){return P.Id==Profiles.SelectedServer;});
    if(!S || S->Scope()!=ServerFields().Scope())return false;
    auto* Existing=Profiles.Accounts.FindByPredicate([&](const auto& A){return A.ServerId==S->Id&&A.Username==Username;});
    FAC2AccountProfile A=Existing?*Existing:FAC2AccountProfile{FGuid::NewGuid().ToString(EGuidFormats::Digits),S->Id,Username,TEXT("")};
    FString Error;
    if(RememberPassword){if(!Profiles.StorePassword(A,*S,Password->Value(),Error)){SetStatus(Error);return false;}}
    else if(!Profiles.ForgetPassword(A,Error)){SetStatus(Error);return false;}
    if(Existing)*Existing=A;else Profiles.Accounts.Add(A);
    Profiles.SelectedAccount=A.Id;
    if(!Profiles.Save(Error)){SetStatus(Error);return false;}
    RefreshProfiles();return true;
}
void SAC2LoginUI::SaveAccount(){if(SaveSelection())SetStatus(RememberPassword?TEXT("Account saved. Password protected by Windows Credential Manager."):TEXT("Username saved. Password will be requested at sign-in."));}
void SAC2LoginUI::RemoveAccount()
{
    FString Error;auto* A=Profiles.Accounts.FindByPredicate([&](const auto& P){return P.Id==Profiles.SelectedAccount;});if(!A)return;
    if(!Profiles.ForgetPassword(*A,Error)){SetStatus(Error);return;}
    Profiles.Accounts.RemoveAll([&](const auto& P){return P.Id==Profiles.SelectedAccount;});Profiles.SelectedAccount.Reset();
    if(!Profiles.Save(Error)){SetStatus(Error);return;}NewAccount();SetStatus(TEXT("Saved account removed from this computer."));
}
void SAC2LoginUI::RemoveServer()
{
    if(Profiles.SelectedServer.IsEmpty())return;FString Error;
    for(auto& A:Profiles.Accounts)if(A.ServerId==Profiles.SelectedServer&&!Profiles.ForgetPassword(A,Error)){SetStatus(Error);return;}
    Profiles.Accounts.RemoveAll([&](const auto& P){return P.ServerId==Profiles.SelectedServer;});
    Profiles.Servers.RemoveAll([&](const auto& P){return P.Id==Profiles.SelectedServer;});Profiles.SelectedServer.Reset();Profiles.SelectedAccount.Reset();
    if(!Profiles.Save(Error)){SetStatus(Error);return;}
    if(Profiles.Servers.IsEmpty())NewServer();else SelectServer(Profiles.Servers[0].Id);
    RefreshProfiles();SetStatus(TEXT("Saved server and its local accounts removed."));
}
void SAC2LoginUI::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if(!LoadedCredentialIdentity.IsEmpty() && LoadedCredentialIdentity!=ServerFields().Scope()+TEXT("|")+Account->Value())
    {Password->SetText(TEXT(""));LoadedCredentialIdentity.Reset();RememberPassword=false;}
    const bool ProfileSmoke=FParse::Param(FCommandLine::Get(),TEXT("AC2ProfilesSmoke"));
    // The profile smoke owns its ticker across the two DAT reloads.
    if(Mode.IsValid() && DatUpdateSmoke==Mode->GetGameInstance())return;
    if(ProfileSmoke && Mode.IsValid() && ProfileReloadSmoke==Mode->GetGameInstance())return;
    if((!ProfileSmoke && !FParse::Param(FCommandLine::Get(),TEXT("AC2LoginSmoke")) && !FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLoginSmoke")))||SmokeStarted)return;
    SmokeTime+=Delta;if(SmokeTime<2)return;
    SmokeStarted=true;
    // Widget Tick runs during paint, before descendants enter the hit-test grid.
    // Route input on the next core tick using the completed previous frame.
    TWeakPtr<SAC2LoginUI> Weak=SharedThis(this);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak,ProfileSmoke](float){
        if(auto Self=Weak.Pin())return ProfileSmoke?Self->SmokeProfiles():Self->SmokeLogin();return false;
    }),1.f);
}
bool SAC2LoginUI::SmokeProfiles()
{
    FString File;
    if(!FParse::Value(FCommandLine::Get(),TEXT("AC2LoginProfiles="),File)||!FPaths::GetCleanFilename(File).StartsWith(TEXT("test-")))
    {UE_LOG(LogTemp,Error,TEXT("Profile smoke requires an isolated test- profile filename"));FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    auto Require=[](bool OK,const TCHAR* Message){if(!OK){UE_LOG(LogTemp,Error,TEXT("AC2 profiles smoke: %s"),Message);FPlatformMisc::RequestExitWithStatus(false,1);}return OK;};
    if(SmokeStep==0){
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-profiles.png"),true,false);
        for(const TSharedPtr<SWidget>& W:TArray<TSharedPtr<SWidget>>{ServerList,AccountList,Host,Port,Account,Password,DatFolder,ConnectButton})
            if(!Require(W->GetCachedGeometry().GetLocalSize().X>50&&W->GetCachedGeometry().GetLocalSize().Y>=20,TEXT("form control has no usable hit target")))return false;
        for(const auto& Control:Buttons){
            const FVector2D Size=Control->GetCachedGeometry().GetLocalSize();
            if(!Require(Size.Equals(FVector2D(ProfileButtonStyle.Normal.ImageSize),1.) || Size.Equals(FVector2D(LoginButtonStyle.Normal.ImageSize),1.),TEXT("button hit target does not match its DAT art")))return false;
        }
        SelectServer(Profiles.Servers[1].Id);Account->SetText(TEXT("ProfileSmoke"));Password->SetText(TEXT("TemporaryProfileSmoke!"));RememberPassword=true;
        if(!Require(SaveSelection(),TEXT("save profile failed")))return false;
    } else if(SmokeStep==1){
        SelectServer(Profiles.Servers[0].Id);
        if(!Require(Password->Value().IsEmpty()&&AccountOptions.IsEmpty(),TEXT("Icefire credential leaked into AC2E form")))return false;
    } else if(SmokeStep==2){
        SelectServer(Profiles.Servers[1].Id);
        if(!Require(Account->Value()==TEXT("ProfileSmoke")&&Password->Value()==TEXT("TemporaryProfileSmoke!"),TEXT("saved account did not restore")))return false;
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-icefire-profile.png"),true,false);
        Host->SetText(TEXT("different.example"));
    } else if(SmokeStep==3){
        if(!Require(Password->Value().IsEmpty(),TEXT("edited server retained saved secret")))return false;
        SelectServer(Profiles.Servers[1].Id);RemoveAccount();
        if(!Require(Profiles.Accounts.IsEmpty()&&Password->Value().IsEmpty(),TEXT("account removal failed")))return false;
        IFileManager::Get().Delete(*File);
        UE_LOG(LogTemp,Display,TEXT("AC2 profiles smoke PASS: sized controls, saved account, server isolation, endpoint edit and removal"));
        // Exercise actual map travel in both directions, without contacting a
        // public server or touching a player's account. Only the bridge's local
        // connection setup is needed to verify it receives the new DAT path.
        const FString Original=Mode->DatDirectory,Alternate=Profiles.Servers[1].DatDirectory;
        if(!Require(!FPaths::IsSamePath(Original,Alternate),TEXT("reload smoke requires two distinct DAT sets")))return false;
        auto* Instance=Mode->GetGameInstance<UAC2GameInstance>();ProfileReloadSmoke=Instance;
        FAC2ConnectionSettings Settings;Settings.Host=TEXT("127.0.0.1");Settings.Port=59876;Settings.Account=TEXT("ProfileSmoke");
        Settings.DirectLogin=true;Settings.Protocol=TEXT("7160");Settings.DirectPassword=TEXT("TemporaryProfileSmoke!");Settings.DatDirectory=Alternate;
        TWeakObjectPtr<AAC2GameMode> Previous=Mode;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Owner=TWeakObjectPtr<UAC2GameInstance>(Instance),Previous,Original,Alternate,Phase=0,Started=FPlatformTime::Seconds()](float) mutable {
            auto* GI=Owner.Get();auto* World=GI?GI->GetWorld():nullptr;
            auto* M=World?Cast<AAC2GameMode>(World->GetAuthGameMode()):nullptr;
            auto Fail=[](){UE_LOG(LogTemp,Error,TEXT("AC2 profiles DAT reload failed"));FPlatformMisc::RequestExitWithStatus(false,1);return false;};
            if(!GI||FPlatformTime::Seconds()-Started>60)return Fail();
            if(!M||M==Previous.Get()||GI->PendingLogin.IsSet()||!M->HasLastLogin)return true;
            const FString& Expected=Phase==0?Alternate:Original;
            if(!M->CharacterDataReady||!M->Network||!FPaths::IsSamePath(M->DatDirectory,Expected)||
               !FPaths::IsSamePath(M->LastLogin.DatDirectory,Expected)||!M->LastLogin.DirectPassword.IsEmpty()||
               !M->LastLogin.Ticket.IsEmpty()||World->URL.ToString().Contains(TEXT("TemporaryProfileSmoke!")))return Fail();
            UE_LOG(LogTemp,Display,TEXT("AC2 profiles DAT reload PASS: %s"),Phase==0?TEXT("Icefire"):TEXT("AC2E"));
            if(Phase++==1){M->Network.Reset();FPlatformMisc::RequestExit(false);return false;}
            Previous=M;Started=FPlatformTime::Seconds();
            FAC2ConnectionSettings Next;Next.Host=TEXT("127.0.0.1");Next.Port=59876;Next.Account=TEXT("ProfileSmoke");
            Next.DirectLogin=true;Next.Protocol=TEXT("7148");Next.DirectPassword=TEXT("TemporaryProfileSmoke!");Next.DatDirectory=Original;
            M->Connect(Next);Next.DirectPassword.Reset();return true;
        }),.1f);
        Mode->Connect(Settings);Settings.DirectPassword.Reset();return false;
    }
    ++SmokeStep;return true;
}
bool SAC2LoginUI::SmokeLogin()
{
    FString Secret=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_TEST_PASSWORD"));
    if(Secret.IsEmpty()||!Mode.IsValid()||Mode->InWorld()){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    auto Fail=[](const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("AC2 login smoke: %s"),Why);FPlatformMisc::RequestExitWithStatus(false,1);return false;};
    const bool Register=!FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_LOGIN_REGISTER_SMOKE")).IsEmpty();
    const bool DirectSmoke=FParse::Param(FCommandLine::Get(),TEXT("AC2DirectLoginSmoke"));
    auto Center=[](const TSharedPtr<SWidget>& W){const auto& G=W->GetCachedGeometry();return G.LocalToAbsolute(G.GetLocalSize()*.5);};
    if(SmokeStep==2 && !DirectSmoke){
        if(Busy){if(++SmokeWait<20)return true;return Fail(TEXT("account response timed out"));}
        if(!Status.Contains(TEXT("incorrect"))||!ConnectButton->IsEnabled())return Fail(TEXT("wrong-password feedback or retry missing"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-wrong-password.png"),true,false);
        SmokeStep=3;return true;
    }
    for(int32 I=0;I<Buttons.Num();++I)
        if(!Center(Buttons[I]).Equals(Center(ButtonLabels[I]),1.))return Fail(TEXT("button caption is not centered"));
    if(SmokeStep==0)SmokeButtonCenter=Center(ConnectButton);
    else if(!DirectSmoke) {
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
    if(DirectSmoke){
        if(SmokeStep==0){
            if(!DirectLogin && !Click(DirectControl.ToSharedRef()))return Fail(TEXT("direct login selector did not receive the click"));
            // Bound Slate attributes and the new wrapped introduction settle next frame.
            SmokeStep=1;return true;
        }
        if(!DirectLogin || AccountServiceField->GetVisibility()!=EVisibility::Collapsed)return Fail(TEXT("direct mode did not hide account service"));
        if(SmokeStep==1){
            if(!Click(Password.ToSharedRef()) || !Password->HasKeyboardFocus())return Fail(TEXT("direct password focus"));
            for(TCHAR C:Secret)App.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),0,false));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-direct.png"),true,false);
            SmokeStep=2;return true;
        }
        FPlatformMisc::SetEnvironmentVar(TEXT("AC2_TEST_PASSWORD"),TEXT(""));Secret.Reset();
        if(!Click(ConnectButton.ToSharedRef()) || !Mode->Network)return Fail(TEXT("direct login did not start the bridge"));
        if(!Mode->LastLogin.DirectPassword.IsEmpty() || !Mode->LastLogin.Ticket.IsEmpty())return Fail(TEXT("login defaults retained a credential"));
        if(FParse::Param(FCommandLine::Get(),TEXT("AC2DatUpdateSmoke")))
        {
            DatUpdateSmoke=Mode->GetGameInstance<UAC2GameInstance>();
            const FString Before=Mode->DatDirectory,ProfileFile=Profiles.Filename(),ProfileId=Profiles.SelectedServer;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [Instance=DatUpdateSmoke,Before,ProfileFile,ProfileId,Started=FPlatformTime::Seconds()](float){
                auto* World=Instance.IsValid()?Instance->GetWorld():nullptr;
                auto* Current=World?Cast<AAC2GameMode>(World->GetAuthGameMode()):nullptr;
                if(FPlatformTime::Seconds()-Started>180){UE_LOG(LogTemp,Error,TEXT("AC2 DAT update smoke timed out"));FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                if(!Current||!Current->Network||!Current->Network->IsSelection()||Current->Characters.Characters.IsEmpty())return true;
                FAC2LoginProfiles Saved;FString Error;
                bool Good=!FPaths::IsSamePath(Before,Current->DatDirectory)&&Current->CharacterDataReady&&
                    Current->LastLogin.DirectPassword.IsEmpty()&&Current->LastLogin.Ticket.IsEmpty()&&
                    Saved.Load(ProfileFile,Error);
                const auto* Profile=Saved.Servers.FindByPredicate([&](const auto& P){return P.Id==ProfileId;});
                Good=Good&&Profile&&FPaths::IsSamePath(Profile->DatDirectory,Current->DatDirectory);
                UE_LOG(LogTemp,Display,TEXT("AC2 DAT update: download, verified snapshot, map reload, automatic login and saved profile %s"),Good?TEXT("PASS"):TEXT("FAIL"));
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Login-updated-icefire.png"),true,false);
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Good](float){FPlatformMisc::RequestExitWithStatus(false,Good?0:1);return false;}),2.f);
                return false;
            }),.1f);
        }
        else if(FParse::Param(FCommandLine::Get(),TEXT("AC2DirectWorldSmoke")))
        {
            // Explicit opt-in: enter an existing character only. Never create,
            // delete, fight, move, or change the live account's inventory.
            TWeakObjectPtr<AAC2GameMode> Owner=Mode;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [Owner,Started=FPlatformTime::Seconds(),StableSince=0.0,SelectionSince=0.0,Entered=false,UIStep=0,NextUI=0.0](float) mutable {
                const double Now=FPlatformTime::Seconds();
                auto* M=Owner.Get();auto* N=M?M->Network.Get():nullptr;
                if(!N || Now-Started>90 || N->Phase==TEXT("disconnected"))
                {UE_LOG(LogTemp,Error,TEXT("AC2 direct world smoke: entry timed out or disconnected"));FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                if(!Entered && N->IsSelection() && !M->Characters.Characters.IsEmpty())
                {
                    if(SelectionSince==0)SelectionSince=Now;
                    if(Now-SelectionSince<1)return true;
                    Entered=true;N->Enter(M->Characters.Characters[0].Id);
                }
                bool PortalActive=M->WantsPortalSpace();
                for(TActorIterator<AAC2PortalSpace> It(M->GetWorld());It;++It)PortalActive|=It->Active();
                auto* PC=M->GetWorld()->GetFirstPlayerController();
                auto* Player=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
                if(N->IsWorld() && Player && !Player->IsHeld() && !PortalActive)
                {if(StableSince==0)StableSince=Now;}
                else if(StableSince>0)
                {UE_LOG(LogTemp,Error,TEXT("AC2 direct world smoke: portal or movement hold restarted"));FPlatformMisc::RequestExitWithStatus(false,1);return false;}
                if(StableSince==0 || Now-StableSince<15)return true;
                if(FParse::Param(FCommandLine::Get(),TEXT("AC2ServerUISmoke")) && UIStep<10){
                    if(Now<NextUI)return true;
                    auto* H=PC?Cast<AAC2HUD>(PC->GetHUD()):nullptr;FString Error;
                    if(!H || !H->GameUI || !H->GameUI->SmokeServerUI(UIStep,Error)){
                        UE_LOG(LogTemp,Error,TEXT("AC2 server UI step %d FAIL: %s"),UIStep,*Error);
                        N->Logout();FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExitWithStatus(false,1);return false;}),3.f);return false;
                    }
                    UE_LOG(LogTemp,Display,TEXT("AC2 server UI step %d PASS"),UIStep);
                    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/AC2ServerUI-%d.png"),UIStep),true,false);
                    ++UIStep;NextUI=Now+3;return true;
                }
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Direct-world.png"),true,false);
                UE_LOG(LogTemp,Display,TEXT("AC2 direct world smoke: world visible, player released and portal inactive for 15 seconds"));
                N->Logout();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExit(false);return false;}),5.f);
                return false;
            }),.1f);
        }
        else FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){FPlatformMisc::RequestExit(false);return false;}),2.f);
        UE_LOG(LogTemp,Display,TEXT("AC2 direct login: selector, masked entry, hidden account URL and Connect passed"));
        return false;
    }
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
    FAC2ConnectionSettings Settings;Settings.Host=Host->Value().TrimStartAndEnd();Settings.Account=Account->Value().TrimStartAndEnd();Settings.Port=FCString::Atoi(*Port->Value().TrimStartAndEnd());
    Settings.DirectLogin=DirectLogin;Settings.Protocol=Protocol->Value().TrimStartAndEnd();
    Settings.DatDirectory=ServerFields().DatDirectory;
    FString DataError;
    if(!FAC2LoginProfiles::ValidateData(Settings.DatDirectory,DataError)){SetStatus(DataError);return;}
    if(!DirectLogin)Settings.Account=Settings.Account.ToLower();
    if(!Matches(Settings.Account,TEXT("[a-zA-Z0-9_]{1,32}")))
    {SetStatus(TEXT("Account must be 1-32 letters, numbers or underscores. Spaces are not allowed."));return;}
    if(!Matches(Settings.Host,TEXT("[a-zA-Z0-9.-]+"))||!Matches(Port->Value().TrimStartAndEnd(),TEXT("[0-9]{1,5}"))||Settings.Port<1||Settings.Port>65534)
    {SetStatus(TEXT("Enter a valid game server and port (1-65534)."));return;}
    if(Settings.Protocol!=TEXT("7148") && Settings.Protocol!=TEXT("7160") && Settings.Protocol!=TEXT("7230"))
    {SetStatus(TEXT("Client version must be 7148, 7160 or 7230."));return;}
    FString URL=Endpoint->Value().TrimStartAndEnd();
    Mode->LastAccountEndpoint=URL;
    if(DirectLogin){
        if(Password->Value().IsEmpty()){SetStatus(TEXT("Enter the password required by the game server."));return;}
        if(!SaveSelection())return;
        Settings.ServerProfileId=Profiles.SelectedServer;Settings.ProfileFile=Profiles.Filename();
        Settings.DirectPassword=Password->Value();Password->SetText(TEXT(""));PreauthenticatedTicket.Reset();
        Busy=true;Mode->Connect(Settings);Settings.DirectPassword.Reset();return;
    }
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
    if(!SaveSelection())return;
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
