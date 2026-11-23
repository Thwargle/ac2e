#include "AC2RegionFog.h"
#include "AC2Sky.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
void FogWord(TArray<uint8>& B,uint32 Value){for(int32 I=0;I<4;++I)B.Add(uint8(Value>>(I*8)));}
void FogFloat(TArray<uint8>& B,float Value){uint32 Bits;FMemory::Memcpy(&Bits,&Value,4);FogWord(B,Bits);}
void FogType(TArray<uint8>& B,uint32 Id,uint32 Mode,float Near,float Far,float Sky,uint32 Color)
{
    FogWord(B,Id);FogFloat(B,Near);FogFloat(B,Far);FogFloat(B,Sky);FogFloat(B,.4f);FogWord(B,Mode);FogWord(B,Color);
    FogFloat(B,1);FogWord(B,0xffffffff);FogFloat(B,1);FogWord(B,0xffffffff);
    // Lighting stays unchanged, dayFog uses current fog color, distance uses max.
    FogWord(B,(0x92492u&~((7u<<15)|(7u<<18)))|(3u<<15)|(4u<<18));
}
TArray<uint8> FogDescription()
{
    TArray<uint8> B;
    for(uint32 V:{1u,0x17000000u,0x19000002u,0u,0u,0u,1u})FogWord(B,V);
    FogType(B,0,0,0,10000,0,0xffffffff);
    FogWord(B,4);
    FogType(B,1,1,-100,100,0,0xff000000);
    FogType(B,2,1,0,200,.2f,0xff000000);
    FogType(B,3,1,100,300,.6f,0xffffffff);
    FogType(B,4,1,200,400,1,0xffffffff);
    return B;
}
TArray<uint8> FogMap(bool Active)
{
    TArray<uint8> B;FogWord(B,0x19000002);FogWord(B,2);FogWord(B,2);
    // Column-major: southwest, northwest, southeast, northeast.
    for(uint8 V:{uint8(1),uint8(2),uint8(3),uint8(4)})B.Add(Active?V:0);
    return B;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RegionFogInterpolationTest,"AC2.Render.RegionFogInterpolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RegionFogInterpolationTest::RunTest(const FString&)
{
    FAC2RegionFog Fog;uint32 Map=0;
    if(!TestTrue(TEXT("Decode four native fog corners"),Fog.Decode(0x17000000,FogDescription(),Map) && Fog.DecodeMap(Map,FogMap(true))))return false;
    FAC2SkyKey Day;Day.FogMin=77;Day.FogMax=999;Day.FogSky=.1f;Day.FogColor=FLinearColor(.5f,.5f,.5f,1);
    Day.Direct=.6f;Day.Ambient=.3f;Day.Heading=123;
    const auto SW=Fog.Apply(FVector::ZeroVector,Day);
    TestEqual(TEXT("Authored negative near distances survive"),SW.FogMin,-100.f);
    const auto Center=Fog.Apply(FVector(8000,8000,0),Day);
    TestEqual(TEXT("Bilinear near distance"),Center.FogMin,50.f);
    TestEqual(TEXT("Bilinear far distance"),Center.FogMax,250.f);
    TestTrue(TEXT("Max color combine runs before interpolation"),Center.FogColor.Equals(FLinearColor(.75f,.75f,.75f,1),.00001f));
    const auto Quarter=Fog.Apply(FVector(4000,12000,0),Day);
    TestEqual(TEXT("Map axes follow native southwest/northwest order"),Quarter.FogMin,25.f);
    TestTrue(TEXT("Sky amount is interpolated independently"),FMath::IsNearlyEqual(Quarter.FogSky,.3375f,.00001f));
    TestEqual(TEXT("Region keeps unrelated sky heading"),Quarter.Heading,Day.Heading);
    TestEqual(TEXT("Region respects brightness combination selectors"),Quarter.Direct,Day.Direct);
    TestEqual(TEXT("Native out-of-map samples use first map byte"),Fog.Apply(FVector(32000,32000,0),Day).FogMin,-100.f);
    TestEqual(TEXT("Non-world viewer bypasses outdoor fog"),Fog.Apply(FVector(-1,0,0),Day).FogMin,Day.FogMin);
    TestTrue(TEXT("Disable all corners"),Fog.DecodeMap(Map,FogMap(false)));
    const auto Clear=Fog.Apply(FVector(8000,8000,0),Day);
    TestEqual(TEXT("All no-fog corners preserve day near distance"),Clear.FogMin,Day.FogMin);
    TestEqual(TEXT("All no-fog corners preserve day far distance"),Clear.FogMax,Day.FogMax);
    TestTrue(TEXT("All no-fog corners preserve day fog color"),Clear.FogColor.Equals(Day.FogColor));
    auto Truncated=FogDescription();Truncated.Pop();
    TestFalse(TEXT("Truncated fog definitions fail safely"),Fog.Decode(0x17000000,Truncated,Map));
    TestEqual(TEXT("Failed decode does not retain previous map"),Fog.Apply(FVector::ZeroVector,Day).FogMin,Day.FogMin);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RegionFogDataTest,"AC2.Data.NativeRegionFog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RegionFogDataTest::RunTest(const FString&)
{
    for(const TCHAR* Profile:{TEXT("../Asheron's Call 2 - Legions"),TEXT("../Saved/IcefireClient")}){
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/Profile);
    FAC2DatArchive Portal;FAC2RegionFog Fog;FString Error;TArray<uint8> Map;
    if(!TestTrue(TEXT("Load authored regional fog and map"),Portal.Open(Dir/TEXT("portal.dat"),1,Error) &&
        Fog.Load(Portal,0x17000000,Error) && Portal.Read(0x19000002,Map,Error)))return false;
    if(!TestEqual(TEXT("Native fog map is 255 by 255 bytes plus header"),Map.Num(),65037))return false;
    int32 Active=0,Type4=INDEX_NONE;
    for(int32 I=12;I<Map.Num();++I){Active+=Map[I]!=0;if(Map[I]==4 && Type4==INDEX_NONE)Type4=I-12;}
    const int32 ExpectedCells=FString(Profile).Contains(TEXT("IcefireClient"))?3599:3558;
    TestEqual(FString::Printf(TEXT("Authored fog cell census for %s"),Profile),Active,ExpectedCells);
    int32 Type12=0;for(int32 I=12;I<Map.Num();++I)Type12+=Map[I]==12;
    TestEqual(TEXT("Icefire adds exactly 41 type-twelve fog cells"),Type12,ExpectedCells==3599?724:683);
    if(!TestTrue(TEXT("Authored fog type four exists"),Type4!=INDEX_NONE))return false;
    TestEqual(TEXT("Pinned native type-four location"),Type4,0x38*255+0x98);
    FAC2SkyKey Day;Day.FogMax=777;Day.Direct=.7f;Day.Ambient=.2f;
    const auto Authored=Fog.Apply(FVector((Type4/255)*16000.,(Type4%255)*16000.,0),Day);
    TestEqual(TEXT("Native type-four fog begins before the viewer"),Authored.FogMin,-100.f);
    TestEqual(TEXT("Native type-four far distance"),Authored.FogMax,300.f);
    TestTrue(TEXT("Native type-four sky contribution"),FMath::IsNearlyEqual(Authored.FogSky,.7f));
    TestEqual(TEXT("Native type-four linear mode"),Authored.FogType,1u);
    // Independent DAT oracle: 3798 corners are 0,0,4,4 in both profiles.
    const auto Boundary=Fog.Apply(FVector(0x37*16000.+8000,0x98*16000.+8000,0),Day);
    TestEqual(TEXT("Native fog boundary preserves negative near blend"),Boundary.FogMin,-50.f);
    TestEqual(TEXT("Native fog boundary blends with authored default far"),Boundary.FogMax,5150.f);
    TestTrue(TEXT("Native fog boundary sky blend"),FMath::IsNearlyEqual(Boundary.FogSky,.35f));
    TestEqual(TEXT("Reported shoreline retains its day fog"),Fog.Apply(FVector(0x8e*16000.,0xb8*16000.,0),Day).FogMax,Day.FogMax);
    TestEqual(TEXT("Last world block handles map edge"),Fog.Apply(FVector(254*16000.+8000,254*16000.+8000,0),Day).FogMax,Day.FogMax);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaterFogBoundaryTest,"AC2.Render.WaterFogKeyBoundaries",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2WaterFogBoundaryTest::RunTest(const FString&)
{
    FAC2SkyKey Day;FAC2BlockWater Water;
    FAC2WaterLightKey A;A.Depth=1;A.FogType=0;A.Combine=0x92492;
    A.FogMin=5;A.FogMax=50;A.FogColor=FLinearColor::White;A.DirectColor=A.AmbientColor=FLinearColor::White;
    FAC2WaterLightKey B=A;B.Depth=3;B.FogMin=10;B.FogMax=20;
    Water.Lighting={A,B};
    TestEqual(TEXT("Before first key retains authored no-fog mode"),AC2WaterLighting(Water,.5f,Day).FogType,0u);
    TestEqual(TEXT("Exact first key selects next interval and linear mode"),AC2WaterLighting(Water,1,Day).FogType,1u);
    TestEqual(TEXT("Between depth keys is linear"),AC2WaterLighting(Water,2,Day).FogType,1u);
    TestEqual(TEXT("Between keys interpolates metres"),AC2WaterLighting(Water,2,Day).FogMax,35.f);
    TestEqual(TEXT("Exact last key retains authored endpoint mode"),AC2WaterLighting(Water,3,Day).FogType,0u);
    TestEqual(TEXT("Beyond last key retains authored endpoint mode"),AC2WaterLighting(Water,4,Day).FogType,0u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SkyUnderwaterLightingTest,"AC2.Render.NativeUnderwaterKeepsDaylight",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SkyUnderwaterLightingTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Create environment test world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    for(const TCHAR* Profile:{TEXT("../Asheron's Call 2 - Legions"),TEXT("../Saved/IcefireClient")}){
        const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/Profile);
        FAC2SkyRenderer Sky;FString Error;FAC2DatArchive Portal,Cell;TArray<uint8> Bytes;
        FAC2LandscapeDefs Defs;FAC2LandBlock Land;FAC2WaterSource Source;
        const bool Open=Sky.Open(World,Dir,Error) && Portal.Open(Dir/TEXT("portal.dat"),1,Error) &&
            Cell.Open(Dir/TEXT("cell_1.dat"),2,Error) && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error) &&
            Cell.Read(0x8eb8ffff,Bytes,Error) && Land.Decode(0x8eb8ffff,Bytes,Error) && Source.Load(Portal,Error);
        if(!TestTrue(FString::Printf(TEXT("Open profile sky and water: %s (%s)"),Profile,*Error),Open))continue;
        auto Water=Source.At(Defs,Land);
        if(!TestTrue(TEXT("Native reported shoreline has water lighting"),Water.Present && !Water.Lighting.IsEmpty()))continue;
        Sky.SetHour(12);const auto Day=Sky.Lighting();
        Sky.SetWaterView(&Water,1);const auto Wet=Sky.Lighting();const auto Expected=AC2WaterLighting(Water,1,Day);
        TestEqual(TEXT("Native underwater direct brightness stays unchanged"),Wet.Direct,Day.Direct);
        TestEqual(TEXT("Native underwater ambient brightness stays unchanged"),Wet.Ambient,Day.Ambient);
        TestTrue(TEXT("Native underwater lighting colors stay unchanged"),Wet.DirectColor.Equals(Day.DirectColor) && Wet.AmbientColor.Equals(Day.AmbientColor));
        TestEqual(TEXT("Native underwater fog type reaches final environment"),Wet.FogType,Expected.FogType);
        TestEqual(TEXT("Native underwater far distance reaches final environment"),Wet.FogMax,Expected.FogMax);
        // Ensure the copy boundary stays correct even if a later DAT water key
        // requests different lighting; UpdateLighting copies fog fields only.
        for(auto& K:Water.Lighting){K.Direct=0;K.Ambient=0;K.Combine=(K.Combine&~((7u<<0)|(7u<<6)))|3u|(3u<<6);}
        Sky.SetWaterView(&Water,1);const auto Changed=Sky.Lighting();
        TestEqual(TEXT("Water keys cannot replace the sunlight"),Changed.Direct,Day.Direct);
        TestEqual(TEXT("Water keys cannot replace ambient light"),Changed.Ambient,Day.Ambient);
        Sky.SetWaterView(nullptr,0);
        TestTrue(TEXT("Leaving water restores the day fog"),Sky.Lighting().FogColor.Equals(Day.FogColor));
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
