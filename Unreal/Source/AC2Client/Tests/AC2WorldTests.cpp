#include "AC2Sky.h"
#include "AC2WorldGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void Word(TArray<uint8> &B, uint32 V)
{
    for (int32 I = 0; I < 4; ++I)
        B.Add(uint8(V >> (8 * I)));
}
void Float(TArray<uint8> &B, float V)
{
    uint32 W;
    FMemory::Memcpy(&W, &V, 4);
    Word(B, W);
}
void Key(TArray<uint8> &B, float Time)
{
    Float(B, Time);
    for (int32 I = 0; I < 11; ++I)
        Word(B, 0); // fog and lighting, through shadow colour
    Word(B, 0);     // replacement count
    for (int32 I = 0; I < 3; ++I)
        Float(B, 0); // rain
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WorldGridTest, "AC2.Data.WorldGridAndResidency",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2WorldGridTest::RunTest(const FString &Parameters)
{
    uint32 Id = 0;
    const FVector Edge = AC2::BlockOrigin(0x5667) + FVector(15999.99, 8000, 200);
    TestTrue(TEXT("point just before seam"), AC2::WorldBlock(Edge, Id));
    TestEqual(TEXT("current tile"), Id, 0x5667u);
    AC2::WorldBlock(Edge + FVector(0.01, 0, 0), Id);
    TestEqual(TEXT("exact seam moves to next tile"), Id, 0x5767u);
    TestFalse(TEXT("negative coordinate does not wrap"), AC2::WorldBlock(FVector(-0.01, 0, 0), Id));
    TestFalse(TEXT("FF lattice is outside the outdoor world"),
              AC2::WorldBlock(FVector(255 * AC2::BlockCentimetres, 0, 0), Id));
    TestTrue(TEXT("last valid lattice point"),
             AC2::WorldBlock(FVector(255 * AC2::BlockCentimetres - 1, 255 * AC2::BlockCentimetres - 1, 0), Id));
    TestEqual(TEXT("last tile"), Id, 0xfefeu);
    const auto Middle = AC2::NearbyBlocks(0x5667, 2);
    TestEqual(TEXT("default residency request"), Middle.Num(), 25);
    TestEqual(TEXT("center is highest priority"), Middle[0], 0x5667u);
    TestEqual(TEXT("corner request clipped"), AC2::NearbyBlocks(0, 2).Num(), 9);
    TestEqual(TEXT("far corner request clipped"), AC2::NearbyBlocks(0xfefe, 2).Num(), 9);
    TestEqual(TEXT("radius bounded"), AC2::NearbyBlocks(0x8080, 100).Num(), 169);
    TestEqual(TEXT("hysteresis uses both axes"), AC2::BlockDistance(0x5667, 0x586a), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SkyDecodeTest, "AC2.Data.NativeSkyRecordBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2SkyDecodeTest::RunTest(const FString &Parameters)
{
    TArray<uint8> B;
    Word(B, 0);
    Word(B, 0x1a000002);
    Word(B, 2);
    Key(B, 0);
    Key(B, 1);
    Word(B, 0);
    FAC2SkyDay Day;
    TestTrue(TEXT("retail midnight terminal key at 1.0"), Day.Decode(0x1a000002, B));
    TestEqual(TEXT("both keys retained"), Day.Keys.Num(), 2);
    TestFalse(TEXT("wrong record identity"), Day.Decode(0x1a000003, B));
    B.Pop();
    TestFalse(TEXT("truncated object list"), Day.Decode(0x1a000002, B));
    B.Reset();
    Word(B, 0);
    Word(B, 0x1a000002);
    Word(B, 2);
    Key(B, .8f);
    Key(B, .2f);
    Word(B, 0);
    TestFalse(TEXT("unsorted keyframe times"), Day.Decode(0x1a000002, B));
    B.Reset();
    Word(B, 0);
    Word(B, 0x1a000002);
    Word(B, 0xffffffff);
    TestFalse(TEXT("oversized key count"), Day.Decode(0x1a000002, B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SkyCycleTest, "AC2.Data.NativeSkyCycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2SkyCycleTest::RunTest(const FString &Parameters)
{
    TestEqual(TEXT("cross-midnight interpolation"), AC2::SkyTimeRatio(.75f, .25f, .875f), .25f);
    TestEqual(TEXT("fixed celestial object"), AC2::SkyTimeRatio(.25f, .25f, .7f), 0.f);
    TestEqual(TEXT("eastward sweep crosses north"), AC2::SkySweep(350, 10, .5f, false), 0.f);
    TestEqual(TEXT("westward sweep crosses north"), AC2::SkySweep(10, 350, .5f, true), 0.f);
    FAC2SkyDay Day;
    FAC2SkyKey A, B;
    A.Time = .25f;
    A.Direct = 0;
    A.Heading = 350;
    B.Time = .75f;
    B.Direct = 1;
    B.Heading = 10;
    A.FogColor = FLinearColor::Red;
    B.FogColor = FLinearColor(0,1,1);
    FAC2SkyReplacement BeforeLayer, AfterLayer;
    BeforeLayer.Name = AfterLayer.Name = 1;
    BeforeLayer.Color = FLinearColor(1,0,0,0);
    AfterLayer.Color = FLinearColor(0,1,1,1);
    A.Replacements.Add(BeforeLayer);
    B.Replacements.Add(AfterLayer);
    Day.Keys = {A, B};
    TestTrue(TEXT("quarter-phase fog follows the retail half-turn hue through magenta"),
             Day.Sample(.375f).FogColor.Equals(FLinearColor(1,0,.75f)));
    TestEqual(TEXT("day lighting interpolation"), Day.Sample(.5f).Direct, .5f);
    TestTrue(TEXT("heading interpolates across north"),
             FMath::Abs(FMath::UnwindDegrees(Day.Sample(.5f).Heading)) < .001f);
    TestEqual(TEXT("exact key uses its own light"), Day.Sample(.75f).Direct, 1.f);
    TestTrue(TEXT("terminal midnight ratio is finite"), FMath::IsFinite(AC2::SkyTimeRatio(1.f,0.f,1.f)));
    TestEqual(TEXT("night lighting wraps"), Day.Sample(.875f).Direct, .75f);
    FAC2SkyObject Sun;
    Sun.Name = 1;
    Sun.Graphic = 0x04000023;
    Sun.Begin = .25f;
    Sun.End = .75f;
    Sun.EndPitch = 180;
    Day.Objects.Add(Sun);
    const auto Quarter = Day.Positions(.375f);
    TestTrue(TEXT("quarter-phase layer uses the native hue path and linear alpha"),
             Quarter.Num() == 1 && Quarter[0].Color.Equals(FLinearColor(1,0,.75f,.25f)));
    auto Noon = Day.Positions(.5f);
    TestEqual(TEXT("sun rises in native window"), Noon.Num(), 1);
    if (Noon.Num())
        TestEqual(TEXT("sun reaches midpoint"), Noon[0].Pitch, 90.f);
    TestEqual(TEXT("sun sets outside native window"), Day.Positions(.9f).Num(), 0);
    Day.Objects[0].After = true;
    TestTrue(TEXT("post-fog sky objects preserve their draw group"), Day.Positions(.5f)[0].After);
    Day.Objects[0].Water = 1;
    TestEqual(TEXT("underwater objects excluded outdoors"), Day.Positions(.5f).Num(), 0);
    TestEqual(TEXT("underwater sky admits its own object flag"), Day.Positions(.5f,true).Num(), 1);
    return true;
}
#endif
