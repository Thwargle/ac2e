#include "AC2AssetReader.h"
#include "AC2Scenery.h"
#include "AC2MaterialAlpha.h"
#include "AC2NativeMaterial.h"
#include "AC2CellLighting.h"
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
    TestTrue(TEXT("High detail class one fallback passes its native gates"),AC2MaterialLayerEnabled(0x140000,0,1));
    TestFalse(TEXT("Low detail alternative is not drawn at high detail"),AC2MaterialLayerEnabled(0x110000,0,1));
    TestFalse(TEXT("Programmable hardware pass is not a fixed-function fallback"),AC2MaterialLayerEnabled(0x240000,0,1));
    TestTrue(TEXT("Ungated effect pass remains enabled"),AC2MaterialLayerEnabled(0,0,2));
    TestFalse(TEXT("Native forbidden fog bit is honored"),AC2MaterialLayerEnabled(0,0x80000,2));
    TestEqual(TEXT("Missing optional detail stops the native cascade"),AC2BoundColorOperation(8,2,false),1u);
    TestEqual(TEXT("Untextured diffuse multiplication continues"),AC2BoundColorOperation(5,0,false),5u);
    TestEqual(TEXT("Present signed detail retains its operation"),AC2BoundColorOperation(8,2,true),8u);
    const TArray<FAC2AlphaStage> OptionalDetail={{2,2,0},{8,2,1},{5,0,1}};
    const TArray<bool> Bound={true,false,false};
    TestEqual(TEXT("Colour cascade stops before an unbound detail stage"),AC2ActiveStageCount(OptionalDetail,Bound),1);
    const TArray<FAC2AlphaStage> OptionalAlpha={{3,2,0},{2,2,0},{2,0,1}};
    const TArray<float> OptionalSamples={.5f,0,0};
    TestEqual(TEXT("Unexecuted detail alpha cannot hide an opaque character"),
        EvaluateAC2Alpha(MakeArrayView(OptionalAlpha.GetData(),AC2ActiveStageCount(OptionalDetail,Bound)),OptionalSamples),1.f);
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
    const TArray<FAC2AlphaStage> Distinct={{2,2,0},{4,2,1}};
    const TArray<float> TexAlpha={.8f,.25f};
    TestEqual(TEXT("Second stage alpha comes from its own texture"),EvaluateAC2Alpha(Distinct,TexAlpha),.2f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CellLightingTest,"AC2.Data.NativeCellLighting",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CellLightingTest::RunTest(const FString& Parameters)
{
    TArray<uint8> Data;
    for(uint32 V:{1u,0x154u,5u,8u,0x800a0004u,0x00140003u}) AssetWord(Data,V);
    FAC2AssetReader R(Data); TMap<uint32,TArray<uint16>> Indices;
    TestTrue(TEXT("Native light vertex runs decode"),AC2LightIndices(R,0x03f40000,Indices));
    TestTrue(TEXT("Indices stay in their owning room"),Indices.Contains(0x03f40154));
    if(Indices.Contains(0x03f40154))TestTrue(TEXT("Run and literal values"),Indices[0x03f40154]==TArray<uint16>({4,10,11,12,20}));
    Data.Pop(); FAC2AssetReader Truncated(Data); Indices.Reset();
    TestFalse(TEXT("Truncated light runs rejected"),AC2LightIndices(Truncated,0x03f40000,Indices));
    TArray<FVector> Positions={FVector::ZeroVector,FVector::ZeroVector};
    TArray<FVector> Normals={FVector::UpVector,FVector::UpVector};
    FAC2CellLight Light;Light.Position=FVector(0,0,100);Light.Range=100;Light.Intensity=1;Light.Vertices.Add(0x154,{0});
    const TArray<FAC2CellLight> Lights={Light};
    const auto Colors=BakeAC2CellLights(0x154,FTransform::Identity,Positions,Normals,Lights,FLinearColor(.1,.1,.1),true);
    TestEqual(TEXT("Only listed vertex receives attenuated light"),Colors[0].R,uint8(48));
    TestEqual(TEXT("Unlisted vertex retains quantized ambient"),Colors[1].R,uint8(25));
    TestEqual(TEXT("Runtime placeholder alpha replaced"),Colors[0].A,uint8(255));
    const auto Dark=BakeAC2CellLights(0x155,FTransform::Identity,Positions,Normals,Lights,FLinearColor::Black,true);
    TestEqual(TEXT("No light leaking into unlisted room"),Dark[0],FColor(0,0,0,255));
    const auto Fallback=BakeAC2CellLights(0x155,FTransform::Identity,Positions,Normals,Lights,FLinearColor::Black,false);
    TestEqual(TEXT("Retail no-light-record fallback"),Fallback[0],FColor(102,102,102,255));
    return true;
}
#endif
