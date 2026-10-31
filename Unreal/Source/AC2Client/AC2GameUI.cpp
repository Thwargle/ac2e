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
    for(const TCHAR* Name:{TEXT("ExternalContainerField"),TEXT("LeftSidePanel"),TEXT("RightSidePanel"),TEXT("ChatWindow1"),TEXT("StatusbarField"),TEXT("ToolbarField"),TEXT("ShortcutField"),TEXT("Shop")})
    {
        FVector2D Offset;
        if(GConfig->GetVector2D(TEXT("AC2.Windows"),Name,Offset,GGameUserSettingsIni) && !Offset.ContainsNaN())
            Native.RootOffsets.Add(Token(Name),FIntPoint(FMath::RoundToInt(Offset.X),FMath::RoundToInt(Offset.Y)));
    }
    if (!MapUI.Open(Mode->DatDirectory, Error))
        UE_LOG(LogTemp, Error, TEXT("Native maps: %s"), *Error);
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
                        [this]() { return Editing ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnEnter_Lambda([this]() {
                        Submit(Entry->Value());
                        Entry->SetText(TEXT(""));
                        EndChat();
                    })
                    .OnEscape_Lambda([this]() { EndChat(); })
                    .OnHistory_Lambda([this](int32 D) { Entry->SetText(Log.Recall(D, Entry->Value())); })]];
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() {
        if (!VisibleWorld())
            return EVisibility::Collapsed;
        auto *PC = Controller(Mode);
        return PC && (PC->bShowMouseCursor || CameraLeft || CameraRight) ? EVisibility::Visible : EVisibility::HitTestInvisible;
    }));
    Log.Add(EAC2ChatType::System, TEXT("Welcome to Dereth. Enter opens local chat; /help lists commands."));
    Log.Add(EAC2ChatType::System, Mode->Network ? TEXT("Mouse: click selects, double-click uses, right-click examines. Hold right: look/steer; drag left: orbit; hold both: run. Wheel: zoom. Z: combat; 1: attack; 2: magic; 3: missile; E: use.") : TEXT("Offline world: messages stay in this session."));
    if (Frontend) RestoreOptions();
    Rebuild();
}
SAC2GameUI::~SAC2GameUI()
{
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
        if(!LastShopVendor.IsEmpty()){ShowInventory=true;ShowStats=ShowOptions=false;UpdatePortrait();}
    }
    Rebuild();
}
void SAC2GameUI::Bind(const TCHAR *Control, FName Action)
{
    if (const auto *N = Native.Find(Control))
    {
        if(N->State==Token(TEXT("ghosted")))return;
        Buttons.Add(Control, SlateRect(N->Clip));
        ButtonActions.Add(Control, Action);
    }
}
void SAC2GameUI::Rebuild()
{
    if (!Mode.IsValid())
        return;
    Native.Begin(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
    Buttons.Reset();
    OptionSliders.Reset();
    ButtonActions.Reset();
    Icons.Reset();
    SkillLinks.Reset();
    Hints.Reset();
    PointerNodesStart=MAX_int32;
    ModeHasNetwork=Mode->Network.IsValid();
    if (Frontend)
    {
        Native.Visibility.Add(Token(TEXT("OptionField")),true);
        Native.Visibility.Add(Token(TEXT("ExaminationField")),false);
        Native.Root(0x22000001,TEXT("LeftSidePanel"));
        BuildOptions();
        Native.Prepare(*Assets,Fonts);
        return;
    }
    auto Show = [&](const TCHAR *Name, bool Value) { Native.Visibility.Add(Token(Name), Value); };
    auto SetText = [&](const TCHAR *Name, FString Value) { Native.Text.Add(Token(Name), MoveTemp(Value)); };
    // gmToolbarUI creates these roots but keeps its mutually exclusive panels hidden until selected.
    for (const TCHAR *Name :
         {TEXT("NavigationField"), TEXT("OptionField"), TEXT("SocialField"), TEXT("QuestField"),
          TEXT("SkillField"), TEXT("DismountButton"), TEXT("ConsignmentsButton"), TEXT("OptionsField"),
          TEXT("FocusText"), TEXT("ExperienceText"), TEXT("PKText"), TEXT("PKVigorlossText"),
          TEXT("TimeLeftMeter")})
        Show(Name, false);
    Native.Visibility.Add(0x2b1d8dd8, false);
    Native.Visibility.Add(0xc4ce9c47, false);
    for (auto Name : SlotNames)
        Show(*(FString(Name) + TEXT("_Durability")), false);
    Show(TEXT("InventoryField"), ShowInventory);
    Show(TEXT("SkillField"),ShowSkills);Show(TEXT("QuestField"),ShowQuests);
    // Native panel_page tabs propagate open/closed to their DAT border pieces.
    const uint32 QuestTabs[]={0x715bdff7,0x70859237,0x92e39d2f};
    const TCHAR* SkillTabs[]={TEXT("MeleeButton"),TEXT("MissileButton"),TEXT("MagicButton"),TEXT("NoncombatButton")};
    for(int32 I=0;I<4;++I)Native.States.Add(Token(SkillTabs[I]),Token(I==SkillTab?TEXT("highlight"):TEXT("normal")));
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
    Native.Meters.Add(Token(TEXT("PKMeter")), 1);
    Native.Meters.Add(Token(TEXT("PKVigorlossMeter")), 1);
    Native.Meters.Add(Token(TEXT("FocusMeter")), 1);
    Native.Meters.Add(Token(TEXT("ExperienceMeter")), 0);
    Native.States.Add(Token(TEXT("FocusMeter")), Token(TEXT("not_hero")));
    Native.Root(0x2200002b, TEXT("StatusbarField"));
    Native.Root(0x22000001, TEXT("ToolbarField"));
    Native.Root(0x22000001, TEXT("ShortcutField"));
    const int32 ChatFirst=Native.Nodes.Num();
    Native.Root(0x22000001, TEXT("ChatWindow1"));
    // Preserve the DAT window size, but keep its entry accessible on shorter displays.
    Native.ConstrainRoot(TEXT("ChatWindow1"), FIntRect(0, 0, FMath::RoundToInt(Size.X),
                                                       Native.Bounds(TEXT("ToolbarField")).Min.Y));
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
    if(ShowOptions)Show(TEXT("ExaminationField"),false);
    if (ShowStats || ShowOptions)
        Native.Root(0x22000001, TEXT("LeftSidePanel"));
    if (ShowInventory || ShowSkills || ShowQuests)
        Native.Root(0x22000001, TEXT("RightSidePanel"));
    Bind(TEXT("InventoryButton"), TEXT("Inventory"));
    Bind(TEXT("InventoryCloseButton"), TEXT("Inventory"));
    Bind(TEXT("ExaminationButton"), TEXT("Stats"));
    Bind(TEXT("ExaminationCloseButton"), TEXT("Stats"));
    Bind(TEXT("NavigationButton"), TEXT("Map"));
    Bind(TEXT("OptionButton"), TEXT("Options"));
    Bind(TEXT("StartAttackButton"), TEXT("StartAttack"));
    Bind(TEXT("StopAttackButton"), TEXT("StopAttack"));
    Hints.Add(TEXT("StartAttackButton"),TEXT("Enter combat (Z). Select a creature to attack."));
    Hints.Add(TEXT("StopAttackButton"),TEXT("Return to peace (Z)."));
    Bind(TEXT("SkillButton"), TEXT("Skills"));
    Bind(TEXT("SocialButton"), TEXT("Social"));
    Bind(TEXT("QuestButton"), TEXT("Quests"));
    Bind(TEXT("HealthField"), TEXT("Health"));
    Bind(TEXT("VigorField"), TEXT("Vigor"));
    Bind(TEXT("ChatEntry"), TEXT("ChatEntry"));
    if(ShowOptions)BuildOptions();
    if (const auto *N = Native.Find(0x518fb9c2))
        Buttons.Add(TEXT("Stats"), SlateRect(N->Clip));
    if (const auto *N = Native.Find(0xe0ff9433))
        Buttons.Add(TEXT("ChatUp"), SlateRect(N->Clip));
    if (const auto *N = Native.Find(0xf94b392b))
        Buttons.Add(TEXT("ChatDown"), SlateRect(N->Clip));
    BuildShortcuts();
    PackBar=PackThumb=FIntRect();PackMaxScroll=0;
    if (ShowInventory && P)
    {
        auto AddIcon=[&](const FAC2InventoryItem& Item,FIntRect Rect,FIntRect Clip)
        {
            const auto& D=Mode->CharacterCatalog.Items[Item.Definition];
            auto Layers=Item.Visual?Assets->IconLayers(Item.Visual,Item.Aspects):TArray<FAC2IconLayer>();
            if(Layers.IsEmpty())Icons.Add({Rect,Clip,D.Icon,Item.Quantity});
            else for(int32 I=0;I<Layers.Num();++I)
                Icons.Add({Rect,Clip,Layers[I].Image,I==Layers.Num()-1?Item.Quantity:1,FLinearColor(FColor(Layers[I].Color))});
        };
        Native.RootOffsets.Add(Token(TEXT("PaperDollViewport")),Native.RootOffsets.FindRef(Token(TEXT("RightSidePanel"))));
        Native.Root(0x22000001, TEXT("PaperDollViewport"));
        const FIntRect List = Native.Bounds(TEXT("ContainerListBox"));
        PackMaxScroll=FMath::Max(0,FMath::DivideAndRoundUp(PackCount(),6)-FMath::Max(1,List.Height()/36));
        PackRow=FMath::Clamp(PackRow,0,PackMaxScroll);
        if(const auto* N=Native.Find(0x4132bc32))Buttons.Add(TEXT("InventoryUp"),SlateRect(N->Clip));
        if(const auto* N=Native.Find(0x312383ed))Buttons.Add(TEXT("InventoryDown"),SlateRect(N->Clip));
        for(int32 I=0;I<Native.Nodes.Num();++I){
            const auto& Bar=Native.Nodes[I];
            if(Bar.Kind!=Token(TEXT("scrollbar")) || !Native.Bounds(TEXT("RightSidePanel")).Contains(Bar.Rect.Min))continue;
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
            const FIntPoint XY = List.Min + FIntPoint((C % 6) * 36, (C / 6 - PackRow) * 36);
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
    }
    if (ShowStats && P)
    {
        const FIntRect List = Native.Bounds(TEXT("ExaminationListBox"));
        TArray<FString> Lines;
        const auto* Net=Mode->Network.Get();
        const bool Inspecting=Net && !InspectedObject.IsEmpty();
        TArray<uint32> Appearances, RowImages;
        if(Inspecting)
        {
            if(Net->ExaminationId==InspectedObject && !Net->ExaminationPending && Net->ExaminationError.IsEmpty())
                for(const auto& Row:Net->ExaminationRows)
                {Lines.Add(Row.Text);Appearances.Add(Row.Appearance);RowImages.Add(Row.Image);}
            if(Lines.IsEmpty())
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
                     FString::Printf(TEXT("Experience: %d"), P->Experience),
                     FString::Printf(TEXT("Health: %.0f / %.0f"), P->Health,MaxHealth),
                     FString::Printf(TEXT("Vigor: %.0f / %.0f"), P->Vigor,MaxVigor),
                     FString::Printf(TEXT("Known armor: %d"), P->Armor(Mode->CharacterCatalog)),
                     Mode->Network ? TEXT("Server character profile") : TEXT("Offline character profile")};
        }
        int32 Y = List.Min.Y-ExamineScroll, RowIndex=0;
        for (const FString &Line : Lines)
        {
            const int32 Start = Native.Nodes.Num();
            FIntRect R(List.Min.X, Y, List.Max.X, Y + 20);
            const uint32 Image=RowImages.IsValidIndex(RowIndex)?RowImages[RowIndex]:0;
            const uint32 Appearance=Appearances.IsValidIndex(RowIndex)?Appearances[RowIndex]:0;
            ++RowIndex;
            if(Image)
            {
                Native.LoadImage(Image,*Assets);
                if(const auto* Brush=Native.Brush(Image))
                {const FIntRect ImageRect(List.Min.X,Y,List.Min.X+Brush->ImageSize.X,Y+Brush->ImageSize.Y);
                    Icons.Add({ImageRect,List,Image});Y+=Brush->ImageSize.Y;}
                continue;
            }
            if(!Appearance || !Native.Template(0x22000001,Appearance,R,List))
                Native.Template(0x22000001, TEXT("ExaminationTextTemplate"), R, List);
            if (Native.Nodes.IsValidIndex(Start))
            {
                auto &N = Native.Nodes[Start];
                N.Text = Line;
                auto F = Fonts.Get(N.Attr(TEXT("font"), 0x4200000c));
                int32 Height = F ? FMath::Max(16, int32(F->Lines(Line, List.Width()).Num() * F->Height)) : 20;
                N.Rect.Max.Y = Y + Height;
                N.Clip.Max.Y = FMath::Min(List.Max.Y, N.Rect.Max.Y);
                Y += Height;
            }
        }
        ExamineMaxScroll=FMath::Max(0,Y+ExamineScroll-List.Max.Y);
        ExamineScroll=FMath::Clamp(ExamineScroll,0,ExamineMaxScroll);
    }
    BuildJournal();
    ShopNodesStart=Native.Nodes.Num();ShopIconsStart=Icons.Num();
    BuildShop();
    BuildLoot();
    for (auto &Icon : Icons)
        Native.LoadImage(Icon.Image, *Assets);
    Native.Prepare(*Assets, Fonts);
    MapUI.Update(*Mode.Get(), FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)), *Assets, Fonts);
    PointerNodesStart=Native.Nodes.Num();
    BuildWorldText();
    BuildPointerFeedback();
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
                Rows.Add({L.Type, Line});
        const int32 RowHeight = ChatFont->Height;
        const int32 Count = N->Rect.Height() / RowHeight;
        MaxScroll = FMath::Max(0, Rows.Num() - Count);
        const int32 Start = FMath::Max(0, Rows.Num() - Count - FMath::Clamp(Scroll, 0, MaxScroll));
        Out.PushClip(FSlateClippingZone(
            P.MakeChild(FVector2D(N->Clip.Size()), FSlateLayoutTransform(FVector2D(N->Clip.Min)))));
        for (int32 I = Start; I < FMath::Min(Start + Count, Rows.Num()); ++I)
        {
            auto &L = Rows[I];
            FLinearColor Color = L.Type == EAC2ChatType::Local    ? FLinearColor::White
                                 : L.Type == EAC2ChatType::Combat ? FLinearColor(.9f, .63f, 0)
                                 : L.Type == EAC2ChatType::Error  ? FLinearColor(1, .35f, .3f)
                                                                  : FLinearColor(.8f, .8f, .8f);
            ChatFont->Draw(Out, ++ChatLayer, P, L.Text,
                           FVector2D(N->Rect.Min) + FVector2D(0, (I - Start) * RowHeight), Color);
        }
        Out.PopClip();
        return ChatLayer;
    };
    Layer = Native.Paint(Out, Layer, P, Fonts,PaintChat,0,ShopNodesStart);
    if(ShowSkills && !SkillLinks.IsEmpty()){
        Out.PushClip(FSlateClippingZone(P.MakeChild(FVector2D(SkillClip.Size()),FSlateLayoutTransform(FVector2D(SkillClip.Min)))));
        for(const auto& Link:SkillLinks)FSlateDrawElement::MakeLines(Out,++Layer,P.ToPaintGeometry(),{Link.Key,Link.Value},ESlateDrawEffect::None,SkillLineColor,true,1);
        Out.PopClip();
    }
    auto PaintIcons=[&](int32 First,int32 End)
    {
    for (int32 Index=First;Index<FMath::Min(End,Icons.Num());++Index)
    {
        const auto& I=Icons[Index];
        if(I.NativeNode!=INDEX_NONE)continue;
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
    PaintIcons(0,ShopIconsStart);
    if (ShowInventory && Portrait.IsValid() && Portrait->Ready)
    {
        auto R = Native.Bounds(TEXT("PaperDollViewport"));
        FSlateDrawElement::MakeBox(
            Out, ++Layer, P.ToPaintGeometry(FVector2D(R.Size()), FSlateLayoutTransform(FVector2D(R.Min))),
            &PortraitBrush);
    }
    Layer=SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 1, Style, Enabled);
    // The shop owns its whole rectangle, including where it overlaps chat.
    Layer=Native.Paint(Out,Layer+1,P,Fonts,{},ShopNodesStart,PointerNodesStart);
    PaintIcons(ShopIconsStart,Icons.Num());
    Layer = MapUI.Paint(Out, Layer + 1, P, Fonts);
    Layer=Native.Paint(Out,Layer+1,P,Fonts,{},PointerNodesStart);
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
    if(DragSkill && FVector2D::Distance(Mouse,ItemPressPoint)>6)
        if(const auto* Brush=Native.Brush(const_cast<FAC2SkillData&>(SkillData).Get(DragSkill).Icon))
            FSlateDrawElement::MakeBox(Out,++Layer,P.ToPaintGeometry(FVector2D(32,32),FSlateLayoutTransform(Mouse-FVector2D(16,16))),Brush);
    return Layer;
}
bool SAC2GameUI::BeginWindowDrag(bool PanelsOnly)
{
    if(Frontend)return false;
    for(const TCHAR* Name:{TEXT("ExternalContainerField"),TEXT("Shop"),TEXT("RightSidePanel"),TEXT("LeftSidePanel"),TEXT("ChatWindow1"),TEXT("StatusbarField"),TEXT("ToolbarField"),TEXT("ShortcutField")})
    {
        if(PanelsOnly && FCString::Strcmp(Name,TEXT("ChatWindow1"))==0)break;
        if(const auto* Root=Native.Find(Name))
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
                DragWindow=Root->Id;WindowPress=Mouse;WindowOffset=Native.RootOffsets.FindRef(DragWindow);return true;
            }
        }
    }
    return false;
}
void SAC2GameUI::EndWindowDrag()
{
    if(!DragWindow)return;
    for(const TCHAR* Name:{TEXT("ExternalContainerField"),TEXT("Shop"),TEXT("RightSidePanel"),TEXT("LeftSidePanel"),TEXT("ChatWindow1"),TEXT("StatusbarField"),TEXT("ToolbarField"),TEXT("ShortcutField")})
        if(Token(Name)==DragWindow)
            GConfig->SetVector2D(TEXT("AC2.Windows"),Name,FVector2D(Native.RootOffsets.FindRef(DragWindow)),GGameUserSettingsIni);
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
    if(DragWindow)
    {
        const FVector2D D=Mouse-WindowPress;
        Native.RootOffsets.Add(DragWindow,WindowOffset+FIntPoint(FMath::RoundToInt(D.X),FMath::RoundToInt(D.Y)));
        Rebuild();return FReply::Handled();
    }
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
        if ((!ShopOwnsPointer(Mouse) || I>=ShopNodesStart) && Native.Nodes[I].Kind == Token(TEXT("button")) && Inside(SlateRect(Native.Nodes[I].Clip), Mouse))
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
    EndWindowDrag();
    DragItem.Reset();DragSkill=0;DragShortcut=INDEX_NONE;
    DraggingOption=INDEX_NONE;PressedAction=NAME_None;Native.Pressed=0;
    RotatingPortrait=false;DraggingChat=false;DraggingPack=false;
    if(MapCaptured){MapCaptured=false;MapUI.Release(Mouse,false,false);}
    SCompoundWidget::OnMouseCaptureLost(E);
}
FReply SAC2GameUI::OnMouseButtonDown(const FGeometry &G, const FPointerEvent &E)
{
    OnMouseMove(G, E);
    if (E.GetEffectingButton()==EKeys::ThumbMouseButton)
    {
        if (auto* PC=Controller(Mode))
            if (auto* P=Cast<AAC2Explorer>(PC->GetPawn())) P->ToggleAutoRun();
        return FReply::Handled(); // Do not toggle again through viewport input.
    }
    if(CameraLeft || CameraRight)return CameraButtonDown(E);
    if(E.GetEffectingButton()==EKeys::RightMouseButton && Mode->Network &&
        (Native.Bounds(TEXT("PaperDollViewport")).Contains(FIntPoint(Mouse.X,Mouse.Y)) || Native.Bounds(TEXT("StatusbarField")).Contains(FIntPoint(Mouse.X,Mouse.Y))))
    {InspectTarget(Mode->Network->PlayerId);return FReply::Handled();}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && BeginWindowDrag(true))
        return FReply::Handled().CaptureMouse(SharedThis(this));
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowInventory && !ShopOwnsPointer(Mouse) && PackBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingPack=true;PackGrab=PackThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-PackThumb.Min.Y:PackThumb.Height()*.5f;
        DragPack(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && ShowInventory && Portrait.IsValid() &&
        Native.Bounds(TEXT("PaperDollViewport")).Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {RotatingPortrait=true;PortraitPointer=Mouse;return FReply::Handled().CaptureMouse(SharedThis(this));}
    if(ShowOptions && E.GetEffectingButton()==EKeys::LeftMouseButton)
        for(const auto& Slider:OptionSliders)if(Inside(Slider.Value,Mouse))
        {DraggingOption=Slider.Key;DragOption(Mouse.X);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if ((E.GetEffectingButton() == EKeys::LeftMouseButton || E.GetEffectingButton() == EKeys::RightMouseButton) &&
        MapUI.Press(Mouse, E.GetEffectingButton() == EKeys::RightMouseButton, E.IsShiftDown()))
    {
        MapCaptured = true;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    bool ChatCovered=ShopOwnsPointer(Mouse);
    for(const TCHAR* Root:{TEXT("LeftSidePanel"),TEXT("RightSidePanel"),TEXT("ExternalContainerField")})
        ChatCovered|=Native.Bounds(Root).Contains(FIntPoint(Mouse.X,Mouse.Y));
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && !ChatCovered && ChatBar.Contains(FIntPoint(Mouse.X,Mouse.Y)))
    {DraggingChat=true;ChatGrab=ChatThumb.Contains(FIntPoint(Mouse.X,Mouse.Y))?Mouse.Y-ChatThumb.Min.Y:ChatThumb.Height()*.5f;
        DragChat(Mouse.Y);return FReply::Handled().CaptureMouse(SharedThis(this));}
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        for (const auto &B : Buttons)
            if (AllowsPanelButton(B.Key,Mouse) && Inside(B.Value, Mouse))
            {
                PressedAction = B.Key;
                ItemPressPoint=Mouse;DragItem.Reset();DragSkill=0;DragShortcut=INDEX_NONE;
                if(Mode->Network)
                {
                    const auto* P=Mode->Characters.Active();const FString Key=B.Key.ToString();
                    const int32 Index=Key.StartsWith(TEXT("Item"))?FCString::Atoi(*Key.Mid(4)):
                        Key.StartsWith(TEXT("Cell"))&&P?P->Items.IndexOfByPredicate([&](const auto& I){return !I.Equipped && I.Cell==FCString::Atoi(*Key.Mid(4));}):INDEX_NONE;
                    if(P && P->Items.IsValidIndex(Index))DragItem=P->Items[Index].NetworkId;
                    if(Key.StartsWith(TEXT("LearnedSkill"))){
                        const uint32 Id=FCString::Atoi(*Key.Mid(12));
                        const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==Id;});
                        if(ShortcutAllowed(Id) && Node && !Node->Passive)DragSkill=Id;
                    }else if(Key.StartsWith(TEXT("ShortcutSlot"))){
                        DragShortcut=ShortcutSet*10+FCString::Atoi(*Key.Mid(12));DragSkill=Shortcuts[DragShortcut];
                    }

                }
                Native.Pressed = Native.Hovered;
                return FReply::Handled().CaptureMouse(SharedThis(this));
            }
    }
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && BeginWindowDrag())
        return FReply::Handled().CaptureMouse(SharedThis(this));
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("LootItem")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            const int32 Index=FCString::Atoi(*B.Key.ToString().Mid(8));
            if(Mode->Network && Mode->Network->LootItems.IsValidIndex(Index))InspectTarget(Mode->Network->LootItems[Index]);
            return FReply::Handled();
        }
    if(E.GetEffectingButton()==EKeys::RightMouseButton)
        for(const auto& B:Buttons)
            if(!ShopOwnsPointer(Mouse) && Inside(B.Value,Mouse) && (B.Key.ToString().StartsWith(TEXT("Item")) || B.Key.ToString().StartsWith(TEXT("Cell"))))
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
        if(!OwnsPointer(Mouse))if(auto* PC=Controller(Mode))PC->Cursor();
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
        if(DragSkill && FVector2D::Distance(Mouse,ItemPressPoint)>6){
            bool Dropped=false;
            for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("ShortcutSlot")) && AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
                const int32 Slot=ShortcutSet*10+FCString::Atoi(*B.Key.ToString().Mid(12));
                if(DragShortcut!=INDEX_NONE)Swap(Shortcuts[Slot],Shortcuts[DragShortcut]);else Shortcuts[Slot]=DragSkill;
                Dropped=true;break;
            }
            if(!Dropped && DragShortcut!=INDEX_NONE && !OwnsPointer(Mouse))Shortcuts[DragShortcut]=0;
            SaveShortcuts();DragSkill=0;DragShortcut=INDEX_NONE;PressedAction=NAME_None;Native.Pressed=0;Rebuild();
            return FReply::Handled().ReleaseMouseCapture();
        }
        if(Mode->Network && !DragItem.IsEmpty() && FVector2D::Distance(Mouse,ItemPressPoint)>6)
        {
            const auto* P=Mode->Characters.Active();
            const int32 Index=P?P->Items.IndexOfByPredicate([&](const auto& I){return I.NetworkId==DragItem;}):INDEX_NONE;
            if(Index!=INDEX_NONE && Native.Bounds(TEXT("Shop")).Contains(FIntPoint(Mouse.X,Mouse.Y)))
            {
                Mode->Network->SellItem(Index);DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;
                return FReply::Handled().ReleaseMouseCapture();
            }
            if(Index!=INDEX_NONE)
            {
                for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("Cell")) && Inside(B.Value,Mouse))
                    {Mode->Network->MoveItem(Index,FCString::Atoi(*B.Key.ToString().Mid(4)));break;}
                if(!P->Items[Index].Equipped)for(int32 Bit=0;Bit<11;++Bit)
                    if(Native.Bounds(*(FString(SlotNames[Bit])+TEXT("_Slot"))).Contains(FIntPoint(Mouse.X,Mouse.Y)))
                        {Mode->Network->UseItem(Index,1u<<Bit);break;}
                if(Mouse.X>=0 && Mouse.Y>=0 && Mouse.X<Size.X && Mouse.Y<Size.Y && !OwnsPointer(Mouse))Mode->Network->DropItem(Index);
            }
            DragItem.Reset();PressedAction=NAME_None;Native.Pressed=0;
            return FReply::Handled().ReleaseMouseCapture();
        }
        DragItem.Reset();DragSkill=0;DragShortcut=INDEX_NONE;
        FName ActionName = PressedAction;
        PressedAction = NAME_None;
        Native.Pressed = 0;
        const auto *R = Buttons.Find(ActionName);
        if (R && Inside(*R, Mouse))
            Action(ActionName);
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}
FReply SAC2GameUI::OnMouseButtonDoubleClick(const FGeometry &G, const FPointerEvent &E)
{
    Mouse = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(E.GetEffectingButton()==EKeys::LeftMouseButton && Mode->Network)
        for(const auto& B:Buttons)if(AllowsPanelButton(B.Key,Mouse) && Inside(B.Value,Mouse)){
            const FString Key=B.Key.ToString();
            if(Key.StartsWith(TEXT("LearnedSkill"))){const uint32 Skill=FCString::Atoi(*Key.Mid(12));
                if(!Mode->Network->Skills.ContainsByPredicate([&](const auto& S){return S.Id==Skill && S.Trained;}))return FReply::Handled();
                const auto* Node=SkillData.Panel(SkillPanel).Nodes.FindByPredicate([&](const auto& N){return N.Id==Skill;});
                if((Node&&Node->Passive) || Mode->Network->UntrainingSkill==Skill)return FReply::Handled();
                if(Skill==0x343)Mode->Network->Recall();else Mode->Network->CastSkill(Skill);return FReply::Handled();}
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
        for(const auto& B:Buttons)if(B.Key.ToString().StartsWith(TEXT("Offer")) && Inside(B.Value,Mouse))
        {SelectedOffer=FCString::Atoi(*B.Key.ToString().Mid(5));Mode->Network->BuyOffer(SelectedOffer);return FReply::Handled();}
    if(ShopOwnsPointer(Mouse))return FReply::Handled();
    if (E.GetEffectingButton() == EKeys::LeftMouseButton)
        for (const auto &B : Buttons)
            if (Inside(B.Value, Mouse) &&
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
    auto P = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Scale;
    if(Native.Bounds(TEXT("ExternalContainerField")).Contains(FIntPoint(P.X,P.Y)))
    {LootRow=FMath::Max(0,LootRow-FMath::RoundToInt(E.GetWheelDelta()));Rebuild();return FReply::Handled();}
    if(ShowOptions&&OptionsTab==1&&Inside(SlateRect(Native.Bounds(TEXT("ClientList"))),P))
    {OptionsScroll=FMath::Clamp(OptionsScroll-FMath::RoundToInt(E.GetWheelDelta()),0,7);Rebuild();return FReply::Handled();}
    if((ShowSkills||ShowQuests) && Inside(SlateRect(Native.Bounds(TEXT("RightSidePanel"))),P))
    {JournalScroll=FMath::Max(0,JournalScroll-FMath::RoundToInt(E.GetWheelDelta()*36));Rebuild();return FReply::Handled();}
    if (MapUI.Wheel(P, E.GetWheelDelta())) return FReply::Handled();
    if(ShopOwnsPointer(P)){ShopPage=FMath::Max(0,ShopPage-FMath::RoundToInt(E.GetWheelDelta()));return FReply::Handled();}
    if (ShowInventory && Inside(SlateRect(Native.Bounds(TEXT("ContainerListBox"))), P))
    {
        const int32 Rows=FMath::DivideAndRoundUp(PackCount(),6);
        const int32 VisibleRows=FMath::Max(1,Native.Bounds(TEXT("ContainerListBox")).Height()/36);
        PackRow = FMath::Clamp(PackRow - FMath::RoundToInt(E.GetWheelDelta()), 0, FMath::Max(0,Rows-VisibleRows));
        Rebuild();
        return FReply::Handled();
    }
    if(ShowStats && Inside(SlateRect(Native.Bounds(TEXT("ExaminationListBox"))),P))
    {ExamineScroll=FMath::Clamp(ExamineScroll-FMath::RoundToInt(E.GetWheelDelta()*40),0,ExamineMaxScroll);return FReply::Handled();}
    if (Inside(ChatRect(), P))
    {
        Scroll = FMath::Clamp(Scroll + FMath::RoundToInt(E.GetWheelDelta() * 3), 0, MaxScroll);
        Rebuild();
        return FReply::Handled();
    }
    if(!OwnsPointer(P))if(auto* PC=Controller(Mode))
        if(auto* Pawn=Cast<AAC2Explorer>(PC->GetPawn()))Pawn->ZoomCamera(E.GetWheelDelta());
    return FReply::Handled();
}
FReply SAC2GameUI::OnKeyDown(const FGeometry &, const FKeyEvent &E)
{
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
    if(Name==TEXT("ShortcutUp") || Name==TEXT("ShortcutDown")){
        ShortcutSet=(ShortcutSet+(Name==TEXT("ShortcutUp")?9:1))%10;SaveShortcuts();Rebuild();return;
    }
    if(Name.ToString().StartsWith(TEXT("ShortcutSlot"))){ActivateShortcut(FCString::Atoi(*Name.ToString().Mid(12)));return;}
    if(OptionAction(Name) || ShopAction(Name) || JournalAction(Name))return;
    if (Name == TEXT("ChatEntry"))
    {
        BeginChat();
        return;
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
        ShowInventory=false;ShowSkills=Skills&&!WasOpen;ShowQuests=!Skills&&!WasOpen;JournalScroll=Skills?-1:0;
        if(Portrait.IsValid()){Portrait->SetActorTickEnabled(false);Portrait->Capture->bCaptureEveryFrame=false;}
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
    ShowInventory = !ShowInventory;ShowSkills=ShowQuests=false;
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
    InspectedObject.Reset();InspectedMob.Reset();ExamineScroll=0;
    ShowStats = !ShowStats;
    if(ShowStats && Mode.IsValid() && Mode->Network){InspectedObject=Mode->Network->PlayerId;Mode->Network->Examine(InspectedObject);}
    if (ShowStats && Mode.IsValid()) { ShowOptions=false; Mode->bMap = false; MapUI.Close(); }
    if (auto *PC = Controller(Mode))
        PC->SetCursor(!PC->WantsMouseLook() || ShowInventory || ShowStats || (Mode.IsValid() && Mode->bMap));
    Rebuild();
}
bool SAC2GameUI::ClosePanels()
{
    if(Mode.IsValid() && Mode->Network && !Mode->Network->LootContainer.IsEmpty()){Mode->Network->CloseLoot();return true;}
    if(Mode.IsValid() && Mode->Network && !Mode->Network->ShopVendor.IsEmpty()){Mode->Network->CloseShop();return true;}
    if (!ShowInventory && !ShowSkills && !ShowQuests && !ShowStats && !ShowOptions && (!Mode.IsValid() || !Mode->bMap))
        return false;
    if (Mode.IsValid()) Mode->bMap = false;
    MapUI.Close();
    ShowInventory = ShowStats = ShowOptions = ShowSkills = ShowQuests = false;
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
    if (Mode->bMap) ShowStats = ShowOptions = false;
    else MapUI.Close();
    if (auto *PC = Controller(Mode)) PC->SetCursor(!PC->WantsMouseLook() || ShowInventory || ShowStats || Mode->bMap);
    Rebuild();
}
void SAC2GameUI::SelectItem(int32 Index)
{
    SelectedItem = Index;
    const auto* P=Mode.IsValid()?Mode->Characters.Active():nullptr;
    SelectedNetworkItem=P && P->Items.IsValidIndex(Index)?P->Items[Index].NetworkId:FString();
    InspectedObject.Reset();InspectedMob.Reset();ExamineScroll=0;
    if(Mode.IsValid() && Mode->Network && !SelectedNetworkItem.IsEmpty()){
        InspectedObject=SelectedNetworkItem;
        Mode->Network->Examine(InspectedObject);
    }
    ShowStats = true;
    if (Mode.IsValid()) { Mode->bMap = false; MapUI.Close(); }
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
        Portrait->Image->ResizeTarget(306, 400);
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
    if (!PC || Editing || (H && H->CharacterUI && H->CharacterUI->IsOpen()))
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
void SAC2GameUI::Submit(const FString &Text)
{
    if(Mode.IsValid() && Text.TrimStartAndEnd().Equals(TEXT("/recall"),ESearchCase::IgnoreCase)){Mode->Recall();return;}
    if(Mode.IsValid() && Mode->Network && !Text.Equals(TEXT("/clear")) && !Text.Equals(TEXT("/loc")))
    { Log.Remember(Text); Mode->Network->Chat(Text); return; }
    auto Request = FAC2ChatRequest::Parse(Text);
    if (Text.TrimStartAndEnd().IsEmpty())
        return;
    Log.Remember(Text);
    Scroll = 0;
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
    Log.Lines=SavedLines;Rebuild();
    return true;
}
void SAC2GameUI::SmokeDraft()
{
    BeginChat();
    Entry->SetText(TEXT("/say Exploring Dereth!"));
}
