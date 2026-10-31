#include "AC2Water.h"
#include "AC2WorldGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaterContinuityTest,"AC2.Data.NativeWaterContinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2WaterContinuityTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2WaterSource Water;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open reported coastline DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)
        && Water.Load(Portal,Error)))return false;
    FAC2TerrainMesh Joined;
    const FVector Origin=AC2::BlockOrigin(0x84b0);
    uint32 Material=0;
    for(uint32 Block:{0x85b0u,0x86b0u,0x87b0u})
    {
        FAC2LandBlock Land;const uint32 Id=(Block<<16)|0xffff;
        if(!TestTrue(TEXT("Load adjacent lake tiles"),Cell.Read(Id,Bytes,Error)&&Land.Decode(Id,Bytes,Error)))return false;
        const auto W=Water.At(Defs,Land);
        TestTrue(TEXT("Native lake surface exists"),W.Present);
        if(Material)TestEqual(TEXT("Same authored material across the old near/far cutoff"),W.Above,Material);
        Material=W.Above;
        FAC2TerrainMesh Near,Far;
        AppendAC2WaterMesh(Block,W,FVector::ZeroVector,Near);
        AppendAC2WaterMesh(Block,W,AC2::BlockOrigin(Block)-Origin,Far);
        TestEqual(TEXT("Water needs only two triangles per tile"),Far.Indices.Num(),6);
        for(int32 I=0;I<Near.Vertices.Num();++I)
        {
            TestTrue(TEXT("Near and distant water use identical world positions"),
                (Near.Vertices[I]+AC2::BlockOrigin(Block)).Equals(Far.Vertices[I]+Origin,.001));
            TestTrue(TEXT("No texture scale/phase change at the terrain LOD boundary"),Near.UVs[I].Equals(Far.UVs[I],.0001));
        }
        const int32 Previous=Joined.Vertices.Num();
        AppendAC2WaterMesh(Block,W,AC2::BlockOrigin(Block)-Origin,Joined);
        if(Previous)
        {
            TestTrue(TEXT("Adjacent lake edges meet without a crack"),Joined.Vertices[Previous-3].Equals(Joined.Vertices[Previous],.001));
            TestTrue(TEXT("Texture coordinates meet at the shared edge"),Joined.UVs[Previous-3].Equals(Joined.UVs[Previous],.001));
        }
    }
    TestEqual(TEXT("The same batch spans nearby and distant water"),Joined.Vertices.Num(),12);
    TestTrue(TEXT("Batched triangles use their own tile vertices"),Joined.Indices[12]>=8 && Joined.Indices.Last()==11);
    AppendAC2WaterMesh(0x84b0,{},FVector::ZeroVector,Joined);
    TestEqual(TEXT("Dry DAT tiles do not acquire water"),Joined.Vertices.Num(),12);
    return true;
}

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
