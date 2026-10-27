#pragma once
#include "CoreMinimal.h"

// CEnvCell portals/shared cells define interior membership. Dungeon coordinates
// can overlap other landblocks; proximity alone must never join those spaces.
struct FAC2CellConnectivity
{
    TMap<uint32, TArray<uint32>> Links;
    TSet<uint32> Outside;

    void Connect(uint32 A, uint32 B)
    {
        if (!B || A == B) return;
        Links.FindOrAdd(A).AddUnique(B);
        Links.FindOrAdd(B).AddUnique(A);
    }
    TSet<uint32> Reachable(uint32 Cell) const
    {
        TSet<uint32> Result;
        TArray<uint32> Queue;
        if (Cell) { if (Links.Contains(Cell)) Queue.Add(Cell); }
        else Queue = Outside.Array();
        for (int32 I = 0; I < Queue.Num(); ++I)
        {
            const uint32 Id = Queue[I];
            if (Result.Contains(Id)) continue;
            Result.Add(Id);
            if (const auto* Next = Links.Find(Id))
                for (uint32 N : *Next) if (!Result.Contains(N)) Queue.Add(N);
        }
        return Result;
    }
    bool ReachesOutside(uint32 Cell) const
    {
        if (!Cell) return true;
        for (uint32 Id : Reachable(Cell)) if (Outside.Contains(Id)) return true;
        return false;
    }
};
