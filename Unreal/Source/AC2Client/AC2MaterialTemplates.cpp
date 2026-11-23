#include "AC2Assets.h"
#include "AC2NativeMaterial.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

bool PrepareAC2Materials(FString &Error)
{
#if WITH_EDITOR
    FString SavedVersion;
    const FString VersionPath=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("AC2/Materials/template-version.txt"));
    FFileHelper::LoadFileToString(SavedVersion,*VersionPath);
    SavedVersion.TrimStartAndEndInline();
    const bool CurrentVersion=SavedVersion==TEXT("46");
    const bool ReflectionOnlyUpdate=false;
    const bool PreviousVersion=false;
    const bool AddedAnisotropic=false;
    const bool WaterOnlyUpdate=false;
    const FString FoliagePath=TEXT("/Game/AC2/Materials/AC2FoliageInteraction");
    auto* Foliage=LoadObject<UMaterialParameterCollection>(nullptr,*(FoliagePath+TEXT(".AC2FoliageInteraction")));
    if(!Foliage){
        auto* Package=CreatePackage(*FoliagePath);
        Foliage=NewObject<UMaterialParameterCollection>(Package,TEXT("AC2FoliageInteraction"),RF_Public|RF_Standalone);
        for(int32 I=0;I<AC2FoliageContactLimit;++I)for(const TCHAR* Prefix:{TEXT("Position"),TEXT("Trail")}){
            FCollectionVectorParameter P;P.ParameterName=*FString::Printf(TEXT("%s%d"),Prefix,I);
            P.DefaultValue=FLinearColor(0,0,0,0);Foliage->VectorParameters.Add(P);
        }
        Foliage->PostEditChange();
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,Foliage,*FPackageName::LongPackageNameToFilename(FoliagePath,FPackageName::GetAssetPackageExtension()),Args)){
            Error=TEXT("Could not save foliage interaction collection");return false;
        }
    }
    // Shared world fog parameters default to disabled in previews and test worlds.
    const FString FogPath=TEXT("/Game/AC2/Materials/AC2Fog");
    auto* FogCollection=LoadObject<UMaterialParameterCollection>(nullptr,*(FogPath+TEXT(".AC2Fog")));
    if(!FogCollection){
        auto* Package=CreatePackage(*FogPath);
        FogCollection=NewObject<UMaterialParameterCollection>(Package,TEXT("AC2Fog"),RF_Public|RF_Standalone);
        for(const TCHAR* Name:{TEXT("Range"),TEXT("Color"),TEXT("AlphaRange")}){
            FCollectionVectorParameter P;P.ParameterName=Name;
            P.DefaultValue=FLinearColor(0,0,0,0);FogCollection->VectorParameters.Add(P);
        }
        FogCollection->PostEditChange();
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,FogCollection,*FPackageName::LongPackageNameToFilename(FogPath,FPackageName::GetAssetPackageExtension()),Args)){
            Error=TEXT("Could not save native fog collection");return false;
        }
    }
    // SceneColor HDR stores inverse opacity. Native UI scenes draw over the
    // existing page, so preserve transparent pixels instead of a black rectangle.
    const FString CapturePath=TEXT("/Game/AC2/Materials/M_AC2Capture");
    auto* CaptureMaterial=LoadObject<UMaterial>(nullptr,*(CapturePath+TEXT(".M_AC2Capture")));
    if((!ReflectionOnlyUpdate && !PreviousVersion && !CurrentVersion && !WaterOnlyUpdate && !AddedAnisotropic) || !CaptureMaterial)
    {
        auto* Package=CreatePackage(*CapturePath);
        auto* Material=CaptureMaterial?CaptureMaterial:NewObject<UMaterial>(Package,TEXT("M_AC2Capture"),RF_Public|RF_Standalone);
        Material->GetExpressionCollection().Empty();
        // Scene color is premultiplied; additive particles have RGB with zero opacity.
        Material->MaterialDomain=MD_UI;Material->BlendMode=BLEND_AlphaComposite;
        auto* Texture=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Texture->ParameterName=TEXT("Capture");Texture->SamplerType=SAMPLERTYPE_Color;
        Texture->Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(Material);
        auto* Mirror=NewObject<UMaterialExpressionScalarParameter>(Material);
        Mirror->ParameterName=TEXT("MirrorHorizontal");Mirror->DefaultValue=0;
        auto* Coordinates=NewObject<UMaterialExpressionCustom>(Material);
        Coordinates->OutputType=CMOT_Float2;
        Coordinates->Inputs.Reset();
        Coordinates->Code=TEXT("return float2(lerp(UV.x,1-UV.x,Mirror),UV.y);");
        FCustomInput U;U.InputName=TEXT("UV");U.Input.Connect(0,UV);Coordinates->Inputs.Add(U);
        FCustomInput M;M.InputName=TEXT("Mirror");M.Input.Connect(0,Mirror);Coordinates->Inputs.Add(M);
        Texture->Coordinates.Connect(0,Coordinates);
        Material->GetExpressionCollection().AddExpression(UV);Material->GetExpressionCollection().AddExpression(Mirror);
        Material->GetExpressionCollection().AddExpression(Coordinates);
        auto* Opacity=NewObject<UMaterialExpressionOneMinus>(Material);Opacity->Input.Connect(4,Texture);
        Material->GetExpressionCollection().AddExpression(Texture);Material->GetExpressionCollection().AddExpression(Opacity);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Texture);
        Material->GetEditorOnlyData()->Opacity.Connect(0,Opacity);
        Material->PostEditChange();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
        if(!UPackage::SavePackage(Package,Material,*FPackageName::LongPackageNameToFilename(CapturePath,FPackageName::GetAssetPackageExtension()),Args))
        {Error=TEXT("Cannot save native UI scene compositor");return false;}
    }
    for (int32 Template = 0; Template < 43; ++Template)
    {
        const bool FrameBuffer=Template==32 || Template==33;
        const bool Terrain=Template==42;
        const bool Anisotropic=Template>=34 && !Terrain;
        constexpr int32 AnisotropicKinds[]={0,8,12,14};
        const int32 Kind=Terrain?0:Anisotropic?AnisotropicKinds[(Template-34)%4]:FrameBuffer?8:Template%16;
        const bool Alternate=!Terrain && (Anisotropic?Template>=38:FrameBuffer?Template==33:Template>=16);
        // Native mesh materials need both culling permutations. Sky, particles,
        // water and generated grass retain their separate rendering policies.
        if(Alternate && !(Kind==0 || Kind==1 || Kind==8 || Kind==9 || Kind>=12))continue;
        const bool Preview = Kind >= 12, Grass = Kind == 11, Masked = Kind == 1 || Kind == 13 || Grass, Sky = Kind >= 2 && Kind <= 4;
        const bool DefaultTwoSided=Masked || Sky || (Kind >= 5 && Kind != 12);
        const bool TwoSided=Alternate?!DefaultTwoSided:DefaultTwoSided;
        FString Name = Terrain ? TEXT("M_AC2Terrain") : Kind == 0    ? TEXT("M_AC2Opaque")
                             : Kind == 1  ? TEXT("M_AC2Cutout")
                             : Kind == 2  ? TEXT("M_AC2Sky")
                             : Kind == 3  ? TEXT("M_AC2SkyAlpha")
                             : Kind == 4  ? TEXT("M_AC2SkyAdd")
                             : Kind == 5  ? TEXT("M_AC2Water")
                             : Kind == 6  ? TEXT("M_AC2Far")
                             : Kind == 7  ? TEXT("M_AC2Particle")
                             : Kind == 8  ? TEXT("M_AC2SceneryAlpha")
                             : Kind == 9  ? TEXT("M_AC2SceneryAdd")
                             : Kind == 10 ? TEXT("M_AC2ParticleAdd")
                             : Kind == 11 ? TEXT("M_AC2Grass")
                             : Kind == 12 ? TEXT("M_AC2UIOpaque")
                             : Kind == 13 ? TEXT("M_AC2UICutout")
                             : Kind == 14 ? TEXT("M_AC2UIAlpha") : TEXT("M_AC2UIAdd");
        if(FrameBuffer)Name=TEXT("M_AC2FrameBuffer");
        if(Anisotropic)Name+=TEXT("Anisotropic");
        if(Alternate)Name+=TwoSided?TEXT("TwoSided"):TEXT("OneSided");
        const FString Path = TEXT("/Game/AC2/Materials/") + Name;
        auto *Existing = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Name));
        if(Existing && ReflectionOnlyUpdate && Kind!=5)continue;
        if(Existing && (Kind<2 || Kind>6) && (PreviousVersion || ((WaterOnlyUpdate || AddedAnisotropic) && !Anisotropic)))continue;
        bool Updated = false;
        if (Existing)
            for (const auto &E : Existing->GetExpressions())
                if (auto *P = Cast<UMaterialExpressionVectorParameter>(E))
                    Updated |= P->ParameterName == (Preview ? TEXT("AC2UIFlags") :
                               ((Kind == 1 || Kind == 8 || Kind == 9) ? TEXT("AC2Opacity") : TEXT("AC2UVRowU")));
        const bool NativeNormals = Existing && !Existing->bTangentSpaceNormal &&
            Cast<UMaterialExpressionVertexNormalWS>(Existing->GetEditorOnlyData()->Normal.Expression) != nullptr;
        bool NativeDetail = false;
        if (Existing) for (const auto &E : Existing->GetExpressions())
            if (auto *P = Cast<UMaterialExpressionScalarParameter>(E)) NativeDetail |= P->ParameterName == TEXT("AC2NativeStages");
        const bool NativeSpecular = Existing && Existing->GetEditorOnlyData()->Specular.Expression != nullptr;
        if (CurrentVersion && Updated && NativeSpecular && NativeDetail && (!TwoSided || NativeNormals) && Existing->TwoSided==TwoSided)
            continue;
        UPackage *Package = CreatePackage(*Path);
        auto *Material = Existing ? Existing : NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
        Material->GetExpressionCollection().Empty();
        Material->bIsSky = Kind == 2;
        Material->BlendMode = Kind == 3 || Kind == 5 || Kind == 7 || Kind == 8 || Kind == 14 ? BLEND_Translucent
                              : (Kind == 4 || Kind == 9 || Kind == 10 || Kind == 15) ? BLEND_Additive
                              : Masked                                         ? BLEND_Masked
                                                                               : BLEND_Opaque;
        Material->TwoSided = TwoSided;
        // Native linear fog is applied explicitly, with separate handling for
        // transparent surfaces and the already-composited water background.
        Material->bComputeFogPerPixel = false;
        Material->bUseTranslucencyVertexFog = false;
        Material->SetShadingModel(Sky || Kind == 5 || Kind == 7 || Kind == 10 || Preview || FrameBuffer ? MSM_Unlit : MSM_DefaultLit);
        Material->OpacityMaskClipValue = 0.4f;
        auto *Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Texture->ParameterName = TEXT("Albedo");
        Texture->Texture =
            LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        if (!Texture->Texture)
        {
            Error = TEXT("Missing engine default texture");
            return false;
        }
        auto *Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
        Tint->ParameterName = TEXT("Tint");
        Tint->DefaultValue = FLinearColor::White;
        auto *Multiply = NewObject<UMaterialExpressionMultiply>(Material);
        Multiply->A.Connect(0, Texture);
        Multiply->B.Connect(0, Tint);
        auto *Roughness = NewObject<UMaterialExpressionConstant>(Material);
        Roughness->R = 0.85f;
        auto &Expressions = Material->GetExpressionCollection();
        Expressions.AddExpression(Texture);
        Expressions.AddExpression(Tint);
        Expressions.AddExpression(Multiply);
        Expressions.AddExpression(Roughness);
        auto FogInput=[](UMaterialExpressionCustom* C,const TCHAR* Name,UMaterialExpression* E,int32 Output=0){
            FCustomInput I;I.InputName=Name;I.Input.Connect(Output,E);C->Inputs.Add(I);
        };
        auto* FogMode=NewObject<UMaterialExpressionVectorParameter>(Material);
        FogMode->ParameterName=TEXT("AC2FogMode");FogMode->DefaultValue=FLinearColor(1,0,0,0);
        Expressions.AddExpression(FogMode);
        auto BindFog=[&](UMaterialExpressionCustom* Node){
            Node->IncludeFilePaths.Add(TEXT("/AC2/Private/AC2Fog.ush"));
            FogInput(Node,TEXT("FogMode"),FogMode);
            for(const TCHAR* Name:{TEXT("Range"),TEXT("Color"),TEXT("AlphaRange")}){
                auto* P=NewObject<UMaterialExpressionCollectionParameter>(Material);
                P->Collection=FogCollection;P->ParameterName=Name;P->ParameterId=FogCollection->GetParameterId(Name);
                Expressions.AddExpression(P);FogInput(Node,FCString::Strcmp(Name,TEXT("Range"))==0?TEXT("FogRange"):FCString::Strcmp(Name,TEXT("Color"))==0?TEXT("FogColor"):TEXT("FogAlphaRange"),P);
            }
            auto* World=NewObject<UMaterialExpressionWorldPosition>(Material);
            auto* Camera=NewObject<UMaterialExpressionCameraPositionWS>(Material);
            Expressions.AddExpression(World);Expressions.AddExpression(Camera);
            FogInput(Node,TEXT("FogPosition"),World);FogInput(Node,TEXT("FogCamera"),Camera);
        };
        auto FogColor=[&](UMaterialExpression* Source,int32 Output,bool Additive){
            auto* Node=NewObject<UMaterialExpressionCustom>(Material);Node->OutputType=CMOT_Float3;
            FogInput(Node,TEXT("Source"),Source,Output);BindFog(Node);
            Node->Code=FString::Printf(TEXT(
                "float4 range=FogRange; range.z*=((ResolvedView.PostVolumeUserFlags & AC2_NATIVE_FOG_USER_FLAG)!=0)*(FogMode.x>.5 && FogMode.z<.5); "
                "return AC2FogApply(Source,length(FogPosition-FogCamera),range,FogColor,%s);"),Additive?TEXT("true"):TEXT("false"));
            Expressions.AddExpression(Node);return Node;
        };
        // Native highlights use COLOR1 below. UE's separate dielectric response
        // would otherwise add a pale reflection over the authored finish.
        auto *Specular = NewObject<UMaterialExpressionScalarParameter>(Material);
        Specular->ParameterName = TEXT("AC2Specular");
        Specular->DefaultValue = 0.f;
        Expressions.AddExpression(Specular);
        Material->GetEditorOnlyData()->Specular.Connect(0, Specular);
        auto *Coord = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        auto *RowU = NewObject<UMaterialExpressionVectorParameter>(Material);
        RowU->ParameterName = TEXT("AC2UVRowU");
        RowU->DefaultValue = FLinearColor(1, 0, 0, 0);
        auto *RowV = NewObject<UMaterialExpressionVectorParameter>(Material);
        RowV->ParameterName = TEXT("AC2UVRowV");
        RowV->DefaultValue = FLinearColor(0, 1, 0, 0);
        auto *UV = NewObject<UMaterialExpressionCustom>(Material);
        UV->OutputType = CMOT_Float2;
        UV->Code = TEXT("return float2(dot(float3(TexCoord,1),RowU),dot(float3(TexCoord,1),RowV));");
        UV->Inputs.SetNum(3);
        UV->Inputs[0].InputName = TEXT("TexCoord");
        UV->Inputs[0].Input.Connect(0, Coord);
        UV->Inputs[1].InputName = TEXT("RowU");
        UV->Inputs[1].Input.Connect(0, RowU);
        UV->Inputs[2].InputName = TEXT("RowV");
        UV->Inputs[2].Input.Connect(0, RowV);
        Texture->Coordinates.Connect(0, UV);
        Expressions.AddExpression(Coord);
        Expressions.AddExpression(RowU);
        Expressions.AddExpression(RowV);
        Expressions.AddExpression(UV);
        auto Native=BuildAC2NativeStages(Material,Texture,Tint,Preview,!Preview && (Kind==0 || Kind==1 || Kind==5 || Kind==6 || Grass),Kind==5,Anisotropic);
        Multiply->A.Connect(0,Native.Color);
        Multiply->B.Expression=nullptr; Multiply->ConstB=1;
        if (TwoSided)
        {
            // Retail lights both sides of a leaf/grass card using the authored
            // vertex normal. In UE 5.8 the two-sided flip applies to tangent-space
            // normals only. A world-space vertex normal preserves retail lighting
            // on the back of the card without depending on the viewing direction.
            auto *Normal = NewObject<UMaterialExpressionVertexNormalWS>(Material);
            Expressions.AddExpression(Normal);
            Material->bTangentSpaceNormal = false;
            Material->GetEditorOnlyData()->Normal.Connect(0, Normal);
        }
        if (Kind == 1 || Kind == 8 || Kind == 9 || Grass || Preview)
        {
            auto *Alpha = NewObject<UMaterialExpressionCustom>(Material);
            Alpha->OutputType = CMOT_Float1;
            Alpha->Code = TEXT("return saturate(TexAlpha);");
            Alpha->Inputs.SetNum(1);
            Alpha->Inputs[0].InputName = TEXT("TexAlpha");
            Alpha->Inputs[0].Input.Connect(0, Native.Alpha);
            Expressions.AddExpression(Alpha);
            if (Grass)
            {
                auto *Position = NewObject<UMaterialExpressionWorldPosition>(Material);
                auto *Camera = NewObject<UMaterialExpressionCameraPositionWS>(Material);
                Alpha->Code = TEXT("float enabled=((ResolvedView.PostVolumeUserFlags & AC2_NATIVE_FOG_USER_FLAG)!=0); "
                                   "float4 ar=FogAlphaRange; ar.z*=enabled*FogMode.y; float4 r=FogRange;r.z*=enabled*FogMode.z; "
                                   "float d=length(FogPosition-FogCamera); "
                                   "float a=saturate(TexAlpha)*(1-AC2FogAmount(d,ar))*(1-AC2FogAmount(d,r)); "
                                   // Native frill visibility measures planar distance. A raised
                                   // camera must not change its geometric culling radius. Authored
                                   // alpha fog is applied before the coverage dither, separately.
                                   "float fade=step(distance(World.xy,Camera.xy),16000.0); "
                                   // Integer hashing avoids sin() precision loss at million-cm world positions.
                                   "uint3 p=uint3(int3(floor(World*0.5))); "
                                   "uint h=p.x*73856093u ^ p.y*19349663u ^ p.z*83492791u; "
                                   "h^=h>>16; h*=2246822519u; h^=h>>13; "
                                   "float noise=float(h&65535u)/65536.0; "
                                   // Preserve fractional DAT alpha as coverage, including
                                   // nearby frills. A hard 0.4 cutout made every surviving
                                   // texel opaque and exposed the crossed-card grid.
                                   // Coverage still writes depth, so batched cards do not
                                   // acquire translucent centre-sorting artifacts.
                                   "return step(noise+0.00001,a*fade);");
                Alpha->Inputs.SetNum(3);
                Alpha->Inputs[1].InputName = TEXT("World");
                Alpha->Inputs[1].Input.Connect(0, Position);
                Alpha->Inputs[2].InputName = TEXT("Camera");
                Alpha->Inputs[2].Input.Connect(0, Camera);
                BindFog(Alpha);
                Expressions.AddExpression(Position);
                Expressions.AddExpression(Camera);
                auto* RootWeight=NewObject<UMaterialExpressionTextureCoordinate>(Material);RootWeight->CoordinateIndex=3;
                auto* Enabled=NewObject<UMaterialExpressionScalarParameter>(Material);
                Enabled->ParameterName=TEXT("AC2FoliageBend");Enabled->DefaultValue=0;
                auto* Bend=NewObject<UMaterialExpressionCustom>(Material);Bend->OutputType=CMOT_Float3;
                // Cosmetic response, independent of DAT placement/collision.
                // Select the strongest contact, rather than summing bodies into
                // an unbounded displacement. UV3 contains a per-plant height.
                Bend->Code=TEXT("float3 bend=0; float best=0; float w=saturate(Weight.x); w=w*w;\n");
                auto Input=[&](const TCHAR* Name,UMaterialExpression* E,int32 Output=0){
                    FCustomInput I;I.InputName=Name;I.Input.Connect(Output,E);Bend->Inputs.Add(I);};
                Input(TEXT("World"),Position);Input(TEXT("Weight"),RootWeight);Input(TEXT("Enabled"),Enabled);
                for(int32 I=0;I<AC2FoliageContactLimit;++I){
                    const FString P=FString::Printf(TEXT("Position%d"),I),T=FString::Printf(TEXT("Trail%d"),I);
                    for(const FString& ParameterName:{P,T}){
                        auto* Parameter=NewObject<UMaterialExpressionCollectionParameter>(Material);
                        Parameter->Collection=Foliage;Parameter->ParameterName=*ParameterName;Parameter->ParameterId=Foliage->GetParameterId(*ParameterName);
                        Expressions.AddExpression(Parameter);Input(*ParameterName,Parameter);
                    }
                    Bend->Code+=FString::Printf(TEXT(
                        "if(%s.w>0){ float3 travel=%s.xyz-%s.xyz; float t=saturate(dot(World.xy-%s.xy,travel.xy)/max(dot(travel.xy,travel.xy),1)); "
                        "float3 nearest=%s.xyz+travel*t; float2 d=World.xy-nearest.xy; float r=length(d); "
                        "float h=World.z-nearest.z; float vertical=smoothstep(-25,0,h)*(1-smoothstep(%s.w,%s.w+60,h)); "
                        "float a=(1-smoothstep(%s.w*.15,%s.w,r))*vertical; "
                        "float2 direction=r>1?d/r:normalize(travel.xy+float2(.01,.01)); "
                        "if(a>best){best=a; bend=float3(direction,-.2)*a;} }\n"),
                        *P,*P,*T,*T,*T,*T,*T,*P,*P);
                }
                Bend->Code+=TEXT("return bend*min(max(Weight.y,0)*.85,75)*w*Enabled;");
                Expressions.AddExpression(RootWeight);
                Expressions.AddExpression(Enabled);Expressions.AddExpression(Bend);
                Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0,Bend);
            }
            if (Masked)
                Material->GetEditorOnlyData()->OpacityMask.Connect(0, Alpha);
            else
                Material->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        }
        if (Sky)
        {
            auto *SkyTint = NewObject<UMaterialExpressionVectorParameter>(Material);
            SkyTint->ParameterName = TEXT("SkyTint");
            SkyTint->DefaultValue = FLinearColor::White;
            auto *S = NewObject<UMaterialExpressionMultiply>(Material);
            S->A.Connect(0, Native.Color);
            S->B.Connect(0, SkyTint);
            auto *AlphaTint = NewObject<UMaterialExpressionMultiply>(Material);
            AlphaTint->A.Connect(0, Native.Alpha);
            AlphaTint->B.Connect(4, SkyTint);
            for (UMaterialExpression *E :
                 {(UMaterialExpression *)SkyTint, (UMaterialExpression *)S, (UMaterialExpression *)AlphaTint})
                Expressions.AddExpression(E);
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, S);
            Material->GetEditorOnlyData()->Opacity.Connect(0, AlphaTint);
        }
        else
            Material->GetEditorOnlyData()->BaseColor.Connect(0, Multiply);
        UMaterialExpression* ShoreLighting=nullptr;
        if (Preview || Kind==0 || Kind==1 || Kind==5 || Kind==6 || Kind==8 || Kind==9 || Grass)
        {
            // Retail UIScene uses vertex diffuse lighting and gamma-space RGB,
            // not UE's physical light units, ACES tone curve or automatic exposure.
            auto* Lighting = NewObject<UMaterialExpressionCustom>(Material);
            Lighting->OutputType = CMOT_Float3;
            auto Input = [&](UMaterialExpressionCustom* Node, const TCHAR* Name, UMaterialExpression* Value) {
                FCustomInput I; I.InputName = Name; I.Input.Connect(0, Value); Node->Inputs.Add(I);
            };
            auto Param = [&](FString Name, FLinearColor Value) {
                auto* P = NewObject<UMaterialExpressionVectorParameter>(Material);
                P->ParameterName = *Name; P->DefaultValue = Value; Expressions.AddExpression(P); return P;
            };
            auto* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
            auto* Normal = NewObject<UMaterialExpressionVertexNormalWS>(Material);
            Expressions.AddExpression(Position); Expressions.AddExpression(Normal);
            auto* Vertex = NewObject<UMaterialExpressionVertexColor>(Material);
            Expressions.AddExpression(Vertex);
            Input(Lighting, TEXT("Position"), Position); Input(Lighting, TEXT("Normal"), Normal);
            Input(Lighting, TEXT("Ambient"), Param(TEXT("AC2UIAmbient"), FLinearColor(.1f, .1f, .1f)));
            Input(Lighting, TEXT("MaterialAmbient"), Param(TEXT("AC2UIMaterialAmbient"), FLinearColor::White));
            Input(Lighting, TEXT("Tint"), Tint); Input(Lighting, TEXT("Vertex"), Vertex);
            auto* Flags = Param(TEXT("AC2UIFlags"), FLinearColor(1,1,0,0));
            FCustomInput FlagInput; FlagInput.InputName = TEXT("Flags"); FlagInput.Input.Connect(5, Flags); Lighting->Inputs.Add(FlagInput);
            Input(Lighting, TEXT("Ignore"), Param(TEXT("AC2UIIgnore"), FLinearColor::Black));
            Lighting->Code = TEXT("float3 n=normalize(Normal); float3 lit=0;\n");
            for (int32 I = 0; I < 8; ++I)
            {
                const FString L = FString::Printf(TEXT("Light%d"), I), C = FString::Printf(TEXT("Color%d"), I);
                auto* P = Param(TEXT("AC2UI") + L, FLinearColor(0,0,0,0));
                // Custom inputs use RGBA; output 0 of a vector parameter is RGB.
                FCustomInput In; In.InputName = *L; In.Input.Connect(5, P); Lighting->Inputs.Add(In);
                Input(Lighting, *C, Param(TEXT("AC2UI") + C, FLinearColor::Black));
                Lighting->Code += FString::Printf(TEXT(
                    "{ float3 d=%s.w>0 ? %s.xyz-Position : %s.xyz; float d2=dot(d,d); "
                    "float a=%s.w>0 ? 1.0/(1.0+10.0*d2/(%s.w*%s.w)) : 1.0; "
                    "lit+=%s.rgb*a*max(0,dot(n,d*rsqrt(max(d2,0.000001))))*(1-(%s.w>0?Ignore.y:Ignore.x)); }\n"), *L,*L,*L,*L,*L,*L,*C,*L);
            }
            Lighting->Code += TEXT(
                "float3 diffuse=Flags.y>0?Tint.rgb:float3(1,1,1);"
                "if(Flags.x==0) return saturate(diffuse*(Flags.z>0?Vertex.rgb:float3(1,1,1)));"
                "if(Flags.z>0) return saturate(Flags.w>0?lit*Vertex.rgb:lit+Vertex.rgb);"
                "return saturate(lit*diffuse+Ambient.rgb*MaterialAmbient.rgb);");
            Expressions.AddExpression(Lighting);
            if(Kind==5){
                // Shore material 31000362 has white diffuse/ambient, independent
                // of the blue water material. It receives native vertex light.
                auto* FoamLight=NewObject<UMaterialExpressionCustom>(Material);
                FoamLight->OutputType=CMOT_Float3;FoamLight->Inputs=Lighting->Inputs;
                FoamLight->Code=Lighting->Code;
                auto* White=Param(TEXT("AC2ShoreDiffuse"),FLinearColor::White);
                for(auto& In:FoamLight->Inputs)if(In.InputName==TEXT("Tint") || In.InputName==TEXT("MaterialAmbient"))In.Input.Connect(0,White);
                Expressions.AddExpression(FoamLight);
                auto* LitShore=NewObject<UMaterialExpressionVertexInterpolator>(Material);
                LitShore->Input.Connect(0,FoamLight);Expressions.AddExpression(LitShore);ShoreLighting=LitShore;
            }
            auto* Interpolate = NewObject<UMaterialExpressionVertexInterpolator>(Material);
            Interpolate->Input.Connect(0, Lighting); Expressions.AddExpression(Interpolate);
            // RenderDeviceD3D / fragment.dll: specular is interpolated COLOR1,
            // added in native RGB after texturing. A zero power disables it.
            // The viewer is view-space (0,0,-1), not a per-pixel reflection ray.
            auto* Highlight=NewObject<UMaterialExpressionCustom>(Material);
            Highlight->OutputType=CMOT_Float3;
            Highlight->Inputs=Lighting->Inputs;
            Input(Highlight,TEXT("SpecularColor"),Param(TEXT("AC2NativeSpecularColor"),FLinearColor::White));
            auto Scalar=[&](const TCHAR* Name) {
                auto* P=NewObject<UMaterialExpressionScalarParameter>(Material);
                P->ParameterName=Name; P->DefaultValue=0; Expressions.AddExpression(P); return P;
            };
            Input(Highlight,TEXT("Power"),Scalar(TEXT("AC2NativeSpecularPower")));
            Input(Highlight,TEXT("Active"),Scalar(TEXT("AC2NativeSpecularEnabled")));
            Highlight->Code=TEXT("if(Active<0.5 || Power==0)return float3(0,0,0); "
                "float3 n=normalize(Normal), eye=-ResolvedView.ViewForward, spec=0;\n");
            for(int32 I=0;I<8;++I) {
                const FString L=FString::Printf(TEXT("Light%d"),I),C=FString::Printf(TEXT("Color%d"),I);
                Highlight->Code+=FString::Printf(TEXT(
                    "{ float3 d=%s.w>0?%s.xyz-Position:%s.xyz; float d2=dot(d,d); "
                    "float3 l=d*rsqrt(max(d2,0.000001)); float3 h=%s.w>0?eye:normalize(l+eye); "
                    "float a=%s.w>0?1.0/(1.0+10.0*d2/(%s.w*%s.w)):1.0; "
                    "float term=dot(n,l)>0 && dot(n,h)>0?pow(max(dot(n,h),0.000001),clamp(Power,-127.9961,127.9961)):0; "
                    "spec+=(%s.w>0?float3(1,1,1):%s.rgb)*term*a*a*(1-(%s.w>0?Ignore.y:Ignore.x)); }\n"),
                    *L,*L,*L,*L,*L,*L,*L,*L,*C,*L);
            }
            Highlight->Code+=TEXT("return saturate(spec*SpecularColor.rgb);");
            Expressions.AddExpression(Highlight);
            auto* HighlightVertex=NewObject<UMaterialExpressionVertexInterpolator>(Material);
            HighlightVertex->Input.Connect(0,Highlight);Expressions.AddExpression(HighlightVertex);
            for(auto& In:Native.Stages->Inputs)if(In.InputName==TEXT("Specular"))In.Input.Connect(0,HighlightVertex);
            auto* NativeColor = NewObject<UMaterialExpressionCustom>(Material);
            NativeColor->OutputType = CMOT_Float3;
            Input(NativeColor, TEXT("Albedo"), Native.Color); Input(NativeColor, TEXT("Tint"), Tint);
            Input(NativeColor, TEXT("Light"), Interpolate);
            // Evaluate the cascade after native vertex lighting, as retail does.
            for(auto &In:Native.Stages->Inputs) if(In.InputName==TEXT("Diffuse")) In.Input.Connect(0,Interpolate);
            Input(NativeColor,TEXT("Original"),Texture);
            for(auto &In:Native.Stages->Inputs) if(In.InputName==TEXT("Enabled"))Input(NativeColor,TEXT("Enabled"),In.Input.Expression);
            NativeColor->Code = TEXT("if(Enabled>0.5)return Albedo; "
                "float3 tex=select(Original<=0.0031308,Original*12.92,1.055*pow(max(Original,0),1.0/2.4)-0.055); "
                "float3 c=saturate(tex*Light); return select(c<=0.04045,c/12.92,pow((c+0.055)/1.055,2.4));");
            Expressions.AddExpression(NativeColor);
            if(Preview) Material->GetEditorOnlyData()->BaseColor.Expression=nullptr;
            else {
                auto* Legacy=NewObject<UMaterialExpressionCustom>(Material); Legacy->OutputType=CMOT_Float3;
                Input(Legacy,TEXT("Color"),Multiply);
                for(auto& In:Native.Stages->Inputs) if(In.InputName==TEXT("Enabled"))Input(Legacy,TEXT("Enabled"),In.Input.Expression);
                Legacy->Code=TEXT("return Enabled>0.5?float3(0,0,0):Color;"); Expressions.AddExpression(Legacy);
                Material->GetEditorOnlyData()->BaseColor.Connect(0,Legacy);
                NativeColor->Code=TEXT("return Enabled>0.5?Albedo:float3(0,0,0);");
            }
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, NativeColor);
            if(!Preview && (Kind==0 || Kind==1 || Kind==6 || Grass)) {
                // World surfaces retain the DAT texture/color cascade, but
                // direct lighting belongs in UE's shadowed lighting pass.
                // Preview scenes still use the retail vertex-lighting path.
                Lighting->Code=TEXT("float3 c=Flags.y>0?Tint.rgb:float3(1,1,1); return Flags.z>0?c*Vertex.rgb:c;");
                Highlight->Code=TEXT("return float3(0,0,0);");
                auto* Albedo=NewObject<UMaterialExpressionCustom>(Material);Albedo->OutputType=CMOT_Float3;
                Input(Albedo,TEXT("Native"),Native.Color);Input(Albedo,TEXT("Fallback"),Multiply);
                for(auto& In:Native.Stages->Inputs)if(In.InputName==TEXT("Enabled"))Input(Albedo,TEXT("Enabled"),In.Input.Expression);
                Albedo->Code=TEXT("return Enabled>0.5?Native:Fallback;");Expressions.AddExpression(Albedo);
                auto* Surface=NewObject<UMaterialExpressionCustom>(Material);Surface->OutputType=CMOT_Float3;
                Input(Surface,TEXT("Color"),Albedo);
                FCustomInput In;In.InputName=TEXT("Flags");In.Input.Connect(5,Flags);Surface->Inputs.Add(In);
                for(auto& V:Native.Stages->Inputs)if(V.InputName==TEXT("Enabled"))Input(Surface,TEXT("Enabled"),V.Input.Expression);
                Surface->Code=TEXT("return Enabled>0.5 && Flags.x==0?float3(0,0,0):Color;");Expressions.AddExpression(Surface);
                Material->GetEditorOnlyData()->BaseColor.Connect(0,Surface);
                auto* Fill=NewObject<UMaterialExpressionCustom>(Material);Fill->OutputType=CMOT_Float3;Fill->Inputs=Surface->Inputs;
                Input(Fill,TEXT("Ambient"),Param(TEXT("AC2UIAmbient"),FLinearColor(.1,.1,.1)));
                Input(Fill,TEXT("MaterialAmbient"),Param(TEXT("AC2UIMaterialAmbient"),FLinearColor::White));
                Fill->Code=TEXT("if(Enabled>0.5 && Flags.x==0)return Color; float3 a=saturate(Ambient*MaterialAmbient); return Color*select(a<=0.04045,a/12.92,pow((a+0.055)/1.055,2.4));");
                Expressions.AddExpression(Fill);Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Fill);
                auto* Finish=NewObject<UMaterialExpressionCustom>(Material);Finish->OutputType=CMOT_Float1;
                Input(Finish,TEXT("Power"),Scalar(TEXT("AC2NativeSpecularPower")));
                Input(Finish,TEXT("Active"),Scalar(TEXT("AC2NativeSpecularEnabled")));
                Input(Finish,TEXT("Color"),Param(TEXT("AC2NativeSpecularColor"),FLinearColor::White));
                Finish->Code=TEXT("return Active>0.5 && Power>0?dot(Color,float3(.2126,.7152,.0722))*0.5:0;");
                Expressions.AddExpression(Finish);Material->GetEditorOnlyData()->Specular.Connect(0,Finish);
                auto* SurfaceRoughness=NewObject<UMaterialExpressionCustom>(Material);SurfaceRoughness->OutputType=CMOT_Float1;SurfaceRoughness->Inputs=Finish->Inputs;
                SurfaceRoughness->Code=TEXT("return Active>0.5 && Power>0?clamp(pow(2/(Power+2),0.25),.15,1):.85;");
                Expressions.AddExpression(SurfaceRoughness);Material->GetEditorOnlyData()->Roughness.Connect(0,SurfaceRoughness);
            }
        }
        if (Kind == 5)
        {
            // D3D9 water/shore/reflection passes blend byte RGB, not linear
            // radiance. The reflection pass blends against the transmission
            // buffer with its previously written destination alpha.
            // Keep the DAT vertex lighting/cascade and compose in native RGB.
            auto Input=[&](UMaterialExpressionCustom* C,const TCHAR* Name,UMaterialExpression* E,int32 Output=0){
                FCustomInput I;I.InputName=Name;I.Input.Connect(Output,E);C->Inputs.Add(I);};
            auto* View=NewObject<UMaterialExpressionCameraVectorWS>(Material);Expressions.AddExpression(View);
            auto* Below=NewObject<UMaterialExpressionScalarParameter>(Material);Below->ParameterName=TEXT("AC2WaterBelow");Expressions.AddExpression(Below);
            auto* Time=NewObject<UMaterialExpressionTime>(Material);Expressions.AddExpression(Time);
            auto* WaterUV=NewObject<UMaterialExpressionTextureCoordinate>(Material);Expressions.AddExpression(WaterUV);
            auto* BumpUV=NewObject<UMaterialExpressionCustom>(Material);BumpUV->OutputType=CMOT_Float2;
            Input(BumpUV,TEXT("UV"),WaterUV);Input(BumpUV,TEXT("Time"),Time);
            BumpUV->Code=TEXT("return (UV+Time*.01)*.5;");Expressions.AddExpression(BumpUV);
            auto* Bump=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
            Bump->ParameterName=TEXT("AC2WaterBumpMap");Bump->Texture=Texture->Texture;
            Bump->Coordinates.Connect(0,BumpUV);Expressions.AddExpression(Bump);
            auto* Screen=NewObject<UMaterialExpressionScreenPosition>(Material);Expressions.AddExpression(Screen);
            auto* ReflectionUV=NewObject<UMaterialExpressionCustom>(Material);ReflectionUV->OutputType=CMOT_Float2;
            Input(ReflectionUV,TEXT("Screen"),Screen);Input(ReflectionUV,TEXT("Bump"),Bump);
            auto* Scale=NewObject<UMaterialExpressionScalarParameter>(Material);Scale->ParameterName=TEXT("AC2WaterProjectionScale");
            Scale->DefaultValue=1;Expressions.AddExpression(Scale);Input(ReflectionUV,TEXT("Scale"),Scale);
            // The capture uses the reflected eye/pitch. Its vertical viewport
            // axis is reversed relative to the main view. D3D9 BUMPENVMAP
            // offsets the projected stage by the signed V8U8 texel.
            ReflectionUV->Code=TEXT("float2 delta=clamp((Bump.rg*255.-128.)/127.,-1.,1.)*.04; return saturate(.5+(float2(Screen.x+delta.x,1.-Screen.y-delta.y)-.5)*Scale);");
            Expressions.AddExpression(ReflectionUV);
            auto* Reflection=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
            Reflection->ParameterName=TEXT("AC2WaterReflection");Reflection->Texture=Texture->Texture;
            Reflection->Coordinates.Connect(0,ReflectionUV);Expressions.AddExpression(Reflection);
            auto* Plane=NewObject<UMaterialExpressionScalarParameter>(Material);
            Plane->ParameterName=TEXT("AC2WaterReflectionHeight");Expressions.AddExpression(Plane);
            auto* Position=NewObject<UMaterialExpressionWorldPosition>(Material);Expressions.AddExpression(Position);
            auto* ShoreUV=NewObject<UMaterialExpressionTextureCoordinate>(Material);ShoreUV->CoordinateIndex=1;Expressions.AddExpression(ShoreUV);
            auto* FoamUV=NewObject<UMaterialExpressionTextureCoordinate>(Material);FoamUV->CoordinateIndex=2;Expressions.AddExpression(FoamUV);
            auto SampleShore=[&](const TCHAR* Name,const TCHAR* Code){
                auto* UV=NewObject<UMaterialExpressionCustom>(Material);UV->OutputType=CMOT_Float2;
                Input(UV,TEXT("Shore"),ShoreUV);Input(UV,TEXT("FoamUV"),FoamUV);
                Input(UV,TEXT("Time"),Time);UV->Code=Code;Expressions.AddExpression(UV);
                auto* Sample=NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);Sample->ParameterName=Name;
                Sample->Texture=Texture->Texture;Sample->Coordinates.Connect(0,UV);Expressions.AddExpression(Sample);return Sample;
            };
            // The active DAT's shore and mask materials supply these texture
            // stages. Their alpha channels have distinct roles in the two passes.
            auto* Mask=SampleShore(TEXT("AC2ShoreMask"),TEXT("return float2(0.5,saturate(Shore.y));"));
            auto* Foam0=SampleShore(TEXT("AC2ShoreFoam"),TEXT("return float2(FoamUV.x+.5+Time*.005,Shore.x+Shore.y+Time*.15);"));
            auto* Foam1=SampleShore(TEXT("AC2ShoreFoam"),TEXT("return float2(FoamUV.x,Shore.x+Shore.y+.5+Time*.1);"));
            auto* FoamMask=SampleShore(TEXT("AC2ShoreFoamMask"),TEXT("return float2(.5,saturate(Shore.y));"));
            auto* FoamEnabled=NewObject<UMaterialExpressionScalarParameter>(Material);
            FoamEnabled->ParameterName=TEXT("AC2ShoreFoamEnabled");Expressions.AddExpression(FoamEnabled);
            auto* Ready=NewObject<UMaterialExpressionScalarParameter>(Material);Ready->ParameterName=TEXT("AC2WaterReflectionReady");
            Expressions.AddExpression(Ready);
            auto* Background=NewObject<UMaterialExpressionSceneColor>(Material);Expressions.AddExpression(Background);
            auto* SceneDepth=NewObject<UMaterialExpressionSceneDepth>(Material);Expressions.AddExpression(SceneDepth);
            auto* PixelDepth=NewObject<UMaterialExpressionPixelDepth>(Material);Expressions.AddExpression(PixelDepth);
            auto* Color=NewObject<UMaterialExpressionCustom>(Material);Color->OutputType=CMOT_Float3;
            Input(Color,TEXT("Below"),Below);
            Input(Color,TEXT("Water"),FogColor(Native.Color,0,false));Input(Color,TEXT("ShoreLight"),ShoreLighting);
            Input(Color,TEXT("Reflection"),Reflection);Input(Color,TEXT("Ready"),Ready);
            Input(Color,TEXT("Plane"),Plane);Input(Color,TEXT("Position"),Position);
            Input(Color,TEXT("Mask"),Mask,4);Input(Color,TEXT("Foam0"),Foam0,5);
            Input(Color,TEXT("Foam1"),Foam1,4);Input(Color,TEXT("FoamMask"),FoamMask,4);
            Input(Color,TEXT("FoamPass"),FoamUV);
            Input(Color,TEXT("Background"),Background);Input(Color,TEXT("Scene"),SceneDepth);
            Input(Color,TEXT("Pixel"),PixelDepth);Input(Color,TEXT("View"),View);
            BindFog(Color);
            Color->Code=TEXT(R"(
struct NativeWater {
 float3 gamma(float3 c){c=saturate(c);return select(c<=.0031308,c*12.92,1.055*pow(c,1.0/2.4)-.055);}
 float3 toLinear(float3 c){return select(c<=.04045,c/12.92,pow((c+.055)/1.055,2.4));}
}; NativeWater fn;
// The underside has its own DAT material and SRCALPHA/INVSRCALPHA blend.
if(Below>.5)return Water;

// DrawWaterMesh first blends the block's authored surface over the bed.
// DrawWater then stencils the screen-space reflection over that result.
// Both draw calls use the destination alpha written by the shore mask.
float contact=saturate(max(0,Scene-Pixel)*abs(View.z)/2);
float weight=.5*saturate(Mask)*contact;
float reflectionWeight=weight*saturate(Ready)*step(abs(Position.z-Plane),1.);
float3 surface=lerp(fn.gamma(Background),fn.gamma(Water),weight);
surface=lerp(surface,fn.gamma(Reflection),reflectionWeight);
float3 bed=fn.toLinear(surface);
if(FoamPass.y>.5){
 // D3D9 blends the shore overlay in display RGB. Solve for the linear
 // source that gives that same result at the actual translucent opacity.
 float a=saturate(2*saturate(Foam0.a+Foam1-.5)*FoamMask)*contact;
 float3 crest=saturate(fn.gamma(Foam0.rgb)*ShoreLight*2);
 float4 range=FogRange;range.z*=((ResolvedView.PostVolumeUserFlags & AC2_NATIVE_FOG_USER_FLAG)!=0)*(FogMode.x>.5 && FogMode.z<.5);
 crest=lerp(crest,FogColor,AC2FogAmount(length(FogPosition-FogCamera),range));
 float3 mixed=fn.toLinear(lerp(surface,crest,a));
 return max(0,bed+(mixed-bed)/max(a,.00001));
}
return bed;
)");
            Expressions.AddExpression(Color);
            Material->GetEditorOnlyData()->BaseColor.Expression=nullptr;
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
            // Background transmission is already included in native RGB above.
            // Applying translucent opacity again would blend in linear twice.
            auto* Face=NewObject<UMaterialExpressionTwoSidedSign>(Material);Expressions.AddExpression(Face);
            auto* Facing=NewObject<UMaterialExpressionCustom>(Material);Facing->OutputType=CMOT_Float1;
            Input(Facing,TEXT("Face"),Face);Input(Facing,TEXT("Below"),Below);
            Input(Facing,TEXT("Alpha"),Native.Alpha);Input(Facing,TEXT("FoamEnabled"),FoamEnabled);
            Input(Facing,TEXT("FoamPass"),FoamUV);Input(Facing,TEXT("Foam0"),Foam0,4);
            Input(Facing,TEXT("Foam1"),Foam1,4);Input(Facing,TEXT("FoamMask"),FoamMask,4);
            Input(Facing,TEXT("Scene"),SceneDepth);Input(Facing,TEXT("Pixel"),PixelDepth);Input(Facing,TEXT("View"),View);
            Facing->Code=TEXT(R"(
if(Below>.5)return step(Face,0)*saturate(Alpha);
if(Face<0)return 0;
if(FoamPass.y<.5)return 1;
return FoamEnabled*saturate(2*saturate(Foam0+Foam1-.5)*FoamMask)*saturate(max(0,Scene-Pixel)*abs(View.z)/2);
)");Expressions.AddExpression(Facing);
            Material->GetEditorOnlyData()->Opacity.Connect(0,Facing);
        }
        if (Kind == 6 || Kind == 7 || Kind == 10)
        {
            auto *Vertex = NewObject<UMaterialExpressionVertexColor>(Material);
            Expressions.AddExpression(Vertex);
            auto *V = NewObject<UMaterialExpressionMultiply>(Material);
            V->A.Connect(0, Multiply);
            V->B.Connect(0, Vertex);
            Expressions.AddExpression(V);
            if (Kind == 6)
                Material->GetEditorOnlyData()->BaseColor.Connect(0, V);
            else
            {
                Material->GetEditorOnlyData()->EmissiveColor.Connect(0, V);
                auto *A = NewObject<UMaterialExpressionMultiply>(Material);
                A->A.Connect(4, Texture);
                A->B.Connect(4, Vertex);
                Expressions.AddExpression(A);
                Material->GetEditorOnlyData()->Opacity.Connect(0, A);
            }
        }
        if (Terrain || Kind == 6)
        {
            // LScapeTerrainMesh::CombineMaterials and InitFar modulate the
            // terrain with the same wrapping, world-space SurfaceDesc cloud map.
            auto* World = NewObject<UMaterialExpressionWorldPosition>(Material);
            auto* Time = NewObject<UMaterialExpressionTime>(Material);
            auto* Motion = NewObject<UMaterialExpressionVectorParameter>(Material);
            Motion->ParameterName = TEXT("AC2CloudMotion");
            Motion->DefaultValue = FLinearColor::Black;
            auto* CloudUV = NewObject<UMaterialExpressionCustom>(Material);
            CloudUV->OutputType = CMOT_Float2;
            CloudUV->Code = TEXT("return Position.xy * (0.01 * Motion.x) + frac(Seconds * Motion.yz);");
            CloudUV->Inputs.SetNum(3);
            CloudUV->Inputs[0].InputName = TEXT("Position"); CloudUV->Inputs[0].Input.Connect(0,World);
            CloudUV->Inputs[1].InputName = TEXT("Seconds"); CloudUV->Inputs[1].Input.Connect(0,Time);
            CloudUV->Inputs[2].InputName = TEXT("Motion"); CloudUV->Inputs[2].Input.Connect(0,Motion);
            auto* Cloud = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
            Cloud->ParameterName = TEXT("AC2CloudShadow");
            Cloud->Texture = Texture->Texture;
            Cloud->Coordinates.Connect(0, CloudUV);
            Expressions.AddExpression(World); Expressions.AddExpression(Time);
            Expressions.AddExpression(Motion); Expressions.AddExpression(CloudUV); Expressions.AddExpression(Cloud);
            auto Modulate = [&](auto& Color) {
                auto* Result = NewObject<UMaterialExpressionMultiply>(Material);
                Result->A.Connect(Color.OutputIndex, Color.Expression);
                Result->B.Connect(0,Cloud);
                Expressions.AddExpression(Result);
                Color.Connect(0,Result);
            };
            Modulate(Material->GetEditorOnlyData()->BaseColor);
            Modulate(Material->GetEditorOnlyData()->EmissiveColor);
        }
        if(FrameBuffer)
        {
            // RenderDeviceD3D uses CAMERASPACEPOSITION and COUNT3|PROJECTED.
            // LM_UVTranslate's fixed-function Translate3 puts the offset in
            // row three, so translation survives the divide by camera-space Z.
            auto* Position=NewObject<UMaterialExpressionWorldPosition>(Material);
            auto* View=NewObject<UMaterialExpressionTransformPosition>(Material);
            View->TransformSourceType=TRANSFORMPOSSOURCE_World;
            View->TransformType=TRANSFORMPOSSOURCE_View;
            View->Input.Connect(0,Position);
            auto* Project=NewObject<UMaterialExpressionCustom>(Material);
            Project->OutputType=CMOT_Float2;
            Project->Code=TEXT("float3 p=float3(ViewPosition.xy/max(ViewPosition.z,0.0001),1); "
                               "return frac(float2(dot(p,RowU),dot(p,RowV)));");
            Project->Inputs.SetNum(3);
            Project->Inputs[0].InputName=TEXT("ViewPosition");Project->Inputs[0].Input.Connect(0,View);
            Project->Inputs[1].InputName=TEXT("RowU");Project->Inputs[1].Input.Connect(0,RowU);
            Project->Inputs[2].InputName=TEXT("RowV");Project->Inputs[2].Input.Connect(0,RowV);
            auto* Scene=NewObject<UMaterialExpressionSceneColor>(Material);
            Scene->InputMode=EMaterialSceneAttributeInputMode::Coordinates;Scene->Input.Connect(0,Project);
            Expressions.AddExpression(Position);Expressions.AddExpression(View);
            Expressions.AddExpression(Project);Expressions.AddExpression(Scene);
            Material->GetEditorOnlyData()->BaseColor.Expression=nullptr;
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Scene);
            Material->TranslucencyPass=MTP_BeforeDOF;
        }
        if(!Preview && !Sky && !FrameBuffer && (Kind==7 || Kind==8 || Kind==9 || Kind==10)){
            const bool Additive=Kind==9 || Kind==10;
            auto& Emissive=Material->GetEditorOnlyData()->EmissiveColor;
            Emissive.Connect(0,FogColor(Emissive.Expression,Emissive.OutputIndex,Additive));
            // Non-native fallback albedo still uses UE lighting; attenuate that
            // contribution without adding a second copy of the fog colour.
            auto& BaseColor=Material->GetEditorOnlyData()->BaseColor;
            if(BaseColor.Expression){
                auto* Attenuate=NewObject<UMaterialExpressionCustom>(Material);Attenuate->OutputType=CMOT_Float3;
                FogInput(Attenuate,TEXT("Source"),BaseColor.Expression,BaseColor.OutputIndex);BindFog(Attenuate);
                Attenuate->Code=TEXT("float4 r=FogRange;r.z*=((ResolvedView.PostVolumeUserFlags & AC2_NATIVE_FOG_USER_FLAG)!=0)*(FogMode.x>.5 && FogMode.z<.5); return Source*(1-AC2FogAmount(length(FogPosition-FogCamera),r));");
                Expressions.AddExpression(Attenuate);BaseColor.Connect(0,Attenuate);
            }
        }
        if(!Preview && !Sky && !FrameBuffer && Kind!=5 && !Grass){
            // Depth-alpha fog uses FrillRender's independent final eight metres;
            // diffuse-fog mode fades alpha with world fog, leaving RGB alone.
            auto* Fade=NewObject<UMaterialExpressionCustom>(Material);Fade->OutputType=CMOT_Float1;
            auto& Alpha=Masked?Material->GetEditorOnlyData()->OpacityMask:Material->GetEditorOnlyData()->Opacity;
            if(Alpha.Expression){
                FogInput(Fade,TEXT("Source"),Alpha.Expression,Alpha.OutputIndex);BindFog(Fade);
                Fade->Code=TEXT("float enabled=((ResolvedView.PostVolumeUserFlags & AC2_NATIVE_FOG_USER_FLAG)!=0); "
                    "float d=length(FogPosition-FogCamera);float4 a=FogAlphaRange;a.z*=enabled*FogMode.y; "
                    "float4 f=FogRange;f.z*=enabled*FogMode.z; "
                    "return Source*(1-AC2FogAmount(d,a))*(1-AC2FogAmount(d,f));");
                Expressions.AddExpression(Fade);Alpha.Connect(0,Fade);
            }
        }
        if(Preview || !(Kind==0 || Kind==1 || Kind==5 || Kind==6 || Grass))Material->GetEditorOnlyData()->Roughness.Connect(0, Roughness);
        Material->PostEditChange();
        if (GShaderCompilingManager)
            GShaderCompilingManager->FinishAllCompilation();
        Package->MarkPackageDirty();
        const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Material, *File, Args))
        {
            Error = TEXT("Cannot save ") + File;
            return false;
        }
        UE_LOG(LogTemp, Display, TEXT("AC2 material template saved: %s"), *File);
    }
    if (!FFileHelper::SaveStringToFile(
            TEXT("46"), *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("AC2/Materials/template-version.txt"))))
    {
        Error = TEXT("Cannot save AC2 material template version");
        return false;
    }
    return true;
#else
    Error = TEXT("Generate AC2 materials with the editor build first");
    return false;
#endif
}
