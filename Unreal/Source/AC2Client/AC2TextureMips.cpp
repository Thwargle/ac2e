#include "AC2TextureMips.h"
#include "Async/ParallelFor.h"

float AC2TextureMips::Coverage(TConstArrayView<FColor> Pixels,float Threshold)
{
    int32 Count=0;
    for(const FColor& P:Pixels)Count+=P.A/255.f>=Threshold;
    return Pixels.IsEmpty()?0:float(Count)/Pixels.Num();
}
TArray<FColor> AC2TextureMips::Downsample(TConstArrayView<FColor> Pixels,int32 Width,int32 Height,bool Cutout)
{
    const int32 W=FMath::Max(1,Width/2),H=FMath::Max(1,Height/2);
    TArray<FColor> Result;Result.SetNumUninitialized(W*H);
    ParallelFor(TEXT("AC2Mip"),H,32,[&](int32 Y){
        for(int32 X=0;X<W;++X)
        {
            FLinearColor Sum(0,0,0,0);float Alpha=0;
            for(int32 DY=0;DY<2;++DY)for(int32 DX=0;DX<2;++DX)
            {
                const FLinearColor C(Pixels[FMath::Min(Y*2+DY,Height-1)*Width+FMath::Min(X*2+DX,Width-1)]);
                Sum+=C*(Cutout?C.A:1.f);Alpha+=C.A;
            }
            Sum/=Cutout?FMath::Max(Alpha,SMALL_NUMBER):4.f;
            Sum.A=Alpha*.25f;
            Result[Y*W+X]=Sum.ToFColorSRGB();
        }
    },EParallelForFlags::BackgroundPriority);
    return Result;
}
void AC2TextureMips::PreserveCoverage(TArray<FColor>& Pixels,float Target,float Threshold)
{
    if(Pixels.IsEmpty() || Target<=0 || Target>=1)return;
    // Pick the alpha cutoff whose coverage is closest, without turning clear
    // texels opaque. A histogram makes this linear rather than sorting pixels.
    int32 Histogram[256]={};for(const FColor& P:Pixels)++Histogram[P.A];
    int32 Visible=0,Best=FMath::CeilToInt(Threshold*255);
    float Error=FMath::Abs(Coverage(Pixels,Threshold)-Target);
    for(int32 A=255;A>=1;--A)
    {
        Visible+=Histogram[A];
        const float E=FMath::Abs(float(Visible)/Pixels.Num()-Target);
        if(E<Error){Error=E;Best=A;}
    }
    const float Scale=FMath::Clamp(FMath::CeilToFloat(Threshold*255)/Best,.25f,4.f);
    for(FColor& P:Pixels)P.A=uint8(FMath::Clamp(FMath::RoundToInt(P.A*Scale),0,255));
}
