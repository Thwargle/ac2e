#include "AC2GameUI.h"
#include "AC2CharacterUI.h"
#include "AC2Explorer.h"
#include "AC2Mob.h"
#include "AC2GameMode.h"
#include "AC2Portrait.h"
#include "AC2WorldGrid.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCanvas.h"
#include "Misc/ConfigCacheIni.h"
#include "Brushes/SlateColorBrush.h"

namespace
{
uint32 Token(const TCHAR *Name)
{
    return FAC2UILayout::Token(Name);
}
AAC2PlayerController *Controller(const TWeakObjectPtr<AAC2GameMode> &Mode)
{
    return Mode.IsValid() ? Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController())
                          : nullptr;
}
bool Inside(const FSlateRect &R, FVector2D P)
{
    return P.X >= R.Left && P.X < R.Right && P.Y >= R.Top && P.Y < R.Bottom;
}
FSlateRect SlateRect(FIntRect R)
{
    return FSlateRect(R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
}
const TCHAR *SlotNames[] = {TEXT("Head"),          TEXT("Torso"),         TEXT("Back"),
                            TEXT("Legs"),          TEXT("Feet"),          TEXT("Hands"),
                            TEXT("PrimaryHand"),   TEXT("SecondaryHand"), TEXT("PrimaryRing"),
                            TEXT("SecondaryRing"), TEXT("Necklace")};
const TCHAR *PackNames[] = {TEXT("PackMainContainer"), TEXT("PackOneContainer"), TEXT("PackTwoContainer"),
                            TEXT("PackThreeContainer")};
const TCHAR *PackButtons[] = {TEXT("PackMainButton"), TEXT("PackOneButton"), TEXT("PackTwoButton"),
                              TEXT("PackThreeButton")};
} // namespace
void SAC2GameUI::Construct(const FArguments &Args)
{
    Mode = Args._Mode;
    Frontend=Args._Frontend; OnClose=Args._OnClose; ShowOptions=Frontend;
    Assets = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    Assets->Open(Mode->DatDirectory, Error);
    Fonts.Open(Mode->DatDirectory, Error);
    Native.SetAudioWorld(Mode->GetWorld());
    SkillData.Open(Mode->DatDirectory);
    if (!Native.Open(Mode->DatDirectory, Error))
        UE_LOG(LogTemp, Error, TEXT("Native HUD layout: %s"), *Error);
    for(const TCHAR* Name:{TEXT("ExternalContainerField"),TEXT("LeftSidePanel"),TEXT("RightSidePanel"),TEXT("InventoryField"),TEXT("SkillField"),TEXT("QuestField"),TEXT("SocialField"),TEXT("ExaminationField"),TEXT("OptionField"),TEXT("ChatWindow1"),TEXT("StatusbarField"),TEXT("ToolbarField"),TEXT("ShortcutField"),TEXT("Shop")})
    {
        FVector2D Offset;
        if(GConfig->GetVector2D(TEXT("AC2.Windows"),Name,Offset,GGameUserSettingsIni) && !Offset.ContainsNaN())
            Native.RootOffsets.Add(Token(Name),FIntPoint(FMath::RoundToInt(Offset.X),FMath::RoundToInt(Offset.Y)));
        FVector2D Dimensions;
        if(GConfig->GetVector2D(TEXT("AC2.WindowSizes"),Name,Dimensions,GGameUserSettingsIni) && !Dimensions.ContainsNaN() && Dimensions.X>0 && Dimensions.Y>0)
            Native.RootSizes.Add(Token(Name),FIntPoint(FMath::RoundToInt(Dimensions.X),FMath::RoundToInt(Dimensions.Y)));
    }
    for(const TCHAR* Field:{TEXT("InventoryField"),TEXT("SkillField"),TEXT("QuestField"),TEXT("SocialField"),TEXT("ExaminationField"),TEXT("OptionField")})
        Native.WindowMinimums.Add(Token(Field),FIntPoint(FCString::Strcmp(Field,TEXT("InventoryField"))==0 || FCString::Strcmp(Field,TEXT("SkillField"))==0?316:256,300));
    Native.WindowMinimums.Add(0x0e036a6c,FIntPoint(256,268));
    Native.WindowMinimums.Add(Token(TEXT("InventoryField")),FIntPoint(316,414));
    Native.WindowMinimums.Add(Token(TEXT("ExternalContainerField")),FIntPoint(244,165));
    Native.WindowMinimums.Add(Token(TEXT("Shop")),FIntPoint(384,360));
    auto LoadWindowGeometry = [](const TCHAR* Section,TMap<uint32,FIntPoint>& Values,bool Positive) {
        TArray<FString> Entries;
        GConfig->GetSection(Section,Entries,GGameUserSettingsIni);
        for(const FString& Pair:Entries){
            FString Key,Raw;
            if(!Pair.Split(TEXT("="),&Key,&Raw) || Key.Len()!=8)continue;
            bool Hex=true;
            for(TCHAR Ch:Key)if(!FChar::IsHexDigit(Ch)){Hex=false;break;}
            FVector2D Value;
            if(Hex && Value.InitFromString(Raw) && !Value.ContainsNaN() && (!Positive || (Value.X>0 && Value.Y>0)))
                Values.Add(FParse::HexNumber(*Key),FIntPoint(FMath::RoundToInt(Value.X),FMath::RoundToInt(Value.Y)));
        }
    };
    LoadWindowGeometry(TEXT("AC2.Windows"),Native.RootOffsets,false);
    LoadWindowGeometry(TEXT("AC2.WindowSizes"),Native.RootSizes,true);
    GConfig->GetBool(TEXT("AC2.HUD"),TEXT("ShowFPS"),ShowFps,GGameUserSettingsIni);
    FVector2D SavedFps;
    if(GConfig->GetVector2D(TEXT("AC2.HUD"),TEXT("FPSPosition"),SavedFps,GGameUserSettingsIni) && !SavedFps.ContainsNaN())
        FpsPosition=SavedFps;
    if (!MapUI.Open(Mode->DatDirectory, Error))
        UE_LOG(LogTemp, Error, TEXT("Native maps: %s"), *Error);
    MapUI.OnCoordinatesChanged=[this](bool Show){SetRadarCoordinates(Show);};
    BodyFont = ChatFont = Fonts.Get(0x4200000c);
    TitleFont = Fonts.Get(0x4200001d);
    ChildSlot
        [SNew(SCanvas) +
         SCanvas::Slot()
             .Position_Lambda([this]() { return FVector2D(Native.Bounds(TEXT("ChatEntry")).Min) * Scale; })
             .Size_Lambda([this]() {
                 return FVector2D(Native.Bounds(TEXT("ChatEntry")).Size());
             })[SAssignNew(Entry, SAC2TextEntry)
                    .Font(ChatFont)
                    .NameOnly(false)
                    .MaxCharacters(512)
                    .RenderTransformPivot(FVector2D::ZeroVector)
                    .RenderTransform_Lambda([this]() { return FSlateRenderTransform(Scale); })
                    .Visibility_Lambda(
                        [this]() { return Editing && !OptionConfirmationOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnEnter_Lambda([this]() {
                        Submit(Entry->Value());
                        Entry->SetText(TEXT(""));
                        EndChat();
                    })
                    .OnEscape_Lambda([this]() { EndChat(); })
                    .OnHistory_Lambda([this](int32 D) { Entry->SetText(Log.Recall(D, Entry->Value())); })]
        + SCanvas::Slot().Position_Lambda([this](){return FVector2D(Native.Bounds(TEXT("NoFellowshipNameEntryText")).Min)*Scale;})
            .Size_Lambda([this](){return FVector2D(Native.Bounds(TEXT("NoFellowshipNameEntryText")).Size());})
            [SAssignNew(FellowshipEntry,SAC2TextEntry).Font(BodyFont).NameOnly(false).MaxCharacters(32)
             .RenderTransformPivot(FVector2D::ZeroVector).RenderTransform_Lambda([this](){return FSlateRenderTransform(Scale);})
             .Visibility_Lambda([this](){return !OptionConfirmationOpen() && ShowSocial && SocialTab==0 && Mode->Network && Mode->Network->Fellows.IsEmpty() && (Mode->Network->FellowshipInviter.IsEmpty() || FPlatformTime::Seconds()>=Mode->Network->FellowshipInvitationUntil)?EVisibility::Visible:EVisibility::Collapsed;})
             .OnEnter_Lambda([this](){SocialAction(TEXT("FellowshipCreateButton"));FSlateApplication::Get().ClearKeyboardFocus();})
             .OnEscape_Lambda([](){FSlateApplication::Get().ClearKeyboardFocus();})]];
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() {
        if (!VisibleWorld())
            return EVisibility::Collapsed;
        auto *PC = Controller(Mode);
        return PC && (PC->bShowMouseCursor || CameraLeft || CameraRight) ? EVisibility::Visible : EVisibility::HitTestInvisible;
    }));
    Log.Add(EAC2ChatType::System, TEXT("Welcome to Dereth. Type /help in chat for commands."));
    if (Frontend) RestoreOptions();
    Rebuild();
}
SAC2GameUI::~SAC2GameUI()
{
    RemoveKeyBindings();
    if (Portrait.IsValid())
        Portrait->Destroy();
}
bool SAC2GameUI::VisibleWorld() const
{
    if (Frontend) return ShowOptions;
    auto *PC = Controller(Mode);
    auto *H = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    return H && Mode.IsValid() && Mode->InWorld() && (!H->CharacterUI || !H->CharacterUI->IsCreation());
}
FSlateRect SAC2GameUI::ChatRect() const
{
    return SlateRect(Native.Bounds(TEXT("ChatWindow1")));
}
void SAC2GameUI::Tick(const FGeometry &G, double Time, float Delta)
{
    ShortcutTime=Time;
    if(ShowFps && Delta>0){
        FpsInterval+=Delta;
        ++FpsFrames;
        if(FpsInterval>=0.25){
            MeasuredFps=FMath::RoundToInt(FpsFrames/FpsInterval);
            FpsFrames=0;
            FpsInterval=0;
        }
    }
    SCompoundWidget::Tick(G, Time, Delta);
    // Native HUD placement is in pixels, anchored to the actual viewport, never a scaled mockup page.
    Scale = 1.f / FMath::Max(.1f, G.GetAccumulatedLayoutTransform().GetScale());
    Size = G.GetLocalSize() / Scale;
    if (Mode.IsValid() && !Mode->Notice.IsEmpty() && LastNotice != Mode->Notice)
    {
        LastNotice = Mode->Notice;
        Log.Add(LastNotice.StartsWith(TEXT("Hit:")) || LastNotice.Contains(TEXT("defeat"))
                    ? EAC2ChatType::Combat
                    : EAC2ChatType::System,
                LastNotice);
    }
    UpdateHover(G, Time);
    if(Mode.IsValid() && Mode->Network && LastShopVendor!=Mode->Network->ShopVendor)
    {
        LastShopVendor=Mode->Network->ShopVendor;SelectedOffer=INDEX_NONE;ShopPage=0;
        if(!LastShopVendor.IsEmpty()){ShowInventory=true;UpdatePortrait();}
    }
    if(!Frontend && ShowStats && !ExaminedSkill && !ExaminedRecipe && !InspectedObject.StartsWith(TEXT("status:")) && VisibleWorld() && Mode.IsValid() && Mode->Network)
        Mode->Network->RefreshExamination(InspectedObject);
    Rebuild();
}
TArray<FString> SAC2GameUI::StatusDescription(const FString& Key) const
{
    const auto* Net=Mode.IsValid()?Mode->Network.Get():nullptr;
    if(Key==TEXT("status:link")){
        TArray<FString> Lines={TEXT("Link status")};
        if(!Net || !Net->IsWorld())Lines.Add(TEXT("Disconnected"));
        else if(!Net->HaveLinkSample)Lines.Add(TEXT("Connected. Measuring traffic…"));
        else {
            Lines.Add(FString::Printf(TEXT("Received: %.0f bytes/s"),Net->ReceivedBytesPerSecond));
            Lines.Add(FString::Printf(TEXT("Sent: %.0f bytes/s"),Net->SentBytesPerSecond));
            Lines.Add(FString::Printf(TEXT("Retransmitted: %.1f%%"),Net->RetransmitPercent));
            Lines.Add(FString::Printf(TEXT("Last packet: %.1f seconds ago"),Net->LinkSilence));
        }
        return Lines;
    }
    if(Key==TEXT("status:vitae")){
        const float Vitae=Net?Net->Vitae:100.f;
        TArray<FString> Lines={TEXT("Vitae"),FString::Printf(TEXT("Current vitae: %.1f%%"),Vitae),
            Vitae<100 ? FString::Printf(TEXT("Death penalty: %.1f%%"),100-Vitae) : FString(TEXT("No death penalty."))};
        if(Vitae<100 && Net && Net->VitaeRecoveryRemaining.IsSet())
            Lines.Add(FString::Printf(TEXT("XP to recover one point: %s"),*FText::AsNumber(Net->VitaeRecoveryRemaining.GetValue()).ToString()));
        return Lines;
    }
    if(Key==TEXT("status:conflict")){
        switch(Net ? Net->PKStatus : 0){
        case 1:return {TEXT("Conflict status"),TEXT("Peaceful")};
        case 2:return {TEXT("Conflict status"),TEXT("Kingdom control")};
        case 3:return {TEXT("Conflict status"),TEXT("Factional conflict")};
        case 4:return {TEXT("Conflict status"),TEXT("Free-for-all")};
        case 5:return {TEXT("Conflict status"),TEXT("Kingdom versus kingdom anywhere")};
        default:return {TEXT("Conflict status"),TEXT("No conflict status received.")};
        }
    }
    if(Net){
        const uint32 Id=FCString::Strtoui64(*Key.RightChop(14),nullptr,10);
        if(const auto* E=Net->StatusEffects.Find(Id)){
            TArray<FString> Lines={E->Name.IsEmpty()?FString(TEXT("Active effect")):E->Name};
            if(!E->Description.IsEmpty())Lines.Add(E->Description);
            if((E->Flags&6) && E->Duration>0){
                const auto Clock=Net->ServerTime();
                if(Clock.IsSet()){
                    const int32 Seconds=FMath::Max(0,FMath::CeilToInt(E->Cast+E->Duration-Clock.GetValue()));
                    Lines.Add(FString::Printf(TEXT("Time remaining: %d:%02d"),Seconds/60,Seconds%60));
                }
            }
            return Lines;
        }
    }
    return {TEXT("Effect has ended.")};
}

