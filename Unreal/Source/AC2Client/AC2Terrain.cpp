#include "AC2Terrain.h"

bool FAC2LandscapeDefs::Decode(TConstArrayView<uint8> Bytes, FString &Error)
{
    *this = {};
    Error.Reset();
    // LandscapeDefs::UnPack, admin 0x00629D60. DID then 48 bytes and 256 floats.
    if (Bytes.Num() != 1076 || AC2::U32(Bytes.GetData()) != 0x36000000)
    {
        Error = TEXT("Unsupported LandscapeDefs envelope or length");
        return false;
    }
    CellSize = AC2::F32(Bytes.GetData() + 4);
    CellsPerBlock = AC2::U32(Bytes.GetData() + 8);
    Subdivisions = AC2::U32(Bytes.GetData() + 12);
    if (CellSize != 20.0f || CellsPerBlock != 8 || Subdivisions != 2)
    {
        Error = TEXT("Unsupported landscape lattice (expected 20m / 8 cells / 2 subdivisions)");
        return false;
    }
    uint64 TotalBits = 0;
    for (int32 I = 0; I < 6; ++I)
        TotalBits += AC2::U32(Bytes.GetData() + 28 + I * 4);
    const uint32 TerrainBits = AC2::U32(Bytes.GetData() + 28);
    if (!TerrainBits || TotalBits > 32)
    {
        Error = TEXT("Invalid landscape cell-info bit fields");
        return false;
    }
    TerrainMask = TerrainBits == 32 ? MAX_uint32 : (1u << TerrainBits) - 1;
    for (int32 I = 0; I < 256; ++I)
    {
        const float Height = AC2::F32(Bytes.GetData() + 52 + I * 4);
        if (!FMath::IsFinite(Height) || FMath::Abs(Height) > 100000)
        {
            Error = TEXT("Invalid landscape height table");
            Heights.Reset();
            return false;
        }
        Heights.Add(Height);
    }
    return true;
}

bool FAC2LandBlock::Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes, FString &Error)
{
    *this = {};
    Error.Reset();
    // CLandBlockData::StreamPack, admin 0x0048F310 / client0930 0x004916A0.
    // Counted bytes are NOT aligned before the following count. Alignment is at the end.
    if (Bytes.Num() != 1468 || AC2::U32(Bytes.GetData()) != ExpectedId || (ExpectedId & 0xFFFF) != 0xFFFF ||
        AC2::U32(Bytes.GetData() + 12) != 289 || AC2::U32(Bytes.GetData() + 305) != 289)
    {
        Error = FString::Printf(TEXT("Unsupported CLandBlockData layout for 0x%08X"), ExpectedId);
        return false;
    }
    Id = ExpectedId;
    InfoId = AC2::U32(Bytes.GetData() + 4);
    PathId = AC2::U32(Bytes.GetData() + 8);
    Heights.Append(Bytes.GetData() + 16, 289);
    for (int32 I = 0; I < 289; ++I)
        CellInfo.Add(AC2::U32(Bytes.GetData() + 309 + I * 4));
    return true;
}

bool SampleAC2Terrain(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                      double X, double Y, double &Height, FVector &Normal)
{
    if (Defs.Heights.Num() != 256 || Block.Heights.Num() != 289 || Block.CellInfo.Num() != 289 ||
        !FMath::IsFinite(X) || !FMath::IsFinite(Y) || X < 0 || Y < 0 || X >= 160 || Y >= 160)
        return false;
    const int32 IX = int32(X / 10), IY = int32(Y / 10);
    const double FX = X / 10 - IX, FY = Y / 10 - IY;
    const int32 Indices[] = {IX * 17 + IY, (IX + 1) * 17 + IY, IX * 17 + IY + 1, (IX + 1) * 17 + IY + 1};
    FVector V[4];
    for (int32 I = 0; I < 4; ++I)
    {
        if ((Block.CellInfo[Indices[I]] & Defs.TerrainMask) == 0x1e)
            return false;
        V[I] = FVector((IX + (I & 1)) * 10, (IY + (I >> 1)) * 10, Defs.Heights[Block.Heights[Indices[I]]]);
    }
    const uint32 GX = (Block.Id >> 24) * 16 + IX, GY = ((Block.Id >> 16) & 255) * 16 + IY;
    const bool Cut = (((GY * GX * GX * 0x39557fcf - 0x4a6506db) * GY - GX * 0x6b3c5f11 + 0xae226ceb) & 0x80000000) != 0;
    int32 A = 0, B = 1, C = 2;
    if (Cut) { B = FX >= FY ? 1 : 3; C = FX >= FY ? 3 : 2; }
    else if (FX + FY > 1) { A = 3; B = 2; C = 1; }
    Normal = FVector::CrossProduct(V[B] - V[A], V[C] - V[A]).GetSafeNormal();
    if (Normal.Z <= SMALL_NUMBER)
        return false;
    Height = V[A].Z - (Normal.X * (X - V[A].X) + Normal.Y * (Y - V[A].Y)) / Normal.Z;
    return true;
}

