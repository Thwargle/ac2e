#include "AC2Audio.h"
#include "AC2AudioProcess.h"
#include "AC2Performance.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"

namespace { TMap<TWeakObjectPtr<UWorld>,FAC2Audio*> Players; }
struct FAC2Audio::FImpl
{
    FAC2AudioProcess Mixer;
    TWeakObjectPtr<UWorld> World;
    TMap<uint64,TWeakObjectPtr<USceneComponent>> Emitters;
    struct FScenery { FAC2ScenerySound Source; uint32 Context=0; };
    TMap<uint64,FScenery> Scenery;
    uint32 MusicContext=0;
    TStrongObjectPtr<USoundWaveProcedural> Output;
    TStrongObjectPtr<UAudioComponent> Speaker;

};
FAC2Audio::FAC2Audio():Impl(MakeUnique<FImpl>()){}
FAC2Audio::~FAC2Audio()
{
    if(Players.FindRef(Impl->World)==this)Players.Remove(Impl->World);
    if(Impl->Speaker.IsValid()){Impl->Speaker->Stop();Impl->Speaker->DestroyComponent();}
    Impl->Speaker.Reset();Impl->Output.Reset();

}
FAC2Audio* FAC2Audio::Get(UWorld* World) { return Players.FindRef(World); }
bool FAC2Audio::Open(UWorld* World,const FString& Directory)
{
    if(!World || FParse::Param(FCommandLine::Get(),TEXT("nosound")))return false;
    FString GM=FPlatformMisc::GetEnvironmentVariable(TEXT("AC2_GM_DLS"));
    if(GM.IsEmpty())GM=FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"))/TEXT("System32/drivers/gm.dls");
    if(!Impl->Mixer.Open(Directory, GM)){UE_LOG(LogTemp,Warning,TEXT("AC2 audio player could not start or read the DATs"));return false;}
    Impl->Output.Reset(NewObject<USoundWaveProcedural>());
    Impl->Output->SetSampleRate(44100);Impl->Output->NumChannels=2;
    Impl->Output->Duration=INDEFINITELY_LOOPING_DURATION;
    Impl->Speaker.Reset(NewObject<UAudioComponent>(World));
    // Native mixer already applies positional attenuation and all four volume
    // preferences. Unreal only delivers PCM to its current audio device.
    Impl->Speaker->bAutoActivate=false;Impl->Speaker->bAutoDestroy=false;
    Impl->Speaker->bIsUISound=true;Impl->Speaker->bAllowSpatialization=false;
    Impl->Speaker->SetSound(Impl->Output.Get());Impl->Speaker->RegisterComponentWithWorld(World);
    Impl->World=World;Players.Add(World,this);
    // Apply saved volumes before the login screen can start its music.
    const TCHAR* Channels[]={TEXT("Sound volume"),TEXT("Music volume"),TEXT("Player music volume"),TEXT("Ambient volume")};
    for(uint32 I=0;I<UE_ARRAY_COUNT(Channels);++I){float Value=1;
        if(GConfig)GConfig->GetFloat(TEXT("AC2.Options"),Channels[I],Value,GGameUserSettingsIni);
        SetVolume(I,Value);
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 native DAT audio ready (SoundInfo, DBWave, DirectMusic and DLS) through Unreal output; GM bank=%s"),*GM);
    return true;
}
uint32 FAC2Audio::Play(uint32 Sound,USceneComponent* Emitter,float Volume,uint32 Channel)
{
    if(!Impl->Mixer.Ready() || (Sound>>24!=0x2a && Sound>>24!=0x2c))return 0;
    uint64 Id=0;
    if(Emitter){Id=Emitter->GetUniqueID();Impl->Emitters.Add(Id,Emitter);}
    const uint32 Context=Impl->Mixer.Play(Sound,Id,Volume,Channel);
    if(Context){++Plays;UE_LOG(LogTemp,Verbose,TEXT("AC2 sound=%08X emitter=%llu context=%u"),Sound,Id,Context);}
    return Context;
}
void FAC2Audio::Stop(uint32 Context,float Fade){if(Impl->Mixer.Ready()&&Context)Impl->Mixer.Stop(Context,Fade);}
bool FAC2Audio::Finished(uint32 Context) const {return !Impl->Mixer.Ready() || Impl->Mixer.Finished(Context)!=0;}
void FAC2Audio::Music(uint32 Sound)
{
    if(Sound==MusicId)return;
    Stop(Impl->MusicContext,2);MusicId=Sound;Impl->MusicContext=Sound?Play(Sound):0;
}
void FAC2Audio::SetVolume(uint32 Channel,float Volume){if(Impl->Mixer.Ready())Impl->Mixer.Volume(Channel,Volume);}
void FAC2Audio::SetSoundVolume(uint32 Context,float Volume){if(Impl->Mixer.Ready() && Context)Impl->Mixer.ContextVolume(Context,Volume);}
void FAC2Audio::SetGroove(int32 Level){if(Impl->Mixer.Ready())Impl->Mixer.Groove(Level);}
void FAC2Audio::Tick(double Time)
{
    AC2Performance::FScope Measurement(TEXT("audio_tick"));
    if(!Impl->Mixer.Ready() || !Impl->World.IsValid())return;
    FVector Position;FRotator Rotation;
    auto* PC=Impl->World->GetFirstPlayerController();if(!PC)return;
    PC->GetPlayerViewPoint(Position,Rotation);
    FTransform View(Rotation,Position);TArray<FAC2AudioProcess::FSource> Sources;
    for(auto It=Impl->Emitters.CreateIterator();It;++It){
        auto* C=It.Value().Get();if(!C){It.RemoveCurrent();continue;}
        const FVector Local=View.InverseTransformPosition(C->GetComponentLocation())*.01;
        // DirectSound listener axes: right, up, forward; native distances are metres.
        Sources.Add({It.Key(),float(Local.Y),float(Local.Z),float(Local.X)});
    }
    for(const auto& Pair:Impl->Scenery)
    {
        if(!Pair.Value.Context || Finished(Pair.Value.Context))continue;
        const FVector Local=View.InverseTransformPosition(Pair.Value.Source.Position)*.01;
        Sources.Add({Pair.Key,float(Local.Y),float(Local.Z),float(Local.X)});
    }
    const FVector Right=View.GetUnitAxis(EAxis::Y),Up=View.GetUnitAxis(EAxis::Z),Forward=View.GetUnitAxis(EAxis::X);
    const float Axes[]={float(Right.X),float(Right.Y),float(Right.Z),float(Up.X),float(Up.Y),float(Up.Z),float(Forward.X),float(Forward.Y),float(Forward.Z)};
    Impl->Mixer.Tick(Time,Sources,Axes);
    if(Impl->Output.IsValid() && Impl->Speaker.IsValid())
    {
        // About 100 ms absorbs normal frame jitter without growing latency.
        constexpr int32 Frames=4410,BytesPerFrame=2*sizeof(int16);
        const int32 Missing=FMath::Clamp(Frames-Impl->Output->GetAvailableAudioByteCount()/BytesPerFrame,0,Frames);
        if(Missing){TArray<int16> PCM;PCM.SetNumUninitialized(Missing*2);
            const uint32 Rendered=Impl->Mixer.Render(PCM.GetData(),Missing);
            if(Rendered)Impl->Output->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()),Rendered*BytesPerFrame);}
        if(!Impl->Speaker->IsPlaying())Impl->Speaker->Play();
    }
}
void FAC2Audio::Environment(const TArray<FAC2AmbientPoint>& Points,const FVector& Player,bool Outside)
{
    if(!Impl->Mixer.Ready())return;
    TArray<FAC2AudioProcess::FAmbient> Sources;
    for(const auto& P:Points){const FVector O=(P.Position-Player)*.01;Sources.Add({P.Type,float(O.X),float(O.Y),float(O.Z),P.Outside?1u:0u});}
    Impl->Mixer.Environment(Sources,Outside);
}
void FAC2Audio::Scenery(const TArray<FAC2ScenerySound>& Sources,bool DayTime)
{
    if(!Impl->Mixer.Ready())return;
    TSet<uint64> Present;
    for(const auto& S:Sources)
    {
        if(!S.Key || !(DayTime?S.DayTime:S.NightTime) || S.Position.ContainsNaN() ||
            !FMath::IsFinite(S.Volume) || S.Volume<=0 || (S.Sound>>24!=0x2a && S.Sound>>24!=0x2c))continue;
        Present.Add(S.Key);
        auto* Existing=Impl->Scenery.Find(S.Key);
        if(Existing && Existing->Source.Sound==S.Sound && Existing->Source.Channel==S.Channel)
        {
            if(Existing->Source.Volume!=S.Volume)SetSoundVolume(Existing->Context,S.Volume);
            Existing->Source=S;continue;
        }
        if(Existing)Stop(Existing->Context);
        // Keep completed and failed one-shots until the source leaves, too.
        // Otherwise every streaming update would replay the startup event.
        const uint32 Context=Impl->Mixer.Play(S.Sound,S.Key,S.Volume,S.Channel);
        if(Context)++Plays;
        Impl->Scenery.Add(S.Key,{S,Context});
    }
    for(auto It=Impl->Scenery.CreateIterator();It;++It)
        if(!Present.Contains(It.Key())){Stop(It.Value().Context);It.RemoveCurrent();}
}
