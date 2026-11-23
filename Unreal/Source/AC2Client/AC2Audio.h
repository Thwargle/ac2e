#pragma once
#include "CoreMinimal.h"
#include "AC2Scenery.h"
#include "AC2ScenerySound.h"
class UWorld;
class USceneComponent;

// Game-thread facade. The pinned native audio engine loads DATs and mixes on worker threads.
class FAC2Audio
{
public:
    FAC2Audio();
    ~FAC2Audio();
    bool Open(UWorld* World,const FString& Directory);
    static FAC2Audio* Get(UWorld* World);
    uint32 Play(uint32 Sound,USceneComponent* Emitter=nullptr,float Volume=1,uint32 Channel=0);
    void Stop(uint32 Context,float Fade=0);
    bool Finished(uint32 Context) const;
    void Music(uint32 Sound);
    void Tick(double Time);
    void SetVolume(uint32 Channel,float Volume);
    void SetSoundVolume(uint32 Context,float Volume);
    void SetGroove(int32 Level);
    void Environment(const TArray<FAC2AmbientPoint>& Points,const FVector& Player,bool Outside);
    void Scenery(const TArray<FAC2ScenerySound>& Sources,bool DayTime);
    uint32 Plays=0, MusicId=0;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
