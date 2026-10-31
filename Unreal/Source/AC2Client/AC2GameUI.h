#pragma once
#include "AC2Assets.h"
#include "AC2Chat.h"
#include "AC2Font.h"
#include "AC2NativeUI.h"
#include "AC2MapUI.h"
#include "AC2Cursor.h"
#include "AC2SkillData.h"
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
    void CancelCameraGesture();
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
    void RefreshInventoryAppearance() {if(ShowInventory)UpdatePortrait();}
    void ToggleStats();
    void ToggleMap();
    void ToggleOptions();
    void RestoreOptions();
    bool OptionsOpen() const {return ShowOptions;}
    bool SmokeOptions(int32 Step,FString& Error);
    bool OptionsClosePoint(FVector2D &Point) const;
    bool MapSmokeStep(int32 Step, FString &Error) { return MapUI.SmokeStep(Step, Error); }
    bool SmokeNetworkRadar(const FString &Id, FString &Error) { return MapUI.SmokeNetworkTarget(Id, Error); }
    bool SmokeCombatInput(int32 Step, FString &Error);
    bool SmokeMovementInput(int32 Step, FString &Error);
    bool SmokeRadarHeading(float Yaw,FString& Error) const { return MapUI.SmokeHeading(Yaw,Error); }
    bool SmokeInventoryInput(int32 Step, FString &Error);
    bool SmokeShopInput(int32 Step,FString& Error);
    bool SmokeJournalInput(int32 Step,FString& Error);
    bool SmokeDamageText(FString& Error);
    bool SmokeLootInput(bool Take,FString& Error);
    void ActivateShortcut(int32 Slot);
    bool SmokeShortcuts(FString& Error);
    bool SmokeWorldTarget(const FString &Id, FString &Error,FKey Button=EKeys::LeftMouseButton,bool DoubleClick=false,bool CenterAim=false);
    bool SmokeMouseControls(int32 Step,const FString& Id,FString& Error);
    FAC2CursorIcon CursorAt(FVector2D ScreenPoint) const;
    void InspectTarget(const FString& Id);
    void ClickWorldCenter(FKey Button,bool DoubleClick=false);
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
    uint32 DragWindow=0;
    FVector2D WindowPress;
    FIntPoint WindowOffset;
    bool BeginWindowDrag(bool PanelsOnly=false);
    void EndWindowDrag();
    FAC2MapUI MapUI;
    bool MapCaptured = false;
    bool RotatingPortrait=false;
    FVector2D PortraitPointer;
    TSharedPtr<FAC2Font> BodyFont, TitleFont, ChatFont;
    TSharedPtr<SAC2TextEntry> Entry;
    FAC2ChatLog Log;
    FString LastNotice;
    mutable TMap<FName, FSlateRect> Buttons;
    TMap<FName, FName> ButtonActions;
    mutable TMap<FName, FString> Hints;
    FVector2D Mouse = FVector2D(-100, -100);
    FString HoverObject, InspectedObject;
    TWeakObjectPtr<class AAC2Mob> HoverMob, InspectedMob;
    double NextHover=0;
    int32 ExamineScroll=0, ExamineMaxScroll=0;
    bool OwnsPointer(FVector2D Point) const;
    bool CameraLeft=false,CameraRight=false,CameraDragging=false,CameraMoved=false;
    bool CameraPrecision=false,CameraRecapturing=false;
    FVector2D CameraPress,CameraScreenPress;
    float CameraTravel=0;
    FReply CameraButtonDown(const FPointerEvent& E);
    FReply CameraButtonUp(const FPointerEvent& E);
    FReply CameraMouseMove(const FPointerEvent& E);
    void UpdateHover(const FGeometry&,double Time);
    void BuildPointerFeedback();
    void BuildWorldText();
    TMap<FString,FSlateRect> WorldNameRects;
    bool DamageNumbers=true,ThirdPartyDamage=false,ShowObjectNames=true;
    float ThirdPartyDamageRange=1;
    bool WorldClick(FKey Button,bool DoubleClick);
    int32 Filter = 0, Scroll = 0;
    mutable int32 MaxScroll = 0;
    FIntRect ChatBar,ChatThumb,PackBar,PackThumb;
    int32 PackMaxScroll=0;
    float PackGrab=0;
    bool DraggingPack=false;
    void DragPack(float Y);
    float ChatGrab=0;
    bool DraggingChat=false;
    void DragChat(float Y);
    TWeakObjectPtr<class AAC2Portrait> Portrait;
    FSlateBrush PortraitBrush;
    bool ShowInventory = false, ShowStats = false;
    bool ShowSkills=false,ShowQuests=false;
    int32 JournalScroll=0,QuestTab=0,SkillTab=0,LootRow=0;
    bool AllowsPanelButton(FName Name,FVector2D Point) const;
    uint32 SelectedSkill=0,SelectedQuest=0,SkillPanel=0;
    FLinearColor SkillLineColor=FLinearColor::White;
    FAC2SkillData SkillData;
    void BuildJournal();
    void BuildSkills();
    TArray<TPair<FVector2D,FVector2D>> SkillLinks;
    FIntRect SkillClip;
    void BuildLoot();
    bool JournalAction(FName Name);
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
    FString SelectedNetworkItem;
    int32 PointerNodesStart=MAX_int32;
    int32 ShopNodesStart=MAX_int32,ShopIconsStart=MAX_int32;
    bool ShopOwnsPointer(FVector2D Point) const;
    bool ShopButton(FName Name) const;
    int32 PackStart() const {return ModeHasNetwork ? (Pack?42+(Pack-1)*12:0) : Pack*24;}
    int32 PackCount() const {return ModeHasNetwork ? (Pack?12:42) : 24;}
    bool ModeHasNetwork=false;
    FName PressedAction;
    FVector2D ItemPressPoint;
    FString DragItem;
    uint32 DragSkill=0;
    int32 DragShortcut=INDEX_NONE,ShortcutSet=0;
    uint32 Shortcuts[100]={};
    FString ShortcutOwner;
    bool TestingShortcuts=false;
    void LoadShortcuts();
    void SaveShortcuts();
    void BuildShortcuts();
    bool ShortcutAllowed(uint32 Skill) const;
    struct FIcon
    {
        FIntRect Rect, Clip;
        uint32 Image;
        int32 Quantity=1;
        FLinearColor Tint=FLinearColor::White;
        int32 NativeNode=INDEX_NONE;
    };
    TArray<FIcon> Icons;
    void Rebuild();
    void BuildShop();
    bool ShopAction(FName Name);
    int32 SelectedOffer=INDEX_NONE,ShopPage=0;
    FString LastShopVendor;
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
