#include "AC2AcademyRepairs.h"
#include "AC2StaticCollision.h"
#include "AC2WorldStream.h"
#include "AC2WorldGrid.h"
#include "AC2Assets.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcademyPlacementRepairTest,"AC2.Data.AcademyPlacementRepairs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAcademyPlacementRepairTest::RunTest(const FString&)
{
    FVector Fire(.67,1.94,-21.9),Smoke(.68,1.96,-21.9);
    TestTrue(TEXT("Reported flame repaired"),RepairAC2AcademyPlacement(0x03fd0111,26,0x470000fc,Fire,FVector::ZeroVector,{}));
    TestTrue(TEXT("Reported smoke repaired"),RepairAC2AcademyPlacement(0x03fd0111,25,0x4700016b,Smoke,FVector::ZeroVector,{}));
    TestTrue(TEXT("Flame centered on bowl"),Fire.Equals(FVector(2.27,2.86,-21.9),.001));
    TestTrue(TEXT("Smoke preserves emitter separation"),(Smoke-Fire).Equals(FVector(.01,.02,0),.001));
    TestFalse(TEXT("Repair is not applied twice"),RepairAC2AcademyPlacement(0x03fd0111,26,0x470000fc,Fire,FVector::ZeroVector,{}));
    FVector Other(.67,1.94,-21.9);
    TestFalse(TEXT("Other cells untouched"),RepairAC2AcademyPlacement(0x03fd0112,26,0x470000fc,Other,FVector::ZeroVector,{}));
    const FVector Offset(48000,4048000,0);
    TArray<FVector> Floors={Offset+FVector(-30000,34000,-7300),Offset+FVector(-28000,34000,-7300),Offset+FVector(-29000,37000,-7300)};
    FVector Mushroom(-291.15,351.79,-69.47);
    TestTrue(TEXT("Mushroom grounded on room floor"),RepairAC2AcademyPlacement(0x03fd012a,2,0x4700058a,Mushroom,Offset,Floors));
    TestTrue(TEXT("Stem bottom touches floor"),FMath::Abs(Mushroom.Z-.0460055+73)<.0001);
    Mushroom=FVector(-291.15,351.79,-69.47);
    TestFalse(TEXT("No invented floor when geometry is unavailable"),RepairAC2AcademyPlacement(0x03fd012a,2,0x4700058a,Mushroom,Offset,{}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStaticPhysicsPayloadTest,"AC2.Data.StaticPhysicsPayload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStaticPhysicsPayloadTest::RunTest(const FString&)
{
    TArray<uint8> Data;
    auto Word=[&](uint32 V){for(int32 I=0;I<4;++I)Data.Add(uint8(V>>(8*I)));};
    auto Short=[&](uint16 V){Data.Add(uint8(V));Data.Add(uint8(V>>8));};
    Word(0);Word(1);Short(9);Short(4);for(uint16 V:{0,1,2,3})Short(V);Word(0);
    FAC2AssetReader R(Data);TArray<int32> Triangles;
    TestTrue(TEXT("Material-free physics quad decoded"),ReadAC2StaticCollision(R,4,Triangles));
    TestTrue(TEXT("Convex quad becomes two clockwise triangles"),Triangles==TArray<int32>({0,2,1,0,3,2}));
    Data.Pop();FAC2AssetReader Truncated(Data);Triangles={17};
    TestFalse(TEXT("Truncated payload rejected"),ReadAC2StaticCollision(Truncated,4,Triangles));
    TestEqual(TEXT("Failed read does not publish partial collision"),Triangles[0],17);
    Data.Add(0);FAC2AssetReader InvalidVertex(Data);
    TestFalse(TEXT("Out-of-range vertex rejected"),ReadAC2StaticCollision(InvalidVertex,3,Triangles));
    // The cell wrapper adds its own BSP and portal polygon references. Those
    // polygons are room boundaries, not walls across traversable doorways.
    Data.Reset();Word(0);Word(2);
    for(uint16 Id:{0,1}) {Short(Id);Short(4);for(uint16 V:{0,1,2,3})Short(V);}
    Word(0);Word(0x4c454146);for(int32 I=0;I<4;++I)Word(0);
    Word(1);Short(0);Short(1);for(int32 I=0;I<4;++I)Word(0);Word(0);
    FAC2AssetReader Cell(Data);
    TestTrue(TEXT("Cell wrapper decoded"),ReadAC2StaticCollision(Cell,4,Triangles,true));
    TestEqual(TEXT("Only solid face retained; portal is open"),Triangles.Num(),6);
    Data.Pop();FAC2AssetReader ShortCell(Data);
    TestFalse(TEXT("Incomplete cell metadata rejected"),ReadAC2StaticCollision(ShortCell,4,Triangles,true));
    return true;
}

class FAcademyStairCollisionCommand:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;UWorld* World=nullptr;TUniquePtr<FAC2WorldStream> Stream;
    double Started=FPlatformTime::Seconds();
public:
    explicit FAcademyStairCollisionCommand(FAutomationTestBase* In):Test(In){}
    ~FAcademyStairCollisionCommand(){Stream.Reset();if(World){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
    bool Update() override
    {
        const FVector Origin=AC2::BlockOrigin(0x03fd);
        if(!World)
        {
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).EnableTraceCollision(true);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Stream=MakeUnique<FAC2WorldStream>();FString Error;
            if(!Test->TestTrue(TEXT("Open academy DATs"),Stream->Open(World,AC2::ResolveDatDirectory(TEXT("")),Error)))return true;
            Stream->SetInteriorCell(0x03fd0132);
        }
        Stream->Tick(Origin+FVector(8000,8000,-7300));
        if(!Stream->AllReady()){if(FPlatformTime::Seconds()-Started<90)return false;Test->AddError(TEXT("Academy collision timed out"));return true;}
        FAC2AssetStats Stats;
        Test->TestTrue(TEXT("Loaded academy exposes its diagnostics"),Stream->BlockStats(0x03fd,Stats));
        Test->TestEqual(TEXT("Both emitters and both mushrooms repaired"),Stats.AcademyPlacementRepairs,4);
        int32 Ramps=0,Walkable=0,Hits=0;
        for(TActorIterator<AActor> It(World);It;++It)
        {
            TArray<UProceduralMeshComponent*> Meshes;It->GetComponents(Meshes);
            for(auto* Mesh:Meshes)
            {
                // Both spiral stair setups have separate material-free meshes.
                if(Mesh->GetNumSections()!=1)continue;
                const auto* Section=Mesh->GetProcMeshSection(0);
                if(!Section || Section->bSectionVisible || !Section->bEnableCollision ||
                    !Mesh->GetComponentLocation().Equals(Origin+FVector(-35431,37530,-7990),10) &&
                    !Mesh->GetComponentLocation().Equals(Origin+FVector(-35403,37819,-6238),10))continue;
                ++Ramps;const auto& Vertices=Section->ProcVertexBuffer;const auto& Indices=Section->ProcIndexBuffer;
                for(int32 I=0;I+2<Indices.Num();I+=3)
                {
                    const auto Frame=Mesh->GetComponentTransform();
                    FVector A=Frame.TransformPosition(Vertices[Indices[I]].Position),B=Frame.TransformPosition(Vertices[Indices[I+1]].Position),C=Frame.TransformPosition(Vertices[Indices[I+2]].Position);
                    if(FVector::CrossProduct(C-A,B-A).GetSafeNormal().Z<.7 || FVector::CrossProduct(C-A,B-A).Size()<5000)continue;
                    ++Walkable;const FVector Center=(A+B+C)/3;
                    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2AcademyStair),true);
                    if(Mesh->LineTraceComponent(Hit,Center+FVector(0,0,15),Center-FVector(0,0,15),Query))++Hits;
                }
            }
        }
        Test->TestEqual(TEXT("Both authored staircase physics meshes instantiated"),Ramps,2);
        Test->TestTrue(TEXT("Stairs expose many walkable ramp faces"),Walkable>20);
        Test->TestEqual(TEXT("Every walkable stair face has cooked pawn collision"),Hits,Walkable);
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcademyStairCollisionTest,"AC2.Data.AcademyStairCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAcademyStairCollisionTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FAcademyStairCollisionCommand(this));return true;}
#endif
