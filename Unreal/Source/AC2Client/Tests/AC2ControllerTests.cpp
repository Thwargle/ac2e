#include "AC2MotionData.h"
#include "AC2Locomotion.h"
#include "AC2LoginUI.h"
#include "AC2Projectile.h"
#include "AC2DatArchive.h"
#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2VaultFixture.h"
#include "AC2Portrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SupportCastStanceTest,"AC2.Data.SupportCastNativeStance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SupportCastStanceTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;FAC2BehaviorData Behavior;
    if(!TestTrue(TEXT("Portal"),Portal.Open(AC2::ResolveDatDirectory(TEXT(""))/TEXT("portal.dat"),1,Error)))return false;
    if(!TestTrue(TEXT("Human native behavior table"),Portal.Read(0x44000001,Bytes,Error)&&Behavior.Decode(0x44000001,Bytes)))return false;
    TestEqual(TEXT("Ease Suffering has no peace combo"),Behavior.Key(AC2Motion::Ground,AC2Motion::Peace,0x410000c2),0u);
    for(const uint32 Mode:{0x4100000au,0x4100000fu,0x41000010u,0x41000011u})
        TestTrue(*FString::Printf(TEXT("Native heal combo in weapon stance %08X"),Mode),Behavior.Key(AC2Motion::Ground,Mode,0x410000c2)!=0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MotionTest,"AC2.Data.NativeControllerTables",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2MotionTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;
    if(!TestTrue(TEXT("Portal"),Portal.Open(AC2::ResolveDatDirectory(TEXT(""))/TEXT("portal.dat"),1,Error)))return false;
    FAC2MotionData Motion;FAC2BehaviorData Behavior;
    TestTrue(TEXT("Native movement record"),Portal.Read(0x32000000,Bytes,Error)&&Motion.Decode(0x32000000,Bytes));
    TestTrue(TEXT("Ground can move and jump"),Motion.Values(AC2Motion::Ground).Forward>0&&Motion.Values(AC2Motion::Ground).Jump>0);
    TestEqual(TEXT("No native human air steering"),Motion.Values(AC2Motion::Air).ForwardAccel,0.f);
    TestTrue(TEXT("Water has buoyancy and drag"),Motion.Values(AC2Motion::Water).Lift>0&&Motion.Values(AC2Motion::Water).Drag>0);
    const double Waterline=180.*Motion.Water;
    TestEqual(TEXT("Each remote body enters water at its own depth"),AC2Locomotion::Element(true,Waterline+1,180,Motion,false,AC2Motion::Air),AC2Motion::Water);
    TestEqual(TEXT("Waterline hysteresis prevents remote pose flicker"),AC2Locomotion::Element(true,Waterline-10,180,Motion,false,AC2Motion::Water),AC2Motion::Water);
    TestEqual(TEXT("Shallow grounded swimmer returns to wading"),AC2Locomotion::Element(true,30,180,Motion,true,AC2Motion::Water),AC2Motion::SeaFloor);
    TestEqual(TEXT("No water under an airborne observer"),AC2Locomotion::Element(false,999,180,Motion,false,AC2Motion::Water),AC2Motion::Air);
    FAC2MotionValues WaterValues;WaterValues.Lift=20;
    TestEqual(TEXT("Above water recovery uses gravity, not amplified buoyancy"),AC2Locomotion::WaterLift(-2,WaterValues,-980),-980.);
    TestEqual(TEXT("Submerged buoyancy saturates at native lift"),AC2Locomotion::WaterLift(2,WaterValues,-980),2000.);
    TestTrue(TEXT("Drag preserves speed in acceleration direction"),AC2Locomotion::Friction(FVector(100,100,0),FVector(1,0,0),.5,1).Equals(FVector(100,50,0)));
    TestTrue(TEXT("No input damps all velocity"),AC2Locomotion::Friction(FVector(100,100,0),FVector::ZeroVector,.5,1).Equals(FVector(50,50,0)));
    TestTrue(TEXT("Native behavior table including tail"),Portal.Read(0x44000001,Bytes,Error)&&Behavior.Decode(0x44000001,Bytes));
    TestTrue(TEXT("Retail table exceeds old 4096 limit"),Behavior.Combos.Num()>4096);
    TestEqual(TEXT("Native ordered layer names"),Behavior.LayerOrder.Num(),9);
    TestTrue(TEXT("Locomotion uses the authored half-second ramp"),FMath::IsNearlyEqual(Behavior.Ramp(AC2Motion::Move),.5f));
    TestTrue(TEXT("Idle uses the authored stop ramp"),FMath::IsNearlyEqual(Behavior.Ramp(0x40000002),.3f));
    TestEqual(TEXT("Missing mode transition has native zero ramp"),Behavior.ModeRamp(0,0,0,0),0.f);
    TestTrue(TEXT("Attack takes priority over damage; death overrides both"),
        Behavior.Layer(AC2Motion::Ground,AC2Motion::Melee,AC2Motion::Hit)<Behavior.Layer(AC2Motion::Ground,AC2Motion::Melee,AC2Motion::Attack) &&
        Behavior.Layer(AC2Motion::Ground,AC2Motion::Melee,AC2Motion::Attack)<Behavior.Layer(AC2Motion::Ground,AC2Motion::Dead,AC2Motion::Death));
    auto Complete=Bytes;Bytes.Pop();TestFalse(TEXT("Truncated table rejected"),FAC2BehaviorData().Decode(0x44000001,Bytes));
    for(uint32 Action:{AC2Motion::Jump,AC2Motion::Land,AC2Motion::Attack,AC2Motion::Hit,AC2Motion::Death,AC2Motion::Recall})
    {
        const uint32 Mode=(Action==AC2Motion::Attack||Action==AC2Motion::Recall)?AC2Motion::Melee:AC2Motion::Peace;
        uint32 Key=Behavior.Key(AC2Motion::Ground,Mode,Action);
        if(!Key)Key=Behavior.Key(AC2Motion::Air,Mode,Action);
        TestTrue(FString::Printf(TEXT("Behavior %08X mapped"),Action),Key!=0);
        auto Graph=FAC2Animator::Load(Portal,0x03000014,Key);
        TestTrue(FString::Printf(TEXT("Behavior %08X graph"),Action),Graph.IsValid());
        if(Graph){FAC2AnimatorInput Input;Input.Mode=Mode;Input.Element=AC2Motion::Ground;TestTrue(TEXT("Action has duration"),Graph->Period(Input)>0);}
    }
    FAC2Setup Setup;Setup.Parents={MAX_uint32,0};
    FAC2AnimatorInput Unarmed;Unarmed.Mode=AC2Motion::Unarmed;
    auto UnarmedAttack=FAC2Animator::Load(Portal,0x03000014,0x8100005f);
    TestTrue(TEXT("Human unarmed graph branch"),UnarmedAttack&&UnarmedAttack->Period(Unarmed)>0);
    // Both releases use this Idol body / animation map, but its visual names
    // different native behavior tables (Legions 44000018, Icefire 44000015).
    if(!TestTrue(TEXT("Idol visual with instance, motion and behavior fields"),Portal.Read(0x1f0003d1,Bytes,Error)
        && Bytes.Num()>=16 && (AC2::U32(Bytes.GetData())&15)==13 && AC2::U32(Bytes.GetData()+4)==0x1f0003d1))return false;
    const uint32 MonsterTable=AC2::U32(Bytes.GetData()+12);
    if(!TestTrue(TEXT("Idol behavior table"),Portal.Read(MonsterTable,Bytes,Error)&&Behavior.Decode(MonsterTable,Bytes)))return false;
    auto IdolAttack=FAC2Animator::Load(Portal,0x03000072,Behavior.Key(AC2Motion::Ground,AC2Motion::Monster,AC2Motion::MeleeNormal));
    FAC2AnimatorInput Monster;Monster.Mode=AC2Motion::Monster;
    TestTrue(TEXT("Idol native melee graph"),IdolAttack&&IdolAttack->Period(Monster)>0);
    auto DrudgeAttack=FAC2Animator::Load(Portal,0x0300002a,Behavior.Key(AC2Motion::Ground,AC2Motion::Monster,AC2Motion::MeleeNormal));
    TestTrue(TEXT("Shelter Drudge native melee graph"),DrudgeAttack&&DrudgeAttack->Period(Monster)>0);
    FAC2AnimatorInput Dead;Dead.Mode=0x41000005;
    auto DrudgeDeath=FAC2Animator::Load(Portal,0x0300002a,Behavior.Key(AC2Motion::Ground,Dead.Mode,AC2Motion::Death));
    TestTrue(TEXT("Shelter Drudge native death graph"),DrudgeDeath&&DrudgeDeath->Period(Dead)>0);
    TArray<FTransform> Pose={FTransform(FQuat::Identity,FVector(1,2,3),FVector(2,2,2)),FTransform(FVector(0,0,10))};
    TestTrue(TEXT("Skin inherits scale"),Setup.World(Pose)[1].GetOrigin().Equals(FVector(1,2,23)));
    TestTrue(TEXT("Attachment bone excludes scale"),Setup.World(Pose,false)[1].GetOrigin().Equals(FVector(1,2,13)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LoginEndpointTest,"AC2.Data.AccountEndpointPolicy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LoginEndpointTest::RunTest(const FString&)
{
    TestTrue(TEXT("Hosted HTTPS"),SAC2LoginUI::ValidEndpoint(TEXT("https://accounts.example.com:8443")));
    TestTrue(TEXT("Local development"),SAC2LoginUI::ValidEndpoint(TEXT("http://127.0.0.1:19280")));
    TestFalse(TEXT("No remote plaintext passwords"),SAC2LoginUI::ValidEndpoint(TEXT("http://accounts.example.com")));
    TestFalse(TEXT("No embedded credentials"),SAC2LoginUI::ValidEndpoint(TEXT("https://user:secret@example.com")));
    TestFalse(TEXT("No argument injection"),SAC2LoginUI::ValidEndpoint(TEXT("https://example.com\" -other")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AnimationFXTest,"AC2.Data.NativeAnimationFXTriggers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2AnimationFXTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;
    if(!TestTrue(TEXT("Portal"),Portal.Open(AC2::ResolveDatDirectory(TEXT(""))/TEXT("portal.dat"),1,Error)))return false;
    // Resolve the authored motion. Its identical keyframes occupy different
    // archive records (05000006 in Legions, 0500114E in Icefire).
    const auto Motion=FAC2Animator::Load(Portal,0x0300004f,0x80000006);
    if(!TestTrue(TEXT("Native AnimInfo motion resolves"),Motion.IsValid()))return false;
    const auto Clips=Motion->ActiveClips(FAC2AnimatorInput());
    if(!TestEqual(TEXT("Native AnimInfo motion contains one clip"),Clips.Num(),1))return false;
    const uint32 ClipId=Clips[0];FAC2Animation Clip;
    if(!TestTrue(TEXT("Native AnimInfo decoded"),Portal.Read(ClipId,Bytes,Error)&&Clip.Decode(ClipId,Bytes)))return false;
    TestEqual(TEXT("Native effect count"),Clip.Effects.Num(),1);
    if(Clip.Effects.Num()==1){TestEqual(TEXT("Native effect key"),Clip.Effects[0].Id,0x81000057u);TestTrue(TEXT("Timed effect"),Clip.Effects[0].Phase>0 && Clip.Effects[0].Phase<1);}
    Bytes.Pop();TestFalse(TEXT("Truncated AnimInfo rejected"),FAC2Animation().Decode(ClipId,Bytes));
    FAC2BehaviorData Behavior;TestTrue(TEXT("Behavior table"),Portal.Read(0x44000001,Bytes,Error)&&Behavior.Decode(0x44000001,Bytes));
    FAC2AnimatorInput Input;Input.Mode=0x41000011;Input.Element=AC2Motion::Ground;
    const uint32 Key=Behavior.Key(Input.Element,Input.Mode,0x410000b9);
    auto Graph=FAC2Animator::Load(Portal,0x03000014,Key);
    TestTrue(TEXT("Mind Lash native Nuke animation"),Graph.IsValid());
    if(Graph){auto Effects=Graph->EffectsBetween(0,1,Input);TestTrue(TEXT("Cast has authored timed effects"),Effects.Num()>0);
        TestEqual(TEXT("No replay when the animation time is unchanged"),Graph->EffectsBetween(.5,.5,Input).Num(),0);}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ProjectileTest,"AC2.Data.ProjectilePresentationAndFXScripts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ProjectileTest::RunTest(const FString&)
{
    const FVector Start(0,0,100),Velocity(1000,0,0),Target(200,0,100);bool Arrived=false;
    const auto Mid=AC2Projectile::Advance(Start,Velocity,&Target,.1f,Arrived);
    TestFalse(TEXT("Projectile has not reached target"),Arrived);
    TestTrue(TEXT("No overshoot on a slow frame"),AC2Projectile::Advance(Mid,Velocity,&Target,.25f,Arrived).Equals(Target));
    TestTrue(TEXT("Arrival ends flight"),Arrived);
    TestTrue(TEXT("Untargeted projectile preserves native velocity"),AC2Projectile::Advance(Start,Velocity,nullptr,.2f,Arrived).Equals(Target));
    TestFalse(TEXT("Untargeted flight does not hit a fabricated target"),Arrived);
    TestFalse(TEXT("Unbounded network velocity rejected"),AC2Projectile::Valid(Start,FVector(1.e9,0,0),Target));
    for(const FVector Direction:{FVector(1,0,0),FVector(0,1,1).GetSafeNormal(),FVector(0,0,-1)})
        TestTrue(TEXT("Native +Y axis points along the full flight direction"),
            AC2Projectile::Facing(Direction).RotateVector(FVector::YAxisVector).Equals(Direction,.0001));
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;FAC2FXScript Script;
    if(!TestTrue(TEXT("Portal"),Portal.Open(AC2::ResolveDatDirectory(TEXT(""))/TEXT("portal.dat"),1,Error)))return false;
    if(!TestTrue(TEXT("Native composite hit script"),Portal.Read(0x5700000c,Bytes,Error)&&Script.Decode(0x5700000c,Bytes)))return false;
    if(!Script.Effects.IsEmpty() && Script.Effects[0].Id==0x41000692)
    {
        TestEqual(TEXT("Icefire composite script period"),Script.Period,2.f);
        TestEqual(TEXT("Icefire pre-roll effect at the start of playback"),Script.EffectsBetween(0,.01f).Num(),1);
        if(TestEqual(TEXT("Both Icefire composite effects kept"),Script.Effects.Num(),2))
            TestEqual(TEXT("Authored second Icefire effect"),Script.Effects[1].Id,0x41000693u);
    }
    else
    {
        TestEqual(TEXT("Legions composite script period"),Script.Period,1.f);
        TestEqual(TEXT("All three simultaneous Legions effects kept"),Script.Effects.Num(),3);
    }
    Bytes.Pop();TestFalse(TEXT("Truncated FX script rejected"),Script.Decode(0x5700000c,Bytes));
    // Wire fields from Icefire 5700000C: pre-roll cue at -0.01, second at
    // 0.05, period two seconds. Native GetTriggers treats the first as due.
    Bytes.Reset();
    auto Word=[&](uint32 W){Bytes.Append(reinterpret_cast<const uint8*>(&W),4);};
    auto Float=[&](float F){Bytes.Append(reinterpret_cast<const uint8*>(&F),4);};
    Word(0x5700000c);Word(2);
    for(int32 I=0;I<2;++I){Word(0x41000692+I);Float(I==0?-.01f:.05f);Word(0);Float(1);Float(1);Float(0);Word(1);}
    Word(0);Float(2);
    if(TestTrue(TEXT("Pre-roll script decodes"),Script.Decode(0x5700000c,Bytes))) {
        const auto Initial=Script.EffectsBetween(0,.01f),Next=Script.EffectsBetween(.01f,.05f);
        if(TestEqual(TEXT("Pre-roll cue fires on first update"),Initial.Num(),1))TestEqual(TEXT("First cue"),Initial[0].Id,0x41000692u);
        if(TestEqual(TEXT("Later cue retains its authored timing"),Next.Num(),1))TestEqual(TEXT("Second cue"),Next[0].Id,0x41000693u);
        TestTrue(TEXT("No repeated cue after it has fired"),Script.EffectsBetween(.05f,1.f).IsEmpty());
        TestTrue(TEXT("Paused clock does not duplicate a cue"),Script.EffectsBetween(0,0).IsEmpty());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LoopFXTest,"AC2.Data.LoopingAnimationEffectTiming",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LoopFXTest::RunTest(const FString&)
{
    TMap<uint32,TArray<uint8>> Records;
    auto Word=[](TArray<uint8>& B,uint32 V){for(int32 I=0;I<4;++I)B.Add(uint8(V>>(I*8)));};
    auto Float=[&](TArray<uint8>& B,float V){uint32 Bits;FMemory::Memcpy(&Bits,&V,4);Word(B,Bits);};
    auto Clip=[&](uint32 Id,const TArray<FAC2Animation::FEffect>& Effects){
        auto& B=Records.Add(Id);Word(B,Id);Word(B,0x40000001);Float(B,1);Word(B,1);
        for(int32 I=0;I<4;++I)Word(B,0); // One bone, empty scale/position/rotation/weight tracks.
        Word(B,1);Word(B,Effects.Num());
        for(const auto& E:Effects){Word(B,E.Id);Float(B,E.Phase);Word(B,E.Stop);Float(B,E.Probability);
            Float(B,E.Intensity);Float(B,E.Variation);Word(B,E.Play);}
        Word(B,0);Word(B,0);Word(B,0);Float(B,0);Float(B,1);
    };
    Clip(0x05000001,{{0x81000001,.25f,1,1},{0x81000002,0,1,1},{0x81000003,1,1,1}});
    Clip(0x05000002,{{0x81000001,.75f,.5f,3}});
    auto Read=[&](uint32 Id,TArray<uint8>& B){const auto* Found=Records.Find(Id);if(!Found)return false;B=*Found;return true;};
    FAC2AnimatorInput Input;
    const auto Loop=FAC2Animator::Load(Read,0x05000001,0);
    if(!TestTrue(TEXT("Synthetic event clip decodes"),Loop.IsValid()))return false;
    FAC2AnimationTriggerState State;
    TestEqual(TEXT("Phase-zero event fires on first advance"),Loop->EffectsBetween(0,.1,Input,true,&State).Num(),1);
    TestEqual(TEXT("Stationary clock never replays FX"),Loop->EffectsBetween(.1,.1,Input,true,&State).Num(),0);
    TestEqual(TEXT("Middle cue fires once"),Loop->EffectsBetween(.1,.5,Input,true,&State).Num(),1);
    const auto Wrap=Loop->EffectsBetween(.9,1.3,Input,true,&State);
    TestEqual(TEXT("Wrap includes old end, new start and next cue"),Wrap.Num(),3);
    if(Wrap.Num()==3){TestEqual(TEXT("Old cycle finishes first"),Wrap[0].Id,0x81000003u);
        TestEqual(TEXT("New phase-zero cue"),Wrap[1].Id,0x81000002u);TestEqual(TEXT("Next stride cue"),Wrap[2].Id,0x81000001u);}
    TestEqual(TEXT("Zero cue is not repeated after exact boundary"),Loop->EffectsBetween(1,1.1,Input,true,&State).Num(),0);
    TestEqual(TEXT("Actions do not loop after completion"),Loop->EffectsBetween(1.1,2,Input).Num(),0);
    TestEqual(TEXT("Rewinding has no historical triggers"),Loop->EffectsBetween(.9,.2,Input,true).Num(),0);
    auto& Cube=Records.Add(0x05000003);Word(Cube,0x05000003);Word(Cube,0x40000002);
    Word(Cube,1);Word(Cube,0x40000001);Word(Cube,2);Float(Cube,0);Float(Cube,10);
    Word(Cube,2);Word(Cube,0x05000001);Word(Cube,0x05000002);
    const auto Blend=FAC2Animator::Load(Read,0x05000003,0);
    if(!TestTrue(TEXT("Walk/run blend graph decodes"),Blend.IsValid()))return false;
    Input.Forward=5;State={};
    const auto Events=Blend->EffectsBetween(0,.51,Input,true,&State);
    const auto* Step=Events.FindByPredicate([](const auto& E){return E.Id==0x81000001;});
    if(TestNotNull(TEXT("Weighted footstep is reached at halfway"),Step)){
        TestEqual(TEXT("Weighted timing"),Step->Phase,.5f);TestEqual(TEXT("Weighted strength"),Step->Intensity,2.f);
        TestEqual(TEXT("Weighted probability"),Step->Probability,.75f);}
    Input.Forward=7.5f;
    TestEqual(TEXT("Changing gait cannot replay a reached cue"),Blend->EffectsBetween(.51,.7,Input,true,&State).Num(),0);
    TestTrue(TEXT("Same cue may play on the next stride"),Blend->EffectsBetween(.9,1.7,Input,true,&State).Num()>0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CombatPoseTest,"AC2.Data.CombatLayersDeathAndParticles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2CombatPoseTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        FAC2AssetLibrary Model;FString Error;
        if(TestTrue(TEXT("Native human for combat pose checks"),Model.Open(AC2::ResolveDatDirectory(TEXT("")),Error) &&
            Model.DrawModel(0x1f000023,Actor,Actor->Body)))
        {
            auto Snapshot=[&](){TArray<FVector> Points;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
                for(auto* M:Meshes)for(int32 S=0;S<M->GetNumSections();++S)if(auto* Section=M->GetProcMeshSection(S))
                    for(const auto& V:Section->ProcVertexBuffer)Points.Add(M->GetComponentTransform().TransformPosition(V.Position));
                return Points;};
            auto Same=[](const TArray<FVector>& A,const TArray<FVector>& B){if(A.Num()!=B.Num())return false;
                for(int32 I=0;I<A.Num();++I)if(!A[I].Equals(B[I],.01))return false;return true;};
            const auto Rest=Snapshot();
            float MovementTime=0;
            const auto BeforeMoveFX=Model.Stats().AnimationEffectTriggers;
            for(int32 I=0;I<80;++I){MovementTime+=.05f;Model.Animate(MovementTime,400);}
            TestTrue(TEXT("Running dispatches native gait effects on a rendered multipart model"),
                Model.Stats().AnimationEffectTriggers>BeforeMoveFX);
            const auto AfterMoveFX=Model.Stats().AnimationEffectTriggers;
            Model.Animate(MovementTime,400);
            TestEqual(TEXT("No duplicate gait effects on a stationary animation clock"),Model.Stats().AnimationEffectTriggers,AfterMoveFX);
            TestTrue(TEXT("Native attack begins"),Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee));
            const uint64 AttackInstance=Model.BehaviorInstance(AC2Motion::Attack);
            TestTrue(TEXT("Playing actions have a nonzero identity"),AttackInstance!=0);
            TestTrue(TEXT("Native hit begins on separate layer"),Model.PlayBehavior(AC2Motion::Hit));
            TestTrue(TEXT("Hit did not cancel the attack"),Model.BehaviorDuration(AC2Motion::Attack)>0 && Model.BehaviorDuration(AC2Motion::Hit)>0);
            TestEqual(TEXT("Separate hit layer preserves attack identity"),Model.BehaviorInstance(AC2Motion::Attack),AttackInstance);
            float Time=MovementTime+.1f;Model.Animate(Time,0);
            TestFalse(TEXT("Combat changes actual rendered vertices"),Same(Rest,Snapshot()));
            for(int32 I=0;I<4;++I){Time+=.1f;Model.Animate(Time,0);}
            const auto BeforeStop=Snapshot();
            Model.StopBehavior(AC2Motion::Attack);Model.Animate(Time,0);
            TestEqual(TEXT("Stopped action loses its identity"),Model.BehaviorInstance(AC2Motion::Attack),uint64(0));
            TestTrue(TEXT("Stopping an attack retains the outgoing pose at blend start"),Same(BeforeStop,Snapshot()));
            Time+=.2f;Model.Animate(Time,0);
            TestFalse(TEXT("Stopped attack blends back into the base pose"),Same(BeforeStop,Snapshot()));
            TestTrue(TEXT("Combat attachments stay on their animated sockets"),Model.AuditAttachments(Error));
            Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee);const uint64 Replaced=Model.BehaviorInstance(AC2Motion::Attack);
            Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee);
            TestTrue(TEXT("Replaying the same behavior creates a distinct action"),Model.BehaviorInstance(AC2Motion::Attack)!=Replaced);
            TestTrue(TEXT("Death resolves in native dead mode"),Model.PlayBehavior(AC2Motion::Death,AC2Motion::Dead));
            TestEqual(TEXT("Death cancels attacks"),Model.BehaviorDuration(AC2Motion::Attack),0.f);
            TestEqual(TEXT("Death releases attack identity"),Model.BehaviorInstance(AC2Motion::Attack),uint64(0));
            TestTrue(TEXT("Ten-second native death/hold is retained"),Model.BehaviorDuration(AC2Motion::Death)>=10.f);
            for(int32 I=0;I<110;++I){Time+=.1f;Model.Animate(Time,0);}
            const auto Corpse=Snapshot();
            TestFalse(TEXT("Death pose differs from standing"),Same(Rest,Corpse));
            TestFalse(TEXT("A late damage packet cannot interrupt death"),Model.PlayBehavior(AC2Motion::Hit));
            TestFalse(TEXT("A late floor collision cannot interrupt death"),Model.PlayBehavior(AC2Motion::Land));
            Model.PlayBehavior(AC2Motion::Death,AC2Motion::Dead);
            Time+=.1f;Model.Animate(Time,0);
            TestTrue(TEXT("Duplicate death does not restart final pose"),Same(Corpse,Snapshot()));
            Model.SetAnimationState(AC2Motion::Ground,AC2Motion::Peace);
            TestEqual(TEXT("Resurrection releases the held death"),Model.BehaviorDuration(AC2Motion::Death),0.f);
            TestTrue(TEXT("Jump resolves a native impulse"),Model.PlayBehavior(AC2Motion::Jump));
            TestTrue(TEXT("Jump impulse occurs within its animation"),Model.JumpDelay()>=0 && Model.JumpDelay()<Model.BehaviorDuration(AC2Motion::Jump));
            Model.StopBehavior();
            Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee,1,true);
            Model.SetAnimationState(AC2Motion::Water,AC2Locomotion::AnimationMode(AC2Motion::Water,AC2Motion::Melee));
            TestEqual(TEXT("Entering water releases held ground attacks"),Model.BehaviorDuration(AC2Motion::Attack),0.f);
            for(int32 I=0;I<20;++I){Time+=.1f;Model.Animate(Time,150);}
            const auto Swim=Snapshot();
            for(int32 I=0;I<3;++I){Time+=.1f;Model.Animate(Time,150);}
            TestFalse(TEXT("Swimming changes actual rendered vertices"),Same(Swim,Snapshot()));
            TestFalse(TEXT("Swim pose differs from standing"),Same(Rest,Swim));
            Model.SetAnimationState(AC2Motion::Ground,AC2Motion::Peace);
            TestTrue(TEXT("Native Mind Lash cast animation"),Model.PlayBehavior(0x410000b9,0x41000011));
            TestTrue(TEXT("Shared native Cast_Attack resolves hand emitters"),Model.PlayEffect(0x810001cd));
            int32 Peak=0;for(int32 I=0;I<80;++I){Time+=.05f;Model.Animate(Time,0);Peak=FMath::Max(Peak,Model.EffectParticleCount());}
            TestTrue(TEXT("Cast cue produces actual native particles"),Peak>0);
            TestTrue(TEXT("Mind Lash impact resolves native FX bank"),Model.PlayEffect(0x41000248));
            Peak=0;for(int32 I=0;I<20;++I){Time+=.05f;Model.Animate(Time,0);Peak=FMath::Max(Peak,Model.EffectParticleCount());}
            TestTrue(TEXT("Impact produces actual native particles"),Peak>0);
            FAC2AssetLibrary::SetEffectChannel(0x40000001,2);
            TestFalse(TEXT("Native None blood preference excludes all death blood and comic variants"),Model.PlayEffect(0x810002cf));
            FAC2AssetLibrary::SetEffectChannel(0x40000001,4);
            TestTrue(TEXT("Native Normal blood channel resolves the death effect"),Model.PlayEffect(0x810002cf));
            Model.StopEffect(0x810002cf);
        }
        {
            auto* Emerging=World->SpawnActor<AAC2Portrait>();
            FAC2AssetLibrary Arrival;
            if(TestTrue(TEXT("Arrival avatar has a native effect bank"),Arrival.Open(AC2::ResolveDatDirectory(TEXT("")),Error) &&
                Arrival.DrawModel(0x1f000023,Emerging,Emerging->Body)))
            {
                TestTrue(TEXT("Enter_World resolves the native avatar effect"),Arrival.PlayEffect(0x41000010,1.f));
                int32 Peak=0;
                for(int32 I=0;I<60;++I){Arrival.Animate(I*.05f,0);Peak=FMath::Max(Peak,Arrival.EffectParticleCount());}
                TestTrue(TEXT("Arrival produces visible avatar particles"),Peak>0);
                Arrival.StopEffect(0x41000010);
                for(int32 I=60;I<260;++I)Arrival.Animate(I*.05f,0);
                TestEqual(TEXT("The stopped arrival effect drains"),Arrival.EffectParticleCount(),0);
            }
            Emerging->Destroy();
        }
        // Ordinary portal travel plays a different one-shot on each object.
        // GenericVault's physical entity resolves its portal visual in either DAT.
        for(const auto& Cue : {TPair<uint32,uint32>(0x1f000023,0x410004fa),
                               TPair<uint32,uint32>(0x47000297,0x41000044)})
        {
            auto* Source=World->SpawnActor<AAC2Portrait>();
            FAC2AssetLibrary Effect;
            if(TestTrue(TEXT("Portal departure object loads"),Effect.Open(AC2::ResolveDatDirectory(TEXT("")),Error) &&
                Effect.DrawModel(Cue.Key,Source,Source->Body)))
            {
                Effect.Animate(0,0);
                const int32 Before=Effect.EffectParticleCount();
                TestTrue(FString::Printf(TEXT("Departure effect %08X resolves on its own object"),Cue.Value),Effect.PlayEffect(Cue.Value));
                int32 Peak=Before;
                for(int32 Frame=1;Frame<=35;++Frame)
                {
                    Effect.Animate(Frame*.04f,0);
                    Peak=FMath::Max(Peak,Effect.EffectParticleCount());
                }
                TestTrue(TEXT("Departure cue emits particles before the forced position"),Peak>Before);
            }
            Source->Destroy();
        }
        // These are the actual static projectile entities referenced by racial skill scripts.
        for(uint32 Visual:{0x1f000066u,0x1f000392u,0x1f000393u,0x1f0003ceu,0x1f0003cfu,0x1f0003d0u})
        {
            auto* Projectile=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Missile;
            TestTrue(FString::Printf(TEXT("Native projectile visual %08X"),Visual),Missile.Open(AC2::ResolveDatDirectory(TEXT("")),Error)&&Missile.DrawModel(Visual,Projectile,Projectile->Body));
            int32 Peak=0;for(int32 I=0;I<20;++I){Projectile->SetActorLocation(FVector(I*100,0,0));Missile.Animate(I*.05f,0);Peak=FMath::Max(Peak,Missile.EffectParticleCount());}
            if(Visual>=0x1f0003ce)
            {
                TestTrue(TEXT("Magic missile has startup effects"),Missile.HasStartupEffects());
                AddInfo(FString::Printf(TEXT("Projectile %08X: startup=%d peak=%d"),Visual,Missile.HasStartupEffects(),Peak));
                TestTrue(TEXT("Magic missile startup creates native trail particles"),Peak>0);
            }
            Projectile->Destroy();
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2DamageAppearanceTest,"AC2.Data.ProgressiveDamageAppearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2DamageAppearanceTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    FAC2AssetLibrary::SetEffectChannel(0x40000001,4);FAC2AssetLibrary::SetEffectChannel(0x40000007,2);
    FAC2CharacterCatalog Catalog;FString Error;
    TestTrue(TEXT("Appearance catalog"),Catalog.Open(AC2::ResolveDatDirectory(TEXT("")),Error));
    struct FCase {uint32 Visual;FAC2Appearance Appearance;bool Wounds=true;};
    TArray<FCase> Cases={{0x1f000023,{}},{0x1f000036,{}},{0x1f0003d1,{},false}};
    for(const auto& Avatar:Catalog.Avatars)
    {
        auto Choices=Avatar.Defaults();
        for(const auto& Choice:Avatar.Choices)if(!Choice.Value.IsEmpty())Choices.Add(Choice.Key,Choice.Value.Num()/2);
        Cases.Add({Avatar.Physics,Avatar.Resolve(Choices)});
    }
    for(const auto& Case:Cases)
    {
        const uint32 Visual=Case.Visual;
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        {
            FAC2AssetLibrary Model;
            if(!TestTrue(FString::Printf(TEXT("Damage visual %08X"),Visual),Model.Open(AC2::ResolveDatDirectory(TEXT("")),Error)&&Model.DrawModel(Visual,Actor,Actor->Body,Case.Appearance)))continue;
            auto Textures=[&](){TArray<UTexture*> Result;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
                for(auto* Mesh:Meshes)for(int32 S=0;S<Mesh->GetNumSections();++S)
                    if(auto* M=Mesh->GetMaterial(S))for(int32 I=0;I<4;++I){UTexture* T=nullptr;M->GetTextureParameterValue(*FString::Printf(TEXT("AC2Stage%dTexture"),I),T);Result.Add(T);}
                return Result;};
            const auto Clean=Textures();const uint32 CleanHash=Model.DamageAppearanceFingerprint();
            const uint32 BaseAppearance=Model.AppearanceFingerprint();
            Model.SetHealth(61,100);
            TestTrue(TEXT("No wound texture below native 40% damage threshold"),Textures()==Clean);
            TArray<UTexture*> Previous=Clean;uint32 PreviousHash=CleanHash;
            // DBFXTable 1E000057 -> appearance key 2: 0.40/0.55/0.70/0.85/1.0.
            for(float Health:{60.f,45.f,30.f,15.f,0.f})
            {
                Model.SetHealth(Health,100);
                if(Case.Wounds)
                {
                    TestNotEqual(TEXT("Each native wound stage changes the rendered appearance"),Model.DamageAppearanceFingerprint(),PreviousHash);
                    TestFalse(FString::Printf(TEXT("%08X health %.0f changes bound DAT textures"),Visual,Health),Textures()==Previous);
                }
                else
                {
                    // Idol 3100047D has material type 2: the inherited native
                    // wound atoms target skin/garment types, never this material.
                    TestTrue(TEXT("No invented wounds on unsupported native material types"),Textures()==Clean);
                    TestEqual(TEXT("Unsupported material stays unchanged"),Model.DamageAppearanceFingerprint(),CleanHash);
                }
                TestEqual(TEXT("Wounds do not change base skin/dye/armor identity"),Model.AppearanceFingerprint(),BaseAppearance);
                Previous=Textures();PreviousHash=Model.DamageAppearanceFingerprint();
            }
            Model.SetHealth(100,100);TestTrue(TEXT("Healing restores exact base textures"),Textures()==Clean);
            Model.SetHealth(45,100);const auto Injured=Textures();
            Model.SetHealth(90,200);TestTrue(TEXT("Wounds use health proportion, not absolute hit points"),Textures()==Injured);
            Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee);
            const float Attack=Model.BehaviorDuration(AC2Motion::Attack);
            Model.SetHealth(60,200);TestEqual(TEXT("Wound updates preserve running animation"),Model.BehaviorDuration(AC2Motion::Attack),Attack);
            TestTrue(TEXT("Wound updates preserve rig attachments"),Model.AuditAttachments(Error));
            Model.PlayEffect(0x41000002,.55f);TestTrue(TEXT("Native network FX scalar matches health-derived stage"),Textures()==Injured);
            FAC2AssetLibrary::SetEffectChannel(0x40000001,2);Model.Animate(.1,0);
            TestTrue(TEXT("None blood preference removes persistent wounds"),Textures()==Clean);
            FAC2AssetLibrary::SetEffectChannel(0x40000001,4);Model.Animate(.2,0);
            TestTrue(TEXT("Restoring blood preference restores current wounds"),Textures()==Injured);
            FAC2AssetLibrary::SetEffectChannel(0x40000007,1);Model.Animate(.3,0);
            if(Case.Wounds)TestFalse(TEXT("Native green-blood option changes wound textures"),Textures()==Injured);
            FAC2AssetLibrary::SetEffectChannel(0x40000007,2);Model.Animate(.4,0);
            TestTrue(TEXT("Returning to red blood restores current wound stage"),Textures()==Injured);
            Model.StopEffect(0x41000002);TestTrue(TEXT("StopFX removes wounds without changing skin/dyes"),Textures()==Clean);
            Model.SetHealth(0,100);Model.SetHealth(100,100);TestTrue(TEXT("Respawn restores clean model"),Textures()==Clean);
        }
        Actor->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WorldPropTest,"AC2.Data.WorldPropsAndInventoryIcons",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2WorldPropTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    {
        FAC2AssetLibrary Assets;FString Error;
        TestTrue(TEXT("DAT assets"),Assets.Open(AC2::ResolveDatDirectory(TEXT("")),Error));
        for(uint32 Visual:{0x1f00059cu,0x1f0005a4u,0x1f0005a8u,0x1f0002a6u,0x1f0004f8u,0x1f000063u,0x1f00058eu,0x1f00058cu,0x1f00058du,0x1f0002beu})
        {
            const auto Icons=Assets.IconLayers(Visual);
            TestTrue(FString::Printf(TEXT("Native armor/quest/weapon icon %08X"),Visual),
                !Icons.IsEmpty() && Icons.ContainsByPredicate([](const auto& I){return I.Image && I.Image!=0x4100034b;}));
            for(const auto& I:Icons){TestNotNull(TEXT("Native icon texture"),Assets.Texture(I.Image));AddInfo(FString::Printf(TEXT("Icon %08X layer=%u image=%08X tint=%08X"),Visual,I.Layer,I.Image,I.Color));}
        }
        FAC2CharacterCatalog Catalog;
        TestTrue(TEXT("Native item catalog"),Catalog.Open(AC2::ResolveDatDirectory(TEXT("")),Error));
        Catalog.LoadNetworkItem(AC2::ResolveDatDirectory(TEXT("")),0x47001582);
        const auto* Glyph=Catalog.Items.Find(0x47001582);
        TestTrue(TEXT("Quest item without layered artwork has an entity icon"),Glyph && Glyph->Icon && Assets.Texture(Glyph->Icon));
        if(Glyph){TestEqual(TEXT("Menhir native icon"),Glyph->Icon,0x41003418u);TestEqual(TEXT("Localized Menhir name"),Glyph->Name,FString(TEXT("Menhir Glyph")));}
        Catalog.LoadNetworkItem(AC2::ResolveDatDirectory(TEXT("")),0x47001745);
        const auto* Potion=Catalog.Items.Find(0x47001745);
        TestTrue(TEXT("Potion native fallback icon"),Potion && Potion->Icon==0x41006f81 && Assets.Texture(Potion->Icon));
        auto* Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);Root->RegisterComponent();
        TestTrue(TEXT("Fountain visual"),Assets.DrawModel(0x1f00002d,Actor,Root));
        TestTrue(TEXT("Fountain solid surfaces cooked"),Assets.EnableWorldObjectCollision()>0);
        FHitResult Hit;
        TestTrue(TEXT("Pawn cannot pass through fountain base"),World->LineTraceSingleByChannel(Hit,FVector(-1000,0,40),FVector(1000,0,40),ECC_Pawn));
        TestTrue(TEXT("Walking capsule is blocked by fountain"),World->SweepSingleByChannel(Hit,FVector(-1000,0,100),FVector(1000,0,100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88)));
        Actor->Destroy();
        FAC2AssetLibrary Shard;Shard.Open(AC2::ResolveDatDirectory(TEXT("")),Error);
        Actor=World->SpawnActor<AActor>();Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        const auto Vault=AC2VaultFixture::Resolve(AC2::ResolveDatDirectory(TEXT("")),Catalog);
        TestTrue(TEXT("Vault shard native visual"),Vault.Shard && Shard.DrawModel(Vault.Shard,Actor,Root));
        bool HasCrystalLayer=false;
        TInlineComponentArray<UProceduralMeshComponent*> CrystalMeshes(Actor);
        for(auto* Mesh:CrystalMeshes) for(int32 S=0;S<Mesh->GetNumSections();++S)
            if(auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(S)))
                if(Material->K2_GetVectorParameterValue(TEXT("AC2Stage1Color")).R==8.f)
                {
                    HasCrystalLayer=true;
                    TestNotNull(TEXT("Crystal's second native texture is bound"),Material->K2_GetTextureParameterValue(TEXT("AC2Stage1Texture")));
                    TestEqual(TEXT("Crystal's independently transformed stage samples native UV0"),Material->K2_GetVectorParameterValue(TEXT("AC2Stage1UV")).R,0.f);
                    TestEqual(TEXT("High-detail final diffuse stage is selected"),Material->K2_GetVectorParameterValue(TEXT("AC2Stage2Color")).R,5.f);
                }
        TestTrue(TEXT("Crystal uses native signed-add layer"),HasCrystalLayer);
        Actor->Destroy();
        FAC2AssetLibrary NPC;NPC.Open(AC2::ResolveDatDirectory(TEXT("")),Error);
        Actor=World->SpawnActor<AActor>();Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        TestTrue(TEXT("Shelter Lieutenant native visual"),NPC.DrawModel(0x1f000d75,Actor,Root));
        const auto Feet=NPC.ModelBounds(0x81000001);
        AddInfo(FString::Printf(TEXT("Lieutenant feet=%s gravity=%d hover=%g"),*Feet.ToString(),NPC.MotionData().Values(AC2Motion::Ground).Gravity,NPC.MotionData().Values(AC2Motion::Ground).Hover));
        TestTrue(TEXT("Lieutenant has a native foot plane"),Feet.IsValid!=0);
        TestEqual(TEXT("Animated NPC does not cook moving triangle walls"),NPC.EnableWorldObjectCollision(),0);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
