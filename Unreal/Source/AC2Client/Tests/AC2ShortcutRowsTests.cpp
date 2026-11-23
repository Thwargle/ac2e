#include "AC2NativeUI.h"
#include "AC2ShortcutData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShortcutRowsTest,"AC2.Data.ShortcutRows",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShortcutRowsTest::RunTest(const FString&)
{
    FAC2NativeUI UI;FString Error;
    if(!TestTrue(TEXT("Installed native layout opens"),UI.Open(AC2::ResolveDatDirectory(TEXT("")),Error))){AddError(Error);return false;}
    const uint32 Root=FAC2UILayout::Token(TEXT("ShortcutField"));
    for(const auto Size:{FIntPoint(1024,768),FIntPoint(1280,720),FIntPoint(2560,1440)})
        for(const auto Offset:{FIntPoint(0,0),FIntPoint(-80,-120)}){
            UI.RootOffsets.Add(Root,Offset);UI.RootBottomRows.Add(Root,1);
            UI.Begin(Size);UI.Root(0x22000001,TEXT("ShortcutField"));const auto Base=UI.Bounds(TEXT("ShortcutField"));
            for(int32 Rows=1;Rows<=5;++Rows){
                UI.RootBottomRows.Add(Root,Rows);UI.Begin(Size);UI.Root(0x22000001,TEXT("ShortcutField"));
                const auto Frame=UI.Bounds(TEXT("ShortcutField")),List=UI.Bounds(TEXT("ShortcutListBox"));
                TestEqual(TEXT("Rows preserve the bottom-right corner including saved offsets"),Frame.Max,Base.Max);
                TestEqual(TEXT("Native height is border plus complete rows"),Frame.Height(),12+32*Rows);
                TestEqual(TEXT("Rows do not stretch slot width"),Frame.Width(),Base.Width());
                TestEqual(TEXT("Referenced list grows with native parent anchors"),List.Height(),32*Rows);
                TestEqual(TEXT("List retains the native top border"),List.Min.Y,Frame.Min.Y+12);
                TestEqual(TEXT("List reaches the bottom edge without clipping rows"),List.Max.Y,Frame.Max.Y);
                TestEqual(TEXT("Rebuilding does not accumulate window offsets"),UI.RootOffsets.FindRef(Root),Offset);
            }
        }
    for(int32 Rows=1;Rows<=5;++Rows)for(int32 Page=0;Page<10;++Page){
        TSet<int32> Bound;
        for(int32 Slot=0;Slot<Rows*10;++Slot){
            const int32 Index=FAC2ShortcutGrid::Index(Page,Slot,Rows);
            if(Index==INDEX_NONE){TestTrue(TEXT("Only end padding is inactive"),Page*10+Slot>=100);continue;}
            TestFalse(TEXT("A visible binding is never duplicated"),Bound.Contains(Index));Bound.Add(Index);
            TestEqual(TEXT("Each subsequent row shows the next page's bindings"),Index,Page*10+Slot);
        }
        TestEqual(TEXT("Ten binding pages with disabled trailing padding"),Bound.Num(),FMath::Min(Rows*10,100-Page*10));
        TestEqual(TEXT("Hidden slots cannot activate"),FAC2ShortcutGrid::Index(Page,Rows*10,Rows),INDEX_NONE);
    }
    TestEqual(TEXT("Invalid negative slot cannot index bindings"),FAC2ShortcutGrid::Index(0,-1,5),INDEX_NONE);
    TestEqual(TEXT("Invalid page cannot index bindings"),FAC2ShortcutGrid::Index(MAX_int32,0,5),INDEX_NONE);
    TestEqual(TEXT("Old settings default to one row"),FAC2ShortcutGrid::Rows(0),1);
    TestEqual(TEXT("Corrupt settings cannot allocate extra rows"),FAC2ShortcutGrid::Rows(MAX_int32),5);
    UI.Begin({1600,900});UI.Root(0x22000001,TEXT("ShortcutField"));
    const auto* List=UI.Find(TEXT("ShortcutListBox"));
    if(TestNotNull(TEXT("Native scrolling list"),List))TestEqual(TEXT("Native scroll duration is a quarter second"),List->Float(TEXT("smooth_movement_duration")),.25f);
    FAC2ShortcutScroll Scroll;Scroll.Start(32,100,.25);
    TestEqual(TEXT("Scrolling starts at the old view"),Scroll.View(100),0);
    TestEqual(TEXT("Midpoint exposes two clipped rows"),Scroll.View(100.125),16);
    TestEqual(TEXT("Scrolling reaches exact target at deadline"),Scroll.View(100.25),32);
    TestFalse(TEXT("Expired transition stays settled"),Scroll.Moving(101));
    Scroll.Snap(0);Scroll.Start(32,100,.25);Scroll.Start(64,100.125,.25);
    TestEqual(TEXT("Repeated paging does not jump at retarget"),Scroll.View(100.125),16);
    TestEqual(TEXT("Retarget finishes in half a duration"),Scroll.View(100.25),64);
    Scroll.Snap(0);Scroll.Start(32,100,.25);Scroll.Start(0,100.125,.25);
    TestEqual(TEXT("Reversing begins at current view"),Scroll.View(100.125),16);
    TestEqual(TEXT("Reversing completes at earlier page"),Scroll.View(100.25),0);
    Scroll.Snap(288);Scroll.Start(0,200,.25);
    TestEqual(TEXT("Page ten to one passes through existing rows"),Scroll.View(200.125),144);
    Scroll.Snap(96);TestEqual(TEXT("Instant option settles an active scroll"),Scroll.View(200.125),96);
    Scroll.Start(160,201,0);TestEqual(TEXT("Zero duration is immediate"),Scroll.View(201),160);
    return true;
}
#endif
