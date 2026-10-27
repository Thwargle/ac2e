#include "AC2Assets.h"
#include "AC2CharacterData.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2PreviewLightingTest, "AC2.Data.NativePreviewLightingAndAppearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2PreviewLightingTest::RunTest(const FString&)
{
    const FString Directory = AC2::ResolveDatDirectory(TEXT(""));
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Portrait = World->SpawnActor<AAC2Portrait>();
    TestFalse(TEXT("Native RGB is not tone mapped"), Portrait->Capture->ShowFlags.Tonemapper);
    TestFalse(TEXT("Exposure cannot change between preview screens"), Portrait->Capture->ShowFlags.EyeAdaptation);
    FString Error;
    {
        FAC2AssetLibrary SceneAssets;
        SceneAssets.SetPreviewLighting({}, FTransform::Identity);
        FAC2UISceneInfo Selection, Lineup, Human;
        if (TestTrue(TEXT("Open preview DATs"), SceneAssets.Open(Directory, Error)))
        {
            TestTrue(TEXT("Selection scene decoded"), SceneAssets.DrawUIScene(0x21000001, Portrait, Portrait->Stage, &Selection));
            TestEqual(TEXT("No-light selection uses native default sun"), Selection.Lighting.Lights.Num(), 1);
            if (!Selection.Lighting.Lights.IsEmpty())
                TestTrue(TEXT("Selection default light direction"), Selection.Lighting.Lights[0].Directional &&
                    Selection.Lighting.Lights[0].Direction.Equals(FVector(.3, -.9, -1).GetSafeNormal()));
            TestTrue(TEXT("Lineup scene decoded"), SceneAssets.DrawUIScene(0x2100000b, Portrait, Portrait->Stage, &Lineup));
            TestEqual(TEXT("Three authored lineup lights"), Lineup.Lighting.Lights.Num(), 3);
            if (Lineup.Lighting.Lights.Num() == 3)
            {
                const auto& Key = Lineup.Lighting.Lights[2];
                TestTrue(TEXT("Native key light position, range and intensity"), Key.Position.Equals(FVector(209,321,259), .01) &&
                    FMath::IsNearlyEqual(Key.Range,3000.f) && FMath::IsNearlyEqual(Key.Intensity,1.25f));
                TestTrue(TEXT("Native packed color is not gamma-decoded"), Key.Diffuse.Equals(FColor(0xffffbd63).ReinterpretAsLinear()));
            }
            TestTrue(TEXT("Human scene decoded"), SceneAssets.DrawUIScene(0x21000007, Portrait, Portrait->Stage, &Human));
            TestEqual(TEXT("All four human lights retained"), Human.Lighting.Lights.Num(), 4);
            Human.Lighting.FollowCamera(FVector(0,-1,0));
            const auto* Camera = Human.Lighting.Lights.FindByPredicate([](const auto& L){ return L.Name == TEXT("Light_1"); });
            TestTrue(TEXT("Creator applies the native downward camera light"), Camera && Camera->Directional &&
                Camera->Direction.Equals(FVector(0,-1,-.5).GetSafeNormal()));
            TestTrue(TEXT("Native camera ambient"), FMath::IsNearlyEqual(Human.Lighting.Ambient.R,.2f));
        }
    }
    FAC2DatArchive Portal; FAC2CharacterCatalog Catalog; TArray<uint8> Bytes;
    if (TestTrue(TEXT("Creation matrix loaded"), Portal.Open(Directory / TEXT("portal.dat"),1,Error) &&
        Portal.Read(0x70000390,Bytes,Error) && Catalog.DecodeMatrix(Bytes)))
    {
        const uint32 Visuals[] = {0x1f000023,0x1f000024,0x1f000025,0x1f000079,0x1f000026,0x1f00007a};
        for (int32 I=0; I<6; ++I)
            for (bool Last : {false,true})
            {
                const auto& A = Catalog.Avatars[I];
                auto Choices = A.Defaults();
                if (Last) for (auto& C : Choices) C.Value = A.Choices[C.Key].Num()-1;
                auto Creation = A.Resolve(Choices);
                auto Login = Creation;
                // The server must retain explicit zero traits. Native hashes
                // arrive in a different order, which must not change the colors.
                Login.Sort([](const auto& L,const auto& R){return L.Table==R.Table ? L.Key<R.Key : L.Table<R.Table;});
                auto* First = World->SpawnActor<AAC2Portrait>();
                auto* Second = World->SpawnActor<AAC2Portrait>();
                FAC2AssetLibrary Creator, LoggedIn;
                if (TestTrue(TEXT("Both preview appearances load"), Creator.Open(Directory,Error) && LoggedIn.Open(Directory,Error) &&
                    Creator.DrawModel(A.Physics,First,First->Body,Creation) && LoggedIn.DrawModel(Visuals[I],Second,Second->Body,Login)))
                    TestEqual(FString::Printf(TEXT("Avatar %d %s colors survive native login"),I,Last?TEXT("last"):TEXT("default")),
                        LoggedIn.AppearanceFingerprint(),Creator.AppearanceFingerprint());
                First->Destroy(); Second->Destroy();
            }
    }
    TestTrue(TEXT("Selection render target created"),Portrait->ShowSelection(Directory,{{0x1f000023,1,1,{}}},0,5));
    if(Portrait->Image)
    {
        TestEqual(TEXT("Final LDR target uses byte sRGB format"),Portrait->Image->GetFormat(),PF_B8G8R8A8);
        TestFalse(TEXT("Slate target does not force linear gamma"),Portrait->Image->bForceLinearGamma);
        TestEqual(TEXT("Display gamma retained through capture"),Portrait->Image->TargetGamma,2.2f);
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
