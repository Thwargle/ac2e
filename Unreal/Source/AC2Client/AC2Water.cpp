#include "AC2Water.h"
#include "AC2AssetReader.h"
#include "AC2WorldGrid.h"
#include "AC2ShoreContours.h"
#include "AC2Sky.h"

namespace {
// Clip the native foam triangles to each landblock's streaming ownership.
// Keep affine attributes through each cut; the water surface is a separate pass.
void AppendFoamRibbon(const FAC2ShoreSegment& S,FVector2D Origin,FVector Offset,
    double Height,FAC2ShoreWaterMesh& Mesh)
{
    if(!S.Waves)return;
    using FPolygon=TArray<FVector4d,TInlineAllocator<8>>;
    const double Repeat=FMath::FloorToDouble(FMath::Min(S.AlongA,S.AlongB));
    auto Vertex=[&](FVector2D XY,double U,double V){return FVector4d(XY.X-Origin.X,XY.Y-Origin.Y,U-Repeat,V);};
    const FVector4d A=Vertex(S.A,S.AlongA,0), B=Vertex(S.B,S.AlongB,0);
    const FVector4d OuterA=Vertex(S.A+S.OffshoreA*S.FoamExtentA,S.AlongA,1);
    const FVector4d OuterB=Vertex(S.B+S.OffshoreB*S.FoamExtentB,S.AlongB,1);
    auto Triangle=[&](FPolygon Poly){
        for(int32 Axis=0;Axis<2;++Axis)for(int32 Side=0;Side<2;++Side){
            if(Poly.IsEmpty())return;
            const double Limit=Side ? 16000. : 0.;
            FPolygon Next;
            FVector4d Prev=Poly.Last();
            for(const FVector4d& P:Poly){
                const bool In=Side ? P[Axis]<=Limit : P[Axis]>=Limit;
                const bool PrevIn=Side ? Prev[Axis]<=Limit : Prev[Axis]>=Limit;
                if(In!=PrevIn)Next.Add(FMath::Lerp(Prev,P,(Limit-Prev[Axis])/(P[Axis]-Prev[Axis])));
                if(In)Next.Add(P);
                Prev=P;
            }
            Poly=MoveTemp(Next);
        }
        if(Poly.Num()<3)return;
        const int32 Base=Mesh.Foam.Vertices.Num();
        for(const auto& P:Poly){
            Mesh.Foam.Vertices.Add(Offset+FVector(P.X,P.Y,Height));
            Mesh.Foam.Normals.Add(FVector::UpVector);
            Mesh.Foam.UVs.Add(FVector2D(P.X,P.Y)/4000.);
            const double Along=(P.Z+Repeat-S.AlongA)/(S.AlongB-S.AlongA);
            Mesh.FoamMaskUVs.Add({FMath::Lerp(S.PhaseA,S.PhaseB,Along),P.W});
            Mesh.FoamPassUVs.Add({P.Z,1});
        }
        for(int32 I=1;I+1<Poly.Num();++I){
            const auto E=Poly[I]-Poly[0],F=Poly[I+1]-Poly[0];
            const double Area=E.X*F.Y-E.Y*F.X;
            if(FMath::Abs(Area)<.001)continue;
            if(Area<0)Mesh.Foam.Indices.Append({Base,Base+I,Base+I+1});
            else Mesh.Foam.Indices.Append({Base,Base+I+1,Base+I});
        }
    };
    // A single diagonal across a converging quad squeezes the entire wave
    // texture onto one side. Interpolate its narrowing width as it approaches
    // the meeting point. Regular strips keep retail's two triangles.
    const bool Converges=S.FoamExtentA<1. || S.FoamExtentB<1.;
    const int32 Rows=Converges?FMath::Max(1,FMath::CeilToInt(FMath::Max(
        S.OffshoreA.Size()*S.FoamExtentA,S.OffshoreB.Size()*S.FoamExtentB)/100.)):1;
    FVector4d Left=A,Right=B;
    for(int32 I=1;I<=Rows;++I){
        const double V=double(I)/Rows;
        const FVector4d NextLeft=FMath::Lerp(A,OuterA,V),NextRight=FMath::Lerp(B,OuterB,V);
        Triangle({Left,NextLeft,Right});
        Triangle({Right,NextLeft,NextRight});
        Left=NextLeft;Right=NextRight;
    }
}
}

