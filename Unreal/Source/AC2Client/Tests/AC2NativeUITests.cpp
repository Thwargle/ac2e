#include "AC2NativeUI.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
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
        UI.Root(0x22000001, TEXT("RightSidePanel"));
        TestEqual(TEXT("inventory right anchored"), UI.Bounds(TEXT("InventoryField")).Max,
                  FIntPoint(Size.X, Size.Y));
        TestEqual(TEXT("inventory native size"), UI.Bounds(TEXT("InventoryField")).Size(),
                  FIntPoint(316, 414));
        TestEqual(TEXT("equipment template resolution"), UI.Bounds(TEXT("Head_Slot")).Size(),
                  FIntPoint(36, 36));
        UI.Root(0x22000001, TEXT("ChatWindow1"));
        UI.ConstrainRoot(TEXT("ChatWindow1"), FIntRect(0, 0, Size.X, UI.Bounds(TEXT("ToolbarField")).Min.Y));
        TestEqual(TEXT("chat retains authored size"), UI.Bounds(TEXT("ChatWindow1")).Size(),
                  FIntPoint(500, 144));
        TestTrue(TEXT("chat input remains above toolbar"),
                 UI.Bounds(TEXT("ChatWindow1")).Max.Y <= UI.Bounds(TEXT("ToolbarField")).Min.Y);
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
    UI.RootOffsets.Add(T(TEXT("RightSidePanel")),FIntPoint(-5000,-5000));
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,TEXT("RightSidePanel"));
    TestEqual(TEXT("off-screen saved panel is constrained"),UI.Bounds(TEXT("RightSidePanel")).Min,FIntPoint::ZeroValue);
    UI.RootOffsets[T(TEXT("RightSidePanel"))]+=FIntPoint(80,40);
    UI.Begin(FIntPoint(1600,900));UI.Root(0x22000001,TEXT("RightSidePanel"));
    TestEqual(TEXT("constrained panel moves immediately when dragged"),UI.Bounds(TEXT("RightSidePanel")).Min,FIntPoint(80,40));
    UI.RootOffsets.Reset();
    UI.Begin(FIntPoint(1024, 768));
    UI.Root(0x22000002, TEXT("Viewport"));
    UI.Root(0x22000002, TEXT("EnterButtonField"));
    UI.Root(0x22000002, TEXT("BackgroundField"));
    TestEqual(TEXT("Selection music retained from DAT"),UI.Find(TEXT("BackgroundField"))->Attr(TEXT("music")),0x2c00039au);
    TestTrue(TEXT("Native UI sound media retained"),UI.Layouts[0x22000002].Art.ContainsByPredicate([](const auto& M){return M.Kind==5 && M.Image==0x2a0000ecu;}));
    TArray<uint32> Roots;
    for (int32 I : UI.PaintOrder())
        if (UI.Nodes[I].Parent == INDEX_NONE)
            Roots.Add(UI.Nodes[I].Id);
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
        TestEqual(TEXT("explicit base state preserves hover or disabled state"),UI.Find(TEXT("CreateButton"))->State,
            Base==T(TEXT("ghosted"))?Base:T(TEXT("normal_rollover")));
        UI.Pressed=UI.Hovered;
        UI.Begin(FIntPoint(1024,768));UI.Root(0x22000002,TEXT("BackgroundField"));
        TestEqual(TEXT("explicit base state preserves press or disabled state"),UI.Find(TEXT("CreateButton"))->State,
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
