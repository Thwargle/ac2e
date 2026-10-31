#include "AC2Water.h"
#include "AC2AssetReader.h"

void AppendAC2WaterMesh(uint32 Block, const FAC2BlockWater& Water,
                       const FVector& Offset, FAC2TerrainMesh& Mesh)
{
    if(!Water.Present || !FMath::IsFinite(Water.Height) || !FMath::IsFinite(Water.Tiling) || Water.Tiling<=0)return;
    const int32 N=Mesh.Vertices.Num();
    const float Z=Water.Height*100, T=160/Water.Tiling;
    const FVector2D Base((Block>>8)*T,(Block&255)*T);
    Mesh.Vertices.Append({Offset+FVector(0,0,Z),Offset+FVector(16000,0,Z),
                          Offset+FVector(0,16000,Z),Offset+FVector(16000,16000,Z)});
    Mesh.Indices.Append({N,N+2,N+1,N+1,N+2,N+3});
    Mesh.Normals.Append({FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector});
    Mesh.UVs.Append({Base,Base+FVector2D(T,0),Base+FVector2D(0,T),Base+FVector2D(T,T)});
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
                R.Take(44);
                uint32 C = R.Word();
                for (int32 J = 0; J < 7; ++J)
                    if (((C >> (J * 3)) & 7) == 1)
                        R.Take(8);
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
