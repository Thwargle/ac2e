#pragma once
#include "AC2Water.h"

struct FAC2ShoreSegment
{
    FVector2D A,B;
    FVector2D OffshoreA=FVector2D::ZeroVector,OffshoreB=FVector2D::ZeroVector;
    double FoamExtentA=1.,FoamExtentB=1.;
    double AlongA=0,AlongB=0,PhaseA=0,PhaseB=0;
    bool Waves=true;

    FBox2D Bounds() const
    {
        FBox2D Box(ForceInit);
        Box+=A; Box+=B;
        // The gridded transmission mask measures distance on both sides of the contour.
        return Box.ExpandBy(FMath::Max(OffshoreA.Size(),OffshoreB.Size())+1.);
    }
};
// Returned coordinates are world centimetres. A connected contour is resolved
// once, independently of the near/far streaming window and request order.
TArray<FAC2ShoreSegment> AC2ShoreContours(uint32 Block,float Height,const FAC2LandscapeDefs& Defs,
    const TMap<uint32,FAC2LandBlock>& Neighborhood,FAC2DatArchive* Cell);
