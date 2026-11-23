#include "AC2TextureMips.h"
#include "AC2AssetReader.h"
#include "AC2Assets.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CutoutMipTest,"AC2.Data.CutoutMipCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CutoutMipTest::RunTest(const FString&)
{
    const TArray<FColor> Pixels={FColor(0,180,0,255),FColor(255,0,255,0),FColor(255,0,255,0),FColor(255,0,255,0)};
    const auto Mask=AC2TextureMips::Downsample(Pixels,2,2,true);
    TestEqual(TEXT("transparent padding never tints a leaf"),Mask[0],FColor(0,180,0,64));
    const auto Ordinary=AC2TextureMips::Downsample(Pixels,2,2,false);
    TestTrue(TEXT("ordinary/translucent samples keep existing RGB filtering"),Ordinary[0].R>0 && Ordinary[0].B>0);
    TestEqual(TEXT("filtering averages alpha independently"),Ordinary[0].A,Mask[0].A);
    TArray<FColor> Thin={FColor(10,130,10,80),FColor(10,130,10,10),FColor::Transparent,FColor::Transparent};
    AC2TextureMips::PreserveCoverage(Thin,.25f);
    TestEqual(TEXT("thin blade remains visible in lower mip"),AC2TextureMips::Coverage(Thin),.25f);
    TestEqual(TEXT("clear padding stays transparent"),Thin[2].A,uint8(0));
    TestEqual(TEXT("coverage adjustment preserves DAT RGB"),Thin[0].G,uint8(130));
    TestEqual(TEXT("one pixel mip remains valid"),AC2TextureMips::Downsample({FColor::White},1,1,true)[0],FColor::White);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AuthoredTextureMipsTest, "AC2.Data.AuthoredTextureMips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2AuthoredTextureMipsTest::RunTest(const FString&)
{
    const FString Directory = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2AssetLibrary Library;
    FAC2DatArchive Portal, HighRes;
    FString Error;
    if (!TestTrue(TEXT("open texture library"), Library.Open(Directory, Error)) ||
        !TestTrue(TEXT("open source archive"), Portal.Open(Directory / TEXT("portal.dat"), 1, Error)))
        return false;
    const bool HasHighRes = HighRes.Open(Directory / TEXT("highres.dat"), 1, Error);
    // Both profiles author this chain, with different surfaces, dimensions and pixel formats.
    constexpr uint32 TextureId = 0x40000000;
    TArray<uint8> Chain;
    if (!TestTrue(TEXT("read texture chain"), Portal.Read(TextureId, Chain, Error)))
        return false;
    FAC2AssetReader Reader(MoveTemp(Chain));
    TestEqual(TEXT("texture DID"), Reader.Word(), TextureId);
    TestEqual(TEXT("2D texture"), Reader.Word(), 2u);
    const int32 Count = Reader.Count(4, 32);
    auto* Texture = Library.Texture(TextureId);
    if (!TestNotNull(TEXT("runtime texture"), Texture) || !TestTrue(TEXT("multi-level chain"), Count > 1))
        return false;
    auto& Mips = Texture->GetPlatformData()->Mips;
    int32 Matched = 0;
    for (int32 I = 0; I < Count; ++I)
    {
        const uint32 Surface = Reader.Word();
        TArray<uint8> Record;
        if (!(HasHighRes && HighRes.Read(Surface, Record, Error)) && !Portal.Read(Surface, Record, Error))
            continue;
        int32 Width, Height;
        TArray<FColor> Expected;
        if (!TestTrue(TEXT("decode DAT level"), DecodeAC2Surface(Record, Surface, Width, Height, Expected)))
            return false;
        FTexture2DMipMap* Mip = nullptr;
        for (auto& Level : Mips)
            if (Level.SizeX == Width && Level.SizeY == Height)
            {
                Mip = &Level;
                break;
            }
        if (!TestNotNull(TEXT("matching runtime mip dimensions"), Mip))
            return false;
        const int64 Bytes = int64(Expected.Num()) * sizeof(FColor);
        if (!TestEqual(TEXT("runtime mip byte count"), Mip->BulkData.GetBulkDataSize(), Bytes))
            return false;
        const auto* Actual = Mip->BulkData.LockReadOnly();
        TestTrue(FString::Printf(TEXT("source %08X (%dx%d) retains RGBA"), Surface, Width, Height),
                 Actual && FMemory::Memcmp(Actual, Expected.GetData(), Bytes) == 0);
        Mip->BulkData.Unlock();
        ++Matched;
    }
    TestTrue(TEXT("verified more than the top level"), Matched > 1);
    TestEqual(TEXT("missing tail completed"), Mips.Last().SizeX, 1);
    TestEqual(TEXT("missing tail height"), Mips.Last().SizeY, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NativeMaterialDefaultsTest, "AC2.Data.NativeMaterialPropertyDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2NativeMaterialDefaultsTest::RunTest(const FString&)
{
    const FString Directory = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2AssetLibrary Library;
    FString Error;
    if (!TestTrue(TEXT("open material library"), Library.Open(Directory, Error)))
        return false;
    // Both DAT profiles store a white layer followed by a diffuse-color default of half intensity.
    auto* Material = Cast<UMaterialInstanceDynamic>(Library.Material(0x2b000020));
    if (!TestNotNull(TEXT("template material instance"), Material))
        return false;
    TestEqual(TEXT("native diffuse-color default is applied before instance overrides"),
        Material->K2_GetVectorParameterValue(TEXT("Tint")), FLinearColor(.5f, .5f, .5f, 1.f));
    return true;
}
#endif
