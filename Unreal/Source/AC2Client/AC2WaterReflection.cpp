#include "AC2WaterReflection.h"
#include "AC2WorldGrid.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "ConvexVolume.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace {
// Leave room beyond the viewport for the retail +/- .04 signed ripple offset.
constexpr float ProjectionScale=.9f;
struct FReflection {
    TWeakObjectPtr<USceneCaptureComponent2D> Capture;
    TWeakObjectPtr<UTexture2D> Bump;
    TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
    TMap<TWeakObjectPtr<AActor>,TMap<uint32,double>> Planes;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> Surfaces;
    TOptional<double> ActiveHeight;
};
TMap<TWeakObjectPtr<UWorld>,FReflection> Reflections;

// LScapeTerrainMesh::CreateLocalMemBumpMap builds a 256-square V8U8 image
// from a damped circular wave and two crossing wave trains. The byte casts
// truncate each signed component before the fixed-function texture lookup.
UTexture2D* MakeWaterBumpMap()
{
    constexpr int32 Size=256;
    auto* Texture=UTexture2D::CreateTransient(Size,Size,PF_B8G8R8A8);
    Texture->SRGB=false;
    Texture->NeverStream=true;
    Texture->Filter=TF_Bilinear;
    auto& Data=Texture->GetPlatformData()->Mips[0].BulkData;
    auto* Pixels=static_cast<FColor*>(Data.Lock(LOCK_READ_WRITE));
    for(int32 Y=0;Y<Size;++Y)for(int32 X=0;X<Size;++X){
        const double U=double(X)/Size-.5,V=double(Y)/Size-.5;
        const double Radius=FMath::Sqrt(U*U+V*V);
        const double Amplitude=-64.*FMath::Exp(-5.*Radius);
        const double A=140.*(.85*U-V),B=150.*(U+V);
        const int32 Du=int32(16.*FMath::Cos(A))-int32(Amplitude*FMath::Cos(300.*Radius))-int32(-32.*FMath::Cos(B));
        const int32 Dv=int32(16.*FMath::Sin(A))-int32(Amplitude*FMath::Sin(300.*Radius))-int32(-32.*FMath::Sin(B));
        Pixels[Y*Size+X]=FColor(uint8(128+Du),uint8(128+Dv),0,255);
    }
    Data.Unlock();
    Texture->UpdateResource();
    return Texture;
}

void Bind(const FReflection& R,UMaterialInstanceDynamic* M)
{
    if(R.Capture.IsValid() && R.Bump.IsValid()){
        M->SetScalarParameterValue(TEXT("AC2WaterProjectionScale"),ProjectionScale);
        M->SetTextureParameterValue(TEXT("AC2WaterReflection"),R.Capture->TextureTarget);
        M->SetTextureParameterValue(TEXT("AC2WaterBumpMap"),R.Bump.Get());
    }
    if(R.ActiveHeight.IsSet())M->SetScalarParameterValue(TEXT("AC2WaterReflectionHeight"),*R.ActiveHeight);
    M->SetScalarParameterValue(TEXT("AC2WaterReflectionReady"),R.ActiveHeight.IsSet()?1:0);
}
}

void ExcludeFromAC2WaterReflection(UWorld* World,UPrimitiveComponent* Surface)
{
    if(!World || !Surface)return;
    auto& R=Reflections.FindOrAdd(World);
    if(auto* Capture=R.Capture.Get()){
        Capture->HiddenComponents.RemoveAllSwap([](const auto& Existing){return !Existing.IsValid();});
        Capture->HideComponent(Surface);
    }else{
        R.Surfaces.RemoveAllSwap([](const auto& Existing){return !Existing.IsValid();});
        R.Surfaces.Add(Surface);
    }
}

void RegisterAC2WaterPlane(UWorld* World,AActor* Owner,uint32 Block,double WorldHeight)
{
    if(World && Owner)Reflections.FindOrAdd(World).Planes.FindOrAdd(Owner).Add(Block,WorldHeight);
}

void RegisterAC2WaterReflection(UWorld* World,UMaterialInstanceDynamic* Material)
{
    if(!World || !Material)return;
    auto& R=Reflections.FindOrAdd(World);
    R.Materials.AddUnique(Material);
    Bind(R,Material);
}