FVector FitAC2GroundFrill(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                         const FTransform &Frame, const FVector &Vertex, double BaseZ)
{
    FVector Result = Frame.TransformPosition(Vertex);
    const FVector Base = Frame.TransformPosition(FVector(Vertex.X, Vertex.Y, BaseZ));
    double Height;
    FVector Normal;
    if (SampleAC2Terrain(Defs, Block, Base.X / 100., Base.Y / 100., Height, Normal))
        Result.Z += Height * 100. - Base.Z;
    return Result;
}

TArray<FVector> SplitAC2GroundFrill(const FAC2LandBlock &Block, const FTransform &Frame,
                                  const FVector &A, const FVector &B, const FVector &C, double BaseZ)
{
    const FVector Base[] = {Frame.TransformPosition(FVector(A.X,A.Y,BaseZ))/100.,
        Frame.TransformPosition(FVector(B.X,B.Y,BaseZ))/100.,
        Frame.TransformPosition(FVector(C.X,C.Y,BaseZ))/100.};
    auto Point = [&](const FVector &W) { return Base[0]*W.X+Base[1]*W.Y+Base[2]*W.Z; };
    FBox Bounds(ForceInit);
    for(const FVector &V:Base)Bounds+=V;
    TArray<FVector> Planes;
    const int32 X0=FMath::Clamp(FMath::FloorToInt(Bounds.Min.X/10),0,15);
    const int32 X1=FMath::Clamp(FMath::FloorToInt(Bounds.Max.X/10),0,15);
    const int32 Y0=FMath::Clamp(FMath::FloorToInt(Bounds.Min.Y/10),0,15);
    const int32 Y1=FMath::Clamp(FMath::FloorToInt(Bounds.Max.Y/10),0,15);
    for(int32 X=X0+1;X<=X1;++X)Planes.Add(FVector(1,0,X*10));
    for(int32 Y=Y0+1;Y<=Y1;++Y)Planes.Add(FVector(0,1,Y*10));
    for(int32 X=X0;X<=X1;++X)for(int32 Y=Y0;Y<=Y1;++Y)
    {
        const uint32 GX=(Block.Id>>24)*16+X, GY=((Block.Id>>16)&255)*16+Y;
        const bool Cut=(((GY*GX*GX*0x39557fcf-0x4a6506db)*GY-GX*0x6b3c5f11+0xae226ceb)&0x80000000)!=0;
        Planes.AddUnique(Cut?FVector(1,-1,(X-Y)*10):FVector(1,1,(X+Y+1)*10));
    }
    TArray<TArray<FVector>> Pieces;
    Pieces.Add({FVector(1,0,0),FVector(0,1,0),FVector(0,0,1)});
    for(const FVector &Plane:Planes)
    {
        auto Distance=[&](const FVector &W){const FVector P=Point(W);return P.X*Plane.X+P.Y*Plane.Y-Plane.Z;};
        const int32 Count=Pieces.Num();
        for(int32 I=0;I<Count;++I)
        {
            const auto Polygon=Pieces[I];
            double Low=0,High=0;
            for(const FVector &W:Polygon){const double D=Distance(W);Low=FMath::Min(Low,D);High=FMath::Max(High,D);}
            // A card lying along an edge belongs to one piece, not both neighbours.
            if(Low>=-1.e-8 || High<=1.e-8)continue;
            TArray<FVector> Front,Back;
            FVector Prev=Polygon.Last(); double PrevD=Distance(Prev);
            for(const FVector &W:Polygon)
            {
                const double D=Distance(W);
                if((D>0 && PrevD<0)||(D<0 && PrevD>0))
                {
                    const FVector Hit=FMath::Lerp(Prev,W,PrevD/(PrevD-D));
                    Front.Add(Hit);Back.Add(Hit);
                }
                if(D>=0)Front.Add(W);
                if(D<=0)Back.Add(W);
                Prev=W;PrevD=D;
            }
            Pieces[I]=MoveTemp(Front);Pieces.Add(MoveTemp(Back));
        }
    }
    TArray<FVector> Triangles;
    for(const auto &Polygon:Pieces)
        for(int32 I=1;I+1<Polygon.Num();++I)
            Triangles.Append({Polygon[0],Polygon[I],Polygon[I+1]});
    return Triangles;
}

TMap<uint32, FAC2LandBlock> LoadAC2TerrainNeighborhood(FAC2DatArchive& Cell, uint32 Center, int32 Radius)
{
    TMap<uint32, FAC2LandBlock> Blocks;
    TArray<uint8> Bytes;FString Error;
    for(int32 X=FMath::Max(0,int32(Center>>8)-Radius);X<=FMath::Min(254,int32(Center>>8)+Radius);++X)
        for(int32 Y=FMath::Max(0,int32(Center&255)-Radius);Y<=FMath::Min(254,int32(Center&255)+Radius);++Y)
        {
            const uint32 Id=(X<<24)|(Y<<16)|0xffff;
            FAC2LandBlock Land;
            if(Cell.Read(Id,Bytes,Error) && Land.Decode(Id,Bytes,Error))Blocks.Add(Id>>16,MoveTemp(Land));
        }
    return Blocks;
}

