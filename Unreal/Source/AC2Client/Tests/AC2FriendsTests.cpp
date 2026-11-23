#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2FriendsTest,"AC2.Network.Friends",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2FriendsTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=TEXT("101");Player.Name=TEXT("Tester");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);auto& Net=*Mode->Network;
    Net.Phase=TEXT("world");Net.PlayerId=Player.Id;Net.FriendsEnabled=true;
    void* Commands=nullptr;
    if(TestTrue(TEXT("Capture bridge IPC"),FPlatformProcess::CreatePipe(Commands,Net.Write,true))){
        const auto UI=SNew(SAC2GameUI).Mode(Mode);Cast<AAC2HUD>(PC->MyHUD)->GameUI=UI;
        UI->Native.RootOffsets.Reset();UI->Native.RootSizes.Reset();
        const auto Geometry=FGeometry::MakeRoot(FVector2D(1280,800),FSlateLayoutTransform());
        UI->Tick(Geometry,FPlatformTime::Seconds(),.016f);
        auto Read=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Receive=[&](const TCHAR* Json){
            TSharedPtr<FJsonObject> J;
            if(TestTrue(TEXT("Incoming friend event parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Json)),J)))Net.Handle(J);
            UI->Rebuild();
        };
        UI->ShowSocial=true;UI->SocialAction(TEXT("SocialFriends"));UI->RaiseWindow(FAC2UILayout::Token(TEXT("SocialField")));UI->Rebuild();
        TestTrue(TEXT("Opening Friends requests authoritative list"),Read().Contains(TEXT("\"action\":\"list\"")));
        Receive(TEXT(R"({"event":"friends","members":[{"name":"Zephyr","mode":0,"location":""},{"name":"Ada","mode":11,"location":"Dereth"}]})"));
        Receive(TEXT(R"({"event":"friend_mode","mode":1})"));
        TestTrue(TEXT("Sorted roster"),Net.Friends.Num()==2 && Net.Friends[0].Name==TEXT("Ada"));
        TestTrue(TEXT("Online and offline entries use native row hit regions"),UI->Buttons.Contains(TEXT("Friend:Ada")) && UI->Buttons.Contains(TEXT("Friend:Zephyr")));
        TestTrue(TEXT("Away friend uses native away name color"),UI->Native.Nodes.ContainsByPredicate([](const auto& Node){
            return Node.Text==TEXT("Ada") && Node.State==FAC2UILayout::Token(TEXT("awayfromkeyboard"));}));
        TestTrue(TEXT("Availability menu resolves its DAT label and hit region"),!UI->FriendModeAnchor.IsEmpty() &&
            UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text==TEXT("Available");}));
        UI->Mouse=FVector2D(UI->FriendModeAnchor.Min+UI->FriendModeAnchor.Size()/2);
        TestTrue(TEXT("Click opens the native status menu"),UI->OptionChoiceMouseDown(EKeys::LeftMouseButton));
        if(TestEqual(TEXT("Three available presence modes"),UI->OptionChoiceItems.Num(),3)){
            const auto Row=UI->OptionChoiceItems[2];UI->Mouse=FVector2D(Row.Min+Row.Size()/2);
            UI->OptionChoiceMouseDown(EKeys::LeftMouseButton);UI->OptionChoiceMouseUp(EKeys::LeftMouseButton);
            TestTrue(TEXT("DND mode selection sends flags"),Read().Contains(TEXT("\"mode\":4")));
            TestEqual(TEXT("Wait for server confirmation"),Net.FriendMode,1u);
            Receive(TEXT(R"({"event":"friend_mode","mode":4})"));
            TestTrue(TEXT("Acknowledged availability shown"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text==TEXT("Do Not Disturb");}));
        }
        auto Target=MakeUnique<FAC2Network::FObject>();Target->Entity=Net.SelectionLook(0x47000531).Category==AC2Radar::Players?0x47000531:0x4700110e;
        Target->Name=TEXT("Bea");Net.TargetId=TEXT("102");Net.Objects.Add(Net.TargetId,MoveTemp(Target));UI->Rebuild();
        if(TestTrue(TEXT("Add enabled for player target"),UI->ButtonActions.FindKey(TEXT("FriendAdd"))!=nullptr)){
            UI->SocialAction(TEXT("FriendAdd"));const FString Added=Read();
            TestTrue(TEXT("Selected player name sent to add"),Added.Contains(TEXT("\"action\":\"add\"")) && Added.Contains(TEXT("\"name\":\"Bea\"")));
        }
        UI->SocialAction(TEXT("Friend:Ada"));UI->SocialAction(TEXT("FriendRemove"));
        const FString Removed=Read();TestTrue(TEXT("Remove sends selected character name"),Removed.Contains(TEXT("\"action\":\"remove\"")) && Removed.Contains(TEXT("\"name\":\"Ada\"")));
        Receive(TEXT(R"({"event":"friend_status","name":"ADA","mode":0,"location":""})"));
        TestTrue(TEXT("Logout clears friend location and changes display group"),Net.Friends.Num()==2 && Net.Friends[0].Mode==0 && Net.Friends[0].Location.IsEmpty() &&
            !UI->Native.Find(TEXT("OnlineFriendsMarker")));
        Receive(TEXT(R"({"event":"friend_error","message":"Unknown player"})"));
        TestEqual(TEXT("Server rejection shown"),Mode->Notice,FString(TEXT("Unknown player")));
        UI->Submit(TEXT("/friend add Other Player"));TestTrue(TEXT("By-name add supports spaces"),Read().Contains(TEXT("Other Player")));
        Receive(TEXT(R"({"event":"friends","members":[]})"));
        TestTrue(TEXT("Server refresh removes stale selection"),UI->SelectedFriend.IsEmpty() && !UI->Buttons.Contains(TEXT("Friend:Ada")));
        Net.FriendsEnabled=false;UI->SocialAction(TEXT("SocialFriends"));UI->Submit(TEXT("/friend add Ada"));
        TestTrue(TEXT("Direct-login worlds never send AC2E friend requests"),Read().IsEmpty() && UI->FriendModeAnchor.IsEmpty());
        Net.ClearWorld();TestTrue(TEXT("Logout resets roster and flags"),!Net.FriendsReceived && Net.Friends.IsEmpty() && Net.FriendMode==0);
    }
    FPlatformProcess::ClosePipe(Commands,nullptr);Cast<AAC2HUD>(PC->MyHUD)->GameUI.Reset();Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
