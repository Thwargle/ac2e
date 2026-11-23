#pragma once
#include "AC2Assets.h"
#include "AC2Chat.h"
#include "AC2Aliases.h"
#include "AC2Font.h"
#include "AC2NativeUI.h"
#include "AC2Network.h"
#include "AC2MapUI.h"
#include "AC2Cursor.h"
#include "AC2SkillData.h"
#include "AC2ShortcutData.h"
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
    virtual FReply OnPreviewMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonUp(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseButtonDoubleClick(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnMouseMove(const FGeometry &, const FPointerEvent &) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override;
    void CancelCameraGesture();
    virtual FReply OnMouseWheel(const FGeometry &, const FPointerEvent &) override;
    virtual FReply OnKeyDown(const FGeometry &, const FKeyEvent &) override;
    virtual bool SupportsKeyboardFocus() const override { return OptionConfirmationOpen() || OpenOptionChoice!=INDEX_NONE; }
    bool OptionConfirmationOpen() const { return OptionConfirmation!=EOptionConfirmation::None; }
    void BeginChat();
    void EndChat();
    bool ChatActive() const
    {
        return Editing;
    }
    void Submit(const FString &Text);
    void ReceiveNetworkChat(const FString& Text,uint32 Type=0x10) {
        if(Text.IsEmpty())return;
        Log.Add(EAC2ChatType::Local,Text);Log.Lines.Last().NativeType=Type;Scroll=0;
    }
    bool Smoke(FString &Error);
    bool SmokeServerUI(int32 Step,FString& Error);
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
    bool SmokeCraftInput(int32 Step,FString& Error);
    bool SmokeJournalInput(int32 Step,FString& Error);
    bool SmokeCooldownInput(int32 Step,FString& Error);
    bool SmokeDamageText(FString& Error);
    bool SmokeLootInput(bool Take,FString& Error);
    void ActivateShortcut(int32 Slot);
    bool HandleShortcutAction(uint32 Action);
    bool SmokeShortcuts(FString& Error);
    bool SmokeShortcutNumbers(int32 Step,FString& Error);
    bool SmokeShortcutKey(FKey Key,bool Shift=false,bool Control=false);
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
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2LiveExaminationTest;
    friend class FAC2ItemShortcutsTest;
    friend class FAC2TextShortcutsTest;
    friend class FAC2AliasesTest;
    friend class FAC2ShortcutSyncTest;
    friend class FAC2GameOptionsSyncTest;
    friend class FAC2OptionChoiceTest;
    friend class FAC2SkillEquipmentTest;
    friend class FAC2RecipeShortcutsTest;
    friend class FAC2ConcurrentWindowsTest;
    friend class FAC2ShortcutRowsTest;
    friend class FAC2ShortcutKeysTest;
    friend class FAC2OptionsTest;
    friend class FAC2GeneralChatTest;
    friend class FAC2FriendsTest;
    friend class FAC2InventoryTransferTest;
#endif
    bool ShortcutInputBlocked() const;
    void SelectShortcutSet(int32 Set);
    bool Frontend = false;
    FSimpleDelegate OnClose;
    TWeakObjectPtr<AAC2GameMode> Mode;
    TUniquePtr<FAC2AssetLibrary> Assets;
    mutable FAC2Fonts Fonts;
    FAC2NativeUI Native;
    uint32 DragWindow=0;
    FVector2D WindowPress;
    FSlateRect FpsBounds() const;
    void FinishFpsDrag();
    bool ShowFps=false, DraggingFps=false;
    FVector2D FpsPosition{12,52}, FpsGrab=FVector2D::ZeroVector;
    double FpsInterval=0;
    int32 FpsFrames=0, MeasuredFps=0;
    FIntPoint WindowOffset;
    bool BeginWindowDrag(bool PanelsOnly=false);
    void EndWindowDrag();
    uint32 ResizeWindow=0;
    uint8 ResizeEdges=0;
    FIntRect ResizeStart;
    FIntPoint ResizeMin,ResizeMax;
    bool WindowResizeAt(FVector2D Point,uint32& Root,uint8& Edges) const;
    bool BeginWindowResize();
    void ResizeWindowTo(FVector2D Point);
    void EndWindowResize();
    FAC2MapUI MapUI;
    bool MapCaptured = false;
    bool RotatingPortrait=false;
    FVector2D PortraitPointer;
    TSharedPtr<FAC2Font> BodyFont, TitleFont, ChatFont;
    TSharedPtr<SAC2TextEntry> Entry;
    TSharedPtr<SAC2TextEntry> FellowshipEntry;
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
    FIntRect ExamineBar,ExamineThumb;
    float ExamineGrab=0;
    bool DraggingExamine=false;
    uint32 ExaminedSkill=0, ExaminedRecipe=0;
    void DragExamine(float Y);
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
    bool ShowSocial=false;
    int32 FellowshipLootChoice=0;
    bool FellowshipShareXpChoice=false;
    FIntRect FellowshipLootAnchor,FriendModeAnchor;
    static constexpr int32 FriendModeMenu=1001;
    FString FriendModeLabel(int32 Row);
    FString FellowshipLootLabel(int32 Method) const;
    FString SelectedFellow;
    FString SelectedFriend;
    void BuildFriends(const FIntRect& Box,const FIntRect& Clip,uint32 Parent);
    int32 SocialTab=0;
    FString SelectedLiege;
    void BuildAllegiance(const FIntRect& Box,const FIntRect& Clip,uint32 Parent);
    void PrepareSocial();
    void BuildSocial();
    bool SocialAction(FName Name);
    void BindNative(uint32 Id,FName Action);
    void UpdateScrollbar(uint32 Id,int32 Offset,int32 Maximum,FIntRect& Bar,FIntRect& Thumb,FName Up,FName Down);
    int32 JournalScroll=0,QuestTab=0,SkillTab=0,LootRow=0;
    int32 JournalMaxScroll=0;
    FIntRect JournalBar,JournalThumb;
    float JournalGrab=0;
    bool DraggingJournal=false;
    TSet<uint32> ExpandedQuests;
    void DragJournal(float Y);
    bool AllowsPanelButton(FName Name,FVector2D Point) const;
    uint32 SelectedSkill=0,SelectedQuest=0,SkillPanel=0;
    FLinearColor SkillLineColor=FLinearColor::White;
    FAC2SkillData SkillData;
    void LayoutExamination(const TArray<FAC2Network::FExaminationRow>& Nodes,FIntRect List);
    void BuildJournal();
    void BuildCrafting();
    void BuildIngredients();
    bool CraftAction(FName Name);
    uint32 CraftSkill=0x6f0028d7;
    int32 CraftPage=0;
    bool CraftProfessions=true;
    bool CraftOwnsPointer(FVector2D Point) const;
    FString CraftItemName(uint32 Definition);
    void InspectRecipe(uint32 Recipe);
    TArray<FString> RecipeExamination(uint32 Recipe);
    uint32 RecipeIcon(uint32 Recipe) const;
    void BuildStatusIndicators();
    TArray<FString> StatusDescription(const FString& Key) const;
    void BuildSkills();
    FAC2SkillContext CurrentSkills() const;
    TArray<TPair<FVector2D,FVector2D>> SkillLinks;
    FIntRect SkillClip;
    void BuildLoot();
    bool JournalAction(FName Name);
    bool ShowOptions=false;
    int32 OptionsTab=0,OptionsScroll=0,OptionsMaxScroll=0,OptionRowHeight=33;
    FIntRect OptionsList,OptionsBar,OptionsThumb;
    bool DraggingOptionsScroll=false,TestingOptions=false;
    float OptionsGrab=0;
    enum class EOptionConfirmation { None, Apply, Recall, Default, Close };
    EOptionConfirmation OptionConfirmation=EOptionConfirmation::None;
    int32 OptionConfirmationTab=0,OptionDialogNodesStart=MAX_int32;
    TWeakPtr<SWidget> OptionPreviousFocus;
    TArray<float> OptionValues,AppliedOptions;
    TArray<float> SyncedGameOptions;
    FString GameOptionsOwner;
    FGuid LoadedGameOptionsSession;
    uint64 LoadedGameOptionsRevision=0;
    int32 SyncedShortcutRows=1;
    int32 OpenOptionChoice=INDEX_NONE,PressedOptionChoice=INDEX_NONE;
    FIntRect OptionChoiceAnchor,OptionChoicePopup;
    TArray<FIntRect> OptionChoiceItems;
    TWeakPtr<SWidget> OptionChoicePreviousFocus;
    FString ShortcutHeightLabel(int32 Rows);
    void RefreshShortcutHeightOption(bool Force=false);
    void BuildOptionChoiceMenu();
    void CloseOptionChoice();
    bool OptionChoiceMouseDown(FKey Key);
    bool OptionChoiceMouseUp(FKey Key);
    bool ServerOptionsActive() const;
    void SyncGameOptions();
    bool SaveGameOptions();
    TMap<int32,FSlateRect> OptionSliders;
    int32 DraggingOption=INDEX_NONE;
    void DragOption(float X);
    void DragOptionsScroll(float Y);
    void ScrollOptions(int32 Pixels);
    TSharedPtr<SWidget> KeyBindingEditor;
    TWeakObjectPtr<class UGameViewportClient> KeyBindingViewport;
    TWeakPtr<SWidget> KeyBindingPreviousFocus;
    void OpenKeyBindings();
    bool SmokeKeyBindingEditor(FString& Error);
    void CloseKeyBindings();
    void RemoveKeyBindings();
    void BuildOptions();
    void ApplyOptions(int32 Tab);
    void SetRadarCoordinates(bool Show);
    bool OptionsChanged(int32 Tab) const;
    void AskOptionConfirmation(EOptionConfirmation Question);
    void AnswerOptionConfirmation(bool Yes);
    void BuildOptionConfirmation();
    void CloseOptions();
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
    FAC2Shortcut DragBinding;
    int32 DragShortcut=INDEX_NONE,ShortcutSet=0,ShortcutRows=1;
    FAC2Shortcut Shortcuts[100];
    FString ShortcutOwner;
    FGuid LoadedShortcutSession;
    uint64 LoadedShortcutRevision=0;
    bool ImportLocalShortcuts=false;
    void SyncShortcuts();
    FAC2Aliases Aliases;
    FString AliasOwner;
    bool TestingAliases=false;
    void LoadAliases();
    void SaveAliases();
    bool HandleAliasCommand(const FAC2AliasRequest& Request);
    bool ExpandAliases(const FString& Text,FString& Expanded);
    bool TestingShortcuts=false;
    double SmokeCooldownStamp=-1;
    bool ShortcutLocked=true,ShortcutDragStarted=false,ShortcutDragAttempted=false;
    bool ShortcutInstantScroll=true,ShowShortcutNumbers=true;
    void SetShortcutNumbers(bool Show);
    FAC2ShortcutScroll ShortcutScroll;
    double ShortcutTime=0;
    int32 ShortcutTopRow=0,ShortcutRowHeight=32;
    TMap<FName,int32> ShortcutHitIndices;
    void LoadShortcuts();
    void SaveShortcuts();
    void BuildShortcuts();
    int32 ShortcutHitIndex(FName Key) const;
    void UseShortcutBinding(int32 Index);
    bool ShortcutAllowed(uint32 Skill) const;
    FString SkillEquipmentHint(uint32 Skill,bool& Blocked);
    bool RecipeShortcutAllowed(uint32 Recipe) const;
    int32 ShortcutItemIndex(const FString& Id) const;
    TArray<FAC2IconLayer> ShortcutLayers(const FAC2Shortcut& Binding) const;
    bool FinishShortcutDrag();
    void UpdateShortcutDrag(bool ShiftDown);
    void AddSkillShortcut(uint32 Skill);
    void InspectShortcut(int32 Slot);
    struct FIcon
    {
        FIntRect Rect, Clip;
        uint32 Image;
        int32 Quantity=1;
        FLinearColor Tint=FLinearColor::White;
        int32 NativeNode=INDEX_NONE;
        uint32 Window=0;
    };
    TArray<FIcon> Icons;
    void Rebuild();
    TArray<uint32> WindowStack;
    TMap<FName,uint32> ButtonWindows;
    void BuildWindowContent(uint32 Window,TFunction<void()> Build);
    uint32 WindowAt(FVector2D Point) const;
    void RaiseWindow(uint32 Window);
    bool WindowOwns(uint32 Window,FVector2D Point) const {return WindowAt(Point)==Window;}
    struct FJournalView {int32 Scroll=0,Maximum=0;FIntRect Bar,Thumb;};
    TMap<uint32,FJournalView> JournalViews;
    uint32 JournalWindow=0;
    void SelectJournalWindow(uint32 Window);
    void SaveJournalWindow();
    int32 IngredientScroll=0,IngredientMaxScroll=0;
    FIntRect IngredientBar,IngredientThumb;
    float IngredientGrab=0;
    bool DraggingIngredients=false;
    void DragIngredients(float Y);

    const TCHAR* LeftPanelRoot() const;
    const TCHAR* RightPanelRoot() const;
    void BuildShop();
    bool ShopAction(FName Name);
    int32 SelectedOffer=INDEX_NONE,ShopPage=0;
    FString LastShopVendor;
    void UpdatePortrait();
    void EquipSelected();
    void SelectItem(int32 Index);
    void Bind(const TCHAR *Control, FName Action);
    bool Editing = false, PreviousCursor = false, HealthNumbers = true, VigorNumbers = true, FocusNumbers = true;
    float Scale = 1;
    FVector2D Size = FVector2D(1280, 720);
    FSlateRect ChatRect() const;
    void Action(FName Name);
    bool VisibleWorld() const;
};
