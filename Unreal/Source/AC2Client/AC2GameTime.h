#pragma once
#include "CoreMinimal.h"

// Retail GameTime::StreamPack / CalcDayBegin / UseTime (Legions 005D52B0,
// 005D5420, 005D5980). Keep seconds in double precision; only the final day
// fraction is float in the original client.
struct FAC2GameTime
{
    double ZeroTime = 0;
    int32 ZeroYear = 0;
    float DayLength = 0;
    uint32 DaysPerYear = 0;
    struct FDate { int32 Year; uint32 Day, Seed; float Fraction; };

    bool Decode(uint32 Id, TConstArrayView<uint8> Bytes)
    {
        if (Bytes.Num() < 24) return false;
        uint32 StoredId;
        FMemory::Memcpy(&StoredId, Bytes.GetData(), 4);
        FMemory::Memcpy(&ZeroTime, Bytes.GetData() + 4, 8);
        FMemory::Memcpy(&ZeroYear, Bytes.GetData() + 12, 4);
        FMemory::Memcpy(&DayLength, Bytes.GetData() + 16, 4);
        FMemory::Memcpy(&DaysPerYear, Bytes.GetData() + 20, 4);
        return StoredId == Id && FMath::IsFinite(ZeroTime) && FMath::IsFinite(DayLength) &&
            DayLength > 0 && DaysPerYear > 0;
    }
    FDate At(double Seconds) const
    {
        const double Total = Seconds + ZeroTime;
        const double YearLength = double(DayLength) * DaysPerYear;
        const double Years = FMath::FloorToDouble(Total / YearLength);
        const double Offset = Total - Years * YearLength;
        const uint32 Day = uint32(FMath::FloorToDouble(Offset / DayLength));
        const int32 Year = ZeroYear + int32(Years);
        return {Year, Day, uint32(Year) * DaysPerYear + Day,
            float((Offset - double(Day) * DayLength) / DayLength)};
    }
    double AtHour(double Seconds, float Hour) const
    {
        const double Begin = FMath::FloorToDouble((Seconds + ZeroTime) / DayLength) * DayLength - ZeroTime;
        return Begin + FMath::Fmod(FMath::Fmod(double(Hour), 24.) + 24., 24.) / 24. * DayLength;
    }
    // CSkyDesc::GetCurrentDay (0050DD80) uses the HIGH 16 bits of this hash,
    // strict '<', and the last day as fallback. Do not use a per-client day counter.
    static int32 Weather(uint32 Seed, TConstArrayView<float> Weights)
    {
        if (Weights.IsEmpty()) return INDEX_NONE;
        const uint32 Hash = (Seed * 0x3fbb657fu - 0x6b3c5f11u) * Seed + 0x0719baf0u;
        float Total = 0, Accumulated = 0;
        for (float Weight : Weights) Total += Weight;
        const float Pick = float(Hash >> 16) * Total * (1.f / 65536.f);
        for (int32 I = 0; I < Weights.Num() - 1; ++I)
        {
            Accumulated += Weights[I];
            if (Pick < Accumulated) return I;
        }
        return Weights.Num() - 1;
    }
};

// Timer::SetTime semantics: adopt forward corrections, never rewind a session.
// Extrapolate from monotonic real time so low FPS, pauses, and world loading do
// not slow the server's sky clock.
struct FAC2ServerClock
{
    bool Valid = false;
    double Base = 0, Received = 0;
    double At(double Now) const { return Base + FMath::Max(0., Now - Received); }
    void Adopt(double Seconds, double Now)
    {
        if (!FMath::IsFinite(Seconds) || Seconds < 0 || Seconds > 1.e12) return;
        Base = Valid ? FMath::Max(Seconds, At(Now)) : Seconds;
        Received = Now;
        Valid = true;
    }
};
