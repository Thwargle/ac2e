#include "AC2GameMode.h"
#include "AC2Performance.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "AC2Explorer.h"
#include "AC2VaultFixture.h"
#include "AC2GameUI.h"
#include "AC2WorldGrid.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "ImageUtils.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "ProceduralMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Reproducible DAT scenery views. No account service or persistent character store.
void AAC2GameMode::EnvironmentSmokeTick(float Delta)
{
    if(AC2Performance::Enabled())
    {
        SmokeTime+=Delta;
        if(SmokeTime>180)
        {
            UE_LOG(LogTemp,Error,TEXT("AC2 performance: world/shaders did not become ready in 180 seconds"));
            FPlatformMisc::RequestExitWithStatus(false,1);
            return;
        }
        // Stand still in the requested block, wait for streaming/shaders, then
        // measure real rendered frames without altering placement or detail.
#if WITH_EDITOR
        if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling())return;
#endif
        if(bWaiting || !Stream->AllReady()) { StageTime=0; return; }
        if(auto* PC=GetWorld()->GetFirstPlayerController())
            if(auto* P=Cast<AAC2Explorer>(PC->GetPawn()))P->SetHeld(true);
        StageTime+=Delta;
        if(StageTime<8) { AC2Performance::Reset(); return; }
        ++SmokeStage;
        AC2Performance::Add(TEXT("frame"),Delta*1000.);
        AC2Performance::Add(TEXT("game_thread"),FPlatformTime::ToMilliseconds(GGameThreadTime));
        AC2Performance::Add(TEXT("render_thread"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
        AC2Performance::Add(TEXT("gpu"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
        AC2Performance::Add(TEXT("draw_calls"),GNumDrawCallsRHI[0]);
        if(StageTime<23)return;
        uint32 ProfileBlock=InitialBlock;
        AC2::WorldBlock(StreamFocus,ProfileBlock);
        const FString Report=FString::Printf(TEXT("block=%04X frames=%d grass_batches=%d\n"),ProfileBlock,SmokeStage,Stream->GrassBatchCount())+AC2Performance::Report(SmokeStage);
        FString Path=FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Performance.txt");
        FParse::Value(FCommandLine::Get(),TEXT("AC2PerformanceReport="),Path);
        FFileHelper::SaveStringToFile(Report,*Path);
        UE_LOG(LogTemp,Display,TEXT("AC2 performance:\n%s"),*Report);
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
#if WITH_EDITOR
    if(GShaderCompilingManager && GShaderCompilingManager->IsCompiling())return;
#endif
    SmokeTime += Delta; StageTime += Delta;
    if (EnvironmentModel) {
        const double ModelTime=FParse::Param(FCommandLine::Get(),TEXT("AC2OutdoorShadowSmoke"))?0:SmokeTime;
        EnvironmentModel->Animate(ModelTime,0); EnvironmentModel->UpdateMaterials(ModelTime);
    }
    auto Finish = [&](bool OK) {
        EnvironmentModel.Reset();
        FFileHelper::SaveStringToFile(OK ? TEXT("PASS") : TEXT("FAIL: environment streaming timed out"),
            *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/AC2Environment-smoke.txt")));
        bEnvironmentSmoke = false;
        FPlatformMisc::RequestExitWithStatus(false, OK ? 0 : 1);
    };
    if (SmokeTime > 240)
    {
        UE_LOG(LogTemp,Error,TEXT("AC2 environment smoke timed out at stage %d: %s. %s; far %04X, focus %s"),
            SmokeStage,*Notice,*Stream->Status(),Far?Far->Center():MAX_uint32,*StreamFocus.ToCompactString());
        Finish(false);
        return;
    }
    auto *PC = GetWorld()->GetFirstPlayerController();
    // Sample the same bank twice within a wave cycle to check its direction.
    const bool SecondCoastFrame = SmokeStage == 2 && FParse::Param(FCommandLine::Get(), TEXT("AC2WaterDistanceSmoke"));
    const bool WindmillInMotion = SmokeStage >= 2 && FParse::Param(FCommandLine::Get(), TEXT("AC2WindmillSmoke"));
    if (!PC || bWaiting || !Stream->AllReady() || (!WindmillInMotion && StageTime < (SecondCoastFrame ? .4f : 3.f))) return;
    auto *P = Cast<AAC2Explorer>(PC->GetPawn());
    if (P) { P->SetActorHiddenInGame(true); P->SetHeld(true); }
    if (auto *H = Cast<AAC2HUD>(PC->GetHUD()))
    { H->bShowHUD=false; if(H->GameUI) H->GameUI->SetVisibility(EVisibility::Collapsed); }
    auto View = [&](FVector Eye, FVector Target, float FieldOfView=60.f) {
        if (!EnvironmentCamera.IsValid())
        {
            auto *Camera = GetWorld()->SpawnActor<ACameraActor>();
            Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
            EnvironmentCamera=Camera;
        }
        EnvironmentCamera->SetActorLocationAndRotation(Eye, (Target-Eye).Rotation());
        PC->SetViewTarget(EnvironmentCamera.Get());
    };
    auto Capture = [&](const TCHAR *Name) {
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots"),Name),false,false);
    };
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2OutdoorShadowSmoke")))
    {
        // Regression at the reported Lost Wish coordinates, far from UE's
        // origin. Contact shadows alone cannot satisfy the cast-shadow check.
        static TWeakObjectPtr<UStaticMeshComponent> Probe;
        Sky->TimeScale=0;
        const FVector Place=AC2::BlockOrigin(0x8eb6)+FVector(3804,7067,10750);
        auto Compare=[&](const TCHAR* Name){
            // Offscreen viewports expose their texture only during the render
            // callback. Use completed engine screenshots, never ReadPixels in Tick.
            FImage A,B;
            const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots");
            if(!FImageUtils::LoadImage(*(Directory/Name),A)||
               !FImageUtils::LoadImage(*(Directory/TEXT("AC2Shadow-unshadowed.png")),B)||
               A.SizeX!=B.SizeX||A.SizeY!=B.SizeY)return false;
            A.ChangeFormat(ERawImageFormat::BGRA8,EGammaSpace::sRGB);B.ChangeFormat(ERawImageFormat::BGRA8,EGammaSpace::sRGB);
            const auto Shadowed=A.AsBGRA8(),Lit=B.AsBGRA8();
            int32 Changed=0;
            for(int32 I=0;I<Lit.Num();++I)
                if(int32(Lit[I].R)+Lit[I].G+Lit[I].B-int32(Shadowed[I].R)-Shadowed[I].G-Shadowed[I].B>45)++Changed;
            UE_LOG(LogTemp,Display,TEXT("AC2 outdoor shadow probe: %d darkened pixels at stage %d"),Changed,SmokeStage);
            return Changed>100;
        };
        switch(SmokeStage++) {
        case 0:
            if(!LoadBlock(0x8eb6)){Finish(false);return;}
            Sky->SetHour(10);break;
        case 1: {
            View(Place+FVector(900,-1500,450),Place+FVector(0,0,80));
            auto* Caster=GetWorld()->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Caster);
            Caster->SetRootComponent(Root);Root->RegisterComponent();Caster->SetActorLocation(Place);
            EnvironmentModel=MakeUnique<FAC2AssetLibrary>();FString Error;EnvironmentModel->Open(DatDirectory,Error);
            EnvironmentModel->DrawModel(0x1f000d8c,Caster,Root);EnvironmentProp=Caster;
            auto* Cube=GetWorld()->SpawnActor<AActor>();auto* Mesh=NewObject<UStaticMeshComponent>(Cube);Cube->SetRootComponent(Mesh);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetMobility(EComponentMobility::Movable);Mesh->RegisterComponent();
            Cube->SetActorLocation(Place+FVector(400,0,150));Cube->SetActorScale3D(FVector(1,1,3));Probe=Mesh;
            break;
        }
        case 2: case 7:
            Capture(SmokeStage==3?TEXT("AC2Shadow-morning.png"):TEXT("AC2Shadow-moved.png"));break;
        case 3: case 8: Probe->SetCastShadow(false);break;
        case 4: case 9: Capture(TEXT("AC2Shadow-unshadowed.png"));break;
        case 5: case 10:
            if(!Compare(SmokeStage==6?TEXT("AC2Shadow-morning.png"):TEXT("AC2Shadow-moved.png")))
            {UE_LOG(LogTemp,Error,TEXT("Outdoor cast shadow missing at native world coordinates"));Finish(false);return;}
            Probe->SetCastShadow(true);break;
        case 6: View(Place+FVector(900,-1800,450),Place+FVector(0,0,80));break;
        case 11: Sky->SetHour(17);View(Place+FVector(-900,-1500,450),Place+FVector(0,0,80));break;
        case 12: Capture(TEXT("AC2Shadow-evening.png"));break;
        default: Probe.Reset();Finish(true);return;
        }
        StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2LightingSmoke")))
    {
        const FVector Origin=AC2::BlockOrigin(0x03fd);
        Sky->TimeScale=0;
        switch(SmokeStage++) {
        case 0: LoadCell(0x03fd0120);break;
        case 1: View(Origin+FVector(-22150,25500,-7050),Origin+FVector(-23230,24735,-6700));break;
        case 2: Capture(TEXT("AC2Lighting-native-ramp.png"));break;
        case 3: View(Origin+FVector(-22800,26000,-5200),Origin+FVector(-25200,28500,-7300));break;
        case 4: Capture(TEXT("AC2Lighting-dungeon.png"));break;
        case 5: if(!LoadBlock(0x77b8)){Finish(false);return;}Sky->SetHour(10);break;
        case 6: View(AC2::BlockOrigin(0x77b8)+FVector(-700,7900,18700),AC2::BlockOrigin(0x77b8)+FVector(1325,10716,18550));break;
        case 7: Capture(TEXT("AC2Lighting-outdoors.png"));break;
        default: Finish(true);return;
        }
        StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2StarterSmoke")))
    {
        const FVector Origin=AC2::BlockOrigin(0x03fd);
        switch(SmokeStage++)
        {
        case 0: LoadCell(0x03fd0120);break;
        case 1: View(Origin+FVector(-20300,23500,-5050),Origin+FVector(-22800,26500,-6500));break;
        case 2: Capture(TEXT("AC2Starter-descent.png"));break;
        case 3: View(Origin+FVector(-22800,26000,-5200),Origin+FVector(-25200,28500,-7300));break;
        case 4: Capture(TEXT("AC2Starter-lower.png"));break;
        case 5: View(Origin+FVector(-24700,25300,-6900),Origin+FVector(-22753,26496,-6200));break;
        case 6: Capture(TEXT("AC2Starter-stair.png"));break;
        case 7: LoadCell(0x03fd0132);break;
        case 8: View(Origin+FVector(-34530,36830,-6200),Origin+FVector(-32000,36830,-6000));break;
        case 9: Capture(TEXT("AC2Starter-tower-landing.png"));break;
        case 10: View(Origin+FVector(-32500,37500,-6000),Origin+FVector(-35400,37600,-6000));break;
        case 11: Capture(TEXT("AC2Starter-tower.png"));break;
        default: Finish(true);return;
        }
        StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2AcademySmoke")))
    {
        const FVector Origin=AC2::BlockOrigin(0x03fd);
        switch(SmokeStage++)
        {
        case 0: LoadCell(0x03fd0111);break;
        case 1: View(Origin+FVector(-450,-400,-2170),Origin+FVector(227,286,-2290));break;
        case 2: Capture(TEXT("AC2Academy-brazier.png"));break;
        case 3: LoadCell(0x03fd012a);break;
        case 4: View(Origin+FVector(-28500,34550,-7140),Origin+FVector(-29100,35150,-7200));break;
        case 5: Capture(TEXT("AC2Academy-mushrooms.png"));break;
        case 6: LoadCell(0x03fd0132);break;
        case 7: View(Origin+FVector(-34100,36200,-7100),Origin+FVector(-35400,37530,-7000));break;
        case 8: Capture(TEXT("AC2Academy-stairs.png"));break;
        default: Finish(true);return;
        }
        StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainSeamSmoke")))
    {
        if(!Far || Far->Center()!=InitialBlock)return;
        if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainOnly")) || FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainFlat")))
            for(TActorIterator<AActor> It(GetWorld());It;++It)
            {
                TArray<UProceduralMeshComponent*> Components;It->GetComponents(Components);
                for(auto* C:Components)
                    if(!C->ComponentHasTag(TEXT("AC2TerrainSurface")))C->SetVisibility(false);
                    else if(FParse::Param(FCommandLine::Get(),TEXT("AC2TerrainFlat")))
                        if(auto* M=Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
                            M->SetTextureParameterValue(TEXT("Albedo"),LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
            }
        Sky->TimeScale=0;
        // Reported seam: 86B0, world 21440.5, 28301.5 metres (west tile edge).
        const FVector Seam(2144050,2830150,0);
        double Ground=0;
        if(!Stream->GroundAt(Seam+FVector(0,0,1000000),Ground)){Finish(false);return;}
        FString Label=TEXT("terrain-seam");
        FParse::Value(FCommandLine::Get(),TEXT("AC2TerrainCapture="),Label);
        switch(SmokeStage)
        {
        case 0: View(Seam+FVector(400,-700,Ground+350),Seam+FVector(-400,1700,Ground-50));break;
        case 1: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-north.png")));break;
        case 2: View(Seam+FVector(1700,-400,Ground+1800),Seam+FVector(0,0,Ground));break;
        case 3: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-overhead.png")));break;
        default: Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2FoliageContactSmoke")))
    {
        // Use the actual uploaded outdoor frills and the normal local-player
        // update path. This requires neither a server nor a character account.
        if(!P){Finish(false);return;}
        static FVector ContactFeet;
        Sky->TimeScale=0;
        auto Place=[&](const FVector& Feet){P->SetActorLocation(Feet+FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),false);};
        switch(SmokeStage){
        case 0: {
            const FVector Focus(1909000,2887000,0);
            double Best=TNumericLimits<double>::Max();int32 Roots=0,Tips=0;
            for(TActorIterator<AActor> It(GetWorld());It;++It){
                TArray<UProceduralMeshComponent*> Components;It->GetComponents(Components);
                for(auto* C:Components)if(C->IsVisible())for(int32 S=0;S<C->GetNumSections();++S)
                    if(auto* M=Cast<UMaterialInstanceDynamic>(C->GetMaterial(S));M && M->K2_GetScalarParameterValue(TEXT("AC2FoliageBend"))>.5f)
                        if(auto* Section=C->GetProcMeshSection(S))for(const auto& V:Section->ProcVertexBuffer){
                            Roots+=V.UV3.X<.001;Tips+=V.UV3.X>.8;
                            if(V.UV3.X<.8 || V.UV3.Y<10 || V.UV3.Y>250)continue;
                            const FVector W=C->GetComponentTransform().TransformPosition(V.Position);
                            const double D=FVector::DistSquaredXY(W,Focus);
                            if(D<Best){double Ground=0;if(Stream->GroundAt(W+FVector(0,0,1000),Ground)){
                                Best=D;ContactFeet=FVector(W.X-25,W.Y,Ground);}}
                        }
            }
            if(Best>FMath::Square(16000.) || !Roots || !Tips){Finish(false);return;}
            UE_LOG(LogTemp,Display,TEXT("AC2 foliage contact: feet=%s roots=%d tips=%d"),*ContactFeet.ToString(),Roots,Tips);
            Place(ContactFeet+FVector(500,0,0));
            View(ContactFeet+FVector(150,-250,180),ContactFeet+FVector(0,0,20));break;
        }
        case 1: Capture(TEXT("AC2Environment-foliage-rest.png"));break;
        case 2: Place(ContactFeet);break;
        case 3: Capture(TEXT("AC2Environment-foliage-contact.png"));break;
        case 4: Place(ContactFeet+FVector(500,0,0));break;
        case 5: Capture(TEXT("AC2Environment-foliage-recovered.png"));break;
        default: UE_LOG(LogTemp,Display,TEXT("AC2 foliage contact: PASS native frills, player contact and recovery captured"));Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2WindmillSmoke")))
    {
        const FVector Bank(2298220,3004750,7380);
        const FVector Mill=AC2::BlockOrigin(0x8ebb)+FVector(2600,6800,14550);
        static TMap<TWeakObjectPtr<UProceduralMeshComponent>,TArray<FVector>> Before;
        static double TotalTravel=0;
        if(SmokeStage==0){
            if(!P){Finish(false);return;}
            Before.Reset();TotalTravel=0;
            P->SetActorLocation(Bank,false,nullptr,ETeleportType::TeleportPhysics);
            View(Bank,Mill+FVector(0,0,1400));
        }else{
            double Travel=0;
            for(TActorIterator<AActor> It(GetWorld());It;++It){
                if(It->IsHidden() || !It->GetActorLocation().Equals(AC2::BlockOrigin(0x8ebb),1))continue;
                TArray<UProceduralMeshComponent*> Parts;It->GetComponents(Parts);
                for(auto* Part:Parts){
                    if(!Part->IsVisible() || !Part->Bounds.GetBox().Intersect(FBox(Mill-FVector(3500,3500,500),Mill+FVector(3500,3500,4000))))continue;
                    const auto* Section=Part->GetProcMeshSection(0);if(!Section)continue;
                    auto& Vertices=Before.FindOrAdd(Part);
                    if(SmokeStage==1)for(const auto& V:Section->ProcVertexBuffer)Vertices.Add(V.Position);
                    else if(Vertices.Num()==Section->ProcVertexBuffer.Num())for(int32 I=0;I<Vertices.Num();++I){
                        const FVector Current=Section->ProcVertexBuffer[I].Position;
                        Travel=FMath::Max(Travel,FVector::Distance(Current,Vertices[I]));
                        Vertices[I]=Current;
                    }
                }
            }
            if(SmokeStage==1)Capture(TEXT("AC2Windmill-bank-before.png"));
            else {
                TotalTravel+=Travel;
                if(Travel<=.001 || SmokeStage==13){
                    UE_LOG(LogTemp,Display,TEXT("AC2 windmill from bank: %.1f m away, %d components, frame %d travel %.3f cm, cumulative travel %.1f cm"),FVector::Dist2D(Bank,Mill)/100,Before.Num(),SmokeStage-1,Travel,TotalTravel);
                    Capture(TEXT("AC2Windmill-bank-after.png"));
                    Before.Reset();Finish(Travel>.001 && TotalTravel>10);return;
                }
            }
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2NightShoreSmoke")))
    {
        // Radar 44.5N, 12.1E. Sample the native water plane so the eye stays
        // above the surface at the reported location, even while the bed is deep.
        if(InitialBlock!=0x8eb8){UE_LOG(LogTemp,Error,TEXT("Night shore requires -AC2Block=8EB8"));Finish(false);return;}
        if(!Far || Far->Center()!=InitialBlock || !Far->WaterTileCount())return;
        const FVector Position(2286840,2947800,0);
        double Surface=0,Bed=0;
        if(!Stream->WaterAt(Position,Surface) || !Stream->GroundAt(Position+FVector(0,0,100000),Bed) || Bed>=Surface)
        {UE_LOG(LogTemp,Error,TEXT("Night shore camera requires the reported submerged riverbed"));Finish(false);return;}
        Sky->TimeScale=0;
        Sky->SetHour(0);
        const FString Output=FPaths::ProjectSavedDir()/TEXT("Screenshots");
        auto Frame=[&](float Yaw){
            const FVector Eye(Position.X,Position.Y,Surface+225);
            View(Eye,Eye+FRotator(-5,Yaw,0).Vector()*10000,80.f);
        };
        switch(SmokeStage++)
        {
        case 0:
            // Keep the material wave phase identical before and after a fix.
            PC->ConsoleCommand(TEXT("r.Test.OverrideTimeMaterialExpressions 0"));
            Frame(198);break;
        case 1: Capture(TEXT("AC2NightShore-bank.png"));break;
        case 2: Frame(-18);break;
        case 3: Capture(TEXT("AC2NightShore-shore.png"));break;
        default:
            for(const TCHAR* Name:{TEXT("AC2NightShore-bank.png"),TEXT("AC2NightShore-shore.png")})
            {
                FImage Image;
                if(!FImageUtils::LoadImage(*(Output/Name),Image)){Finish(false);return;}
                Image.ChangeFormat(ERawImageFormat::BGRA8,EGammaSpace::sRGB);
                const auto Pixels=Image.AsBGRA8();
                int32 Orange=0,Visible=0;
                for(int64 Index=int64(Image.SizeY/3)*Image.SizeX;Index<Pixels.Num();++Index)
                {
                    const auto Color=Pixels[Index];
                    if(Color.R>=65 && Color.R*10>Color.G*18 && Color.R*10>Color.B*25)++Orange;
                    if(FMath::Max3(Color.R,Color.G,Color.B)>20)++Visible;
                }
                UE_LOG(LogTemp,Display,TEXT("AC2 night shore %s: %d orange, %d visible of %lld pixels; bed %.1f cm, surface %.1f cm"),
                    Name,Orange,Visible,Pixels.Num(),Bed,Surface);
                // The original fixed-phase viewport captures had 3.6% and 6.0% of the
                // entire image occupied by the same saturated orange ribbon.
                if(Orange>Pixels.Num()/100 || Visible<Pixels.Num()/25){Finish(false);return;}
            }
            Finish(true);return;
        }
        StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2WaterDistanceSmoke")))
    {
        const bool ReportedBank=InitialBlock==0x8fbc || InitialBlock==0x8fbb;
        const uint32 NextBlock=ReportedBank?InitialBlock-0x100:0x85b0;
        const uint32 ExpectedCenter=SmokeStage>=6?NextBlock:InitialBlock;
        if(!Far || Far->Center()!=ExpectedCenter)return;
        int32 WaterBatches=0,WaterTriangles=0;
        UMaterialInstanceDynamic* WaterMaterial=nullptr;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            if(It->IsHidden())continue;
            TArray<UProceduralMeshComponent*> Components;It->GetComponents(Components);
            for(auto* C:Components)if(C->IsVisible())
                for(int32 Section=0;Section<C->GetNumSections();++Section)
                    if(auto* M=C->GetMaterial(Section);M && M->GetBaseMaterial()->GetName()==TEXT("M_AC2Water"))
                    {auto* Dynamic=Cast<UMaterialInstanceDynamic>(M);
                     if(Dynamic && Dynamic->K2_GetScalarParameterValue(TEXT("AC2WaterBelow"))>.5f)continue;
                     ++WaterBatches;WaterTriangles+=C->GetProcMeshSection(Section)->ProcIndexBuffer.Num()/3;
                     WaterMaterial=Cast<UMaterialInstanceDynamic>(M);}
        }
        if(Far->WaterTileCount()<=Stream->WaterCount() || WaterBatches!=Far->WaterBatchCount() ||
           WaterTriangles<Far->WaterTileCount()*2)
        {UE_LOG(LogTemp,Error,TEXT("AC2 water: incomplete or overlapping surface (%d batches, %d triangles)"),WaterBatches,WaterTriangles);Finish(false);return;}
        // The combined water library must tick independently of WorldStream.
        // Check actual rendered material parameters, both before and after the
        // block transition, so a static but correctly tiled surface cannot pass.
        static FLinearColor PreviousWaterUV;
        if(!WaterMaterial){Finish(false);return;}
        const auto WaterUV=WaterMaterial->K2_GetVectorParameterValue(TEXT("AC2Stage0RowU"));
        if(SmokeStage==0 || SmokeStage==6)PreviousWaterUV=WaterUV;
        if((SmokeStage==1 || SmokeStage==7) && WaterUV.Equals(PreviousWaterUV,.0001f))
        {UE_LOG(LogTemp,Error,TEXT("AC2 water: DAT ripple animation did not advance at stage %d"),SmokeStage);Finish(false);return;}
        Sky->TimeScale=0;
        // Reported coastline: 38.1N, 3.7E. Look over the near/far terrain boundary.
        const FVector Shore=InitialBlock==0x8fbb?FVector(2298220,3004750,0):ReportedBank?FVector(2299950,3012590,0):AC2::BlockOrigin(0x84b0)+FVector(3480,1240,0);
        FVector Direction(60000,-10000,7100);
        if(ReportedBank){
            // Find the water beside the reported tree, rather than aiming into
            // the uphill canopy. GroundAt includes the tree's restored cylinder.
            double Best=DBL_MAX;
            for(int32 X=-6;X<=6;++X)for(int32 Y=-6;Y<=6;++Y){
                const FVector Candidate=Shore+FVector(X*2000,Y*2000,1000000);
                double Surface=0,Bottom=0;
                if(Stream->WaterAt(Candidate,Surface)&&Stream->GroundAt(Candidate,Bottom)&&Bottom<Surface-350){
                    const double Distance=FMath::Square(X)+FMath::Square(Y);
                    if(Distance<Best){Best=Distance;Direction=FVector(X*2000,Y*2000,Surface);}
                }
            }
            if(Best==DBL_MAX){Finish(false);return;}
        }
        double Ground=0;
        if(!Stream->GroundAt(Shore+FVector(0,0,1000000),Ground)){Finish(false);return;}
        const FVector Eye=ReportedBank?Shore+FVector(Direction.X,Direction.Y,0).GetSafeNormal()*500+FVector(0,0,InitialBlock==0x8fbb?7380:8100):Shore+FVector(0,0,Ground+500);
        FString Label=TEXT("water-distance");
        FParse::Value(FCommandLine::Get(),TEXT("AC2WaterCapture="),Label);
        switch(SmokeStage)
        {
        case 0: View(Eye,Shore+Direction);break;
        case 1: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-coast.png")));break;
        case 2: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-coast-later.png")));break;
        case 3: View(Shore+FVector(-18000,12000,Ground+26000),Shore+Direction*.5);break;
        case 4: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-overview.png")));break;
        case 5: if(!LoadBlock(NextBlock)){Finish(false);return;}break;
        case 6: View(Eye,Shore+Direction);break;
        case 7: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-streamed.png")));Rain(1);break;
        case 8: {
            double Bottom=0;
            const FVector WaterPoint=Shore+Direction;
            if(Effects->RainCount()==0 || Sky->IsSubmerged()){UE_LOG(LogTemp,Error,TEXT("AC2 water: no rain control above the bank"));Finish(false);return;}
            if(!Stream->GroundAt(WaterPoint+FVector(0,0,1000000),Bottom) || Bottom>=WaterPoint.Z-200){
                UE_LOG(LogTemp,Error,TEXT("AC2 water: underwater capture must be above the riverbed"));Finish(false);return;}
            View(WaterPoint-FVector(0,0,180),WaterPoint+FVector(500,400,500));break;
        }
        case 9:
            if(Effects->RainCount() || !Sky->IsSubmerged()){UE_LOG(LogTemp,Error,TEXT("AC2 water: rain or wrong camera state underwater"));Finish(false);return;}
            Capture(*(TEXT("AC2Environment-")+Label+TEXT("-underwater.png")));break;
        case 10: {
            // Inspect shallow transmission separately from the deep underwater view.
            FVector Point=Shore+Direction;double Best=DBL_MAX;
            for(int32 X=-30;X<=30;++X)for(int32 Y=-30;Y<=30;++Y){
                const FVector Candidate=Shore+FVector(X*500,Y*500,1000000);double Surface=0,Bottom=0;
                if(Stream->WaterAt(Candidate,Surface) && Stream->GroundAt(Candidate,Bottom) &&
                    Bottom<Surface-70 && Bottom>Surface-160){
                    const double D=FMath::Square(X)+FMath::Square(Y);
                    if(D<Best){Best=D;Point=FVector(Candidate.X,Candidate.Y,Surface);}
                }}
            if(Best==DBL_MAX){UE_LOG(LogTemp,Error,TEXT("AC2 water: no shallow riverbed found"));Finish(false);return;}
            View(Point+FVector(0,-350,300),Point-FVector(0,0,100));break;
        }
        case 11:
            if(!Effects->RainCount() || Sky->IsSubmerged()){UE_LOG(LogTemp,Error,TEXT("AC2 water: rain did not resume over the river"));Finish(false);return;}
            Rain(-1);Capture(*(TEXT("AC2Environment-")+Label+TEXT("-riverbed.png")));break;
        case 12: View(Shore+Direction-FVector(0,0,180),Shore+Direction+FVector(1500,1000,-150));break;
        case 13: Capture(*(TEXT("AC2Environment-")+Label+TEXT("-submerged.png")));break;
        default: UE_LOG(LogTemp,Display,TEXT("AC2 water: PASS %d tiles in %d batches after streaming %04X to %04X, no overlapping near surface; DAT ripple animation advances on both sides"),Far->WaterTileCount(),WaterBatches,InitialBlock,NextBlock);Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AC2ShorelineSmoke")))
    {
        const FVector Bank=AC2::BlockOrigin(0x74a9);
        double Surface=0;
        if(!Stream->WaterAt(Bank+FVector(5550,15540,9390),Surface) || FMath::Abs(Surface-9670)>0.1)
        { UE_LOG(LogTemp,Error,TEXT("AC2 shoreline: rendered bank and swimming surface disagree"));Finish(false);return; }
        switch(SmokeStage)
        {
        case 0: View(Bank+FVector(14000,18500,11000),Bank+FVector(5550,15540,9670));break;
        case 1: Capture(TEXT("AC2Environment-shoreline-bank.png"));break;
        case 2: View(Bank+FVector(5000,5000,19000),Bank+FVector(12000,14000,9670));break;
        case 3: Capture(TEXT("AC2Environment-shoreline-overview.png"));break;
        default: UE_LOG(LogTemp,Display,TEXT("AC2 shoreline: PASS 74A9 surface=%.2f cm"),Surface);Finish(true);return;
        }
        ++SmokeStage;StageTime=0;return;
    }
    const FVector Sign = AC2::BlockOrigin(0x75b8)+FVector(13075.708,12993.612,14183.672);
    switch (SmokeStage)
    {
    case 0: View(Sign+FVector(-700,-1250,500),Sign+FVector(0,0,400)); break;
    case 1: Capture(TEXT("AC2Environment-sign-front.png")); break;
    case 2: View(Sign+FVector(700,1250,500),Sign+FVector(0,0,400)); break;
    case 3: Capture(TEXT("AC2Environment-sign-back.png")); break;
    case 4: LoadCell(0x03f40154); break;
    case 5: {
        auto *Actor=GetWorld()->SpawnActor<AActor>(); EnvironmentProp=Actor;
        auto *Root=NewObject<USceneComponent>(Actor); Actor->SetRootComponent(Root); Root->RegisterComponent();
        const FVector Location=AC2::BlockOrigin(0x03f4)+FVector(18173.0484,6294.236,1512.1846);
        Actor->SetActorLocation(Location); EnvironmentModel=MakeUnique<FAC2AssetLibrary>(); FString Error;
        const auto Vault = AC2VaultFixture::Resolve(DatDirectory, CharacterCatalog);
        if(!Vault.Shard || !EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(Vault.Shard,Actor,Root)) { Finish(false); return; }
        View(Location+FVector(-900,-750,360),Location+FVector(0,0,250));
        break;
    }
    case 6: Capture(TEXT("AC2Environment-menhir.png")); break;
    case 7: EnvironmentModel.Reset(); EnvironmentProp->Destroy(); if(!LoadBlock(0x5667)) { Finish(false); return; } break;
    case 8: {
        if (!Far || Far->TileCount()==0) return;
        const FVector LookAt = P->GetActorLocation();
        View(LookAt+FVector(0,0,140),LookAt+FVector(1300,200,-50));
        break;
    }
    case 9:
        if(Stream->GrassBatchCount()==0 || !Stream->GrassDepthReady()) { Finish(false); return; }
        Capture(TEXT("AC2Environment-foliage.png")); break;
    case 10: if(!LoadBlock(0x77b8)) { Finish(false); return; } break;
    case 11: {
        const FVector Origin=AC2::BlockOrigin(0x77b8);
        View(Origin+FVector(-700,7900,18700),Origin+FVector(1325,10716,18550));
        break;
    }
    case 12: Capture(TEXT("AC2Environment-arwic-placement.png")); break;
    case 13: LoadCell(0x7cb90102); break;
    case 14: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(15700,2100,6150),Origin+FVector(16117,2449,6180));
        break;
    }
    case 15: Capture(TEXT("AC2Environment-cave-torch.png")); break;
    case 16: if(!LoadBlock(0x7cb9)) { Finish(false); return; } break;
    case 17: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(1500,2600,9600),Origin+FVector(2500,5000,9700));
        auto* Actor=GetWorld()->SpawnActor<AActor>(); EnvironmentProp=Actor;
        auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        Actor->SetActorLocation(Origin+FVector(1800,3000,9500));
        EnvironmentModel=MakeUnique<FAC2AssetLibrary>();FString Error;
        if(!EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(0x1f000023,Actor,Root)) {Finish(false);return;}
        EnvironmentModel->SetAnimationState(AC2Motion::Ground,0x41000010);
        View(Actor->GetActorLocation()+FVector(-180,320,130),Actor->GetActorLocation()+FVector(0,0,100));
        break;
    }
    case 18: Capture(TEXT("AC2Environment-bow-idle.png")); break;
    case 19: EnvironmentModel->PlayBehavior(0x410000e2,0x41000010,.2f); break;
    case 20: Capture(TEXT("AC2Environment-bow-attack.png")); break;
    case 21: Capture(TEXT("AC2Environment-bow-recovery.png")); break;
    case 22: EnvironmentModel.Reset();EnvironmentProp->Destroy();break;
    case 23: {
        auto* Actor=GetWorld()->SpawnActor<AActor>();EnvironmentProp=Actor;
        auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
        Actor->SetActorLocation(AC2::BlockOrigin(0x7cb9)+FVector(1800,3000,9600));
        EnvironmentModel=MakeUnique<FAC2AssetLibrary>();FString Error;
        if(!EnvironmentModel->Open(DatDirectory,Error) || !EnvironmentModel->DrawModel(0x1f0007dd,Actor,Root)){Finish(false);return;}
        const FVector Focus=Actor->GetActorLocation()+FVector(-85,0,16);
        View(Focus+FVector(0,300,30),Focus);break;
    }
    case 24: Capture(TEXT("AC2Environment-road-sign-front.png"));break;
    case 25: {
        const FVector Focus=EnvironmentProp->GetActorLocation()+FVector(-85,0,16);
        View(Focus+FVector(0,-300,30),Focus);break;
    }
    case 26: Capture(TEXT("AC2Environment-road-sign-back.png"));break;
    case 27: EnvironmentModel.Reset();EnvironmentProp->Destroy();break;
    case 28: {
        const FVector Origin=AC2::BlockOrigin(0x7cb9);
        View(Origin+FVector(1500,2600,10400),Origin+FVector(2500,5000,9600));break;
    }
    case 29: Capture(TEXT("AC2Environment-ground-cover.png"));break;
    default: Finish(true); return;
    }
    ++SmokeStage; StageTime=0;
}
