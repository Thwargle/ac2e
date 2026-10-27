#pragma once
#include "AC2Assets.h"

struct FAC2SkyReplacement
{
    uint32 Name = 0, Graphic = 0;
    float Pitch = 0, Heading = 0;
    FLinearColor Color = FLinearColor::White;
};
struct FAC2SkyKey
{
    float Time = 0, FogMin = 0, FogMax = 1000, FogSky = 0, Direct = 1, Heading = 0, Pitch = 45, Ambient = 0.5f;
    FLinearColor FogColor = FLinearColor::White, DirectColor = FLinearColor::White, AmbientColor = FLinearColor::White;
    float Rain = 0, WindX = 0, WindY = 0;
    TArray<FAC2SkyReplacement> Replacements;
    // Native CDayDesc::CalcLightVector: direction the light travels, not toward its source.
    FVector LightDirection() const;
};
struct FAC2SkyObject
{
    uint32 Name = 0, Graphic = 0, Water = 0;
    float Begin = 0, End = 0, BeginPitch = 0, EndPitch = 0, BeginHeading = 0, EndHeading = 0;
    bool WestToEast = false, After = false;
};
struct FAC2SkyDay
{
    uint32 Id = 0;
    TArray<FAC2SkyKey> Keys;
    TArray<FAC2SkyObject> Objects;
    bool Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes);
    FAC2SkyKey Sample(float Time) const;
    TArray<FAC2SkyReplacement> Positions(float Time) const;
};
namespace AC2
{
float SkyTimeRatio(float Before, float After, float Time);
float SkySweep(float Begin, float End, float Ratio, bool West);
} // namespace AC2

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class UWorld;
class FAC2SkyRenderer
{
  public:
    ~FAC2SkyRenderer();
    bool Open(UWorld *World, const FString &Directory, FString &Error);
    void Update(const FVector &Viewer, float DeltaTime);
    void SetHour(float Hour);
    void SetWeather(int32 Index);
    int32 WeatherIndex = 0;
    float RainOverride = -1;
    float Hour() const
    {
        return DayFraction * 24;
    }
    int32 ObjectCount() const;
    uint32 DayId() const
    {
        return Day.Id;
    }
    FAC2SkyKey Lighting() const
    {
        return Day.Sample(DayFraction);
    }
    float TimeScale = 1;

  private:
    struct FInstance
    {
        AActor *Actor = nullptr;
        bool Rendered = false;
    };
    TMap<uint64, FInstance> Instances;
    FAC2SkyDay Day;
    TArray<FAC2SkyDay> Days;
    TArray<float> Weights;
    uint32 DayNumber = 0;
    AExponentialHeightFog *Fog = nullptr;
    TUniquePtr<FAC2AssetLibrary> Assets;
    UWorld *World = nullptr;
    ADirectionalLight *Sun = nullptr;
    ASkyLight *Ambient = nullptr;
    float DayFraction = 0.5f, DayLength = 14400;
};
