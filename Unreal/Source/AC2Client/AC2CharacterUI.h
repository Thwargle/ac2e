#pragma once
#include "AC2CharacterData.h"
#include "AC2Assets.h"
#include "AC2UILayout.h"
#include "AC2Font.h"
#include "AC2NativeUI.h"
#include "AC2Cursor.h"
#include "Widgets/SCompoundWidget.h"

class AAC2GameMode;
class AAC2Portrait;

class SBox;
class SAC2CharacterUI : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2CharacterUI)
    {
    }
    SLATE_ARGUMENT(AAC2GameMode *, Mode) SLATE_END_ARGS() void Construct(const FArguments &Args);
    virtual ~SAC2CharacterUI();
    enum class EScreen
    {
        Closed,
        Login,
        Select,
        Credits,
        Create,
        Inventory,
        Stats
    };
    void Open(EScreen Screen,const FString& InitialMessage=FString());
    void Close();
    void NetworkMessage(const FString& Text) { Message=Text; Refresh(); }
    void NetworkEntered() { Screen=EScreen::Stats; Close(); }
    bool IsOpen() const
    {
        return Screen != EScreen::Closed;
    }
    bool IsCreation() const
    {
        return Screen == EScreen::Create || Screen == EScreen::Select || Screen==EScreen::Login || Screen==EScreen::Credits;
    }
    bool PreviewReady() const;
    FAC2CursorIcon CursorAt(FVector2D ScreenPoint) const;
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }
    virtual FReply OnKeyDown(const FGeometry &, const FKeyEvent &) override;
    virtual void Tick(const FGeometry&, double, float) override;
    void Shutdown();
    bool SmokeCreate(FString &Error);
    void SmokeEnter(){Enter();}
    EScreen CurrentScreen() const {return Screen;}
    bool SmokePreview(int32 Index);
    bool SmokeAppearanceAudit(FString &Error)
    {
        return SmokeAppearance(Error);
    }
    void SmokeCreatorPage(int32 Tab, bool Confirm = false);
    bool SmokeFraming(int32 Avatar, bool Tall, bool Head, FString& Error);
    bool AuditPortrait(FString& Error) const;
    bool SmokeEyes(bool Last);
    bool SmokeSelection(int32 Step, FString &Error);
    bool SmokeCreatorDrafts(FString& Error);
    bool SmokeAvatarPicking(int32 Step, FString& Error);
    bool WaitingForAvatarAnimation() const { return PendingAvatar!=INDEX_NONE; }

  private:
    TWeakObjectPtr<AAC2GameMode> Mode;
    TWeakObjectPtr<AAC2Portrait> Portrait, Lineup, Selection;
    FSlateBrush SelectionBrush;
    TMap<uint32,TWeakObjectPtr<AAC2Portrait>> SceneOverlays;
    TMap<uint32,FSlateBrush> SceneOverlayBrushes;
    void UpdateSceneOverlays();
    TSharedPtr<SWidget> CreatorView;
    TSharedPtr<class SAC2GameUI> SelectionOptions;
    bool ShowSelectionOptions = false;
    FString DeletePending;
    FString CreditsText;
    double CreditsStart=0;
    void BuildCredits();
    void SelectCharacter(int32 Index);
    int32 CharacterAt(FVector2D Point) const;
    void BuildSelectionHint();
    TArray<FIntRect> SelectionRows;
    FVector2D SelectionPointer = FVector2D(-1,-1);
    FSlateBrush LineupBrush;
    int32 HoveredAvatar = INDEX_NONE;
    int32 PendingAvatar = INDEX_NONE;
    TUniquePtr<FAC2AssetLibrary> Assets;
    TMap<uint32, TSharedPtr<FSlateBrush>> Brushes;
    FSlateBrush PortraitBrush;
    TSharedPtr<SBox> Content, PageSize;
    FAC2UILayout ModifyLayout, ChooseLayout, ConfirmLayout;
    FAC2NativeUI CreatorLayout;
    void BuildCreatorLayout();
    void CreatorAction(uint32 Id);
    void CreatorSlider(uint32 Id, float Value);
    TMap<uint32, int32> Appearance;
    TMap<uint32, FAC2CharacterDraft> CreatorDrafts;
    FAC2CharacterRandom CreatorRandom;
    int32 CreationStep = 0, AppearanceTab = 1;
    float Rotation = 0;
    bool HeadZoom = false;
    void SetChoice(uint32 Type, int32 Index);
    void Randomize(bool All);
    void SetCreatorStep(int32 Step);
    bool SmokeAppearance(FString &Error);

    TSharedPtr<SAC2TextEntry> NameEntry;
    FAC2Fonts Fonts;
    EScreen Screen = EScreen::Closed;
    uint32 Race = 1, Sex = 4096;
    int32 HeightIndex = 0, BuildIndex = 0, Page = 0;
    FString DraftName, Message;
    const FSlateBrush *Art(uint32 Id);
    TSharedRef<SWidget> Creation();
    void Refresh();
    TSharedRef<SWidget> Login();
    void PreviewDraft();
    void ResetAppearance();
    void Create();
    void Enter();
    bool Persist(const FAC2CharacterStore &Previous);
};
