#pragma once
#include "AC2AssetReader.h"

// CBaseMesh/CStaticMesh::StreamPack and CPolygon::UnPack (Legions 7160).
// Called after render degrades. Material-free meshes still carry the authored
// walkable ramps; visual triangles and alpha blending do not define collision.
inline bool ReadAC2StaticCollision(FAC2AssetReader& R, int32 Vertices, TArray<int32>& Out, bool CellMesh = false)
{
    auto Polygons = [&](TArray<TArray<int32>>* Faces) {
        const int32 Count = R.Count(4, 65536);
        for (int32 I=0;I<Count && R.Good;++I)
        {
            R.Short(); const int32 Points=R.Short();
            TArray<int32> Face;
            for (int32 J=0;J<Points && R.Good;++J)
            {
                const int32 Index=R.Short();
                if (Index>=Vertices) R.Good=false;
                Face.Add(Index);
            }
            R.Align();
            if (Faces && R.Good) Faces->Add(MoveTemp(Face));
        }
        return Count;
    };
    int32 Budget=131072;
    auto Tree = [&](auto&& Self,int32 Polys,int32 Depth)->void {
        if (!R.Good || Depth>256 || --Budget<0) {R.Good=false;return;}
        const uint32 Tag=R.Word();
        if (Tag==0x4c454146) // LEAF: solid, sphere, polygon slots
        {
            R.Take(20); const int32 Count=R.Count(2,65536);
            for (int32 I=0;I<Count && R.Good;++I) if (R.Short()>=Polys) R.Good=false;
            R.Align(); return;
        }
        if (Tag!=0x42502d4e && Tag!=0x42502d2d && Tag!=0x422d2d4e && Tag!=0x4250464c)
        {R.Good=false;return;}
        R.Take(16); // splitting plane
        if (Tag==0x42502d4e || Tag==0x42502d2d) Self(Self,Polys,Depth+1);
        if (Tag==0x42502d4e || Tag==0x422d2d4e) Self(Self,Polys,Depth+1);
        R.Take(16); // bounding sphere
    };
    const int32 Render=Polygons(nullptr);
    if (Render) Tree(Tree,Render,0);
    TArray<TArray<int32>> Faces;
    const int32 Physics=Polygons(&Faces);
    if (R.Word()) Tree(Tree,Physics,0);
    TSet<int32> Portals;
    if (CellMesh)
    {
        // CCellMesh::StreamPack appends a CELL_BSP and portal references to
        // CStaticMesh. Portal polygons bound the room but are open passages.
        // CELL_BSP uses plain BSPNODEs (including its leaves), with no sphere
        // or physics polygon list: BSPNODE::UnPack in the retail/admin client.
        auto CellTree = [&](auto&& Self,int32 Depth)->void {
            if (!R.Good || Depth>256 || --Budget<0) {R.Good=false;return;}
            const uint32 Tag=R.Word();
            if (Tag!=0x4c454146 && Tag!=0x42502d4e && Tag!=0x42502d2d && Tag!=0x422d2d4e && Tag!=0x4250464c)
            {R.Good=false;return;}
            R.Take(16);
            if (Tag==0x42502d4e || Tag==0x42502d2d) Self(Self,Depth+1);
            if (Tag==0x42502d4e || Tag==0x422d2d4e) Self(Self,Depth+1);
        };
        CellTree(CellTree,0);
        const int32 Count=R.Count(20,65536);
        for (int32 I=0;I<Count && R.Good;++I)
        {
            R.Short(); const int32 Index=R.Short(); R.Take(16);
            if (Index>=Physics) R.Good=false;
            Portals.Add(Index);
        }
        R.Word(); // cell mesh info DID
    }
    if (!R.End()) return false;
    TArray<int32> Indices;
    for (int32 I=0;I<Faces.Num();++I)
        if (!Portals.Contains(I))
            for (int32 J=1;J+1<Faces[I].Num();++J)
                Indices.Append({Faces[I][0],Faces[I][J+1],Faces[I][J]});
    Out=MoveTemp(Indices); return true;
}
