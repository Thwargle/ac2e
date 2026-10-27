#include "AC2MotionData.h"
namespace
{
int32 HashCount(FAC2AssetReader& R,int32 Size)
{
    uint32 N=R.Word()&65535;
    if(uint64(N)*Size>uint64(R.Left())) {R.Good=false;return 0;}
    return int32(N);
}
FAC2MotionValues ReadValues(FAC2AssetReader& R)
{
    FAC2MotionValues V;
    V.Lift=R.Float();V.Drag=R.Float();V.Terminal=R.Float();V.Gravity=R.Word()!=0;
    V.Sink=R.Float();V.Hover=R.Float();V.Jump=R.Float();V.Forward=R.Float();V.Backward=R.Float();V.Strafe=R.Float();
    V.StillTurn=R.Float();V.RunTurn=R.Float();V.Vertical=R.Float();V.ForwardBrake=R.Float();V.BackwardBrake=R.Float();V.StrafeBrake=R.Float();
    V.TurnBrake=R.Float();V.ForwardAccel=R.Float();V.BackwardAccel=R.Float();V.StrafeAccel=R.Float();V.TurnAccel=R.Float();V.VerticalAccel=R.Float();
    return V;
}
TMap<uint32,FAC2MotionValues> ReadHash(FAC2AssetReader& R)
{
    TMap<uint32,FAC2MotionValues> Out;
    int32 N=HashCount(R,92);
    for(int32 I=0;I<N&&R.Good;++I) {uint32 Key=R.Word();Out.Add(Key,ReadValues(R));}
    return Out;
}
}
bool FAC2MotionData::Decode(uint32 Id,TConstArrayView<uint8> Bytes)
{
    *this={};FAC2AssetReader R(Bytes);if(R.Word()!=Id)return false;
    int32 N=HashCount(R,12);
    for(int32 I=0;I<N&&R.Good;++I) {uint32 E=R.Word(),M=R.Word(),Index=R.Word();States.Add((uint64(E)<<32)|M,Index);}
    N=R.Count(88,4096);
    for(int32 I=0;I<N&&R.Good;++I)
    {
        FList L;int32 K=HashCount(R,96);
        for(int32 J=0;J<K&&R.Good;++J){uint32 T=R.Word();FTerrain V;V.Modifiers=ReadHash(R);V.Default=ReadValues(R);L.Terrain.Add(T,MoveTemp(V));}
        L.Global=ReadHash(R);L.Default=ReadValues(R);Lists.Add(MoveTemp(L));
    }
    SeaFloor=R.Float();Water=R.Float();
    for(const auto& P:States) if(!Lists.IsValidIndex(P.Value))return false;
    return R.End()&&Water>0&&Water<=1&&SeaFloor>=0&&SeaFloor<Water;
}
FAC2MotionValues FAC2MotionData::Values(uint32 Element,uint32 Mode,uint32 Terrain,uint32 Global,uint32 Modifier) const
{
    const uint32* Index=States.Find((uint64(Element)<<32)|Mode);
    if(!Index)Index=States.Find((uint64(Element)<<32)|AC2Motion::Peace);
    if(!Index||!Lists.IsValidIndex(*Index))return {};
    const auto& L=Lists[*Index];
    if(const auto* V=L.Global.Find(Global))return *V;
    if(const auto* T=L.Terrain.Find(Terrain))
    {
        if(!Modifier)return T->Default;
        if(const auto* V=T->Modifiers.Find(Modifier))return *V;
        if(Mode!=AC2Motion::Peace)return Values(Element,AC2Motion::Peace,Terrain,Global,Modifier);
    }
    return L.Default;
}
bool FAC2BehaviorData::Decode(uint32 Id,TConstArrayView<uint8> Bytes)
{
    *this={};FAC2AssetReader R(Bytes);if(R.Word()!=Id)return false;
    int32 N=HashCount(R,20);
    for(int32 I=0;I<N&&R.Good;++I)
    {FModeRamp V{R.Word(),R.Word(),R.Word(),R.Word(),R.Float()};if(V.Seconds<0 || V.Seconds>10)R.Good=false;ModeRamps.Add(V);}
    N=HashCount(R,20);
    for(int32 I=0;I<N&&R.Good;++I){FCombo C;C.Element=R.Word();C.Mode=R.Word();C.Behavior=R.Word();C.Key=R.Word();C.Layer=R.Word();Combos.Add(C);}
    N=HashCount(R,28);
    for(int32 I=0;I<N&&R.Good;++I)
    {FTransition T;T.FromElement=R.Word();T.FromMode=R.Word();T.ToElement=R.Word();T.ToMode=R.Word();T.Behavior=R.Word();T.Effect=R.Word();T.StopEffect=R.Word();Transitions.Add(T);}
    N=R.Count(4,4096);for(int32 I=0;I<N&&R.Good;++I)LayerOrder.Add(R.Word());
    N=R.Count(4,4096);R.Take(int64(N)*4); // tweak layers
    N=HashCount(R,12);
    for(int32 I=0;I<N&&R.Good;++I){R.Word();R.Word();int32 K=R.Count(8,4096);R.Take(int64(K)*8);}
    N=HashCount(R,12);R.Take(int64(N)*12); // mode priorities
    N=HashCount(R,12);
    for(int32 I=0;I<N&&R.Good;++I){uint32 B=R.Word();float Ramp=R.Float();R.Word();Ramps.Add(B,FMath::Clamp(Ramp,0.f,10.f));}
    N=R.Count(4,4096);R.Take(int64(N)*4);InitialMode=R.Word();
    for(const auto& C:Combos)if(C.Layer>=uint32(LayerOrder.Num()))return false;
    return R.End()&&!Combos.IsEmpty();
}
uint32 FAC2BehaviorData::Key(uint32 Element,uint32 Mode,uint32 Behavior) const
{
    for(const auto& C:Combos)if(C.Element==Element&&C.Mode==Mode&&C.Behavior==Behavior)return C.Key;
    return 0;
}
float FAC2BehaviorData::ModeRamp(uint32 FromElement,uint32 FromMode,uint32 ToElement,uint32 ToMode) const
{
    for(const auto& R:ModeRamps)if(R.FromElement==FromElement && R.FromMode==FromMode && R.ToElement==ToElement && R.ToMode==ToMode)return R.Seconds;
    return 0.f; // BehaviorTable::GetModeTransitionRampTime, Legions 00625B10.
}
uint32 FAC2BehaviorData::Layer(uint32 Element,uint32 Mode,uint32 Behavior) const
{
    for(const auto& C:Combos)if(C.Element==Element&&C.Mode==Mode&&C.Behavior==Behavior)return C.Layer;
    return 0;
}
