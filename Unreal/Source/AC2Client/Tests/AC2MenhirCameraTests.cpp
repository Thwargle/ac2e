#include "AC2Assets.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "AC2WorldGrid.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MenhirShardNetworkCameraTest, "AC2.Render.MenhirShardNetworkCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAC2MenhirShardNetworkCameraTest::RunTest(const FString&)
{
    struct FProfile { const TCHAR* Name; const TCHAR* Folder; uint32 Cell; };
    const FProfile Profiles[] = {
        {TEXT("AC2E"), TEXT("../Asheron's Call 2 - Legions"), 0x03fd0154},
        {TEXT("Icefire"), TEXT("../Saved/IcefireClient"), 0x03f40154}
    };
    constexpr float Step = 1.f / 30;
    const FString ShardId = TEXT("9313500000000000001");
    for (const FProfile& Profile : Profiles)
    {
        const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Profile.Folder);
        if (Profile.Cell == 0x03f40154 && !FPaths::FileExists(Dir / TEXT("portal.dat")))
        {
            AddInfo(TEXT("Optional Icefire DAT fixture is not installed."));
            continue;
        }
        const FString Prefix = FString(Profile.Name) + TEXT(" ");
        const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->GetWorldSettings()->DefaultGameMode = AAC2GameMode::StaticClass();
        World->SetGameMode(FURL());
        World->InitializeActorsForPlay(FURL());
        auto* Mode = World->GetAuthGameMode<AAC2GameMode>();
        auto* Controller = World->SpawnActor<APlayerController>();
        auto* Player = World->SpawnActor<AAC2Explorer>();
        Controller->Possess(Player);
        ON_SCOPE_EXIT {
            Mode->Network.Reset();
            Mode->LeaveWorld();
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        };

        Mode->ShowLogin();
        Mode->DatDirectory = Dir;
        Mode->ViewRadius = 0;
        Mode->Network = MakeUnique<FAC2Network>(Mode);
        auto& Net = *Mode->Network;
        Net.Phase = TEXT("world");
        Net.PlayerId = TEXT("101");
        Net.Cell = Profile.Cell;
        const FVector ShardLocation = AC2::BlockOrigin(Profile.Cell >> 16) + FVector(18685.1562, 19605.7251, 2219.07558);
        Net.Feet = ShardLocation + FVector(200, 0, 0);
        Net.HasPlayerPose = Net.NeedsPlacement = true;
        const double Deadline = FPlatformTime::Seconds() + 30;
        while (!Mode->CellVisible(Profile.Cell) && FPlatformTime::Seconds() < Deadline)
        {
            Mode->Tick(Step);
            FPlatformProcess::Sleep(.001f);
        }
        if (!TestTrue(Prefix + TEXT("streams the Menhir vault cell"), Mode->CellVisible(Profile.Cell))) continue;

        auto AddShard = [&](int32 Instance)
        {
            auto Object = MakeUnique<FAC2Network::FObject>();
            Object->Entity = 0x4700079a;
            Object->Placed = true;
            Object->Instance = Instance;
            Object->Cell = Profile.Cell;
            Object->Goal = ShardLocation;
            Object->Rotation = FRotator(0, 37, 0).Quaternion();
            auto Visual = MakeShared<FJsonObject>();
            Visual->SetNumberField(TEXT("did"), 0x1f00031c);
            Net.ApplyVisual(*Object, Visual, false);
            Net.Objects.Add(ShardId, MoveTemp(Object));
        };
        AddShard(7);
        auto& Shard = *Net.Objects[ShardId];
        if (!TestNotNull(Prefix + TEXT("loads the native shard rig"), Shard.Model.Get())) continue;
        Net.Tick(Step);
        Net.SelectTarget(ShardId);
        if (!TestEqual(Prefix + TEXT("can select visible shard"), Net.TargetId, ShardId)) continue;

        auto View = [&]()
        {
            FMinimalViewInfo Result;
            Player->CalcCamera(Step, Result);
            return Result;
        };
        const auto Default = View();
        auto IsPlayerView = [&](const FMinimalViewInfo& Camera)
        {
            return Default.Location.Equals(Camera.Location, .1) && Default.Rotation.Equals(Camera.Rotation, .1);
        };
        auto Activate = [&](const FString& Target)
        {
            auto Event = MakeShared<FJsonObject>();
            Event->SetStringField(TEXT("event"), TEXT("fx"));
            Event->SetStringField(TEXT("id"), ShardId);
            Event->SetNumberField(TEXT("instance"), 7);
            Event->SetNumberField(TEXT("fx"), 0x41000044);
            Event->SetNumberField(TEXT("strength"), 1);
            if (!Target.IsEmpty()) Event->SetStringField(TEXT("target"), Target);
            Net.Handle(Event);
        };
        Activate(TEXT("202"));
        Net.Tick(Step);
        TestTrue(Prefix + TEXT("another player's shard activation leaves the selected player's view alone"), IsPlayerView(View()));

        Net.UseTarget();
        Activate(TEXT("202"));
        Net.Tick(Step);
        TestTrue(Prefix + TEXT("an explicitly addressed remote activation cannot consume a pending local use"),
            IsPlayerView(View()) && Net.PendingShardUse == ShardId);
        Activate(FString()); // The captured object FX is broadcast without a recipient.
        FVector FirstEye;
        double MaxTravel = 0;
        int32 MatchingSamples = 0;
        for (int32 Frame = 0; Frame < 90; ++Frame)
        {
            Net.Tick(Step);
            FTransform EyeMount, TargetMount;
            if (!Shard.Model->AnimatedHoldingLocation(0x40000004, EyeMount) ||
                !Shard.Model->AnimatedHoldingLocation(0x40000005, TargetMount)) continue;
            const FTransform& Body = Shard.Body->GetComponentTransform();
            const FVector Eye = Body.TransformPosition(EyeMount.GetLocation());
            const FVector Target = Body.TransformPosition(TargetMount.GetLocation());
            const auto Camera = View();
            if (Camera.Location.Equals(Eye, .1) &&
                FVector::DotProduct(Camera.Rotation.Vector(), (Target - Eye).GetSafeNormal()) > .999)
                ++MatchingSamples;
            if (Frame == 0) FirstEye = Camera.Location;
            MaxTravel = FMath::Max(MaxTravel, FVector::Distance(FirstEye, Camera.Location));
        }
        TestEqual(Prefix + TEXT("all sampled pawn views follow the two animated world-space camera mounts"), MatchingSamples, 90);
        TestTrue(Prefix + TEXT("camera travels during the native swirl"), MaxTravel > 50.);
        auto Remove = [&](int32 Instance)
        {
            auto Event = MakeShared<FJsonObject>();
            Event->SetStringField(TEXT("event"), TEXT("remove"));
            Event->SetStringField(TEXT("id"), ShardId);
            Event->SetNumberField(TEXT("instance"), Instance);
            Net.Handle(Event);
        };
        Remove(6);
        TestTrue(Prefix + TEXT("a stale removal preserves the active shard view"), Net.Objects.Contains(ShardId) && !IsPlayerView(View()));
        Remove(7);
        TestFalse(Prefix + TEXT("the active shard was removed"), Net.Objects.Contains(ShardId));
        if (Profile.Cell == 0x03f40154) AddShard(8); // Same IID can identify a new incarnation before the next frame.
        TestTrue(Prefix + TEXT("destroying or replacing the shard restores the original camera"), IsPlayerView(View()));
    }
    return !HasAnyErrors();
}
#endif
