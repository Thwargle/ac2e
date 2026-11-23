#include "AC2Network.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2WorldGrid.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Components/CapsuleComponent.h"
#include "UObject/UObjectIterator.h"

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
                int32 Roots=0,Tips=0,InteractiveSections=0,CachedSections=0;
                const FVector PlayerFeet=P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                for(TObjectIterator<UProceduralMeshComponent> It;It;++It)if(It->GetWorld()==Mode->GetWorld() && It->IsRegistered() && !It->IsBeingDestroyed())
                    for(int32 Section=0;Section<It->GetNumSections();++Section)
                        if(auto* Mat=Cast<UMaterialInstanceDynamic>(It->GetMaterial(Section)))
                            if(Mat->K2_GetScalarParameterValue(TEXT("AC2FoliageBend"))>.5f){
                                auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/AC2/Materials/AC2FoliageInteraction.AC2FoliageInteraction"));
                                FLinearColor Position(0,0,0,0);
                                if(Collection)Mode->GetWorld()->GetParameterCollectionInstance(Collection)->GetVectorParameterValue(TEXT("Position0"),Position);
                                if(Position.A<=0 || FVector::Dist(PlayerFeet,FVector(Position.R,Position.G,Position.B))>2.)
                                {Finish(false,FString::Printf(TEXT("Foliage interaction is not following the connected player: material=%s component=%s position=%s feet=%s"),
                                    *Mat->GetName(),*It->GetPathName(),*Position.ToString(),*PlayerFeet.ToString()));return;}
                                if(auto* Mesh=It->GetProcMeshSection(Section)){
                                    ++InteractiveSections;
                                    CachedSections+=It->ShadowCacheInvalidationBehavior==EShadowCacheInvalidationBehavior::Rigid;
                                    for(const auto& V:Mesh->ProcVertexBuffer){Roots+=V.UV3.X<.001;Tips+=V.UV3.X>.5;}
                                }
                            }
                if(!InteractiveSections || !Roots || !Tips || !CachedSections)
                {Finish(false,TEXT("Rendered grass lacks anchored roots or movable tips"));return;}
                UE_LOG(LogTemp,Display,TEXT("AC2 foliage interaction: sections=%d cached_shadows=%d anchored_roots=%d movable_tips=%d"),InteractiveSections,CachedSections,Roots,Tips);
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
