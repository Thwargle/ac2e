#include "AC2NativeUI.h"
#include "AC2Network.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
// Run with each supported DAT directory: later retail revisions expose the
// individual panels as roots instead of the older Left/RightSidePanel wrappers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ProfilePanelsTest, "AC2.Data.ProfilePanels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2ProfilePanelsTest::RunTest(const FString&)
{
    FAC2NativeUI UI;FString Error;
    if(!UI.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error)){AddError(Error);return false;}
    auto T=[](const TCHAR* Name){return FAC2UILayout::Token(Name);};
    const TCHAR* Fields[]={TEXT("InventoryField"),TEXT("SkillField"),TEXT("QuestField"),TEXT("SocialField"),
        TEXT("ExaminationField"),TEXT("OptionField"),TEXT("NavigationField")};
    int32 HeaderCount=0;
    for(const TCHAR* Field:Fields)for(FIntPoint Size:{FIntPoint(1024,768),FIntPoint(1600,900)}) {
        UI.RootOffsets.Reset();UI.Visibility.Reset();
        for(const TCHAR* Other:Fields)UI.Visibility.Add(T(Other),Other==Field);
        UI.Begin(Size);
        const TCHAR* Root=UI.PanelRoot(Field);
        if(!TestTrue(FString(Field)+TEXT(" root exists"),UI.Root(0x22000001,Root)))continue;
        const auto* Panel=UI.Find(Field);
        if(!TestNotNull(FString(Field)+TEXT(" panel exists"),Panel))continue;
        TestTrue(TEXT("panel visible and clipped to display"),Panel->Clip.Area()>0 && Panel->Clip.Min.X>=0 && Panel->Clip.Min.Y>=0 && Panel->Clip.Max.X<=Size.X && Panel->Clip.Max.Y<=Size.Y);
        const FIntRect Before=Panel->Rect;
        const FIntRect RootRect=UI.Bounds(Root);
        const FIntPoint Delta(RootRect.Width()+24>Size.X?0:(RootRect.Min.X+RootRect.Max.X>Size.X?-12:12),
            RootRect.Height()+24>Size.Y?0:(RootRect.Min.Y+RootRect.Max.Y>Size.Y?-12:12));
        UI.RootOffsets.FindOrAdd(T(Root))+=Delta;UI.Begin(Size);UI.Root(0x22000001,Root);
        TestEqual(FString(Field)+TEXT(" panel and hit bounds follow their actual root"),UI.Bounds(Field).Min,Before.Min+Delta);
        TestEqual(TEXT("moving panel preserves authored size"),UI.Bounds(Field).Size(),Before.Size());
        UI.RootOffsets.FindOrAdd(T(Root))+=FIntPoint(0,Size.Y);
        UI.Begin(Size);UI.Root(0x22000001,Root);
        TestEqual(TEXT("panel can reach the bottom edge"),UI.Bounds(Root).Max.Y,Size.Y);
        int32 PanelHeaders=0;
        for(const auto& Title:UI.Nodes)if(Title.FitHeader) {
            ++HeaderCount;
            ++PanelHeaders;
            TestTrue(TEXT("title retains usable width"),Title.Clip.Width()>0);
            TestTrue(TEXT("title remains inside the caption when content stretches"),Title.Rect.Height()<=30);
            for(const auto& Button:UI.Nodes)if(Button.Parent==Title.Parent &&
                (Button.Attr(TEXT("referent"))==4254656600u || Button.Attr(TEXT("referent"))==4229490776u)) {
                TestTrue(TEXT("title leaves four pixels before header controls"),Title.Clip.Max.X<=Button.Rect.Min.X-4);
                TestEqual(TEXT("header controls retain native hit size"),Button.Rect.Size(),FIntPoint(17,17));
            }
        }
        TestTrue(FString(Field)+TEXT(" has a padded native title at this viewport size"),PanelHeaders>0);
    }
    TestTrue(TEXT("Title padding assertions exercised native headers"),HeaderCount>=14);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeWindowsTest, "AC2.Data.NativeWindowLayouts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2NativeWindowsTest::RunTest(const FString &)
{
    FAC2NativeUI UI;
    FString Error;
    if (!TestTrue(TEXT("installed layout trees"),
                  UI.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")), Error)))
    {
        AddError(Error);
        return false;
    }
    auto T = [](const TCHAR *Name) { return FAC2UILayout::Token(Name); };
    const TCHAR* InventoryRoot = UI.PanelRoot(TEXT("InventoryField"));
    const uint32 InventoryId = T(InventoryRoot);
    for (const TCHAR *Name : {TEXT("QuestField"), TEXT("SkillField"), TEXT("SocialField"),
                              TEXT("NavigationField"), TEXT("OptionField"), TEXT("PackOneContainer"),
                              TEXT("PackTwoContainer"), TEXT("PackThreeContainer")})
        UI.Visibility.Add(T(Name), false);
    for (FIntPoint Size : {FIntPoint(1024, 768), FIntPoint(1600, 900), FIntPoint(1280, 720)})
    {
        UI.Begin(Size);
        TestTrue(TEXT("native toolbar"), UI.Root(0x22000001, TEXT("ToolbarField")));
        TestEqual(TEXT("toolbar retains native pixel width"), UI.Bounds(TEXT("ToolbarField")).Width(), 798);
        TestEqual(TEXT("toolbar centered"), UI.Bounds(TEXT("ToolbarField")).Min.X, Size.X / 2 - 399);
        TestEqual(TEXT("toolbar bottom edge"), UI.Bounds(TEXT("ToolbarField")).Max.Y, Size.Y);
        UI.Root(0x2200002b, TEXT("StatusbarField"));
        TestEqual(TEXT("health origin"), UI.Bounds(TEXT("HealthField")).Min, FIntPoint(67, 0));
        TestEqual(TEXT("native health width"), UI.Bounds(TEXT("HealthField")).Width(), 215);
        if (!TestTrue(TEXT("inventory window resolves on this layout"), UI.Root(0x22000001, InventoryRoot)))
            return false;
        TestEqual(TEXT("inventory right anchored"), UI.Bounds(TEXT("InventoryField")).Max,
                  FIntPoint(Size.X, Size.Y));
        TestEqual(TEXT("inventory native size"), UI.Bounds(TEXT("InventoryField")).Size(),
                  FIntPoint(316, 414));
        TestEqual(TEXT("equipment template resolution"), UI.Bounds(TEXT("Head_Slot")).Size(),
                  FIntPoint(36, 36));
        UI.RootOffsets.Add(T(TEXT("ChatWindow1")),FIntPoint(0,Size.Y));
        UI.Root(0x22000001, TEXT("ChatWindow1"));
        TestEqual(TEXT("chat retains authored size"), UI.Bounds(TEXT("ChatWindow1")).Size(),
                  FIntPoint(500, 144));
        TestEqual(TEXT("chat can be dragged to the viewport bottom"),
                  UI.Bounds(TEXT("ChatWindow1")).Max.Y,Size.Y);
        const auto *Entry = UI.Find(TEXT("ChatEntry")), *Log = UI.Find(TEXT("ChatLog"));
        if (!TestNotNull(TEXT("chat entry template"), Entry) || !TestNotNull(TEXT("chat log template"), Log))
            return false;
        TestTrue(TEXT("resized template stretches entry"), Entry->Rect.Width() > 400);
        TestTrue(TEXT("log stays inside frame"), UI.Bounds(TEXT("ChatWindow1")).Contains(Log->Rect.Min));
        TestEqual(TEXT("entry DAT font"), Entry->Attr(TEXT("font")), 0x4200000cu);
        const int32 ChatIndex=int32(UI.Find(TEXT("ChatWindow1"))-UI.Nodes.GetData());
        const auto* ChatBackground=UI.Nodes.FindByPredicate([&](const auto& N){return N.Parent==ChatIndex && N.Id==0xe930eb61u;});
        if(!TestNotNull(TEXT("native chat background"),ChatBackground))return false;
        const int32 BackgroundIndex=int32(ChatBackground-UI.Nodes.GetData());
        TArray<FIntRect> Tiles;
        for(const auto& N:UI.Nodes)if(N.Parent==BackgroundIndex)Tiles.Add(N.Clip);
        TestEqual(TEXT("nine chat background tiles"),Tiles.Num(),9);
        for(int32 I=0;I<Tiles.Num();++I)for(int32 J=I+1;J<Tiles.Num();++J){
            FIntRect Overlap=Tiles[I];Overlap.Clip(Tiles[J]);
            TestTrue(TEXT("translucent chat frame tiles never double-blend a seam"),Overlap.Width()<=0 || Overlap.Height()<=0);
        }
    }
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,TEXT("ChatWindow1"));
    const auto OldLog=UI.Bounds(TEXT("ChatLog"));
    UI.RootSizes.Add(T(TEXT("ChatWindow1")),FIntPoint(350,300));
    UI.RootSizes.Add(InventoryId,FIntPoint(600,600));
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,TEXT("ChatWindow1"));UI.Root(0x22000001,InventoryRoot);
    TestEqual(TEXT("chat resizes without scaling its font"),UI.Bounds(TEXT("ChatWindow1")).Size(),FIntPoint(350,300));
    TestEqual(TEXT("chat text width reflows with its border"),UI.Bounds(TEXT("ChatLog")).Width(),OldLog.Width()-150);
    TestEqual(TEXT("chat text height reflows with its border"),UI.Bounds(TEXT("ChatLog")).Height(),OldLog.Height()+156);
    TestEqual(TEXT("fixed inventory ignores resized dimensions"),UI.Bounds(TEXT("InventoryField")).Size(),FIntPoint(316,414));
    TestEqual(TEXT("native three-variable quest detail formatting"),UI.Localized(TEXT("ID_Quest_Description"),
        {{T(TEXT("DESC")),TEXT("Description")},{T(TEXT("JOURNAL")),TEXT("Objective")},{T(TEXT("LEVEL")),TEXT("3")}}),
        FString(TEXT("Description\nObjective\n\nIntended Level: 3")));
    FAC2Network::FQuest Quest;Quest.Status=0x10000000;Quest.Expires=true;Quest.Received=100;Quest.Remaining=60;
    TestEqual(TEXT("quest deadline counts down monotonically"),Quest.Seconds(130),30.);
    TestEqual(TEXT("expired task moves to finished"),Quest.DisplayStatus(161),0x20000000u);
    Quest.Status=0x30000000;Quest.Retry=72000;
    TestEqual(TEXT("completed task counts down its retry clock"),Quest.Seconds(130),71970.);
    TestEqual(TEXT("completion is never replaced by expiration"),Quest.DisplayStatus(90000),0x30000000u);
    UI.RootSizes.Reset();
    UI.RootOffsets.Add(InventoryId,FIntPoint(-5000,-5000));
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,InventoryRoot);
    TestEqual(TEXT("off-screen saved panel is constrained"),UI.Bounds(InventoryRoot).Min,FIntPoint::ZeroValue);
    TestEqual(TEXT("layout preserves saved placement"),UI.RootOffsets.FindRef(InventoryId),FIntPoint(-5000,-5000));
    const auto* DisplayedOffset = UI.DisplayedRootOffsets.Find(InventoryId);
    if (!TestNotNull(TEXT("inventory root exposes its constrained drag origin"), DisplayedOffset))
        return false;
    UI.RootOffsets.Add(InventoryId,*DisplayedOffset+FIntPoint(80,40));
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,InventoryRoot);
    TestEqual(TEXT("constrained panel moves immediately when dragged"),UI.Bounds(InventoryRoot).Min,FIntPoint(80,40));
    UI.RootOffsets.Reset();
    UI.Begin(FIntPoint(1024, 768));
    UI.Root(0x22000002, TEXT("Viewport"));
    UI.Root(0x22000002, TEXT("EnterButtonField"));
    UI.Root(0x22000002, TEXT("BackgroundField"));
    const auto* Background = UI.Find(TEXT("BackgroundField"));
    if (!TestNotNull(TEXT("selection background"), Background))
        return false;
    const auto* AuthoredBackground = UI.Layouts[0x22000002].Find(TEXT("BackgroundField"));
    const auto* BackgroundState = AuthoredBackground ? AuthoredBackground->States.FindByPredicate(
        [&](const auto& S){ return S.Id == AuthoredBackground->DefaultState; }) : nullptr;
    const auto* Music = BackgroundState ? BackgroundState->Attributes.Find(T(TEXT("music"))) : nullptr;
    if (!TestNotNull(TEXT("selection background defines its music in the DAT"), Music))
        return false;
    TestTrue(TEXT("selection music is a native music asset"), (*Music >> 24) == 0x2c);
    TestEqual(TEXT("selection uses this DAT's own background music"), Background->Attr(TEXT("music")), *Music);
    TestTrue(TEXT("Native UI sound media retained"),UI.Layouts[0x22000002].Art.ContainsByPredicate([](const auto& M){return M.Kind==5 && M.Image==0x2a0000ecu;}));
    TArray<uint32> Roots;
    for (int32 I : UI.PaintOrder())
        if (UI.Nodes[I].Parent == INDEX_NONE)
            Roots.Add(UI.Nodes[I].Id);
    if (!TestEqual(TEXT("selection screen has three active layers"), Roots.Num(), 3))
        return false;
    TestEqual(TEXT("native background drawn behind selection viewport"), Roots[0],
              T(TEXT("BackgroundField")));
    TestEqual(TEXT("native viewport draws before foreground controls"), Roots[1], T(TEXT("Viewport")));
    TestEqual(TEXT("native foreground controls"), Roots[2], T(TEXT("EnterButtonField")));
    UI.Begin(FIntPoint(1024, 768));
    UI.States.Add(T(TEXT("ButtonField")), T(TEXT("nonadminclient")));
    UI.Root(0x22000008, TEXT("GenerationConfirmationField"));
    TestEqual(TEXT("centered native name entry"), UI.Bounds(TEXT("NameEntry")), FIntRect(417, 348, 607, 368));
    TestEqual(TEXT("non-admin enter button"), UI.Bounds(TEXT("EnterButton")), FIntRect(517, 378, 597, 408));
    TestTrue(TEXT("native localized dialog text"),
             UI.Nodes.ContainsByPredicate([](const auto &N) { return !N.Text.IsEmpty(); }));
    UI.Begin(FIntPoint(1024, 768));
    UI.States.Add(T(TEXT("RaceDescField")), T(TEXT("humanmale")));
    UI.Root(0x22000006, TEXT("AvatarSelectionFieldBottom"));
    TestTrue(TEXT("race descriptions come from DAT string table"),
             UI.Nodes.ContainsByPredicate([](const auto &N) { return N.Text.Contains(TEXT("Human")); }));
    UI.Begin(FIntPoint(1024, 768));
    UI.Hovered = T(TEXT("InventoryButton"));
    UI.Root(0x22000001, TEXT("ToolbarField"));
    const auto *Button = UI.Find(TEXT("InventoryButton"));
    if (TestNotNull(TEXT("inventory button"), Button))
    {
        TestEqual(TEXT("native rollover state"), Button->State, T(TEXT("normal_rollover")));
        TestTrue(TEXT("native rollover artwork"),
                 Button->Media.ContainsByPredicate([](const auto &M) { return M.Image == 0x410009cdu; }));
    }
    UI.States.Reset();UI.Visibility.Reset();
    for(uint32 Base:{T(TEXT("normal")),T(TEXT("ghosted"))})
    {
        UI.States.Add(T(TEXT("CreateButton")),Base);
        UI.Hovered=T(TEXT("CreateButton"));UI.Pressed=0;
        UI.Begin(FIntPoint(1024,768));UI.Root(0x22000002,TEXT("BackgroundField"));
        const auto* CreateButton = UI.Find(TEXT("CreateButton"));
        if (!TestNotNull(TEXT("character creation button resolves for hover"), CreateButton))
            return false;
        TestEqual(TEXT("explicit base state preserves hover or disabled state"),CreateButton->State,
            Base==T(TEXT("ghosted"))?Base:T(TEXT("normal_rollover")));
        UI.Pressed=UI.Hovered;
        UI.Begin(FIntPoint(1024,768));UI.Root(0x22000002,TEXT("BackgroundField"));
        CreateButton = UI.Find(TEXT("CreateButton"));
        if (!TestNotNull(TEXT("character creation button resolves for press"), CreateButton))
            return false;
        TestEqual(TEXT("explicit base state preserves press or disabled state"),CreateButton->State,
            Base==T(TEXT("ghosted"))?Base:T(TEXT("normal_pressed")));
    }
    auto R=UI.Bounds(TEXT("CharacterListBox"));
    UI.Text.Add(T(TEXT("list_box_item")),TEXT("Selection Test"));
    UI.Template(0x22000002,TEXT("list_box_item"),FIntRect(R.Min,R.Min+FIntPoint(R.Width(),20)),R,T(TEXT("CharacterListBox")));
    const auto *Row=UI.Find(TEXT("list_box_item"));
    TestTrue(TEXT("character row belongs to native list, above its background"),Row && Row->Parent!=INDEX_NONE && UI.Nodes[Row->Parent].Id==T(TEXT("CharacterListBox")) && Row->Text==TEXT("Selection Test"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeCursorTest,"AC2.Data.NativeCursors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2NativeCursorTest::RunTest(const FString&)
{
    FAC2NativeUI UI;FString Error;
    if(!UI.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")),Error)){AddError(Error);return false;}
    UI.Begin(FIntPoint(1024,768));
    TestTrue(TEXT("native mouse-look crosshair"),UI.Root(0x22000005,TEXT("MouseLookCursorField")));
    TestEqual(TEXT("crosshair center"),UI.Bounds(TEXT("MouseLookCursorField")).Min,FIntPoint(501,373));
    UI.Begin(FIntPoint(1024,768));
    UI.Root(0x22000007,TEXT("AvatarModificationField"));
    const auto* Rotate=UI.Find(TEXT("MouseRotationOverField"));
    if(!TestNotNull(TEXT("native rotation field"),Rotate))return false;
    uint32 Image=0;FIntPoint Hotspot;
    TestTrue(TEXT("rotation field cursor inherited through children"),UI.CursorAt(FVector2D(Rotate->Rect.Min+FIntPoint(10,10)),Image,Hotspot));
    TestEqual(TEXT("native rotation cursor"),Image,0x41000d0du);
    TestEqual(TEXT("native rotation hotspot"),Hotspot,FIntPoint(17,14));
    // A topmost control with no cursor must restore the default, never leak a covered sibling's cursor.
    FAC2NativeNode Cover;Cover.Id=123;Cover.Rect=Cover.Clip=Rotate->Rect;Cover.Z=-100;
    UI.Nodes.Add(Cover);
    TestFalse(TEXT("covered cursor does not bleed through"),UI.CursorAt(FVector2D(Cover.Rect.Min+FIntPoint(10,10)),Image,Hotspot));
    return true;
}
#endif
