#include "AC2Water.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShorelineTest,"AC2.Data.RepairedShoreline74A9",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShorelineTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2WaterSource Water;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open shoreline DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)
        && Water.Load(Portal,Error)))return false;
    auto Read=[&](uint32 Block,FAC2LandBlock& Land) {
        const uint32 Id=(Block<<16)|0xffff;
        return TestTrue(TEXT("Read shoreline terrain"),Cell.Read(Id,Bytes,Error)&&Land.Decode(Id,Bytes,Error));
    };
    FAC2LandBlock Bank,East,North,West,South;
    if(!Read(0x74a9,Bank)||!Read(0x75a9,East)||!Read(0x74aa,North)
        ||!Read(0x73a9,West)||!Read(0x74a8,South))return false;
    const auto Surface=Water.At(Defs,Bank);
    TestTrue(TEXT("Repaired bank has water"),Surface.Present);
    TestTrue(TEXT("Uses adjacent lake's native height"),FMath::IsNearlyEqual(Surface.Height,96.7f,.001f));
    TestEqual(TEXT("East seam height"),Surface.Height,Water.At(Defs,East).Height);
    TestEqual(TEXT("North seam height"),Surface.Height,Water.At(Defs,North).Height);
    TestEqual(TEXT("Uses native water material"),Surface.Above,Water.At(Defs,East).Above);
    TestFalse(TEXT("Unconnected western depression remains dry"),Water.At(Defs,West).Present);
    TestFalse(TEXT("Southern hillside remains dry"),Water.At(Defs,South).Present);
    for(int32 I=0;I<17;++I)
    {
        TestTrue(TEXT("West boundary is above lake: no new rectangular water edge"),Defs.Heights[Bank.Heights[I]]>=Surface.Height);
        TestTrue(TEXT("South boundary is above lake: no new rectangular water edge"),Defs.Heights[Bank.Heights[I*17]]>=Surface.Height);
    }
    double Ground=0;FVector Normal;
    TestTrue(TEXT("Sample reported shore location"),SampleAC2Terrain(Defs,Bank,55.5,155.4,Ground,Normal));
    TestTrue(TEXT("Reported missing-water point is submerged"),Ground<Surface.Height);
    return true;
}
#endif
