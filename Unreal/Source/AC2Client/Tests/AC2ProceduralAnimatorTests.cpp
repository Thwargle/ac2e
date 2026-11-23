#include "AC2Animation.h"
#include "AC2Look.h"
#include "AC2Assets.h"
#include "AC2Portrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void Word(TArray<uint8>& B,uint32 V){for(int32 I=0;I<4;++I)B.Add(uint8(V>>(8*I)));}
void Float(TArray<uint8>& B,float V){uint32 U;FMemory::Memcpy(&U,&V,4);Word(B,U);}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ProceduralAnimatorTest,"AC2.Data.ProceduralAnimators",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ProceduralAnimatorTest::RunTest(const FString&)
{
    TMap<uint32,TArray<uint8>> Records;
    for(uint32 I=1;I<=3;++I)
    {
        auto& B=Records.Add(0x05001000+I);
        Word(B,0x05001000+I);Word(B,0x40000001);Float(B,2);Word(B,1);
        Word(B,0);Word(B,1);Float(B,float(I));Float(B,0);Float(B,0);Float(B,0);
        Word(B,0);Word(B,0);
    }
    TArray<uint8> RandomBytes;
    for(uint32 V:{0x05001004u,0x40000007u,0x4000000eu,4u,0x05001001u,0x05001002u,0x05001002u,0x05001003u})Word(RandomBytes,V);
    Float(RandomBytes,1);Float(RandomBytes,.2f);Word(RandomBytes,0x4000000e);Float(RandomBytes,.05f);
    Records.Add(0x05001004,MoveTemp(RandomBytes));
    auto& Look=Records.Add(0x05001005);Word(Look,0x05001005);Word(Look,0x40000005);
    auto& Angle=Records.Add(0x05001006);
    for(uint32 V:{0x05001006u,0x4000000au,1u,0x8100000eu})Word(Angle,V);
    Float(Angle,1);Float(Angle,1);Float(Angle,.35f);Float(Angle,-.35f);Float(Angle,.35f);Float(Angle,-.35f);
    FAC2Animator::FRead Read=[&](uint32 Id,TArray<uint8>& B){if(auto* P=Records.Find(Id)){B=*P;return true;}return false;};
    auto R=FAC2Animator::Load(Read,0x05001004,0);
    if(!TestTrue(TEXT("authored random controller loads"),R.IsValid()))return false;
    FAC2AnimatorState State;State.Random.Initialize(42);
    FAC2AnimatorInput Input;Input.State=&State;
    TArray<FTransform> Rest{FTransform::Identity};
    TestEqual(TEXT("initial random child"),R->ActiveClips(Input)[0],0x05001001u);
    TestEqual(TEXT("initial period"),R->Period(Input),2.f);
    TSet<uint32> Seen;bool Ramped=false;
    for(int32 I=0;I<300;++I)
    {
        Input.Time=I*.05;
        const auto P=R->Sample(0,Input,Rest);
        const auto Ids=R->ActiveClips(Input);for(uint32 Id:Ids)Seen.Add(Id);
        TestTrue(TEXT("queries at one timestamp are stable"),R->Sample(0,Input,Rest)[0].Equals(P[0]));
        TestEqual(TEXT("random blend retains period"),R->Period(Input),2.f);
        if(Ids.Num()>1 && P[0].GetLocation().X>100 && P[0].GetLocation().X<300)Ramped=true;
    }
    TestEqual(TEXT("all weighted choices reachable"),Seen.Num(),3);
    TestTrue(TEXT("random transitions blend rather than snap"),Ramped);
    Input.Time=0;TestEqual(TEXT("rewound clock resets choice"),R->ActiveClips(Input)[0],0x05001001u);
    FAC2AnimatorState ClockState;ClockState.Random.Initialize(42);Input.State=&ClockState;
    R->Sample(0,Input,Rest);
    Input.Time=1.1;R->Sample(0,Input,Rest);
    TestTrue(TEXT("random ramp starts after idle frequency"),ClockState.Choices.FindChecked(0x4000000e).Active);
    const int32 RampSeed=ClockState.Random.GetCurrentSeed();
    R->ActiveClips(Input);R->Period(Input);
    TestEqual(TEXT("same-frame queries do not consume random choices"),ClockState.Random.GetCurrentSeed(),RampSeed);
    Input.Time=1.2;R->Sample(0,Input,Rest);
    TestEqual(TEXT("active ramp does not roll variation"),ClockState.Random.GetCurrentSeed(),RampSeed);
    Input.Time=1.31;R->Sample(0,Input,Rest);
    TestFalse(TEXT("finished ramp becomes idle"),ClockState.Choices.FindChecked(0x4000000e).Active);
    TestEqual(TEXT("finished ramp resets idle clock"),ClockState.Choices.FindChecked(0x4000000e).Elapsed,0.0);
    Input.Time=2.2;R->Sample(0,Input,Rest);
    TestFalse(TEXT("next delay begins after the previous ramp finishes"),ClockState.Choices.FindChecked(0x4000000e).Active);
    Input.Time=2.37;R->Sample(0,Input,Rest);
    TestTrue(TEXT("next idle starts once its fresh delay expires"),ClockState.Choices.FindChecked(0x4000000e).Active);
    Input.State=&State;
    auto& Random=Records.FindChecked(0x05001004);
    const auto Saved=Random;Random.Pop();TestFalse(TEXT("truncated controller rejected"),FAC2Animator::Load(Read,0x05001004,0).IsValid());Random=Saved;
    // Random graphs must obey the same recursion limits as all other graphs.
    Random[16]=4;Random[17]=0x10;
    TestFalse(TEXT("recursive random graph rejected"),FAC2Animator::Load(Read,0x05001004,0).IsValid());Random=Saved;
    auto L=FAC2Animator::Load(Read,0x05001005,0),A=FAC2Animator::Load(Read,0x05001006,0);
    if(!TestTrue(TEXT("look and angle controllers load"),L.IsValid()&&A.IsValid()))return false;
    TArray<uint32> Names{0x40000003,0x40000005,0x40000004,0x40000006,0x8100000e,0};
    Rest.Init(FTransform(FQuat(FVector::UpVector,.4),FVector(2,3,4),FVector(1,2,3)),Names.Num());
    Input.BoneNames=Names;Input.Look=FVector(0,0,10);
    const auto P=L->Sample(0,Input,Rest);
    for(int32 I=0;I<4;++I)
    {
        TestFalse(TEXT("named look bone rotates"),P[I].GetRotation().Equals(Rest[I].GetRotation()));
        TestTrue(TEXT("look preserves scaled skeleton and attachment translation"),P[I].GetLocation()==Rest[I].GetLocation()&&P[I].GetScale3D()==Rest[I].GetScale3D());
    }
    TestTrue(TEXT("unrelated bone untouched"),P[4].Equals(Rest[4]));
    TestTrue(TEXT("head rotation clamps look vector"),P[0].GetRotation().Equals(FQuat(FVector::UpVector,.75),.00001));
    TestTrue(TEXT("spine uses distinct retail weight"),P[2].GetRotation().Equals(FQuat(FVector::UpVector,.6325),.00001));
    Input.Look=FVector::ZeroVector;TestTrue(TEXT("neutral look preserves pose"),L->Sample(0,Input,Rest)[0].Equals(Rest[0]));
    Input.Grounded=true;Input.GroundNormal=FVector(0,.8,.6);Input.Time=0;
    TestTrue(TEXT("large terrain change begins from prior pose"),A->Sample(0,Input,Rest)[4].Equals(Rest[4]));
    Input.Time=.125;const auto Half=A->Sample(0,Input,Rest);
    Input.Time=.25;const auto Full=A->Sample(0,Input,Rest);
    TestTrue(TEXT("terrain ramp reaches authored clamp"),Full[4].GetRotation().Equals(FQuat(FVector::XAxisVector,-.35)*Rest[4].GetRotation(),.00001));
    TestTrue(TEXT("terrain ramp midpoint"),Half[4].GetRotation().Equals(FQuat(FVector::XAxisVector,-.175)*Rest[4].GetRotation(),.00001));
    TestTrue(TEXT("terrain only touches authored bone"),Full[0].Equals(Rest[0]));
    Input.Grounded=false;Input.Time=.3;A->Sample(0,Input,Rest);Input.Time=.6;
    TestTrue(TEXT("airborne clears terrain tilt"),A->Sample(0,Input,Rest)[4].Equals(Rest[4]));
    TestTrue(TEXT("target straight ahead keeps neutral head"),AC2Look::At(FTransform::Identity,FVector(0,0,200),FVector(0,300,200)).IsNearlyZero());
    TestTrue(TEXT("target to native right turns head right"),AC2Look::At(FTransform::Identity,FVector::ZeroVector,FVector(100,300,0)).Z<0);
    TestTrue(TEXT("elevated target pitches head up"),AC2Look::At(FTransform::Identity,FVector::ZeroVector,FVector(0,300,100)).X>0);
    const FTransform Scaled(FQuat(FVector::UpVector,1),FVector(100,200,300),FVector(2,2,2));
    TestTrue(TEXT("look rotates into local space without applying body scale twice"),
        AC2Look::At(Scaled,Scaled.GetLocation(),Scaled.TransformPosition(FVector(100,300,100))).Equals(
        AC2Look::At(FTransform::Identity,FVector::ZeroVector,FVector(100,300,100)),.00001));
    AC2Look::FInterest Interest;
    TestFalse(TEXT("head tracking survives the exact two-second boundary"),Interest.Update(FVector(0,-200,0),2));
    TestFalse(TEXT("high targets within horizontal range regain interest"),Interest.Update(FVector(0,900,2000),.1));
    TestEqual(TEXT("in-range target clears disinterest"),Interest.Disinterested,0.f);
    TestFalse(TEXT("distant target does not immediately clear head tracking"),Interest.Update(FVector(0,1100,0),1.9));
    TestTrue(TEXT("persistent distant target releases head tracking"),Interest.Update(FVector(0,1100,0),.2));
    AC2Look::FReporter Reporter;
    TestFalse(TEXT("look packets wait more than half a second"),Reporter.Update(.5,false,FVector(.3,0,.2)));
    TestFalse(TEXT("target tracking suppresses direction packets"),Reporter.Update(1,true,FVector(.3,0,.2)));
    TestTrue(TEXT("changed camera head pose reports"),Reporter.Update(1,false,FVector(.3,0,.2)));
    TestFalse(TEXT("stationary head never floods the server"),Reporter.Update(30,false,FVector(.3001,0,.2)));
    TestTrue(TEXT("return to neutral is sent"),Reporter.Update(30,false,FVector::ZeroVector));
    TestTrue(TEXT("camera behind player looks forward"),AC2Look::Camera(FTransform::Identity,FVector(0,1,0)).IsNearlyZero());
    TestTrue(TEXT("camera in front does not twist neck backward"),AC2Look::Camera(FTransform::Identity,FVector(0,-1,0)).IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2InstalledAnimationAudit,"AC2.Data.InstalledAnimationCoverage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2InstalledAnimationAudit::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<FAC2DatEntry> Entries;
    if(!TestTrue(TEXT("enumerate installed portal.dat"),Portal.Open(FPaths::Combine(AC2::ResolveDatDirectory(TEXT("")),TEXT("portal.dat")),1,Error) && Portal.Enumerate(Entries,Error)))return false;
    int32 GraphCount=0,Bindings=0;
    TSet<uint32> Animators;
    for(const auto& E:Entries)if((E.Id>>24)==5)Animators.Add(E.Id);
    for(const auto& E:Entries)
    {
        if((E.Id>>24)==5)
        {
            if(!FAC2Animator::Load(Portal,E.Id,0))AddError(FString::Printf(TEXT("Undecodable animation %08X"),E.Id));
            ++GraphCount;
        }
        if((E.Id>>24)!=3)continue;
        TArray<uint8> Data;
        if(!Portal.Read(E.Id,Data,Error)){AddError(Error);continue;}
        FAC2AssetReader Reader(MoveTemp(Data));Reader.Word();
        const uint32 Count=Reader.Word()&65535;
        for(uint32 I=0;I<Count && Reader.Good;++I)
        {
            const uint32 Key=Reader.Word(),Graph=Reader.Word();
            if(Graph && !Animators.Contains(Graph))AddError(FString::Printf(TEXT("Animation map %08X key %08X references missing graph %08X"),E.Id,Key,Graph));
            ++Bindings;
        }
        if(!Reader.End())AddError(FString::Printf(TEXT("Invalid animation map %08X"),E.Id));
    }
    AddInfo(FString::Printf(TEXT("Audited %d installed native animators and %d animation-map bindings"),GraphCount,Bindings));
    return TestTrue(TEXT("installed archive contains native animation sets"),GraphCount>0 && Bindings>0);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2InstalledProceduralTest,"AC2.Data.InstalledProceduralAnimators",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2InstalledProceduralTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;
    if(!TestTrue(TEXT("portal archive"),Portal.Open(FPaths::Combine(AC2::ResolveDatDirectory(TEXT("")),TEXT("portal.dat")),1,Error)))return false;
    FAC2AnimatorInput Input;FAC2AnimatorState State;Input.State=&State;
    for(uint32 Id:{0x0500002au,0x050002fdu,0x05000275u,0x050003f3u,0x050000ffu,0x0500010du,0x050003e1u,0x050003f0u})
    {
        auto Graph=FAC2Animator::Load(Portal,Id,0);
        if(!TestTrue(FString::Printf(TEXT("installed controller %08X loads"),Id),Graph.IsValid()))continue;
        TSet<uint32> Seen;
        for(int32 I=0;I<1000;++I)
        {
            Input.Time=I*.1;const float Period=Graph->Period(Input);
            TestTrue(TEXT("finite native controller period"),FMath::IsFinite(Period)&&Period>=0);
            for(uint32 Clip:Graph->ActiveClips(Input))Seen.Add(Clip);
            const auto Events=Graph->EffectsBetween(0,.1,Input);
            for(const auto& E:Events)TestTrue(TEXT("finite random FX"),FMath::IsFinite(E.Phase)&&FMath::IsFinite(E.Intensity));
        }
        if(Id==0x05000275 || Id==0x050003f3)TestTrue(TEXT("installed random graph changes idle"),Seen.Num()>1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ProceduralMeshTest,"AC2.Data.InstalledProceduralMeshes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ProceduralMeshTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Vertices=[](AActor* Actor)
    {
        TArray<FVector> Out;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
        for(auto* M:Meshes)for(int32 S=0;S<M->GetNumSections();++S)if(auto* P=M->GetProcMeshSection(S))
            for(const auto& V:P->ProcVertexBuffer)Out.Add(M->GetComponentTransform().TransformPosition(V.Position));
        return Out;
    };
    auto Difference=[](const TArray<FVector>& A,const TArray<FVector>& B)
    {
        double Maximum=0;for(int32 I=0;I<FMath::Min(A.Num(),B.Num());++I)
            Maximum=FMath::Max(Maximum,FVector::Distance(A[I],B[I]));
        return Maximum;
    };
    // These two installed visuals reference maps 03000023/4D, whose native
    // idle graphs previously failed on AMRandom. Exercise the actual meshes.
    for(uint32 Visual:{0x1f00002fu,0x1f0001f1u,0x1f000023u,0x1f000026u})
    {
        auto* Actor=World->SpawnActor<AAC2Portrait>();FAC2AssetLibrary Model;FString Error;
        if(TestTrue(FString::Printf(TEXT("procedural visual %08X loads"),Visual),
            Model.Open(AC2::ResolveDatDirectory(TEXT("")),Error)&&Model.DrawModel(Visual,Actor,Actor->Body)))
        {
            Model.SetAnimationEnvironment(true,FVector::UpVector);Model.Animate(0,0);
            const auto Before=Vertices(Actor);TestTrue(TEXT("controller test has actual rendered vertices"),Before.Num()>0);
            double Movement=0;
            for(int32 Frame=1;Frame<=240;++Frame)
            {
                Model.Animate(Frame*.1f,0);
                const auto Current=Vertices(Actor);
                TestTrue(TEXT("native pose remains finite"),!Current.ContainsByPredicate([](const FVector& V){return V.ContainsNaN();}));
                Movement=FMath::Max(Movement,Difference(Before,Current));
            }
            TestTrue(TEXT("native idle moves the actual model"),Movement>.1);
            if(Visual==0x1f000023 || Visual==0x1f000026)
            {
                for(double Height:{.8,1.2})
                {
                    Actor->Body->SetRelativeScale3D(FVector(1,1,Height));
                    Model.SetAnimationEnvironment(true,FVector::UpVector);Model.Animate(24,0);
                    const auto Neutral=Vertices(Actor);
                    Model.SetAnimationEnvironment(true,FVector::UpVector,FVector(.2,0,.6));Model.Animate(24,0);
                    TestTrue(TEXT("initial look behavior reaches the rendered scaled body"),Difference(Neutral,Vertices(Actor))>.1);
                    Model.SetAnimationEnvironment(true,FVector::UpVector);Model.Animate(24,0);
                    TestTrue(TEXT("cleared look returns to the same native pose without accumulation"),Difference(Neutral,Vertices(Actor))<.01);
                }
            }
        }
        Actor->Destroy();
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