void UpdateAC2WaterReflection(UWorld* World)
{
    if(!World || !FApp::CanEverRender())return;
    for(auto It=Reflections.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    auto* R=Reflections.Find(World);
    auto* PC=World->GetFirstPlayerController();
    if(!R || !PC || !PC->PlayerCameraManager)return;
    R->Materials.RemoveAll([](const auto& M){return !M.IsValid();});
    if(R->Materials.IsEmpty())return;
    const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
    const FRotator Rotation=PC->PlayerCameraManager->GetCameraRotation();
    auto* Player=PC->GetLocalPlayer();
    FSceneViewProjectionData Projection;
    if(!Player || !Player->ViewportClient || !Player->GetProjectionData(Player->ViewportClient->Viewport,Projection))return;
    FConvexVolume Frustum;
    GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);
    constexpr double HalfBlock=AC2::BlockCentimetres*.5;
    const FVector Extent(HalfBlock,HalfBlock,1.);
    TOptional<double> Height;
    double Nearest=DBL_MAX;
    for(auto It=R->Planes.CreateIterator();It;++It){
        auto* Owner=It.Key().Get();
        if(!Owner){It.RemoveCurrent();continue;}
        if(Owner->IsHidden())continue;
        for(const auto& Tile:It.Value()){
            if(Tile.Value>=Eye.Z)continue;
            const FVector Origin=AC2::BlockOrigin(Tile.Key);
            const FVector Center(Origin.X+HalfBlock,Origin.Y+HalfBlock,Tile.Value);
            if(!Frustum.IntersectBox(Center,Extent))continue;
            const FVector Closest(FMath::Clamp(Eye.X,Origin.X,Origin.X+16000.),
                FMath::Clamp(Eye.Y,Origin.Y,Origin.Y+16000.),Tile.Value);
            const double Distance=(Closest-Eye).SizeSquared();
            if(Distance<Nearest){Nearest=Distance;Height=Tile.Value;}
        }
    }
    if(!Height.IsSet()){
        if(R->ActiveHeight.IsSet()){
            R->ActiveHeight.Reset();
            for(auto M:R->Materials)Bind(*R,M.Get());
        }
        return;
    }
    const auto Rect=Projection.GetConstrainedViewRect();
    const int32 W=Rect.Width(), H=Rect.Height();
    if(W<=0 || H<=0)return;
    // Keep the capture projection aligned with the main camera, including
    // when the window resizes. The original pass also uses a reduced target.
    const int32 CaptureW=FMath::Clamp(W/2,64,768);
    const int32 CaptureH=FMath::Max(1,FMath::RoundToInt(double(CaptureW)*H/W));
    bool Rebind=Height!=R->ActiveHeight;
    auto* Capture=R->Capture.Get();
    if(!Capture){
        auto* Actor=World->SpawnActor<AActor>();
        if(!Actor)return;
        Capture=NewObject<USceneCaptureComponent2D>(Actor);
        R->Capture=Capture;
        Actor->SetRootComponent(Capture);Actor->AddInstanceComponent(Capture);
        Capture->bCaptureEveryFrame=false;
        Capture->bCaptureOnMovement=false;
        Capture->CaptureSource=SCS_SceneColorHDR;
        Capture->bEnableClipPlane=true;
        Capture->bUseCustomProjectionMatrix=true;
        R->Surfaces.RemoveAll([](const auto& Surface){return !Surface.IsValid();});
        Capture->HiddenComponents=MoveTemp(R->Surfaces);
        Capture->ClipPlaneNormal=FVector::UpVector;
        Capture->ShowFlags.SetMotionBlur(false);
        Capture->ShowFlags.SetTemporalAA(false);
        Capture->ShowFlags.SetDynamicShadows(false);
        auto* Target=NewObject<UTextureRenderTarget2D>(Capture);
        Target->AddressX=TA_Clamp;Target->AddressY=TA_Clamp;
        Target->ClearColor=FLinearColor::Black;
        Target->InitCustomFormat(CaptureW,CaptureH,PF_FloatRGBA,true);
        Capture->TextureTarget=Target;
        Capture->RegisterComponent();
        Rebind=true;
    }else if(Capture->TextureTarget->SizeX!=CaptureW || Capture->TextureTarget->SizeY!=CaptureH){
        Capture->TextureTarget->ResizeTarget(CaptureW,CaptureH);
    }
    if(!R->Bump.IsValid()){
        R->Bump=MakeWaterBumpMap();
        Rebind=true;
    }
    Capture->SetWorldLocationAndRotation(FVector(Eye.X,Eye.Y,2.*(*Height)-Eye.Z),
        FRotator(-Rotation.Pitch,Rotation.Yaw,-Rotation.Roll));
    Capture->FOVAngle=PC->PlayerCameraManager->GetFOVAngle();
    Capture->CustomProjectionMatrix=Projection.ProjectionMatrix;
    Capture->CustomProjectionMatrix.M[0][0]*=ProjectionScale;
    Capture->CustomProjectionMatrix.M[1][1]*=ProjectionScale;
    Capture->ClipPlaneBase=FVector(Eye.X,Eye.Y,*Height);
    Capture->CaptureSceneDeferred();
    R->ActiveHeight=Height;
    if(Rebind)for(auto M:R->Materials)Bind(*R,M.Get());
}

void ReleaseAC2WaterReflection(UWorld* World)
{
    if(auto* R=Reflections.Find(World);R && R->Capture.IsValid())R->Capture->GetOwner()->Destroy();
    Reflections.Remove(World);
}
