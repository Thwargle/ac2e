#include "AC2DatArchive.h"
#include "AC2Terrain.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    void Put(TArray<uint8>& B, int32 At, uint32 Value)
    {
        for (int32 I = 0; I < 4; ++I) B[At + I] = uint8(Value >> (I * 8));
    }
    void PutFloat(TArray<uint8>& B, int32 At, float Value)
    {
        uint32 Bits; FMemory::Memcpy(&Bits, &Value, 4); Put(B, At, Bits);
    }
    TArray<uint8> Fixture()
    {
        TArray<uint8> B; B.SetNumZeroed(4096);
        Put(B, 300, 0x5442); Put(B, 304, 256); Put(B, 308, B.Num()); Put(B, 312, 2); Put(B, 332, 1024);
        TArray<uint8> Node; Node.SetNumZeroed(1228);
        Put(Node, 248, 1); Put(Node, 252, 0x1234FFFF); Put(Node, 256, 2304); Put(Node, 260, 300);
        for (int32 I = 0; I < 5; ++I)
        {
            const int32 Off = 1024 + I * 256;
            Put(B, Off, I == 4 ? 0 : Off + 256);
            FMemory::Memcpy(B.GetData() + Off + 4, Node.GetData() + I * 252, FMath::Min(252, 1228 - I * 252));
        }
        Put(B, 2304, 2560);
        for (int32 I = 0; I < 300; ++I) B[2308 + (I / 252) * 256 + I % 252] = uint8(I);
        return B;
    }
    struct FTempDat
    {
        FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/AC2"), FGuid::NewGuid().ToString() + TEXT(".dat"));
        FTempDat() { IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true); }
        ~FTempDat() { IFileManager::Get().Delete(*Path); }
        bool Write(const TArray<uint8>& Bytes) { return FFileHelper::SaveArrayToFile(Bytes, *Path); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ArchiveTest, "AC2.Data.ArchiveBoundsAndChains", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2ArchiveTest::RunTest(const FString& Parameters)
{
    FTempDat Temp;
    FString Error;
    TArray<uint8> Bytes;
    auto Source = Fixture();
    TestTrue(TEXT("write synthetic archive"), Temp.Write(Source));
    {
        FAC2DatArchive Archive;
        TestTrue(TEXT("open AC2 archive"), Archive.Open(Temp.Path, 2, Error));
        TestTrue(TEXT("read cross-sector record"), Archive.Read(0x1234FFFF, Bytes, Error));
        TestEqual(TEXT("payload length"), Bytes.Num(), 300);
        if (Bytes.Num() == 300) for (int32 I = 0; I < 300; ++I) if (Bytes[I] != uint8(I)) { AddError(TEXT("sector payload mismatch")); break; }
        FAC2DatEntry E;
        TestFalse(TEXT("missing record"), Archive.Find(0x1235FFFF, E, Error));
        TestTrue(TEXT("missing is not corruption"), Error.IsEmpty());
        TArray<FAC2DatEntry> Entries;
        TestTrue(TEXT("enumerate"), Archive.Enumerate(Entries, Error));
        TestEqual(TEXT("entry count"), Entries.Num(), 1);
    }
    auto Reject = [&](const TCHAR* Label, TArray<uint8> Mutated, bool OnRead)
    {
        if (!Temp.Write(Mutated)) { AddError(TEXT("fixture write failed")); return; }
        FAC2DatArchive Archive;
        const bool Opened = Archive.Open(Temp.Path, 2, Error);
        const bool Accepted = Opened && (!OnRead || Archive.Read(0x1234FFFF, Bytes, Error));
        TestFalse(Label, Accepted);
        TestFalse(TEXT("failure explains cause"), Error.IsEmpty());
        if (OnRead) TestTrue(TEXT("failed read does not return partial bytes"), Bytes.IsEmpty());
    };
    auto B = Source; B.SetNum(200); Reject(TEXT("short header"), B, false);
    B = Source; Put(B, 300, 0); Put(B, 320, 0x5442); Reject(TEXT("AC1 header rejected"), B, false);
    B = Source; Put(B, 312, 1); Reject(TEXT("wrong dataset rejected"), B, false);
    B = Source; Put(B, 304, 0); Reject(TEXT("zero sector size"), B, false);
    B = Source; Put(B, 332, 4096); Reject(TEXT("root outside archive"), B, false);
    B = Source; Put(B, 1024, 1024); Reject(TEXT("directory sector cycle"), B, false);
    B = Source; Put(B, 1276, 62); Reject(TEXT("oversized directory count"), B, false);
    B = Source; Put(B, 2304, 2304); Reject(TEXT("record cycle"), B, true);
    B = Source; Put(B, 2304, 0); Reject(TEXT("early chain end"), B, true);
    B = Source; Put(B, 2560, 2816); Reject(TEXT("chain exceeds record size"), B, true);
    B = Source; Put(B, 2304, 0x80000A00); Reject(TEXT("free-sector flag"), B, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TerrainTest, "AC2.Data.TerrainDecodeAndTopology", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2TerrainTest::RunTest(const FString& Parameters)
{
    FString Error;
    TArray<uint8> Definitions; Definitions.SetNumZeroed(1076);
    Put(Definitions, 0, 0x36000000); PutFloat(Definitions, 4, 20); Put(Definitions, 8, 8); Put(Definitions, 12, 2); Put(Definitions, 28, 7);
    for (int32 I = 0; I < 256; ++I) PutFloat(Definitions, 52 + I * 4, I * 1.5f);
    FAC2LandscapeDefs Defs;
    TestTrue(TEXT("landscape definitions"), Defs.Decode(Definitions, Error));
    TArray<uint8> Record; Record.SetNumZeroed(1468);
    Put(Record, 0, 0x1234FFFF); Put(Record, 12, 289); Put(Record, 305, 289);
    for (int32 I = 0; I < 289; ++I) Record[16 + I] = 10;
    FAC2LandBlock Land;
    TestTrue(TEXT("unaligned cell-info array"), Land.Decode(0x1234FFFF, Record, Error));
    auto Mesh = BuildAC2TerrainMesh(Defs, Land);
    TestEqual(TEXT("17x17 vertices"), Mesh.Vertices.Num(), 289);
    TestEqual(TEXT("16x16 quads"), Mesh.Indices.Num(), 1536);
    if (Mesh.Vertices.Num() == 289)
    {
        TestEqual(TEXT("height LUT converted to cm"), Mesh.Vertices[0].Z, 1500.0);
        TestEqual(TEXT("160m block width"), Mesh.Vertices.Last().X, 16000.0);
        TestTrue(TEXT("upward normals"), Mesh.Normals[144].Z > 0.99);
        const FVector A = Mesh.Vertices[Mesh.Indices[0]], B = Mesh.Vertices[Mesh.Indices[1]], C = Mesh.Vertices[Mesh.Indices[2]];
        TestTrue(TEXT("clockwise Unreal front faces"), FVector::CrossProduct(B - A, C - A).Z < 0);
    }
    Land.CellInfo[8 * 17 + 8] = 0x1E;
    Mesh = BuildAC2TerrainMesh(Defs, Land);
    TestEqual(TEXT("hole removes four adjacent quads"), Mesh.Indices.Num(), 1512);
    TestFalse(TEXT("embedded ID mismatch"), Land.Decode(0x1235FFFF, Record, Error));
    Put(Record, 305, 288);
    TestFalse(TEXT("bad cell-info count"), Land.Decode(0x1234FFFF, Record, Error));
    PutFloat(Definitions, 4, 24);
    TestFalse(TEXT("AC1 lattice rejected"), Defs.Decode(Definitions, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2IdTest, "AC2.Data.BlockArguments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2IdTest::RunTest(const FString& Parameters)
{
    uint32 Block = 0;
    TestTrue(TEXT("block ID"), AC2::ParseBlock(TEXT("0x9882"), Block)); TestEqual(TEXT("value"), Block, 0x9882u);
    TestTrue(TEXT("full terrain ID"), AC2::ParseBlock(TEXT("9882FFFF"), Block)); TestEqual(TEXT("full ID value"), Block, 0x9882u);
    TestFalse(TEXT("invalid characters"), AC2::ParseBlock(TEXT("9882oops"), Block));
    TestFalse(TEXT("overflow"), AC2::ParseBlock(TEXT("19882FFFF"), Block));
    TestFalse(TEXT("empty"), AC2::ParseBlock(TEXT(""), Block));
    return true;
}
#endif
