#pragma once
#include "AC2Assets.h"
#include "AC2Chat.h"
#include "AC2Font.h"
#include "AC2NativeUI.h"
#include "AC2MapUI.h"
#include "Widgets/SCompoundWidget.h"
class AAC2GameMode;
class AAC2HUD;

// Native in-game presentation. World controls remain on the player controller; chat owns input only
// while its entry is active. All text and image resources are decoded from the installed DATs.
class SAC2GameUI : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SAC2GameUI) : _Frontend(false)
    {
    }
    SLATE_ARGUMENT(AAC2GameMode *, Mode)
    SLATE_ARGUMENT(bool, Frontend)
    SLATE_EVENT(FSimpleDelegate, OnClose)
    SLATE_END_ARGS() void Construct(const FArguments &Args);
    virtual ~SAC2GameUI();
    virtual void Tick(const FGeometry &, double, float) override;
    virtual int32 OnPaint(const FPaintArgs &, const FGeometry &, const FSlateRect &,
                          FSlateWindowElementList &, int32, const FWidgetStyle &, bool) const override;
    virtual FReply OnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonUp(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonDoubleClick(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseMove(const FGeometry &, const FPointerEvent &) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
    virtual FReply OnMouseWheel(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnKeyDown(const FGeometry &, const FKeyEvent &) override;
    void BeginChat();
    void EndChat();
    bool ChatActive() const
    {
        return Editing;
    }
    void Submit(const FString &Text);
    void ReceiveNetworkChat(const FString& Text) { Log.Add(EAC2ChatType::Local, Text); Scroll=0; }
    bool Smoke(FString &Error);
    void SmokeDraft();
    void ToggleInventory();
    void ToggleStats();
    void ToggleMap();
    void ToggleOptions();
    void RestoreOptions();
    bool OptionsOpen() const {return ShowOptions;}
    bool SmokeOptions(int32 Step,FString& Error);
    bool OptionsClosePoint(FVector2D &Point) const;
    bool MapSmokeStep(int32 Step, FString &Error) { return MapUI.SmokeStep(Step, Error); }
    bool SmokeNetworkRadar(const FString &Id, FString &Error) { return MapUI.SmokeNetworkTarget(Id, Error); }
    bool ClosePanels();
    bool InventoryOpen() const
    {
        return ShowInventory;
    }
    bool SmokeEquip();
    bool HasChatFocus() const
    {
        return Editing && Entry && Entry->HasKeyboardFocus();
    }

  private:
    bool Frontend = false;
    FSimpleDelegate OnClose;
    TWeakObjectPtr<AAC2GameMode> Mode;
    TUniquePtr<FAC2AssetLibrary> Assets;
    mutable FAC2Fonts Fonts;
    FAC2NativeUI Native;
    FAC2MapUI MapUI;
    bool MapCaptured = false;
    TSharedPtr<FAC2Font> BodyFont, TitleFont, ChatFont;
    TSharedPtr<SAC2TextEntry> Entry;
    FAC2ChatLog Log;
    FString LastNotice;
    mutable TMap<FName, FSlateRect> Buttons;
    TMap<FName, FName> ButtonActions;
    mutable TMap<FName, FString> Hints;
    FVector2D Mouse = FVector2D(-100, -100);
    int32 Filter = 0, Scroll = 0;
    mutable int32 MaxScroll = 0;
    TWeakObjectPtr<class AAC2Portrait> Portrait;
    FSlateBrush PortraitBrush;
    bool ShowInventory = false, ShowStats = false;
    bool ShowOptions=false;
    int32 OptionsTab=0,OptionsScroll=0;
    TArray<float> OptionValues,AppliedOptions;
    TMap<int32,FSlateRect> OptionSliders;
    int32 DraggingOption=INDEX_NONE;
    void DragOption(float X);
    void BuildOptions();
    void ApplyOptions();
    bool OptionAction(FName Name);
    int32 Pack = 0, PackRow = 0, SelectedItem = INDEX_NONE;
    FName PressedAction;
    struct FIcon
    {
        FIntRect Rect, Clip;
        uint32 Image;
    };
    TArray<FIcon> Icons;
    void Rebuild();
    void UpdatePortrait();
    void EquipSelected();
    void SelectItem(int32 Index);
    void Bind(const TCHAR *Control, FName Action);
    bool Editing = false, PreviousCursor = false, HealthNumbers = true, VigorNumbers = true;
    float Scale = 1;
    FVector2D Size = FVector2D(1280, 720);
    FSlateRect ChatRect() const;
    void Action(FName Name);
    bool VisibleWorld() const;
};
