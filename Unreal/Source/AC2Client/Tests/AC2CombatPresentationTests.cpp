#include "AC2Effects.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2BloodDecalTest,"AC2.Data.NativeBloodDecals",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2BloodDecalTest::RunTest(const FString&)
{
    FAC2AssetLibrary Assets;FString Error;
    TestTrue(TEXT("Open native assets"),Assets.Open(AC2::ResolveDatDirectory(TEXT("")),Error));
    FAC2Decal D;
    FAC2AssetLibrary::SetEffectChannel(0x40000001,4);
    FAC2AssetLibrary::SetEffectChannel(0x40000007,2);
    TestTrue(TEXT("Native red blood decal"),Assets.DamageDecal(D));
    TestEqual(TEXT("Native blood material"),D.Material,0x310003ddu);
    TestEqual(TEXT("Native decal lifetime"),D.Lifetime,30.f);
    TestEqual(TEXT("Not visible before creation"),FAC2Decal::Opacity(-1,30),0.f);
    TestEqual(TEXT("Native fade out"),FAC2Decal::Opacity(29.5f,30),.5f);
    TestEqual(TEXT("Expired"),FAC2Decal::Opacity(30,30),0.f);
    FAC2AssetLibrary::SetEffectChannel(0x40000007,1);
    FAC2Decal Green;TestTrue(TEXT("Native green blood variant"),Assets.DamageDecal(Green));
    TestEqual(TEXT("Green blood material"),Green.Material,0x310006ddu);
    FAC2AssetLibrary::SetEffectChannel(0x40000001,2);
    TestFalse(TEXT("No blood preference suppresses ground splatter"),Assets.DamageDecal(Green));
    FAC2AssetLibrary::SetEffectChannel(0x40000001,4);
    FAC2AssetLibrary::SetEffectChannel(0x40000007,2);

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(500,500,10));
    Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
    {
        FAC2Effects Effects;Effects.Open(World,AC2::ResolveDatDirectory(TEXT("")));
        TestTrue(TEXT("Blood projects onto collision floor"),Effects.GroundDecal(D,FTransform(FVector(0,0,80))));
        TestEqual(TEXT("One decal retained"),Effects.DecalCount(),1);
        TArray<UProceduralMeshComponent*> Meshes;Effects.RenderActor()->GetComponents(Meshes);
        int32 Projected=0;
        for(auto* Mesh:Meshes)if(const auto* Section=Mesh->GetProcMeshSection(0)){
            for(const auto& V:Section->ProcVertexBuffer){TestTrue(TEXT("Splatter rests on floor"),FMath::Abs(V.Position.Z-.15f)<.1f);++Projected;}
            TestNotNull(TEXT("Native decal material bound"),Mesh->GetMaterial(0));
        }
        TestTrue(TEXT("Projected decal has geometry"),Projected>=4);
        TestFalse(TEXT("No blood floats over missing ground"),Effects.GroundDecal(D,FTransform(FVector(5000,0,80))));
        for(int32 Frame=0;Frame<310;++Frame)World->Tick(LEVELTICK_All,.1f);
        Effects.Update(FVector(0,0,400),FVector::YAxisVector,FVector::UpVector,0,0,FVector::ZeroVector,true);
        TestEqual(TEXT("Expired ground splatter removed"),Effects.DecalCount(),0);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
