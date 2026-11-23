#pragma once
#include "AC2Font.h"
#include "AC2Assets.h"
#include "AC2LoginProfiles.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
class AAC2GameMode;
class SAC2LoginUI : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAC2LoginUI){}
    SLATE_ARGUMENT(AAC2GameMode*,Mode)
    SLATE_ARGUMENT(FString,Message)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SAC2LoginUI();
    static bool ValidEndpoint(const FString& URL);
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override;
private:
    TWeakObjectPtr<AAC2GameMode> Mode;
    FAC2Fonts Fonts;
    TUniquePtr<FAC2AssetLibrary> Assets;
    FSlateBrush Background;
    FButtonStyle ProfileButtonStyle,LoginButtonStyle;
    TSharedPtr<SAC2TextEntry> Account,Password,Host,Port,Endpoint,Protocol;
    TSharedPtr<SAC2TextEntry> ServerName,DatFolder;
    FAC2LoginProfiles Profiles;
    TArray<TSharedPtr<FString>> ServerOptions,AccountOptions;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> ServerList,AccountList;
    bool RememberPassword=false,SelectingProfile=false;
    FString LoadedCredentialIdentity;
    FAC2ServerProfile ServerFields() const;
    void RefreshProfiles();
    void SelectServer(const FString& Id);
    void SelectAccount(const FString& Id);
    bool SaveServer();
    void SaveAccount();
    void RemoveServer();
    void RemoveAccount();
    void NewServer();
    void NewAccount();
    bool SaveSelection();
    TSharedPtr<class SCheckBox> DirectControl;
    TSharedPtr<SWidget> AccountServiceField;
    TSharedPtr<class SButton> ConnectButton;
    TSharedPtr<SAC2Text> StatusText;
    TArray<TSharedPtr<class SButton>> Buttons;
    TArray<TSharedPtr<SAC2Text>> ButtonLabels;
    FString Status;
    FString PreauthenticatedTicket,PreauthenticatedAccount,PreauthenticatedHost;
    int32 PreauthenticatedPort=0;
    bool Busy=false;
    bool DirectLogin=false;
    float SmokeTime=0;
    bool SmokeStarted=false;
    int32 SmokeStep=0;
    int32 SmokeWait=0;
    FVector2D SmokeButtonCenter=FVector2D::ZeroVector;
    bool SmokeLogin();
    bool SmokeProfiles();
    void SetStatus(FString Message);
    void Login();
};
