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
    PC->ClientSetHUD(AAC2HUD::StaticClass());
    auto* HUD=Cast<AAC2HUD>(PC->GetHUD());
    if(!TestNotNull(TEXT("Portal HUD installed"),HUD))return false;
    HUD->PortalEffect=Portal;
    Mode->Network=MakeUnique<FAC2Network>(Mode);
    auto& Net=*Mode->Network;
    Net.Phase=TEXT("entering");Net.HasPlayerPose=Net.NeedsPlacement=true;
    Net.Cell=0x02990109;Net.Feet=AC2::BlockOrigin(0x0299)+FVector(5906.0577,24019.9,-4460.6205);
    Net.PlayerId=TEXT("arrival-player");
    auto Object=MakeUnique<FAC2Network::FObject>();Object->Placed=true;
    Net.Objects.Add(Net.PlayerId,MoveTemp(Object));
    TestTrue(TEXT("Test avatar loads the native portal effects"), Player->ApplyAvatar(0x1f000023,1,1));
    TestEqual(TEXT("Destination is covered before the effect's first tick"),Portal->CoverOpacity(),1.f);
    struct FState {int32 Stage=0,Ticks=0,StoryTicks=0,VisibleParticles=0;bool CorrectedWhileLoading=false,SawNearWithoutFar=false;double Started=FPlatformTime::Seconds();};
    auto State=MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,Mode,Player,Portal,State](){
        if(FPlatformTime::Seconds()-State->Started>240)
        {AddError(FString::Printf(TEXT("Arrival stage %d timed out: %s"),State->Stage,*Mode->Status));return true;}
        auto OwnerScript=[&](const TCHAR* Event,const TCHAR* Field,bool Value)
        {
            auto Script=MakeShared<FJsonObject>();Script->SetStringField(TEXT("event"),Event);
            Script->SetBoolField(Field,Value);Mode->Network->Handle(Script);
        };
        auto ArrivalFx=[&](const TCHAR* Event)
        {
            auto Fx=MakeShared<FJsonObject>();Fx->SetStringField(TEXT("event"),Event);
            Fx->SetNumberField(TEXT("fx"),0x41000010);
            Mode->Network->Handle(Fx);
        };
        // Icefire sends ordinary self position echoes while the portal exit
        // animation is still running. They must not restart streaming or audio.
        auto Echo=MakeShared<FJsonObject>();Echo->SetStringField(TEXT("event"),TEXT("position"));
        Echo->SetStringField(TEXT("id"),Mode->Network->PlayerId);Echo->SetNumberField(TEXT("instance"),0);
        Echo->SetNumberField(TEXT("position_stamp"),State->Ticks+1);Echo->SetNumberField(TEXT("cell"),Mode->Network->Cell);
        Echo->SetNumberField(TEXT("teleport"),State->Stage>0?1:0);Echo->SetNumberField(TEXT("force"),0);
        Mode->Network->Handle(Echo);
        Mode->Tick(.05f);Portal->Tick(.05f);Player->Tick(.05f);
        World->SendAllEndOfFrameUpdates();++State->Ticks;
        auto CorrectCurrentPosition=[&](int32 ForceStamp)
        {
            auto& Network=*Mode->Network;
            const FVector Local=(Network.Feet-AC2::BlockOrigin(Network.Cell>>16))/100;
            Echo->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(Local.X),MakeShared<FJsonValueNumber>(Local.Y),MakeShared<FJsonValueNumber>(Local.Z)});
            Echo->SetNumberField(TEXT("position_stamp"),++State->Ticks);
            Echo->SetNumberField(TEXT("force"),ForceStamp);
            Network.Handle(Echo);
        };
        if (State->Stage==1 && Mode->bRevealing && !State->CorrectedWhileLoading)
        {
            State->CorrectedWhileLoading=true;
            CorrectCurrentPosition(1);
            TestFalse(TEXT("A force correction after landing preserves the pending render fence"),Mode->Network->NeedsPlacement);
        }
        if (State->Stage==1 && Mode->Network->PlayingStoryScene() && Mode->WaitingForExitAnimation)
        {
            if (!TestTrue(TEXT("Destination is ready behind the story"),Mode->Network->IsWorld()) ||
                !TestTrue(TEXT("Story keeps destination covered and movement held"),
                    Mode->WantsPortalSpace() && Portal->CoverOpacity()==1.f && Player->IsHeld())) return true;
            if (State->StoryTicks==0)
            {
                auto& Network=*Mode->Network;
                CorrectCurrentPosition(2);
                TestFalse(TEXT("A server correction under the movie does not reload the destination"),Network.NeedsPlacement);
                ArrivalFx(TEXT("stop_fx"));
                TestTrue(TEXT("An early server stop preserves the covered arrival effect"),Mode->Network->RevealingArrivalEffect);
                auto Visual=MakeShared<FJsonObject>();
                Visual->SetNumberField(TEXT("did"),0x1f000023);
                Mode->Network->ApplyVisual(*Mode->Network->Objects[Mode->Network->PlayerId],Visual,true);
                // A player can watch the entire narrated scene while the destination is ready.
                for (int32 Frame=0;Frame<1800;++Frame) Player->Tick(1.f/15);
            }
            if (++State->StoryTicks==4) Mode->Network->CompleteStoryScene(0x40000021);
            return false;
        }
        if(Mode->WantsPortalSpace())
        {
            if(State->Stage==0 && Mode->Network->IsWorld())
            {AddError(TEXT("Scene acknowledgement activated combat before the loading hold ended"));return true;}
            if(!Player->IsHeld() || Portal->CoverOpacity()!=1.f)
            {AddError(TEXT("Incomplete destination was exposed or allowed player movement"));return true;}
            if(State->Stage==1 && Mode->Stream->AllReady() && Mode->Far->Center()!=0x77b8)
                State->SawNearWithoutFar=true;
            return false;
        }
        if(!TestTrue(TEXT("Every required destination block finished uploading"),Mode->Stream->AllReady()))return true;
        if(State->Stage==0)
        {
            TestTrue(TEXT("World acknowledged under cover keeps player held through emergence"),Player->IsHeld());
            TestFalse(TEXT("Closed dungeon does not wait for distant outdoor terrain"),Mode->Stream->ExteriorVisible());
            TestEqual(TEXT("Closed dungeon loads only its own block"),Mode->Stream->ResidentCount(),1);
            TestTrue(TEXT("Initial scene acknowledgement completes login"),Mode->Network->IsWorld());
            // Start outdoor travel during the exit flash. This must cover even
            // before the portal actor receives another Tick.
            auto& Network=*Mode->Network;FString Error;FVector Destination;
            if(!TestTrue(TEXT("Outdoor DAT destination exists"),Mode->Stream->Destination(0x77b8,Destination,Error)))return true;
            const FVector Local=(Destination-AC2::BlockOrigin(0x77b8))/100;
            auto Travel=MakeShared<FJsonObject>();Travel->SetStringField(TEXT("event"),TEXT("position"));
            Travel->SetStringField(TEXT("id"),Network.PlayerId);Travel->SetNumberField(TEXT("instance"),0);
            Travel->SetNumberField(TEXT("position_stamp"),++State->Ticks);
            Travel->SetNumberField(TEXT("cell"),0x77b80001);
            Travel->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(Local.X),MakeShared<FJsonValueNumber>(Local.Y),MakeShared<FJsonValueNumber>(Local.Z)});
            Travel->SetNumberField(TEXT("teleport"),1);Travel->SetNumberField(TEXT("force"),0);
            OwnerScript(TEXT("portal_movement_lock"),TEXT("locked"),true);
            Network.ActiveStoryScene=0x40000021; // Retail queues the movie before the transfer.
            OwnerScript(TEXT("portal_space"),TEXT("active"),true);
            ArrivalFx(TEXT("fx"));
            OwnerScript(TEXT("portal_space"),TEXT("active"),false);
            Network.Handle(Travel);
            TestFalse(TEXT("Destination stamp consumes the pending owner entry"),Network.PortalSpaceRequested);
            TestTrue(TEXT("Server teleport stamp starts destination streaming"),Network.NeedsPlacement);
            TestEqual(TEXT("Reentry during exit is immediately opaque"),Portal->CoverOpacity(),1.f);
            Portal->Tick(.01f);
            TestEqual(TEXT("Reentry cancels exit rather than flashing the new terrain"),Portal->Phase,2);
            State->Stage=1;return false;
        }
        if(State->Stage==1)
        {
            TestEqual(TEXT("Story completion releases only the visual hold"),State->StoryTicks,4);
            TestEqual(TEXT("Distant mesh matches the revealed outdoor destination"),Mode->Far->Center(),uint32(0x77b8));
            TestTrue(TEXT("Loading hold covered the near-ready / far-pending interval"),State->SawNearWithoutFar);
            TestTrue(TEXT("Outdoor scenery was uploaded"),Mode->Stream->SceneryCount()>0);
            OwnerScript(TEXT("portal_movement_lock"),TEXT("locked"),false);
            TestTrue(TEXT("Ready reply preserves the arrival effect through the tunnel"),Mode->Network->RevealingArrivalEffect);
            TestTrue(TEXT("Destination acknowledgement keeps the local emergence hold"),Player->IsHeld());
            State->Stage=2;return false;
        }
        if(Portal->Active())
        {
            if (Portal->Phase==4 && Portal->Flash()<.75f)
                State->VisibleParticles=FMath::Max(State->VisibleParticles,Player->EffectParticleCount());
            return false;
        }
        if(Player->IsHeld())return false;
        TestFalse(TEXT("Player is released after the emergence animation"),Player->IsHeld());
        TestTrue(TEXT("Native particles are present on the rebuilt avatar in the visible destination"),State->VisibleParticles>0);
        TestFalse(TEXT("Local effect hold ends after emergence"),Mode->Network->RevealingArrivalEffect);
        TestEqual(TEXT("Completed native exit reveals the world"),Portal->CoverOpacity(),0.f);
        TestFalse(TEXT("Native portal capture stops after arrival"),Portal->Capture->bCaptureEveryFrame);
        TestEqual(TEXT("World field of view restored after reveal"),Portal->WorldFOVScale(),1.f);
        // A retained origin is the only scene allowed to show through the entry fade.
        OwnerScript(TEXT("portal_movement_lock"),TEXT("locked"),true);
        Player->SetHeld(false);
        TestTrue(TEXT("Server movement lock survives a local release"),Player->IsHeld());
        const FVector Source=Player->GetActorLocation();
        OwnerScript(TEXT("portal_space"),TEXT("active"),true);
        TestTrue(TEXT("Owner entry starts at the retained source"),Mode->WantsPortalSpace());
        Portal->Tick(.5f);
        TestEqual(TEXT("Departing avatar remains visible behind the half-faded tunnel"),Portal->CoverOpacity(),0.f);
        TestEqual(TEXT("Halfway through native entry blend"),Portal->Opacity(),.5f);
        TestTrue(TEXT("Departure view narrows quadratically"),FMath::IsNearlyEqual(Portal->WorldFOVScale(),.8f));
        OwnerScript(TEXT("portal_space"),TEXT("active"),true);
        Portal->Tick(.5f);
        TestEqual(TEXT("Entry leaves the pawn at the source"),Player->GetActorLocation(),Source);
        TestFalse(TEXT("Full tunnel permits relocation"),Portal->HoldDeparture());
        TestEqual(TEXT("Destination streams under full cover"),Portal->CoverOpacity(),1.f);
        OwnerScript(TEXT("portal_space"),TEXT("active"),false);
        Portal->Tick(.05f);
        TestEqual(TEXT("Exit signal alone cannot expose an unplaced destination"),Portal->CoverOpacity(),1.f);
        OwnerScript(TEXT("portal_movement_lock"),TEXT("locked"),false);
        TestFalse(TEXT("Owner unlock releases the cancelled transfer"),Player->IsHeld());
        Portal->Tick(.001f);Portal->Tick(1.f);
        TestTrue(TEXT("Emerging camera opens with native wide view"),FMath::IsNearlyEqual(Portal->WorldFOVScale(),2.4f));
        Portal->Tick(.5f);
        TestTrue(TEXT("Exit camera eases to the player's lens"),FMath::IsNearlyEqual(Portal->WorldFOVScale(),1.35f));
        Portal->Tick(.5f);
        // Also exercise offline/map travel; these previously requested no portal.
        Mode->Network.Reset();
        TestTrue(TEXT("Offline map travel starts"),Mode->TravelToMap(FVector2D(0x77*8+4,0xb8*8+4)));
        TestTrue(TEXT("Offline travel requests portal space"),Mode->WantsPortalSpace());
        TestEqual(TEXT("Offline departure displays the retained origin"),Portal->CoverOpacity(),0.f);
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
