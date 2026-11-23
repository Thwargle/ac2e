#include "AC2Assets.h"
#include "AC2WorldGrid.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
class FTreeCollisionCommand:public IAutomationLatentCommand {
    FAutomationTestBase* Test;UWorld* World=nullptr;TUniquePtr<FAC2AssetLibrary> Assets;AActor* Actor=nullptr;
    double Started=FPlatformTime::Seconds();
public:
    explicit FTreeCollisionCommand(FAutomationTestBase* T):Test(T){}
    ~FTreeCollisionCommand(){Assets.Reset();if(World){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
    bool Update() override {
        if(!World){
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
            Actor->SetActorLocation(AC2::BlockOrigin(0x8fbc));
            const auto Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
            FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2LandBlock Land;TArray<uint8> Bytes;FString Error;
            if(!Test->TestTrue(TEXT("Load reported tree block"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)&&Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)&&Portal.Read(0x36000000,Bytes,Error)&&Defs.Decode(Bytes,Error)&&Cell.Read(0x8fbcffff,Bytes,Error)&&Land.Decode(0x8fbcffff,Bytes,Error)))return true;
            Assets=MakeUnique<FAC2AssetLibrary>();
            if(!Test->TestTrue(TEXT("Open scenery assets"),Assets->Open(Dir,Error)))return true;
            if(!Test->TestTrue(TEXT("Prepare tree scenery"),Assets->PrepareBlock(Dir,Defs,Land,BuildAC2TerrainMesh(Defs,Land),Error)))return true;
        }
        const bool Uploaded=Assets->UploadScenery(Actor,Actor->GetRootComponent(),4096);
        if(!Uploaded || !Assets->CollisionReady()) {if(FPlatformTime::Seconds()-Started<90)return false;Test->AddError(TEXT("Tree collision cook timed out"));return true;}
        World->SendAllEndOfFrameUpdates();
        const FVector Center(2299950,3012590,7800);
        TArray<UProceduralMeshComponent*> Components;Actor->GetComponents(Components);
        for(auto* C:Components)if(C->Bounds.GetBox().IsInsideOrOn(Center)){
            int32 Tris=0;for(int32 I=0;I<C->GetNumSections();++I)if(auto* S=C->GetProcMeshSection(I);S&&S->bEnableCollision)Tris+=S->ProcIndexBuffer.Num()/3;
            Test->AddInfo(FString::Printf(TEXT("Reported tree bounds %s collision triangles=%d"),*C->Bounds.GetBox().ToString(),Tris));
        }
        FHitResult Hit;FCollisionQueryParams Query;Query.bTraceComplex=false;
        Test->TestTrue(TEXT("Reported tree blocks a horizontal pawn sweep"),World->SweepSingleByChannel(Hit,Center-FVector(600,0,0),Center+FVector(600,0,0),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(25,65),Query));
        Test->TestFalse(TEXT("Open space beside the trunk remains traversable"),World->SweepSingleByChannel(Hit,Center-FVector(250,300,0),Center+FVector(250,-300,0),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(25,65),Query));
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TreeCollisionTest,"AC2.Data.TreeCollision8FBC",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TreeCollisionTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FTreeCollisionCommand(this));return true;}
#endif
