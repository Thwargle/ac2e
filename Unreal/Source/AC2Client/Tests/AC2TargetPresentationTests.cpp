#include "AC2Assets.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TargetPresentationTest,"AC2.Data.StableFlyingTargetPresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TargetPresentationTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Mode=World->SpawnActor<AAC2GameMode>();
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    {
        FAC2Network Network(Mode);
        auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),0x1f000051); // Red Soldier Wasp
        auto Object=MakeUnique<FAC2Network::FObject>();Object->Placed=true;
        Object->Goal=FVector(100,200,300);
        Network.ApplyVisual(*Object,Visual,false);
        if(TestTrue(TEXT("Native Red Soldier Wasp loaded"),Object->Actor.IsValid() && Object->Model && Object->PresentationBounds.IsValid))
        {
            auto& Wasp=*Object;Network.Objects.Add(TEXT("wasp"),MoveTemp(Object));
            const FBox Stable=Network.ObjectPresentationBounds(TEXT("wasp"));
            Wasp.Model->SetAnimationState(AC2Motion::Ground,AC2Motion::Peace);
            Wasp.Model->Animate(0,0);
            const FBox InitialPose=Wasp.Actor->GetComponentsBoundingBox(true);
            double MaxPoseChange=0,MaxFrameChange=0;
            for(int32 Frame=1;Frame<=120;++Frame)
            {
                Wasp.Model->Animate(Frame/30.f,0);
                const FBox Posed=Wasp.Actor->GetComponentsBoundingBox(true);
                MaxPoseChange=FMath::Max(MaxPoseChange,FVector::Distance(Posed.Max,InitialPose.Max));
                const FBox Display=Network.ObjectPresentationBounds(TEXT("wasp"));
                MaxFrameChange=FMath::Max(MaxFrameChange,FMath::Max(FVector::Distance(Display.Min,Stable.Min),FVector::Distance(Display.Max,Stable.Max)));
            }
            AddInfo(FString::Printf(TEXT("Native flight changed mesh bounds %.3f cm; display bounds changed %.6f cm"),MaxPoseChange,MaxFrameChange));
            TestTrue(TEXT("Regression exercises a moving native flight pose"),MaxPoseChange>1.);
            TestTrue(TEXT("Name and reticle bounds stay fixed through flight"),MaxFrameChange<.001);
            const FVector Move(200,-50,80);Wasp.Actor->AddActorWorldOffset(Move);
            const FBox Moved=Network.ObjectPresentationBounds(TEXT("wasp"));
            TestTrue(TEXT("Actual world movement moves the frame immediately"),Moved.Min.Equals(Stable.Min+Move,.001) && Moved.Max.Equals(Stable.Max+Move,.001));
            Wasp.Actor->SetActorScale3D(FVector(2));
            TestTrue(TEXT("Instance scaling still sizes the frame"),Network.ObjectPresentationBounds(TEXT("wasp")).GetSize().Equals(Stable.GetSize()*2,.001));
            Wasp.AnimMode=AC2Motion::Dead;
            TestTrue(TEXT("Death frame follows the corpse posture"),Network.ObjectPresentationBounds(TEXT("wasp")).Equals(Wasp.Actor->GetComponentsBoundingBox(true),.001));
            Wasp.AnimMode=AC2Motion::Peace;
            Visual->SetNumberField(TEXT("did"),0x1f000023); // Appearance replacement must discard the wasp extent.
            Network.ApplyVisual(Wasp,Visual,false);
            TestTrue(TEXT("Replacement appearance refreshes cached bounds"),Wasp.PresentationBounds.IsValid && !Network.ObjectPresentationBounds(TEXT("wasp")).GetSize().Equals(Stable.GetSize(),.001));
            TestFalse(TEXT("Removed/unknown object has no stale frame"),Network.ObjectPresentationBounds(TEXT("missing")).IsValid!=0);
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
