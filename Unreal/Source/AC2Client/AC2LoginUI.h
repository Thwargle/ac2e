#pragma once
#include "AC2Font.h"
#include "AC2Assets.h"
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
    FSlateBrush Background,ButtonArt;
    FButtonStyle ButtonStyle;
    TSharedPtr<SAC2TextEntry> Account,Password,Host,Port,Endpoint;
    TSharedPtr<class SButton> ConnectButton;
    TSharedPtr<SAC2Text> StatusText;
    TArray<TSharedPtr<class SButton>> Buttons;
    TArray<TSharedPtr<SAC2Text>> ButtonLabels;
    FString Status;
    FString PreauthenticatedTicket,PreauthenticatedAccount,PreauthenticatedHost;
    int32 PreauthenticatedPort=0;
    bool Busy=false;
    float SmokeTime=0;
    bool SmokeStarted=false;
    int32 SmokeStep=0;
    int32 SmokeWait=0;
    FVector2D SmokeButtonCenter=FVector2D::ZeroVector;
    bool SmokeLogin();
    void SetStatus(FString Message);
    void Login();
};
