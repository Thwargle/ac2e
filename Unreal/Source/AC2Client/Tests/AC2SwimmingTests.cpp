#include "AC2Assets.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Locomotion.h"
#include "AC2Network.h"
#include "AC2NativeMaterial.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace {
void FinishSwimShaders() {
#if WITH_EDITOR
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
// Compare the actual rendered multipart meshes, in the body's native frame.
TArray<FVector> SwimVertices(AActor* Actor,USceneComponent* Body) {
    TArray<FVector> Out;TInlineComponentArray<UProceduralMeshComponent*> Meshes(Actor);
    for(auto* Mesh:Meshes)for(int32 I=0;I<Mesh->GetNumSections();++I)
        if(auto* S=Mesh->GetProcMeshSection(I))for(const auto& V:S->ProcVertexBuffer)
            Out.Add(Body->GetComponentTransform().InverseTransformPosition(Mesh->GetComponentTransform().TransformPosition(V.Position)));
    return Out;
}
double SwimDifference(const TArray<FVector>& A,const TArray<FVector>& B) {
    if(A.IsEmpty() || A.Num()!=B.Num())return MAX_dbl;
    double Largest=0;for(int32 I=0;I<A.Num();++I)Largest=FMath::Max(Largest,FVector::Dist(A[I],B[I]));
    return Largest;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SwimmingClipsTest,"AC2.Data.NativeSwimmingClips",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SwimmingClipsTest::RunTest(const FString&)
{
    FAC2DatArchive Portal;FString Error;TArray<uint8> Bytes;FAC2BehaviorData Behaviors;
    if(!TestTrue(TEXT("Open swim DATs"),Portal.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"))/TEXT("portal.dat"),1,Error)))return false;
    if(!TestTrue(TEXT("Read human behaviors"),Portal.Read(0x44000001,Bytes,Error)&&Behaviors.Decode(0x44000001,Bytes)))return false;
    // EnumMapper 23000048 and the human water graphs distinguish peace from
    // riding. These exact clips catch the former 'Swim = 40000003' regression.
    for(uint32 Requested:{AC2Motion::Peace,AC2Motion::Melee,0x41000011u})
    {
        FAC2AnimatorInput Input;Input.Element=AC2Motion::Water;
        Input.Mode=AC2Locomotion::AnimationMode(Input.Element,Requested);
        TestEqual(TEXT("Unmounted water uses peace mode"),Input.Mode,0x40000001u);
        for(uint32 Behavior:{0x40000002u,AC2Motion::Move})
        {
            auto Graph=FAC2Animator::Load(Portal,0x03000014,Behaviors.Key(Input.Element,Input.Mode,Behavior));
            if(!TestTrue(TEXT("Native water graph exists"),Graph.IsValid()))continue;
            for(float Forward:{-1.f,0.f,1.5f,3.f})for(float Side:{-1.f,0.f,1.f})
            {
                Input.Forward=Forward;Input.Side=Side;
                const auto Clips=Graph->ActiveClips(Input);
                TestTrue(TEXT("Swimming has authored clips"),!Clips.IsEmpty());
                for(uint32 Clip:Clips)TestTrue(TEXT("Water selects swimming, never mounted clips"),
                    Behavior==AC2Motion::Move ? Clip==0x05000323||Clip==0x050002dd||Clip==0x05000345 : Clip==0x05000280);
            }
            Input.Mode=AC2Motion::Riding;
            const auto Riding=Graph->ActiveClips(Input);
            TestTrue(TEXT("Old incorrect mode selects a distinct mounted pose"),Riding.Contains(0x05000ebe)||Riding.Contains(0x05000ebf));
            Input.Mode=AC2Locomotion::AnimationMode(Input.Element,Requested);
        }
        TestEqual(TEXT("Leaving water restores requested stance"),AC2Locomotion::AnimationMode(AC2Motion::Ground,Requested),Requested);
        TestEqual(TEXT("Wading keeps requested stance"),AC2Locomotion::AnimationMode(AC2Motion::SeaFloor,Requested),Requested);
    }
    TestEqual(TEXT("Water cannot replace death with idle swimming"),AC2Locomotion::AnimationMode(AC2Motion::Water,AC2Motion::Dead),AC2Motion::Dead);
    return true;
}

// Exercise the local controller and the remote observer's network mode handler
// with the same native body and feet depth. No production account/server needed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2SwimmingRenderTest,"AC2.Render.NativeSwimmingObservers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2SwimmingRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Swimming render test requires a graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    if(!TestNotNull(TEXT("Isolated swim world"),Mode)){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;}
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    auto* Controller=World->SpawnActor<APlayerController>();
    auto* Player=World->SpawnActor<AAC2Explorer>();Controller->Possess(Player);
    Mode->Network=MakeUnique<FAC2Network>(Mode);Mode->Network->PlayerId=TEXT("101");
    struct FState {AActor* Remote=nullptr;USceneCaptureComponent2D* Camera=nullptr;UTextureRenderTarget2D* Image=nullptr;
        float Time=0;TArray<FVector> Idle;};
    auto State=MakeShared<FState>();
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/Swimming");IFileManager::Get().MakeDirectory(*Output,true);
    auto* CameraActor=World->SpawnActor<AActor>();State->Camera=NewObject<USceneCaptureComponent2D>(CameraActor);
    CameraActor->SetRootComponent(State->Camera);State->Camera->RegisterComponent();
    State->Camera->bCaptureEveryFrame=false;State->Camera->bCaptureOnMovement=false;
    State->Camera->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    State->Camera->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;State->Camera->FOVAngle=50;
    State->Camera->ShowFlags.SetMotionBlur(false);State->Camera->ShowFlags.SetEyeAdaptation(false);
    State->Camera->ShowFlags.SetTonemapper(false);State->Camera->ShowFlags.SetFog(false);
    State->Image=NewObject<UTextureRenderTarget2D>(CameraActor);State->Image->ClearColor=FLinearColor(.025f,.04f,.06f);
    State->Image->InitCustomFormat(640,480,PF_B8G8R8A8,false);State->Image->UpdateResourceImmediate(true);State->Camera->TextureTarget=State->Image;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mode,Player,State](){
        auto& Network=*Mode->Network;
        for(const TCHAR* Id:{TEXT("101"),TEXT("202")})
        {
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("object"));J->SetStringField(TEXT("id"),Id);
            J->SetNumberField(TEXT("instance"),1);auto Placement=MakeShared<FJsonObject>();Placement->SetNumberField(TEXT("cell"),0x6ead0001);
            J->SetObjectField(TEXT("placement"),Placement);auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),0x1f000023);
            J->SetObjectField(TEXT("visual"),Visual);Network.Handle(J);
        }
        auto* Object=Network.Objects.Find(TEXT("202"));
        if(!TestTrue(TEXT("Local and remote native bodies loaded"),Player->HasModel()&&Object&&(*Object)->Model))return true;
        auto& Remote=**Object;State->Remote=Remote.Actor.Get();
        Player->SetActorHiddenInGame(false);Player->SetHeld(false);
        Player->SetActorRotation(FRotator(0,90,0)); // Native +Y forward, same as observer body.
        const double Height=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2;
        const double Feet=-Height*Remote.Model->MotionData().Water-5;
        Player->SetActorLocation(FVector(0,0,Feet+Height*.5));
        State->Remote->SetActorLocation(FVector(0,0,Feet));State->Remote->SetActorRotation(FRotator::ZeroRotator);
        Remote.Element=AC2Locomotion::Element(true,-Feet,Remote.BodyHeight,Remote.Model->MotionData(),false,AC2Motion::Ground);
        TestEqual(TEXT("Replicated feet select water"),Remote.Element,AC2Motion::Water);
        Player->UpdateWater(true,0);TestTrue(TEXT("Local controller enters swimming at the same depth"),Player->IsSwimming());
        const FVector Eye(330,280,130),Target(0,0,-25);
        State->Camera->SetWorldLocationAndRotation(Eye,(Target-Eye).Rotation());
        return true;
    }));
    for(int32 Stage=0;Stage<4;++Stage)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Mode,Player,World,State,Stage](){
            if(!State->Remote)return true;
            auto& Remote=*Mode->Network->Objects[TEXT("202")];
            const FVector Velocity=Stage==1?FVector(0,250,0):Stage==2?FVector(0,-100,0):FVector::ZeroVector;
            if(Stage==3){Player->UpdateWater(false,0);Remote.Element=AC2Motion::Ground;Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);}
            for(int32 Step=0;Step<40;++Step)
            {
                // Repeated combat-mode packets must not restore riding/armed
                // locomotion while submerged, or prevent stance restoration ashore.
                for(const TCHAR* Id:{TEXT("101"),TEXT("202")}){
                    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("event"),TEXT("mode"));J->SetStringField(TEXT("id"),Id);
                    J->SetNumberField(TEXT("mode"),AC2Motion::Melee);Mode->Network->Handle(J);
                }
                Player->GetCharacterMovement()->Velocity=Velocity;Player->Tick(.05f);
                State->Time+=.05f;Remote.Model->Animate(State->Time,Velocity.Size2D(),nullptr,&Velocity);
            }
            const auto LocalPose=SwimVertices(Player,Player->Body),RemotePose=SwimVertices(State->Remote,Remote.Body.Get());
            TestTrue(TEXT("Local player and observer render the same multipart pose"),SwimDifference(LocalPose,RemotePose)<.1);
            if(Stage==0)State->Idle=LocalPose;
            else TestTrue(TEXT("Stroke or restored land stance differs from water idle"),SwimDifference(LocalPose,State->Idle)>1);
            TestTrue(TEXT("Attachments remain joined while swimming"),Player->AuditAttachments(Mode->Notice)&&Remote.Model->AuditAttachments(Mode->Notice));
            if(Stage==3)TestFalse(TEXT("Leaving water returns to land controller"),Player->IsSwimming());
            FAC2PreviewLighting Lighting;Lighting.Ambient=FLinearColor(.65f,.65f,.65f);
            UpdateAC2WorldLighting(World,Lighting,[](const FVector&,FAC2PreviewLighting&){return false;});
            FinishSwimShaders();World->SendAllEndOfFrameUpdates();return true;
        }));
        for(bool Local:{true,false}){
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Player,State,Local](){
                State->Camera->ShowOnlyActors.Reset();State->Camera->ShowOnlyActorComponents(Local?Player:State->Remote);return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Camera->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){FinishSwimShaders();State->Camera->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Stage,Local](){
                TArray<FColor> Pixels;TestTrue(TEXT("Read swim render"),State->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
                int32 Visible=0;for(auto& Pixel:Pixels){Pixel.A=255;if(Pixel.R>100 && Pixel.G>60)++Visible;}
                TestTrue(TEXT("Swimmer is visible"),Visible>500);
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,480,Pixels,PNG);
                const FString Name=FString::Printf(TEXT("%s-%s.png"),Local?TEXT("local"):TEXT("observer"),Stage==0?TEXT("idle"):Stage==1?TEXT("forward"):Stage==2?TEXT("backward"):TEXT("land"));
                TestTrue(TEXT("Save swim render"),FFileHelper::SaveArrayToFile(PNG,*(Output/Name)));return true;
            }));
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Mode,Controller,World](){Mode->Network.Reset();Controller->UnPossess();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
