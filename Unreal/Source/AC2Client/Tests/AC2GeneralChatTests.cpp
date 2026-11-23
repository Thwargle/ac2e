#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2NativeUI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2GeneralChatTest,"AC2.Network.GeneralChat",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2GeneralChatTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=TEXT("101");Player.Name=TEXT("Chat tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture actual bridge IPC"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))
    {
        const auto UI=SNew(SAC2GameUI).Mode(Mode);Cast<AAC2HUD>(PC->MyHUD)->GameUI=UI;
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Enter=[&](const FString& Message){UI->Editing=true;UI->Entry->SetText(Message);
            UI->Entry->OnKeyDown(FGeometry(),FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));};
        for(const TCHAR* Message:{TEXT("/cg Hello, Dereth!"),TEXT("  /CG   Hello, Dereth!  ")}){
            Enter(Message);
            TSharedPtr<FJsonObject> Request;
            if(TestTrue(TEXT("Enter submits one JSON command"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Read()),Request))){
                TestEqual(TEXT("Native room chat selected"),Request->GetStringField(TEXT("channel")),FString(TEXT("general")));
                TestEqual(TEXT("Command removed from message"),Request->GetStringField(TEXT("text")),FString(TEXT("Hello, Dereth!")));
            }
            TestFalse(TEXT("Enter exits input mode"),UI->Editing);
            TestTrue(TEXT("Input clears after submission"),UI->Entry->Value().IsEmpty());
        }
        for(const TCHAR* Empty:{TEXT("/cg"),TEXT("/cg   ")}){
            Enter(Empty);TestTrue(TEXT("Empty General message sends nothing"),Read().IsEmpty());
            TestTrue(TEXT("Empty command gives usage"),Mode->Notice.Contains(TEXT("Use /cg")));
        }
        Enter(TEXT("/cgarbage hello"));TestTrue(TEXT("Similar unknown command never broadcasts"),Read().IsEmpty());
        Enter(TEXT("/say nearby"));TestTrue(TEXT("Nearby chat retains its own channel"),Read().Contains(TEXT("\"channel\":\"say\"")));
        Enter(TEXT("/tell Other Player, private"));const FString Tell=Read();
        TestTrue(TEXT("Private chat remains private"),Tell.Contains(TEXT("\"channel\":\"tell\"")) && Tell.Contains(TEXT("Other Player")));
        auto Literal=[](const TCHAR* Text){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("literal"),Text);return J;};
        auto Chat=MakeShared<FJsonObject>();Chat->SetStringField(TEXT("event"),TEXT("chat"));
        Chat->SetStringField(TEXT("channel"),TEXT("General"));Chat->SetObjectField(TEXT("speaker"),Literal(TEXT("Other Player")));
        Chat->SetObjectField(TEXT("text"),Literal(TEXT("Hello, Ω!")));Net.Handle(Chat);
        TestEqual(TEXT("Incoming global chat shows channel and sender"),UI->Log.Lines.Last().Text,FString(TEXT("[General] Other Player: Hello, Ω!")));
        Chat->RemoveField(TEXT("channel"));Chat->SetNumberField(TEXT("kind"),32);
        Chat->SetStringField(TEXT("format"),TEXT("tell_sent"));Net.Handle(Chat);
        TestEqual(TEXT("Accepted outgoing tell is visible with its recipient"),UI->Log.Lines.Last().Text,FString(TEXT("You tell Other Player, \"Hello, Ω!\"")));
        TestEqual(TEXT("Tell type reaches the chat log"),UI->Log.Lines.Last().NativeType,32u);
        Chat->SetStringField(TEXT("format"),TEXT("tell"));Net.Handle(Chat);
        TestEqual(TEXT("Incoming tell retains quoted message formatting"),UI->Log.Lines.Last().Text,FString(TEXT("Other Player tells you, \"Hello, Ω!\"")));
        Chat->SetStringField(TEXT("format"),TEXT("speech"));Chat->SetStringField(TEXT("speaker_id"),Player.Id);Chat->SetNumberField(TEXT("kind"),16);Net.Handle(Chat);
        TestEqual(TEXT("Own nearby speech is identified as own speech"),UI->Log.Lines.Last().Text,FString(TEXT("You say, \"Hello, Ω!\"")));
        const uint32 Types[]={1,2,4,8,16,32,64,128,0x10000,0x20000,0x80000,0x100,0x20020};
        const uint32 Colors[]={0,1,2,3,4,5,6,4,7,8,4,9,5};
        const auto Geometry=FGeometry::MakeRoot(FVector2D(1280,720),FSlateLayoutTransform());
        UI->Tick(Geometry,FPlatformTime::Seconds(),.016f);
        const auto* ChatNode=UI->Native.Nodes.FindByPredicate([](const auto& N){return N.Id==FAC2UILayout::Token(TEXT("ChatLog"));});
        for(int32 I=0;I<UE_ARRAY_COUNT(Types);++I){
            Chat->SetNumberField(TEXT("kind"),Types[I]);Net.Handle(Chat);
            TestEqual(TEXT("Network retains native text type"),UI->Log.Lines.Last().NativeType,Types[I]);
            TestEqual(TEXT("Native text type chooses its DAT palette"),AC2ChatColorIndex(Types[I],EAC2ChatType::Local),Colors[I]);
        }
        if(TestNotNull(TEXT("Native chat template exists"),ChatNode)){
            TestNotEqual(TEXT("Tell and speech use distinct native colors"),ChatNode->TextColor(5),ChatNode->TextColor(4));
            TestNotEqual(TEXT("Error and combat use distinct native colors"),ChatNode->TextColor(0),ChatNode->TextColor(1));
        }
        auto Error=MakeShared<FJsonObject>();Error->SetStringField(TEXT("event"),TEXT("error"));
        Error->SetStringField(TEXT("message"),TEXT("General chat was rejected by the server (10)."));Net.Handle(Error);
        TestTrue(TEXT("Server rejection is visible"),Mode->Notice.Contains(TEXT("rejected")));
        auto Capability=MakeShared<FJsonObject>();Capability->SetStringField(TEXT("event"),TEXT("chat"));
        auto CapabilityText=Literal(TEXT("[AC2E:map-travel-v1]"));CapabilityText->SetStringField(TEXT("resolved"),TEXT(""));
        Capability->SetObjectField(TEXT("text"),CapabilityText);Capability->SetObjectField(TEXT("speaker"),Literal(TEXT("Other Player")));
        Net.Handle(Capability);TestFalse(TEXT("Player speech cannot enable server capabilities"),Net.MapTravelEnabled);
        Capability->RemoveField(TEXT("speaker"));const int32 Lines=UI->Log.Lines.Num();Net.Handle(Capability);
        TestTrue(TEXT("Wire capability survives native metadata stripping"),Net.MapTravelEnabled);
        TestEqual(TEXT("Capability stays out of visible chat"),UI->Log.Lines.Num(),Lines);
        Net.Phase=TEXT("selection");Enter(TEXT("/cg should not send"));
        TestTrue(TEXT("No global chat outside world"),Read().IsEmpty());
    }
    FPlatformProcess::ClosePipe(Commands,nullptr);Cast<AAC2HUD>(PC->MyHUD)->GameUI.Reset();Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
