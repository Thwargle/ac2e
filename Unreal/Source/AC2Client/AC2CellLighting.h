#pragma once
#include "AC2AssetReader.h"
#include "AC2PreviewLighting.h"

// CLightInfo::StreamPack / RLE::Decode, CEnvCell::flush_cells and LightIndices.
// Keep light lists on their owning cell: proximity alone illuminates through walls.
struct FAC2CellLight : FAC2PreviewLight
{
    bool Sunlight = false;
    TMap<uint32, TArray<uint16>> Vertices;
};
inline FLinearColor AC2NativeRGB(FColor C)
{ return FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f); }
inline bool AC2LightIndices(FAC2AssetReader& R, uint32 Block, TMap<uint32,TArray<uint16>>& Out)
{
    const int32 Cells=R.Count(8);
    for(int32 C=0; C<Cells && R.Good; ++C)
    {
        const uint32 Cell=Block | (R.Word() & 0xffff);
        const int32 Count=R.Count(0);
        auto& Indices=Out.FindOrAdd(Cell);
        if(!Count) continue;
        const int32 Bytes=R.Count(1);
        if(Bytes&1) { R.Good=false; break; }
        const int32 End=R.Pos+Bytes;
        while(R.Good && R.Pos<End)
        {
            const uint16 V=R.Short();
            if(V&0x8000)
            {
                if(R.Pos+2>End) { R.Good=false; break; }
                const int16 Run=R.Short();
                if(Run<0 || Indices.Num()+Run>Count) { R.Good=false; break; }
                for(int32 I=0;I<Run;++I) Indices.Add(uint16((V&0x7fff)+I));
            }
            else Indices.Add(V);
            if(Indices.Num()>Count) R.Good=false;
        }
        if(Indices.Num()!=Count) R.Good=false;
        R.Align();
    }
    return R.Good;
}
inline bool DecodeAC2CellLights(FAC2AssetReader& R, uint32 Id,
    TFunctionRef<void(FAC2AssetReader&,TMap<uint32,TArray<uint8>>&)> Properties,
    TArray<FAC2CellLight>& Lights)
{
    if(R.Word()!=Id) return false;
    const int32 Count=R.Count(20);
    for(int32 I=0;I<Count && R.Good;++I)
    {
        R.Word(); // owning cell
        TMap<uint32,TArray<uint8>> Props; Properties(R,Props);
        FAC2CellLight L;
        for(const auto& P:Props)
        {
            FAC2AssetReader V(P.Value);
            switch(P.Key)
            {
            case 0x40000002: { const uint32 Kind=V.Word(); L.Directional=Kind==0x40000002; L.Sunlight=Kind!=0x40000001 && !L.Directional; break; }
            case 0x40000003: L.Direction=V.Vector(); break;
            case 0x40000004: V.Word(); L.Range=V.Float()*100; break;
            case 0x40000008: L.Diffuse=AC2NativeRGB(FColor(V.Word())); break;
            case 0x40000009: V.Word(); L.Intensity=V.Float(); break;
            default: break;
            }
            if(!V.Good) R.Good=false;
        }
        L.Position=R.Vector()*100;
        AC2LightIndices(R,Id&0xffff0000,L.Vertices);
        Lights.Add(MoveTemp(L));
    }
    const int32 Entries=R.Word()&0xffff;
    for(int32 I=0;I<Entries && R.Good;++I) { R.Word(); const int32 Keys=R.Count(); R.Take(Keys*4); }
    if(R.Word()) for(int32 I=0;I<9 && R.Good;++I) { TMap<uint32,TArray<uint16>> Sun; AC2LightIndices(R,Id&0xffff0000,Sun); }
    if(!R.End()) { Lights.Reset(); return false; }
    return true;
}
inline FColor AC2PackLight(FLinearColor C)
{
    auto Byte=[](float V){return uint8(FMath::Clamp(V,0.f,1.f)*255.f);};
    return FColor(Byte(C.R),Byte(C.G),Byte(C.B),255);
}
inline TArray<FColor> BakeAC2CellLights(uint32 Cell, const FTransform& Frame,
    TConstArrayView<FVector> Positions,TConstArrayView<FVector> Normals,
    TConstArrayView<FAC2CellLight> Lights, FLinearColor Ambient, bool HasLightInfo)
{
    TArray<FColor> Colors;
    Colors.Init(AC2PackLight(HasLightInfo?Ambient:FLinearColor(.4,.4,.4)),Positions.Num());
    for(const auto& L:Lights)
    {
        const auto* Indices=L.Vertices.Find(Cell);
        if(L.Sunlight || !Indices) continue;
        const FVector Local=Frame.InverseTransformPosition(L.Position);
        for(uint16 Index:*Indices)
        {
            if(!Positions.IsValidIndex(Index) || !Normals.IsValidIndex(Index)) break;
            FVector Direction=Frame.InverseTransformVectorNoScale(-L.Direction);
            float Attenuation=L.Intensity;
            if(!L.Directional)
            {
                Direction=Local-Positions[Index]; const double D=Direction.Size();
                Direction/=FMath::Max(D,.02);
                Attenuation/=1+10*D*D/FMath::Square(FMath::Max(L.Range,.02f));
            }
            const float Dot=FVector::DotProduct(Normals[Index],Direction);
            if(Dot>.0002f) Colors[Index]=AC2PackLight(AC2NativeRGB(Colors[Index])+L.Diffuse*(FMath::Min(Dot,1.f)*Attenuation));
        }
    }
    return Colors;
}
