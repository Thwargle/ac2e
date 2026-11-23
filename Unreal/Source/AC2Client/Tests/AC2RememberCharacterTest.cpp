#include "AC2GameMode.h"
#include "AC2Network.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2RememberCharacterTest, "AC2.Login.LastCharacterSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAC2RememberCharacterTest::RunTest(const FString&)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode = AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());
    AAC2GameMode* Mode = World->GetAuthGameMode<AAC2GameMode>();

    const FString Host = FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".example");
    const FString First = TEXT("9007199254740993"), Last = TEXT("9007199254740994");
    auto Roster = [&](FAC2Network& Net, TArray<FString> Ids) {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), TEXT("characters"));
        TArray<TSharedPtr<FJsonValue>> List;
        for (const FString& Id : Ids) {
            auto Character = MakeShared<FJsonObject>();
            Character->SetStringField(TEXT("id"), Id);
            List.Add(MakeShared<FJsonValueObject>(Character));
        }
        Event->SetArrayField(TEXT("characters"), List);
        Net.Handle(Event);
    };
    auto ReceiveId = [](FAC2Network& Net, const TCHAR* EventName, const FString& Id) {
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("event"), EventName);
        Event->SetStringField(TEXT("id"), Id);
        Net.Handle(Event);
    };

    for (bool Direct : {false, true}) {
        FString Section;
        {
            FAC2Network Session(Mode);
            Session.SetAccountScope(Host, 9000, TEXT("Tester"), Direct);
            Section = Session.CharacterSelectionSection;
            Roster(Session, {First, Last});
            TestEqual(TEXT("First visit selects the first available character"), Mode->Characters.ActiveId, First);
            Session.Enter(Last);
            ReceiveId(Session, TEXT("player"), Last);
            Session.SceneComplete();
        }
        {
            FAC2Network Relaunched(Mode);
            Relaunched.SetAccountScope(Host.ToUpper(), 9000, Direct ? TEXT("Tester") : TEXT("tester"), Direct);
            Roster(Relaunched, {First, Last});
            TestEqual(TEXT("Fresh session highlights last world character"), Mode->Characters.ActiveId, Last);
            TestTrue(TEXT("Selection still waits for Play"), Relaunched.IsSelection());
            Relaunched.Enter(First);
            ReceiveId(Relaunched, TEXT("character_error"), First);
        }
        {
            FAC2Network Relaunched(Mode);
            Relaunched.SetAccountScope(Host, 9000, TEXT("Tester"), Direct);
            Roster(Relaunched, {First, Last});
            TestEqual(TEXT("Rejected entry leaves last played character intact"), Mode->Characters.ActiveId, Last);
            Roster(Relaunched, {First});
            TestEqual(TEXT("An absent character falls back to the roster"), Mode->Characters.ActiveId, First);
            Roster(Relaunched, {});
            TestTrue(TEXT("Empty roster has no selected character"), Mode->Characters.ActiveId.IsEmpty());
            for (int32 Alternative = 0; Alternative < 3; ++Alternative) {
                Relaunched.SetAccountScope(Host, Alternative == 0 ? 9001 : 9000,
                    Alternative == 1 ? TEXT("Other") : TEXT("Tester"), Alternative == 2 ? !Direct : Direct);
                Roster(Relaunched, {First, Last});
                TestEqual(TEXT("Server endpoint, account and protocol have independent selections"), Mode->Characters.ActiveId, First);
            }
            Relaunched.SetAccountScope(Host, 9000, TEXT("Tester"), Direct);
            Roster(Relaunched, {First, Last});
            ReceiveId(Relaunched, TEXT("deleted"), Last);
            TestEqual(TEXT("Deleting the selected character highlights the next one"), Mode->Characters.ActiveId, First);
            FString SavedId;
            TestFalse(TEXT("Deletion clears the preference"), GConfig->GetString(*Section, TEXT("LastCharacter"), SavedId, GGameUserSettingsIni));
        }
        GConfig->EmptySection(*Section, GGameUserSettingsIni);
    }
    GConfig->Flush(false, GGameUserSettingsIni);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
