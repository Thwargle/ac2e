#include "AC2MotionData.h"
#include "AC2LoginUI.h"
#include "AC2Projectile.h"
#include "AC2DatArchive.h"
#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Portrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
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
    TestTrue(TEXT("Idol behavior table"),Portal.Read(0x44000018,Bytes,Error)&&Behavior.Decode(0x44000018,Bytes));
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
    FAC2Animation Clip;
    TestTrue(TEXT("Native AnimInfo decoded"),Portal.Read(0x05000006,Bytes,Error)&&Clip.Decode(0x05000006,Bytes));
    TestEqual(TEXT("Native effect count"),Clip.Effects.Num(),1);
    if(Clip.Effects.Num()==1){TestEqual(TEXT("Native effect key"),Clip.Effects[0].Id,0x81000057u);TestTrue(TEXT("Timed effect"),Clip.Effects[0].Phase>0 && Clip.Effects[0].Phase<1);}
    Bytes.Pop();TestFalse(TEXT("Truncated AnimInfo rejected"),FAC2Animation().Decode(0x05000006,Bytes));
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
    TestTrue(TEXT("Native composite hit script"),Portal.Read(0x5700000c,Bytes,Error)&&Script.Decode(0x5700000c,Bytes));
    TestEqual(TEXT("Native script period"),Script.Period,1.f);TestEqual(TEXT("All three simultaneous effects kept"),Script.Effects.Num(),3);
    Bytes.Pop();TestFalse(TEXT("Truncated FX script rejected"),Script.Decode(0x5700000c,Bytes));
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
            TestTrue(TEXT("Native attack begins"),Model.PlayBehavior(AC2Motion::Attack,AC2Motion::Melee));
            TestTrue(TEXT("Native hit begins on separate layer"),Model.PlayBehavior(AC2Motion::Hit));
            TestTrue(TEXT("Hit did not cancel the attack"),Model.BehaviorDuration(AC2Motion::Attack)>0 && Model.BehaviorDuration(AC2Motion::Hit)>0);
            float Time=.1f;Model.Animate(Time,0);
            TestFalse(TEXT("Combat changes actual rendered vertices"),Same(Rest,Snapshot()));
            for(int32 I=0;I<4;++I){Time+=.1f;Model.Animate(Time,0);}
            const auto BeforeStop=Snapshot();
            Model.StopBehavior(AC2Motion::Attack);Model.Animate(Time,0);
            TestTrue(TEXT("Stopping an attack retains the outgoing pose at blend start"),Same(BeforeStop,Snapshot()));
            Time+=.2f;Model.Animate(Time,0);
            TestFalse(TEXT("Stopped attack blends back into the base pose"),Same(BeforeStop,Snapshot()));
            TestTrue(TEXT("Combat attachments stay on their animated sockets"),Model.AuditAttachments(Error));
            TestTrue(TEXT("Death resolves in native dead mode"),Model.PlayBehavior(AC2Motion::Death,AC2Motion::Dead));
            TestEqual(TEXT("Death cancels attacks"),Model.BehaviorDuration(AC2Motion::Attack),0.f);
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
            TestTrue(TEXT("Native Mind Lash cast animation"),Model.PlayBehavior(0x410000b9,0x41000011));
            TestTrue(TEXT("Shared native Cast_Attack resolves hand emitters"),Model.PlayEffect(0x810001cd));
            int32 Peak=0;for(int32 I=0;I<80;++I){Time+=.05f;Model.Animate(Time,0);Peak=FMath::Max(Peak,Model.EffectParticleCount());}
            TestTrue(TEXT("Cast cue produces actual native particles"),Peak>0);
            TestTrue(TEXT("Mind Lash impact resolves native FX bank"),Model.PlayEffect(0x41000248));
            Peak=0;for(int32 I=0;I<20;++I){Time+=.05f;Model.Animate(Time,0);Peak=FMath::Max(Peak,Model.EffectParticleCount());}
            TestTrue(TEXT("Impact produces actual native particles"),Peak>0);
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
        for(uint32 Visual:{0x1f00059cu,0x1f0005a4u,0x1f0005a8u,0x1f0002a6u,0x1f0004f8u,0x1f000063u,0x1f00058eu,0x1f00058cu,0x1f00058du})
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
        TestTrue(TEXT("Vault shard native visual"),Shard.DrawModel(0x1f0012be,Actor,Root));
        bool HasCrystalLayer=false;
        TInlineComponentArray<UProceduralMeshComponent*> CrystalMeshes(Actor);
        for(auto* Mesh:CrystalMeshes) for(int32 S=0;S<Mesh->GetNumSections();++S)
            if(auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(S)))
                if(Material->K2_GetScalarParameterValue(TEXT("AC2DetailOperation"))==8.f)
                {
                    HasCrystalLayer=true;
                    TestNotNull(TEXT("Crystal's second native texture is bound"),Material->K2_GetTextureParameterValue(TEXT("AC2Detail")));
                    TestEqual(TEXT("Crystal's independently transformed stage samples native UV0"),Material->K2_GetScalarParameterValue(TEXT("AC2DetailChannel")),0.f);
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
