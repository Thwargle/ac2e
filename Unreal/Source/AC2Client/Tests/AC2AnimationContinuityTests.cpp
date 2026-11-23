#include "AC2Assets.h"
#include "AC2CharacterMovement.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "AC2Portrait.h"
#include "AC2RemoteMotion.h"
#include "AC2SkillData.h"
#include "AC2WorldGrid.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace {
TArray<FVector> Vertices(AActor* Actor)
{
    TArray<FVector> Out;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
    for(auto* M:Meshes)for(int32 S=0;S<M->GetNumSections();++S)if(auto* P=M->GetProcMeshSection(S))
        for(const auto& V:P->ProcVertexBuffer)Out.Add(M->GetComponentTransform().TransformPosition(V.Position));
    return Out;
}
double Change(const TArray<FVector>& A,const TArray<FVector>& B)
{
    double Result=0;for(int32 I=0;I<FMath::Min(A.Num(),B.Num());++I)Result=FMath::Max(Result,FVector::Distance(A[I],B[I]));return Result;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AnimationContinuityTest,"AC2.Data.AnimationContinuityAndItemActions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AnimationContinuityTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    // Keep cell visibility real, but place the test well above every floor so
    // the ground probe misses exactly as it does with delayed collision/offsets.
    Mode->Stream=MakeUnique<FAC2WorldStream>();Mode->Stream->Radius=0;
    FString StreamError;
    TestTrue(TEXT("Open native animation test landblock"),Mode->Stream->Open(World,Mode->DatDirectory,StreamError));
    const FVector Focus=AC2::BlockOrigin(0x77b8)+FVector(8000,8000,200000);
    Player->SetActorLocation(Focus);
    const double Deadline=FPlatformTime::Seconds()+20;
    while(!Mode->CellVisible(0x77b80001) && FPlatformTime::Seconds()<Deadline)
    {Mode->Stream->Tick(Focus);FPlatformProcess::Sleep(.001f);}
    TestTrue(TEXT("NPC test cell is visible"),Mode->CellVisible(0x77b80001));
    {
        FAC2Network Network(Mode);
        // Missing nearby collision must not select an unsupported air branch.
        // The parent creature is shared across releases. The Legions child
        // 1F0004E0 is an unrelated static scenery model in Icefire's DAT.
        for(uint32 Visual:{0x1f000d8cu,0x1f001042u,0x1f001037u,0x1f000d8fu,0x1f000051u,0x1f0001eau,0x1f000031u})
        {
            auto Obj=MakeUnique<FAC2Network::FObject>();Obj->Placed=true;Obj->Content=true;
            Obj->Cell=0x77b80001;Obj->Goal=Focus+FVector(100,100,0);
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("did"),Visual);Network.ApplyVisual(*Obj,J,false);
            if(!TestTrue(TEXT("Native NPC/creature loads"),Obj->Model.IsValid()))continue;
            auto* O=Obj.Get();Network.Objects.Add(TEXT("actor"),MoveTemp(Obj));
            const auto NativeMotion=O->Model->MotionData().Values(AC2Motion::Ground);
            if(O->Model->HasLocomotion() && NativeMotion.Gravity && FMath::IsNearlyZero(NativeMotion.Hover))
            {
                TestTrue(TEXT("Terrestrial models ground even without a humanoid feet part"),O->FollowsGround());
                O->Body->SetRelativeLocation(FVector(0,0,999));
                Network.Tick(.05f);
                TestTrue(TEXT("Missing floor clears the previous surface offset"),FMath::IsNearlyEqual(O->Body->GetRelativeLocation().Z,-O->FootZ,.01));
                auto* FloorActor=World->SpawnActor<AActor>();auto* Floor=NewObject<UBoxComponent>(FloorActor);
                FloorActor->SetRootComponent(Floor);Floor->SetBoxExtent(FVector(300,300,5));
                Floor->SetCollisionObjectType(ECC_WorldStatic);Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
                Floor->SetCollisionResponseToAllChannels(ECR_Block);Floor->RegisterComponent();
                FloorActor->SetActorLocation(O->Goal-FVector(0,0,65));O->NextGroundCheck=0;Network.Tick(.05f);
                const auto Feet=O->Body->GetComponentTransform().TransformPosition(FVector(0,0,O->FootZ));
                TestTrue(TEXT("Scaled native body meets the queried surface"),FMath::IsNearlyEqual(Feet.Z,O->Goal.Z-58.,.1));
                FloorActor->Destroy();O->NextGroundCheck=0;Network.Tick(.05f);
                TestTrue(TEXT("Leaving a surface clears its offset"),FMath::IsNearlyEqual(O->Body->GetRelativeLocation().Z,-O->FootZ,.01));
            }
            // Icefire/native objects do not carry AC2E's reserved Content ID.
            // Missing streamed collision must not change their idle to falling.
            O->Content=false;
            TestFalse(TEXT("Remote actor uses ordinary floor checks"),O->FollowsGround());
            O->Summoner=TEXT("7");
            TestEqual(TEXT("Summoner quality enables authored ground motion"),O->FollowsGround(),O->GroundedBody);
            O->Summoner.Reset();
            const auto Before=Vertices(O->Actor.Get());
            double Max=0;
            FTransform HandBefore,HandAfter;const bool Hand=O->Model->AnimatedHoldingLocation(4,HandBefore);double HandMotion=0;
            for(int32 I=0;I<40;++I){Network.Tick(.05f);Max=FMath::Max(Max,Change(Before,Vertices(O->Actor.Get())));
                if(Hand && O->Model->AnimatedHoldingLocation(4,HandAfter))HandMotion=FMath::Max(HandMotion,FVector::Distance(HandBefore.GetLocation(),HandAfter.GetLocation()));}
            if(Visual==0x1f000d8c){AddInfo(FString::Printf(TEXT("Explorer hand %d movement %.4f"),Hand,HandMotion));TestTrue(TEXT("Explorer body hand animates independently of head and tail"),Hand&&HandMotion>.01);}
            TestFalse(TEXT("NPC is rendered during idle regression"),O->Actor->IsHidden());
            TestEqual(FString::Printf(TEXT("%08X stays in native ground animation"),Visual),O->Element,AC2Motion::Ground);
            TestTrue(FString::Printf(TEXT("%08X has a living idle, not an A pose"),Visual),Max>.1);
            O->AnimMode=AC2Motion::Monster;
            for(int32 I=0;I<80;++I)
            {
                if(I%4==0)
                {
                    auto Packet=MakeShared<FJsonObject>();Packet->SetStringField(TEXT("event"),TEXT("position"));
                    Packet->SetStringField(TEXT("id"),TEXT("actor"));Packet->SetNumberField(TEXT("cell"),O->Cell);
                    const FVector Next=(O->Goal-AC2::BlockOrigin(O->Cell>>16)+FVector(0,40,0))/100;
                    Packet->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(Next.X),MakeShared<FJsonValueNumber>(Next.Y),MakeShared<FJsonValueNumber>(Next.Z)});
                    Packet->SetNumberField(TEXT("heading"),0);Network.Handle(Packet);
                }
                Network.Tick(.025f);
            }
            TestTrue(FString::Printf(TEXT("%08X animates its pursuit with native movement packets"),Visual),O->Model->MovementAnimationUpdates()>=5);
            O->Actor->Destroy();Network.Objects.Remove(TEXT("actor"));
        }
        // Actual multipart body periods: a Tumerok head's longer idle must not
        // slow the body's pickup/drop animation.
        FString CatalogError;
        FAC2CharacterCatalog Catalog;
        TestTrue(TEXT("Open this DAT's ten playable race/sex entries"),Catalog.Open(Mode->DatDirectory,CatalogError));
        for(const auto& Avatar:Catalog.Avatars)
        {
            auto* Actor=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Model;FString Error;
            if(TestTrue(TEXT("Item-action body loads"),Model.Open(Mode->DatDirectory,Error)&&
                Model.DrawModel(Avatar.Physics,Actor,Actor->Body,Avatar.Resolve(Avatar.Defaults()))))
            {
                float Time=0;
                for(uint32 B:{AC2Motion::Pickup,AC2Motion::Putdown})
                {
                    const auto Before=Vertices(Actor);
                    TestTrue(TEXT("Native pickup/putdown resolves"),Model.PlayBehavior(B,AC2Motion::Peace));
                    if(Avatar.Race<=4)
                    {
                        const float Expected=Avatar.Race==2?(B==AC2Motion::Pickup?1.45833f:2.f):
                            Avatar.Race==4?(B==AC2Motion::Pickup?1.875f:2.f):(B==AC2Motion::Pickup?1.79167f:1.75f);
                        TestTrue(TEXT("Body controls duration, not the head"),FMath::IsNearlyEqual(Model.BehaviorDuration(B),Expected,.002f));
                    }
                    TestTrue(TEXT("Hands released during reach"),Model.HandlingItem());
                    for(int32 I=0;I<20;++I){Time+=.025f;Model.Animate(Time,0);}
                    TestTrue(TEXT("Reach changes rendered vertices"),Change(Before,Vertices(Actor))>1);
                    const float End=Time+Model.BehaviorDuration(B)+.1f;
                    while(Time<End){Time+=.025f;Model.Animate(Time,0);}
                    TestFalse(TEXT("Finite action returns to idle and releases hands"),Model.HandlingItem());
                }
                for(uint32 Recall:{0x410000bfu,0x410000d8u,0x410000d5u})
                {
                    const bool Plays=Model.PlayBehavior(Recall,0x41000011u);
                    AddInfo(FString::Printf(TEXT("Recall body %08X behavior %08X duration %.3f"),Avatar.Physics,Recall,Model.BehaviorDuration(Recall)));
                    TestTrue(FString::Printf(TEXT("Body %08X resolves recall action %08X"),Avatar.Physics,Recall),Plays);
                    if(!Plays)continue;
                    const float Period=Model.BehaviorDuration(Recall);
                    if(Recall==0x410000bf)
                    {
                        TestTrue(TEXT("Charge is a DAT hold with immediate zero-cycle callback"),Model.CompletedHoldCycles(Recall,0));
                        // Human, Lugian and Tumerok loop from .01, Empyrean and Drudge from 0.
                        // All end at .99 on both DATs.
                        const float LoopWidth=Avatar.Race<=4?.98f:.99f;
                        Model.SeekBehavior(Recall,1.95f*Period);
                        TestTrue(TEXT("One charge cycle has elapsed"),Model.CompletedHoldCycles(Recall,1));
                        TestFalse(TEXT("Second charge cycle still pending"),Model.CompletedHoldCycles(Recall,2));
                        Time+=.05f;Model.Animate(Time,0);
                        TestTrue(TEXT("Live animation passes the second callback and keeps charging"),Model.CompletedHoldCycles(Recall,2));
                        Model.SeekBehavior(Recall,2.50f*Period);Model.Animate(Time,0);
                        const auto Wrapped=Vertices(Actor);
                        Model.SeekBehavior(Recall,(2.50f-2*LoopWidth)*Period);Model.Animate(Time,0);
                        TestTrue(TEXT("Late seek matches the same real charge pose after two wraps"),Change(Wrapped,Vertices(Actor))<.1);
                        Model.SeekBehavior(Recall,2.5f*Period);Model.ReleaseBehavior(Recall);
                        Time+=.05f;Model.Animate(Time,0);
                        TestTrue(TEXT("Release continues from the wrapped pose"),Model.BehaviorInstance(Recall)!=0);
                    }
                    else TestFalse(TEXT("Release is a finite action even with zero hold cycles"),Model.CompletedHoldCycles(Recall,0));
                    for(int32 Frame=0;Frame<FMath::CeilToInt(Period/.1f)+1;++Frame){Time+=.1f;Model.Animate(Time,0);}
                    TestEqual(TEXT("Released or finite recall track returns to idle"),Model.BehaviorInstance(Recall),uint64(0));
                }
            }
            Actor->Destroy();
        }
        auto* Movement=Cast<UAC2CharacterMovement>(Player->GetCharacterMovement());
        Movement->SetMovementMode(MOVE_Walking);Movement->MaxAcceleration=1500;
        Movement->GroundNative.Forward=5.5f;Movement->Native.Forward=0;Movement->Native.ForwardAccel=0;
        Movement->Velocity=FVector(550,0,0);Movement->Acceleration=FVector(1500,0,0);
        Movement->CalcVelocity(1.f/60,0,false,1500);
        TestTrue(TEXT("First grounded physics step retains running speed despite stale air state"),FMath::IsNearlyEqual(Movement->Velocity.X,550.,.01));
    }
    Mode->Stream.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NetworkPresentationTest,"AC2.Network.CorpseAndMissilePresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2NetworkPresentationTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    const uint32 Cell=0x77b80001;const FVector Focus=AC2::BlockOrigin(Cell>>16)+FVector(8000,8000,200000);
    Player->SetActorLocation(Focus);Mode->Stream=MakeUnique<FAC2WorldStream>();Mode->Stream->Radius=0;FString Error;
    TestTrue(TEXT("Open presentation test landblock"),Mode->Stream->Open(World,Mode->DatDirectory,Error));
    const double Deadline=FPlatformTime::Seconds()+20;
    while(!Mode->CellVisible(Cell)&&FPlatformTime::Seconds()<Deadline){Mode->Stream->Tick(Focus);FPlatformProcess::Sleep(.001f);}
    TestTrue(TEXT("Presentation test cell is visible"),Mode->CellVisible(Cell));
    {
        FAC2Network Net(Mode);
        auto Event=[](const TCHAR* Kind,const TCHAR* Id){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),Kind);J->SetStringField(TEXT("id"),Id);return J;};
        auto Enemy=MakeUnique<FAC2Network::FObject>();Enemy->Placed=true;Enemy->Cell=Cell;Enemy->Goal=Focus+FVector(1000,0,0);Enemy->AnimMode=AC2Motion::Monster;
        auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),0x1f000031);Net.ApplyVisual(*Enemy,Visual,false);
        auto* E=Enemy.Get();Net.Objects.Add(TEXT("enemy"),MoveTemp(Enemy));
        if(TestTrue(TEXT("Creature visual loads"),E->Model.IsValid())){
            auto Death=Event(TEXT("behavior"),TEXT("enemy"));Death->SetNumberField(TEXT("behavior"),AC2Motion::Death);Net.Handle(Death);
            TestTrue(TEXT("Creature death behavior resolves"),E->Model->BehaviorDuration(AC2Motion::Death)>0);
            for(int32 I=0;I<100;++I)Net.Tick(.05f);
            const auto Corpse=Vertices(E->Actor.Get());Net.ApplyVisual(*E,Visual,false);Net.Tick(.05f);
            TestTrue(TEXT("Replacement visual preserves fallen corpse pose"),Change(Corpse,Vertices(E->Actor.Get()))<.1);
            auto Idle=Event(TEXT("mode"),TEXT("enemy"));Idle->SetNumberField(TEXT("mode"),AC2Motion::Peace);Net.Handle(Idle);
            for(int32 I=0;I<40;++I)Net.Tick(.05f);
            TestTrue(TEXT("Late idle mode cannot stand a corpse up"),Change(Corpse,Vertices(E->Actor.Get()))<.1);
            TestEqual(TEXT("Corpse retains dead mode"),E->AnimMode,AC2Motion::Dead);
            Net.Handle(Event(TEXT("stop_behavior"),TEXT("enemy")));Net.Tick(.05f);
            TestTrue(TEXT("StopAll does not discard the terminal death pose"),Change(Corpse,Vertices(E->Actor.Get()))<.1);
            auto Attack=Event(TEXT("behavior"),TEXT("enemy"));Attack->SetNumberField(TEXT("behavior"),AC2Motion::Attack);Net.Handle(Attack);Net.Tick(.05f);
            TestTrue(TEXT("Late living behavior does not resurrect the corpse"),Change(Corpse,Vertices(E->Actor.Get()))<.1);
        }
        auto Missile=MakeUnique<FAC2Network::FObject>();Missile->Instance=7;
        auto MissileVisual=MakeShared<FJsonObject>();MissileVisual->SetNumberField(TEXT("did"),0x1f000066);
        Net.ApplyVisual(*Missile,MissileVisual,false);auto* M=Missile.Get();Net.Objects.Add(TEXT("missile"),MoveTemp(Missile));
        TestFalse(TEXT("Contained missile initially has no world actor"),M->Actor.IsValid());
        auto Position=[&](const FVector& P){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("cell"),Cell);
            const FVector Local=(P-AC2::BlockOrigin(Cell>>16))/100;J->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(Local.X),MakeShared<FJsonValueNumber>(Local.Y),MakeShared<FJsonValueNumber>(Local.Z)});return J;};
        auto Launch=Event(TEXT("missile"),TEXT("missile"));Launch->SetNumberField(TEXT("instance"),6);
        Launch->SetObjectField(TEXT("placement"),Position(Focus));Launch->SetObjectField(TEXT("offset"),Position(E->Goal));
        Launch->SetArrayField(TEXT("velocity"),{MakeShared<FJsonValueNumber>(10),MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(0)});
        Launch->SetBoolField(TEXT("initialized"),true);Launch->SetStringField(TEXT("target"),TEXT("enemy"));Net.Handle(Launch);
        TestFalse(TEXT("Stale activation cannot launch a missile"),M->Missile);
        // Retail GetObjectOrQueueEvent defers InitializeMissile when Create
        // arrives later. Exercise the actual incoming object path as well.
        Launch->SetStringField(TEXT("id"),TEXT("early-missile"));Launch->SetNumberField(TEXT("instance"),7);Net.Handle(Launch);
        TestTrue(TEXT("Activation waits for its object"),Net.PendingMissiles.Contains(TEXT("early-missile")));
        auto Create=Event(TEXT("object"),TEXT("early-missile"));Create->SetNumberField(TEXT("instance"),7);
        Create->SetObjectField(TEXT("visual"),MissileVisual);Net.Handle(Create);Net.Tick(.05f);
        auto* Early=Net.Objects.Find(TEXT("early-missile"));
        TestTrue(TEXT("Queued launch creates a moving visible projectile"),Early && (*Early)->Missile && (*Early)->Actor.IsValid() && !(*Early)->Actor->IsHidden());
        TestFalse(TEXT("Queued launch consumed once"),Net.PendingMissiles.Contains(TEXT("early-missile")));
        Launch->SetStringField(TEXT("id"),TEXT("removed-missile"));Net.Handle(Launch);
        Net.Handle(Event(TEXT("remove"),TEXT("removed-missile")));
        TestFalse(TEXT("Removal retires an uncreated launch"),Net.PendingMissiles.Contains(TEXT("removed-missile")));
        Launch->SetStringField(TEXT("id"),TEXT("expired-missile"));Net.Handle(Launch);Net.Tick(5.1f);
        TestFalse(TEXT("Unfulfilled launch expires"),Net.PendingMissiles.Contains(TEXT("expired-missile")));
        Launch->SetStringField(TEXT("id"),TEXT("missile"));
        Launch->SetNumberField(TEXT("instance"),7);Net.Handle(Launch);Net.Tick(.1f);
        if(TestTrue(TEXT("Activation creates a visible world missile"),M->Actor.IsValid())){
            TestFalse(TEXT("Released missile is visible"),M->Actor->IsHidden());
            TestTrue(TEXT("Target Position is absolute, not added to world origin twice"),M->Goal.Equals(Focus+FVector(100,0,0),.1));
            E->Goal+=FVector(100,0,0);E->Actor->SetActorLocation(E->Goal);Net.Tick(.1f);
            TestTrue(TEXT("Tracking retains the last known destination"),M->MissilePoint.Equals(E->Goal,.1));
            // A not-yet-created target does not immediately despawn the missile.
            M->MissileTarget=TEXT("unloaded");Net.Tick(.1f);
            TestFalse(TEXT("Missing target retains missile until destination or timeout"),M->MissileFinished);
            TestTrue(TEXT("Missing target still advances toward its last world position"),M->Goal.Equals(Focus+FVector(300,0,0),.1));
            for(int32 I=0;I<12;++I)Net.Tick(.1f);
            TestTrue(TEXT("Missile retires at its destination"),M->MissileFinished&&M->Actor->IsHidden());
        }
        FAC2SkillData Skills;TestTrue(TEXT("Open selected DAT missile definitions"),Skills.Open(Mode->DatDirectory));
        const auto Spear=Skills.Missile(0x316,0),Arrow=Skills.Missile(0xcf,0);
        if(TestTrue(TEXT("Javelin and arrow have native missile definitions"),Spear.IsSet()&&Arrow.IsSet())){
            TestEqual(TEXT("Javelin native speed in metres per second"),Spear->Speed,25.f);
            TestEqual(TEXT("Arrow native speed in metres per second"),Arrow->Speed,35.f);
            TestEqual(TEXT("Javelin native holding location"),Spear->Holding,4u);
            TestTrue(TEXT("Native javelin projectile visual resolves"),Spear->Visual>>24==0x1f);
        }
        TestFalse(TEXT("Invalid hook does not borrow another missile"),Skills.Missile(0x316,99).IsSet());
        TestFalse(TEXT("A melee hook does not invent a projectile"),Skills.Missile(0x2a0,0).IsSet());
        auto Shooter=MakeUnique<FAC2Network::FObject>();Shooter->Instance=8;Shooter->Placed=true;Shooter->Cell=Cell;Shooter->Goal=Focus;
        auto ShooterVisual=MakeShared<FJsonObject>();ShooterVisual->SetNumberField(TEXT("did"),0x1f000025);
        Net.ApplyVisual(*Shooter,ShooterVisual,false);Net.Objects.Add(TEXT("shooter"),MoveTemp(Shooter));
        auto Story=Event(TEXT("story_missiles"),TEXT("shooter"));Story->SetNumberField(TEXT("instance"),7);
        Story->SetStringField(TEXT("attacker"),TEXT("shooter"));Story->SetNumberField(TEXT("skill"),0x316);
        auto Hook=MakeShared<FJsonObject>();Hook->SetNumberField(TEXT("kind"),3);Hook->SetNumberField(TEXT("attack_hook"),0);
        Hook->SetStringField(TEXT("target"),TEXT("enemy"));Story->SetArrayField(TEXT("hooks"),{MakeShared<FJsonValueObject>(Hook)});
        Net.Handle(Story);TestTrue(TEXT("Stale story cannot create a projectile"),Net.StoryMissiles.IsEmpty());
        Story->SetNumberField(TEXT("instance"),8);Net.Handle(Story);
        TestEqual(TEXT("Story fire hook queues one visual without a server missile object"),Net.StoryMissiles.Num(),1);
        Net.Tick(.01f);auto* Local=Net.Objects.Find(TEXT("story:1"));
        if(TestTrue(TEXT("Story creates a visible projectile with actual DAT geometry"),Local&&(*Local)->Actor.IsValid()&&(*Local)->Model&&Vertices((*Local)->Actor.Get()).Num()>0)){
            TestFalse(TEXT("Story projectile is not hidden"),(*Local)->Actor->IsHidden());
            const auto Start=(*Local)->Actor->GetActorLocation();Net.Tick(.1f);
            TestTrue(TEXT("Javelin advances through the space between actors"),FMath::IsNearlyEqual(FVector::Distance(Start,(*Local)->Actor->GetActorLocation()),250.,.5));
            TestFalse(TEXT("Projectile cannot become an interaction target"),Net.Targetable(TEXT("story:1"),**Local));
        }
        Net.Tick(2.f);Net.Tick(.01f);
        TestFalse(TEXT("Local missile is destroyed after reaching target"),Net.Objects.Contains(TEXT("story:1")));
        // Exercise the queued release/cancel contract independently of an
        // individual skeleton's hook count (covered by the DAT animation read).
        Net.Handle(Story);Net.StoryMissiles.Last().Due=Net.Time+.5;
        Net.Tick(.1f);TestEqual(TEXT("Release waits until its animation hook"),Net.NextLocalMissile,uint64(1));
        Net.Handle(Event(TEXT("stop_behavior"),TEXT("shooter")));Net.Tick(.5f);
        TestEqual(TEXT("Cancelled action cannot launch a late projectile"),Net.NextLocalMissile,uint64(1));
        Net.Handle(Story);Net.StoryMissiles.Last().Due=Net.Time+1;
        auto NextStory=Event(TEXT("story"),TEXT("shooter"));NextStory->SetNumberField(TEXT("instance"),8);
        Net.Handle(NextStory);
        TestEqual(TEXT("Replacing a completed story flushes its remaining release"),Net.NextLocalMissile,uint64(2));
        TestTrue(TEXT("Story replacement consumes release only once"),Net.StoryMissiles.IsEmpty());
    }
    Mode->Stream.Reset();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PrivatePlayerPresentationTest,"AC2.Network.PrivatePlayerPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2PrivatePlayerPresentationTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    TestTrue(TEXT("Native player model loads"),Player->ApplyAvatar(0x1f000025,1,1));
    {
        FAC2Network Net(Mode);Net.PlayerId=TEXT("42");
        auto Object=MakeUnique<FAC2Network::FObject>();Object->Instance=7;Net.Objects.Add(Net.PlayerId,MoveTemp(Object));
        auto Event=[](const TCHAR* Kind){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),Kind);return J;};
        auto Fx=Event(TEXT("fx"));Fx->SetNumberField(TEXT("fx"),0x810001cd);Fx->SetNumberField(TEXT("strength"),1);
        Net.Handle(Fx);TestEqual(TEXT("Unaddressed private FX plays on the actual player model"),Net.PlayedEffects,1);
        Fx->SetStringField(TEXT("id"),Net.PlayerId);Fx->SetNumberField(TEXT("instance"),6);Net.Handle(Fx);
        TestEqual(TEXT("Stale explicit FX cannot affect the new player instance"),Net.PlayedEffects,1);
        auto Name=Event(TEXT("quality"));Name->SetNumberField(TEXT("table"),10);Name->SetNumberField(TEXT("key"),0x100);
        auto Text=MakeShared<FJsonObject>();Text->SetStringField(TEXT("literal"),TEXT("Current player"));Name->SetObjectField(TEXT("value"),Text);Net.Handle(Name);
        TestEqual(TEXT("Private name updates the local object"),Net.Objects[Net.PlayerId]->Name,FString(TEXT("Current player")));
        auto Timer=Event(TEXT("quality"));Timer->SetNumberField(TEXT("table"),8);Timer->SetNumberField(TEXT("key"),0x12f);Timer->SetNumberField(TEXT("value"),123456.125);Net.Handle(Timer);
        TestEqual(TEXT("Initial private timestamp reaches shared skill recovery"),Net.SharedSkillReset.Started,123456.125);
        TestTrue(TEXT("Held pickup starts"),Player->PlayBehavior(AC2Motion::Pickup,AC2Motion::Peace,1,true));
        const uint64 Token=Player->BehaviorInstance(AC2Motion::Pickup);
        auto Release=Event(TEXT("release_behavior"));Release->SetStringField(TEXT("id"),Net.PlayerId);
        Release->SetNumberField(TEXT("instance"),7);Release->SetNumberField(TEXT("behavior"),AC2Motion::Pickup);Net.Handle(Release);
        TestEqual(TEXT("Release retains the running action rather than restarting it"),Player->BehaviorInstance(AC2Motion::Pickup),Token);
        for(int32 I=0;I<100;++I)Player->Tick(.05f);
        TestEqual(TEXT("Released action finishes"),Player->BehaviorInstance(AC2Motion::Pickup),uint64(0));
        TestTrue(TEXT("Second held pickup starts"),Player->PlayBehavior(AC2Motion::Pickup,AC2Motion::Peace,1,true));
        Release->SetNumberField(TEXT("instance"),6);Net.Handle(Release);
        for(int32 I=0;I<100;++I)Player->Tick(.05f);
        TestTrue(TEXT("Stale release leaves the hold active"),Player->BehaviorInstance(AC2Motion::Pickup)!=0);
        auto Stop=Event(TEXT("stop_layer"));Stop->SetStringField(TEXT("id"),Net.PlayerId);Stop->SetNumberField(TEXT("instance"),6);
        for(uint32 Layer=0;Layer<32;++Layer){Stop->SetNumberField(TEXT("layer"),Layer);Net.Handle(Stop);}
        TestTrue(TEXT("Stale layer commands cannot cancel the newer action"),Player->BehaviorInstance(AC2Motion::Pickup)!=0);
        Stop->SetNumberField(TEXT("instance"),7);Stop->SetNumberField(TEXT("layer"),65535);Net.Handle(Stop);
        TestTrue(TEXT("An unrelated layer leaves the action active"),Player->BehaviorInstance(AC2Motion::Pickup)!=0);
        for(uint32 Layer=0;Layer<32;++Layer){Stop->SetNumberField(TEXT("layer"),Layer);Net.Handle(Stop);}
        TestEqual(TEXT("Stopping the native layer cancels the hold"),Player->BehaviorInstance(AC2Motion::Pickup),uint64(0));
        TestTrue(TEXT("Death starts"),Player->PlayBehavior(AC2Motion::Death));
        const uint64 Death=Player->BehaviorInstance(AC2Motion::Death);
        Release->SetNumberField(TEXT("instance"),7);Release->SetNumberField(TEXT("behavior"),AC2Motion::Death);Net.Handle(Release);
        for(uint32 Layer=0;Layer<32;++Layer){Stop->SetNumberField(TEXT("layer"),Layer);Net.Handle(Stop);}
        TestEqual(TEXT("Release and layer controls preserve the terminal corpse pose"),Player->BehaviorInstance(AC2Motion::Death),Death);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RemoteGaitTest,"AC2.Data.RemoteMovementCadence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RemoteGaitTest::RunTest(const FString&)
{
    for(double Speed:{390.,650.,780.})for(double Hz:{30.,60.,120.})
    {
        FAC2RemoteMotion Motion;FVector Current=FVector::ZeroVector;double Last=0,Min=1.e9,Max=0;
        for(int32 I=0;I<int32(Hz*2);++I)
        {
            double Now=I/Hz;
            if(I%int32(Hz/10)==0)Motion.Push(Current,FVector(Speed*(Now+.1),0,0),Now);
            const FVector Next=Motion.Sample(Now+1/Hz,Current);
            if(I>Hz*.5){double V=(Next.X-Current.X)*Hz;Min=FMath::Min(Min,V);Max=FMath::Max(Max,V);}
            Current=Next;Last=Now+1/Hz;
        }
        TestTrue(TEXT("Native speeds survive packet presentation without easing pulses"),FMath::Abs(Max-Min)<.01&&FMath::Abs(Max-Speed)<.01);
        const auto End=Motion.Sample(Last+5,Current);
        TestTrue(TEXT("A stopped/missing sender never extrapolates forever"),End.Equals(Motion.To));
        Motion.Push(Current,FVector(50000,0,0),Last+6);
        TestTrue(TEXT("Teleport snaps without a false running segment"),Motion.Sample(Last+6,Current).Equals(FVector(50000,0,0)));
    }
    return true;
}
#endif
