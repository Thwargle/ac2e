#include "AC2AssetReader.h"
#include "AC2Scenery.h"
#include "AC2MaterialAlpha.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void AssetWord(TArray<uint8> &Bytes, uint32 V)
{
    for (int32 I = 0; I < 4; ++I)
        Bytes.Add(uint8(V >> (I * 8)));
}
TArray<uint8> Surface(uint32 Format, int32 W, int32 H, TConstArrayView<uint8> Payload)
{
    TArray<uint8> B;
    for (uint32 V : {0x41000001u, 0u, uint32(W), uint32(H), Format, uint32(Payload.Num())})
        AssetWord(B, V);
    B.Append(Payload.GetData(), Payload.Num());
    return B;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AssetReaderTest, "AC2.Data.AssetReaderBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2AssetReaderTest::RunTest(const FString &Parameters)
{
    TArray<uint8> B = {0xff, 0xff, 0xff, 0xff};
    FAC2AssetReader R(B);
    TestEqual(TEXT("oversized count returns no work"), R.Count(16), 0);
    TestFalse(TEXT("count failure is sticky"), R.Good);
    TestFalse(TEXT("cannot recover by advancing"), R.Take(0));
    FAC2AssetReader Short(B);
    Short.Take(3);
    TestEqual(TEXT("unaligned truncated word"), Short.Word(), 0u);
    TestFalse(TEXT("truncated word fails"), Short.Good);
    FAC2AssetReader String(B);
    String.String();
    TestFalse(TEXT("extended string length cannot exceed record"), String.Good);
    B.Reset();
    AssetWord(B, 0x7fc00000);
    FAC2AssetReader Float(B);
    Float.Float();
    TestFalse(TEXT("NaN rejected before entering mesh transforms"), Float.Good);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SurfaceTest, "AC2.Data.SurfacePixelsAndAlpha",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2SurfaceTest::RunTest(const FString &Parameters)
{
    int32 W = 0, H = 0;
    TArray<FColor> Pixels;
    const uint8 RGB[] = {3, 5, 7, 11, 13, 17};
    auto B = Surface(20, 2, 1, RGB);
    TestTrue(TEXT("BGR surface"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    TestEqual(TEXT("BGR channel order"), Pixels[0], FColor(7, 5, 3));
    TestEqual(TEXT("second RGB pixel"), Pixels[1], FColor(17, 13, 11));
    const uint8 BC1[] = {0x00, 0xf8, 0xe0, 0x07, 0xe4, 0xe4, 0xe4, 0xe4};
    B = Surface(0x31545844, 4, 4, BC1);
    TestTrue(TEXT("BC1 block"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    TestEqual(TEXT("red 565 endpoint"), Pixels[0], FColor(255, 0, 0));
    TestEqual(TEXT("green 565 endpoint"), Pixels[1], FColor(0, 255, 0));
    TestEqual(TEXT("BC1 interpolation"), Pixels[2], FColor(170, 85, 0));
    const uint8 BC1Alpha[] = {0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    B = Surface(0x31545844, 3, 2, BC1Alpha);
    TestTrue(TEXT("edge block dimensions"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    TestEqual(TEXT("edge block size"), Pixels.Num(), 6);
    TestEqual(TEXT("BC1 transparent mode"), Pixels[5].A, uint8(0));
    const uint8 BC3[] = {32, 200, 0x88, 0xc6, 0xfa, 0x88, 0xc6, 0xfa, 0, 0xf8, 0, 0, 0, 0, 0, 0};
    B = Surface(0x35545844, 4, 4, BC3);
    TestTrue(TEXT("BC3 alpha block"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    TestEqual(TEXT("BC3 alpha endpoint"), Pixels[0].A, uint8(32));
    TestEqual(TEXT("BC3 alpha second endpoint"), Pixels[1].A, uint8(200));
    TestEqual(TEXT("BC3 zero sentinel"), Pixels[6].A, uint8(0));
    TestEqual(TEXT("BC3 opaque sentinel"), Pixels[7].A, uint8(255));
    B.Pop();
    TestFalse(TEXT("truncated block rejected"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    TestTrue(TEXT("failed decode has no partial pixels"), Pixels.IsEmpty());
    B = Surface(20, 0x7fffffff, 0x7fffffff, RGB);
    TestFalse(TEXT("dimensions bounded before allocation"), DecodeAC2Surface(B, 0x41000001, W, H, Pixels));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SceneryHashTest, "AC2.Data.SceneryDeterminism",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2SceneryHashTest::RunTest(const FString &Parameters)
{
    // Golden values from the Legions 7160 integer expressions, including overflow.
    TestEqual(TEXT("scene choice"), AC2::SceneRandom(3149, 1245), 0.5406951904296875);
    TestEqual(TEXT("second scene choice"), AC2::SceneRandom(3260, 4000), 0.9131622314453125);
    TestEqual(TEXT("location draw"), AC2::PlacementRandom(3149, 1245, 0x28d0642d), 0.745086669921875);
    TestEqual(TEXT("scale draw"), AC2::PlacementRandom(3260, 4000, 0x0de876c7), 0.4124755859375);
    TestEqual(TEXT("unsigned wrapping"), AC2::PlacementRandom(4095, 4095, 0xffffffff), 0.92742919921875);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MaterialAlphaTest, "AC2.Data.NativeMaterialOpacity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2MaterialAlphaTest::RunTest(const FString &Parameters)
{
    // Native human material 2B000000: alpha SELECTARG2 through texture/specular/light stages.
    const TArray<FAC2AlphaStage> Body = {{3, 2, 0}, {3, 2, 1}, {3, 0, 1}};
    TestEqual(TEXT("body mask does not punch holes in skin"), EvaluateAC2Alpha(Body, 0.f), 1.f);
    TestEqual(TEXT("body stays opaque with fractional texture alpha"), EvaluateAC2Alpha(Body, .4f), 1.f);
    TestEqual(TEXT("constant red helper retains zero diffuse opacity"), EvaluateAC2Alpha(Body, 1.f, 0.f), 0.f);
    const TArray<FAC2AlphaStage> Leaves = {{2, 2, 0}, {4, 1, 0}};
    TestEqual(TEXT("leaf card transparency retained"), EvaluateAC2Alpha(Leaves, 0.f), 0.f);
    TestEqual(TEXT("leaf texture alpha modulates diffuse"), EvaluateAC2Alpha(Leaves, .5f, .5f), .25f);
    const TArray<FAC2AlphaStage> Complement = {{2, 2 | 16, 0}};
    TestEqual(TEXT("complement argument"), EvaluateAC2Alpha(Complement, .25f), .75f);
    return true;
}
#endif