void SAC2GameUI::BuildStatusIndicators()
{
    for(const auto& Pair : {TPair<const TCHAR*,const TCHAR*>(TEXT("NetStatusField"),TEXT("status:link")),
                            TPair<const TCHAR*,const TCHAR*>(TEXT("PKStatusField"),TEXT("status:conflict")),
                            TPair<const TCHAR*,const TCHAR*>(TEXT("VitaeStatusField"),TEXT("status:vitae"))}){
        Bind(Pair.Key,FName(Pair.Value));
        Hints.Add(FName(Pair.Key),FString::Join(StatusDescription(Pair.Value),TEXT("\n")));
    }
    const auto* Net=Mode->Network.Get();
    const auto* List=Native.Find(TEXT("EffectListBox"));
    if(!Net || !List || Net->StatusEffects.IsEmpty())return;
    const FIntRect Area=List->Rect, Clip=List->Clip;
    const int32 Columns=Area.Width()/32;
    if(!Columns)return;
    const auto Clock=Net->ServerTime();
    TArray<FAC2Network::FStatusEffect> Effects;
    Net->StatusEffects.GenerateValueArray(Effects);
    auto ExpiresAt=[](const FAC2Network::FStatusEffect& E){
        return (E.Flags&6) && E.Duration>0 ? E.Cast+E.Duration : TNumericLimits<double>::Max();
    };
    Effects.Sort([&ExpiresAt](const auto& A,const auto& B){
        if(ExpiresAt(A)!=ExpiresAt(B))return ExpiresAt(A)<ExpiresAt(B);
        return A.Order==B.Order ? A.Id<B.Id : A.Order<B.Order;
    });
    int32 Index=0;
    for(const auto& E:Effects){
        const bool Timed=(E.Flags&6) && E.Duration>0;
        const double Left=Timed && Clock.IsSet()?E.Cast+E.Duration-Clock.GetValue():E.Duration;
        if(Timed && Left<=0)continue;
        const FIntPoint Origin=Area.Min+FIntPoint((Index%Columns)*32,(Index/Columns)*38);
        if(Origin.Y+32>Clip.Max.Y)break;
        ++Index;
        const FIntRect Slot(Origin,Origin+FIntPoint(32,38));
        const bool ShowTimer=Timed && !(E.Flags&0x400);
        const uint32 Meter=Token(TEXT("EffectTimeMeter"));
        const FString Key=FString::Printf(TEXT("status:effect:%u"),E.Id);
        if(Timed)Native.States.Add(Token(TEXT("EffectTemplate")),Token((E.Flags&0x400)?TEXT("pulsed"):TEXT("active")));
        Native.States.Add(Token(TEXT("IconField")),Token(ShowStats && InspectedObject==Key ? TEXT("selected") : TEXT("unselected")));
        Native.States.Add(0x5287c33c,Token(ShowTimer?TEXT("active"):TEXT("stateless")));
        Native.Visibility.Add(Meter,ShowTimer);
        if(ShowTimer){
            Native.Meters.Add(Meter,FMath::Clamp(Left/E.Duration,0.,1.));
            Native.States.Add(Token(TEXT("left_fill")),Token(Left<=FMath::Min(60.,E.Duration*.25)?TEXT("low"):
                Left<=FMath::Min(300.,E.Duration*.5)?TEXT("mid"):TEXT("full")));
        }
        const int32 Start=Native.Nodes.Num();
        Native.Template(0x2200002b,TEXT("EffectTemplate"),Slot,Clip,Token(TEXT("EffectListBox")));
        Native.Visibility.Remove(Meter);Native.Meters.Remove(Meter);
        Native.States.Remove(Token(TEXT("left_fill")));Native.States.Remove(0x5287c33c);
        Native.States.Remove(Token(TEXT("EffectTemplate")));
        Native.States.Remove(Token(TEXT("IconField")));
        FIntRect IconRect(Origin,Origin+FIntPoint(32,32));
        for(int32 I=Start;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==Token(TEXT("IconField"))){
            IconRect=Native.Nodes[I].Rect;break;
        }
        IconRect.Clip(Clip);
        if(E.Icon)Icons.Add({IconRect,Clip,E.Icon});
        Buttons.Add(FName(*Key),SlateRect(IconRect));
        Hints.Add(FName(*Key),FString::Join(StatusDescription(Key),TEXT("\n")));
    }
}

