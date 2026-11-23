#pragma once
#include "CoreMinimal.h"

// Bounded reconstruction for misplaced furnishings in the installed Legions DAT.
// These are placement repairs, not recovered retail values. Keep the cell,
// ordinal, entity and original coordinate guards so other copies stay authored.
inline bool RepairAC2AcademyPlacement(uint32 Cell,int32 Ordinal,uint32 Entity,
    FVector& Origin,const FVector& BlockOffset,const TArray<FVector>& Floors)
{
    if(Cell==0x03fd0111 && ((Ordinal==26 && Entity==0x470000fc && Origin.Equals(FVector(.67,1.94,-21.9),.03)) ||
        (Ordinal==25 && Entity==0x4700016b && Origin.Equals(FVector(.68,1.96,-21.9),.03))))
    {
        // Fire and smoke share the adjacent brazier's bowl, preserving their
        // authored height and the small separation between the two emitters.
        Origin+=FVector(1.6,.92,0);return true;
    }
    const bool MushroomA=Ordinal==2 && Entity==0x4700058a && Origin.Equals(FVector(-291.15,351.79,-69.47),.03);
    const bool MushroomB=Ordinal==6 && Entity==0x47000592 && Origin.Equals(FVector(-290.24,351.17,-69.52),.03);
    if(Cell!=0x03fd012a || (!MushroomA && !MushroomB))return false;
    const FVector Root=Origin*100+BlockOffset;
    // Plant the whole cluster on this sloping floor. Sampling only its root
    // leaves the downhill stems suspended even when the centre touches ground.
    const FVector Extent=MushroomA?FVector(31,41,0):FVector(21,25,0);
    double Floor=DBL_MAX;
    for(const FVector& Delta:{FVector::ZeroVector,Extent,FVector(-Extent.X,Extent.Y,0),-Extent,FVector(Extent.X,-Extent.Y,0)})
    {
      const FVector P=Root+Delta;
      double Highest=-DBL_MAX;
      for(int32 I=0;I+2<Floors.Num();I+=3)
      {
        const FVector A=Floors[I],B=Floors[I+1],C=Floors[I+2];
        const double Den=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
        if(FMath::Abs(Den)<.01)continue;
        const double U=((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y))/Den;
        const double V=((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y))/Den;
        if(U<0 || V<0 || U+V>1)continue;
        const double Z=U*A.Z+V*B.Z+(1-U-V)*C.Z;
        if(Z<=P.Z && Z>=P.Z-600)Highest=FMath::Max(Highest,Z);
      }
      // The uphill edge can extend into the room wall. Require the root's
      // floor, then use supported corners; a wall is not a second floor.
      if(Highest==-DBL_MAX){if(Delta.IsZero())return false;continue;}
      Floor=FMath::Min(Floor,Highest);
    }
    // The static setup roots lie slightly above the bottom vertex of the stem.
    Origin.Z=(Floor-BlockOffset.Z)/100+(MushroomA?.0460055:.0356795);
    return true;
}
