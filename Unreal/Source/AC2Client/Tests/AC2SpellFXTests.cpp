#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2DatArchive.h"
#include "AC2Portrait.h"
#include "AC2RadarData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Components/PointLightComponent.h"
#include "AC2EffectLight.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SpellFXTest,"AC2.Data.NamedSpellParticles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SpellFXTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));FString Error;
    {
        FAC2EffectLight Envelope;Envelope.Life=2;Envelope.Fade=.25f;
        TestEqual(TEXT("Light fades in"),Envelope.Envelope(.125f),.5f);
        TestEqual(TEXT("Light fades out"),Envelope.Envelope(1.875f),.5f);
        TestEqual(TEXT("Light expires"),Envelope.Envelope(2),0.f);
        Envelope.Life=0;TestEqual(TEXT("Zero lifetime remains active"),Envelope.Envelope(1000),1.f);
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        {
            FAC2AssetLibrary Model;
            if(TestTrue(TEXT("Native light recipient"),Model.Open(Directory,Error)&&Model.DrawModel(0x1f000026,Actor,Actor->Body)))
            {
                TestTrue(TEXT("Authored light and particle effect resolves"),Model.PlayEffect(0x8100026c));
                TInlineComponentArray<UPointLightComponent*> Lights(Actor);
                if(TestEqual(TEXT("Authored point light is created"),Lights.Num(),1))
                {
                    Model.Animate(.125f,0);
                    TestTrue(TEXT("Native light intensity and fade"),FMath::IsNearlyEqual(Lights[0]->Intensity,1.4f,.002f));
                    TestEqual(TEXT("Native range is converted to centimetres"),Lights[0]->AttenuationRadius,300.f);
                    const FVector Before=Lights[0]->GetComponentLocation();Actor->AddActorWorldOffset(FVector(100,0,0));
                    TestTrue(TEXT("Light follows its effect source"),Lights[0]->GetComponentLocation().Equals(Before+FVector(100,0,0),.01));
                    for(int32 I=1;I<=22;++I)Model.Animate(.125f+I*.1f,0);
                    TestFalse(TEXT("Expired light component is removed"),IsValid(Lights[0])&&!Lights[0]->IsBeingDestroyed());
                }
                TestTrue(TEXT("Light-only FX is retained"),Model.PlayEffect(0x410005fb));
                Model.Animate(2.5f,0);Model.StopEffect(0x410005fb);
                TInlineComponentArray<UPointLightComponent*> Remaining(Actor);
                TestEqual(TEXT("Stop removes persistent light"),Remaining.Num(),0);
            }
        }
        Actor->Destroy();
    }
    FAC2CharacterCatalog Catalog;
    if(TestTrue(TEXT("Native character catalog"),Catalog.Open(Directory,Error)))
    {
        // Fresh models isolate every cue: leftover cast particles cannot make an
        // empty impact or sound-only release pass this test.
        for(uint32 Race:{1u,2u,4u})for(uint32 FX:{0x81000281u,0x810001d0u,0x810001cdu,0x410004a8u,0x410004a9u,0x41000033u,0x41000248u,0x41000487u,0x41000031u})
        {
            auto* Actor=World->SpawnActor<AAC2Portrait>();
            {
                FAC2AssetLibrary Model;
                // Catalog choices use the same complete body assembly as creation.
                const auto* Avatar=Catalog.Avatar(Race,0x1000);
                if(TestNotNull(TEXT("Race avatar"),Avatar) &&
                   TestTrue(TEXT("Native spell recipient"),Model.Open(Directory,Error)&&Model.DrawModel(Avatar->Physics,Actor,Actor->Body,Avatar->Resolve(Avatar->Defaults()))))
                {
                    Model.Animate(0,0);
                    const FString Label=FString::Printf(TEXT("Race %u FX %08X"),Race,FX);
                    TestTrue(Label+TEXT(" resolves"),Model.PlayEffect(FX));
                    int32 Peak=0;
                    for(int32 I=1;I<=60;++I){Model.Animate(I*.05f,0);Peak=FMath::Max(Peak,Model.EffectParticleCount());}
                    AddInfo(FString::Printf(TEXT("%s: peak %d"),*Label,Peak));
                    TestTrue(Label+TEXT(" emits particles"),Peak>0);
                    for(int32 I=61;I<=500;++I)Model.Animate(I*.05f,0);
                    // 41000031 is the ongoing vigor-damage status FX (native
                    // 39000186/187 are unlimited), not a Lightning Bolt impact.
                    // The authoritative status lifetime supplies StopFX.
                    if(FX==0x41000031)
                    {
                        TestTrue(Label+TEXT(" persists until status removal"),Model.EffectParticleCount()>0);
                        Model.StopEffect(FX);
                        // Unshared particles finish their authored lifespans
                        // after native StopFX; the emitter no longer births.
                        for(int32 I=1;I<=200;++I)Model.Animate(25.f+I*.1f,0);
                    }
                    TestEqual(Label+TEXT(" releases particles"),Model.EffectParticleCount(),0);
                }
            }
            Actor->Destroy();
        }
    }
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Status;
        // 47000CDB is stock-only. This visual is the ongoing vigor-damage
        // status model, not a Lightning Bolt impact entity.
        if(TestTrue(TEXT("Authored vigor-damage status mesh"),Status.Open(Directory,Error)&&Status.DrawModel(0x1f00031a,Actor,Actor->Body)))
        {
            TestTrue(TEXT("Vigor-damage status behavior resolves"),Status.PlayBehavior(0x41000086));
            AddInfo(FString::Printf(TEXT("Vigor-damage status duration %.3f"),Status.BehaviorDuration(0x41000086)));
        }
        Actor->Destroy();
    }
    // Resolve the profile from its entity inheritance. The Icefire release renumbered entities
    // and models. NPCTable_PetSandFiend and its English name identify each native pair.
    FAC2RadarData Radar;
    if(TestTrue(TEXT("Open summon entity catalog"),Radar.Open(Directory,Error)))
    {
        const bool IcefireEntities=Radar.Look(0x47000531).Category==AC2Radar::Players;
        const uint32 Visual=IcefireEntities ? 0x1f000d02 : 0x1f000752;
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        {
            FAC2AssetLibrary Pet;
            if(TestTrue(TEXT("Draw native Sand Fiend"),Pet.Open(Directory,Error)&&Pet.DrawModel(Visual,Actor,Actor->Body)))
            {
                TestTrue(TEXT("Sand Fiend has visible geometry"),Pet.Stats().SceneryTriangles>0);
                TestTrue(TEXT("Sand Fiend provides native locomotion"),Pet.HasLocomotion());
                TestTrue(TEXT("Sand Fiend plays the server's melee behavior"),Pet.PlayBehavior(0x41000077,0x41000012));
                Pet.Animate(.1f,0);
                for(uint32 Impact : {0x41000490u,0x410002e8u,0x8100025au})
                {
                    TestTrue(FString::Printf(TEXT("Area impact %08X binds on a creature"),Impact),Pet.PlayEffect(Impact));
                    Pet.Animate(.2f,0);
                    Pet.StopEffect(Impact);
                }
            }
        }
        Actor->Destroy();
    }
    // Flame Bolt is a native mesh missile; Fireball carries startup particles.
    for(uint32 Visual:{0x1f000b85u,0x1f000189u})
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        {
            FAC2AssetLibrary Missile;
            if(TestTrue(TEXT("Native spell projectile"),Missile.Open(Directory,Error)&&Missile.DrawModel(Visual,Actor,Actor->Body)))
            {
                int32 Peak=0;
                for(int32 I=0;I<20;++I){Actor->SetActorLocation(FVector(I*100,0,0));Missile.Animate(I*.05f,0);Peak=FMath::Max(Peak,Missile.EffectParticleCount());}
                AddInfo(FString::Printf(TEXT("Spell projectile %08X: peak %d"),Visual,Peak));
                if(Visual==0x1f000189u)TestTrue(TEXT("Native Fireball emits trail particles"),Peak>0);
                else TestTrue(TEXT("Native Flame Bolt has visible mesh geometry"),Missile.Stats().SceneryTriangles>0);
                TestEqual(TEXT("Projectile materials resolve"),Missile.Stats().MissingMaterials,0);
            }
        }
        Actor->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
