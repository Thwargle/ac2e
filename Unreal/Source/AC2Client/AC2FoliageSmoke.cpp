#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2WorldGrid.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Exercise the actual connected path at the two reported bare locations, then
// their nearest southern block and the reported floating-grass slope. Never populate empty scenes.
void FAC2Network::FoliageSmokeTick()
{
    auto Finish=[&](bool OK,const FString& Error){
        UE_LOG(LogTemp,Display,TEXT("AC2 foliage network smoke %s: %s"),OK?TEXT("PASS"):TEXT("FAIL"),*Error);
        FFileHelper::SaveStringToFile(OK?TEXT("PASS"):TEXT("FAIL: ")+Error,
            *(FPaths::ProjectSavedDir()/TEXT("Diagnostics/AC2Network-smoke.txt")));
        Smoke=false;FPlatformMisc::RequestExitWithStatus(false,OK?0:1);
    };
    if(Time>160){Finish(false,TEXT("Timed out waiting for connected foliage"));return;}
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(!IsWorld() || !P || !MapTravelEnabled || NeedsPlacement || P->IsHeld() || Time<FoliageSmokeNext)return;
    if(FoliageSmokeStep>=12){Finish(true,TEXT("Authored empty scenes preserved; ground-contact grass uploaded at native highres dimensions"));return;}
    const FVector2D Positions[]={FVector2D(19053.6,29294.4),FVector2D(19074,29070),FVector2D(19090,28870),FVector2D(19074,28886.4)};
    const uint32 Blocks[]={0x77b7,0x77b5,0x77b4,0x77b4};
    const int32 Index=FoliageSmokeStep/3;
    if(FoliageSmokeStep%3==0)
    {
        if(!Mode->TravelToMap(Positions[Index]/20.)){Finish(false,TEXT("Map travel request refused"));return;}
    }
    else
    {
        FAC2AssetStats Stats;
        if((Cell>>16)!=Blocks[Index] || !Mode->EnvironmentStats(Blocks[Index],Stats))return;
        if((FVector2D(P->GetActorLocation()/100)-Positions[Index]).Size()>10)
        {Finish(false,TEXT("Server placed character outside the requested foliage sample"));return;}
        if(FoliageSmokeStep%3==1)
        {
            const float Yaw=Index==3?120.f:-90.f;
            P->SetActorRotation(FRotator(0,Yaw,0));PC->SetControlRotation(FRotator(-12,Yaw,0));
            P->SetCameraDistance(350);
        }
        else
        {
            const bool Expected=Index>=2;
            if((Stats.FrillsGenerated>0)!=Expected || Stats.FrillsResolved!=Stats.FrillsGenerated ||
               (Expected && (Stats.FrillBatchesUploaded==0 || Stats.GrassDepthSections==0 ||
                             Stats.GrassSortedSections!=0 || !Stats.HighResAvailable || Stats.HighResSurfaces==0)))
            {Finish(false,TEXT("Local block foliage did not match authored scenery"));return;}
            if(Expected)
            {
                const auto* Grass=Stats.GrassTextures.FindByPredicate([](const FAC2TextureSource& S){return S.Texture==0x40000008;});
                if(!Grass || !Grass->HighResolution || Grass->Surface!=0x41000034 ||
                    Grass->DecodedSize!=FIntPoint(1024,256) || Grass->UploadedSize!=Grass->DecodedSize)
                {Finish(false,TEXT("Grass did not upload the native 1024x256 highres surface"));return;}
                if(Stats.GrassGroundContacts==0 || Stats.GrassMaxRootGapAfter>.01)
                {Finish(false,TEXT("Grass bases are not in contact with rendered terrain"));return;}
                UE_LOG(LogTemp,Display,TEXT("AC2 grass audit %04X: texture=%08X surface=%08X uploaded=%dx%d highres=%d contacts=%d max_root_gap_cm=%.3f->%.6f"),
                    Blocks[Index],Grass->Texture,Grass->Surface,Grass->UploadedSize.X,Grass->UploadedSize.Y,
                    Grass->HighResolution,Stats.GrassGroundContacts,Stats.GrassMaxRootGapBefore,Stats.GrassMaxRootGapAfter);
            }
            UE_LOG(LogTemp,Display,TEXT("AC2 connected foliage %04X: generated=%d resolved=%d uploaded=%d highres=%d/%d"),
                Blocks[Index],Stats.FrillsGenerated,Stats.FrillsResolved,Stats.FrillBatchesUploaded,Stats.HighResSurfaces,Stats.Textures);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/
                FString::Printf(TEXT("Screenshots/AC2Foliage-network-%04X%s.png"),Blocks[Index],Index==3?TEXT("-slope"):TEXT("")),true,false);
        }
    }
    ++FoliageSmokeStep;
    FoliageSmokeNext=Time+3.;
}
