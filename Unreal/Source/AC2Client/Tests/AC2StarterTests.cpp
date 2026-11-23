#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Placement.h"
#include "AC2WorldGrid.h"
#include "AC2WorldStream.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <queue>

#if WITH_DEV_AUTOMATION_TESTS
namespace {
// Audit continuous capsule paths against cooked native geometry. This is not
// a replacement for an interactive playthrough or CharacterMovement simulation.
static bool WalkStarterRoute(UWorld *World, const FVector &Start,
                             const FVector &End, TArray<FVector> &Path,
                             float Radius, float HalfHeight, double Reach = 250,
                             const AActor *Target = nullptr, double Grid = 50) {
  struct FNode {
    FVector Feet, Center;
    int32 Cost, Parent;
  };
  TArray<FNode> Nodes;
  TMap<FIntVector, int32> Visited;
  const auto Key = [&](const FVector &P) {
    const FVector V = (P - Start) / Grid;
    return FIntVector(FMath::RoundToInt(V.X), FMath::RoundToInt(V.Y),
                      FMath::RoundToInt(V.Z));
  };
  FVector Center;
  if (!AC2::StandingPosition(World, Start, Radius, HalfHeight, Center))
    return false;
  Nodes.Add({Center - FVector(0, 0, HalfHeight + 3), Center, 0, -1});
  Visited.Add(Key(Start), 0);
  using FEntry = std::pair<int32, int32>;
  std::priority_queue<FEntry, std::vector<FEntry>, std::greater<FEntry>> Queue;
  Queue.emplace(0, 0);
  FCollisionQueryParams Q(SCENE_QUERY_STAT(AC2StarterWalk), false);
  const auto Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
  while (!Queue.empty() && Nodes.Num() < 600000) {
    const int32 Index = Queue.top().second;
    Queue.pop();
    const FNode Node = Nodes[Index];
    FCollisionQueryParams Sight(SCENE_QUERY_STAT(AC2StarterUse), false, Target);
    if (FVector::Dist(Node.Feet, End) < Reach &&
        !World->LineTraceTestByChannel(Node.Feet + FVector(0, 0, 100),
                                       End + FVector(0, 0, 100), ECC_Pawn,
                                       Sight)) {
      for (int32 I = Index; I >= 0; I = Nodes[I].Parent)
        Path.Insert(Nodes[I].Feet, 0);
      return true;
    }
    for (int32 Step = 1; Step <= 2; ++Step)
      for (int32 X = -1; X <= 1; ++X)
        for (int32 Y = -1; Y <= 1; ++Y) {
          if (X == 0 && Y == 0)
            continue;
          FVector Hint = Node.Feet + FVector(X * Grid * Step, Y * Grid * Step, 0);
          FHitResult Floor;
          if (!World->LineTraceSingleByChannel(Floor, Hint + FVector(0, 0, 60),
                                               Hint - FVector(0, 0, 300),
                                               ECC_Pawn, Q) ||
              Floor.ImpactNormal.Z < .574)
            continue;
          FVector Next;
          if (!AC2::StandingPosition(World, Floor.ImpactPoint, Radius,
                                     HalfHeight, Next))
            continue;
          const FVector Feet = Next - FVector(0, 0, HalfHeight + 3);
          const auto K = Key(Feet);
          const int32 Cost = Node.Cost + Step * (X && Y ? 14 : 10);
          if (const int32 *Existing = Visited.Find(K))
            if (Nodes[*Existing].Cost <= Cost)
              continue;
          // CharacterMovement permits a 60 cm step. Check the lift and the
          // raised capsule segment, so a floor ray cannot walk through walls.
          const FVector Lift(0, 0, 60);
          if (Next.Z < Node.Center.Z - 60) {
            const FVector Ledge(Next.X, Next.Y, Node.Center.Z);
            if (World->SweepTestByChannel(Node.Center, Ledge, FQuat::Identity,
                                          ECC_Pawn, Shape, Q) ||
                World->SweepTestByChannel(Ledge, Next, FQuat::Identity,
                                          ECC_Pawn, Shape, Q))
              continue;
          } else if (World->SweepTestByChannel(Node.Center, Next,
                                               FQuat::Identity, ECC_Pawn, Shape,
                                               Q) &&
                     (World->SweepTestByChannel(Node.Center, Node.Center + Lift,
                                                FQuat::Identity, ECC_Pawn,
                                                Shape, Q) ||
                      World->SweepTestByChannel(Node.Center + Lift, Next + Lift,
                                                FQuat::Identity, ECC_Pawn,
                                                Shape, Q)))
            continue;
          const int32 N = Nodes.Add({Feet, Next, Cost, Index});
          Visited.Add(K, N);
          Queue.emplace(Cost + FMath::RoundToInt(FVector::Dist(Feet, End) / (Grid / 10)),
                        N);
        }
  }
  int32 Closest = 0;
  for (int32 I = 1; I < Nodes.Num(); ++I)
    if (FVector::DistSquared(Nodes[I].Feet, End) <
        FVector::DistSquared(Nodes[Closest].Feet, End))
      Closest = I;
  UE_LOG(LogTemp, Display,
         TEXT("Starter blocked after %d nodes at %s; destination %s"),
         Nodes.Num(), *Nodes[Closest].Feet.ToString(), *End.ToString());
  for (int32 I = Closest; I >= 0; I = Nodes[I].Parent)
    Path.Insert(Nodes[I].Feet, 0);
  return false;
}
// Uses native height/build extremes for both sexes and the same capsule-sizing
// rule as ApplyAvatar. Point anchors also get a conservative 70/165 cm check.
// -AC2StarterProbe reports nearby candidate floors for offline content
// authoring.
class FStarterFloorCommand : public IAutomationLatentCommand {
  FAutomationTestBase *Test;
  UWorld *World = nullptr;
  TUniquePtr<FAC2WorldStream> Stream;
  TUniquePtr<FAC2AssetLibrary> Shard;
  AActor *ShardActor = nullptr;
  float Radius = 24, HalfHeight = 24;
  TArray<TSharedPtr<FJsonValue>> Areas, Results;
  int32 Area = 0;
  double Started = 0;
  bool Probe = FParse::Param(FCommandLine::Get(), TEXT("AC2StarterProbe"));
  void Close() {
    Shard.Reset();
    ShardActor = nullptr;
    Stream.Reset();
    if (World) {
      World->DestroyWorld(false);
      GEngine->DestroyWorldContext(World);
      World = nullptr;
    }
  }

public:
  explicit FStarterFloorCommand(FAutomationTestBase *In) : Test(In) {
    FString Text;
    TSharedPtr<FJsonObject> Root;
    FFileHelper::LoadFileToString(
        Text, *FPaths::ConvertRelativePathToFull(
                  FPaths::ProjectDir() /
                  TEXT("../AC2E/emulator/content/starter-areas-v1.json")));
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),
                                     Root) &&
        Root) {
      Areas = Root->GetArrayField(TEXT("areas"));
      FString AreaFilter;
      if (FParse::Value(FCommandLine::Get(), TEXT("AC2StarterArea="), AreaFilter))
        Areas.RemoveAll([&](const auto &A){return A->AsObject()->GetStringField(TEXT("name")) != AreaFilter;});
      const auto Main = Areas;
      for (const auto &A : Main)
        if (A->AsObject()->HasField(TEXT("vault")))
          Areas.Add(MakeShared<FJsonValueObject>(
              A->AsObject()->GetObjectField(TEXT("vault"))));
    } else
      Test->AddError(TEXT("Starter-area fixture missing"));
  }
  ~FStarterFloorCommand() { Close(); }
  bool Update() override {
    if (Area >= Areas.Num())
      return true;
    auto Data = Areas[Area]->AsObject();
    const auto Start = Data->GetObjectField(TEXT("start"));
    const uint32 Cell = uint32(Start->GetNumberField(TEXT("cell")));
    const FVector Origin = AC2::BlockOrigin(Cell >> 16);
    if (!World) {
      const auto Init = UWorld::InitializationValues()
                            .AllowAudioPlayback(false)
                            .RequiresHitProxies(false)
                            .CreateNavigation(false)
                            .CreateAISystem(false)
                            .ShouldSimulatePhysics(false)
                            .CreateFXSystem(false)
                            .EnableTraceCollision(true);
      World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
                                  true, ERHIFeatureLevel::Num, &Init);
      GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
      Stream = MakeUnique<FAC2WorldStream>();
      FString Error;
      if (!Test->TestTrue(
              TEXT("Starter DAT open"),
              Stream->Open(World, AC2::ResolveDatDirectory(TEXT("")), Error)))
        return true;
      FAC2CharacterCatalog Catalog;
      if (!Test->TestTrue(
              TEXT("Avatar catalog"),
              Catalog.Open(AC2::ResolveDatDirectory(TEXT("")), Error)))
        return true;
      Radius = 24;
      HalfHeight = 24;
      const int32 Race =
          Areas[Area % (Areas.Num() / 2)]->AsObject()->GetIntegerField(TEXT("race"));
      for (const auto &Avatar : Catalog.Avatars)
        if (Avatar.Race == Race) {
          auto *Actor = World->SpawnActor<AActor>();
          auto *Root = NewObject<USceneComponent>(Actor);
          Actor->SetRootComponent(Root);
          Root->RegisterComponent();
          FAC2AssetLibrary Model;
          if (Test->TestTrue(
                  TEXT("Native starter avatar"),
                  Model.Open(AC2::ResolveDatDirectory(TEXT("")), Error) &&
                      Model.DrawModel(Avatar.Physics, Actor, Root,
                                      Avatar.Resolve(Avatar.Defaults())))) {
            const FBox Bounds = Model.ModelBounds(),
                       Feet = Model.ModelBounds(0x81000001);
            const float FootZ = Feet.IsValid ? Feet.Min.Z : Bounds.Min.Z;
            const float R = FMath::Clamp(
                float(FMath::Min(Bounds.GetSize().X, Bounds.GetSize().Y) *
                      Avatar.Builds.Last() * .3),
                24.f, 70.f);
            const float H = FMath::Max(
                R, float((Bounds.Max.Z - FootZ) * Avatar.Heights.Last() * .5));
            Radius = FMath::Max(Radius, R);
            HalfHeight = FMath::Max(HalfHeight, H);
          }
          Actor->Destroy();
        }
      UE_LOG(LogTemp, Display,
             TEXT("Starter capsule %s radius %.2f half-height %.2f"),
             *Data->GetStringField(TEXT("name")), Radius, HalfHeight);
      Stream->SetInteriorCell(Cell);
      Started = FPlatformTime::Seconds();
    }
    Stream->Tick(Origin);
    if (!Stream->AllReady()) {
      if (FPlatformTime::Seconds() - Started < 90)
        return false;
      Test->AddError(TEXT("Starter streaming timeout"));
      return true;
    }
    if (!Shard) {
      for (const auto &Actor : Data->GetArrayField(TEXT("actors"))) {
        auto O = Actor->AsObject();
        if (O->GetStringField(TEXT("role")) != TEXT("shard"))
          continue;
        ShardActor = World->SpawnActor<AActor>();
        auto *Root = NewObject<USceneComponent>(ShardActor);
        ShardActor->SetRootComponent(Root);
        Root->RegisterComponent();
        const auto V = O->GetArrayField(TEXT("position"));
        ShardActor->SetActorLocation(Origin + FVector(V[0]->AsNumber(),
                                                      V[1]->AsNumber(),
                                                      V[2]->AsNumber()) *
                                                  100);
        const auto Q = O->GetArrayField(TEXT("orientation"));
        ShardActor->SetActorRotation(FQuat(Q[1]->AsNumber(), Q[2]->AsNumber(),
                                           Q[3]->AsNumber(), Q[0]->AsNumber()));
        Shard = MakeUnique<FAC2AssetLibrary>();
        FString Error;
        if (!Test->TestTrue(
                TEXT("Shard DAT open"),
                Shard->Open(AC2::ResolveDatDirectory(TEXT("")), Error)) ||
            !Test->TestTrue(
                TEXT("Shard model"),
                Shard->DrawModel(uint32(O->GetNumberField(TEXT("visual"))),
                                 ShardActor, Root)))
          return true;
        Test->TestEqual(TEXT("Vault contains the captured Menhir Shard"),
                        uint32(O->GetNumberField(TEXT("entity"))), 0x4700079au);
        ShardActor->SetActorEnableCollision(Shard->EnableWorldObjectCollision() > 0);
        return false;
      }
    }
    if (Shard && !Shard->CollisionReady())
      return false;
    TArray<TSharedPtr<FJsonValue>> Points = {
        MakeShared<FJsonValueObject>(Start)};
    Points.Append(Data->GetArrayField(TEXT("actors")));
    for (const auto &Point : Points) {
      const auto Object = Point->AsObject();
      FString Role;
      Object->TryGetStringField(TEXT("role"), Role);
      if (Role == TEXT("shard"))
        continue; // Test the player's approach to the shard below.
      const auto XYZ = Object->GetArrayField(TEXT("position"));
      const FVector Hint =
          Origin +
          FVector(XYZ[0]->AsNumber(), XYZ[1]->AsNumber(), XYZ[2]->AsNumber()) *
              100;
      FVector Feet = Hint, Center;
      const TArray<TSharedPtr<FJsonValue>> *Approach = nullptr;
      if (Object->TryGetArrayField(TEXT("approach"), Approach)) {
        const auto &V = *Approach;
        Feet = Origin + FVector(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber()) * 100;
        Test->TestTrue(TEXT("Fixture reachable within the server use radius"), FVector::DistSquared(Hint, Feet) <= FMath::Square(400.0));
      }
      bool Found = AC2::StandingPosition(World, Feet, 70, 165, Center);
      if (!Found) {
        FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(AC2StarterFloorDiagnostic),false);
        if(World->LineTraceSingleByChannel(Floor,Hint+FVector(0,0,500),Hint-FVector(0,0,500),ECC_Pawn,Query))
          UE_LOG(LogTemp,Display,TEXT("Starter anchor %s at %s: nearby floor %s normal %s"),
            *Role,*(Hint-Origin).ToString(),*((Floor.ImpactPoint-Origin)/100).ToString(),*Floor.ImpactNormal.ToString());
      }
      if (Probe && !Found) {
        // Bounded authoring search only; runtime must use the checked position.
        for (int32 Attempt = 0; Attempt < 300 && !Found; ++Attempt) {
          const double R = FMath::Sqrt(double(Attempt)) * .45,
                       A = Attempt * 2.399963;
          FVector Candidate = Hint + FVector(FMath::Cos(A) * R * 100,
                                             FMath::Sin(A) * R * 100, 0);
          FHitResult Hit;
          FCollisionQueryParams Q(SCENE_QUERY_STAT(AC2StarterProbe), false);
          if (World->LineTraceSingleByChannel(
                  Hit, Candidate + FVector(0, 0, 100),
                  Candidate - FVector(0, 0, 1200), ECC_Pawn, Q) &&
              Hit.ImpactNormal.Z > .574) {
            Candidate = Hit.ImpactPoint;
            if (AC2::StandingPosition(World, Candidate, 70, 165, Center)) {
              Found = true;
              Feet = Candidate;
            }
          }
        }
      }
      FString ID = TEXT("start");
      Object->TryGetStringField(TEXT("id"), ID);
      const FString Label = Data->GetStringField(TEXT("name")) + TEXT(" ") + ID;
      if (!Probe)
        Test->TestTrue(*Label, Found);
      auto Row = MakeShared<FJsonObject>();
      Row->SetStringField(TEXT("name"), Label);
      Row->SetBoolField(TEXT("clear"), Found);
      uint32 At = uint32(Object->GetNumberField(TEXT("cell")));
      if (Found)
        Stream->InteriorCellAt(Feet, At);
      Row->SetNumberField(TEXT("cell"), At);
      const FVector Local = (Feet - Origin) / 100;
      Row->SetArrayField(TEXT("position"),
                         {MakeShared<FJsonValueNumber>(Local.X),
                          MakeShared<FJsonValueNumber>(Local.Y),
                          MakeShared<FJsonValueNumber>(Local.Z)});
      Results.Add(MakeShared<FJsonValueObject>(Row));
    }
    if (!Probe) {
      const auto Position = [&](const TSharedPtr<FJsonObject> &O) {
        const auto V = O->GetArrayField(TEXT("position"));
        return Origin +
               FVector(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber()) *
                   100;
      };
      FVector ShortcutFrom = Position(Start);
      for (const FString Role : {TEXT("exit-warning"), TEXT("warning-skull"), TEXT("shortcut-portal")}) {
        for (const auto &Entry : Data->GetArrayField(TEXT("actors"))) {
          const auto Actor = Entry->AsObject();
          if (Actor->GetStringField(TEXT("role")) != Role) continue;
          TArray<FVector> Path;
          const bool Found = WalkStarterRoute(World, ShortcutFrom, Position(Actor), Path, Radius, HalfHeight, 250);
          Test->TestTrue(*(Data->GetStringField(TEXT("name")) + TEXT(" walking route to ") + Role), Found);
          if (Found) ShortcutFrom = Path.Last();
        }
      }
      FVector From = Position(Start);
      TArray<TSharedPtr<FJsonValue>> Walk;
      bool ContinueWalk = true;
      for (const FString Role :
           {TEXT("guard"), TEXT("lieutenant"), TEXT("sentry"), TEXT("digger"),
            TEXT("scout"), TEXT("sentinel"), TEXT("menhir-portal"),
            TEXT("advance"), TEXT("shard")}) {
        if (!ContinueWalk)
          break;
        for (const auto &Actor : Data->GetArrayField(TEXT("actors")))
          if (Actor->AsObject()->GetStringField(TEXT("role")) == Role) {
            const FVector To = Position(Actor->AsObject());
            TArray<FVector> Path;
            const bool Found =
                WalkStarterRoute(World, From, To, Path, Radius, HalfHeight,
                                 Role == TEXT("shard") ? 390 : 250,
                                 Role == TEXT("shard") ? ShardActor : nullptr);
            Test->TestTrue(*(Data->GetStringField(TEXT("name")) +
                             TEXT(" walking route to ") + Role),
                           Found);
            UE_LOG(LogTemp, Display, TEXT("Starter route %s %s: %d samples"),
                   *Data->GetStringField(TEXT("name")), *Role, Path.Num());
            for (const auto &P : Path) {
              const FVector V = (P - Origin) / 100;
              Walk.Add(
                  MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
                      MakeShared<FJsonValueNumber>(V.X),
                      MakeShared<FJsonValueNumber>(V.Y),
                      MakeShared<FJsonValueNumber>(V.Z)}));
            }
            if (!Found) {
              ContinueWalk = false;
              break;
            }
            From = Path.Last();
          }
      }
      auto Row = MakeShared<FJsonObject>();
      Row->SetStringField(TEXT("route"), Data->GetStringField(TEXT("name")));
      Row->SetArrayField(TEXT("walk"), Walk);
      Results.Add(MakeShared<FJsonValueObject>(Row));
      // A player may reach the portal before accepting the Shelter quest.
      // Forward traversal alone permits drops that can strand that player.
      FVector ReturnFrom = From;
      TArray<TSharedPtr<FJsonValue>> ReturnWalk;
      bool ReturnClear = true;
      for (const FString Role : {TEXT("sentinel"), TEXT("scout"), TEXT("digger"),
                                 TEXT("sentry"), TEXT("lieutenant"), TEXT("guard")}) {
        if (!ReturnClear) break;
        for (const auto &Actor : Data->GetArrayField(TEXT("actors")))
          if (Actor->AsObject()->GetStringField(TEXT("role")) == Role) {
            TArray<FVector> Path;
            const bool Found = WalkStarterRoute(World, ReturnFrom, Position(Actor->AsObject()),
                                                Path, Radius, HalfHeight, 250, nullptr, 25);
            Test->TestTrue(*(Data->GetStringField(TEXT("name")) + TEXT(" reverse route to ") + Role), Found);
            for (const FVector &P : Path) {
              const FVector V = (P - Origin) / 100;
              ReturnWalk.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
                  MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y),
                  MakeShared<FJsonValueNumber>(V.Z)}));
            }
            if (Found) ReturnFrom = Path.Last();
            else ReturnClear = false;
          }
      }
      auto ReturnRow = MakeShared<FJsonObject>();
      ReturnRow->SetStringField(TEXT("route"), Data->GetStringField(TEXT("name")) + TEXT(" reverse"));
      ReturnRow->SetArrayField(TEXT("walk"), ReturnWalk);
      Results.Add(MakeShared<FJsonValueObject>(ReturnRow));
      const TArray<TSharedPtr<FJsonValue>> *Recovery = nullptr;
      if (Data->TryGetArrayField(TEXT("recovery"), Recovery))
        for (const auto &Point : *Recovery) {
          TArray<FVector> Path;
          Test->TestTrue(*(Data->GetStringField(TEXT("name")) +
                           TEXT(" return from ") +
                           Point->AsObject()->GetStringField(TEXT("name"))),
                         WalkStarterRoute(World, Position(Point->AsObject()),
                                          From, Path, Radius, HalfHeight));
        }
    }
    Close();
    ++Area;
    if (Area == Areas.Num()) {
      FString Text;
      FJsonSerializer::Serialize(Results, TJsonWriterFactory<>::Create(&Text));
      FFileHelper::SaveStringToFile(
          Text, *(FPaths::ProjectSavedDir() / TEXT("starter-clearance.json")));
      return true;
    }
    return false;
  }
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStarterFloorTest, "AC2.Data.StarterClearance",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FStarterFloorTest::RunTest(const FString &) {
  ADD_LATENT_AUTOMATION_COMMAND(FStarterFloorCommand(this));
  return true;
}
#endif
