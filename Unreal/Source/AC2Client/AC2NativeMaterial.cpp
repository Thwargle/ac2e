#include "AC2NativeMaterial.h"
#include "AC2Performance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
namespace {
struct FWorldMaterial { TWeakObjectPtr<USceneComponent> Source; bool Baked=false; double NextUpdate=0; };
TMap<TWeakObjectPtr<UMaterialInstanceDynamic>,FWorldMaterial> WorldMaterials;
}
void RegisterAC2WorldMaterial(UMaterialInstanceDynamic* M,USceneComponent* C,bool Baked)
{
    if(M && C && !WorldMaterials.Contains(M)) WorldMaterials.Add(M,{C,Baked,0});
}
void UpdateAC2WorldLighting(UWorld* W,const FAC2PreviewLighting& Outdoor,
    TFunctionRef<bool(const FVector&,FAC2PreviewLighting&)> Interior)
{
    const double Now=W->GetTimeSeconds();
    AC2Performance::FScope Measurement(TEXT("world_lighting"));
    for(auto It=WorldMaterials.CreateIterator();It;++It) {
        auto& Entry=It.Value(); auto* M=It.Key().Get();auto* Source=Entry.Source.Get();
        if(!M || !Source) {It.RemoveCurrent();continue;}
        if(Source->GetWorld()!=W || Now<Entry.NextUpdate) continue;
        Entry.NextUpdate=Now+.1;
        FAC2PreviewLighting Lighting;
        { AC2Performance::FScope Resolve(TEXT("lighting_room_lookup"));
          if(!Interior(Source->Bounds.Origin,Lighting))Lighting=Outdoor; }
        // Room light lists were baked into their vertex colours; only moving
        // models and props consume these same static lights as runtime lights.
        if(Entry.Baked)Lighting.Lights.Reset();
        AC2Performance::FScope Parameters(TEXT("lighting_parameters"));
        M->SetVectorParameterValue(TEXT("AC2UIAmbient"),Lighting.Ambient);
        for(int32 I=0;I<8;++I) {
            FVector P=FVector::ZeroVector; FLinearColor Color=FLinearColor::Black; float Range=0;
            if(Lighting.Lights.IsValidIndex(I)) {
                const auto& L=Lighting.Lights[I]; P=L.Directional?-L.Direction.GetSafeNormal():L.Position;
                Color=L.Diffuse*L.Intensity; Range=L.Directional?0:FMath::Max(.02f,L.Range);
            }
            M->SetVectorParameterValue(*FString::Printf(TEXT("AC2UILight%d"),I),FLinearColor(P.X,P.Y,P.Z,Range));
            M->SetVectorParameterValue(*FString::Printf(TEXT("AC2UIColor%d"),I),Color);
        }
    }
}
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"