void AppendAC2ShoreWaterMesh(uint32 Block,const FAC2BlockWater& Water,const FVector& Offset,
    const FAC2LandscapeDefs& Defs,const TMap<uint32,FAC2LandBlock>& Neighborhood,FAC2ShoreWaterMesh& Mesh,FAC2DatArchive* Cell)
{
    if(!Water.Present || !FMath::IsFinite(Water.Height))return;
    const FVector Origin=AC2::BlockOrigin(Block);
    const FVector2D WorldOrigin(Origin.X,Origin.Y);
    const auto Contours=AC2ShoreContours(Block,Water.Height,Defs,Neighborhood,Cell);
    for(const auto& S:Contours)AppendFoamRibbon(S,WorldOrigin,Offset,Water.Height*100.,Mesh);
    constexpr int32 TileSize=4000;
    for(int32 TX=0;TX<4;++TX)for(int32 TY=0;TY<4;++TY){
        const FVector2D Start(TX*TileSize,TY*TileSize);
        const FBox2D Bounds(WorldOrigin+Start,WorldOrigin+Start+FVector2D(TileSize,TileSize));
        TArray<const FAC2ShoreSegment*,TInlineAllocator<16>> Nearby;
        for(const auto& Segment:Contours)
            if(Segment.Bounds().Intersect(Bounds))Nearby.Add(&Segment);
        const bool Shore=!Nearby.IsEmpty();
        const int32 Steps=Shore?20:1, Stride=Steps+1;
        FAC2TerrainMesh& Target=Shore?Mesh.Strip:static_cast<FAC2TerrainMesh&>(Mesh);
        const int32 Base=Target.Vertices.Num();
        for(int32 Y=0;Y<=Steps;++Y)for(int32 X=0;X<=Steps;++X){
            const FVector2D P=Start+FVector2D(X,Y)*(double(TileSize)/Steps);
            Target.Vertices.Add(Offset+FVector(P.X,P.Y,Water.Height*100.));
            Target.Normals.Add(FVector::UpVector);
            Target.UVs.Add(P/TileSize);
            if(Shore){
                const FVector2D WorldP=WorldOrigin+P;
                double V=1.;
                for(const FAC2ShoreSegment* S:Nearby){
                    const FVector2D Along=S->B-S->A;
                    const double T=FMath::Clamp(FVector2D::DotProduct(WorldP-S->A,Along)/FMath::Max(Along.SizeSquared(),.0001),0.,1.);
                    const double Distance=(WorldP-(S->A+Along*T)).Size();
                    const double Width=FMath::Lerp(S->OffshoreA.Size(),S->OffshoreB.Size(),T);
                    V=FMath::Min(V,Distance/FMath::Max(Width,1.));
                }
                Mesh.StripUVs.Add({0,V});
            }else Mesh.ShoreUVs.Add({0,1});
            if(X<Steps && Y<Steps){
                const int32 N=Base+Y*Stride+X;
                Target.Indices.Append({N,N+Stride,N+1,N+1,N+Stride,N+Stride+1});
            }
        }
    }
}

void AppendAC2WaterMesh(uint32 Block, const FAC2BlockWater& Water,
                       const FVector& Offset, FAC2TerrainMesh& Mesh)
{
    if(!Water.Present || !FMath::IsFinite(Water.Height) || !FMath::IsFinite(Water.Tiling) || Water.Tiling<=0)return;
    // LScapeTerrainMesh::UpdateNearWater: four 40 m tiles along each axis,
    // each with its own 0..1 UVs. WaterType::m_nTileWidth is not a UV divisor.
    // Use the same mesh for the distant blocks to avoid a change in wavelength.
    for(int32 X=0;X<4;++X)for(int32 Y=0;Y<4;++Y)
    {
        const int32 N=Mesh.Vertices.Num();
        const FVector Origin=Offset+FVector(X*4000,Y*4000,Water.Height*100);
        Mesh.Vertices.Append({Origin,Origin+FVector(4000,0,0),
                              Origin+FVector(0,4000,0),Origin+FVector(4000,4000,0)});
        Mesh.Indices.Append({N,N+2,N+1,N+1,N+2,N+3});
        Mesh.Normals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
        Mesh.UVs.Append({{0,0},{1,0},{0,1},{1,1}});
    }
}

