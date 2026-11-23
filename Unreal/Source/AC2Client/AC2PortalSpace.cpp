#include "AC2PortalSpace.h"
#include "AC2GameMode.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

bool FAC2PortalSounds::Decode(TConstArrayView<uint8> Bytes)
{
    Enter=Exit=0;Loops.Reset();
    // FileArray group 40000002: EnumIDMap 28000003. Only the leading
    // enum/DID hash is needed; the following filename map is not audio state.
    if(Bytes.Num()<8 || AC2::U32(Bytes.GetData())!=0x28000003)return false;
    const uint32 Count=AC2::U32(Bytes.GetData()+4)&0xffffff;
    if(Count>4096 || uint64(Count)*8>uint64(Bytes.Num()-8))return false;
    TMap<uint32,uint32> Map;
    for(uint32 I=0;I<Count;++I)Map.Add(AC2::U32(Bytes.GetData()+8+I*8),AC2::U32(Bytes.GetData()+12+I*8));
    Enter=Map.FindRef(0x40000018);Exit=Map.FindRef(0x4000001c);
    for(uint32 Key:{0x4000001du,0x4000001eu})if(uint32 Id=Map.FindRef(Key);Id>>24==0x2a)Loops.Add(Id);
    return Enter>>24==0x2a && Exit>>24==0x2a;
}

