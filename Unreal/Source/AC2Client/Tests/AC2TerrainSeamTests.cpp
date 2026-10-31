#include "AC2Terrain.h"
#include "AC2Assets.h"
#include "AC2WorldGrid.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TerrainSeamTest,"AC2.Data.TerrainSharedEdges",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TerrainSeamTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open reported terrain DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)))return false;
    const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,0x86b0,2);
    TMap<uint32,FAC2TerrainMesh> Meshes;
    for(uint32 Id:{0x85b0u,0x86b0u,0x85b1u,0x86b1u})
    {
        const auto* Land=Neighborhood.Find(Id);
        if(!TestNotNull(TEXT("Read neighboring seam block"),Land))return false;
        const auto Old=BuildAC2TerrainMesh(Defs,*Land);
        auto Mesh=BuildAC2TerrainMesh(Defs,*Land,&Neighborhood);
        TestTrue(TEXT("Authored terrain positions are unchanged"),Mesh.Vertices==Old.Vertices);
        TestTrue(TEXT("Collision topology and cave holes are unchanged"),Mesh.Indices==Old.Indices);
        Meshes.Add(Id,MoveTemp(Mesh));
    }
    for(uint32 Id:{0x85b0u,0x86b0u,0x85b1u})
        for(uint32 Delta:{0x100u,1u})if(const auto* Next=Meshes.Find(Id+Delta))
            for(int32 I=0;I<17;++I)
            {
                const auto& Here=Meshes[Id];
                const int32 A=Delta==0x100?16*17+I:I*17+16;
                const int32 B=Delta==0x100?I:I*17;
                TestTrue(TEXT("Shared edge positions coincide"),
                    (AC2::BlockOrigin(Id)+Here.Vertices[A]).Equals(AC2::BlockOrigin(Id+Delta)+Next->Vertices[B],.001));
                TestTrue(TEXT("Shared lighting and slope selection normals coincide"),Here.Normals[A].Equals(Next->Normals[B],1.e-8));
            }
    // Independent application of the recovered CacheFarValues unit-face fan
    // to these DAT heights, including the western and northwestern neighbours.
    TestTrue(TEXT("Reported seam uses retail's complete triangle fan"),Meshes[0x86b0].Normals[14].Equals(
        FVector(-.5142972835,.0229585832,.8573046177),1.e-7));
    TestTrue(TEXT("Four-block corner includes the diagonal neighbor"),Meshes[0x86b0].Normals[16].Equals(
        FVector(-.2361623792,.3546069364,.9046995365),1.e-7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TerrainSamplerTest,"AC2.Data.TerrainAtlasClamping",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TerrainSamplerTest::RunTest(const FString&)
{
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2AssetLibrary Assets;
    TArray<uint8> Bytes;FString Error;
    if(!TestTrue(TEXT("Open terrain assets"),Assets.Open(Dir,Error)
        && Portal.Open(Dir/TEXT("portal.dat"),1,Error) && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)))return false;
    const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,0x86b0,1);
    const auto* Land=Neighborhood.Find(0x86b0);
    if(!TestNotNull(TEXT("Load reported seam"),Land))return false;
    const auto Mesh=BuildAC2TerrainMesh(Defs,*Land,&Neighborhood);
    auto CheckSampler=[&](UMaterialInterface* Material) {
        UTexture* Texture=nullptr;
        if(!TestTrue(TEXT("Material binds its terrain atlas"),Material &&
            Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Albedo")),Texture)))return;
        const auto* Atlas=Cast<UTexture2D>(Texture);
        if(!TestNotNull(TEXT("Bound atlas is a 2D texture"),Atlas))return;
        TestEqual(TEXT("East/west edges never sample the opposite side of the block"),int32(Atlas->AddressX),int32(TA_Clamp));
        TestEqual(TEXT("North/south edges never sample the opposite side of the block"),int32(Atlas->AddressY),int32(TA_Clamp));
    };
    CheckSampler(Assets.TerrainMaterial(Defs,*Land,Mesh));
    if(!TestTrue(TEXT("Prepare streamed terrain"),Assets.PrepareBlock(Dir,Defs,*Land,Mesh,Error)))return false;
    CheckSampler(Assets.UploadTerrain());
    return true;
}
#endif
