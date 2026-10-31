#pragma once
#include "AC2Assets.h"
#include "AC2Effects.h"
#include "GameFramework/Actor.h"
#include "Widgets/SLeafWidget.h"
#include "AC2PortalSpace.generated.h"

struct FAC2PortalSounds
{
    uint32 Enter=0,Exit=0;
    TArray<uint32> Loops;
    bool Decode(TConstArrayView<uint8> Bytes);
};

// PortalSpace::Effect: independent of world lighting and streaming lifetime.
UCLASS()
class AAC2PortalSpace : public AActor
{
    GENERATED_BODY()
public:
    AAC2PortalSpace();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Requested() const;
    bool Active() const {return Phase!=0 || Requested();}
    // Never blend an incompletely streamed destination through the entry fade.
    float CoverOpacity() const {return Requested() || (Phase>0 && Phase<4) ? 1.f : 0.f;}
    float Opacity() const {return Phase==1?FMath::Min(Age,1.f):Phase==4?0:1;}
    float Flash() const {return Phase==3?FMath::Min(Age,1.f):Phase==4?FMath::Max(0.f,1-Age):0;}
    UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> Image;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Composite;
    UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Mesh;
    // Used by the render smoke, never enabled by normal launch.
    bool Preview=false;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAC2PortalArrivalTest;
#endif
    struct FRing {float Distance=0,Radius=0;FTransform Frame;};
    TArray<FRing> Rings;
    TArray<int32> Indices;
    TUniquePtr<FAC2AssetLibrary> Assets;
    TUniquePtr<FAC2Effects> Effects;
    int32 First=0,Phase=0;
    float Position=3,Seed=0,Age=0,Time=0;
    FAC2PortalSounds Sounds;
    TArray<uint32> SoundContexts;
    void StopSounds();
    bool Initialize();
    void Generate(int32 Index);
    void UpdateGeometry();
};

class SAC2PortalSpace : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SAC2PortalSpace){} SLATE_ARGUMENT(AAC2PortalSpace*,Effect) SLATE_END_ARGS()
    void Construct(const FArguments& Args){Effect=Args._Effect;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D::ZeroVector;}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
private:
    TWeakObjectPtr<AAC2PortalSpace> Effect;
};
