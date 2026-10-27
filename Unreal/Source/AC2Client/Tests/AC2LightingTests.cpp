#include "AC2Sky.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SkyLightingTest, "AC2.Data.NativeSkyLightingAndLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2SkyLightingTest::RunTest(const FString &)
{
    FAC2SkyKey Key;
    Key.Heading = 90;
    Key.Pitch = 30;
    TestTrue(TEXT("East sun casts downward toward west"),
        Key.LightDirection().Equals(FVector(-FMath::Sqrt(3.) / 2, 0, -.5), .00001));
    Key.Pitch = 150;
    TestTrue(TEXT("Sun crossing zenith changes horizontal direction, not vertical sign"),
        Key.LightDirection().Equals(FVector(FMath::Sqrt(3.) / 2, 0, -.5), .00001));
    Key.Pitch = 90;
    TestTrue(TEXT("Zenith light travels straight down"), Key.LightDirection().Equals(-FVector::UpVector, .00001));

    const FString Directory = AC2::ResolveDatDirectory(TEXT(""));
    FAC2DatArchive Portal; FString Error; TArray<uint8> Bytes; FAC2SkyDay Day;
    if (!TestTrue(TEXT("Native day record"), Portal.Open(Directory / TEXT("portal.dat"), 1, Error) &&
        Portal.Read(0x1a000002, Bytes, Error) && Day.Decode(0x1a000002, Bytes))) return false;
    for (int32 Hour = 0; Hour < 24; ++Hour)
    {
        const auto Sample = Day.Sample(Hour / 24.f);
        TestTrue(FString::Printf(TEXT("Native hour %d lights from above"), Hour), Sample.LightDirection().Z < 0);
        TestTrue(TEXT("Unit light direction"), FMath::IsNearlyEqual(Sample.LightDirection().Size(), 1., .00001));
    }

    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld *World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto CountEnvironment = [&]() {
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
            if (It->IsA<ADirectionalLight>() || It->IsA<ASkyLight>() || It->IsA<AExponentialHeightFog>()) ++Count;
        return Count;
    };
    for (int32 Entry = 0; Entry < 2; ++Entry)
    {
        {
            FAC2SkyRenderer Sky;
            TestTrue(TEXT("Open world lighting"), Sky.Open(World, Directory, Error));
            TestEqual(TEXT("One sun, ambient and fog"), CountEnvironment(), 3);
            for (TActorIterator<ASkyLight> It(World); It; ++It)
            {
                const auto *Light = It->GetLightComponent();
                TestTrue(TEXT("Ground hemisphere has only dim bounce, not full skylight"),
                    Light->bLowerHemisphereIsBlack && Light->LowerHemisphereColor.GetLuminance() > 0 &&
                    Light->LowerHemisphereColor.GetLuminance() < .1f);
            }
        }
        TestEqual(TEXT("World exit removes lights and fog before reentry"), CountEnvironment(), 0);
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
