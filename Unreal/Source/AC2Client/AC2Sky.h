#pragma once
#include "AC2Assets.h"
#include "AC2GameTime.h"
#include "AC2RegionFog.h"

struct FAC2SkyReplacement
{
    uint32 Name = 0, Graphic = 0;
    float Pitch = 0, Heading = 0;
    FLinearColor Color = FLinearColor::White;
    bool After = false;
};
struct FAC2SkyKey
{
    // Native LScapeFogType: 0 disables distance fog, 1 is linear range fog.
    uint32 FogType = 1;
    float Time = 0, FogMin = 0, FogMax = 1000, FogSky = 0, Direct = 1, Heading = 0, Pitch = 45, Ambient = 0.5f;
    // Native display RGB, not linear-light values. Convert only at UE light inputs.
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
enum class EAC2CelestialBody : uint8 { None, Sun, Moon };
struct FAC2SkyDay
{
    uint32 Id = 0;
    TArray<FAC2SkyKey> Keys;
    TArray<FAC2SkyObject> Objects;
    bool Decode(uint32 ExpectedId, TConstArrayView<uint8> Bytes);
    FAC2SkyKey Sample(float Time) const;
    TArray<FAC2SkyReplacement> Positions(float Time,bool InWater=false) const;
    EAC2CelestialBody CelestialBody(uint32 ObjectName) const;
};
namespace AC2
{
float SkyTimeRatio(float Before, float After, float Time);
float SkySweep(float Begin, float End, float Ratio, bool West);
} // namespace AC2

class ADirectionalLight;
class ASkyLight;
class UWorld;
class FAC2SkyRenderer
{
  public:
    ~FAC2SkyRenderer();
    bool Open(UWorld *World, const FString &Directory, FString &Error);
    void Update(const FVector &Viewer, float DeltaTime, TOptional<double> ServerTime = {});
    void SetInteriorFog(TOptional<FAC2SkyKey> Fog) { InteriorFogOverride=MoveTemp(Fog); }
    void SetHour(float Hour);
    void SetWeather(int32 Index);
    bool IsSubmerged() const {return SubmergedWater.IsSet();}
    void SetWaterView(const FAC2BlockWater* Water,float Depth)
    {
        if(Water && Depth>=0){SubmergedWater=*Water;WaterDepth=Depth;}else SubmergedWater.Reset();
    }
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
        auto K=RegionFog.Apply(CurrentViewer,Day.Sample(DayFraction));
        if(CelestialDirection.IsSet())
        {
            const auto D=-CelestialDirection.GetValue();
            K.Heading=FMath::RadiansToDegrees(FMath::Atan2(D.X,D.Y));
            K.Pitch=FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z,-1.,1.)));
        }
        if (SubmergedWater.IsSet())
        {
            const auto Water = AC2WaterLighting(SubmergedWater.GetValue(),WaterDepth,K);
            // LScapeLighting::UpdateLighting retains the above-water lighting;
            // CalcWaterLight contributes only the fog properties to this frame.
            K.FogType=Water.FogType; K.FogMin=Water.FogMin; K.FogMax=Water.FogMax;
            K.FogSky=Water.FogSky; K.FogColor=Water.FogColor;
        }
        return K;
    }
    float TimeScale = 1;

  private:
    struct FInstance
    {
        AActor *Actor = nullptr;
        bool Rendered = false;
        FVector CelestialCenter=FVector::ZeroVector;
    };
    TMap<uint64, FInstance> Instances;
    FAC2SkyDay Day;
    FAC2RegionFog RegionFog;
    FVector CurrentViewer=FVector::ZeroVector;
    TArray<FAC2SkyDay> Days;
    TArray<float> Weights;
    uint32 DayNumber = 0;
    FAC2GameTime Calendar;
    double CurrentTime = 0;
    AActor* SkyFogLayer = nullptr;
    UMaterialInstanceDynamic* SkyFogMaterial = nullptr;
    TUniquePtr<FAC2AssetLibrary> Assets;
    UWorld *World = nullptr;
    ADirectionalLight *Sun = nullptr;
    ASkyLight *Ambient = nullptr;
    float DayFraction = 0.5f;
    TOptional<FVector> CelestialDirection;
    TOptional<FAC2BlockWater> SubmergedWater;
    float WaterDepth=0;
    TOptional<FAC2SkyKey> InteriorFogOverride;
};
