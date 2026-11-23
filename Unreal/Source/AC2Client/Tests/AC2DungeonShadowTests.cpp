#include "AC2NativeMaterial.h"
#include "AC2WorldStream.h"
#include "AC2WorldGrid.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2DungeonShadowTest,"AC2.Data.DungeonShadowStability",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2DungeonShadowTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    auto* PC=World->SpawnActor<APlayerController>();auto* Camera=World->SpawnActor<AActor>();
    Camera->SetRootComponent(NewObject<USceneComponent>(Camera));Camera->GetRootComponent()->RegisterComponent();
    PC->SetViewTarget(Camera);
    auto Stream=MakeShared<FAC2WorldStream>();FString Error;
    if(!TestTrue(TEXT("Open native dungeon"),Stream->Open(World,AC2::ResolveDatDirectory(TEXT("")),Error))) {
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;
    }
    Stream->SetInteriorCell(0x03fd0130);
    const double Started=FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,Camera,Stream,Started](){
        Stream->Tick(AC2::BlockOrigin(0x03fd));
        if(!Stream->AllReady()) {
            if(FPlatformTime::Seconds()-Started<90)return false;
            AddError(TEXT("Dungeon shadow fixture timed out"));return true;
        }
        TArray<UPointLightComponent*> Lights;
        for(TActorIterator<AActor> It(World);It;++It) {
            TArray<UPointLightComponent*> Found;It->GetComponents(Found);
            for(auto* Light:Found)if(Light->IsVisible())Lights.Add(Light);
        }
        // Load real cell lights, including more than the old eight-light cap.
        TestTrue(TEXT("Dungeon fixture exceeds the old shadow budget"),Lights.Num()>8);
        for(auto* Light:Lights) {
            Camera->SetActorLocation(Light->GetComponentLocation());
            FAC2PreviewLighting Ambient;
            UpdateAC2WorldLighting(World,Ambient,[](const FVector&,FAC2PreviewLighting&){return false;});
            for(auto* Other:Lights) {
                if(!Other->CastShadows) {
                    AddError(TEXT("Moving to another torch disables an existing light's shadows"));return true;
                }
            }
            TestTrue(TEXT("Whole-light distance fade retains the native influence volume"),
                Light->MaxDrawDistance-Light->MaxDistanceFadeRange>Light->AttenuationRadius);
        }
        AddInfo(FString::Printf(TEXT("Shadow ownership remained stable at %d native light positions"),Lights.Num()));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Stream,World](){
        Stream->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
    }));
    return true;
}
#endif
