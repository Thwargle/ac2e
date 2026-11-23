#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Portrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2AvatarSequenceTest,"AC2.Data.NativeAvatarSelectionSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2AvatarSequenceTest::RunTest(const FString&)
{
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));
    FAC2CharacterCatalog Catalog;FString Error;
    if(!TestTrue(TEXT("Native avatar catalog"),Catalog.Open(Directory,Error))){AddError(Error);return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Pose=[](AActor* Actor){
        uint32 Hash=0;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
        for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)
            if(const auto* Section=Mesh->GetProcMeshSection(I))for(const auto& V:Section->ProcVertexBuffer)
                Hash=HashCombine(Hash,GetTypeHash(V.Position));
        return Hash;
    };
    for(const auto& Avatar:Catalog.Avatars)
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();
        auto* ControlActor=World->SpawnActor<AAC2Portrait>();
        FAC2AssetLibrary Assets,Control;
        const FString Label=FString::Printf(TEXT("%u/%u"),Avatar.Race,Avatar.Sex);
        if(TestTrue(Label+TEXT(" model loaded"),Assets.Open(Directory,Error) && Assets.DrawModel(Avatar.Physics,Actor,Actor->Body,Avatar.Resolve(Avatar.Defaults()))))
        {
            if(!TestTrue(Label+TEXT(" inactive control loaded"),Control.Open(Directory,Error) &&
                Control.DrawModel(Avatar.Physics,ControlActor,ControlActor->Body,Avatar.Resolve(Avatar.Defaults()))))continue;
            Control.SetAnimationState(AC2Motion::Ground,AC2Motion::AvatarInactive);
            float Time=0;
            auto Advance=[&](float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I){Time+=1.f/60;Assets.Animate(Time,0);Control.Animate(Time,0);}};
            Assets.SetAnimationState(AC2Motion::Ground,AC2Motion::AvatarInactive);
            Advance(.5f);
            TestEqual(Label+TEXT(" inactive controls agree"),Pose(Actor),Pose(ControlActor));
            Assets.SetAnimationState(AC2Motion::Ground,AC2Motion::AvatarActive);
            Advance(.5f);TestNotEqual(Label+TEXT(" hover changes pose at the same clock time"),Pose(Actor),Pose(ControlActor));
            TestTrue(Label+TEXT(" click plays native selection"),Assets.PlayBehavior(AC2Motion::AvatarSelect));
            const float Duration=Assets.BehaviorDuration(AC2Motion::AvatarSelect);
            TestTrue(Label+TEXT(" authored finite duration"),Duration>.1f && Duration<15);
            const uint32 Before=Pose(Actor);
            Advance(Duration*.5f);
            TestTrue(Label+TEXT(" still playing halfway"),Assets.BehaviorDuration(AC2Motion::AvatarSelect)>0);
            TestNotEqual(Label+TEXT(" selected animation deforms model"),Pose(Actor),Before);
            Advance(Duration*.5f+.1f);
            TestEqual(Label+TEXT(" completion releases transition"),Assets.BehaviorDuration(AC2Motion::AvatarSelect),0.f);
            AddInfo(FString::Printf(TEXT("Avatar %s selection duration %.3fs"),*Label,Duration));
            Assets.SetAnimationState(AC2Motion::Ground,AC2Motion::Peace);
            for (const uint32 Action : {AC2Motion::Pickup, AC2Motion::Putdown})
            {
                if (!TestTrue(Label+TEXT(" inventory action starts"), Assets.PlayBehavior(Action,AC2Motion::Peace))) continue;
                const float Seconds = Assets.BehaviorDuration(Action);
                if (!TestTrue(Label+TEXT(" inventory action has a finite duration"), Seconds>0 && Seconds<20)) continue;
                Advance(Seconds+.1f);
                TestEqual(Label+TEXT(" inventory action completes"), Assets.BehaviorDuration(Action),0.f);
            }
        }
        Actor->Destroy();ControlActor->Destroy();
    }
    auto* Lineup=World->SpawnActor<AAC2Portrait>();
    FAC2CharacterRandom Random;
    TMap<uint32,FAC2CharacterDraft> Drafts;
    for(const auto& Avatar:Catalog.Avatars)
    {
        Drafts.Add(Avatar.Race|Avatar.Sex,FAC2CharacterDraft::Create(Avatar,Random));
    }
    if(TestTrue(TEXT("Full native lineup loads"),Lineup->ShowLineup(Directory,Catalog,Drafts)))
    {
        const int32 Empyrean=Catalog.Avatars.IndexOfByPredicate([](const auto& A){return A.Race==8 && A.Sex==8192;});
        TestTrue(TEXT("Empyrean click begins selection"),Lineup->BeginAvatarSelection(Empyrean));
        TestFalse(TEXT("Repeated click cannot restart selected animation"),Lineup->BeginAvatarSelection(Empyrean));
        TestFalse(TEXT("Selection is pending before native animation finishes"),Lineup->AvatarSelectionComplete());
        for(int32 I=0;I<300;++I)
        {
            Lineup->Tick(1.f/60);
        }
        TestTrue(TEXT("Lineup completion releases customization"),Lineup->AvatarSelectionComplete());
    }
    Lineup->Destroy();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
