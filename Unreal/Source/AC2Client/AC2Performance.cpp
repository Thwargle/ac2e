#include "AC2Performance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace AC2Performance
{
namespace { TMap<FName, double> Totals; }
bool Enabled()
{
    static const bool Value=FParse::Param(FCommandLine::Get(),TEXT("AC2Performance"));
    return Value;
}
void Add(FName Name,double Milliseconds) { Totals.FindOrAdd(Name)+=Milliseconds; }
void Reset() { Totals.Reset(); }
FString Report(int32 Frames)
{
    FString Result;
    for(const auto& Pair:Totals) Result+=FString::Printf(TEXT("%s%s=%.3f\n"),*Pair.Key.ToString(),
        Pair.Key==TEXT("draw_calls")?TEXT(""):TEXT("_ms"),Pair.Value/FMath::Max(1,Frames));
    return Result;
}
}
