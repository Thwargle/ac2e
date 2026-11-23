#include "AC2Audio.h"
#include "AC2Assets.h"
#include "AC2FXScalar.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SceneryAudioTest,"AC2.Data.SceneryStartupAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SceneryAudioTest::RunTest(const FString&)
{
    FAC2FXScalar Scalar;Scalar.Start(.8f,2.f);
    TestEqual(TEXT("A ramped FX starts at silence"),Scalar.Current,0.f);
    Scalar.Advance(.5f);TestEqual(TEXT("Retail scalar rate is delta divided by ramp"),Scalar.Current,.25f);
    Scalar.Set(.1f);Scalar.Advance(.2f);
    TestTrue(TEXT("Retargeting continues from the current scalar"),FMath::IsNearlyEqual(Scalar.Current,.15f));
    Scalar.Advance(.5f);TestEqual(TEXT("Ramp clamps at the target without overshoot"),Scalar.Current,.1f);
    Scalar.Start(.8f,2.f,true);TestEqual(TEXT("Startup FX does not fade in"),Scalar.Current,.8f);
    Scalar.Start(.8f,0);Scalar.Set(.4f);TestEqual(TEXT("Unramped updates apply immediately"),Scalar.Current,.4f);
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));
    FString Error;TArray<uint8> Bytes;
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;
    if(!TestTrue(TEXT("Open scenery archives"),Portal.Open(Directory/TEXT("portal.dat"),1,Error) &&
        Cell.Open(Directory/TEXT("cell_1.dat"),2,Error) && Portal.Read(0x36000000,Bytes,Error) &&
        Defs.Decode(Bytes,Error)))return false;
    int32 NativeSounds=0,IndoorSounds=0;
    // Independent native reader resolves five startup sounds on placements in
    // 07EE cells 0113/0118/011C (visuals 1F001509 through 1F00150D).
    for(uint32 Block:{0x07eeu,0x08c3u})
    {
        FAC2LandBlock Land;FAC2AssetLibrary Assets;
        if(!TestTrue(TEXT("Load scenery fixture"),Assets.Open(Directory,Error) && Cell.Read(Block<<16|0xffff,Bytes,Error) &&
            Land.Decode(Block<<16|0xffff,Bytes,Error) && Assets.PrepareBlock(Directory,Defs,Land,
                BuildAC2TerrainMesh(Defs,Land),Error)))continue;
        for(const auto& S:Assets.SoundSources())
        {
            ++NativeSounds;IndoorSounds+=S.Cell!=0;
            TestTrue(TEXT("Scenery sound resolves to an authored record"),Portal.Read(S.Sound,Bytes,Error));
            TestTrue(TEXT("Finite placed origin and native startup scalar"),!S.Position.ContainsNaN() &&
                FMath::IsFinite(S.Volume) && S.Volume>0);
        }
        AddInfo(FString::Printf(TEXT("Block %04X has %d native startup sounds"),Block,Assets.SoundSources().Num()));
    }
    TestTrue(TEXT("Installed scenery exposes authored startup sound sources"),NativeSounds>0);
    TestTrue(TEXT("Dungeon sound emitters retain their owning cells"),IndoorSounds>0);

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SpawnActor<APlayerController>();
    {
        FAC2Audio Audio;
        if(TestTrue(TEXT("Open native mixer without speaker playback"),Audio.Open(World,Directory)))
        {
            for(uint32 I=0;I<4;++I)Audio.SetVolume(I,0);
            FAC2ScenerySound S;S.Key=uint64(1)<<62;S.Sound=0x2a0000ee;
            S.Position=FVector(100,200,300);S.NightTime=false;
            const uint32 Before=Audio.Plays;
            Audio.Scenery({S},true);
            TestEqual(TEXT("New scenery starts its sound"),Audio.Plays,Before+1);
            for(int32 I=0;I<20;++I){S.Position.X+=100;Audio.Scenery({S},true);Audio.Tick(I*.25);}
            TestEqual(TEXT("Moving the listener or repeated updates never retrigger a startup cue"),Audio.Plays,Before+1);
            S.Volume=.5f;Audio.Scenery({S},true);
            TestEqual(TEXT("An authored scalar update does not restart scenery audio"),Audio.Plays,Before+1);
            Audio.Scenery({S},false);
            TestEqual(TEXT("Day-only sound is not started at night"),Audio.Plays,Before+1);
            Audio.Scenery({S},true);
            TestEqual(TEXT("Returning to day starts a new active interval"),Audio.Plays,Before+2);
            Audio.Scenery({},true);Audio.Scenery({S},true);
            TestEqual(TEXT("Unloading and re-entering restores the scenery sound"),Audio.Plays,Before+3);
            auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);
            Actor->SetRootComponent(Root);Root->RegisterComponent();
            const uint32 ModelBefore=Audio.Plays;
            {
                FAC2AssetLibrary Model;
                // Shared visual 1F00031E resolves through its own FX table in
                // each profile (2A00066A in stock, 2A000644 in Icefire).
                TestTrue(TEXT("Native sounding model loads"),Model.Open(Directory,Error) && Model.DrawModel(0x1f00031e,Actor,Root));
                TestTrue(TEXT("Model startup also dispatches its sound"),Audio.Plays>ModelBefore);
            }
            const uint32 PreviewBefore=Audio.Plays;
            {
                FAC2AssetLibrary Preview;
                TestTrue(TEXT("Open preview assets"),Preview.Open(Directory,Error));
                Preview.SetPreviewLighting({},FTransform::Identity);
                TestTrue(TEXT("Same model draws in a preview"),Preview.DrawModel(0x1f00031e,Actor,Root));
                TestEqual(TEXT("Inventory and menu previews do not duplicate world sounds"),Audio.Plays,PreviewBefore);
            }
            Actor->Destroy();
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
