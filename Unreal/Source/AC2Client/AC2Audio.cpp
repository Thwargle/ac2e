#include "AC2Audio.h"
#include "AC2Performance.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"

namespace { TMap<TWeakObjectPtr<UWorld>,FAC2Audio*> Players; }
struct FAC2Audio::FImpl
{
    struct FSource { uint64 Id; float X,Y,Z; };
    struct FAmbient { uint32 Type;float X,Y,Z;uint32 Outside; };
    void *Dll=nullptr,*Engine=nullptr;
    TWeakObjectPtr<UWorld> World;
    TMap<uint64,TWeakObjectPtr<USceneComponent>> Emitters;
    uint32 MusicContext=0;
    void* (*Create)(const char*,const char*)=nullptr;
    void (*Destroy)(void*)=nullptr;
    uint32 (*Play)(void*,uint32,uint64,float)=nullptr;
    void (*Stop)(void*,uint32,float)=nullptr;
    uint32 (*Finished)(void*,uint32)=nullptr;
    void (*Tick)(void*,double,const FSource*,uint32,const float*)=nullptr;
    void (*Environment)(void*,const FAmbient*,uint32,uint32)=nullptr;
    void (*Volume)(void*,uint32,float)=nullptr;
    void (*Groove)(void*,int32)=nullptr;
};
FAC2Audio::FAC2Audio():Impl(MakeUnique<FImpl>()){}
FAC2Audio::~FAC2Audio()
{
    if(Players.FindRef(Impl->World)==this)Players.Remove(Impl->World);
    if(Impl->Engine)Impl->Destroy(Impl->Engine);
    if(Impl->Dll)FPlatformProcess::FreeDllHandle(Impl->Dll);
}
FAC2Audio* FAC2Audio::Get(UWorld* World) { return Players.FindRef(World); }
bool FAC2Audio::Open(UWorld* World,const FString& Directory)
{
    if(!World || FParse::Param(FCommandLine::Get(),TEXT("nosound")))return false;
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Audio/Bridge/target/release/ac2_audio_bridge.dll"));
    Impl->Dll=FPlatformProcess::GetDllHandle(*Path);
    if(!Impl->Dll){UE_LOG(LogTemp,Warning,TEXT("AC2 audio DLL missing: run Build-AC2Audio.ps1"));return false;}
    auto Version=(uint32(*)())FPlatformProcess::GetDllExport(Impl->Dll,TEXT("ac2_audio_version"));
    if(!Version || Version()!=1)return false;
#define AC2_AUDIO_EXPORT(Member,Name) Impl->Member=reinterpret_cast<decltype(Impl->Member)>(FPlatformProcess::GetDllExport(Impl->Dll,TEXT(Name))); if(!Impl->Member)return false;
    AC2_AUDIO_EXPORT(Create,"ac2_audio_create") AC2_AUDIO_EXPORT(Destroy,"ac2_audio_destroy")
    AC2_AUDIO_EXPORT(Play,"ac2_audio_play") AC2_AUDIO_EXPORT(Stop,"ac2_audio_stop")
    AC2_AUDIO_EXPORT(Finished,"ac2_audio_finished")
    AC2_AUDIO_EXPORT(Tick,"ac2_audio_tick") AC2_AUDIO_EXPORT(Volume,"ac2_audio_volume")
    AC2_AUDIO_EXPORT(Environment,"ac2_audio_environment")
    AC2_AUDIO_EXPORT(Groove,"ac2_audio_groove")
#undef AC2_AUDIO_EXPORT
    FString GM=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_GM_DLS"));
    if(GM.IsEmpty())GM=FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"))/TEXT("System32/drivers/gm.dls");
    Impl->Engine=Impl->Create(TCHAR_TO_UTF8(*Directory),TCHAR_TO_UTF8(*GM));
    if(!Impl->Engine){UE_LOG(LogTemp,Warning,TEXT("AC2 audio could not open DATs or the default output device"));return false;}
    Impl->World=World;Players.Add(World,this);
    // Apply saved volumes before the login screen can start its music.
    const TCHAR* Channels[]={TEXT("Sound volume"),TEXT("Music volume"),TEXT("Player music volume"),TEXT("Ambient volume")};
    for(uint32 I=0;I<UE_ARRAY_COUNT(Channels);++I){float Value=1;
        if(GConfig)GConfig->GetFloat(TEXT("AC2.Options"),Channels[I],Value,GGameUserSettingsIni);
        SetVolume(I,Value);
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 native DAT audio ready (SoundInfo, DBWave, DirectMusic and DLS); GM bank=%s"),*GM);
    return true;
}
uint32 FAC2Audio::Play(uint32 Sound,USceneComponent* Emitter,float Volume)
{
    if(!Impl->Engine || (Sound>>24!=0x2a && Sound>>24!=0x2c))return 0;
    uint64 Id=0;
    if(Emitter){Id=Emitter->GetUniqueID();Impl->Emitters.Add(Id,Emitter);}
    const uint32 Context=Impl->Play(Impl->Engine,Sound,Id,Volume);
    if(Context){++Plays;UE_LOG(LogTemp,Verbose,TEXT("AC2 sound=%08X emitter=%llu context=%u"),Sound,Id,Context);}
    return Context;
}
void FAC2Audio::Stop(uint32 Context,float Fade){if(Impl->Engine&&Context)Impl->Stop(Impl->Engine,Context,Fade);}
bool FAC2Audio::Finished(uint32 Context) const {return !Impl->Engine || Impl->Finished(Impl->Engine,Context)!=0;}
void FAC2Audio::Music(uint32 Sound)
{
    if(Sound==MusicId)return;
    Stop(Impl->MusicContext,2);MusicId=Sound;Impl->MusicContext=Sound?Play(Sound):0;
}
void FAC2Audio::SetVolume(uint32 Channel,float Volume){if(Impl->Engine)Impl->Volume(Impl->Engine,Channel,Volume);}
void FAC2Audio::SetGroove(int32 Level){if(Impl->Engine)Impl->Groove(Impl->Engine,Level);}
void FAC2Audio::Tick(double Time)
{
    AC2Performance::FScope Measurement(TEXT("audio_tick"));
    if(!Impl->Engine || !Impl->World.IsValid())return;
    FVector Position;FRotator Rotation;
    auto* PC=Impl->World->GetFirstPlayerController();if(!PC)return;
    PC->GetPlayerViewPoint(Position,Rotation);
    FTransform View(Rotation,Position);TArray<FImpl::FSource> Sources;
    for(auto It=Impl->Emitters.CreateIterator();It;++It){
        auto* C=It.Value().Get();if(!C){It.RemoveCurrent();continue;}
        const FVector Local=View.InverseTransformPosition(C->GetComponentLocation())*.01;
        // DirectSound listener axes: right, up, forward; native distances are metres.
        Sources.Add({It.Key(),float(Local.Y),float(Local.Z),float(Local.X)});
    }
    const FVector Right=View.GetUnitAxis(EAxis::Y),Up=View.GetUnitAxis(EAxis::Z),Forward=View.GetUnitAxis(EAxis::X);
    const float Axes[]={float(Right.X),float(Right.Y),float(Right.Z),float(Up.X),float(Up.Y),float(Up.Z),float(Forward.X),float(Forward.Y),float(Forward.Z)};
    Impl->Tick(Impl->Engine,Time,Sources.GetData(),Sources.Num(),Axes);
}
void FAC2Audio::Environment(const TArray<FAC2AmbientPoint>& Points,const FVector& Player,bool Outside)
{
    if(!Impl->Engine)return;
    TArray<FImpl::FAmbient> Sources;
    for(const auto& P:Points){const FVector O=(P.Position-Player)*.01;Sources.Add({P.Type,float(O.X),float(O.Y),float(O.Z),P.Outside?1u:0u});}
    Impl->Environment(Impl->Engine,Sources.GetData(),Sources.Num(),Outside?1:0);
}
