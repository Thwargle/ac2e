#include "AC2ShoreContours.h"
#include "AC2WorldGrid.h"
#include "Misc/ScopeLock.h"

namespace {
FIntPoint Key(const FVector2D& P) { return {int32(FMath::RoundToInt(P.X)),int32(FMath::RoundToInt(P.Y))}; }
struct FContours {
    TSet<uint32> Examined;
    TMap<uint32,TArray<FAC2ShoreSegment>> Bins;
};

// GenerateShore assigns the same V phase to a whole strip and accumulates U
// along its bank. Order the endpoints in world space so a restarted streamer
// reaches the same strip origin from either side of a landblock boundary.
void AssignStripCoordinates(TArray<FAC2ShoreSegment>& Shore)
{
    TMap<FIntPoint,TArray<int32>> Links;
    for(int32 I=0;I<Shore.Num();++I){
        Links.FindOrAdd(Key(Shore[I].A)).Add(I);
        Links.FindOrAdd(Key(Shore[I].B)).Add(I);
    }
    TArray<FIntPoint> Keys;
    Links.GetKeys(Keys);
    auto Before=[](FIntPoint A,FIntPoint B){return A.X==B.X ? A.Y<B.Y : A.X<B.X;};
    Keys.Sort(Before);
    TBitArray<> Visited(false,Shore.Num());
    auto Walk=[&](FIntPoint Start){
        FIntPoint Point=Start;
        double Length=0;
        TArray<int32> Strip;
        do{
            int32 Next=INDEX_NONE;
            FIntPoint End=Point;
            for(int32 Index:Links.FindChecked(Point)){
                if(Visited[Index])continue;
                const auto& S=Shore[Index];
                const FIntPoint Other=Key(S.A)==Point ? Key(S.B) : Key(S.A);
                if(Next==INDEX_NONE || Before(Other,End)){Next=Index;End=Other;}
            }
            if(Next==INDEX_NONE)break;
            auto& S=Shore[Next];
            const double NextLength=Length+(S.B-S.A).Size();
            const bool Forward=Key(S.A)==Point;
            S.AlongA=Forward ? Length : NextLength;
            S.AlongB=Forward ? NextLength : Length;
            Length=NextLength;
            Visited[Next]=true;
            Strip.Add(Next);
            Point=End;
        }while(Point!=Start && Links.FindChecked(Point).Num()==2);
        if(Strip.IsEmpty())return;
        const bool Closed=Point==Start;
        // Close the phase on loops to avoid a texture cut where the ends meet.
        const double Scale=Closed && Length>0 ? FMath::Max(1.,FMath::RoundToDouble(Length/1000.))/Length : .001;
        // Retail assigns a random V start to each generated shore strip.
        // A lake-wide offset locks both banks to the same wave. Give each
        // landblock its own seed, blending only near the adjoining strip.
        const uint32 StripSeed=GetTypeHash(Start);
        auto PhaseAt=[StripSeed](FVector2D P){
            const FVector2D Tile=P/16000.-FVector2D(.5,.5);
            const FIntPoint Base(FMath::FloorToInt(Tile.X),FMath::FloorToInt(Tile.Y));
            auto Phase=[StripSeed](FIntPoint Cell){
                return .8*(HashCombineFast(StripSeed,GetTypeHash(Cell))&0xffff)/65535.;
            };
            const double X=FMath::SmoothStep(.4,.6,Tile.X-Base.X),Y=FMath::SmoothStep(.4,.6,Tile.Y-Base.Y);
            return FMath::Lerp(FMath::Lerp(Phase(Base),Phase(Base+FIntPoint(1,0)),X),
                FMath::Lerp(Phase(Base+FIntPoint(0,1)),Phase(Base+FIntPoint(1,1)),X),Y);
        };
        for(int32 Index:Strip){
            auto& S=Shore[Index];
            S.AlongA*=Scale; S.AlongB*=Scale; S.PhaseA=PhaseAt(S.A); S.PhaseB=PhaseAt(S.B);
            // DrawShore draws the alpha mask, but skips waves on loops under 20 m.
            S.Waves=!Closed || Length>=2000.;
        }
    };
    for(FIntPoint P:Keys)if(Links[P].Num()!=2)for(int32 I:Links[P])if(!Visited[I])Walk(P);
    for(FIntPoint P:Keys)for(int32 I:Links[P])if(!Visited[I])Walk(P);
}

void Resolve(FContours& Cache,uint32 Seed,double Height,const FAC2LandscapeDefs& Defs,
    const TMap<uint32,FAC2LandBlock>& Neighborhood,FAC2DatArchive* Cell)
{
    TArray<uint32> Queue{Seed};TArray<FAC2ShoreSegment> Shore;TMap<FIntPoint,FVector> Normals;
    for(int32 Q=0;Q<Queue.Num();++Q){
        const uint32 Id=Queue[Q];if(Cache.Examined.Contains(Id))continue;
        Cache.Examined.Add(Id);
        FAC2LandBlock Loaded;const auto* Land=Neighborhood.Find(Id);
        if(!Land && Cell){TArray<uint8> Bytes;FString Error;
            if(Cell->Read((Id<<16)|0xffff,Bytes,Error) && Loaded.Decode((Id<<16)|0xffff,Bytes,Error))Land=&Loaded;}
        if(!Land)continue;
        const auto Terrain=BuildAC2TerrainMesh(Defs,*Land);const FVector Origin=AC2::BlockOrigin(Id);
        const int32 Before=Shore.Num();
        for(int32 I=0;I+2<Terrain.Indices.Num();I+=3){
            FVector V[3];for(int32 K=0;K<3;++K)V[K]=Terrain.Vertices[Terrain.Indices[I+K]]+Origin;
            TArray<FVector2D,TInlineAllocator<3>> Points;
            for(int32 K=0;K<3;++K){const FVector A=V[K],B=V[(K+1)%3];
                if((A.Z<Height)==(B.Z<Height))continue;
                const auto Hit=FMath::Lerp(A,B,(Height-A.Z)/(B.Z-A.Z));Points.AddUnique({Hit.X,Hit.Y});}
            if(Points.Num()!=2 || (Points[1]-Points[0]).SizeSquared()<.01)continue;
            FVector N=FVector::CrossProduct(V[1]-V[0],V[2]-V[0]).GetSafeNormal();if(N.Z<0)N=-N;
            Shore.Add({Points[0],Points[1]});Normals.FindOrAdd(Key(Points[0]))+=N;Normals.FindOrAdd(Key(Points[1]))+=N;
        }
        // Only follow blocks containing the contour, not the whole submerged basin.
        if(Shore.Num()>Before)for(int32 X=-1;X<=1;++X)for(int32 Y=-1;Y<=1;++Y){
            const int32 BX=int32(Id>>8)+X,BY=int32(Id&255)+Y;
            if(BX>=0 && BY>=0 && BX<255 && BY<255){const uint32 Next=(BX<<8)|BY;
                if(!Cache.Examined.Contains(Next))Queue.Add(Next);}}
    }
    AssignStripCoordinates(Shore);
    TMap<FIntPoint,double> FoamLimits;
    for(auto& S:Shore){
        const FVector NA=Normals[Key(S.A)].GetSafeNormal(), NB=Normals[Key(S.B)].GetSafeNormal();
        const FVector2D DirA=FVector2D(NA.X,NA.Y).GetSafeNormal();
        const FVector2D DirB=FVector2D(NB.X,NB.Y).GetSafeNormal();
        S.OffshoreA=DirA*FMath::Max(1.,1200*NA.Z);
        S.OffshoreB=DirB*FMath::Max(1.,1200*NB.Z);
        S.A-=DirA*5;
        S.B-=DirB*5;
        // At an inward bend the two waterward rays can meet before the native
        // outer edge. Beyond that point the strip reverses onto itself.
        const double Det=FVector2D::CrossProduct(S.OffshoreA,S.OffshoreB);
        if(FMath::Abs(Det)>UE_SMALL_NUMBER){
            const FVector2D Edge=S.B-S.A;
            const double TA=FVector2D::CrossProduct(Edge,S.OffshoreB)/Det;
            const double TB=FVector2D::CrossProduct(Edge,S.OffshoreA)/Det;
            if(TA>0 && TB>0){
                auto Limit=[&](FVector2D Point,double T){
                    if(T>=1.)return;
                    double& End=FoamLimits.FindOrAdd(Key(Point),1.);
                    End=FMath::Min(End,T);
                };
                Limit(S.A,TA);
                Limit(S.B,TB);
            }
        }
    }
    for(auto& S:Shore){
        if(const auto* End=FoamLimits.Find(Key(S.A)))S.FoamExtentA=*End;
        if(const auto* End=FoamLimits.Find(Key(S.B)))S.FoamExtentB=*End;
        const FBox2D Bounds=S.Bounds();
        for(int32 X=FMath::Max(0,FMath::FloorToInt(Bounds.Min.X/16000));X<=FMath::Min(254,FMath::FloorToInt(Bounds.Max.X/16000));++X)
        for(int32 Y=FMath::Max(0,FMath::FloorToInt(Bounds.Min.Y/16000));Y<=FMath::Min(254,FMath::FloorToInt(Bounds.Max.Y/16000));++Y)
            Cache.Bins.FindOrAdd((X<<8)|Y).Add(S);
    }
}
}
TArray<FAC2ShoreSegment> AC2ShoreContours(uint32 Block,float Height,const FAC2LandscapeDefs& Defs,
    const TMap<uint32,FAC2LandBlock>& Neighborhood,FAC2DatArchive* Cell)
{
    static FCriticalSection Mutex;static TMap<FString,FContours> Caches;
    FScopeLock Lock(&Mutex);FContours Local;
    // Isolate server profiles, patched archives and their landscape height tables.
    const uint32 HeightsHash=FCrc::MemCrc32(Defs.Heights.GetData(),Defs.Heights.Num()*sizeof(float));
    const FString CacheKey=Cell?FString::Printf(TEXT("%s:%u:%u:%u:%u:%.4f"),*Cell->GetPath(),Cell->GetHeader().Root,
        Cell->GetHeader().FileSize,Cell->GetHeader().GameVersion,HeightsHash,Height):FString();
    auto& Cache=Cell?Caches.FindOrAdd(CacheKey):Local;
    for(int32 X=-1;X<=1;++X)for(int32 Y=-1;Y<=1;++Y){const int32 BX=int32(Block>>8)+X,BY=int32(Block&255)+Y;
        if(BX>=0 && BY>=0 && BX<255 && BY<255)Resolve(Cache,(BX<<8)|BY,Height*100.,Defs,Neighborhood,Cell);}
    if(const auto* Found=Cache.Bins.Find(Block))return *Found;
    return {};
}
