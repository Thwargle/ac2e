#include "AC2NativeUI.h"
#include "AC2Audio.h"
#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2InteractionAudioTest,"AC2.Data.InteractionAudio",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2InteractionAudioTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    const FString Directory=AC2::ResolveDatDirectory(TEXT(""));
    Mode->DatDirectory=Directory;
    auto* PC=World->SpawnActor<APlayerController>();auto* Player=World->SpawnActor<AAC2Explorer>();PC->Possess(Player);
    TestTrue(TEXT("Tumerok pickup model loads"),Player->ApplyAvatar(0x1f000025,1,1));
    Player->SetNativeMode(0x41000010);
    {
        FAC2Audio Audio;
        if(TestTrue(TEXT("Native audio engine opens"),Audio.Open(World,Directory))) {
            for(uint32 I=0;I<4;++I)Audio.SetVolume(I,0); // Request routing, without speaker output.
            FAC2NativeUI UI;FString Error;
            TestTrue(TEXT("Native layout opens"),UI.Open(Directory,Error));UI.SetAudioWorld(World);
            FAC2AssetLibrary Assets;FAC2Fonts Fonts;
            TestTrue(TEXT("Asset catalogue"),Assets.Open(Directory,Error));
            TestTrue(TEXT("Font catalogue"),Fonts.Open(Directory,Error));
            auto T=[](const TCHAR* Name){return FAC2UILayout::Token(Name);};
            auto Build=[&](){UI.Begin({1280,720});UI.Root(0x22000005,TEXT("ExternalContainerField"));};
            Build();UI.Prepare(Assets,Fonts);
            for(const TCHAR* Name:{TEXT("LootAllButton"),TEXT("TransmuteAllButton")}) {
                const uint32 Id=T(Name),Before=Audio.Plays;
                const auto* N=UI.Find(Name);
                if(!TestNotNull(TEXT("Native action button"),N))continue;
                TestTrue(TEXT("DAT action has a sound"),!N->PressSounds.IsEmpty());
                UI.Press(Id);UI.Pressed=0; // A complete click between paints.
                TestEqual(TEXT("Quick click triggers exactly one cue"),Audio.Plays,Before+1);
                Build();UI.Prepare(Assets,Fonts);
                TestEqual(TEXT("Following paint does not replay quick click"),Audio.Plays,Before+1);
                UI.Press(Id);UI.Press(Id);Build();UI.Prepare(Assets,Fonts);
                TestEqual(TEXT("Held click and its paint trigger once"),Audio.Plays,Before+2);
                UI.Pressed=0;UI.States.Add(Id,T(TEXT("ghosted")));Build();
                UI.Press(Id);TestEqual(TEXT("Disabled action stays silent"),Audio.Plays,Before+2);
                UI.Pressed=0;UI.States.Remove(Id);Build();UI.Prepare(Assets,Fonts);
            }
            FAC2Network Net(Mode);Net.Phase=TEXT("world");Net.LootItems={TEXT("42")};Net.LootContainer=TEXT("41");
            auto Reply=MakeShared<FJsonObject>();Reply->SetStringField(TEXT("event"),TEXT("inventory_result"));
            Reply->SetStringField(TEXT("id"),TEXT("42"));Reply->SetNumberField(TEXT("slot"),0);
            const uint32 Before=Audio.Plays;
            Net.TakeLoot(0);TestEqual(TEXT("Pickup waits for acknowledgement"),Audio.Plays,Before);
            Reply->SetNumberField(TEXT("status"),54);Net.Handle(Reply);
            TestEqual(TEXT("Rejected pickup stays silent"),Audio.Plays,Before);
            TestFalse(TEXT("Rejected pickup does not animate a successful reach"),Player->HandlingItem());
            Net.TakeLoot(0);Reply->SetNumberField(TEXT("status"),0);Net.Handle(Reply);
            TestEqual(TEXT("Successful pickup plays the loot cue"),Audio.Plays,Before+1);
            TestTrue(TEXT("Confirmed pickup starts the native reach"),Player->HandlingItem());
            TestEqual(TEXT("Pickup preserves authoritative weapon mode"),Player->GetNativeMode(),0x41000010u);
            const uint64 PickupInstance=Player->BehaviorInstance(AC2Motion::Pickup);
            Net.Handle(Reply);TestEqual(TEXT("Repeated acknowledgement stays silent"),Audio.Plays,Before+1);
            TestEqual(TEXT("Duplicate reply does not restart the reach"),Player->BehaviorInstance(AC2Motion::Pickup),PickupInstance);
            Net.PlayerId=TEXT("17");Reply->SetStringField(TEXT("event"),TEXT("inventory_acquired"));
            Reply->SetStringField(TEXT("id"),TEXT("43"));Reply->SetStringField(TEXT("owner"),TEXT("18"));
            Reply->SetStringField(TEXT("from"),TEXT("0"));Net.Handle(Reply);
            TestEqual(TEXT("Other player's acquisition is silent"),Audio.Plays,Before+1);
            Reply->SetStringField(TEXT("owner"),TEXT("17"));Reply->SetNumberField(TEXT("status"),54);Net.Handle(Reply);
            TestEqual(TEXT("Failed ground pickup is silent"),Audio.Plays,Before+1);
            Reply->SetNumberField(TEXT("status"),0);Net.Handle(Reply);Net.Handle(Reply);
            TestEqual(TEXT("Ground pickup plays once"),Audio.Plays,Before+2);
            Reply->SetStringField(TEXT("event"),TEXT("inventory_remove"));Net.Handle(Reply);
            Reply->SetStringField(TEXT("event"),TEXT("inventory_acquired"));Net.Handle(Reply);
            TestEqual(TEXT("Picking up a dropped item plays again"),Audio.Plays,Before+3);

            // The stock reedshark idle (0500011C) has eight guaranteed breath cues per cycle.
            auto* Creature=World->SpawnActor<AActor>();
            auto* Body=NewObject<USceneComponent>(Creature);
            Creature->SetRootComponent(Body);Body->RegisterComponent();
            {
                FAC2AssetLibrary Model;
                if(TestTrue(TEXT("Reedshark model loads"),Model.Open(Directory,Error) && Model.DrawModel(0x1f000030,Creature,Body)))
                {
                    const uint32 Start=Audio.Plays;
                    for(int32 Frame=1;Frame<=70;++Frame)Model.Animate(Frame*.1f,0);
                    TestTrue(TEXT("DAT idle contains frequent sound requests"),Audio.Plays-Start>=6);
                    Model.EnableCreatureIdleAudio();
                    const uint32 AmbientStart=Audio.Plays;
                    for(int32 Frame=71;Frame<=140;++Frame)Model.Animate(Frame*.1f,0);
                    TestTrue(TEXT("Monster idle spaces ambient one-shots"),Audio.Plays-AmbientStart<=2);
                    const uint32 DirectStart=Audio.Plays;
                    Model.PlayEffect(0x810000d4);Model.PlayEffect(0x810000d4);
                    TestEqual(TEXT("Explicit FX retain native playback outside the idle track"),Audio.Plays,DirectStart+2);
                }
            }
            Creature->Destroy();

            // Exercise the DLL -> Unreal procedural PCM boundary without a live
            // server or speaker playback. The test world disables audio playback.
            USoundWaveProcedural* Output=nullptr;
            for(TObjectIterator<UAudioComponent> It;It;++It)if(It->GetWorld()==World)
                if(auto* Wave=Cast<USoundWaveProcedural>(It->GetSound()))Output=Wave;
            if(TestNotNull(TEXT("Native mixer is connected to Unreal audio"),Output))
            {
                Audio.SetVolume(0,1);Audio.Play(0x2a0000ee);bool Audible=false;int32 MaxQueued=0;
                for(int32 I=0;I<30;++I)
                {
                    FPlatformProcess::Sleep(.005f);Audio.Tick(I*.02);
                    MaxQueued=FMath::Max(MaxQueued,Output->GetAvailableAudioByteCount());
                    while(Output->GetAvailableAudioByteCount()>0)
                    {
                        int16 PCM[1024]={};const int32 Bytes=Output->GeneratePCMData(reinterpret_cast<uint8*>(PCM),1024);
                        if(Bytes<=0)break;
                        for(int32 S=0;S<Bytes/int32(sizeof(int16));++S)Audible|=PCM[S]!=0;
                    }
                }
                TestTrue(TEXT("Native pickup produces audible PCM in Unreal's output queue"),Audible);
                TestTrue(TEXT("PCM buffering remains bounded to 100 ms"),MaxQueued>0 && MaxQueued<=4410*4);
            }
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
