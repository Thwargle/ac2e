#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

// The sound renderer is a standalone executable. The client only exchanges
// playback commands and PCM over two private pipes.
class FAC2AudioProcess
{
public:
    struct FSource { uint64 Id; float X, Y, Z; };
    struct FAmbient { uint32 Type; float X, Y, Z; uint32 Outside; };
    ~FAC2AudioProcess();
    bool Open(const FString& Directory, const FString& Bank);
    bool Ready() const { return bReady; }
    uint32 Play(uint32 Sound, uint64 Emitter, float Gain, uint32 Channel);
    bool Finished(uint32 Context);
    void Tick(double Time, const TArray<FSource>& Sources, const float* Axes);
    void Environment(const TArray<FAmbient>& Points, bool Outside);
    void Stop(uint32 Id, float Fade) { Begin(2); Put(Id); Put(Fade); Send(); }
    void Volume(uint32 Channel, float Gain) { Begin(6); Put(Channel); Put(Gain); Send(); }
    void ContextVolume(uint32 Context, float Gain) { Begin(7); Put(Context); Put(Gain); Send(); }
    void Groove(int32 Level) { Begin(8); Put(Level); Send(); }
    uint32 Render(int16* Output, uint32 Frames);
private:
    void Begin(uint8 Op) { Command.Reset(); Command.Add(Op); }
    template<class T> void Put(T Value) { Command.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value)); }
    bool Send();
    bool Receive(void* Out, int32 Size, double Timeout = 2.0);
    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    FProcHandle Process;
    bool bReady = false;
    TArray<uint8> Command, Received;
};