FAC2TerrainMesh BuildAC2TerrainMesh(const FAC2LandscapeDefs &Defs, const FAC2LandBlock &Block,
                                  const TMap<uint32, FAC2LandBlock>* Neighborhood)
{
    FAC2TerrainMesh Mesh;
    if (Defs.Heights.Num() != 256 || Block.Heights.Num() != 289 || Block.CellInfo.Num() != 289)
        return Mesh;
    const float Spacing = Defs.CellSize / Defs.Subdivisions * 100.0f;
    for (int32 X = 0; X < 17; ++X)
    {
        for (int32 Y = 0; Y < 17; ++Y)
        {
            Mesh.Vertices.Add(FVector(X * Spacing, Y * Spacing, Defs.Heights[Block.Heights[X * 17 + Y]] * 100.0f));
            Mesh.UVs.Add(FVector2D(X / 16.0, Y / 16.0));
        }
    }
    const uint32 BlockX = Block.Id >> 24, BlockY = (Block.Id >> 16) & 255;
    for (int32 X = 0; X < 16; ++X)
    {
        for (int32 Y = 0; Y < 16; ++Y)
        {
            const int32 A = X * 17 + Y, B = A + 17, C = A + 1, D = B + 1;
            // CacheFarValues: polygons touching terrain type 0x1E are cave mouths.
            if ((Block.CellInfo[A] & Defs.TerrainMask) == 0x1E || (Block.CellInfo[B] & Defs.TerrainMask) == 0x1E ||
                (Block.CellInfo[C] & Defs.TerrainMask) == 0x1E || (Block.CellInfo[D] & Defs.TerrainMask) == 0x1E)
                continue;
            // CLandBlockData::CachePackValues deterministic diagonal, uint32 wrapping.
            const uint32 GX = BlockX * 16 + X, GY = BlockY * 16 + Y;
            const uint32 Seed = (GY * GX * GX * 0x39557FCFu - 0x4A6506DBu) * GY - GX * 0x6B3C5F11u + 0xAE226CEBu;
            // UE front faces use clockwise winding in this axis mapping.
            if (Seed & 0x80000000u)
                Mesh.Indices.Append({A, D, B, A, C, D});
            else
                Mesh.Indices.Append({A, C, B, B, C, D});
        }
    }
    // CLandBlockData::CacheFarValues / GetNormalFromTriangle (admin 0x0048FE10,
    // 0x0048F830): sum unit face normals, including triangles in adjacent blocks.
    // The same normal also selects the native surface's slope band.
    auto Neighbor=[&](int32 X,int32 Y,bool Vertex=false)->const FAC2LandBlock* {
        const int32 Limit=Vertex?17:16;
        const int32 BX=int32(BlockX)+(X<0?-1:X>=Limit?1:0);
        const int32 BY=int32(BlockY)+(Y<0?-1:Y>=Limit?1:0);
        return Neighborhood && BX>=0 && BX<=254 && BY>=0 && BY<=254
            ? Neighborhood->Find((BX<<8)|BY):nullptr;
    };
    auto Height=[&](int32 X,int32 Y) {
        if(X>=0 && X<=16 && Y>=0 && Y<=16)return Defs.Heights[Block.Heights[X*17+Y]];
        const auto* Land=Neighbor(X,Y,true);
        if(!Land)return 0.f; // Retail's absent-neighbor height fallback.
        const int32 NX=X<0?X+16:X>16?X-16:X,NY=Y<0?Y+16:Y>16?Y-16:Y;
        return Defs.Heights[Land->Heights[NX*17+NY]];
    };
    Mesh.Normals.Init(FVector::ZeroVector, Mesh.Vertices.Num());
    const int32 Start=Neighborhood?-1:0,End=Neighborhood?17:16;
    for(int32 X=Start;X<End;++X)for(int32 Y=Start;Y<End;++Y)
    {
        const uint32 GX=BlockX*16+X,GY=BlockY*16+Y;
        const bool Available=(X>=0 && X<16 && Y>=0 && Y<16)||Neighbor(X,Y);
        const bool Cut=Available && (((GY*GX*GX*0x39557fcfu-0x4a6506dbu)*GY-GX*0x6b3c5f11u+0xae226cebu)&0x80000000u);
        auto Face=[&](FIntPoint A,FIntPoint B,FIntPoint C) {
            auto Position=[&](FIntPoint P){return FVector(P.X*Spacing,P.Y*Spacing,Height(P.X,P.Y)*100.);};
            const FVector N=FVector::CrossProduct(Position(B)-Position(A),Position(C)-Position(A)).GetSafeNormal();
            for(FIntPoint P:{A,B,C})if(P.X>=0 && P.X<=16 && P.Y>=0 && P.Y<=16)Mesh.Normals[P.X*17+P.Y]+=N;
        };
        if(Cut){Face({X,Y},{X+1,Y},{X+1,Y+1});Face({X,Y},{X+1,Y+1},{X,Y+1});}
        else{Face({X,Y},{X+1,Y},{X,Y+1});Face({X+1,Y},{X+1,Y+1},{X,Y+1});}
    }
    for (FVector &N : Mesh.Normals)
        N = N.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    return Mesh;
}
