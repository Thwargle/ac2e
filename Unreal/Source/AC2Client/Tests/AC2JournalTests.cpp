#include "AC2SkillData.h"
#include "AC2NativeUI.h"
#include "AC2PortalSpace.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2JournalTest,"AC2.Data.JournalSkillsAndWindowPlacement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2JournalTest::RunTest(const FString&)
{
    const auto Directory=AC2::ResolveDatDirectory(TEXT(""));FAC2SkillData Skills;
    TestTrue(TEXT("Native skill index"),Skills.Open(Directory));
    TestEqual(TEXT("Native Mind Lash name"),Skills.Get(0x60).Name,FString(TEXT("Mind Lash")));
    TestEqual(TEXT("Native Mind Lash icon"),Skills.Get(0x60).Icon,0x41003d05u);
    TestFalse(TEXT("Native skill description"),Skills.Get(0x60).Description.IsEmpty());
    for(uint32 Race:{1u,2u,4u})TestTrue(TEXT("Entire racial catalogue, not only learned starters"),Skills.All(Race).Num()>50);
    TestTrue(TEXT("Mind Lash is a free starter"),Skills.Get(0x60).Valid && Skills.Get(0x60).Credits==0);
    TestEqual(TEXT("First native trained rank"),Skills.Rank(0x60,0),1);
    TestTrue(TEXT("Next rank reads the native cumulative XP curve"),Skills.NextCost(0x59,0)>0);
    TestEqual(TEXT("All native skill panels"),Skills.PanelCount(),70);
    for(uint32 Race:{1u,2u,4u,8u,16u})for(int32 Tab=0;Tab<4;++Tab)
        TestTrue(TEXT("Native racial root decodes"),Skills.Panel(FAC2SkillData::RootPanel(Race,Tab)).Valid);
    const auto& Magic=Skills.Panel(FAC2SkillData::RootPanel(1,2));
    TestEqual(TEXT("Human magic native root"),FAC2SkillData::RootPanel(1,2),0x8000000bu);
    TestEqual(TEXT("Human magic node count"),Magic.Nodes.Num(),23);
    TestTrue(TEXT("Native class navigation"),Magic.Nodes.ContainsByPredicate([](const auto& N){return N.Kind==0x40000002&&N.Id==0x80000004;}));
    TestTrue(TEXT("All registered panels decode including formatted descriptions"),[&](){
        int32 Count=0;for(uint32 Id=0x80000004;Id<=0x80000052;++Id)Count+=Skills.Panel(Id).Valid;return Count==70;}());
    FAC2NativeUI UI;FString Error;TestTrue(TEXT("Native UI"),UI.Open(Directory,Error));
    const auto T=[](const TCHAR* N){return FAC2UILayout::Token(N);};
    UI.Visibility.Add(T(TEXT("InventoryField")),true);
    UI.Begin({1600,900});UI.Root(0x22000001,TEXT("RightSidePanel"));
    const FIntRect Before=UI.Bounds(TEXT("ContainerListBox"));
    UI.RootOffsets.Add(T(TEXT("RightSidePanel")),FIntPoint(-200,-100));
    UI.Begin({1600,900});UI.Root(0x22000001,TEXT("RightSidePanel"));
    TestTrue(TEXT("Moving a window moves its actual inventory hit region"),UI.Bounds(TEXT("ContainerListBox")).Min==Before.Min+FIntPoint(-200,-100));
    UI.Visibility.Add(T(TEXT("InventoryField")),false);UI.Visibility.Add(T(TEXT("SkillField")),true);
    UI.Begin({1600,900});UI.Root(0x22000001,TEXT("RightSidePanel"));
    TestNotNull(TEXT("Native skill panel and close button"),UI.Find(0x96a07fb7));
    UI.Visibility.Add(T(TEXT("SkillField")),false);UI.Visibility.Add(T(TEXT("QuestField")),true);
    UI.Begin({1600,900});UI.Root(0x22000001,TEXT("RightSidePanel"));
    TestNotNull(TEXT("Native quest panel and close button"),UI.Find(0x92d81e44));
    const uint32 Tabs[]={0x715bdff7,0x70859237,0x92e39d2f};
    for(int32 Selected=0;Selected<3;++Selected){
        UI.Begin({1600,900});
        for(int32 I=0;I<3;++I)UI.States.Add(Tabs[I],T(I==Selected?TEXT("open"):TEXT("closed")));
        UI.Root(0x22000001,TEXT("RightSidePanel"));
        for(int32 I=0;I<3;++I){
            const auto* N=UI.Find(Tabs[I]);
            if(!TestNotNull(TEXT("Quest tab"),N))continue;
            TestEqual(TEXT("DAT page state"),N->State,T(I==Selected?TEXT("open"):TEXT("closed")));
            // Check rendered art under each tab rather than only the state variable.
            bool Art=false;const int32 Parent=int32(N-UI.Nodes.GetData());
            for(int32 J=Parent+1;J<UI.Nodes.Num();++J)if(UI.Nodes[J].Parent==Parent)
                for(const auto& M:UI.Nodes[J].Media)Art|=M.Image==(I==Selected?0x41001e5au:0x41001e5bu);
            TestTrue(TEXT("Selected tab uses open border artwork; others use closed"),Art);
        }
    }
    UI.Visibility.Add(T(TEXT("ExternalContainerField")),true);UI.Root(0x22000005,TEXT("ExternalContainerField"));
    TestNotNull(TEXT("Native loot container"),UI.Find(TEXT("ExternalContainer")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PortalSoundTest,"AC2.Data.NativePortalSoundRoles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2PortalSoundTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;
    if(!TestTrue(TEXT("Portal DAT"),Portal.Open(AC2::ResolveDatDirectory(TEXT(""))/TEXT("portal.dat"),1,Error)))return false;
    FAC2PortalSounds Sounds;
    TestTrue(TEXT("Portal sound enums"),Portal.Read(0x28000003,Bytes,Error)&&Sounds.Decode(Bytes));
    TestEqual(TEXT("Two native tunnel loops"),Sounds.Loops.Num(),2);
    for(uint32 Id:{Sounds.Enter,Sounds.Exit}){
        TestTrue(TEXT("Entry and exit cues exist"),Portal.Read(Id,Bytes,Error));
        TestFalse(TEXT("Entry and exit must not leave a looping voice"),(AC2::U32(Bytes.GetData()+4)&1)!=0);
    }
    for(uint32 Id:Sounds.Loops){
        TestTrue(TEXT("Tunnel cue exists"),Portal.Read(Id,Bytes,Error));
        TestTrue(TEXT("Tunnel contexts require explicit stop"),(AC2::U32(Bytes.GetData()+4)&1)!=0);
    }
    TestFalse(TEXT("Truncated map rejected"),Sounds.Decode(TConstArrayView<uint8>()));
    TestTrue(TEXT("Invalid map clears old roles"),Sounds.Enter==0&&Sounds.Exit==0&&Sounds.Loops.IsEmpty());
    return true;
}
#endif
