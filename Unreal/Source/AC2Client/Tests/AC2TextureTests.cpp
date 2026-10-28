#include "AC2TextureMips.h"
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
#endif
