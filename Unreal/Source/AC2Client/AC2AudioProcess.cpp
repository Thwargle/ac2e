#include "AC2AudioProcess.h"
#include "Misc/Paths.h"

FAC2AudioProcess::~FAC2AudioProcess()
{
    FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
    if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
}
bool FAC2AudioProcess::Open(const FString& Directory, const FString& Bank)
{
    FString Exe = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("ac2-audio-player.exe"));
    if (!FPaths::FileExists(Exe))
        Exe = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Audio/Bridge/target/release/ac2-audio-player.exe"));
    void* ChildInput = nullptr;
    void* ChildOutput = nullptr;
    if (FPaths::FileExists(Exe) && FPlatformProcess::CreatePipe(ReadPipe, ChildOutput) &&
        FPlatformProcess::CreatePipe(ChildInput, WritePipe, true))
    {
        FString Dir = Directory;
        FPaths::NormalizeDirectoryName(Dir);
        const FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *Dir, *Bank);
        Process = FPlatformProcess::CreateProc(*Exe, *Args, false, true, true, nullptr, 0, nullptr, ChildOutput, ChildInput);
    }
    FPlatformProcess::ClosePipe(ChildInput, ChildOutput);
    uint32 Version = 0;
    bReady = Process.IsValid() && Receive(&Version, sizeof(Version), 30.0) && Version == 1;
    return bReady;
}
bool FAC2AudioProcess::Send()
{
    int32 Written = 0;
    if (bReady && (!FPlatformProcess::WritePipe(WritePipe, Command.GetData(), Command.Num(), &Written) || Written != Command.Num()))
        bReady = false;
    return bReady;
}
bool FAC2AudioProcess::Receive(void* Out, int32 Size, double Timeout)
{
    const double Deadline = FPlatformTime::Seconds() + Timeout;
    while (Received.Num() < Size)
    {
        TArray<uint8> Chunk;
        if (FPlatformProcess::ReadPipeToArray(ReadPipe, Chunk) && Chunk.Num())
            Received.Append(Chunk);
        else if (!FPlatformProcess::IsProcRunning(Process) || FPlatformTime::Seconds() >= Deadline)
        {
            bReady = false;
            return false;
        }
        else FPlatformProcess::SleepNoStats(0.0f);
    }
    FMemory::Memcpy(Out, Received.GetData(), Size);
    Received.RemoveAt(0, Size, EAllowShrinking::No);
    return true;
}
uint32 FAC2AudioProcess::Play(uint32 Sound, uint64 Emitter, float Gain, uint32 Channel)
{
    Begin(1); Put(Sound); Put(Emitter); Put(Gain); Put(Channel);
    uint32 Id = 0;
    return Send() && Receive(&Id, sizeof(Id)) ? Id : 0;
}
bool FAC2AudioProcess::Finished(uint32 Context)
{
    Begin(3); Put(Context);
    uint32 Done = 1;
    return !Send() || !Receive(&Done, sizeof(Done)) || Done != 0;
}
void FAC2AudioProcess::Tick(double Time, const TArray<FSource>& Sources, const float* Axes)
{
    Begin(4); Put(Time);
    for (int32 Axis = 0; Axis < 9; ++Axis) Put(Axes[Axis]);
    const uint32 Count = FMath::Min(Sources.Num(), 65536);
    Put(Count);
    for (uint32 I = 0; I < Count; ++I)
    {
        const auto& S = Sources[I]; Put(S.Id); Put(S.X); Put(S.Y); Put(S.Z);
    }
    Send();
}
void FAC2AudioProcess::Environment(const TArray<FAmbient>& Points, bool Outside)
{
    Begin(5); Put(uint32(Outside));
    const uint32 Count = FMath::Min(Points.Num(), 65536);
    Put(Count);
    for (uint32 I = 0; I < Count; ++I)
    {
        const auto& P = Points[I]; Put(P.Type); Put(P.X); Put(P.Y); Put(P.Z); Put(P.Outside);
    }
    Send();
}
uint32 FAC2AudioProcess::Render(int16* Output, uint32 Frames)
{
    Begin(9); Put(Frames);
    uint32 Count = 0;
    if (!Send() || !Receive(&Count, sizeof(Count))) return 0;
    if (Count > Frames) { bReady = false; return 0; }
    return Count && Receive(Output, Count * 4) ? Count : 0;
}
