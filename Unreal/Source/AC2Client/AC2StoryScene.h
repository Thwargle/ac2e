#pragma once

#include "CoreMinimal.h"

// gmKeyframe defines tracks independently. An omitted track keeps its last value.
struct FAC2StoryFrame
{
    uint32 Video = 0;
    uint32 Audio = 0;
    uint32 Voice = 0;
    uint32 Tracks = 0;
    double Duration = -1;
    FString Subtitle;
};
