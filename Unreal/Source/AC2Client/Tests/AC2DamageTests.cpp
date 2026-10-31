#include "AC2Assets.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "AC2Network.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void FinishDamageShaders()
{
#if WITH_EDITOR
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2DamageNetworkTest,"AC2.Data.NetworkDamageAppearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2DamageNetworkTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();
    if(!TestNotNull(TEXT("Isolated network world"),Mode))
    {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return false;}
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* Controller=World->SpawnActor<APlayerController>();
    auto* Player=World->SpawnActor<AAC2Explorer>();Controller->Possess(Player);
    TestTrue(TEXT("Network resolves isolated player controller"),World->GetFirstPlayerController()==Controller && Controller->GetPawn()==Player);
    FAC2CharacterProfile Profile;Profile.Id=TEXT("101");
    Mode->Characters.Characters.Add(Profile);Mode->Characters.ActiveId=Profile.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);
    auto& Network=*Mode->Network;Network.PlayerId=Profile.Id;
    FAC2AssetLibrary::SetEffectChannel(0x40000001,4);
    FAC2AssetLibrary::SetEffectChannel(0x40000007,2);
    auto Event=[](const TCHAR* Type,const TCHAR* Id){auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);return J;};
    auto Visual=MakeShared<FJsonObject>();Visual->SetNumberField(TEXT("did"),0x1f000023);
    auto Create=[&](const TCHAR* Id,bool WithVisual){auto J=Event(TEXT("object"),Id);
        J->SetNumberField(TEXT("instance"),1);
        auto Pose=MakeShared<FJsonObject>();Pose->SetNumberField(TEXT("cell"),0x56670001);
        J->SetObjectField(TEXT("placement"),Pose);if(WithVisual)J->SetObjectField(TEXT("visual"),Visual);
        Network.Handle(J);};
    auto Quality=[&](const TCHAR* Id,uint32 Key,float Value){auto J=Event(TEXT("quality"),Id);
        J->SetNumberField(TEXT("table"),4);J->SetNumberField(TEXT("key"),Key);J->SetNumberField(TEXT("value"),Value);
        Network.Handle(J);};
    auto Rebuild=[&](const TCHAR* Id){auto J=Event(TEXT("visual"),Id);J->SetObjectField(TEXT("visual"),Visual);Network.Handle(J);};
    auto Hash=[&](const TCHAR* Id){auto O=Network.Objects.Find(Id);return O&&(*O)->Model?(*O)->Model->DamageAppearanceFingerprint():0;};
    Create(TEXT("101"),true);Create(TEXT("202"),true);
    TestTrue(TEXT("Local native model loaded"),Player->HasModel());
    const uint32 Clean=Player->DamageAppearanceFingerprint();
    TestNotEqual(TEXT("Model has material bindings"),Clean,0u);
    TestEqual(TEXT("Local and remote clean appearance agrees"),Hash(TEXT("202")),Clean);
    Quality(TEXT("101"),0x107,100);Quality(TEXT("101"),0x102,45);
    // Current and maximum may arrive in either order after the object create.
    Quality(TEXT("202"),0x102,45);
    TestEqual(TEXT("Unknown maximum does not fabricate a wound stage"),Hash(TEXT("202")),Clean);
    Quality(TEXT("202"),0x107,100);
    const uint32 Injured=Player->DamageAppearanceFingerprint();
    TestNotEqual(TEXT("Own health update changes appearance"),Injured,Clean);
    TestEqual(TEXT("Remote player health shows same wounds"),Hash(TEXT("202")),Injured);
    Rebuild(TEXT("101"));Rebuild(TEXT("202"));
    TestEqual(TEXT("Own wounds survive appearance/equipment update"),Player->DamageAppearanceFingerprint(),Injured);
    TestEqual(TEXT("Remote wounds survive appearance/equipment update"),Hash(TEXT("202")),Injured);
    Create(TEXT("303"),false);Quality(TEXT("303"),0x107,100);Quality(TEXT("303"),0x102,45);Rebuild(TEXT("303"));
    TestEqual(TEXT("Health snapshot before model creation is replayed"),Hash(TEXT("303")),Injured);
    auto FX=Event(TEXT("fx"),TEXT("202"));FX->SetNumberField(TEXT("fx"),0x41000002);
    FX->SetNumberField(TEXT("instance"),1);FX->SetNumberField(TEXT("strength"),1);
    Network.Handle(FX);const uint32 Dead=Hash(TEXT("202"));
    TestNotEqual(TEXT("Native server FX selects maximum wounds"),Dead,Injured);
    Rebuild(TEXT("202"));TestEqual(TEXT("Explicit native wound scalar survives re-skin"),Hash(TEXT("202")),Dead);
    FX->SetStringField(TEXT("event"),TEXT("stop_fx"));FX->SetNumberField(TEXT("instance"),0);Network.Handle(FX);
    TestEqual(TEXT("Stale instance cannot remove current wounds"),Hash(TEXT("202")),Dead);
    FX->SetNumberField(TEXT("instance"),1);Network.Handle(FX);
    TestEqual(TEXT("Native StopFX restores base materials"),Hash(TEXT("202")),Clean);
    Quality(TEXT("101"),0x102,100);Quality(TEXT("202"),0x102,100);
    TestEqual(TEXT("Local healing removes wounds"),Player->DamageAppearanceFingerprint(),Clean);
    TestEqual(TEXT("Remote healing removes wounds"),Hash(TEXT("202")),Clean);
    Quality(TEXT("101"),0x102,0);Quality(TEXT("202"),0x102,0);
    TestEqual(TEXT("Local death has final wounds"),Player->DamageAppearanceFingerprint(),Dead);
    TestEqual(TEXT("Remote death has final wounds"),Hash(TEXT("202")),Dead);
    Quality(TEXT("101"),0x102,100);Quality(TEXT("202"),0x102,100);
    TestEqual(TEXT("Respawn clears persistent wounds"),Hash(TEXT("202")),Clean);
    Quality(TEXT("101"),0x102,15);Network.ClearWorld();
    TestEqual(TEXT("Logout removes wounds from retained pawn"),Player->DamageAppearanceFingerprint(),Clean);
    TestEqual(TEXT("Logout clears previous character maximum"),Network.MaxHealth,0.f);
    Mode->Network.Reset();Controller->UnPossess();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2DamageRenderTest,"AC2.Render.ProgressiveDamageAppearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2DamageRenderTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("Damage render test requires an active graphics device"));return false;}
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    FAC2AssetLibrary::SetEffectChannel(0x40000001,4);FAC2AssetLibrary::SetEffectChannel(0x40000007,2);
    const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots/DamageAppearance");
    IFileManager::Get().MakeDirectory(*Output,true);
    struct FRenderState {TWeakObjectPtr<AAC2Portrait> Portrait;TArray<FColor> Clean,Previous;};
    auto State=MakeShared<FRenderState>();
    for(uint32 Visual:{0x1f000023u,0x1f000036u})
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,State,Visual](){
            auto* Portrait=World->SpawnActor<AAC2Portrait>();State->Portrait=Portrait;
            State->Clean.Reset();State->Previous.Reset();
            TestTrue(TEXT("Render native damage model"),Portrait->Show(AC2::ResolveDatDirectory(TEXT("")),Visual,1,1));
            Portrait->Capture->bCaptureEveryFrame=false;Portrait->Capture->bCaptureOnMovement=false;
            return true;
        }));
        int32 Step=0;
        for(float Health:{100.f,60.f,45.f,30.f,15.f,0.f,100.f})
        {
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,State,Health](){
                // Present the same native idle pose and UV animation time in
                // every frame, including the initial clean-model capture.
                State->Portrait->Tick(0);
                State->Portrait->SetHealth(Health,100);
                FinishDamageShaders();
                World->SendAllEndOfFrameUpdates();return true;
            }));
            // Allow render-thread proxy/uniform updates between material edits,
            // captures and readback, as the running game does each frame.
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Portrait->Capture->CaptureScene();return true;}));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){
                // First use of a preview permutation can request its shader
                // during capture. Finish that work before the measured image.
                FinishDamageShaders();State->Portrait->Capture->CaptureScene();return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State,Output,Visual,Step,Health](){
                auto* Portrait=State->Portrait.Get();TArray<FColor> Pixels;
                TestTrue(TEXT("Read native wound rendering"),Portrait->Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
                int32 Lit=0,Different=0;
                for(int32 I=0;I<Pixels.Num();++I)
                {
                    auto& Pixel=Pixels[I];Pixel.A=255;
                    if(Pixel.R+Pixel.G+Pixel.B>30)++Lit;
                    if(State->Previous.IsValidIndex(I) && Pixel!=State->Previous[I])++Different;
                }
                TestTrue(TEXT("Character is visible in damage render"),Lit>1000);
                if(!Step)State->Clean=Pixels;
                else if(Health==100)
                {
                    int32 Changed=0;
                    for(int32 I=0;I<Pixels.Num();++I)if(State->Clean.IsValidIndex(I))
                        Changed+=FMath::Max3(FMath::Abs(int32(Pixels[I].R)-State->Clean[I].R),FMath::Abs(int32(Pixels[I].G)-State->Clean[I].G),FMath::Abs(int32(Pixels[I].B)-State->Clean[I].B))>3;
                    TestTrue(FString::Printf(TEXT("%08X rendered healing restores appearance (%d changed pixels)"),Visual,Changed),Changed<10);
                }
                else TestTrue(TEXT("Next native wound stage changes visible pixels"),Different>50);
                TArray<uint8> PNG;FImageUtils::CompressImageArray(Portrait->Image->SizeX,Portrait->Image->SizeY,Pixels,PNG);
                TestTrue(TEXT("Save wound comparison"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("%08X-%d-health%.0f.png"),Visual,Step,Health))));
                State->Previous=MoveTemp(Pixels);return true;
            }));
            ++Step;
        }
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State](){State->Portrait->Destroy();return true;}));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World](){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;}));
    return true;
}
#endif