FAC2NativeMaterialExpressions BuildAC2NativeStages(UMaterial *M, UMaterialExpression *Base,
    UMaterialExpression *Tint, bool Preview)
{
    auto &E=M->GetExpressionCollection();
    auto Input=[](UMaterialExpressionCustom *Node,FName Name,UMaterialExpression *Value,int32 Output=0) {
        FCustomInput I; I.InputName=Name; I.Input.Connect(Output,Value); Node->Inputs.Add(I);
    };
    auto Vector=[&](const FString &Name,FLinearColor Value) {
        auto *P=NewObject<UMaterialExpressionVectorParameter>(M);P->ParameterName=*Name;P->DefaultValue=Value;E.AddExpression(P);return P;
    };
    auto *Enabled=NewObject<UMaterialExpressionScalarParameter>(M);
    Enabled->ParameterName=TEXT("AC2NativeStages");Enabled->DefaultValue=0;E.AddExpression(Enabled);
    auto *Node=NewObject<UMaterialExpressionCustom>(M);Node->OutputType=CMOT_Float4;
    Input(Node,TEXT("Enabled"),Enabled);Input(Node,TEXT("Base"),Base);Input(Node,TEXT("BaseAlpha"),Base,4);Input(Node,TEXT("Tint"),Tint,5);
    Input(Node,TEXT("Diffuse"),Tint);
    Input(Node,TEXT("Specular"),Vector(TEXT("AC2UnlitSpecular"),FLinearColor::Black));
    auto *Vertex=NewObject<UMaterialExpressionVertexColor>(M);E.AddExpression(Vertex);
    Input(Node,TEXT("Vertex"),Vertex,0);
    Input(Node,TEXT("VertexAlpha"),Vertex,4);
    Input(Node,TEXT("Flags"),Vector(TEXT("AC2StageFlags"),FLinearColor(1,0,0,Preview?1:0)),5);
    // Fixed-function operations in gamma space. RGB and alpha read the same
    // previous CURRENT register, and each stage samples its own texture alpha.
    Node->Code=TEXT(R"(
struct NativeOps {
 float3 gamma(float3 v) { return select(v<=0.0031308,v*12.92,1.055*pow(max(v,0),1.0/2.4)-0.055); }
 float3 toLinear(float3 v) { return select(v<=0.04045,v/12.92,pow((v+0.055)/1.055,2.4)); }
 float4 arg(int a,float4 d,float4 c,float4 t,float4 f,float3 s) {
   int id=a&15; float4 v=id==0?d:id==2?t:id==3?f:id==4?float4(s,0):c;
   if(a&32)v.rgb=v.aaa; if(a&16)v=1-v; return v;
 }
 float4 op(int n,float4 a,float4 b,float4 d,float4 c,float4 t,float4 f) {
   if(n==2)return a; if(n==3)return b; if(n==4)return a*b;
   if(n==5)return 2*a*b; if(n==6)return 4*a*b; if(n==7)return a+b;
   if(n==8)return a+b-0.5; if(n==9)return 2*(a+b-0.5); if(n==10)return a-b;
   if(n==11)return a+b-a*b; if(n==12)return lerp(b,a,d.a);
   if(n==13)return lerp(b,a,t.a); if(n==14)return lerp(b,a,f.a);
   if(n==15)return a+b*(1-t.a); if(n==16)return lerp(b,a,c.a);
   if(n==18)return a+b*a.a; if(n==19)return a*b+a.a;
   if(n==20)return (1-a.a)*b+a; if(n==21)return (1-a)*b+a.a;
   if(n==24)return dot(2*a.rgb-1,2*b.rgb-1).xxxx;
   return c;
 }
}; NativeOps fn;
if(Enabled<0.5)return float4(Base.rgb*Tint.rgb,BaseAlpha);
float4 d=float4(Flags.w>0?Diffuse:(Flags.x>0?Tint.rgb:float3(1,1,1)),Flags.x>0?Tint.a:1);
if(Flags.y>0) { if(Flags.w==0)d.rgb*=Vertex.rgb; d.a*=VertexAlpha; }
float4 current=d;
)");
    UMaterialExpression *UVs[4];
    for(int32 I=0;I<4;++I) {
        auto *UV=NewObject<UMaterialExpressionTextureCoordinate>(M);UV->CoordinateIndex=I;E.AddExpression(UV);UVs[I]=UV;
    }
    for(int32 I=0;I<4;++I)
    {
        const FString Prefix=FString::Printf(TEXT("AC2Stage%d"),I), N=FString::FromInt(I);
        auto *UV=NewObject<UMaterialExpressionCustom>(M);UV->OutputType=CMOT_Float2;
        UV->Code=TEXT("float2 uv=Channel.x<0.5?UV0:Channel.x<1.5?UV1:Channel.x<2.5?UV2:UV3; return float2(dot(float3(uv,1),RowU),dot(float3(uv,1),RowV));");
        for(int32 J=0;J<4;++J)Input(UV,*FString::Printf(TEXT("UV%d"),J),UVs[J]);
        Input(UV,TEXT("Channel"),Vector(Prefix+TEXT("UV"),FLinearColor::Black));
        Input(UV,TEXT("RowU"),Vector(Prefix+TEXT("RowU"),FLinearColor(1,0,0,0)));
        Input(UV,TEXT("RowV"),Vector(Prefix+TEXT("RowV"),FLinearColor(0,1,0,0)));
        E.AddExpression(UV);
        auto *Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
        Texture->ParameterName=*(Prefix+TEXT("Texture"));
        Texture->Texture=Cast<UMaterialExpressionTextureSampleParameter2D>(Base)->Texture;
        Texture->Coordinates.Connect(0,UV);E.AddExpression(Texture);
        Input(Node,*(TEXT("T")+N),Texture);
        Input(Node,*(TEXT("TA")+N),Texture,4);
        Input(Node,*(TEXT("C")+N),Vector(Prefix+TEXT("Color"),FLinearColor(1,1,1,0)));
        Input(Node,*(TEXT("A")+N),Vector(Prefix+TEXT("Alpha"),FLinearColor(1,1,1,0)));
        Node->Code+=FString::Printf(TEXT(
            "if(C%s.x>1) { float4 t=float4(fn.gamma(T%s.rgb),TA%s); "
            "float4 a=fn.arg((int)C%s.y,d,current,t,Tint,Specular), b=fn.arg((int)C%s.z,d,current,t,Tint,Specular); "
            "float3 rgb=fn.op((int)C%s.x,a,b,d,current,t,Tint).rgb; "
            "a=fn.arg((int)A%s.y,d,current,t,Tint,Specular); b=fn.arg((int)A%s.z,d,current,t,Tint,Specular); "
            "float alpha=A%s.x<=1?current.a:fn.op((int)A%s.x,a,b,d,current,t,Tint).a; "
            "current=saturate(float4(rgb,alpha)); } else return float4(fn.toLinear(saturate(current.rgb+Specular)),current.a);\n"),
            *N,*N,*N,*N,*N,*N,*N,*N,*N,*N);
    }
    Node->Code+=TEXT("return float4(fn.toLinear(saturate(current.rgb+Specular)),current.a);");E.AddExpression(Node);
    auto *RGB=NewObject<UMaterialExpressionComponentMask>(M);RGB->R=RGB->G=RGB->B=true;RGB->A=false;RGB->Input.Connect(0,Node);E.AddExpression(RGB);
    // Opacity has no dependency on vertex lighting. In particular the depth-only
    // masked pass must not require the colour cascade's vertex interpolators.
    auto *Alpha=NewObject<UMaterialExpressionCustom>(M);Alpha->OutputType=CMOT_Float1;
    for(const auto& In:Node->Inputs)
        if(In.InputName!=TEXT("Diffuse") && In.InputName!=TEXT("Specular") && In.InputName!=TEXT("Vertex") && In.InputName!=TEXT("Base")) Alpha->Inputs.Add(In);
    Input(Alpha,TEXT("Opacity"),Vector(TEXT("AC2Opacity"),FLinearColor(1,0,0,0)));
    Alpha->Code=TEXT(R"(
struct NativeAlpha {
 float arg(int n,float d,float c,float t,float f) {
   int id=n&15; float v=id==0?d:id==2?t:id==3?f:id==4?0:c; return (n&16)?1-v:v;
 }
 float op(int n,float a,float b,float d,float c,float t,float f) {
   if(n==2)return a; if(n==3)return b; if(n==4)return a*b;
   if(n==5)return 2*a*b; if(n==6)return 4*a*b; if(n==7)return a+b;
   if(n==8)return a+b-.5; if(n==9)return 2*(a+b-.5); if(n==10)return a-b;
   if(n==11)return a+b-a*b; if(n==12)return lerp(b,a,d);
   if(n==13)return lerp(b,a,t); if(n==14)return lerp(b,a,f);
   if(n==15)return a+b*(1-t); if(n==16)return lerp(b,a,c); return c;
 }
}; NativeAlpha fn;
if(Enabled<.5)return saturate(BaseAlpha*Opacity.x+Opacity.y);
float d=Flags.x>0?Tint.a:1; if(Flags.y>0)d*=VertexAlpha;
float current=d;
)");
    for(int32 I=0;I<4;++I) {
        const FString N=FString::FromInt(I);
        Alpha->Code+=FString::Printf(TEXT("if(C%s.x<=1)return current; if(A%s.x>1) { float a=fn.arg((int)A%s.y,d,current,TA%s,Tint.a); float b=fn.arg((int)A%s.z,d,current,TA%s,Tint.a); current=saturate(fn.op((int)A%s.x,a,b,d,current,TA%s,Tint.a)); }\n"),*N,*N,*N,*N,*N,*N,*N,*N);
    }
    Alpha->Code+=TEXT("return current;"); E.AddExpression(Alpha);
    return {RGB,Alpha,Node};
}
#endif
