#include "AC2MotionData.h"
#include "AC2LoginUI.h"
#include "AC2DatArchive.h"
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
#endif
