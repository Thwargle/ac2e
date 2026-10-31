#pragma once
#include "CoreMinimal.h"
#include "RenderCommandFence.h"

// CPU uploads can finish before deferred component updates reach the renderer.
// Allow a complete frame submission, then fence those commands before revealing
// a destination. A new arrival or newly received object invalidates that frame.
class FAC2ScenePresentationBarrier
{
public:
    void Reset() { Stage=0; }
    bool Poll(bool Ready, uint64 Revision=0)
    {
        if(!Ready || Revision!=ObservedRevision) { Reset(); ObservedRevision=Revision; }
        if(!Ready)return false;
        if(Stage==0) { Frame=GFrameCounter; Stage=1; return false; }
        if(Stage==1 && GFrameCounter>Frame)
        { Fence.BeginFence(); Frame=GFrameCounter; Stage=2; return false; }
        return Stage==2 && GFrameCounter>Frame && Fence.IsFenceComplete();
    }
private:
    FRenderCommandFence Fence;
    uint64 Frame=0,ObservedRevision=0;
    uint8 Stage=0;
};
