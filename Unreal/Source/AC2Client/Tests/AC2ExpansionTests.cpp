#include "AC2Animation.h"
#include "AC2Water.h"
#include "AC2Particle.h"
#include "AC2UILayout.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void W(TArray<uint8> &B, uint32 V)
{
    for (int I = 0; I < 4; ++I)
        B.Add(uint8(V >> (I * 8)));
}
void F(TArray<uint8> &B, float V)
{
    uint32 V32;
    FMemory::Memcpy(&V32, &V, 4);
    W(B, V32);
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaterBoundsTest, "AC2.Data.NativeWaterBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2WaterBoundsTest::RunTest(const FString &)
{
    TArray<uint8> B;
    for (uint32 V : {0u, 0x16000000u, 0x19000000u, 0x19000001u, 1u, 0u, 1u, 0u})
        W(B, V);
    FAC2WaterSource S;
    uint32 H = 0, T = 0;
    TestTrue(TEXT("dry default record"), S.Decode(B, H, T));
    TestEqual(TEXT("height map"), H, 0x19000000u);
    TestEqual(TEXT("type map"), T, 0x19000001u);
    B.Pop();
    TestFalse(TEXT("truncation rejected"), S.Decode(B, H, T));
    B.Reset();
    for (uint32 V : {0u, 0x16000000u, 0u, 0u, 0u, 0xffffffffu})
        W(B, V);
    TestFalse(TEXT("unbounded type allocation rejected"), S.Decode(B, H, T));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AnimationSampleTest, "AC2.Data.NativeAnimationAndSkeleton",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2AnimationSampleTest::RunTest(const FString &)
{
    TArray<uint8> B;
    W(B, 0x05000001);
    W(B, 0x40000001);
    F(B, 2);
    W(B, 1);
    W(B, 0);
    W(B, 2);
    for (int32 I = 0; I < 2; ++I)
    {
        F(B, I * 2);
        F(B, 0);
        F(B, 0);
        F(B, I * 2);
    }
    W(B, 0);
    W(B, 0);
    FAC2Animation A;
    TestTrue(TEXT("constant/linear animation channels decode"), A.Decode(0x05000001, B));
    TArray<FTransform> Rest{FTransform::Identity};
    TestTrue(TEXT("metres become centimetres with interpolation"),
             A.Sample(1, Rest)[0].GetTranslation().Equals(FVector(100, 0, 0), .001));
    TestTrue(TEXT("period wraps"), A.Sample(3, Rest)[0].GetTranslation().Equals(FVector(100, 0, 0), .001));
    TestTrue(TEXT("missing rotation preserves rest"),
             A.Sample(1, Rest)[0].GetRotation().Equals(FQuat::Identity));
    TestTrue(TEXT("repeat decode replaces tracks"), A.Decode(0x05000001, B));
    TestEqual(TEXT("no accumulated tracks"), A.Tracks.Num(), 1);
    B.Pop();
    TestFalse(TEXT("truncated blend keys rejected"), A.Decode(0x05000001, B));
    FAC2Setup S;
    S.Parents = {MAX_uint32, 0};
    TArray<FTransform> P{FTransform(FVector(100, 0, 0)), FTransform(FVector(0, 25, 0))};
    auto Pose = S.World(P);
    TestTrue(TEXT("child inherits parent transform"), Pose[1].GetOrigin().Equals(FVector(100, 25, 0), .001));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ParticleBoundsTest, "AC2.Data.NativeParticleBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2ParticleBoundsTest::RunTest(const FString &)
{
    FAC2ParticlePreset P;
    P.ScaleType=1;
    TestEqual(TEXT("Native type one inherits entity scale, not visual particle scale"),P.RenderScale(2,.25f),2.f);
    P.ScaleType=2;
    TestEqual(TEXT("Native type two combines entity and visual particle scale"),P.RenderScale(2,.25f),.5f);
    TestEqual(TEXT("Type two retains entity scale when the visual uses its default"),P.RenderScale(2,1),2.f);
    TArray<uint8> B;
    W(B, 0x39000003);
    W(B, 100);
    for (int32 I = 0; I < 8; ++I)
        W(B, 0);
    W(B, 0xffffffff);
    TestFalse(TEXT("oversized emitter count rejected"), P.Decode(0x39000003, B));
    B.Reset();
    W(B, 0x39000003);
    TestFalse(TEXT("short header rejected"), P.Decode(0x39000003, B));
    TestFalse(TEXT("identity mismatch rejected"), P.Decode(0x39000004, B));
    B.Reset();
    W(B, 0x39000003);
    W(B, 100);
    for (int I = 0; I < 8; ++I)
        W(B, 0);
    W(B, 1);
    auto Wave = [&](float Value) {
        W(B, 0);
        F(B, Value);
        for (int I = 0; I < 9; ++I)
            F(B, 0);
    };
    W(B, 100);
    for (int I = 0; I < 3; ++I)
        Wave(0);
    W(B, 0);
    W(B, 4);
    Wave(1);
    Wave(1);
    W(B, 1);
    W(B, 1);
    W(B, 1);
    F(B, .1f);
    Wave(1);
    Wave(2);
    Wave(0);
    for (int I = 0; I < 14; ++I)
        Wave(0);
    W(B, 100);
    W(B, 1);
    for (int I = 0; I < 5; ++I)
        F(B, 0);
    Wave(0);
    for (int I = 0; I < 3; ++I)
        W(B, 0);
    W(B, 2);
    for (int K = 0; K < 2; ++K)
    {
        F(B, K);
        W(B, K ? 3 : 24);
        F(B, K);
        F(B, K);
        F(B, 1);
        F(B, 0);
        F(B, 0);
        F(B, K);
        for (int I = 0; I < 3; ++I)
            W(B, 0);
        for (int I = 0; I < 3; ++I)
            Wave(0);
        W(B, 0);
    }
    W(B, 0x310002e5);
    TestTrue(TEXT("bounded native emitter and lifetime keys"), P.Decode(0x39000003, B));
    float X, Y;
    FLinearColor C;
    P.Sample(.5f, X, Y, C);
    TestTrue(TEXT("fade-in interpolates out of zero alpha"), FMath::IsNearlyEqual(C.A, .5f));
    TestTrue(TEXT("scale key interpolation"), FMath::IsNearlyEqual(X, 25.f));
    TestEqual(TEXT("native material preserved"), P.Material, 0x310002e5u);
    B.Pop();
    TestFalse(TEXT("material tail required"), P.Decode(0x39000003, B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaveAndKeysTest, "AC2.Data.NativeWaveAndIndependentParticleChannels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2WaveAndKeysTest::RunTest(const FString &)
{
    FAC2Wave W;
    W.Base = 1;
    W.BaseSpeed = .5f;
    TestEqual(TEXT("linear base velocity"), W.Value(4), 3.f);
    W = {};
    W.Kind = 4;
    W.Base = 1;
    W.Amplitude = 2;
    W.Phase = .5f;
    TestTrue(TEXT("native phase is half turns"), FMath::IsNearlyEqual(W.Value(0), 3.f));
    W.Kind = 6;
    W.Phase = -.5f;
    TestTrue(TEXT("bounce reflects negative lobe"), FMath::IsNearlyEqual(W.Value(0), 3.f));
    W = {};
    W.Kind = 9;
    W.Amplitude = 4;
    W.Frequency = 2;
    W.Phase = 1.f / UE_PI;
    TestTrue(TEXT("frame-loop steps and wraps"),
             FMath::IsNearlyEqual(W.Value(1.6f), 3.f) && FMath::IsNearlyZero(W.Value(2.1f)));
    FAC2ParticleEmitter E;
    FAC2ParticleKey A, B, C;
    A.Flags = 8 | 16 | 32 | 128 | 256;
    A.X = A.Y = 2;
    A.Mass = 3;
    A.Controller = 2;
    A.Forces = 1;
    B.Time = .5f;
    B.Flags = 16;
    B.Color = FLinearColor::Red;
    B.X = 99;
    C.Time = 1;
    C.Flags = 1;
    C.X = C.Y = 4;
    E.Keys = {A, B, C};
    float X, Y, Mass;
    FLinearColor Color;
    E.Sample(.75f, 1, X, Y, Color, Mass);
    TestTrue(TEXT("unrelated colour key does not reset scale interpolation"), FMath::IsNearlyEqual(X, 175.f));
    TestEqual(TEXT("mass channel survives colour-only key"), Mass, 3.f);
    TestTrue(TEXT("colour set is held"), Color.Equals(FLinearColor::Red));
    uint32 Forces = 0;
    TestEqual(TEXT("controller survives unrelated keys"), E.ControllerAt(.75f, Forces)->Controller, 2u);
    TestEqual(TEXT("native force flags preserved"), Forces, 1u);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2UILayoutBoundsTest, "AC2.Data.NativeUILayoutBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2UILayoutBoundsTest::RunTest(const FString &)
{
    TArray<uint8> B;
    auto S = [&](uint16 V) {
        B.Add(V & 255);
        B.Add(V >> 8);
    };
    auto Align = [&]() {
        while (B.Num() % 4)
            B.Add(0);
    };
    W(B, 0x22000001);
    W(B, 0);
    W(B, 0);
    S(1024);
    S(768);
    S(1);
    Align();
    W(B, 123);
    W(B, 1);
    W(B, 7);
    S(10);
    S(20);
    S(42);
    S(52);
    S(0);
    W(B, 0);
    S(1);
    Align();
    W(B, 7);
    B.Add(0);
    S(10);
    S(20);
    S(42);
    S(52);
    S(0);
    Align();
    W(B, 0);
    W(B, 1);
    W(B, 3);
    W(B, 3);
    W(B, 0x41000001);
    W(B, 0);
    W(B, 2);
    W(B, 3);
    W(B, 0);
    W(B, 0);
    Align();
    FAC2UILayout L;
    TestTrue(TEXT("native image media layout"), L.Decode(0x22000001, B));
    TestEqual(TEXT("preserves direct Surface identity"), L.Art[0].Image, 0x41000001u);
    TestEqual(TEXT("authored element rectangle"), L.Art[0].Rect.Width(), 32);
    TestTrue(TEXT("default state marked"), L.Art[0].Default);
    B.Pop();
    TestFalse(TEXT("truncated state rejected"), L.Decode(0x22000001, B));
    B.Reset();
    W(B, 0x22000001);
    W(B, 0xffffffff);
    TestFalse(TEXT("unbounded sub-layout allocation rejected"), L.Decode(0x22000001, B));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LocomotionGraphTest, "AC2.Data.NativeLocomotionGraphs",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2LocomotionGraphTest::RunTest(const FString &)
{
    TMap<uint32, TArray<uint8>> Records;
    for (uint32 I = 1; I <= 6; ++I)
    {
        const float Period = I == 3 ? 4.f : 2.f;
        auto &B = Records.Add(0x05000000 + I);
        W(B, 0x05000000 + I);
        W(B, 0x40000001);
        F(B, Period);
        W(B, 1);
        W(B, 0);
        W(B, 2);
        for (int32 K = 0; K < 2; ++K)
        {
            F(B, float(I) + K * 2);
            F(B, 0);
            F(B, 0);
            F(B, K * Period);
        }
        W(B, 0);
        W(B, 0);
    }
    auto &Cube = Records.Add(0x05000100);
    W(Cube, 0x05000100);
    W(Cube, 0x40000002);
    W(Cube, 2);
    W(Cube, 0x40000001);
    W(Cube, 3);
    F(Cube, -2);
    F(Cube, 0);
    F(Cube, 2);
    W(Cube, 0x40000002);
    W(Cube, 2);
    F(Cube, -1);
    F(Cube, 1);
    W(Cube, 6);
    for (uint32 I = 1; I <= 6; ++I)
        W(Cube, 0x05000000 + I);
    auto &Switch = Records.Add(0x05000101);
    for (uint32 V : {0x05000101u, 0x40000008u, 0x05000100u, 0u, 1u, 7u, 0x05000001u})
        W(Switch, V);
    auto &Human = Records.Add(0x05000102);
    for (uint32 V : {0x05000102u, 0x40000003u, 0x05000101u})
        W(Human, V);
    auto &Map = Records.Add(0x03000001);
    for (uint32 V : {0x03000001u, 1u, 0x80000001u, 0x05000102u})
        W(Map, V);
    FAC2Animator::FRead Read = [&](uint32 Id, TArray<uint8> &B) {
        if (auto *Found = Records.Find(Id))
        {
            B = *Found;
            return true;
        }
        return false;
    };
    auto Graph = FAC2Animator::Load(Read, 0x03000001, 0x80000001);
    if (!TestTrue(TEXT("map, human driver, switch and cube decode"), Graph.IsValid()))
        return false;
    FAC2AnimatorInput Input;
    Input.Forward = 2;
    Input.Side = 1;
    TestEqual(TEXT("first axis varies fastest"), Graph->ActiveClips(Input)[0], 0x05000006u);
    Input.Forward = 100;
    TestEqual(TEXT("out-of-range speed clamps to last tick"), Graph->ActiveClips(Input)[0], 0x05000006u);
    Input.Forward = -2;
    Input.Side = -1;
    TestEqual(TEXT("reverse movement selects reverse corner"), Graph->ActiveClips(Input)[0], 0x05000001u);
    Input.Forward = 1;
    TArray<FTransform> Rest{FTransform::Identity};
    TestTrue(TEXT("interpolated gait and normalized phase"),
             Graph->Sample(.25f, Input, Rest)[0].GetTranslation().Equals(FVector(300, 0, 0), .001));
    TestTrue(TEXT("shared normalized phase wraps"),
             Graph->Sample(1.25f, Input, Rest)[0].GetTranslation().Equals(FVector(300, 0, 0), .001));
    TestEqual(TEXT("weighted native cycle period"), Graph->Period(Input), 3.f);
    // A missing lattice corner contributes no clip, not a half-speed period.
    const int32 CornerOffset=Cube.Num()-5*4; // clip 2 at half weight, clip 3 remains.
    const uint8 SavedCorner=Cube[CornerOffset];Cube[CornerOffset]=0;
    Cube[CornerOffset+3]=0;
    auto Sparse=FAC2Animator::Load(Read,0x05000100,0);
    if(TestTrue(TEXT("sparse native locomotion graph"),Sparse.IsValid()))
        TestEqual(TEXT("missing corner does not double playback speed"),Sparse->Period(Input),4.f);
    Cube[CornerOffset]=SavedCorner;Cube[CornerOffset+3]=5;
    Input.Mode = 7;
    TestEqual(TEXT("switch selects matching override"), Graph->ActiveClips(Input)[0], 0x05000001u);
    TestFalse(TEXT("unknown map key has no invented clip"),
              FAC2Animator::Load(Read, 0x03000001, 123).IsValid());
    Records[0x05000102].Reset();
    for (uint32 V : {0x05000102u, 0x40000003u, 0x05000102u})
        W(Records[0x05000102], V);
    TestFalse(TEXT("recursive animator rejected"), FAC2Animator::Load(Read, 0x05000102, 0).IsValid());
    Records[0x05000100].Pop();
    TestFalse(TEXT("truncated lattice rejected"), FAC2Animator::Load(Read, 0x05000100, 0).IsValid());
    return true;
}
#endif
