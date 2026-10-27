#include "AC2MapData.h"
#include "AC2NativeUI.h"
#include "AC2MapUI.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MapCoordinatesTest, "AC2.Data.MapCoordinates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2MapCoordinatesTest::RunTest(const FString &)
{
    const FIntRect Pixels(8, 33, 246, 271);
    const FVector2D Origin(400, 600), Extent(128, 128);
    TestTrue(TEXT("north is at screen top"),
             FAC2MapData::Project(FVector2D(400, 728), Origin, Extent, Pixels).Equals(FVector2D(8, 33)));
    TestTrue(TEXT("south east is at bottom right"),
             FAC2MapData::Project(FVector2D(528, 600), Origin, Extent, Pixels).Equals(FVector2D(246, 271)));
    for (FVector2D Cell : {Origin, Origin + FVector2D(3.5, 29.25), Origin + Extent})
        TestTrue(
            TEXT("click/marker inverse agrees"),
            FAC2MapData::Unproject(FAC2MapData::Project(Cell, Origin, Extent, Pixels), Origin, Extent, Pixels)
                .Equals(Cell, .00001));
    TestTrue(TEXT("land block seam is continuous"),
             FAC2MapData::Landscape(FVector(16001, 15999, 0)).Equals(FVector2D(8.0005, 7.9995)));
    TestTrue(TEXT("lower map boundary"),
             FAC2MapData::DetailOrigin(FVector2D(-1, -100)).Equals(FVector2D::ZeroVector));
    TestTrue(TEXT("upper map boundary"),
             FAC2MapData::DetailOrigin(FVector2D(2040, 5000)).Equals(FVector2D(1912, 1912)));
    TestTrue(TEXT("north facing: east is right"),
             FAC2MapData::RadarOffset(FVector(7500, 0, 0), 90, 75).Equals(FVector2D(60, 0), .001));
    TestTrue(TEXT("east facing: east is ahead"),
             FAC2MapData::RadarOffset(FVector(7500, 0, 0), 0, 75).Equals(FVector2D(0, -60), .001));
    TestTrue(TEXT("east facing: north is left"),
             FAC2MapData::RadarOffset(FVector(0, 5000, 0), 0, 50).Equals(FVector2D(-60, 0), .001));
    TestEqual(TEXT("retail N/E coordinate convention"), FAC2MapData::Coordinates(FVector2D(1122, 918)),
              FString(TEXT("10.0S, 10.0E")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MapResourcesTest, "AC2.Data.NativeMapResources",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2MapResourcesTest::RunTest(const FString &)
{
    const FString Directory = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FString Error;
    FAC2MapData Data;
    if (!TestTrue(TEXT("7160 region, block-map, detail tiles and notes decode"), Data.Open(Directory, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("region's authored map image"), Data.Image, 0x41100004u);
    TestEqual(TEXT("all detail tiles"), Data.Tiles.Num(), 95);
    TestTrue(TEXT("native note catalog"), Data.Notes.Num() > 0);
    FAC2AssetLibrary Assets;
    if (!Assets.Open(Directory, Error))
    {
        AddError(Error);
        return false;
    }
    TSet<uint32> Images = {Data.Image, 0x41100000, 0x41006f0e, 0x41006f0f,
                           0x41006f10, 0x41007bf3, 0x4100a913};
    for (const auto &Row : Data.Tiles)
        Images.Add(Row.Image);
    int32 Labeled = 0;
    for (const auto &Entry : Data.Notes)
        for (const auto &N : Entry.Value)
            if (!N.Label.IsEmpty())
                ++Labeled;
    for (const auto &Entry : Data.Notes)
        for (const auto &N : Entry.Value)
        {
            TestFalse(TEXT("localized map note has no unresolved text"), N.Label.IsEmpty());
            Images.Add(N.Image);
        }
    for (uint32 Id : Images)
        TestNotNull(*FString::Printf(TEXT("DAT map texture %08X"), Id), Assets.Texture(Id));
    TestTrue(TEXT("map note localized labels"), Labeled > 10);
    TestNull(TEXT("out of world cells have no tile"), Data.At(FIntPoint(-1, 0)));
    const auto *Tile = Data.At(FIntPoint(692, 828));
    TestNotNull(TEXT("starter landscape detail map"), Tile);
    FAC2NativeUI UI;
    if (!UI.Open(Directory, Error))
    {
        AddError(Error);
        return false;
    }
    for (FIntPoint Size : {FIntPoint(1024, 768), FIntPoint(1280, 720), FIntPoint(1600, 900)})
    {
        UI.Begin(Size);
        UI.Template(0x22000005, TEXT("RadarField"), FAC2MapUI::RadarPlacement(UI.Layouts[0x22000005], Size),
                    FIntRect(0, 0, Size.X, Size.Y));
        UI.Root(0x22000001, TEXT("LeftSidePanel"));
        TestEqual(TEXT("native radar size"), UI.Bounds(TEXT("RadarField")).Size(), FIntPoint(143, 177));
        TestEqual(TEXT("radar anchored to right edge"), UI.Bounds(TEXT("RadarField")).Max.X, Size.X - 6);
        TestEqual(TEXT("map remains pixel accurate on viewport resize"),
                  UI.Bounds(TEXT("MapParentField")).Size(), FIntPoint(238, 238));
        const auto *Close = UI.Find(TEXT("NavigationCloseButton"));
        TestTrue(TEXT("reference close button has normal-state artwork"),
                 Close && Close->Media.ContainsByPredicate([](const auto &M) { return M.Kind == 3; }));
    }
    return true;
}
#endif
