#include "AC2RadarData.h"
#include "AC2GameMode.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RadarDataTest,"AC2.Data.RetailRadar",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RadarDataTest::RunTest(const FString&)
{
    FAC2RadarData Data;FString Error;
    if(!TestTrue(TEXT("Installed native radar metadata loads"),Data.Open(AC2::ResolveDatDirectory(TEXT("")),Error)))
    {AddError(Error);return false;}
    // EntityDesc -> script class -> inherited template, including indirect bases.
    // The two DAT revisions remap EntityDesc DIDs. Identify the installed
    // AdminAvatar record before checking the corresponding native entities.
    const bool Later=Data.Look(0x47000531).Category==AC2Radar::Players;
    TestTrue(TEXT("Recognized installed AdminAvatar fixture"),Later || Data.Look(0x4700110e).Category==AC2Radar::Players);
    const uint32 LegacyEntities[]={0x4700053f,0x470012f9,0x4700110e,0x4700079a,0x47000714,0x4700079b,0x470008a2};
    const uint32 LaterEntities[]={0x47000676,0x470031fb,0x47000531,0x47001bff,0x47001143,0x47000fce,0x47000ead};
    const uint32* Entities=Later?LaterEntities:LegacyEntities;
    const uint32 Categories[]={1,2,4,8,16,32,32};
    for(int32 I=0;I<UE_ARRAY_COUNT(Categories);++I){
        const auto L=Data.Look(Entities[I]);
        TestEqual(*FString::Printf(TEXT("Native category %08X"),Entities[I]),L.Category,Categories[I]);
        TestFalse(TEXT("Unrelated category cannot display or receive a radar hit"),L.Visible(63&~Categories[I]));
    }
    const auto Override=Data.Look(Later?0x47000676:0x4700289e);
    TestEqual(TEXT("DAT custom shape wins over monster default"),Override.Shape,0x40000002u);
    TestTrue(TEXT("DAT custom ARGB color is preserved"),Override.Color.Equals(FLinearColor(0,1,0,1)));
    TestFalse(TEXT("Authored no-blip objects stay hidden with every category enabled"),Data.Look(Later?0x47000892:0x47001d35).Visible(63));
    TestFalse(TEXT("Unknown entity does not masquerade as a monster"),Data.Look(0x47ffffff).Visible(63));
    TestFalse(TEXT("Unknown native class stays hidden"),Data.Look(0xffff).Visible(63));
    TestTrue(TEXT("NPC has native yellow edges"),AC2Radar::DefaultLook(AC2Radar::NPCs).Color.Equals(FLinearColor(1,1,0)));
    const auto Monster=AC2Radar::DefaultLook(AC2Radar::Monsters);
    const auto Corpse=AC2Radar::Resolve(Monster,0,{}, {}, {});
    TestTrue(TEXT("Corpse label switches to the native item color"),Corpse.Color.Equals(AC2Radar::DefaultLook(AC2Radar::Items).Color));
    const auto Live=AC2Radar::Resolve(Monster,1,{}, {}, {});
    TestTrue(TEXT("Living monster label retains native color"),Live.Color.Equals(Monster.Color));
    const auto Colored=AC2Radar::Resolve(Monster,0,{}, {}, 0x8040a0ffu);
    TestTrue(TEXT("Server ARGB override also colors corpse labels"),Colored.Color.Equals(FLinearColor(64/255.f,160/255.f,1,128/255.f)));
    TestEqual(TEXT("Monster X has five pixels"),AC2Radar::Pixels(0x40000004).Num(),5);
    TestEqual(TEXT("NPC edges have four pixels"),AC2Radar::Pixels(0x40000007).Num(),4);
    TestEqual(TEXT("Portal hollow square has eight pixels"),AC2Radar::Pixels(0x40000005).Num(),8);
    TestEqual(TEXT("Lifestone L has four pixels"),AC2Radar::Pixels(0x40000006).Num(),4);
    TestEqual(TEXT("Selected marker adds twenty outline pixels"),AC2Radar::Pixels(0x40000004,true).Num(),25);
    TestTrue(TEXT("No-blip has no selection outline"),AC2Radar::Pixels(0x4000000b,true).IsEmpty());
    for(uint32 S=0x40000001;S<=0x4000000a;++S){
        const auto Pixels=AC2Radar::Pixels(S);TSet<FIntPoint> Unique;
        for(const auto& P:Pixels){Unique.Add(P);TestTrue(TEXT("Native symbol fits three pixels"),FMath::Abs(P.X)<=1&&FMath::Abs(P.Y)<=1);}
        TestEqual(TEXT("Pixel positions never repeat"),Unique.Num(),Pixels.Num());
    }
    TArray<uint8> Truncated={5,0,0,0x56};
    TestFalse(TEXT("Truncated script metadata fails closed"),Data.DecodeClasses(Truncated));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RadarUpdatesTest,"AC2.Data.RadarQualityUpdates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2RadarUpdatesTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    {
        FAC2Network Net(World->GetAuthGameMode<AAC2GameMode>());
        Net.Objects.Add(TEXT("202"),MakeUnique<FAC2Network::FObject>());
        auto& Obj=*Net.Objects[TEXT("202")];Obj.Instance=8;
        auto Event=[&](uint32 Table,uint32 Key,int64 Value,int32 Instance=8){
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("quality"));J->SetStringField(TEXT("id"),TEXT("202"));
            J->SetNumberField(TEXT("table"),Table);J->SetNumberField(TEXT("key"),Key);J->SetNumberField(TEXT("instance"),Instance);
            if(Table==12)J->SetStringField(TEXT("value"),FString::Printf(TEXT("%lld"),Value));else J->SetNumberField(TEXT("value"),Value);
            Net.Handle(J);
        };
        Event(4,0x268,0x4000000b);
        TestTrue(TEXT("No-blip visual quality is retained"),Obj.RadarShape.IsSet() && *Obj.RadarShape==0x4000000b);
        Event(4,0x268,0x40000002,7);
        TestEqual(TEXT("Stale instance cannot restore a hidden marker"),*Obj.RadarShape,0x4000000bu);
        Event(4,0x268,0);
        TestTrue(TEXT("Explicit invalid shape is distinct from no override"),Obj.RadarShape.IsSet() && *Obj.RadarShape==0);
        Event(1,0x270,0);TestTrue(TEXT("Live nonselectability hides radar contact"),Obj.RadarSelectable.IsSet() && !*Obj.RadarSelectable);
        Event(1,0x270,1);TestTrue(TEXT("Live selectability can be restored"),*Obj.RadarSelectable);
        Event(12,0x259,0xff00ff00);TestTrue(TEXT("Long-int color is accepted from decimal bridge string"),Obj.RadarColor.IsSet() && *Obj.RadarColor==0xff00ff00);
        Event(12,0x259,0xffffffff,6);TestEqual(TEXT("Stale color is ignored"),*Obj.RadarColor,0xff00ff00u);
        TestEqual(TEXT("Radar updates do not change gameplay health"),Obj.Health,-1.f);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
