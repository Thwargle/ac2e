#include "AC2Assets.h"
#include "AC2NativeMaterial.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
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
    const bool CurrentVersion=FFileHelper::LoadFileToString(SavedVersion,*VersionPath) && SavedVersion.TrimStartAndEnd()==TEXT("18");
    // SceneColor HDR stores inverse opacity. Native UI scenes draw over the
    // existing page, so preserve transparent pixels instead of a black rectangle.
    const FString CapturePath=TEXT("/Game/AC2/Materials/M_AC2Capture");
    auto* CaptureMaterial=LoadObject<UMaterial>(nullptr,*(CapturePath+TEXT(".M_AC2Capture")));
    if(!CurrentVersion || !CaptureMaterial)
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
    for (int32 Template = 0; Template < 32; ++Template)
    {
        const int32 Kind=Template%16;
        const bool Alternate=Template>=16;
        // Native mesh materials need both culling permutations. Sky, particles,
        // water and generated grass retain their separate rendering policies.
        if(Alternate && !(Kind==0 || Kind==1 || Kind==8 || Kind==9 || Kind>=12))continue;
        const bool Preview = Kind >= 12, Grass = Kind == 11, Masked = Kind == 1 || Kind == 13 || Grass, Sky = Kind >= 2 && Kind <= 4;
        const bool DefaultTwoSided=Masked || Sky || (Kind >= 5 && Kind != 12);
        const bool TwoSided=Alternate?!DefaultTwoSided:DefaultTwoSided;
        FString Name = Kind == 0    ? TEXT("M_AC2Opaque")
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
        if(Alternate)Name+=TwoSided?TEXT("TwoSided"):TEXT("OneSided");
        const FString Path = TEXT("/Game/AC2/Materials/") + Name;
        auto *Existing = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Name));
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
        const bool NativeSpecular = Existing && Cast<UMaterialExpressionScalarParameter>(Existing->GetEditorOnlyData()->Specular.Expression) != nullptr;
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
        if (Sky || Kind == 7 || Kind == 10 || Preview)
            Material->SetShadingModel(MSM_Unlit);
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
        // Native highlights use COLOR1 below. UE's separate dielectric response
        // would otherwise add a pale reflection over the authored finish.
        auto *Specular = NewObject<UMaterialExpressionScalarParameter>(Material);
        Specular->ParameterName = TEXT("AC2Specular");
        Specular->DefaultValue = Kind == 5 ? .5f : 0.f;
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
        auto Native=BuildAC2NativeStages(Material,Texture,Tint,Preview);
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
                Alpha->Code = TEXT("float a=saturate(TexAlpha); "
                                   // Native frill visibility measures planar distance. A raised
                                   // camera must not shorten ground cover's visible radius.
                                   "float fade=saturate((16000.0-distance(World.xy,Camera.xy))/4000.0); "
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
                Expressions.AddExpression(Position);
                Expressions.AddExpression(Camera);
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
        if (Preview || Kind==0 || Kind==1 || Kind==8 || Kind==9 || Grass)
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
        }
        if (Kind == 5)
        {
            auto *Pan = NewObject<UMaterialExpressionPanner>(Material);
            Pan->SpeedX = .015f;
            Pan->SpeedY = .008f;
            Pan->Coordinate.Connect(0, UV);
            Texture->Coordinates.Connect(0, Pan);
            Expressions.AddExpression(Pan);
            auto *Fade = NewObject<UMaterialExpressionDepthFade>(Material);
            Fade->OpacityDefault = .72f;
            Fade->FadeDistanceDefault = 110;
            Expressions.AddExpression(Fade);
            Material->GetEditorOnlyData()->Opacity.Connect(0, Fade);
            Roughness->R = .18f;
            Material->TranslucencyLightingMode = TLM_Surface;
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
        Material->GetEditorOnlyData()->Roughness.Connect(0, Roughness);
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
            TEXT("18"), *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("AC2/Materials/template-version.txt"))))
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
