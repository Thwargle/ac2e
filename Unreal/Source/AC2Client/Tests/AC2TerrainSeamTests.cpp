#include "AC2Terrain.h"
#include "AC2Assets.h"
#include "AC2WorldGrid.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Tests/AutomationCommon.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TerrainSeamTest,"AC2.Data.TerrainSharedEdges",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TerrainSeamTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open reported terrain DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)))return false;
    const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,0x86b0,2);
    TMap<uint32,FAC2TerrainMesh> Meshes;
    for(uint32 Id:{0x85b0u,0x86b0u,0x85b1u,0x86b1u})
    {
        const auto* Land=Neighborhood.Find(Id);
        if(!TestNotNull(TEXT("Read neighboring seam block"),Land))return false;
        const auto Old=BuildAC2TerrainMesh(Defs,*Land);
        auto Mesh=BuildAC2TerrainMesh(Defs,*Land,&Neighborhood);
        TestTrue(TEXT("Authored terrain positions are unchanged"),Mesh.Vertices==Old.Vertices);
        TestTrue(TEXT("Collision topology and cave holes are unchanged"),Mesh.Indices==Old.Indices);
        Meshes.Add(Id,MoveTemp(Mesh));
    }
    for(uint32 Id:{0x85b0u,0x86b0u,0x85b1u})
        for(uint32 Delta:{0x100u,1u})if(const auto* Next=Meshes.Find(Id+Delta))
            for(int32 I=0;I<17;++I)
            {
                const auto& Here=Meshes[Id];
                const int32 A=Delta==0x100?16*17+I:I*17+16;
                const int32 B=Delta==0x100?I:I*17;
                TestTrue(TEXT("Shared edge positions coincide"),
                    (AC2::BlockOrigin(Id)+Here.Vertices[A]).Equals(AC2::BlockOrigin(Id+Delta)+Next->Vertices[B],.001));
                TestTrue(TEXT("Shared lighting and slope selection normals coincide"),Here.Normals[A].Equals(Next->Normals[B],1.e-8));
            }
    // Independent application of the recovered CacheFarValues unit-face fan
    // to these DAT heights, including the western and northwestern neighbours.
    TestTrue(TEXT("Reported seam uses retail's complete triangle fan"),Meshes[0x86b0].Normals[14].Equals(
        FVector(-.5142972835,.0229585832,.8573046177),1.e-7));
    TestTrue(TEXT("Four-block corner includes the diagonal neighbor"),Meshes[0x86b0].Normals[16].Equals(
        FVector(-.2361623792,.3546069364,.9046995365),1.e-7));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2TerrainSamplerTest,"AC2.Data.TerrainAtlasClamping",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2TerrainSamplerTest::RunTest(const FString&)
{
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2AssetLibrary Assets;
    TArray<uint8> Bytes;FString Error;
    if(!TestTrue(TEXT("Open terrain assets"),Assets.Open(Dir,Error)
        && Portal.Open(Dir/TEXT("portal.dat"),1,Error) && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)))return false;
    const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,0x86b0,1);
    const auto* Land=Neighborhood.Find(0x86b0);
    if(!TestNotNull(TEXT("Load reported seam"),Land))return false;
    const auto Mesh=BuildAC2TerrainMesh(Defs,*Land,&Neighborhood);
    auto CheckSampler=[&](UMaterialInterface* Material) {
        UTexture* Texture=nullptr;
        if(!TestTrue(TEXT("Material binds its terrain atlas"),Material &&
            Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Albedo")),Texture)))return;
        const auto* Atlas=Cast<UTexture2D>(Texture);
        if(!TestNotNull(TEXT("Bound atlas is a 2D texture"),Atlas))return;
        TestEqual(TEXT("East/west edges never sample the opposite side of the block"),int32(Atlas->AddressX),int32(TA_Clamp));
        UTexture* Shadow=nullptr;
        TestTrue(TEXT("The surface descriptor supplies a cloud mask"),Material->GetTextureParameterValue(
            FMaterialParameterInfo(TEXT("AC2CloudShadow")),Shadow) && Shadow && Shadow->GetPackage()==GetTransientPackage());
        FLinearColor Motion;
        TestTrue(TEXT("Cloud scale and drift match the stock DAT"),Material->GetVectorParameterValue(
            FMaterialParameterInfo(TEXT("AC2CloudMotion")),Motion) && Motion.Equals(FLinearColor(.001f,-.006f,-.002f,1.f),.000001f));
        TestEqual(TEXT("North/south edges never sample the opposite side of the block"),int32(Atlas->AddressY),int32(TA_Clamp));
    };
    CheckSampler(Assets.TerrainMaterial(Defs,*Land,Mesh));
    if(!TestTrue(TEXT("Prepare streamed terrain"),Assets.PrepareBlock(Dir,Defs,*Land,Mesh,Error)))return false;
    CheckSampler(Assets.UploadTerrain());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CloudTravelTest,"AC2.Render.TerrainCloudTravel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2CloudTravelTest::RunTest(const FString&)
{
    if (GUsingNullRHI) { AddError(TEXT("Cloud travel needs a graphics device")); return false; }
    const FString Dir = AC2::ResolveDatDirectory(TEXT(""));
    FAC2DatArchive Portal, Cell;
    FAC2LandscapeDefs Defs;
    FAC2AssetLibrary Assets;
    TArray<uint8> Bytes;
    FString Error;
    if (!TestTrue(TEXT("Open stock terrain"), Assets.Open(Dir,Error) && Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error) && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error))) return false;
    constexpr uint32 Block = 0x86b0;
    const auto Neighborhood = LoadAC2TerrainNeighborhood(Cell,Block,1);
    const auto* Land = Neighborhood.Find(Block);
    if (!TestNotNull(TEXT("Landblock for fixed-view render"), Land)) return false;
    const auto Data = BuildAC2TerrainMesh(Defs,*Land,&Neighborhood);
    auto* Material = Assets.TerrainMaterial(Defs,*Land,Data);
    if (!TestNotNull(TEXT("Native terrain material"), Material)) return false;

    auto* World = UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    auto* Ground = World->SpawnActor<AActor>();
    auto* Mesh = NewObject<UProceduralMeshComponent>(Ground);
    Ground->SetRootComponent(Mesh);
    Mesh->RegisterComponent();
    Mesh->CreateMeshSection(0,Data.Vertices,Data.Indices,Data.Normals,Data.UVs,{}, {},false);
    Mesh->SetMaterial(0,Material);
    Ground->SetActorLocation(AC2::BlockOrigin(Block));
    auto* Sun = World->SpawnActor<ADirectionalLight>();
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetIntensity(4.f);
    Sun->SetActorRotation(FRotator(-75,45,0));
    auto* Camera = World->SpawnActor<AActor>();
    auto* Capture = NewObject<USceneCaptureComponent2D>(Camera);
    Camera->SetRootComponent(Capture);
    Capture->RegisterComponent();
    Capture->bCaptureEveryFrame = Capture->bCaptureOnMovement = false;
    Capture->SetWorldLocationAndRotation(Mesh->Bounds.Origin + FVector(0,0,26000), FRotator(-90,0,0));
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = 15000;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetTonemapper(false);
    auto* Image = NewObject<UTextureRenderTarget2D>(Camera);
    Image->InitCustomFormat(256,256,PF_B8G8R8A8,false);
    Image->UpdateResourceImmediate(true);
    Capture->TextureTarget = Image;
    const auto First = MakeShared<TArray<FColor>>();
    const FString Output = FPaths::ProjectSavedDir()/TEXT("Screenshots/TerrainCloudTravel");
    IFileManager::Get().MakeDirectory(*Output,true);
#if WITH_EDITOR
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture]() {
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.5f));
    // Lighting, textures, and camera are fixed. Rewinding the material clock
    // must restore the original spatial pattern on the same ground pixels.
    for (int32 Pass = 0; Pass < 3; ++Pass)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World,Capture,Pass]() {
            World->TimeSeconds = Pass == 1 ? 30 : 0;
            Capture->CaptureScene(); return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,Pass,Image,First,Output]() {
            TArray<FColor> Pixels;
            if (!TestTrue(TEXT("Read fixed terrain view"), Image->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)
                && Pixels.Num() == 256*256)) return true;
            for (auto& P : Pixels) P.A = 255;
            if (Pass == 0) *First = Pixels;
            else if (Pixels.Num() == First->Num())
            {
                int32 Brighter = 0, Darker = 0;
                for (int32 I = 0; I < Pixels.Num(); ++I)
                {
                    const int32 Delta = int32(Pixels[I].G) - (*First)[I].G;
                    if (Delta >= 3) ++Brighter;
                    if (Delta <= -3) ++Darker;
                }
                AddInfo(FString::Printf(TEXT("Pass %d of %d ground pixels: %d brightened, %d darkened"),Pass,Pixels.Num(),Brighter,Darker));
                if (Pass == 1) TestTrue(TEXT("Clouds move into and out of different areas"), Brighter > 256 && Darker > 256);
                else TestTrue(TEXT("The original shadow pattern returns when time is restored"), Brighter+Darker < 128);
            }
            TArray64<uint8> PNG;
            FImageUtils::PNGCompressImageArray(256,256,Pixels,PNG);
            TestTrue(TEXT("Save test view"),FFileHelper::SaveArrayToFile(PNG,*(Output/FString::Printf(TEXT("phase-%d.png"),Pass))));
            return true;
        }));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([World]() {
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
    }));
    return true;
}
#endif
