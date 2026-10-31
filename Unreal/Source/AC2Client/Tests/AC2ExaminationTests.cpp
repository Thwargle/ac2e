#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LiveExaminationTest,"AC2.Data.LiveExamination",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LiveExaminationTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();
    PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=TEXT("101");Player.Name=TEXT("Inspector");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);
    auto& Net=*Mode->Network;Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    for(const TCHAR* Id:{TEXT("101"),TEXT("202"),TEXT("303")})
        Net.Objects.Add(Id,MakeUnique<FAC2Network::FObject>());
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture real bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))
    {
        const auto UI=SNew(SAC2GameUI).Mode(Mode);
        TestTrue(TEXT("Native examination HUD is visible"),UI->VisibleWorld());
        const auto Geometry=FGeometry::MakeRoot(FVector2D(1280,720),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(Geometry,FPlatformTime::Seconds(),.016f);};
        auto ReadCommands=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Due=[&](){Net.ExaminationStarted=FPlatformTime::Seconds()-.6;};
        auto Event=[](const TCHAR* Type,const TCHAR* Id){auto J=MakeShared<FJsonObject>();
            J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);return J;};
        auto Profile=[&](const TCHAR* Id,const TCHAR* Name,const TCHAR* Stat){
            auto J=Event(TEXT("examination"),Id);TArray<TSharedPtr<FJsonValue>> Nodes;
            auto Row=[&](const FString& Line){
                auto N=MakeShared<FJsonObject>();N->SetNumberField(TEXT("kind"),0x40000004);
                auto Text=MakeShared<FJsonObject>();Text->SetStringField(TEXT("literal"),Line);
                N->SetObjectField(TEXT("text"),Text);Nodes.Add(MakeShared<FJsonValueObject>(N));
                auto Break=MakeShared<FJsonObject>();Break->SetNumberField(TEXT("kind"),0x40000006);
                Nodes.Add(MakeShared<FJsonValueObject>(Break));
            };
            Row(Name);Row(Stat);
            // Overflow the native panel to exercise retained scroll position.
            for(int32 I=0;I<40;++I)Row(FString::Printf(TEXT("Description line %d"),I));
            J->SetArrayField(TEXT("nodes"),Nodes);Net.Handle(J);
        };
        auto Shows=[&](const TCHAR* Text){return UI->Native.Nodes.ContainsByPredicate(
            [&](const auto& N){return N.Text==Text;});};

        UI->InspectTarget(TEXT("202"));
        TestTrue(TEXT("Opening panel requests authoritative object profile"),ReadCommands().Contains(TEXT("\"id\":\"202\"")));
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 100 / 100"));Tick();
        TestTrue(TEXT("Initial native rows are displayed"),Shows(TEXT("Health: 100 / 100")));
        TestTrue(TEXT("Every-frame UI rebuild does not poll every frame"),ReadCommands().IsEmpty());
        UI->ExamineScroll=32;Due();Tick();
        TestTrue(TEXT("Visible panel requests live update"),ReadCommands().Contains(TEXT("\"command\":\"examine\"")));
        TestTrue(TEXT("Old rows stay visible during refresh"),Shows(TEXT("Health: 100 / 100")));
        TestFalse(TEXT("Refresh never replaces rows with a loading message"),Shows(TEXT("Examining...")));
        TestEqual(TEXT("Refresh preserves scroll"),UI->ExamineScroll,32);
        Due();Tick();
        TestTrue(TEXT("Slow response cannot create overlapping polls"),ReadCommands().IsEmpty());
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 40 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Damage appears without another user examination"),Shows(TEXT("Health: 40 / 100")));
        TestFalse(TEXT("Previous health is replaced"),Shows(TEXT("Health: 100 / 100")));
        TestEqual(TEXT("New server rows preserve scroll"),UI->ExamineScroll,32);

        UI->ExamineScroll=0;
        Due();Tick();ReadCommands();
        Profile(TEXT("202"),TEXT("Corpse of Red Soldier Wasp"),TEXT("Health: 0 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Death updates both name and health"),Shows(TEXT("Corpse of Red Soldier Wasp")) && Shows(TEXT("Health: 0 / 100")));

        // The selected combat target is independent from the inspected object.
        Net.TargetId=TEXT("303");Due();Tick();
        TestTrue(TEXT("Inspection continues on its own object"),ReadCommands().Contains(TEXT("\"id\":\"202\"")));
        UI->InspectTarget(TEXT("101"));
        TestTrue(TEXT("Rapid target switch waits for server cooldown"),ReadCommands().IsEmpty());
        Profile(TEXT("202"),TEXT("Old target"),TEXT("Health: 0 / 100"));
        TestTrue(TEXT("Late old target response leaves new request pending"),Net.ExaminationPending);
        TestTrue(TEXT("Changing objects clears previous object's rows"),Net.ExaminationRows.IsEmpty());
        Due();Tick();
        TestTrue(TEXT("Queued target switch sends after cooldown"),ReadCommands().Contains(TEXT("\"id\":\"101\"")));
        Profile(TEXT("101"),TEXT("Inspector"),TEXT("Vigor: 20 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Self examination uses authoritative vigor"),Shows(TEXT("Vigor: 20 / 100")));
        Due();Tick();ReadCommands();
        Profile(TEXT("101"),TEXT("Inspector"),TEXT("Vigor: 45 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Self regeneration updates live"),Shows(TEXT("Vigor: 45 / 100")));

        Due();UI->InspectTarget(TEXT("303"));ReadCommands();
        Profile(TEXT("303"),TEXT("Boots"),TEXT("In your inventory."));
        Due();Tick();ReadCommands();Profile(TEXT("303"),TEXT("Boots"),TEXT("Equipped."));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Equipment location updates live"),Shows(TEXT("Equipped.")));
        UI->ClosePanels();Due();Tick();
        TestTrue(TEXT("Closed panel stops polling"),ReadCommands().IsEmpty());
        auto Name=Event(TEXT("quality"),TEXT("303"));Name->SetNumberField(TEXT("table"),10);Name->SetNumberField(TEXT("key"),0x100);
        auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("literal"),TEXT("Renamed boots"));Name->SetObjectField(TEXT("value"),Value);
        Net.Handle(Name);Tick();
        TestTrue(TEXT("Name updates do not poll a closed panel"),ReadCommands().IsEmpty());

        UI->InspectTarget(TEXT("202"));ReadCommands();
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 100 / 100"));Due();Tick();ReadCommands();
        Net.Handle(Event(TEXT("remove"),TEXT("202")));Tick();
        TestTrue(TEXT("Removed object clears stale statistics"),Net.ExaminationRows.IsEmpty());
        TestTrue(TEXT("Removed object shows unavailable message"),Shows(TEXT("That object is no longer available to examine.")));
        Profile(TEXT("202"),TEXT("Late removed object"),TEXT("Health: 100 / 100"));
        TestTrue(TEXT("Late response cannot restore removed object"),Net.ExaminationRows.IsEmpty());
        Due();Tick();TestTrue(TEXT("Removed object stops polling"),ReadCommands().IsEmpty());

        Due();UI->InspectTarget(TEXT("101"));ReadCommands();
        Net.ExaminationStarted=FPlatformTime::Seconds()-9;Net.Tick(0);Tick();
        TestFalse(TEXT("Timed out request finishes"),Net.ExaminationPending);
        TestFalse(TEXT("Timed out request reports an error"),Net.ExaminationError.IsEmpty());
        TestTrue(TEXT("Timeout does not hammer the server"),ReadCommands().IsEmpty());
        UI->InspectTarget(TEXT("101"));TestFalse(TEXT("Explicit retry recovers after a timeout"),ReadCommands().IsEmpty());
        Net.ClearWorld();
        TestTrue(TEXT("Logout clears examination state"),Net.ExaminationId.IsEmpty() && Net.ExaminationRows.IsEmpty() && !Net.ExaminationPending);
    }
    FPlatformProcess::ClosePipe(Commands,nullptr);
    Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
