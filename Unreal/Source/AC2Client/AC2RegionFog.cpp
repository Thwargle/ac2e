#include "AC2RegionFog.h"
#include "AC2AssetReader.h"
#include "AC2Sky.h"
#include "AC2WorldGrid.h"

bool FAC2RegionFog::Decode(uint32 DescId,TConstArrayView<uint8> Bytes,uint32& DistanceMapId)
{
    Types.Reset();Values.Reset();Width=Height=0;
    // The serialized default is optional. Match FogInfo/LightInfo constructors.
    Default={};Default.FogMax=10000;Default.FogDensity=.4f;Default.FogType=1;
    Default.Direct=Default.Ambient=1;Default.Combine=0x92492;
    Default.FogColor=Default.DirectColor=Default.AmbientColor=FLinearColor::White;
    FAC2AssetReader R(Bytes);
    R.Word();
    if(R.Word()!=DescId || (DescId>>24)!=0x17)return false;
    DistanceMapId=R.Word();
    R.Take(12); // volume / bottom / top map IDs; GetFogCorners uses distance only.
    auto ReadType=[&](){
        const uint32 Index=R.Word();FAC2WaterLightKey K;
        K.FogMin=R.Float();K.FogMax=R.Float();K.FogSky=R.Float();K.FogDensity=R.Float();K.FogType=R.Word();
        auto Color=[&](){const uint32 C=R.Word();return FLinearColor(float((C>>16)&255)/255,float((C>>8)&255)/255,float(C&255)/255,float(C>>24)/255);};
        K.FogColor=Color();K.Direct=R.Float();K.DirectColor=Color();K.Ambient=R.Float();K.AmbientColor=Color();K.Combine=R.Word();
        for(int32 I=0;I<7;++I)if(((K.Combine>>(I*3))&7)==1){K.Multipliers[I].X=R.Float();K.Multipliers[I].Y=R.Float();}
        if(K.FogType>1)R.Good=false;
        return TPair<uint32,FAC2WaterLightKey>(Index,K);
    };
    if(R.Word())Default=ReadType().Value;
    const int32 N=R.Count(48,256);
    for(int32 I=0;I<N && R.Good;++I){auto Pair=ReadType();Types.Add(Pair.Key,Pair.Value);}
    return R.End();
}

bool FAC2RegionFog::DecodeMap(uint32 MapId,TConstArrayView<uint8> Bytes)
{
    Width=Height=0;Values.Reset();
    FAC2AssetReader R(Bytes);
    if(R.Word()!=MapId)return false;
    const uint32 H=R.Word(),W=R.Word();
    if(!H || !W || H>256 || W>256 || uint64(H)*W!=uint64(R.Left()))return false;
    Values.Append(R.Data.GetData()+R.Pos,W*H);Width=W;Height=H;
    return true;
}

bool FAC2RegionFog::Load(FAC2DatArchive& Portal,uint32 DescId,FString& Error)
{
    TArray<uint8> Bytes;uint32 Map=0;
    if(!Portal.Read(DescId,Bytes,Error) || !Decode(DescId,Bytes,Map) ||
        (Map && (!Portal.Read(Map,Bytes,Error) || !DecodeMap(Map,Bytes)))){
        *this={};Error=TEXT("Unsupported native region fog description or distance map");return false;
    }
    return true;
}

const FAC2WaterLightKey& FAC2RegionFog::At(uint32 X,uint32 Y) const
{
    // BlockMap::GetValue returns its first byte for out-of-range samples.
    const uint32 Index=X<Width && Y<Height ? X*Height+Y : 0;
    const uint32 Type=Values.IsValidIndex(Index)?Values[Index]:0;
    const auto* Found=Types.Find(Type);
    return Found?*Found:Default;
}

FAC2SkyKey FAC2RegionFog::Apply(const FVector& ViewerCm,const FAC2SkyKey& Day) const
{
    uint32 Block=0;
    if(Values.IsEmpty() || !AC2::WorldBlock(ViewerCm,Block))return Day;
    const uint32 X=Block>>8,Y=Block&255;
    const FAC2WaterLightKey* Keys[]={&At(X,Y),&At(X,Y+1),&At(X+1,Y),&At(X+1,Y+1)};
    if(!Keys[0]->FogType && !Keys[1]->FogType && !Keys[2]->FogType && !Keys[3]->FogType)return Day;
    const FVector Local=(ViewerCm-AC2::BlockOrigin(Block))/AC2::BlockCentimetres;
    const float U=FMath::Clamp(float(Local.X),0.f,1.f),V=FMath::Clamp(float(Local.Y),0.f,1.f);
    const float Weights[]={(1-U)*(1-V),(1-U)*V,U*(1-V),U*V};
    FAC2SkyKey Result=Day;
    Result.FogType=1;Result.FogMin=Result.FogMax=Result.FogSky=Result.Direct=Result.Ambient=0;
    Result.FogColor=Result.DirectColor=Result.AmbientColor=FLinearColor(0,0,0,0);
    for(int32 I=0;I<4;++I){
        // Apply authored max/min/normalization before interpolation, as retail does.
        const auto K=AC2CombineFogLighting(*Keys[I],Day);const float W=Weights[I];
        Result.FogMin+=K.FogMin*W;Result.FogMax+=K.FogMax*W;Result.FogSky+=K.FogSky*W;
        Result.Direct+=K.Direct*W;Result.Ambient+=K.Ambient*W;
        Result.FogColor+=K.FogColor*W;Result.DirectColor+=K.DirectColor*W;Result.AmbientColor+=K.AmbientColor*W;
    }
    // Native CornerInterp copies alpha from southwest rather than interpolating.
    const auto SW=AC2CombineFogLighting(*Keys[0],Day);
    Result.FogColor.A=SW.FogColor.A;Result.DirectColor.A=SW.DirectColor.A;Result.AmbientColor.A=SW.AmbientColor.A;
    return Result;
}