uint8 FAC2WaterSource::FMap::At(uint32 X, uint32 Y) const
{
    const uint32 I = X < W && Y < H ? X * H + Y : 0;
    return Values.IsValidIndex(I) ? Values[I] : 0;
}
bool FAC2WaterSource::FMap::Decode(TConstArrayView<uint8> Bytes, uint32 Id)
{
    FAC2AssetReader R(Bytes);
    if (R.Word() != Id)
        return false;
    H = R.Word();
    W = R.Word();
    if (!H || !W || H > 256 || W > 256 || uint64(W) * H > uint64(R.Left()))
        return false;
    Values.Reset();
    Values.Append(R.Data.GetData() + R.Pos, W * H);
    return true;
}
bool FAC2WaterSource::Decode(TConstArrayView<uint8> Bytes, uint32 &HeightMap, uint32 &TypeMap)
{
    WaterTypes.Reset();
    Default = {};
    FAC2AssetReader R(Bytes);
    R.Word();
    if (R.Word() != 0x16000000)
        return false;
    HeightMap = R.Word();
    TypeMap = R.Word();
    auto ReadType = [&]() {
        uint32 Index = R.Word();
        FAC2BlockWater W;
        W.Present = R.Word() == 0;
        if (W.Present)
        {
            W.Above = R.Word();
            W.Below = R.Word();
            R.Word();
            W.Tiling = R.Float();
            R.Float();
            W.Color = R.Word();
            int32 N = R.Count(48, 256);
            for (int32 I = 0; I < N && R.Good; ++I)
            {
                FAC2WaterLightKey K;K.Depth=R.Float();K.FogMin=R.Float();K.FogMax=R.Float();K.FogSky=R.Float();
                K.FogDensity=R.Float();K.FogType=R.Word();
                auto Color=[&](){const uint32 C=R.Word();return FLinearColor(float((C>>16)&255)/255,float((C>>8)&255)/255,float(C&255)/255,float(C>>24)/255);};
                K.FogColor=Color();K.Direct=R.Float();K.DirectColor=Color();K.Ambient=R.Float();K.AmbientColor=Color();
                uint32 C = K.Combine=R.Word();
                for (int32 J = 0; J < 7; ++J)
                    if (((C >> (J * 3)) & 7) == 1)
                        {K.Multipliers[J].X=R.Float();K.Multipliers[J].Y=R.Float();}
                W.Lighting.Add(K);
            }
            if (W.Tiling <= 0)
                R.Good = false;
        }
        return TPair<uint32, FAC2BlockWater>(Index, W);
    };
    if (R.Word())
        Default = ReadType().Value;
    const int32 N = R.Count(8, 256);
    for (int32 I = 0; I < N && R.Good; ++I)
    {
        auto P = ReadType();
        WaterTypes.Add(P.Key, P.Value);
    }
    return R.End();
}
FAC2SkyKey AC2CombineFogLighting(const FAC2WaterLightKey& K,const FAC2SkyKey& Day)
{
    auto Out=Day;
    auto Value=[&](float A,float B,int32 Field)->float{switch((K.Combine>>(Field*3))&7){
        case 1:return A*K.Multipliers[Field].X+B*K.Multipliers[Field].Y;
        case 2:return A;case 3:return B;case 4:return FMath::Max(A,B);case 5:return FMath::Min(A,B);default:return A*B;}};
    auto Color=[&](FLinearColor A,FLinearColor B,int32 Field){return FLinearColor(Value(A.R,B.R,Field),Value(A.G,B.G,Field),Value(A.B,B.B,Field),Value(A.A,B.A,Field));};
    Out.Ambient=Value(Day.Ambient,K.Ambient,0);Out.AmbientColor=Color(Day.AmbientColor,K.AmbientColor,1);
    Out.Direct=Value(Day.Direct,K.Direct,2);Out.DirectColor=Color(Day.DirectColor,K.DirectColor,3);
    // CalcDayLight uses the current (not already combined) lighting. The native
    // combineDayLight selector is not read by that routine. Color math is in
    // authored display RGB; alpha comes from the directional light color.
    FLinearColor DayColor=Day.DirectColor*FMath::Abs(Day.Direct)+Day.AmbientColor*Day.Ambient;
    DayColor.A=Day.DirectColor.A;
    if(DayColor.R<-.0002f || DayColor.G<-.0002f || DayColor.B<-.0002f || DayColor.A<-.0002f ||
       DayColor.R>1.0002f || DayColor.G>1.0002f || DayColor.B>1.0002f || DayColor.A>1.0002f){
        const float Max=FMath::Max3(DayColor.R,DayColor.G,DayColor.B);
        if(Max!=0){DayColor.R/=Max;DayColor.G/=Max;DayColor.B/=Max;}
    }
    Out.FogColor=Color(Color(DayColor,Day.FogColor,5),K.FogColor,6);
    Out.FogType=K.FogType;Out.FogMin=K.FogMin;Out.FogMax=K.FogMax;Out.FogSky=K.FogSky;
    return Out;
}
FAC2SkyKey AC2WaterLighting(const FAC2BlockWater& Water,float Depth,const FAC2SkyKey& Day)
{
    if(Water.Lighting.IsEmpty() || Depth<0)return Day;
    auto Key=[&](const FAC2WaterLightKey& K){return AC2CombineFogLighting(K,Day);};
    int32 Upper=0;while(Upper<Water.Lighting.Num() && Depth>=Water.Lighting[Upper].Depth)++Upper;
    if(Upper==0)return Key(Water.Lighting[0]);
    if(Upper==Water.Lighting.Num())return Key(Water.Lighting.Last());
    const auto& A=Water.Lighting[Upper-1];const auto& B=Water.Lighting[Upper];
    const float T=FMath::Clamp((Depth-A.Depth)/FMath::Max(.00001f,B.Depth-A.Depth),0.f,1.f);
    auto Out=Key(A);const auto End=Key(B);
    // Native CalcWaterLight forces linear fog between distinct depth keys.
    Out.FogType=1;
    Out.Direct=FMath::Lerp(Out.Direct,End.Direct,T);Out.DirectColor=FMath::Lerp(Out.DirectColor,End.DirectColor,T);
    Out.Ambient=FMath::Lerp(Out.Ambient,End.Ambient,T);Out.AmbientColor=FMath::Lerp(Out.AmbientColor,End.AmbientColor,T);
    Out.FogMin=FMath::Lerp(Out.FogMin,End.FogMin,T);Out.FogMax=FMath::Lerp(Out.FogMax,End.FogMax,T);
    Out.FogSky=FMath::Lerp(Out.FogSky,End.FogSky,T);Out.FogColor=FMath::Lerp(Out.FogColor,End.FogColor,T);
    // The original interpolation retains the deeper key's alpha components.
    Out.DirectColor.A=End.DirectColor.A;Out.AmbientColor.A=End.AmbientColor.A;Out.FogColor.A=End.FogColor.A;
    return Out;
}
bool FAC2WaterSource::Load(FAC2DatArchive &Portal, FString &Error)
{
    TArray<uint8> B;
    uint32 H = 0, T = 0;
    if (!Portal.Read(0x16000000, B, Error) || !Decode(B, H, T) || !Portal.Read(H, B, Error) || !Heights.Decode(B, H) ||
        !Portal.Read(T, B, Error) || !Types.Decode(B, T))
    {
        Error = TEXT("Unsupported native water maps");
        return false;
    }
    return true;
}
FAC2BlockWater FAC2WaterSource::At(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Land) const
{
    const uint32 X = Land.Id >> 24, Y = (Land.Id >> 16) & 255;
    const auto *Found = WaterTypes.Find(Types.At(X, Y));
    auto W = Found ? *Found : Default;
    if (!W.Present || Defs.Heights.Num() != 256 || Land.Heights.IsEmpty())
        return {};
    W.Height = Defs.Heights[Heights.At(X, Y)] + .95f;
    float Min = TNumericLimits<float>::Max();
    for (uint8 H : Land.Heights)
        Min = FMath::Min(Min, Defs.Heights[H]);
    // Explicit content repair, not a retail rule: 74A9's dry map value cuts
    // the lake off before its north/east bank. Borrow the adjoining lake
    // plane only while this block remains dry in the DAT. Terrain depth testing
    // supplies the real shoreline. Keep in sync with server Geometry::water_height.
    if((Land.Id>>16)==0x74a9 && Min>=W.Height && Types.At(X,Y)==Types.At(0x75,0xa9))
    {
        const float Adjacent=Defs.Heights[Heights.At(0x75,0xa9)]+.95f;
        bool WetEdge=false;
        for(int32 I=0;I<17 && Land.Heights.Num()==289;++I)
            WetEdge|=Defs.Heights[Land.Heights[16*17+I]]<Adjacent;
        if(WetEdge && Min<Adjacent)W.Height=Adjacent;
    }
    W.Present = Min < W.Height;
    return W;
}
