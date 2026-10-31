#pragma once
#include "CoreMinimal.h"

// Opt-in frame measurements; disabled in ordinary play.
namespace AC2Performance
{
bool Enabled();
void Add(FName Name, double Milliseconds);
void Reset();
FString Report(int32 Frames);
struct FScope
{
    FName Name;
    double Start;
    explicit FScope(FName InName) : Name(InName), Start(Enabled() ? FPlatformTime::Seconds() : 0) {}
    ~FScope() { if(Start) Add(Name, (FPlatformTime::Seconds()-Start)*1000); }
};
}
