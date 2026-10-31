#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2PortalSpace.h"
#include "AC2WorldGrid.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PortalArrivalTest,"AC2.Render.PortalArrivalReadiness",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2PortalArrivalTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    Mode->LoginStarted=true;Mode->CharacterUIStarted=true;Mode->ViewRadius=2;
    auto* PC=World->SpawnActor<APlayerController>();
    auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    auto* Portal=World->SpawnActor<AAC2PortalSpace>(FVector(0,0,-20000000),FRotator::ZeroRotator);
    Mode->Network=MakeUnique<FAC2Network>(Mode);
    auto& Net=*Mode->Network;
    Net.Phase=TEXT("entering");Net.HasPlayerPose=Net.NeedsPlacement=true;
    Net.Cell=0x02990109;Net.Feet=AC2::BlockOrigin(0x0299)+FVector(5906.0577,24019.9,-4460.6205);
    TestEqual(TEXT("Destination is covered before the effect's first tick"),Portal->CoverOpacity(),1.f);
    struct FState {int32 Stage=0,Ticks=0;bool SawNearWithoutFar=false;double Started=FPlatformTime::Seconds();};
    auto State=MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,Mode,Player,Portal,State](){
        if(FPlatformTime::Seconds()-State->Started>240)
        {AddError(FString::Printf(TEXT("Arrival stage %d timed out: %s"),State->Stage,*Mode->Status));return true;}
        Mode->Tick(.05f);Portal->Tick(.05f);World->SendAllEndOfFrameUpdates();++State->Ticks;
        if(Mode->WantsPortalSpace())
        {
            if(!Player->IsHeld() || Portal->CoverOpacity()!=1.f)
            {AddError(TEXT("Incomplete destination was exposed or allowed player movement"));return true;}
            if(State->Stage==1 && Mode->Stream->AllReady() && Mode->Far->Center()!=0x77b8)
                State->SawNearWithoutFar=true;
            return false;
        }
        if(!TestTrue(TEXT("Every required destination block finished uploading"),Mode->Stream->AllReady()))return true;
        if(!TestFalse(TEXT("Ready player is released"),Player->IsHeld()))return true;
        if(State->Stage==0)
        {
            TestFalse(TEXT("Closed dungeon does not wait for distant outdoor terrain"),Mode->Stream->ExteriorVisible());
            TestEqual(TEXT("Closed dungeon loads only its own block"),Mode->Stream->ResidentCount(),1);
            TestTrue(TEXT("Initial scene acknowledgement completes login"),Mode->Network->IsWorld());
            // Start outdoor travel during the exit flash. This must cover even
            // before the portal actor receives another Tick.
            auto& Network=*Mode->Network;FString Error;
            if(!TestTrue(TEXT("Outdoor DAT destination exists"),Mode->Stream->Destination(0x77b8,Network.Feet,Error)))return true;
            Network.Cell=0x77b80001;Network.NeedsPlacement=true;
            TestEqual(TEXT("Reentry during exit is immediately opaque"),Portal->CoverOpacity(),1.f);
            Portal->Tick(.01f);
            TestEqual(TEXT("Reentry cancels exit rather than flashing the new terrain"),Portal->Phase,2);
            State->Stage=1;return false;
        }
        if(State->Stage==1)
        {
            TestEqual(TEXT("Distant mesh matches the revealed outdoor destination"),Mode->Far->Center(),uint32(0x77b8));
            TestTrue(TEXT("Loading hold covered the near-ready / far-pending interval"),State->SawNearWithoutFar);
            TestTrue(TEXT("Outdoor scenery was uploaded"),Mode->Stream->SceneryCount()>0);
            State->Stage=2;return false;
        }
        if(Portal->Active())return false;
        TestEqual(TEXT("Completed native exit reveals the world"),Portal->CoverOpacity(),0.f);
        TestFalse(TEXT("Native portal capture stops after arrival"),Portal->Capture->bCaptureEveryFrame);
        // Also exercise offline/map travel; these previously requested no portal.
        Mode->Network.Reset();
        TestTrue(TEXT("Offline map travel starts"),Mode->TravelToMap(FVector2D(0x77*8+4,0xb8*8+4)));
        TestTrue(TEXT("Offline travel requests portal space"),Mode->WantsPortalSpace());
        TestEqual(TEXT("Offline destination is immediately covered"),Portal->CoverOpacity(),1.f);
        AddInfo(FString::Printf(TEXT("Portal readiness checked for %d streaming frames"),State->Ticks));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Mode,Portal](){
        Portal->Destroy();Mode->Network.Reset();Mode->LeaveWorld();
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
    }));
    return true;
}
#endif
