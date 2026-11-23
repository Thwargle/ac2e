#include "AC2Water.h"
#include "AC2ShoreContours.h"
#include "AC2WorldGrid.h"
#include "AC2Assets.h"
#include "AC2Sky.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShoreTexturesTest,"AC2.Data.NativeShoreTextures",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShoreTexturesTest::RunTest(const FString&)
{
    const FString Dir = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2DatArchive Portal, Cell;
    FAC2LandscapeDefs Defs;
    FAC2LandBlock Bank;
    FAC2WaterSource Water;
    FAC2AssetLibrary Assets;
    FString Error;
    TArray<uint8> Bytes;
    // 44.5N, 12.1E on Icefire. Its foam and mask DIDs differ from Legions.
    if (!TestTrue(TEXT("Read the reported bank from the active profile"),
        Portal.Open(Dir / TEXT("portal.dat"), 1, Error) && Cell.Open(Dir / TEXT("cell_1.dat"), 2, Error) &&
        Portal.Read(0x36000000, Bytes, Error) && Defs.Decode(Bytes, Error) &&
        Cell.Read(0x8eb8ffff, Bytes, Error) && Bank.Decode(0x8eb8ffff, Bytes, Error) &&
        Water.Load(Portal, Error) && Assets.Open(Dir, Error))) return false;
    const auto W = Water.At(Defs, Bank);
    if (!TestTrue(TEXT("Reported tile has a water surface"), W.Present)) return false;
    auto* Surface = Cast<UMaterialInstanceDynamic>(Assets.Material(W.Above, true));
    if (!TestNotNull(TEXT("Reported tile water material"), Surface)) return false;
    TestEqual(TEXT("Both wave stages are available"), Surface->K2_GetScalarParameterValue(TEXT("AC2ShoreFoamEnabled")), 1.f);

    for (FName Name : {FName(TEXT("AC2ShoreFoam")), FName(TEXT("AC2ShoreFoamMask")), FName(TEXT("AC2ShoreMask"))})
    {
        auto* Texture = Cast<UTexture2D>(Surface->K2_GetTextureParameterValue(Name));
        if (!TestNotNull(Name.ToString(), Texture) || !Texture->GetPlatformData()) return false;
        auto& Mip = Texture->GetPlatformData()->Mips[0];
        if (!TestEqual(TEXT("Readable RGBA texels"), Mip.BulkData.GetBulkDataSize(), int64(Mip.SizeX)*Mip.SizeY*4)) return false;
        const auto* Pixels = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
        if (!Pixels) { Mip.BulkData.Unlock(); AddError(TEXT("Could not read shoreline pixels")); return false; }
        const FColor Inner = Pixels[Mip.SizeX/2];
        const FColor Middle = Pixels[(Mip.SizeY/2)*Mip.SizeX + Mip.SizeX/2];
        const FColor Outer = Pixels[(Mip.SizeY-1)*Mip.SizeX + Mip.SizeX/2];
        Mip.BulkData.Unlock();
        const bool Waves = Name == TEXT("AC2ShoreFoam");
        TestTrue(TEXT("Animated foam wraps and shoreline masks clamp across the bank"), Texture->AddressY == (Waves ? TA_Wrap : TA_Clamp));
        TestEqual(TEXT("The bank edge is transparent"), Inner.A, uint8(0));
        TestTrue(TEXT("The foam crest and masks use white source pixels"), Middle.R == 255 && Middle.G == 255 && Middle.B == 255);
        TestTrue(TEXT("The crest has partial coverage and both masks rise offshore"), Waves ? Middle.A > 0 && Middle.A < 255 : Middle.A > 200);
        TestEqual(TEXT("Waves fade offshore and deep water retains its reflection mask"), Outer.A, uint8(Name == TEXT("AC2ShoreMask") ? 255 : 0));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShoreMaskTest,"AC2.Data.NativeShoreMask",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShoreMaskTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2WaterSource Water;FString Error;TArray<uint8> Bytes;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open shoreline data"),Portal.Open(Dir/TEXT("portal.dat"),1,Error) && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error) && Water.Load(Portal,Error)))return false;
    const auto Neighborhood=LoadAC2TerrainNeighborhood(Cell,0x8fbc,2);
    bool FoundShore=false;
    TMap<uint32,FAC2ShoreWaterMesh> Surfaces;
    int32 FoldedEnds=0;
    for(uint32 Id:{0x8fbbu,0x8fbcu,0x8fbdu,0x8ebcu})if(const auto* Land=Neighborhood.Find(Id)){
        const auto W=Water.At(Defs,*Land);if(!W.Present)continue;
        for(const auto& S:AC2ShoreContours(Id,W.Height,Defs,Neighborhood,&Cell)){
            if(S.FoamExtentA>=1. && S.FoamExtentB>=1.)continue;
            ++FoldedEnds;
            const FVector2D OA=S.A+S.OffshoreA*S.FoamExtentA, OB=S.B+S.OffshoreB*S.FoamExtentB;
            const double Near=FVector2D::CrossProduct(OA-S.A,S.B-S.A);
            const double Far=FVector2D::CrossProduct(OA-S.B,OB-S.B);
            TestTrue(TEXT("Foam does not fold back over a concave bank"),Near*FMath::Sign(Far)>=-.01 || FMath::Abs(Far)<.01);
        }
        FAC2ShoreWaterMesh Near,Far;
        AppendAC2ShoreWaterMesh(Id,W,FVector::ZeroVector,Defs,Neighborhood,Near,&Cell);
        const FVector Shift=AC2::BlockOrigin(Id)-AC2::BlockOrigin(0x8fbc);
        AppendAC2ShoreWaterMesh(Id,W,Shift,Defs,Neighborhood,Far,&Cell);
        TestEqual(TEXT("Every water vertex has a shoreline mask coordinate"),Near.ShoreUVs.Num(),Near.Vertices.Num());
        TestEqual(TEXT("Every strip vertex has native shoreline coordinates"),Near.StripUVs.Num(),Near.Strip.Vertices.Num());
        TestTrue(TEXT("Streaming preserves strip topology"),Near.Strip.Indices==Far.Strip.Indices);
        TestEqual(TEXT("Foam UV phase is present for every strip vertex"),Near.FoamPassUVs.Num(),Near.Foam.Vertices.Num());
        auto Area=[](const FAC2TerrainMesh& M){double A=0;for(int32 I=0;I<M.Indices.Num();I+=3){
            const auto P=M.Vertices[M.Indices[I]],Q=M.Vertices[M.Indices[I+1]],R=M.Vertices[M.Indices[I+2]];
            A+=FMath::Abs(FVector::CrossProduct(Q-P,R-P).Z)*.5;}return A;};
        TestTrue(TEXT("Open water and ribbon partition the tile without missing or doubled area"),FMath::Abs(Area(Near)+Area(Near.Strip)-256000000)<1);
        TestTrue(TEXT("Shore clipping stays bounded"),Near.Vertices.Num()+Near.Strip.Vertices.Num()<10000);
        TMap<FIntPoint,FVector2D> Phases;
        for(int32 I=0;I<Near.Strip.Vertices.Num();++I){
            const auto& P=Near.Strip.Vertices[I];
            const FIntPoint K(FMath::RoundToInt(P.X),FMath::RoundToInt(P.Y));
            if(const auto* Previous=Phases.Find(K))
                TestTrue(TEXT("Adjacent shoreline tiles agree at every shared vertex"),Previous->Equals(Near.StripUVs[I],.0001));
            else Phases.Add(K,Near.StripUVs[I]);
            TestTrue(TEXT("Streaming preserves shoreline position and animation phase"),(Near.Strip.Vertices[I]+Shift).Equals(Far.Strip.Vertices[I],.001) && Near.StripUVs[I].Equals(Far.StripUVs[I],.0001));
            TestTrue(TEXT("Shore mask is finite and stays within its clamped range"),!Near.StripUVs[I].ContainsNaN() && Near.StripUVs[I].Y>=-.00001 && Near.StripUVs[I].Y<=1.00001);
            TestTrue(TEXT("Foam phase retains precision in half-float vertex UVs"),FMath::Abs(Near.StripUVs[I].X)<512);
            TestTrue(TEXT("Shore ribbon stays inside its owning block"),Near.Strip.Vertices[I].X>=-.01 && Near.Strip.Vertices[I].Y>=-.01 && Near.Strip.Vertices[I].X<=16000.01 && Near.Strip.Vertices[I].Y<=16000.01);
            FoundShore|=Near.StripUVs[I].Y<.9;
        }
        Surfaces.Add(Id,MoveTemp(Near));
    }
    TestTrue(TEXT("Reported bank includes shoreline transparency, not only deep water"),FoundShore);
    TestTrue(TEXT("Reported water includes converging foam edges"),FoldedEnds>0);
    int32 SharedSamples=0;
    auto EdgeUV=[](const FAC2ShoreWaterMesh& M,bool XAxis,double Edge,double Along,FVector& UV){
        for(int32 I=0;I<M.Strip.Indices.Num();I+=3)for(int32 J=0;J<3;++J){
            const int32 A=M.Strip.Indices[I+J],B=M.Strip.Indices[I+(J+1)%3];
            const auto P=M.Strip.Vertices[A],Q=M.Strip.Vertices[B];
            if(FMath::Abs((XAxis?P.X:P.Y)-Edge)>.01 || FMath::Abs((XAxis?Q.X:Q.Y)-Edge)>.01)continue;
            const double U=XAxis?P.Y:P.X,V=XAxis?Q.Y:Q.X;
            if(FMath::Abs(U-V)<.01 || Along<=FMath::Min(U,V)+.01 || Along>=FMath::Max(U,V)-.01)continue;
            const double T=(Along-U)/(V-U);
            UV=FVector(0,0,FMath::Lerp(M.StripUVs[A].Y,M.StripUVs[B].Y,T));return true;
        }return false;};
    for(const auto& Pair:Surfaces)for(bool XAxis:{true,false})if(const auto* Next=Surfaces.Find(Pair.Key+(XAxis?0x100:1))){
        for(double Along=50;Along<16000;Along+=100){FVector A,B;
            const bool HasA=EdgeUV(Pair.Value,XAxis,16000,Along,A),HasB=EdgeUV(*Next,XAxis,0,Along,B);
            TestEqual(TEXT("Active shore mask meets on both sides of a block boundary"),HasA && A.Z<.9999,HasB && B.Z<.9999);
            if(HasA && HasB && A.Z<.9999){++SharedSamples;
                TestTrue(TEXT("Shared edge has identical water transmission mask"),FMath::Abs(A.Z-B.Z)<.001);}
        }}
    TestTrue(TEXT("Test crossed an actual shoreline block edge"),SharedSamples>0);

    // A north/south channel catches scrolls that head the same world direction
    // at both banks. Sample on the wet side of each independently generated strip.
    FAC2LandscapeDefs ChannelDefs;
    ChannelDefs.CellSize=20;ChannelDefs.Subdivisions=2;
    ChannelDefs.Heights.Init(0,256);ChannelDefs.Heights[1]=10;
    TMap<uint32,FAC2LandBlock> ChannelLand;
    auto& Bank=ChannelLand.Add(0x2020);Bank.Id=0x2020ffff;
    Bank.CellInfo.Init(0,289);Bank.Heights.Init(0,289);
    for(int32 X=0;X<17;++X)for(int32 Y=0;Y<17;++Y)Bank.Heights[X*17+Y]=(X<=3 || X>=13);
    FAC2BlockWater ChannelWater;ChannelWater.Present=true;ChannelWater.Height=5;
    FAC2ShoreWaterMesh Channel;
    AppendAC2ShoreWaterMesh(0x2020,ChannelWater,FVector::ZeroVector,ChannelDefs,ChannelLand,Channel);
    int32 Segments[2]={0,0};
    for(int32 I=0;I+2<Channel.Foam.Indices.Num();I+=3){
        int32 Edge[2]={INDEX_NONE,INDEX_NONE},Count=0,Waterward=INDEX_NONE;
        for(int32 J=0;J<3;++J){
            const int32 N=Channel.Foam.Indices[I+J];
            if(Channel.FoamMaskUVs[N].Y==0 && Count<2)Edge[Count++]=N;
            if(Channel.FoamMaskUVs[N].Y==1)Waterward=N;
        }
        if(Count!=2 || Waterward==INDEX_NONE)continue;
        const auto A=Channel.Foam.Vertices[Edge[0]],B=Channel.Foam.Vertices[Edge[1]];
        const auto Outer=Channel.Foam.Vertices[Waterward];
        const bool West=A.X<8000;
        ++Segments[West?0:1];
        TestTrue(TEXT("Scrolling toward decreasing V runs toward the correct bank on both sides"),
            West ? Outer.X>A.X : Outer.X<A.X);
        TestTrue(TEXT("The affine foam coordinate repeats every ten metres along the shore"),
            FMath::IsNearlyEqual(FMath::Abs(Channel.FoamPassUVs[Edge[0]].X-Channel.FoamPassUVs[Edge[1]].X),(B-A).Size()/1000.,.001));
    }
    TestTrue(TEXT("Both opposing banks contain native shore strips"),Segments[0]>0 && Segments[1]>0);
    const auto W=Water.At(Defs,Neighborhood[0x8fbc]);
    TestTrue(TEXT("Native underside material and depth lighting recovered"),W.Below!=0 && W.Lighting.Num()>=2);
    FAC2AssetLibrary Assets;
    if(!TestTrue(TEXT("Open native water materials"),Assets.Open(Dir,Error)))return false;
    auto* SurfaceMaterial=Cast<UMaterialInstanceDynamic>(Assets.Material(W.Above,true));
    if(!TestNotNull(TEXT("Native river surface"),SurfaceMaterial))return false;
    TestEqual(TEXT("Native river texture stages are active"),
        SurfaceMaterial->K2_GetScalarParameterValue(TEXT("AC2NativeStages")),1.f);
    auto* Ripple0=SurfaceMaterial->K2_GetTextureParameterValue(TEXT("AC2Stage0Texture"));
    auto* Ripple1=SurfaceMaterial->K2_GetTextureParameterValue(TEXT("AC2Stage1Texture"));
    TestTrue(TEXT("The river uses both distinct ripple layers selected by the DAT"),
        Ripple0 && Ripple1 && Ripple0!=Ripple1);
    auto* Underside=Cast<UMaterialInstanceDynamic>(Assets.Material(W.Below,true,true));
    if(!TestNotNull(TEXT("Native underside material"),Underside))return false;
    TestTrue(TEXT("Underside instance selects water, not the template's UV checker"),
        Underside->K2_GetTextureParameterValue(TEXT("AC2Stage0Texture"))==Assets.Texture(0x40000056));
    FAC2SkyKey Day;Day.Direct=.9f;Day.Ambient=.8f;
    const auto Surface=AC2WaterLighting(W,0,Day),Deep=AC2WaterLighting(W,30,Day),Mid=AC2WaterLighting(W,15,Day);
    TestTrue(TEXT("Underwater fog tightens at depth"),Deep.FogMax<Surface.FogMax);
    TestTrue(TEXT("Native water lighting interpolates with camera depth"),FMath::IsNearlyEqual(Mid.FogMax,(Surface.FogMax+Deep.FogMax)*.5f) && Mid.FogColor.Equals(FMath::Lerp(Surface.FogColor,Deep.FogColor,.5f)));
    TestTrue(TEXT("Leaving water restores daylight"),AC2WaterLighting(W,-.01f,Day).Direct==Day.Direct);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2WaterContinuityTest,"AC2.Data.NativeWaterContinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2WaterContinuityTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2WaterSource Water;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open reported coastline DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)
        && Water.Load(Portal,Error)))return false;
    FAC2TerrainMesh Joined;
    const FVector Origin=AC2::BlockOrigin(0x84b0);
    uint32 Material=0;
    for(uint32 Block:{0x85b0u,0x86b0u,0x87b0u})
    {
        FAC2LandBlock Land;const uint32 Id=(Block<<16)|0xffff;
        if(!TestTrue(TEXT("Load adjacent lake tiles"),Cell.Read(Id,Bytes,Error)&&Land.Decode(Id,Bytes,Error)))return false;
        const auto W=Water.At(Defs,Land);
        TestTrue(TEXT("Native lake surface exists"),W.Present);
        if(Material)TestEqual(TEXT("Same authored material across the old near/far cutoff"),W.Above,Material);
        Material=W.Above;
        FAC2TerrainMesh Near,Far;
        AppendAC2WaterMesh(Block,W,FVector::ZeroVector,Near);
        AppendAC2WaterMesh(Block,W,AC2::BlockOrigin(Block)-Origin,Far);
        TestEqual(TEXT("Retail uses sixteen 40 m water quads per landblock"),Far.Indices.Num(),96);
        TestTrue(TEXT("One full texture repeat covers 40 m"),(Near.Vertices[1]-Near.Vertices[0]).Equals(FVector(4000,0,0)) && Near.UVs[0]==FVector2D(0,0) && Near.UVs[1]==FVector2D(1,0));
        for(int32 I=0;I<Near.Vertices.Num();++I)
        {
            TestTrue(TEXT("Near and distant water use identical world positions"),
                (Near.Vertices[I]+AC2::BlockOrigin(Block)).Equals(Far.Vertices[I]+Origin,.001));
            TestTrue(TEXT("No texture scale/phase change at the terrain LOD boundary"),Near.UVs[I].Equals(Far.UVs[I],.0001));
        }
        const int32 Previous=Joined.Vertices.Num();
        AppendAC2WaterMesh(Block,W,AC2::BlockOrigin(Block)-Origin,Joined);
        if(Previous)
        {
            for(int32 Y=0;Y<4;++Y) {
                const int32 Left=Previous-15+4*Y,Right=Previous+4*Y;
                TestTrue(TEXT("Adjacent lake edges meet without a crack"),Joined.Vertices[Left].Equals(Joined.Vertices[Right],.001));
                TestTrue(TEXT("Texture repeats meet at the shared edge"),(Joined.UVs[Left]-Joined.UVs[Right]).Equals(FVector2D(1,0),.001));
            }
        }
    }
    TestEqual(TEXT("The same batch spans nearby and distant water"),Joined.Vertices.Num(),192);
    TestTrue(TEXT("Batched triangles use their own tile vertices"),Joined.Indices[192]>=128 && Joined.Indices.Last()==191);
    AppendAC2WaterMesh(0x84b0,{},FVector::ZeroVector,Joined);
    TestEqual(TEXT("Dry DAT tiles do not acquire water"),Joined.Vertices.Num(),192);
    FAC2AssetLibrary Assets;
    if(!TestTrue(TEXT("Open water materials"),Assets.Open(Dir,Error)))return false;
    auto* Surface=Cast<UMaterialInstanceDynamic>(Assets.Material(Material,true));
    if(!TestNotNull(TEXT("Authored water material"),Surface))return false;
    TestEqual(TEXT("Water uses the native texture cascade"),Surface->K2_GetScalarParameterValue(TEXT("AC2NativeStages")),1.f);
    TestEqual(TEXT("No extra PBR shine"),Surface->K2_GetScalarParameterValue(TEXT("AC2Specular")),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ShorelineTest,"AC2.Data.RepairedShoreline74A9",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2ShorelineTest::RunTest(const FString&)
{
    FAC2DatArchive Portal,Cell;FAC2LandscapeDefs Defs;FAC2WaterSource Water;
    TArray<uint8> Bytes;FString Error;
    const FString Dir=AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if(!TestTrue(TEXT("Open shoreline DATs"),Portal.Open(Dir/TEXT("portal.dat"),1,Error)
        && Cell.Open(Dir/TEXT("cell_1.dat"),2,Error)
        && Portal.Read(0x36000000,Bytes,Error) && Defs.Decode(Bytes,Error)
        && Water.Load(Portal,Error)))return false;
    auto Read=[&](uint32 Block,FAC2LandBlock& Land) {
        const uint32 Id=(Block<<16)|0xffff;
        return TestTrue(TEXT("Read shoreline terrain"),Cell.Read(Id,Bytes,Error)&&Land.Decode(Id,Bytes,Error));
    };
    FAC2LandBlock Bank,East,North,West,South;
    if(!Read(0x74a9,Bank)||!Read(0x75a9,East)||!Read(0x74aa,North)
        ||!Read(0x73a9,West)||!Read(0x74a8,South))return false;
    const auto Surface=Water.At(Defs,Bank);
    TestTrue(TEXT("Repaired bank has water"),Surface.Present);
    TestTrue(TEXT("Uses adjacent lake's native height"),FMath::IsNearlyEqual(Surface.Height,96.7f,.001f));
    TestEqual(TEXT("East seam height"),Surface.Height,Water.At(Defs,East).Height);
    TestEqual(TEXT("North seam height"),Surface.Height,Water.At(Defs,North).Height);
    TestEqual(TEXT("Uses native water material"),Surface.Above,Water.At(Defs,East).Above);
    TestFalse(TEXT("Unconnected western depression remains dry"),Water.At(Defs,West).Present);
    TestFalse(TEXT("Southern hillside remains dry"),Water.At(Defs,South).Present);
    for(int32 I=0;I<17;++I)
    {
        TestTrue(TEXT("West boundary is above lake: no new rectangular water edge"),Defs.Heights[Bank.Heights[I]]>=Surface.Height);
        TestTrue(TEXT("South boundary is above lake: no new rectangular water edge"),Defs.Heights[Bank.Heights[I*17]]>=Surface.Height);
    }
    double Ground=0;FVector Normal;
    TestTrue(TEXT("Sample reported shore location"),SampleAC2Terrain(Defs,Bank,55.5,155.4,Ground,Normal));
    TestTrue(TEXT("Reported missing-water point is submerged"),Ground<Surface.Height);
    return true;
}
#endif