void SAC2GameUI::Bind(const TCHAR *Control, FName Action)
{
    if (const auto *N = Native.Find(Control))
    {
        if(N->State==Token(TEXT("ghosted")))return;
        Buttons.Add(Control, SlateRect(N->Clip));
        ButtonActions.Add(Control, Action);ButtonWindows.Add(Control,N->Window);
    }
}
const TCHAR* SAC2GameUI::LeftPanelRoot() const
{
    return Native.PanelRoot(ShowOptions?TEXT("OptionField"):TEXT("ExaminationField"));
}
const TCHAR* SAC2GameUI::RightPanelRoot() const
{
    return Native.PanelRoot(ShowSkills?TEXT("SkillField"):ShowQuests?TEXT("QuestField"):
        ShowSocial?TEXT("SocialField"):TEXT("InventoryField"));
}
void SAC2GameUI::Rebuild()
{
    if (!Mode.IsValid())
        return;
    TSet<uint32> PreviousWindows;for(const auto& N:Native.Nodes)if(N.Id==N.Window)PreviousWindows.Add(N.Window);
    Native.Begin(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
    Buttons.Reset();ButtonWindows.Reset();SaveJournalWindow();
    OptionSliders.Reset();
    OptionsList=OptionsBar=OptionsThumb=FIntRect();OptionsMaxScroll=0;OptionDialogNodesStart=MAX_int32;
    OptionChoiceAnchor=OptionChoicePopup=FIntRect();OptionChoiceItems.Reset();
    ButtonActions.Reset();
    Icons.Reset();
    SkillLinks.Reset();
    Hints.Reset();
    PointerNodesStart=MAX_int32;
    ModeHasNetwork=Mode->Network.IsValid();
    const TCHAR* OptionTabs[]={TEXT("CommandTab"),TEXT("ClientTab"),TEXT("CharacterTab"),TEXT("ChatTab")};
    for(int32 I=0;I<4;++I)Native.States.Add(Token(OptionTabs[I]),Token(I==OptionsTab?TEXT("open"):TEXT("closed")));
    if (Frontend)
    {
        Native.Visibility.Add(Token(TEXT("OptionField")),true);
        Native.Visibility.Add(Token(TEXT("ExaminationField")),false);
        Native.Root(0x22000001,LeftPanelRoot());
        BuildWindowContent(Token(TEXT("OptionField")),[&]{BuildOptions();});
        RaiseWindow(Token(TEXT("OptionField")));
        BuildOptionChoiceMenu();
        BuildOptionConfirmation();
        Native.Prepare(*Assets,Fonts);
        return;
    }
    auto Show = [&](const TCHAR *Name, bool Value) { Native.Visibility.Add(Token(Name), Value); };
    auto SetText = [&](const TCHAR *Name, FString Value) { Native.Text.Add(Token(Name), MoveTemp(Value)); };
    // Each panel keeps its own lifetime and window geometry.
    for (const TCHAR *Name :
         {TEXT("NavigationField"), TEXT("OptionField"), TEXT("SocialField"), TEXT("QuestField"),
          TEXT("SkillField"), TEXT("DismountButton"), TEXT("ConsignmentsButton"), TEXT("OptionsField"),
          TEXT("FocusText"), TEXT("ExperienceText"), TEXT("PKText"), TEXT("PKVigorlossText"),
          TEXT("TimeLeftMeter")})
        Show(Name, false);
    Native.Visibility.Add(0x2b1d8dd8, false);
    Native.Visibility.Add(0xc4ce9c47, false);
    // The alternate four-tab examination page shares the object's list origin.
    // It must not paint its tab underline through the item icon and name.
    Native.Visibility.Add(0x8f26b636, false);
    Native.States.Add(0x5dd5ba54, 0xbdc28f3e);
    for (auto Name : SlotNames)
        Show(*(FString(Name) + TEXT("_Durability")), false);
    Show(TEXT("InventoryField"), ShowInventory);
    Show(TEXT("SkillField"),ShowSkills);Show(TEXT("QuestField"),ShowQuests);PrepareSocial();
    // Native panel_page tabs propagate open/closed to their DAT border pieces.
    const uint32 QuestTabs[]={0x715bdff7,0x70859237,0x92e39d2f};
    const TCHAR* SkillTabs[]={TEXT("MeleeButton"),TEXT("MissileButton"),TEXT("MagicButton"),TEXT("NoncombatButton"),TEXT("CraftTab")};
    for(int32 I=0;I<UE_ARRAY_COUNT(SkillTabs);++I){
        const bool Selected=I==SkillTab;
        const TCHAR* State=I==4?(Selected?TEXT("open"):TEXT("closed")):(Selected?TEXT("highlight"):TEXT("normal"));
        Native.States.Add(Token(SkillTabs[I]),Token(State));
    }
    for(int32 I=0;I<UE_ARRAY_COUNT(QuestTabs);++I)
        Native.States.Add(QuestTabs[I],Token(I==QuestTab?TEXT("open"):TEXT("closed")));
    Show(TEXT("ExaminationField"), ShowStats);
    for (int32 I = 0; I < 4; ++I)
    {
        Show(PackNames[I], I == Pack);
        Native.States.Add(Token(PackButtons[I]), Token(I == Pack ? TEXT("normal_pressed") : TEXT("normal")));
    }
    Show(TEXT("HealthText"), HealthNumbers);
    Show(TEXT("VigorText"), VigorNumbers);
    // gmShortcutUI::CheckAttackButtons: keep both native buttons, ghost the
    // unavailable action, and update from the server's acknowledged attack state.
    const bool Combat=Mode->InCombat();
    Native.States.Add(Token(TEXT("StartAttackButton")),Token(Combat?TEXT("ghosted"):TEXT("normal")));
    Native.States.Add(Token(TEXT("StopAttackButton")),Token(Combat?TEXT("normal"):TEXT("ghosted")));
    const auto *P = Mode->Characters.Active();
    if(P && ModeHasNetwork && !SelectedNetworkItem.IsEmpty())
        SelectedItem=P->Items.IndexOfByPredicate([&](const auto& I){return I.NetworkId==SelectedNetworkItem;});
    const float MaxHealth=Mode->Network ? Mode->Network->MaxHealth : 100, MaxVigor=Mode->Network ? Mode->Network->MaxVigor : 100;
    if(ShowInventory && Portrait.IsValid() && P)Portrait->SetHealth(P->Health,MaxHealth);
    SetText(TEXT("HealthText"), FString::Printf(TEXT("%.0f / %.0f"), P ? P->Health : 0,MaxHealth));
    SetText(TEXT("VigorText"), FString::Printf(TEXT("%.0f / %.0f"), P ? P->Vigor : 0,MaxVigor));
    SetText(TEXT("AmountLabel"), Mode->Network ? FString::FromInt(Mode->Network->Gold) : TEXT("0"));
    SetText(TEXT("SetNumberDisplay"), TEXT("1"));
    SetText(TEXT("ChatEntry"), Editing ? TEXT("") : TEXT(""));
    Native.Meters.Add(Token(TEXT("HealthMeter")), P && MaxHealth>0 ? P->Health / MaxHealth : 0);
    Native.Meters.Add(Token(TEXT("VigorMeter")), P && MaxVigor>0 ? P->Vigor / MaxVigor : 0);
    const auto* Status=Mode->Network.Get();
    Native.Meters.Add(Token(TEXT("PKMeter")), Status && MaxHealth>0 ? 1.f-Status->PKDamage/MaxHealth : 1.f);
    Native.Meters.Add(Token(TEXT("PKVigorlossMeter")), Status && MaxVigor>0 ? 1.f-Status->PKVigorLoss/MaxVigor : 1.f);
    Native.Meters.Add(Token(TEXT("FocusMeter")), Status && Status->IsHero ? (Status->MaxFocus>0 ? Status->CurrentFocus/Status->MaxFocus : 0) : 1);
    SetText(TEXT("FocusText"), Status && Status->IsHero ? FString::Printf(TEXT("%.0f / %.0f"),Status->CurrentFocus,Status->MaxFocus) : FString());
    Show(TEXT("FocusText"),Status && Status->IsHero && FocusNumbers);
    uint64 CurrentXp=0,MaxXp=0;
    if(Status && P){
        const bool ServerBounds=Status->ServerLevelXpFloor.IsSet() && Status->ServerLevelXpCeiling.IsSet()
            && *Status->ServerLevelXpCeiling>*Status->ServerLevelXpFloor;
        const uint64 Floor=ServerBounds ? *Status->ServerLevelXpFloor : Status->LevelXpFloor;
        const uint64 Span=ServerBounds ? *Status->ServerLevelXpCeiling-Floor :
            (P->Level==50 && !Status->IsHero ? Floor : Status->LevelXpSpan);
        if(Span>0){
            const uint64 Total=uint64(P->Experience);
            CurrentXp=Total>Floor ? Total-Floor : 0;
            MaxXp=FMath::Max(Span,CurrentXp);
        }
    }
    Native.Meters.Add(Token(TEXT("ExperienceMeter")), MaxXp ? float(double(CurrentXp)/MaxXp) : 0.f);
    SetText(TEXT("ExperienceText"),FString::Printf(TEXT("%llu / %llu"),CurrentXp,MaxXp));
    Native.States.Add(Token(TEXT("FocusMeter")), Token(Status && Status->IsHero ? TEXT("hero") : TEXT("not_hero")));
    Native.States.Add(Token(TEXT("NetStatusField")),Token(!Status || Status->Phase!=TEXT("world") || Status->LinkSilence>=30 ? TEXT("ugly") : Status->LinkSilence>=10 ? TEXT("bad") : TEXT("good")));
    Native.States.Add(Token(TEXT("VitaeStatusField")),Token(Status && Status->Vitae<100.f ? TEXT("notfull") : TEXT("stateless")));
    static const TCHAR* ConflictStates[]={TEXT("stateless"),TEXT("peace"),TEXT("control"),TEXT("factional"),TEXT("ffa"),TEXT("kvkanywhere")};
    const uint32 Conflict=Status ? Status->PKStatus : 0;
    Native.States.Add(Token(TEXT("PKStatusField")),Token(Conflict<UE_ARRAY_COUNT(ConflictStates)?ConflictStates[Conflict]:ConflictStates[0]));
    Native.Visibility.Add(Token(TEXT("Overlay")),ShowStats && InspectedObject==TEXT("status:vitae"));
    Native.Root(0x2200002b, TEXT("StatusbarField"));
    Native.Visibility.Remove(Token(TEXT("Overlay")));
    BuildWindowContent(Token(TEXT("StatusbarField")),[&]{BuildStatusIndicators();});
    Native.Root(0x22000001, TEXT("ToolbarField"));
    LoadShortcuts();
    Native.RootSizes.Remove(Token(TEXT("ShortcutField")));
    Native.RootBottomRows.Add(Token(TEXT("ShortcutField")),ShortcutRows);
    Native.Root(0x22000001, TEXT("ShortcutField"));
    const int32 ChatFirst=Native.Nodes.Num();
    Native.Root(0x22000001, TEXT("ChatWindow1"));
    // Native windows may overlap the toolbar. Root layout already constrains
    // them to the viewport; an extra toolbar exclusion prevents bottom-edge drags.
    if(ChatFont && ChatFont->Height>0){
        const auto Rect=Native.Bounds(TEXT("ChatLog"));int32 Rows=0;
        for(const auto& Line:Log.Lines)Rows+=ChatFont->Lines(Line.Text,Rect.Width()).Num();
        MaxScroll=FMath::Max(0,Rows-Rect.Height()/int32(ChatFont->Height));
        Scroll=FMath::Clamp(Scroll,0,MaxScroll);
    }
    ChatBar=ChatThumb=FIntRect();
    for(int32 I=ChatFirst;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Kind==Token(TEXT("scrollbar")))
    {
        const auto& Bar=Native.Nodes[I];ChatBar=Bar.Rect;
        const float Position=MaxScroll>0?1.f-float(FMath::Clamp(Scroll,0,MaxScroll))/MaxScroll:1.f;
        for(int32 J=I+1;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Parent==I && Native.Nodes[J].Id==Token(TEXT("widget")))
        {
            auto& Node=Native.Nodes[J];
            const FIntPoint Offset(0,ChatBar.Min.Y+FMath::RoundToInt((ChatBar.Height()-Node.Rect.Height())*Position)-Node.Rect.Min.Y);
            Node.Rect+=Offset;Node.Clip=Node.Rect;Node.Clip.Clip(ChatBar);
            ChatThumb=Node.Rect;
            for(int32 K=J+1;K<Native.Nodes.Num();++K){
                int32 Parent=Native.Nodes[K].Parent;
                while(Parent>J)Parent=Native.Nodes[Parent].Parent;
                if(Parent==J){Native.Nodes[K].Rect+=Offset;Native.Nodes[K].Clip+=Offset;Native.Nodes[K].Clip.Clip(ChatBar);}
            }
        }
    }
    Show(TEXT("OptionField"),ShowOptions);
    for(const TCHAR* Field:{TEXT("ExaminationField"),TEXT("OptionField"),TEXT("InventoryField"),TEXT("SkillField"),TEXT("QuestField"),TEXT("SocialField")})
        Native.Root(0x22000001,Field);
    Bind(TEXT("InventoryButton"), TEXT("Inventory"));
    Bind(TEXT("InventoryCloseButton"), TEXT("Inventory"));
    Bind(TEXT("ExaminationButton"), TEXT("Stats"));
    Bind(TEXT("ExaminationCloseButton"), TEXT("Stats"));
    Bind(TEXT("NavigationButton"), TEXT("Map"));
    Bind(TEXT("OptionButton"), TEXT("Options"));
    Bind(TEXT("StartAttackButton"), TEXT("StartAttack"));
    Bind(TEXT("StopAttackButton"), TEXT("StopAttack"));
    Hints.Add(TEXT("StartAttackButton"),TEXT("Enter combat. Select a creature to attack."));
    Hints.Add(TEXT("StopAttackButton"),TEXT("Return to peace."));
    Bind(TEXT("SkillButton"), TEXT("Skills"));
    Bind(TEXT("SocialButton"), TEXT("Social"));
    Bind(TEXT("QuestButton"), TEXT("Quests"));
    Bind(TEXT("HealthField"), TEXT("Health"));
    Bind(TEXT("VigorField"), TEXT("Vigor"));
    if(Status && Status->IsHero)Bind(TEXT("FocusField"), TEXT("Focus"));
    Bind(TEXT("ChatEntry"), TEXT("ChatEntry"));
    if(ShowOptions)BuildWindowContent(Token(TEXT("OptionField")),[&]{BuildOptions();});
    if (const auto *N = Native.Find(0x518fb9c2))
        {Buttons.Add(TEXT("Stats"), SlateRect(N->Clip));ButtonWindows.Add(TEXT("Stats"),N->Window);}
    if (const auto *N = Native.Find(0xe0ff9433))
        {Buttons.Add(TEXT("ChatUp"), SlateRect(N->Clip));ButtonWindows.Add(TEXT("ChatUp"),N->Window);}
    if (const auto *N = Native.Find(0xf94b392b))
        {Buttons.Add(TEXT("ChatDown"), SlateRect(N->Clip));ButtonWindows.Add(TEXT("ChatDown"),N->Window);}
    BuildWindowContent(Token(TEXT("ShortcutField")),[&]{BuildShortcuts();});
    // The DAT puts the page arrows on the toolbar beside the shortcut field.
    for(const auto Pair:{TPair<uint32,FName>(0x52b27974,TEXT("ShortcutUp")),TPair<uint32,FName>(0xe6d21fa6,TEXT("ShortcutDown"))})
        if(const auto* N=Native.Find(Pair.Key)){Buttons.Add(Pair.Value,SlateRect(N->Clip));ButtonWindows.Add(Pair.Value,N->Window);}
    PackBar=PackThumb=FIntRect();PackMaxScroll=0;
    if (ShowInventory && P)BuildWindowContent(Token(TEXT("InventoryField")),[&]
    {
        auto AddIcon=[&](const FAC2InventoryItem& Item,FIntRect Rect,FIntRect Clip)
        {
            const auto& D=Mode->CharacterCatalog.Items[Item.Definition];
            auto Layers=Item.Visual?Assets->IconLayers(Item.Visual,Item.Aspects):TArray<FAC2IconLayer>();
            if(Layers.IsEmpty())Icons.Add({Rect,Clip,D.Icon,Item.Quantity});
            else for(int32 I=0;I<Layers.Num();++I)
                Icons.Add({Rect,Clip,Layers[I].Image,I==Layers.Num()-1?Item.Quantity:1,FLinearColor(FColor(Layers[I].Color))});
        };
        // Paper doll belongs to this inventory instance, including after resize.
        const auto Inventory=Native.Bounds(TEXT("InventoryField"));
        const int32 PortraitX=63+FMath::Max(0,Inventory.Width()-316)/2;
        const auto Doll=FIntRect(Inventory.Min+FIntPoint(PortraitX,52),Inventory.Min+FIntPoint(PortraitX+153,252));
        Native.Template(0x22000001,TEXT("PaperDollViewport"),Doll,Inventory,Token(TEXT("InventoryField")));
        const FIntRect List = Native.Bounds(TEXT("ContainerListBox"));
        const int32 Columns=FMath::Max(1,List.Width()/36);
        PackMaxScroll=FMath::Max(0,FMath::DivideAndRoundUp(PackCount(),Columns)-FMath::Max(1,List.Height()/36));
        PackRow=FMath::Clamp(PackRow,0,PackMaxScroll);
        if(const auto* N=Native.Find(0x4132bc32))Buttons.Add(TEXT("InventoryUp"),SlateRect(N->Clip));
        if(const auto* N=Native.Find(0x312383ed))Buttons.Add(TEXT("InventoryDown"),SlateRect(N->Clip));
        for(int32 I=0;I<Native.Nodes.Num();++I){
            const auto& Bar=Native.Nodes[I];
            if(Bar.Kind!=Token(TEXT("scrollbar")) || Bar.Window!=Token(TEXT("InventoryField")))continue;
            PackBar=Bar.Rect;
            for(int32 J=I+1;J<Native.Nodes.Num();++J)if(Native.Nodes[J].Parent==I && Native.Nodes[J].Id==Token(TEXT("widget"))){
                const auto Offset=FIntPoint(0,PackBar.Min.Y+FMath::RoundToInt((PackBar.Height()-Native.Nodes[J].Rect.Height())*(PackMaxScroll?float(PackRow)/PackMaxScroll:0))-Native.Nodes[J].Rect.Min.Y);
                for(int32 K=J;K<Native.Nodes.Num();++K){int32 Parent=K;while(Parent>J)Parent=Native.Nodes[Parent].Parent;
                    if(Parent==J){Native.Nodes[K].Rect+=Offset;Native.Nodes[K].Clip=Native.Nodes[K].Rect;Native.Nodes[K].Clip.Clip(PackBar);}}
                PackThumb=Native.Nodes[J].Rect;
            }
        }
        for (int32 C = 0; C < PackCount(); ++C)
        {
            const int32 Cell = PackStart() + C;
            const int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return !I.Equipped && I.Cell == Cell; });
            const FIntPoint XY = List.Min + FIntPoint((C % Columns) * 36, (C / Columns - PackRow) * 36);
            FIntRect R(XY, XY + FIntPoint(36, 36));
            if (R.Max.Y <= List.Min.Y || R.Min.Y >= List.Max.Y)
                continue;
            Native.Template(0x22000001, TEXT("ItemBackground"), R, List);
            FName Key(*FString::Printf(TEXT("Cell%d"), Cell));
            Buttons.Add(Key, SlateRect(FIntRect(FMath::Max(R.Min.X,List.Min.X),FMath::Max(R.Min.Y,List.Min.Y),
                FMath::Min(R.Max.X,List.Max.X),FMath::Min(R.Max.Y,List.Max.Y))));
            if (Index != INDEX_NONE)
            {
                const auto &D = Mode->CharacterCatalog.Items[P->Items[Index].Definition];
                AddIcon(P->Items[Index],FIntRect(XY + FIntPoint(2, 2), XY + FIntPoint(34, 34)),List);
                Hints.Add(Key, D.Name);
            }
        }
        for (int32 I = 0; I < 4; ++I)
            Bind(PackButtons[I], FName(*FString::Printf(TEXT("Pack%d"), I)));
        for (int32 Bit = 0; Bit < 11; ++Bit)
        {
            const auto *Slot = Native.Find(*(FString(SlotNames[Bit]) + TEXT("_Slot")));
            if (!Slot)
                continue;
            const int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return I.Equipped == (1u << Bit); });
            if (Index != INDEX_NONE)
            {
                const auto &D = Mode->CharacterCatalog.Items[P->Items[Index].Definition];
                if (const auto *Icon = Native.Descendant(*Slot, TEXT("ItemIcon")))
                    AddIcon(P->Items[Index],Icon->Rect,Icon->Clip);
                FName Key(*FString::Printf(TEXT("Item%d"), Index));
                Buttons.Add(Key, SlateRect(Slot->Clip));
                Hints.Add(Key, D.Name);
            }
        }
    });
    if (ShowStats && P)BuildWindowContent(Token(TEXT("ExaminationField")),[&]
    {
        const FIntRect List = Native.Bounds(TEXT("ExaminationListBox"));
        TArray<FString> Lines;
        const auto* Net=Mode->Network.Get();
        const bool Inspecting=Net && !InspectedObject.IsEmpty() && !InspectedObject.StartsWith(TEXT("status:"));
        TArray<FAC2Network::FExaminationRow> Examination;
        if(InspectedObject.StartsWith(TEXT("status:")))Lines=StatusDescription(InspectedObject);
        else if(ExaminedRecipe)Lines=RecipeExamination(ExaminedRecipe);
        else if(ExaminedSkill){
            const auto Info=SkillData.Get(ExaminedSkill);Lines={Info.Name,Info.Description,FString::Printf(TEXT("Required level: %u"),Info.Level),FString::Printf(TEXT("Training cost: %u skill credits"),Info.Credits)};
            bool EquipmentBlocked=false;const FString Equipment=SkillEquipmentHint(ExaminedSkill,EquipmentBlocked);
            if(!Equipment.IsEmpty())Lines.Add(Equipment);
            if(Net){
                const auto Context=CurrentSkills();
                const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==ExaminedSkill;});
                const auto Actions=SkillData.Actions(ExaminedSkill,Context,!Node||Node->Untrainable);
                if(const auto* X=Context.Known.Find(ExaminedSkill)){
                    Lines.Add(FString::Printf(TEXT("Rank: %d / %u"),SkillData.Rank(ExaminedSkill,*X),Info.Cap));
                    Lines.Add(FString::Printf(TEXT("Invested XP: %llu"),*X));
                    if(const uint64 Cost=SkillData.NextCost(ExaminedSkill,*X);Cost)Lines.Add(FString::Printf(TEXT("Next rank: %llu XP"),Cost));
                    if(!Actions.Raise.IsEmpty())Lines.Add(Actions.Raise);
                    if(Net->UntrainingSkill==ExaminedSkill)Lines.Add(FString::Printf(TEXT("Untraining: earn %llu more XP, or Cancel Untraining to use the skill again."),*X));
                    else if(Node)Lines.Add(Node->Passive?TEXT("Passive skill; no activation required."):TEXT("Double-click in the skill book to add to this hotbar page, or drag to a slot."));
                    if(!Actions.Untrain.IsEmpty())Lines.Add(Actions.Untrain);
                }else {Lines.Add(TEXT("Not trained"));Lines.Add(Actions.Train.IsEmpty()?TEXT("Available to train."):Actions.Train);}
            }
            for(const auto& R:Info.Required)Lines.Add(SkillData.Get(R.Key).Name+(R.Value?FString::Printf(TEXT(" rank %u required"),R.Value):TEXT(" must be trained")));
            for(const auto& F:Info.Forbidden)Lines.Add(TEXT("Incompatible with ")+SkillData.Get(F.Key).Name);
        }
        else if(Inspecting)
        {
            if(Net->ExaminationId==InspectedObject && Net->ExaminationError.IsEmpty())
                Examination=Net->ExaminationRows;
            if(Examination.IsEmpty())
            {
                Lines.Add(Net->ObjectName(InspectedObject));
                Lines.Add(Net->ExaminationPending?TEXT("Examining..."):!Net->ExaminationError.IsEmpty()?Net->ExaminationError:TEXT("No examination information is available."));
            }
        }
        else if(!Net && InspectedMob.IsValid())
            Lines={InspectedMob->DisplayName,FString::Printf(TEXT("Health: %.0f"),InspectedMob->Health)};
        else if (P->Items.IsValidIndex(SelectedItem))
        {
            const auto &Item = P->Items[SelectedItem];
            const auto &D = Mode->CharacterCatalog.Items[Item.Definition];
            Lines.Add(D.Name);
            Lines.Add(D.Description);
            Lines.Add(FAC2CharacterCatalog::SlotName(D.Preferred));
            if (D.HasArmor)
                Lines.Add(FString::Printf(TEXT("Armor: %d"), D.Armor));
            else if(D.Preferred && (D.Preferred&0x3f))
                Lines.Add(TEXT("Armor rating unavailable."));
            if(D.Race)Lines.Add(TEXT("Race: ")+FAC2CharacterCatalog::RaceName(D.Race));
            if(D.MinLevel>1)Lines.Add(FString::Printf(TEXT("Required level: %d"),D.MinLevel));
            if (D.HasDamage)
                Lines.Add(FString::Printf(TEXT("Damage: %d"), D.Damage));
            Lines.Add(Item.Equipped ? TEXT("Equipped") : TEXT("In backpack"));
            Lines.Add(D.Preferred?TEXT("Double-click to equip or remove."):TEXT("Double-click to use."));
            if(Item.Quantity>1)Lines.Add(FString::Printf(TEXT("Quantity: %d"),Item.Quantity));
        }
        else
        {
            Lines = {P->Name,
                     FAC2CharacterCatalog::RaceName(P->Race),
                     FString::Printf(TEXT("Level: %d"), P->Level),
                     FString::Printf(TEXT("Experience: %lld"), P->Experience),
                     FString::Printf(TEXT("Health: %.0f / %.0f"), P->Health,MaxHealth),
                     FString::Printf(TEXT("Vigor: %.0f / %.0f"), P->Vigor,MaxVigor),
                     FString::Printf(TEXT("Known armor: %d"), P->Armor(Mode->CharacterCatalog)),
                     Mode->Network ? TEXT("Server character profile") : TEXT("Offline character profile")};
            if(Net && Net->QuestPoints.IsSet())Lines.Add(FString::Printf(TEXT("Quest points: %u"), *Net->QuestPoints));
        }
        for(const auto& Line:Lines)Examination.Add({Line});
        if(Inspecting && !Examination.IsEmpty() && (Net->OwnsInventoryItem(InspectedObject)
            || Net->LootItems.Contains(InspectedObject) || Net->IsGroundItem(InspectedObject))) {
            // Common equipment descriptors carry the unknown-item image. Resolve it
            // from the instance's appearance, just as the native container does.
            constexpr uint32 UnknownItem=0x4100034b;
            const auto& Header=Examination[0];
            const bool HasHeaderImage=Header.Kind==0x40000005 || Header.Kind==0x40000009;
            const bool HasArtwork=(Header.Image && Header.Image!=UnknownItem)
                || Header.Layers.ContainsByPredicate([](const auto& L){return L.Image && L.Image!=UnknownItem;});
            if(!HasArtwork) {
                FAC2Network::FExaminationRow Icon;
                Icon.Layers=Net->ItemIconLayers(InspectedObject,*Assets);
                if(!Icon.Layers.IsEmpty())Icon.Kind=0x40000009;
                else if(const uint32 Image=Net->ItemIcon(InspectedObject);Image && Image!=UnknownItem) {
                    Icon.Kind=0x40000005;
                    Icon.Image=Image;
                }
                if(Icon.Kind) {
                    if(HasHeaderImage)Examination[0]=MoveTemp(Icon);
                    else Examination.Insert(MoveTemp(Icon),0);
                }
            }
        }
        LayoutExamination(Examination,List);
    });
    if(ShowSkills)BuildWindowContent(Token(TEXT("SkillField")),[&]{SelectJournalWindow(Token(TEXT("SkillField")));BuildSkills();SaveJournalWindow();});
    if(ShowQuests)BuildWindowContent(Token(TEXT("QuestField")),[&]{SelectJournalWindow(Token(TEXT("QuestField")));BuildJournal();SaveJournalWindow();});
    if(ShowSocial)BuildWindowContent(Token(TEXT("SocialField")),[&]{SelectJournalWindow(Token(TEXT("SocialField")));BuildSocial();SaveJournalWindow();});
    ShopNodesStart=Native.Nodes.Num();ShopIconsStart=Icons.Num();
    BuildWindowContent(Token(TEXT("Shop")),[&]{BuildShop();});
    BuildWindowContent(0x0e036a6c,[&]{BuildIngredients();});
    BuildWindowContent(Token(TEXT("ExternalContainerField")),[&]{BuildLoot();});
    for (auto &Icon : Icons)
        Native.LoadImage(Icon.Image, *Assets);
    Native.Prepare(*Assets, Fonts);
    MapUI.Update(*Mode.Get(), FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)), *Assets, Fonts);
    for(const auto& N:Native.Nodes)if(N.Parent==INDEX_NONE && N.Id==N.Window && (!WindowStack.Contains(N.Window)||!PreviousWindows.Contains(N.Window))){
        RaiseWindow(N.Window);
        // Selecting a backpack item opens Examine automatically. Keep the
        // inventory in front so this does not cover the next item hit target.
        // Explicitly opening/raising Examine still brings it to the front.
        if(N.Window==Token(TEXT("ExaminationField")) && ShowInventory && !SelectedNetworkItem.IsEmpty())
            RaiseWindow(Token(TEXT("InventoryField")));
    }
    for(const auto& B:Buttons)if(!ButtonWindows.Contains(B.Key)){
        if(const auto* N=Native.Find(*B.Key.ToString()))ButtonWindows.Add(B.Key,N->Window);
    }
    PointerNodesStart=Native.Nodes.Num();
    BuildWorldText();
    BuildPointerFeedback();
    BuildOptionChoiceMenu();
    BuildOptionConfirmation();
    Native.Prepare(*Assets, Fonts);
}
int32 SAC2GameUI::OnPaint(const FPaintArgs &Args, const FGeometry &G, const FSlateRect &Cull,
                          FSlateWindowElementList &Out, int32 Layer, const FWidgetStyle &Style,
                          bool Enabled) const
{
    if (!Mode.IsValid() || !VisibleWorld())
        return Layer;
    const FGeometry P = G.MakeChild(Size, FSlateLayoutTransform(Scale));
    auto PaintChat = [&](const FAC2NativeNode& Node, int32 ChatLayer)
    {
        const int32 NodeIndex=int32(&Node-Native.Nodes.GetData());
        for(const auto& Icon:Icons)if(Icon.NativeNode==NodeIndex)
            if(const auto* Brush=Native.Brush(Icon.Image))
                FSlateDrawElement::MakeBox(Out,++ChatLayer,P.ToPaintGeometry(FVector2D(Icon.Rect.Size()),FSlateLayoutTransform(FVector2D(Icon.Rect.Min))),Brush,ESlateDrawEffect::None,Icon.Tint);
        if(Node.Id != Token(TEXT("ChatLog")))return ChatLayer;
        const auto* N=&Node;
        TArray<FAC2ChatLine> Rows;
        for (const auto &L : Log.Lines)
            for (const auto &Line : ChatFont->Lines(L.Text, N->Rect.Width()))
                Rows.Add({L.Type, Line,L.NativeType});
        const int32 RowHeight = ChatFont->Height;
        const int32 Count = N->Rect.Height() / RowHeight;
        MaxScroll = FMath::Max(0, Rows.Num() - Count);
        const int32 Start = FMath::Max(0, Rows.Num() - Count - FMath::Clamp(Scroll, 0, MaxScroll));
        Out.PushClip(FSlateClippingZone(
            P.MakeChild(FVector2D(N->Clip.Size()), FSlateLayoutTransform(FVector2D(N->Clip.Min)))));
        for (int32 I = Start; I < FMath::Min(Start + Count, Rows.Num()); ++I)
        {
            auto &L = Rows[I];
            const FLinearColor Color=N->TextColor(AC2ChatColorIndex(L.NativeType,L.Type));
            ChatFont->Draw(Out, ++ChatLayer, P, L.Text,
                           FVector2D(N->Rect.Min) + FVector2D(0, (I - Start) * RowHeight), Color);
        }
        Out.PopClip();
        return ChatLayer;
    };
    auto PaintIcons=[&](uint32 Window,int32 First,int32 End)
    {
    for (int32 Index=First;Index<FMath::Min(End,Icons.Num());++Index)
    {
        const auto& I=Icons[Index];
        if(I.NativeNode!=INDEX_NONE || I.Window!=Window)continue;
        if (const auto *B = Native.Brush(I.Image))
        {
            Out.PushClip(FSlateClippingZone(
                P.MakeChild(FVector2D(I.Clip.Size()), FSlateLayoutTransform(FVector2D(I.Clip.Min)))));
            FSlateDrawElement::MakeBox(
                Out, ++Layer,
                P.ToPaintGeometry(FVector2D(I.Rect.Size()), FSlateLayoutTransform(FVector2D(I.Rect.Min))), B,ESlateDrawEffect::None,I.Tint);
            if(I.Quantity>1)
                BodyFont->Draw(Out,++Layer,P,FString::FromInt(I.Quantity),FVector2D(I.Rect.Min)+FVector2D(1,16),FLinearColor::White);
            Out.PopClip();
        }
    }
    };
    for(uint32 Window:WindowStack){
        if(!Native.Find(Window))continue;
        Layer=Native.Paint(Out,Layer+1,P,Fonts,PaintChat,0,FMath::Min(PointerNodesStart,OptionDialogNodesStart),Window);
        if(Window==Token(TEXT("SkillField"))){
    if(ShowSkills && !SkillLinks.IsEmpty()){
        Out.PushClip(FSlateClippingZone(P.MakeChild(FVector2D(SkillClip.Size()),FSlateLayoutTransform(FVector2D(SkillClip.Min)))));
        for(const auto& Link:SkillLinks)FSlateDrawElement::MakeLines(Out,++Layer,P.ToPaintGeometry(),{Link.Key,Link.Value},ESlateDrawEffect::None,SkillLineColor,true,1);
        Out.PopClip();
    }
        }
        PaintIcons(Window,0,Icons.Num());
        if(Window==Token(TEXT("InventoryField")) && ShowInventory && Portrait.IsValid() && Portrait->Ready){
            const auto R=Native.Bounds(TEXT("PaperDollViewport"));
            FSlateDrawElement::MakeBox(Out,++Layer,P.ToPaintGeometry(FVector2D(R.Size()),FSlateLayoutTransform(FVector2D(R.Min))),&PortraitBrush);
        }
        // Each editable child belongs to its panel's paint layer, too.
        if(Window==Token(TEXT("ChatWindow1")) || Window==Token(TEXT("SocialField"))){
            FArrangedChildren Children(EVisibility::Visible);ChildSlot.GetWidget()->ArrangeChildren(G,Children);
            for(int32 I=0;I<Children.Num();++I){const auto& Child=Children[I];
                if((Child.Widget==Entry && Window==Token(TEXT("ChatWindow1"))) || (Child.Widget==FellowshipEntry && Window==Token(TEXT("SocialField"))))
                    Layer=Child.Widget->Paint(Args.WithNewParent(this),Child.Geometry,Cull,Out,Layer+1,Style,Enabled);
            }
        }
    }
    Layer = MapUI.Paint(Out, Layer + 1, P, Fonts);
    Layer=Native.Paint(Out,Layer+1,P,Fonts,{},PointerNodesStart,OptionDialogNodesStart);
    // Draw the native layered icon above panels while captured. Keeping it
    // tied to the item identity also survives inventory updates during a drag.
    if(!DragItem.IsEmpty() && FVector2D::Distance(Mouse,ItemPressPoint)>6)
        if(const auto* Profile=Mode->Characters.Active())
            for(int32 Index=0;Index<Profile->Items.Num();++Index)
                if(Profile->Items[Index].NetworkId==DragItem)
                {
                    const auto& I=Profile->Items[Index];
                    const auto* R=Buttons.Find(*(I.Equipped?FString::Printf(TEXT("Item%d"),Index):FString::Printf(TEXT("Cell%d"),I.Cell)));
                    if(R)for(const auto& Icon:Icons)
                        if(R->ContainsPoint(FVector2D(Icon.Rect.Min)+FVector2D(Icon.Rect.Size())*.5))
                            if(const auto* Brush=Native.Brush(Icon.Image))
                                FSlateDrawElement::MakeBox(Out,++Layer,P.ToPaintGeometry(FVector2D(32,32),FSlateLayoutTransform(Mouse-FVector2D(16,16))),Brush,ESlateDrawEffect::None,Icon.Tint);
                    break;
                }
    if(!DragBinding.IsEmpty() && (DragShortcut==INDEX_NONE || ShortcutDragStarted) && FVector2D::Distance(Mouse,ItemPressPoint)>6)
    {
        for(const auto& Icon:ShortcutLayers(DragBinding))if(const auto* Brush=Native.Brush(Icon.Image))
            FSlateDrawElement::MakeBox(Out,++Layer,P.ToPaintGeometry(FVector2D(32,32),FSlateLayoutTransform(Mouse-FVector2D(16,16))),Brush,ESlateDrawEffect::None,FLinearColor(FColor(Icon.Color)));
        if(!DragBinding.Text.IsEmpty())if(const auto* Source=Native.Find(TEXT("AliasText"))){
            auto Label=*Source;const FIntPoint Origin(Mouse.X-16,Mouse.Y-16);
            Label.Rect=Label.Clip=FIntRect(Origin,Origin+FIntPoint(32,32));
            Layer=Native.PaintText(Label,DragBinding.Text,Out,Layer+1,P,Fonts);
        }
    }
    if(ShowFps && !Frontend && BodyFont){
        const FSlateRect R=FpsBounds();
        const FVector2D Origin(R.Left,R.Top);
        static const FSlateColorBrush Background(FLinearColor::White);
        FSlateDrawElement::MakeBox(Out,++Layer,P.ToPaintGeometry(R.GetSize(),FSlateLayoutTransform(Origin)),&Background,
            ESlateDrawEffect::None,FLinearColor(0,0,0,.68f));
        BodyFont->Draw(Out,++Layer,P,FString::Printf(TEXT("%d FPS"),MeasuredFps),Origin+FVector2D(5,2),FLinearColor::White);
    }
    return Native.Paint(Out,Layer+1,P,Fonts,{},OptionDialogNodesStart);
}
bool SAC2GameUI::WindowResizeAt(FVector2D Point,uint32& Root,uint8& Edges) const
{
    if(Frontend || MapUI.OwnsPointer(Point))return false;
    if(const uint32 Window=WindowAt(Point))if(const auto* N=Native.Find(Window);N && N->Attr(TEXT("allow_resize")) && Window!=Token(TEXT("ShortcutField"))){
        const int32 Grip=6;Edges=0;
        if(Point.X<N->Rect.Min.X+Grip)Edges|=1;else if(Point.X>=N->Rect.Max.X-Grip)Edges|=2;
        if(Point.Y<N->Rect.Min.Y+Grip)Edges|=4;else if(Point.Y>=N->Rect.Max.Y-Grip)Edges|=8;
        if(Edges){Root=Window;return true;}
    }
    if(const auto* Bar=Native.Find(TEXT("ShortcutField"));Bar && Bar->Attr(TEXT("allow_resize"))){
        const FIntRect Grip(Bar->Rect.Min,FIntPoint(Bar->Rect.Max.X,Bar->Rect.Min.Y+Bar->Attr(TEXT("border_size"))));
        if(WindowAt(Point)==Bar->Id && Grip.Contains(FIntPoint(Point.X,Point.Y))){Root=Bar->Id;Edges=4;return true;}
    }
    for(const TCHAR* Name:{TEXT("ToolbarField"),TEXT("ShortcutField")})
        if(Native.Bounds(Name).Contains(FIntPoint(Point.X,Point.Y)))return false;
    const auto Order=Native.PaintOrder();
    for(int32 J=Order.Num()-1;J>=0;--J){int32 I=Order[J];const auto& N=Native.Nodes[I];
        if(!N.Clip.Contains(FIntPoint(Point.X,Point.Y)))continue;
        const uint8 Sides=(N.Attr(TEXT("border_left"))?1:0)|(N.Attr(TEXT("border_right"))?2:0)|
            (N.Attr(TEXT("border_top"))?4:0)|(N.Attr(TEXT("border_bottom"))?8:0);
        if(!Sides)continue;
        while(Native.Nodes[I].Parent!=INDEX_NONE)I=Native.Nodes[I].Parent;
        // The hotbar's full-area native border is handled by its top grip above.
        if(Native.Nodes[I].Id!=Token(TEXT("ChatWindow1")) || WindowAt(Point)!=Native.Nodes[I].Id)continue;
        if(Native.Nodes[I].Attr(TEXT("allow_resize"))){Root=Native.Nodes[I].Id;Edges=Sides;return true;}
    }
    return false;
}
bool SAC2GameUI::BeginWindowResize()
{
    if(!WindowResizeAt(Mouse,ResizeWindow,ResizeEdges))return false;
    const auto* Root=Native.Find(ResizeWindow);ResizeStart=Root->Rect;WindowPress=Mouse;
    WindowOffset=Native.DisplayedRootOffsets.FindRef(ResizeWindow);
    ResizeMin=FIntPoint(Root->Attr(TEXT("min_width"),1),Root->Attr(TEXT("min_height"),1));
    ResizeMax=FIntPoint(Root->Attr(TEXT("max_width"),32768),Root->Attr(TEXT("max_height"),32768));
    return true;
}
void SAC2GameUI::ResizeWindowTo(FVector2D Point)
{
    auto Rect=ResizeStart;const auto Delta=Point-WindowPress;
    if(ResizeWindow==Token(TEXT("ShortcutField"))){
        const auto* Bar=Native.Find(ResizeWindow);if(!Bar)return;
        const int32 RowHeight=FMath::Max(1,int32(Bar->Attr(TEXT("row_height"))));
        const int32 Border=Bar->Attr(TEXT("border_size"));
        const int32 Available=FMath::Max(1,(ResizeStart.Max.Y-Border)/RowHeight);
        ShortcutRows=FMath::Min(Available,FAC2ShortcutGrid::Rows(FMath::RoundToInt((ResizeStart.Height()-Delta.Y-Border)/RowHeight)));
        RefreshShortcutHeightOption(true);
        Rebuild();return;
    }
    for(int32 Axis=0;Axis<2;++Axis){const uint8 Near=Axis?4:1,Far=Axis?8:2;
        if(!(ResizeEdges&(Near|Far)))continue;
        const int32 Limit=FMath::RoundToInt(Size[Axis]);
        const int32 Requested=ResizeStart.Size()[Axis]+FMath::RoundToInt(Delta[Axis])*((ResizeEdges&Near)?-1:1);
        const int32 Available=(ResizeEdges&Near)?ResizeStart.Max[Axis]:Limit-ResizeStart.Min[Axis];
        const int32 Extent=FMath::Clamp(Requested,FMath::Min(ResizeMin[Axis],Available),FMath::Min(ResizeMax[Axis],Available));
        if(ResizeEdges&Near)Rect.Min[Axis]=Rect.Max[Axis]-Extent;else Rect.Max[Axis]=Rect.Min[Axis]+Extent;
    }
    Native.RootSizes.Add(ResizeWindow,Rect.Size());Native.RootOffsets.Add(ResizeWindow,WindowOffset+Rect.Min-ResizeStart.Min);
    Rebuild();
}
void SAC2GameUI::EndWindowResize()
{
    if(!ResizeWindow)return;
    if(ResizeWindow==Token(TEXT("ShortcutField")))SaveShortcuts();
    if(ResizeWindow!=Token(TEXT("ShortcutField"))){
        GConfig->SetVector2D(TEXT("AC2.WindowSizes"),*FString::Printf(TEXT("%08X"),ResizeWindow),FVector2D(Native.RootSizes.FindRef(ResizeWindow)),GGameUserSettingsIni);
        GConfig->SetVector2D(TEXT("AC2.Windows"),*FString::Printf(TEXT("%08X"),ResizeWindow),FVector2D(Native.RootOffsets.FindRef(ResizeWindow)),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }
    ResizeWindow=0;ResizeEdges=0;
}
FSlateRect SAC2GameUI::FpsBounds() const
{
    const FVector2D Extent(80,BodyFont ? BodyFont->Height+4 : 20);
    const FVector2D Origin(FMath::Clamp(FpsPosition.X,0.,FMath::Max(0.,Size.X-Extent.X)),
                           FMath::Clamp(FpsPosition.Y,0.,FMath::Max(0.,Size.Y-Extent.Y)));
    return FSlateRect(Origin.X,Origin.Y,Origin.X+Extent.X,Origin.Y+Extent.Y);
}
void SAC2GameUI::FinishFpsDrag()
{
    if(!DraggingFps)return;
    DraggingFps=false;
    GConfig->SetVector2D(TEXT("AC2.HUD"),TEXT("FPSPosition"),FpsPosition,GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
bool SAC2GameUI::BeginWindowDrag(bool PanelsOnly)
{
    if(Frontend)return false;
    for(uint32 Id:{WindowAt(Mouse)})
    {
        if(PanelsOnly && (Id==Token(TEXT("ChatWindow1")) || Id==Token(TEXT("ShortcutField")) || Id==Token(TEXT("ToolbarField")) || Id==Token(TEXT("StatusbarField"))))return false;
        if(const auto* Root=Native.Find(Id))
        {
            const FIntRect Header(Root->Rect.Min,FIntPoint(Root->Rect.Max.X,FMath::Min(Root->Rect.Max.Y,Root->Rect.Min.Y+24)));
            if(Header.Contains(FIntPoint(Mouse.X,Mouse.Y)))
            {
                // Header controls still receive clicks, but controls on a HUD
                // underneath this window must not intercept its drag gesture.
                for(int32 I=0;I<Native.Nodes.Num();++I)
                    if(Native.Nodes[I].Kind==Token(TEXT("button")) && Native.Nodes[I].Clip.Contains(FIntPoint(Mouse.X,Mouse.Y)))
                        for(int32 Parent=I;Native.Nodes.IsValidIndex(Parent);Parent=Native.Nodes[Parent].Parent)
                            if(Native.Nodes[Parent].Id==Root->Id)return false;
                DragWindow=Root->Id;WindowPress=Mouse;WindowOffset=Native.DisplayedRootOffsets.FindRef(DragWindow);return true;
            }
        }
    }
    return false;
}
void SAC2GameUI::EndWindowDrag()
{
    if(!DragWindow)return;
    GConfig->SetVector2D(TEXT("AC2.Windows"),*FString::Printf(TEXT("%08X"),DragWindow),FVector2D(Native.RootOffsets.FindRef(DragWindow)),GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);DragWindow=0;
}
void SAC2GameUI::DragPack(float Y)
{
    const float Travel=PackBar.Height()-PackThumb.Height();
    if(Travel>0)PackRow=FMath::RoundToInt(FMath::Clamp((Y-PackGrab-PackBar.Min.Y)/Travel,0.f,1.f)*PackMaxScroll);
    Rebuild();
}
void SAC2GameUI::DragChat(float Y)
{
    const int32 Travel=ChatBar.Height()-ChatThumb.Height();
    if(Travel>0)Scroll=FMath::RoundToInt((1.f-FMath::Clamp((Y-ChatGrab-ChatBar.Min.Y)/Travel,0.f,1.f))*MaxScroll);
    Rebuild();
}
FReply SAC2GameUI::OnMouseMove(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(OptionConfirmationOpen()){
        Native.Hovered=0;
        for(int32 I=OptionDialogNodesStart;I<Native.Nodes.Num();++I)
            if(Native.Nodes[I].Kind==Token(TEXT("button")) && Native.Nodes[I].Clip.Contains(FIntPoint(Mouse.X,Mouse.Y)))Native.Hovered=Native.Nodes[I].Id;
        return FReply::Handled();
    }
    if(OpenOptionChoice!=INDEX_NONE){Rebuild();return FReply::Handled();}
    if(DraggingFps){
        FpsPosition=Mouse-FpsGrab;
        const auto R=FpsBounds();
        FpsPosition=FVector2D(R.Left,R.Top);
        return FReply::Handled();
    }
    UpdateShortcutDrag(E.IsShiftDown());
    if(DraggingOptionsScroll){DragOptionsScroll(Mouse.Y);return FReply::Handled();}
    if(ResizeWindow){ResizeWindowTo(Mouse);return FReply::Handled();}
    if(DragWindow)
    {
        const FVector2D D=Mouse-WindowPress;
        Native.RootOffsets.Add(DragWindow,WindowOffset+FIntPoint(FMath::RoundToInt(D.X),FMath::RoundToInt(D.Y)));
        Rebuild();return FReply::Handled();
    }
    if(DraggingExamine){DragExamine(Mouse.Y);return FReply::Handled();}
    if(DraggingIngredients){DragIngredients(Mouse.Y);return FReply::Handled();}
    if(DraggingJournal){DragJournal(Mouse.Y);return FReply::Handled();}
    if(DraggingPack){DragPack(Mouse.Y);return FReply::Handled();}
    if(DraggingChat){DragChat(Mouse.Y);return FReply::Handled();}
    if(CameraLeft || CameraRight)return CameraMouseMove(E);
    if(RotatingPortrait)
    {
        if(Portrait.IsValid())Portrait->Rotate((Mouse.X-PortraitPointer.X)*.6f);
        PortraitPointer=Mouse;return FReply::Handled();
    }
    if(DraggingOption!=INDEX_NONE){DragOption(Mouse.X);return FReply::Handled();}
    MapUI.Move(Mouse);
    Native.Hovered = 0;
    for (int32 I = Native.Nodes.Num() - 1; I >= 0; --I)
        if (WindowOwns(Native.Nodes[I].Window,Mouse) && Native.Nodes[I].Kind == Token(TEXT("button")) && Inside(SlateRect(Native.Nodes[I].Clip), Mouse))
        {
            Native.Hovered = Native.Nodes[I].Id;
            break;
        }
    return FReply::Unhandled();
}
void SAC2GameUI::OnMouseCaptureLost(const FCaptureLostEvent& E)
{
    // Slate releases the current captor even when upgrading this same widget
    // to high-precision input. That one synchronous transfer is not focus loss.
    if(CameraRecapturing){CameraRecapturing=false;return;}
    CancelCameraGesture();
    FinishFpsDrag();
    EndWindowDrag();
    EndWindowResize();
    DragItem.Reset();DragBinding={};DragShortcut=INDEX_NONE;
    ShortcutDragStarted=ShortcutDragAttempted=false;
    DraggingOption=INDEX_NONE;DraggingOptionsScroll=false;PressedAction=NAME_None;Native.Pressed=0;
    RotatingPortrait=false;DraggingIngredients=false;DraggingChat=false;DraggingPack=false;DraggingJournal=false;DraggingExamine=false;
    if(MapCaptured){MapCaptured=false;MapUI.Release(Mouse,false,false);}
    SCompoundWidget::OnMouseCaptureLost(E);
}
FReply SAC2GameUI::OnPreviewMouseButtonDown(const FGeometry &G, const FPointerEvent &E)
{
    const auto Point=G.AbsoluteToLocal(E.GetScreenSpacePosition())/Scale;
    if(!Frontend && ShowFps && !OptionConfirmationOpen() && OpenOptionChoice==INDEX_NONE && Inside(FpsBounds(),Point))
        return OnMouseButtonDown(G,E);
    const uint32 Top=WindowAt(Point);RaiseWindow(Top);
    // Slate text entries are real child widgets. Covered entries must not
    // take focus through a foreground panel painted by the native renderer.
    if((Editing && Inside(SlateRect(Native.Bounds(TEXT("ChatEntry"))),Point) && Top!=Token(TEXT("ChatWindow1"))) ||
       (ShowSocial && Inside(SlateRect(Native.Bounds(TEXT("NoFellowshipNameEntryText"))),Point) && Top!=Token(TEXT("SocialField"))))
        return OnMouseButtonDown(G,E);
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseButtonDown(const FGeometry &G, const FPointerEvent &E)
{
    OnMouseMove(G, E);
    if(OptionConfirmationOpen()){
        if(E.GetEffectingButton()==EKeys::LeftMouseButton)for(const auto& B:Buttons)if(Inside(B.Value,Mouse)){
            PressedAction=B.Key;Native.Press(Token(*B.Key.ToString()));return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        return FReply::Handled();
    }
    if(OptionChoiceMouseDown(E.GetEffectingButton()))return FReply::Handled().CaptureMouse(SharedThis(this));
    if (E.GetEffectingButton()==EKeys::ThumbMouseButton || E.GetEffectingButton()==EKeys::ThumbMouseButton2)
    {
        if (auto* PC=Controller(Mode))
            PC->DispatchPointerKey(E.GetEffectingButton(), E.GetModifierKeys());
        return FReply::Handled(); // Do not toggle again through viewport input.
    }
    if(CameraLeft || CameraRight)return CameraButtonDown(E);
    if(!Frontend && ShowFps && Inside(FpsBounds(),Mouse)){
        if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Handled();
        const auto R=FpsBounds();
        FpsGrab=Mouse-FVector2D(R.Left,R.Top);
        DraggingFps=true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    const uint32 HitWindow=WindowAt(Mouse);RaiseWindow(HitWindow);SelectJournalWindow(HitWindow);
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && BeginWindowResize())
        return FReply::Handled().CaptureMouse(SharedThis(this));
    if(E.GetEffectingButton()==EKeys::RightMouseButton && Mode->Network &&
        ((WindowOwns(Token(TEXT("InventoryField")),Mouse) && Native.Bounds(TEXT("PaperDollViewport")).Contains(FIntPoint(Mouse.X,Mouse.Y))) ||
         (WindowOwns(Token(TEXT("StatusbarField")),Mouse) && Native.Bounds(TEXT("StatusbarField")).Contains(FIntPoint(Mouse.X,Mouse.Y)))))
    {InspectTarget(Mode->Network->PlayerId);return FReply::Handled();}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && BeginWindowDrag(true))
        return FReply::Handled().CaptureMouse(SharedThis(this));
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowOptions && WindowOwns(Token(TEXT("OptionField")),Mouse) && OptionsMaxScroll>0 && OptionsBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingOptionsScroll=true;OptionsGrab=OptionsThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-OptionsThumb.Min.Y:OptionsThumb.Height()*.5f;
        DragOptionsScroll(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && WindowOwns(0x0e036a6c,Mouse) && IngredientBar.Contains(FIntPoint(Mouse.X,Mouse.Y))){
        DraggingIngredients=true;IngredientGrab=IngredientThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-IngredientThumb.Min.Y:IngredientThumb.Height()*.5f;
        DragIngredients(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && (ShowQuests||ShowSkills||ShowSocial) && WindowOwns(JournalWindow,Mouse) && JournalBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingJournal=true;JournalGrab=JournalThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-JournalThumb.Min.Y:JournalThumb.Height()*.5f;
        DragJournal(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowStats && WindowOwns(Token(TEXT("ExaminationField")),Mouse) && ExamineBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingExamine=true;ExamineGrab=ExamineThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-ExamineThumb.Min.Y:ExamineThumb.Height()*.5f;
        DragExamine(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowInventory && WindowOwns(Token(TEXT("InventoryField")),Mouse) && PackBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingPack=true;PackGrab=PackThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-PackThumb.Min.Y:PackThumb.Height()*.5f;
        DragPack(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowInventory && Portrait.IsValid() && WindowOwns(Token(TEXT("InventoryField")),Mouse) &&
        Native.Bounds(TEXT("PaperDollViewport")).Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {RotatingPortrait=true;PortraitPointer=Mouse;return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(ShowOptions && WindowOwns(Token(TEXT("OptionField")),Mouse) && E.GetEffectingButton()==EKeys::LeftMouseButton)
        for(const auto& Slider:OptionSliders)if(Inside(Slider.Value,Mouse))
        {DraggingOption=Slider.Key;DragOption(Mouse.X);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if ((E.GetEffectingButton() == EKeys::LeftMouseButton || E.GetEffectingButton() == EKeys::RightMouseButton) &&
        MapUI.Press(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown()))
    {
        MapCaptured = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    const bool ChatCovered=WindowAt(Mouse)!=Token(TEXT("ChatWindow1"));
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && !ChatCovered && ChatBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingChat=true;ChatGrab=ChatThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-ChatThumb.Min.Y:ChatThumb.Height()*.5f;
        DragChat(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        for (const auto &B : Buttons)
            if (AllowsPanelButton(B.Key,Mouse) && Inside(B.Value, Mouse))
            {
                PressedAction = B.Key;
                ItemPressPoint=Mouse;DragItem.Reset();DragBinding={};DragShortcut=INDEX_NONE;
                ShortcutDragStarted=ShortcutDragAttempted=false;
                if(Mode->Network)
                {
                    const auto* P=Mode->Characters.Active();const FString Key=B.Key.ToString();
                    const int32 Index=Key.StartsWith(TEXT("Item"))?FCString::Atoi(*Key.Mid(4)):
                        Key.StartsWith(TEXT("Cell"))&&P?P->Items.IndexOfByPredicate([&](const auto& I){return !I.Equipped && I.Cell==FCString::Atoi(*Key.Mid(4));}):INDEX_NONE;
                    if(P && P->Items.IsValidIndex(Index))DragItem=P->Items[Index].NetworkId;
                    if(Key.StartsWith(TEXT("LearnedSkill"))){
                        const uint32 Id=FCString::Atoi(*Key.Mid(12));
                        const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==Id;});
                        if(ShortcutAllowed(Id) && Node && !Node->Passive)DragBinding=FAC2Shortcut::ForSkill(Id);
                    }else if(Key.StartsWith(TEXT("CraftRecipe"))){
                        const uint32 Id=FCString::Strtoui64(*Key.Mid(11),nullptr,10);
                        if(RecipeShortcutAllowed(Id))DragBinding=FAC2Shortcut::ForRecipe(Id);
                    }

                }
                if(B.Key.ToString().StartsWith(TEXT("ShortcutSlot"))){
                    DragShortcut=ShortcutHitIndex(B.Key);
                    if(DragShortcut!=INDEX_NONE)DragBinding=Shortcuts[DragShortcut];
                }
                Native.Press(Native.Hovered);
                return FReply::Handled().CaptureMouse(SharedThis(this));
            }
    }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && BeginWindowDrag())
        return FReply::Handled().CaptureMouse(SharedThis(this));
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("CraftRecipe")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            InspectRecipe(FCString::Strtoui64(*B.Key.ToString().Mid(11),nullptr,10));return FReply::Handled();
        }
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("ShortcutSlot")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            InspectShortcut(FCString::Atoi(*B.Key.ToString().Mid(12)));return FReply::Handled();
        }
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("LootItem")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            const int32 Index=FCString::Atoi(*B.Key.ToString().Mid(8));
            if(Mode->Network && Mode->Network->LootItems.IsValidIndex(Index))InspectTarget(Mode->Network->LootItems[Index]);
            return FReply::Handled();
        }
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)
            if(AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse) && (B.Key.ToString().StartsWith(TEXT("Item")) || B.Key.ToString().StartsWith(TEXT("Cell"))))
            {
                // Inspect only populated slots; never move an item as a side effect of right-click.
                auto* P=Mode->Characters.Active();
                const FString Key=B.Key.ToString();
                const int32 Index=Key.StartsWith(TEXT("Item"))?FCString::Atoi(*Key.Mid(4)):
                    P?P->Items.IndexOfByPredicate([&](const auto& I){return !I.Equipped && I.Cell==FCString::Atoi(*Key.Mid(4));}):INDEX_NONE;
                if(P && P->Items.IsValidIndex(Index))SelectItem(Index);
                return FReply::Handled();
            }
    if(E.GetEffectingButton()==EKeys::MiddleMouseButton)
    {
        if(!OwnsPointer(Mouse))if(auto* PC=Controller(Mode))PC->DispatchPointerKey(E.GetEffectingButton(), E.GetModifierKeys());
        return FReply::Handled();
    }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton || E.GetEffectingButton()==EKeys::RightMouseButton)
    {
        if(OwnsPointer(Mouse))return FReply::Handled();
        if(VisibleWorld() && Mode->InWorld())return CameraButtonDown(E);
    }
    return FReply::Unhandled();
}

FReply SAC2GameUI::OnMouseButtonUp(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(OptionConfirmationOpen()){
        if(E.GetEffectingButton()==EKeys::LeftMouseButton){
            const FName Pressed=PressedAction;PressedAction=NAME_None;Native.Pressed=0;
            if(const auto* R=Buttons.Find(Pressed))if(Inside(*R,Mouse))Action(Pressed);
            return FReply::Handled().ReleaseMouseCapture();
        }
        return FReply::Handled();
    }
    if(DraggingFps && E.GetEffectingButton()==EKeys::LeftMouseButton){
        OnMouseMove(G,E);
        FinishFpsDrag();
        return FReply::Handled().ReleaseMouseCapture();
    }
    if(OptionChoiceMouseUp(E.GetEffectingButton()))return FReply::Handled().ReleaseMouseCapture();
    if(DraggingOptionsScroll && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragOptionsScroll(Mouse.Y);DraggingOptionsScroll=false;return FReply::Handled().ReleaseMouseCapture();}
    if(ResizeWindow && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {ResizeWindowTo(Mouse);EndWindowResize();return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingExamine && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragExamine(Mouse.Y);DraggingExamine=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingIngredients && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragIngredients(Mouse.Y);DraggingIngredients=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingJournal && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragJournal(Mouse.Y);DraggingJournal=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingPack && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragPack(Mouse.Y);DraggingPack=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingChat && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragChat(Mouse.Y);DraggingChat=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DragWindow && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {EndWindowDrag();return FReply::Handled().ReleaseMouseCapture();}
    if(CameraLeft || CameraRight)return CameraButtonUp(E);
    if(RotatingPortrait && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {RotatingPortrait=false;return FReply::Handled().ReleaseMouseCapture();}
    if(DraggingOption!=INDEX_NONE && E.GetEffectingButton()==EKeys::LeftMouseButton)
    {DragOption(Mouse.X);DraggingOption=INDEX_NONE;return FReply::Handled().ReleaseMouseCapture();}
    if (MapCaptured)
    {
        MapCaptured = false;
        MapUI.Release(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown());
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (E.GetEffectingButton() == EKeys::LeftMouseButton && !PressedAction.IsNone())
    {
        UpdateShortcutDrag(E.IsShiftDown());
        if(FinishShortcutDrag())return FReply::Handled().ReleaseMouseCapture();
        if(Mode->Network && !DragItem.IsEmpty() && FVector2D::Distance(Mouse,ItemPressPoint)>6)
        {
            const auto* P=Mode->Characters.Active();
            const int32 Index=P?P->Items.IndexOfByPredicate([&](const auto& I){return I.NetworkId==DragItem;}):INDEX_NONE;
            if(Index!=INDEX_NONE)for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("CraftIngredient")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
                Mode->Network->SetCraftIngredient(FCString::Atoi(*B.Key.ToString().Mid(15)),DragItem);
                DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;return FReply::Handled().ReleaseMouseCapture();
            }
            if(Index!=INDEX_NONE && WindowOwns(Token(TEXT("Shop")),Mouse))
            {
                Mode->Network->SellItem(Index);DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;
                return FReply::Handled().ReleaseMouseCapture();
            }
            if(Index!=INDEX_NONE)
            {
                for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("Cell")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse))
                    {Mode->Network->MoveItem(Index,FCString::Atoi(*B.Key.ToString().Mid(4)));break;}
                if(WindowOwns(Token(TEXT("InventoryField")),Mouse) && !P->Items[Index].Equipped)for(int32 Bit=0;Bit<11;++Bit)
                    if(Native.Bounds(*(FString(SlotNames[Bit])+TEXT("_Slot"))).Contains(FIntPoint(Mouse.X,Mouse.Y)))
                        {Mode->Network->UseItem(Index,1u<<Bit);break;}
                if(Mouse.X>=0 && Mouse.Y>=0 && Mouse.X<Size.X && Mouse.Y<Size.Y && !OwnsPointer(Mouse))
                {
                    // gm3DItemsUI::FinishFind3DTarget resolves the release
                    // position, not the selected object or a throttled hover.
                    FString Recipient;
                    for(const auto& Name:WorldNameRects)if(Inside(Name.Value,Mouse)){Recipient=Name.Key;break;}
                    auto* PC=Controller(Mode);FVector Origin,Direction;
                    const bool RayValid=PC && PC->DeprojectScreenPositionToWorld(Mouse.X,Mouse.Y,Origin,Direction);
                    if(Recipient.IsEmpty() && RayValid)Recipient=Mode->Network->WorldTargetAtRay(Origin,Direction);
                    if(!Recipient.IsEmpty())Mode->Network->GiveItem(Index,Recipient);
                    else if(RayValid)Mode->Network->DropItem(Index);
                }
            }
            DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;
            return FReply::Handled().ReleaseMouseCapture();
        }
        const bool SameShortcut=!PressedAction.ToString().StartsWith(TEXT("ShortcutSlot")) ||
            (DragShortcut==ShortcutHitIndex(PressedAction) && DragShortcut!=INDEX_NONE && Shortcuts[DragShortcut]==DragBinding);
        DragItem.Reset();DragBinding={};DragShortcut=INDEX_NONE;
        FName ActionName = PressedAction;
        PressedAction = NAME_None;
        Native.Pressed = 0;
        const auto *R = Buttons.Find(ActionName);
        if (R && Inside(*R, Mouse) && SameShortcut && AllowsPanelButton(ActionName,Mouse))
            Action(ActionName);
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseButtonDoubleClick(const FGeometry &G, const FPointerEvent &E)
{
    if(OptionConfirmationOpen() || OpenOptionChoice!=INDEX_NONE)return OnMouseButtonDown(G,E);
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && Mode->Network)
        for(const auto& B:Buttons)if(AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            const FString Key=B.Key.ToString();
            if(Key.StartsWith(TEXT("LearnedSkill"))){const uint32 Skill=FCString::Atoi(*Key.Mid(12));
                if(!Mode->Network->Skills.ContainsByPredicate([&](const auto& S){return S.Id==Skill && S.Trained;}))return FReply::Handled();
                const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==Skill;});
                if((Node&&Node->Passive) || Mode->Network->UntrainingSkill==Skill)return FReply::Handled();
                DragBinding={};DragItem.Reset();DragShortcut=INDEX_NONE;ShortcutDragStarted=ShortcutDragAttempted=false;
                PressedAction=NAME_None;Native.Pressed=0;AddSkillShortcut(Skill);
                return FReply::Handled().ReleaseMouseCapture();}
            if(Key.StartsWith(TEXT("LootItem"))){Mode->Network->TakeLoot(FCString::Atoi(*Key.Mid(8)));return FReply::Handled();}
        }
    if (E.GetEffectingButton() == EKeys::LeftMouseButton && MapUI.DoubleClick(Mouse))
    {
        MapCaptured = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    if ((E.GetEffectingButton()==EKeys::LeftMouseButton || E.GetEffectingButton()==EKeys::RightMouseButton) &&
        MapUI.Press(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown()))
    {
        MapCaptured = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("Offer")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse))
        {SelectedOffer=FCString::Atoi(*B.Key.ToString().Mid(5));Mode->Network->BuyOffer(SelectedOffer);return FReply::Handled();}
    if(ShopOwnsPointer(Mouse))return FReply::Handled();
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
        for (const auto &B : Buttons)
            if (AllowsPanelButton(B.Key,Mouse) && Inside(B.Value, Mouse) &&
                (B.Key.ToString().StartsWith(TEXT("Item")) || B.Key.ToString().StartsWith(TEXT("Cell"))))
            {
                const auto* P=Mode->Characters.Active();
                const FString Key=B.Key.ToString();
                const int32 Index=Key.StartsWith(TEXT("Item"))?FCString::Atoi(*Key.Mid(4)):
                    P?P->Items.IndexOfByPredicate([&](const auto& I){return !I.Equipped && I.Cell==FCString::Atoi(*Key.Mid(4));}):INDEX_NONE;
                if(P && P->Items.IsValidIndex(Index)){SelectItem(Index);EquipSelected();}
                return FReply::Handled();
            }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && WorldClick(E.GetEffectingButton(),true))
        return FReply::Handled();
    return OwnsPointer(Mouse)?FReply::Handled():FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseWheel(const FGeometry &G, const FPointerEvent &E)
{
    if(OptionConfirmationOpen() || OpenOptionChoice!=INDEX_NONE)return FReply::Handled();
    auto P = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    const uint32 WheelWindow=WindowAt(P);SelectJournalWindow(WheelWindow);
    if(WheelWindow==0x0e036a6c){IngredientScroll=FMath::Clamp(IngredientScroll-FMath::RoundToInt(E.GetWheelDelta()*38),0,IngredientMaxScroll);Rebuild();return FReply::Handled();}
    if(WheelWindow==Token(TEXT("ExternalContainerField")))
    {LootRow=FMath::Max(0,LootRow-FMath::RoundToInt(E.GetWheelDelta()));Rebuild();return FReply::Handled();}
    if(WheelWindow==Token(TEXT("OptionField")) && ShowOptions && (OptionsList.Contains(FIntPoint(P.X,P.Y)) || OptionsBar.Contains(FIntPoint(P.X,P.Y))))
    {ScrollOptions(-FMath::RoundToInt(E.GetWheelDelta()*OptionRowHeight));return FReply::Handled();}
    if(JournalViews.Contains(WheelWindow))
    {if(JournalWindow==Token(TEXT("SkillField"))&&SkillTab==4)CraftPage=FMath::Max(0,CraftPage-FMath::RoundToInt(E.GetWheelDelta()));
        else JournalScroll=FMath::Max(0,JournalScroll-FMath::RoundToInt(E.GetWheelDelta()*36));Rebuild();return FReply::Handled();}
    if (MapUI.Wheel(P, E.GetWheelDelta())) return FReply::Handled();
    if(ShopOwnsPointer(P)){ShopPage=FMath::Max(0,ShopPage-FMath::RoundToInt(E.GetWheelDelta()));return FReply::Handled();}
    if (WheelWindow==Token(TEXT("InventoryField")) && ShowInventory && Inside(SlateRect(Native.Bounds(TEXT("ContainerListBox"))), P))
    {
        const int32 Rows=FMath::DivideAndRoundUp(PackCount(),FMath::Max(1,Native.Bounds(TEXT("ContainerListBox")).Width()/36));
        const int32 VisibleRows=FMath::Max(1,Native.Bounds(TEXT("ContainerListBox")).Height()/36);
        PackRow = FMath::Clamp(PackRow - FMath::RoundToInt(E.GetWheelDelta()), 0, FMath::Max(0,Rows-VisibleRows));
        Rebuild();
        return FReply::Handled();
    }
    if(WheelWindow==Token(TEXT("ExaminationField")) && ShowStats && Inside(SlateRect(Native.Bounds(TEXT("ExaminationListBox"))),P))
    {ExamineScroll=FMath::Clamp(ExamineScroll-FMath::RoundToInt(E.GetWheelDelta()*40),0,ExamineMaxScroll);return FReply::Handled();}
    if (WheelWindow==Token(TEXT("ChatWindow1")) && Inside(ChatRect(), P))
    {
        Scroll = FMath::Clamp(Scroll + FMath::RoundToInt(E.GetWheelDelta() * 3), 0, MaxScroll);
        Rebuild();
        return FReply::Handled();
    }
    if(!OwnsPointer(P))if(auto* PC=Controller(Mode))
        PC->DispatchPointerKey(E.GetWheelDelta() > 0 ? EKeys::MouseScrollUp : EKeys::MouseScrollDown, E.GetModifierKeys(), FMath::Abs(E.GetWheelDelta()));
    return FReply::Handled();
}
FReply SAC2GameUI::OnKeyDown(const FGeometry &, const FKeyEvent &E)
{
    if(OpenOptionChoice!=INDEX_NONE){
        if(E.GetKey()==EKeys::Escape){CloseOptionChoice();Rebuild();}
        return FReply::Handled();
    }
    if(OptionConfirmationOpen()){
        if(!E.IsRepeat() && (E.GetKey()==EKeys::Enter || E.GetKey()==EKeys::Escape))AnswerOptionConfirmation(E.GetKey()==EKeys::Enter);
        return FReply::Handled();
    }
    if (Editing && (E.GetKey() == EKeys::PageUp || E.GetKey() == EKeys::PageDown))
    {
        Scroll = FMath::Clamp(Scroll + (E.GetKey() == EKeys::PageUp ? 6 : -6), 0, MaxScroll);
        return FReply::Handled();
    }
    return FReply::Unhandled();
}
void SAC2GameUI::Action(FName Name)
{
    if (const FName *Bound = ButtonActions.Find(Name))
        Name = *Bound;
    if(OptionConfirmationOpen()){OptionAction(Name);return;}
    if(Name==TEXT("ShortcutUp") || Name==TEXT("ShortcutDown")){
        SelectShortcutSet(ShortcutSet+(Name==TEXT("ShortcutUp")?-1:1));return;
    }
    if(Name.ToString().StartsWith(TEXT("ShortcutSlot"))){UseShortcutBinding(ShortcutHitIndex(Name));return;}
    if(Name==TEXT("ExamineUp")||Name==TEXT("ExamineDown")){ExamineScroll=FMath::Clamp(ExamineScroll+(Name==TEXT("ExamineUp")?-40:40),0,ExamineMaxScroll);Rebuild();return;}
    if(SocialAction(Name) || OptionAction(Name) || CraftAction(Name) || ShopAction(Name) || JournalAction(Name))return;
    if (Name == TEXT("ChatEntry"))
    {
        BeginChat();
        return;
    }
    if(Name.ToString().StartsWith(TEXT("status:"))){
        InspectedObject=Name.ToString(); InspectedMob.Reset();
        ExaminedSkill=ExaminedRecipe=0; ExamineScroll=0; ShowStats=true;
        RaiseWindow(Token(TEXT("ExaminationField")));Rebuild();return;
    }
    if (Name == TEXT("Health"))
    {
        HealthNumbers = !HealthNumbers;
        return;
    }
    if (Name == TEXT("Vigor"))
    {
        VigorNumbers = !VigorNumbers;
        return;
    }
    if (Name == TEXT("Focus"))
    {
        FocusNumbers = !FocusNumbers;
        return;
    }
    if (Name == TEXT("ChatUp") || Name == TEXT("ChatDown"))
    {
        Scroll = FMath::Clamp(Scroll + (Name == TEXT("ChatUp") ? 1 : -1), 0, MaxScroll);
        Rebuild();
        return;
    }
    if (Name == TEXT("InventoryUp") || Name == TEXT("InventoryDown"))
    {
        PackRow=FMath::Clamp(PackRow+(Name==TEXT("InventoryUp")?-1:1),0,PackMaxScroll);
        Rebuild();
        return;
    }
    if (Editing)
        EndChat();
    if (Name == TEXT("StartAttack") || Name == TEXT("StopAttack"))
    {
        Mode->SetCombat(Name==TEXT("StartAttack"));
        return;
    }
    if(Name==TEXT("Skills") || Name==TEXT("Quests"))
    {
        EndChat();const bool Skills=Name==TEXT("Skills");
        const bool WasOpen=Skills?ShowSkills:ShowQuests;
        if(Skills){
            ShowSkills=!WasOpen;
            if(ShowSkills && SkillTab==4){CraftProfessions=true;CraftPage=0;}
        } else ShowQuests=!WasOpen;
        SelectJournalWindow(Token(Skills?TEXT("SkillField"):TEXT("QuestField")));JournalScroll=Skills?-1:0;
        RaiseWindow(Token(Skills?TEXT("SkillField"):TEXT("QuestField")));
        if(Portrait.IsValid()){Portrait->SetActorTickEnabled(ShowInventory);Portrait->Capture->bCaptureEveryFrame=ShowInventory;}
        Rebuild();return;
    }
    if(Name==TEXT("MissileSkill") && Mode->Network){Mode->Network->StarterSkill(1);return;}
    FString Key = Name.ToString();
    if (Key.StartsWith(TEXT("Pack")))
    {
        Pack = FMath::Clamp(FCString::Atoi(*Key.Mid(4)), 0, 3);
        PackRow = 0;
        return;
    }
    if (Key.StartsWith(TEXT("Item")))
    {
        SelectItem(FCString::Atoi(*Key.Mid(4)));
        return;
    }
    if (Key.StartsWith(TEXT("Cell")))
    {
        if (auto *P = Mode->Characters.Active())
        {
            int32 Cell = FCString::Atoi(*Key.Mid(4));
            int32 Index =
                P->Items.IndexOfByPredicate([&](const auto &I) { return !I.Equipped && I.Cell == Cell; });
            if (Index != INDEX_NONE)
                SelectItem(Index);
            else if (P->Items.IsValidIndex(SelectedItem))
            {
                if(Mode->Network) {Mode->Network->MoveItem(SelectedItem,Cell);return;}
                auto Previous = Mode->Characters;
                FString Error;
                if (P->Move(SelectedItem, Cell, Error) &&
                    Mode->Characters.Save(Mode->CharacterSavePath, Error))
                    UpdatePortrait();
                else
                {
                    Mode->Characters = Previous;
                    Log.Add(EAC2ChatType::Error, Error);
                }
            }
        }
        return;
    }
    Mode->UIAction(Name);
}
void SAC2GameUI::ToggleInventory()
{
    EndChat();
    ShowInventory = !ShowInventory;
    if(ShowInventory)RaiseWindow(Token(TEXT("InventoryField")));
    if (ShowInventory)
        UpdatePortrait();
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(ShowInventory);
        Portrait->Capture->bCaptureEveryFrame = ShowInventory;
    }
    if (auto *PC = Controller(Mode))
        PC->SetCursor(!PC->WantsMouseLook() || ShowInventory || ShowStats || (Mode.IsValid() && Mode->bMap));
    Rebuild();
}
void SAC2GameUI::ToggleStats()
{
    EndChat();
    SelectedItem = INDEX_NONE;
    InspectedObject.Reset();InspectedMob.Reset();ExamineScroll=0;ExaminedSkill=ExaminedRecipe=0;
    ShowStats = !ShowStats;
    if(ShowStats && Mode.IsValid() && Mode->Network){InspectedObject=Mode->Network->PlayerId;Mode->Network->Examine(InspectedObject);}
    if(ShowStats)RaiseWindow(Token(TEXT("ExaminationField")));
    if (auto *PC = Controller(Mode))
        PC->SetCursor(!PC->WantsMouseLook() || ShowInventory || ShowStats || (Mode.IsValid() && Mode->bMap));
    Rebuild();
}
bool SAC2GameUI::ClosePanels()
{
    if(OpenOptionChoice!=INDEX_NONE){CloseOptionChoice();Rebuild();return true;}
    if(OptionConfirmationOpen()){AnswerOptionConfirmation(false);return true;}
    if(ShowOptions){CloseOptions();return true;}
    if(Mode.IsValid() && Mode->Network && Mode->Network->OpenCraftRecipe){Mode->Network->CloseCraft();return true;}
    if(Mode.IsValid() && Mode->Network && !Mode->Network->LootContainer.IsEmpty()){Mode->Network->CloseLoot();return true;}
    if(Mode.IsValid() && Mode->Network && !Mode->Network->ShopVendor.IsEmpty()){Mode->Network->CloseShop();return true;}
    if (!ShowInventory && !ShowSkills && !ShowQuests && !ShowSocial && !ShowStats && !ShowOptions && (!Mode.IsValid() || !Mode->bMap))
        return false;
    if (Mode.IsValid()) Mode->bMap = false;
    MapUI.Close();
    ShowInventory = ShowStats = ShowOptions = ShowSkills = ShowQuests = ShowSocial = false;
    DraggingOption=INDEX_NONE;PressedAction=NAME_None;
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(false);
        Portrait->Capture->bCaptureEveryFrame = false;
    }
    if (auto *PC = Controller(Mode))
        PC->RestoreCursor();
    return true;
}
void SAC2GameUI::ToggleMap()
{
    if (!Mode.IsValid()) return;
    EndChat();
    Mode->bMap = !Mode->bMap;
    if (!Mode->bMap) MapUI.Close();
    if (auto *PC = Controller(Mode)) PC->SetCursor(!PC->WantsMouseLook() || ShowInventory || ShowStats || Mode->bMap);
    Rebuild();
}
void SAC2GameUI::SelectItem(int32 Index)
{
    SelectedItem = Index;
    const auto* P=Mode.IsValid()?Mode->Characters.Active():nullptr;
    SelectedNetworkItem=P && P->Items.IsValidIndex(Index)?P->Items[Index].NetworkId:FString();
    InspectedObject.Reset();InspectedMob.Reset();ExamineScroll=0;ExaminedSkill=ExaminedRecipe=0;
    if(Mode.IsValid() && Mode->Network && !SelectedNetworkItem.IsEmpty()){
        InspectedObject=SelectedNetworkItem;
        Mode->Network->Examine(InspectedObject);
    }
    ShowStats = true;

}
void SAC2GameUI::UpdatePortrait()
{
    const auto *P = Mode->Characters.Active();
    const auto *A = P ? Mode->CharacterCatalog.Avatar(P->Race, P->Sex) : nullptr;
    if (!A)
        return;
    if (!Portrait.IsValid())
        Portrait = Mode->GetWorld()->SpawnActor<AAC2Portrait>(FVector(0, 0, -800000), FRotator::ZeroRotator);
    if (Portrait.IsValid() &&
        Portrait->Show(Mode->DatDirectory, Mode->Network ? Mode->Network->Visual(P->Id) : A->Physics, P->Height, P->Build,
                       Mode->Network ? Mode->Network->Appearance(P->Id) : A->Resolve(P->Appearance, P->ClothingMask(Mode->CharacterCatalog)), 0x21000000))
    {
        // The paper doll has no creator stage/arch to reserve space for.
        Portrait->SetViewport(FIntPoint(306,400),.9f);
        PortraitBrush.SetResourceObject(Portrait->Composite);
        PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
        PortraitBrush.ImageSize = FVector2D(153, 200);
    }
}
void SAC2GameUI::EquipSelected()
{
    if(Mode.IsValid() && Mode->Network) {Mode->Network->UseItem(SelectedItem);return;}
    auto Previous = Mode->Characters;
    FString Error;
    if (auto *P = Mode->Characters.Active())
        if (P->Equip(SelectedItem, Mode->CharacterCatalog, Error))
        {
            if (Mode->Characters.Save(Mode->CharacterSavePath, Error))
            {
                UpdatePortrait();
                if (auto *PC = Controller(Mode))
                    if (auto *Pawn = Cast<AAC2Explorer>(PC->GetPawn()))
                        if (const auto *A = Mode->CharacterCatalog.Avatar(P->Race, P->Sex))
                            Pawn->ApplyAvatar(
                                A->Physics, P->Height, P->Build,
                                A->Resolve(P->Appearance, P->ClothingMask(Mode->CharacterCatalog)));
                Log.Add(EAC2ChatType::System, TEXT("Equipment saved."));
                return;
            }
            Mode->Characters = Previous;
        }
    if (!Error.IsEmpty())
        Log.Add(EAC2ChatType::Error, Error);
}
bool SAC2GameUI::SmokeEquip()
{
    auto *P = Mode->Characters.Active();
    if (!P || P->Items.IsEmpty())
        return false;
    SelectedItem = 0;
    uint32 Before = P->Items[0].Equipped;
    EquipSelected();
    return Mode->Characters.Active()->Items[0].Equipped != Before;
}
void SAC2GameUI::BeginChat()
{
    auto *PC = Controller(Mode);
    auto *H = PC ? Cast<AAC2HUD>(PC->GetHUD()) : nullptr;
    if (!PC || Editing || OptionConfirmationOpen() || (H && H->CharacterUI && H->CharacterUI->IsOpen()))
        return;
    PreviousCursor = PC->bShowMouseCursor;
    Editing = true;
    PC->SetCursor(true);
    PC->ResetIgnoreMoveInput();
    PC->SetIgnoreMoveInput(true);
    PC->FlushPressedKeys();
    if (auto *P = Cast<AAC2Explorer>(PC->GetPawn()))
        P->GetCharacterMovement()->StopMovementImmediately();
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(Entry);
    Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Input);
    FSlateApplication::Get().SetKeyboardFocus(Entry);
}
void SAC2GameUI::EndChat()
{
    if (!Editing)
        return;
    Editing = false;
    if (auto *PC = Controller(Mode))
    {
        PC->ResetIgnoreMoveInput();
        PC->FlushPressedKeys();
        PC->SetCursor(PreviousCursor);
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
void SAC2GameUI::Submit(const FString &Input)
{
    if(Input.TrimStartAndEnd().IsEmpty())return;
    // Retail remembers what the player typed before any alias expansion.
    Log.Remember(Input);Scroll=0;
    FString Text;
    if(!ExpandAliases(Input,Text))return;
    if(Text.TrimStartAndEnd().Equals(TEXT("/fps"),ESearchCase::IgnoreCase)){
        FinishFpsDrag();
        ShowFps=!ShowFps;
        FpsInterval=0;
        FpsFrames=0;
        GConfig->SetBool(TEXT("AC2.HUD"),TEXT("ShowFPS"),ShowFps,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
        return;
    }
    if(HandleAliasCommand(FAC2AliasRequest::Parse(Text)))return;
    const auto Shortcut=FAC2ShortcutRequest::Parse(Text);
    if(Shortcut.Recognized){
        if(!Shortcut.Error.IsEmpty()){Log.Add(EAC2ChatType::Error,Shortcut.Error);return;}
        if(Frontend || !Mode.IsValid() || !Mode->InWorld() || !Mode->Characters.Active())return;
        if(Shortcut.ShowNumbers.IsSet()){
            SetShortcutNumbers(Shortcut.ShowNumbers.GetValue());Rebuild();
            Log.Add(EAC2ChatType::System,ShowShortcutNumbers?TEXT("You will now see numbers on shortcuts."):TEXT("You will no longer see numbers on shortcuts."));
            return;
        }
        LoadShortcuts();
        const int32 Index=FAC2ShortcutGrid::Index(ShortcutTopRow,Shortcut.Slot,1);
        if(Index==INDEX_NONE)return;
        Shortcuts[Index]=FAC2Shortcut::ForText(Shortcut.Text);SaveShortcuts();Rebuild();
        Log.Add(EAC2ChatType::System,FString::Printf(TEXT("Text shortcut added to slot %d on bar %d."),Shortcut.Slot+1,ShortcutTopRow+1));
        return;
    }
    if(Mode.IsValid() && Text.TrimStartAndEnd().Equals(TEXT("/recall"),ESearchCase::IgnoreCase)){Mode->Recall();return;}
    if(Mode.IsValid() && Mode->Network && !Text.Equals(TEXT("/clear")) && !Text.Equals(TEXT("/loc")))
    {
        const FString FriendCommand=Text.TrimStartAndEnd();
        if(Mode->Network->FriendsEnabled && (FriendCommand.Equals(TEXT("/friend"),ESearchCase::IgnoreCase) || FriendCommand.StartsWith(TEXT("/friend ")))){
            const FString Args=FriendCommand.Mid(7).TrimStartAndEnd();
            FString Action,Name;
            if(Args.Split(TEXT(" "),&Action,&Name) && (Action.Equals(TEXT("add"),ESearchCase::IgnoreCase)||Action.Equals(TEXT("remove"),ESearchCase::IgnoreCase)) && !Name.TrimStartAndEnd().IsEmpty())
                Mode->Network->FriendAction(Action.ToLower(),Name.TrimStartAndEnd());
            else Log.Add(EAC2ChatType::System,TEXT("Use /friend add Name or /friend remove Name. Your friends list is in Social > Friends."));
            return;
        }
        if(Text.TrimStartAndEnd().Equals(TEXT("/help"),ESearchCase::IgnoreCase)){
            Log.Add(EAC2ChatType::System,TEXT("/fps: toggle the movable frame rate meter. /shortcut text <1-10> <message or command>: bind text on the current hotbar. /shortcut numbers on|off: number labels."));
            Log.Add(EAC2ChatType::System,TEXT("/alias [add] ;name <replacement>; /alias remove ;name; /alias list; /alias clear; /alias shortcut ;name <1-10>. Built-ins: ;target and ;loc."));
        }
        Mode->Network->Chat(Text); return;
    }
    auto Request = FAC2ChatRequest::Parse(Text);
    if (Text.TrimStartAndEnd().IsEmpty())
        return;
    const auto *Character = Mode.IsValid() ? Mode->Characters.Active() : nullptr;
    const FString Name = Character ? Character->Name : TEXT("Explorer");
    switch (Request.Command)
    {
    case EAC2ChatCommand::Say:
        Log.Add(EAC2ChatType::Local, Name + TEXT(": ") + Request.Text);
        break;
    case EAC2ChatCommand::Emote:
        Log.Add(EAC2ChatType::Local, TEXT("* ") + Name + TEXT(" ") + Request.Text);
        break;
    case EAC2ChatCommand::Help:
        Log.Add(EAC2ChatType::System,
                TEXT("/say or /s: local speech; /me: emote; /loc: position; /clear: clear log; /recall: return."));
        Log.Add(EAC2ChatType::System,
                TEXT("Up/Down: history. Mouse wheel or Page Up/Down: scroll. Escape: cancel entry."));
        Log.Add(EAC2ChatType::System,TEXT("/fps: toggle the movable frame rate meter. /shortcut text <1-10> <message or command>: bind text on the current hotbar. /shortcut numbers on|off: number labels."));
        Log.Add(EAC2ChatType::System,TEXT("/alias [add] ;name <replacement>; /alias remove ;name; /alias list; /alias clear; /alias shortcut ;name <1-10>. Built-ins: ;target and ;loc."));
        break;
    case EAC2ChatCommand::Clear:
        Log.Lines.Reset();
        break;
    case EAC2ChatCommand::Location:
        if (auto *PC = Controller(Mode))
            if (APawn *Pawn = PC->GetPawn())
            {
                uint32 Block = 0;
                const FVector P = Pawn->GetActorLocation();
                AC2::WorldBlock(P, Block);
                Log.Add(EAC2ChatType::System,
                        FString::Printf(TEXT("Block %04X | position %.1f, %.1f, %.1f m"), Block, P.X / 100,
                                        P.Y / 100, P.Z / 100));
            }
        break;
    default:
        Log.Add(EAC2ChatType::Error, Request.Text);
        break;
    }
}
bool SAC2GameUI::Smoke(FString &Error)
{
    auto *PC = Controller(Mode);
    if (!PC || !BodyFont || !ChatFont || !TitleFont || Native.Nodes.IsEmpty() || Native.MissingImages != 0)
    {
        Error = TEXT("Native game UI resources incomplete");
        return false;
    }
    PC->SetCursor(false);
    BeginChat();
    if (!Editing || !PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored())
    {
        Error = TEXT("Chat did not capture world input");
        return false;
    }
    Entry->SetText(TEXT("Hello Dereth! 123"));
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
    if (Editing || PC->IsMoveInputIgnored() || PC->bShowMouseCursor ||
        Log.Lines.Last().Type != EAC2ChatType::Local ||
        !Log.Lines.Last().Text.EndsWith(TEXT("Hello Dereth! 123")))
    {
        Error = TEXT("Chat submit or world input restoration failed");
        return false;
    }
    BeginChat();
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Up, FModifierKeysState(), 0, false, 0, 0));
    if (Entry->Value() != TEXT("Hello Dereth! 123"))
    {
        Error = TEXT("Chat history failed");
        return false;
    }
    Entry->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
    if (Editing || PC->IsMoveInputIgnored())
    {
        Error = TEXT("Chat cancel failed");
        return false;
    }
    Submit(TEXT("/tell nobody test"));
    if (Log.Lines.Last().Type != EAC2ChatType::Error)
    {
        Error = TEXT("Offline chat channel not identified");
        return false;
    }
    Submit(TEXT("/loc"));
    Log.Add(EAC2ChatType::Combat, TEXT("Combat messages appear here when you strike a target."));
    const auto SavedLines=Log.Lines;
    // Exercise an exposed scrollbar at the bottom edge. The native toolbar
    // (Z=500) correctly covers chat (Z=1000) when their controls overlap;
    // saved user positions must not turn this into a toolbar resize test.
    const uint32 ChatId=Token(TEXT("ChatWindow1"));
    const auto SavedChatOffset=Native.RootOffsets.FindRef(ChatId);
    Native.RootOffsets.Add(ChatId,SavedChatOffset+FIntPoint(FMath::RoundToInt(Size.X),FMath::RoundToInt(Size.Y))-
        Native.Bounds(TEXT("ChatWindow1")).Max);
    for(int32 I=0;I<40;++I)Log.Add(EAC2ChatType::System,FString::Printf(TEXT("Scroll check %d"),I));
    Rebuild();
    if(MaxScroll<=0 || ChatThumb.IsEmpty() || ChatBar.Height()<=ChatThumb.Height())
    {Error=TEXT("Chat history did not create a usable native scroll track");return false;}
    auto Pointer=[&](FVector2D Local,FKey Button){const auto P=GetCachedGeometry().LocalToAbsolute(Local*Scale);
        return FPointerEvent(0,P,P,{EKeys::LeftMouseButton},Button,0,FModifierKeysState());};
    const FVector2D Bottom(ChatThumb.Min+ChatThumb.Size()/2);
    OnMouseButtonDown(GetCachedGeometry(),Pointer(Bottom,EKeys::LeftMouseButton));
    const FVector2D Top(Bottom.X,ChatBar.Min.Y+ChatGrab);
    OnMouseMove(GetCachedGeometry(),Pointer(Top,EKeys::Invalid));
    if(!DraggingChat || Scroll!=MaxScroll){Error=TEXT("Dragging the chat thumb did not reach oldest history");return false;}
    OnMouseMove(GetCachedGeometry(),Pointer(FVector2D(Bottom.X,ChatBar.Max.Y),EKeys::Invalid));
    OnMouseButtonUp(GetCachedGeometry(),Pointer(FVector2D(Bottom.X,ChatBar.Max.Y),EKeys::LeftMouseButton));
    if(DraggingChat || Scroll!=0){Error=TEXT("Chat thumb did not return to newest history and release capture");return false;}
    Log.Lines=SavedLines;Native.RootOffsets.Add(ChatId,SavedChatOffset);Rebuild();
    // Leave native text shortcut examples visible in the following rendered HUD
    // capture. This fixture owns a disposable offline character; never save them.
    TestingShortcuts=true;ShortcutSet=0;ShortcutInstantScroll=true;Rebuild();
    TestingAliases=true;
    Submit(TEXT("/alias ;wave /me waves"));
    Submit(TEXT("/alias shortcut ;wave 8"));
    Submit(TEXT("/shortcut text 9 Ready, everyone?"));
    Submit(TEXT("/shortcut text 10 /loc"));
    if(Shortcuts[7].Text!=TEXT(";wave") || Shortcuts[8].Text!=TEXT("Ready, everyone?"))
    {Error=TEXT("Text shortcut creation failed");return false;}
    if(!SmokeShortcutKey(EKeys::Eight) || !Log.Lines.Last().Text.EndsWith(TEXT(" waves")))
    {Error=TEXT("Text shortcut keyboard activation failed");return false;}
    const auto Layers=ShortcutLayers(Shortcuts[7]);
    if(Layers.Num()!=1 || Layers[0].Image!=0x4100159a || !Native.Brush(Layers[0].Image))
    {Error=TEXT("Native text shortcut artwork missing");return false;}
    return true;
}
void SAC2GameUI::SmokeDraft()
{
    BeginChat();
    Entry->SetText(TEXT("/say Exploring Dereth!"));
}