AAC2PortalSpace::AAC2PortalSpace()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("PortalSpace"));
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Tunnel"));Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);
    Capture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortalCamera"));Capture->SetupAttachment(RootComponent);
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetTonemapper(false);Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetBloom(false);Capture->ShowFlags.SetColorGrading(false);
    SetActorEnableCollision(false);
}
bool AAC2PortalSpace::Initialize()
{
    if(Assets)return true;
    auto* Mode=GetWorld()->GetAuthGameMode<AAC2GameMode>();if(!Mode)return false;
    Assets=MakeUnique<FAC2AssetLibrary>();FString Error;
    if(!Assets->Open(Mode->DatDirectory,Error)){Assets.Reset();return false;}
    FAC2DatArchive Portal;TArray<uint8> Bytes;
    if(Portal.Open(Mode->DatDirectory/TEXT("portal.dat"),1,Error) && Portal.Read(0x28000003,Bytes,Error))Sounds.Decode(Bytes);
    // EnumIdMap group 40000002 in 28000003, verified against Legions portal.dat.
    Mesh->SetMaterial(0,Assets->Material(0x31000350));
    Mesh->SetMaterial(1,Assets->Material(0x31000351));
    Image=NewObject<UTextureRenderTarget2D>(this);Image->ClearColor=FLinearColor(0,0,0,1);
    Image->TargetGamma=1;Image->InitCustomFormat(1280,720,PF_FloatRGBA,true);
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/AC2/Materials/M_AC2Capture.M_AC2Capture"));
    if(Base){Composite=UMaterialInstanceDynamic::Create(Base,this);Composite->SetTextureParameterValue(TEXT("Capture"),Image);}
    Capture->TextureTarget=Image;Capture->ShowOnlyActors.Add(this);
    for(int32 R=0;R<149;++R)for(int32 J=0;J<16;++J){int32 A=R*17+J,B=A+17;
        // AC2 +Y forward to Unreal +X forward changes handedness.
        Indices.Append({A,B,A+1,A+1,B,B+1});}
    Effects=MakeUnique<FAC2Effects>();Effects->Open(GetWorld(),Mode->DatDirectory,false);
    Capture->ShowOnlyActors.Add(Effects->RenderActor());
    return true;
}
void AAC2PortalSpace::Generate(int32 Index)
{
    const FRing Previous=Rings[(Index+149)%150];auto& R=Rings[Index];
    R.Distance=Previous.Distance+3;R.Radius=10+6*FMath::Sin(R.Distance*.01f);
    const float X=.35f*FAC2Wave::Perlin(Seed*UE_PI+R.Distance*.005f),Y=.25f*FAC2Wave::Perlin(Seed*UE_PI+R.Distance*.002f);
    // Native X/Y/Z axes become UE Y/X/Z; reflection reverses each rotation.
    const FQuat Q=FQuat(FVector::YAxisVector,-X)*FQuat(FVector::XAxisVector,-Y)*FQuat(FVector::ZAxisVector,-X);
    R.Frame=FTransform(Q,FVector(300,0,0))*Previous.Frame;
}
void AAC2PortalSpace::UpdateGeometry()
{
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<FColor> C;TArray<FProcMeshTangent> T;
    V.SetNumUninitialized(150*17);N.SetNumUninitialized(V.Num());UV.SetNumUninitialized(V.Num());C.SetNumUninitialized(V.Num());
    for(int32 Layer=0;Layer<2;++Layer){
        for(int32 R=0;R<150;++R){const auto& Ring=Rings[(First+R)%150];
            const float Alpha=R<16?1-.2f*R/15.f:.8f*FMath::Square(1-R/149.f);
            for(int32 J=0;J<=16;++J){const float A=J*UE_PI/8;const FVector Radial(0,FMath::Cos(A),FMath::Sin(A));const int32 I=R*17+J;
                V[I]=Ring.Frame.TransformPosition(Radial*Ring.Radius*(Layer?120:100));
                N[I]=-Ring.Frame.TransformVectorNoScale(Radial);UV[I]=FVector2D(Ring.Distance,J/16.f);C[I]=FColor(255,255,255,uint8(Alpha*255));}}
        if(!Mesh->GetProcMeshSection(Layer))Mesh->CreateMeshSection(Layer,V,Indices,N,UV,C,T,false);
        else Mesh->UpdateMeshSection(Layer,V,N,UV,C,T);
    }
    const auto& A=Rings[First];const auto& B=Rings[(First+1)%150];const float Blend=FMath::Clamp((Position-A.Distance)/3,0.f,1.f);
    Capture->SetRelativeLocation(FMath::Lerp(A.Frame.GetLocation(),B.Frame.GetLocation(),Blend));
    Capture->SetRelativeRotation(FQuat::Slerp(A.Frame.GetRotation(),B.Frame.GetRotation(),Blend));
    Capture->FOVAngle=65.f*(Phase==3?1+1.4f*Age*Age:1);
}
bool AAC2PortalSpace::HoldDeparture()
{
    // The source scene must stay resident while the tunnel fades over it.
    if (!Phase && !DepartureScene) DepartureScene=Initialize();
    return DepartureScene;
}
float AAC2PortalSpace::WorldFOVScale() const
{
    const float Fraction=FMath::Clamp(Age,0.f,1.f);
    if (Phase==1 && DepartureScene) return 1.f-.8f*Fraction*Fraction;
    if (Phase==4 && !Requested()) return 1.f+1.4f*FMath::Square(1.f-Fraction);
    return 1.f;
}
void AAC2PortalSpace::Tick(float Delta)
{
    Super::Tick(Delta);
    const bool Wanted=Requested();
    if(Wanted && !Phase && Initialize()){
        Phase=1;Age=0;Position=3;First=0;Seed=FMath::FRandRange(0.f,10000.f);
        Rings.Init(FRing(),150);for(int32 I=0;I<150;++I)Generate(I);
        StopSounds();
        if(auto* Audio=FAC2Audio::Get(GetWorld())){
            SoundContexts.Add(Audio->Play(Sounds.Enter));
            for(uint32 Loop:Sounds.Loops)SoundContexts.Add(Audio->Play(Loop));
        }
    }
    if(!Phase){DepartureScene=false;return;}
    if(Wanted && Phase>=3)
    {
        // A second travel can arrive during either exit phase. Resume the
        // tunnel immediately, before the flash exposes that new destination.
        Phase=2;Age=0;StopSounds();
        if(auto* Audio=FAC2Audio::Get(GetWorld()))
            for(uint32 Loop:Sounds.Loops)SoundContexts.Add(Audio->Play(Loop));
    }
    Age+=Delta;Time+=Delta;
    if(Phase==1 && Age>=1){Phase=2;Age=0;DepartureScene=false;
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_MAP_TRAVEL_SMOKE"))==TEXT("1"))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Map-network-portal.png"),true,false);
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_JOURNAL_SMOKE"))==TEXT("1"))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Journal-portal.png"),true,false);}
    if(Phase==2 && !Wanted){Phase=3;Age=0;StopSounds();
        if(auto* Audio=FAC2Audio::Get(GetWorld()))SoundContexts.Add(Audio->Play(Sounds.Exit));}
    if(Phase==3 && Age>=1){Phase=4;Age=0;}
    if(Phase==4 && Age>=1){Phase=0;Age=0;Capture->bCaptureEveryFrame=false;Effects->SyncSources({});return;}
    // Catch up safely even after synchronous world loading stalls a frame.
    Position+=FMath::Min(Delta,.25f)*120;
    while(Position>=Rings[(First+1)%150].Distance){const int32 Old=First;First=(First+1)%150;Generate(Old);}
    UpdateGeometry();Assets->UpdateMaterials(Time);
    FAC2ParticleSource Source;Source.Key=1;Source.Descriptor=0x39000148;Source.Frame=Capture->GetComponentTransform();
    Effects->SyncSources({Source});Effects->Update(Capture->GetComponentLocation(),Capture->GetRightVector(),Capture->GetUpVector(),Delta,0,FVector::ZeroVector,true);
    Capture->bCaptureEveryFrame=Phase!=4;
}
bool AAC2PortalSpace::Requested() const
{
    const auto* M=GetWorld()->GetAuthGameMode<AAC2GameMode>();
    return Preview || (M && M->WantsPortalSpace());
}
void AAC2PortalSpace::StopSounds()
{
    if(auto* Audio=FAC2Audio::Get(GetWorld()))for(uint32 Context:SoundContexts)Audio->Stop(Context);
    SoundContexts.Reset();
}
void AAC2PortalSpace::EndPlay(const EEndPlayReason::Type Reason)
{
    StopSounds();
    Effects.Reset();Assets.Reset();Super::EndPlay(Reason);
}
int32 SAC2PortalSpace::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool) const
{
    if(!Effect.IsValid() || !Effect->Active())return L;
    const float Alpha=Effect->Opacity();
    auto Box=[&](const FSlateBrush* Brush,FLinearColor Tint){FSlateDrawElement::MakeBox(Out,++L,G.ToPaintGeometry(),Brush,ESlateDrawEffect::None,Tint);};
    if(Effect->CoverOpacity()>0)Box(FCoreStyle::Get().GetBrush("WhiteBrush"),FLinearColor(0,0,0,Effect->CoverOpacity()));
    if(Alpha>0 && Effect->Image){
        FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(Effect->Composite?static_cast<UObject*>(Effect->Composite):Effect->Image.Get());
        Box(&Brush,FLinearColor(1,1,1,Alpha));}
    if(Effect->Flash()>0)Box(FCoreStyle::Get().GetBrush("WhiteBrush"),FLinearColor(0,1,1,Effect->Flash()));
    return L;
}
