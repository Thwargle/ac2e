#include "AC2Assets.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
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
    for (int32 Kind = 0; Kind < 12; ++Kind)
    {
        const bool Grass = Kind == 11, Masked = Kind == 1 || Grass, Sky = Kind >= 2 && Kind <= 4;
        const FString Name = Kind == 0    ? TEXT("M_AC2Opaque")
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
                                          : TEXT("M_AC2Grass");
        const FString Path = TEXT("/Game/AC2/Materials/") + Name;
        auto *Existing = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + Name));
        bool Updated = false;
        if (Existing)
            for (const auto &E : Existing->GetExpressions())
                if (auto *P = Cast<UMaterialExpressionVectorParameter>(E))
                    Updated |= P->ParameterName ==
                               ((Kind == 1 || Kind == 8 || Kind == 9) ? TEXT("AC2Opacity") : TEXT("AC2UVRowU"));
        const bool NativeNormals = Existing && !Existing->bTangentSpaceNormal &&
            Cast<UMaterialExpressionVertexNormalWS>(Existing->GetEditorOnlyData()->Normal.Expression) != nullptr;
        if (Updated && (!Masked || NativeNormals))
            continue;
        UPackage *Package = CreatePackage(*Path);
        auto *Material = Existing ? Existing : NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
        Material->GetExpressionCollection().Empty();
        Material->bIsSky = Kind == 2;
        Material->BlendMode = Kind == 3 || Kind == 5 || Kind == 7 || Kind == 8 ? BLEND_Translucent
                              : (Kind == 4 || Kind == 9 || Kind == 10)         ? BLEND_Additive
                              : Masked                                         ? BLEND_Masked
                                                                               : BLEND_Opaque;
        Material->TwoSided = Masked || Sky || Kind >= 5;
        if (Sky || Kind == 7 || Kind == 10)
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
        if (Masked)
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
        if (Kind == 1 || Kind == 8 || Kind == 9 || Grass)
        {
            auto *Opacity = NewObject<UMaterialExpressionVectorParameter>(Material);
            Opacity->ParameterName = TEXT("AC2Opacity");
            Opacity->DefaultValue = FLinearColor(1, 0, 0, 0);
            auto *Alpha = NewObject<UMaterialExpressionCustom>(Material);
            Alpha->OutputType = CMOT_Float1;
            Alpha->Code = TEXT("return saturate(TexAlpha * Opacity.x + Opacity.y);");
            Alpha->Inputs.SetNum(2);
            Alpha->Inputs[0].InputName = TEXT("TexAlpha");
            Alpha->Inputs[0].Input.Connect(4, Texture);
            Alpha->Inputs[1].InputName = TEXT("Opacity");
            Alpha->Inputs[1].Input.Connect(0, Opacity);
            Expressions.AddExpression(Opacity);
            Expressions.AddExpression(Alpha);
            if (Grass)
            {
                auto *Position = NewObject<UMaterialExpressionWorldPosition>(Material);
                auto *Camera = NewObject<UMaterialExpressionCameraPositionWS>(Material);
                Alpha->Code = TEXT("float a=saturate(TexAlpha*Opacity.x+Opacity.y); "
                                   "float fade=saturate((8000.0-distance(World,Camera))/2000.0); "
                                   "float3 p=floor(World*0.5); "
                                   "float noise=frac(sin(dot(p,float3(12.9898,78.233,37.719)))*43758.5453); "
                                   "return a*step(noise+0.00001,fade);");
                Alpha->Inputs.SetNum(4);
                Alpha->Inputs[2].InputName = TEXT("World");
                Alpha->Inputs[2].Input.Connect(0, Position);
                Alpha->Inputs[3].InputName = TEXT("Camera");
                Alpha->Inputs[3].Input.Connect(0, Camera);
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
            auto *Vertex = NewObject<UMaterialExpressionVertexColor>(Material);
            auto *SkyTint = NewObject<UMaterialExpressionVectorParameter>(Material);
            SkyTint->ParameterName = TEXT("SkyTint");
            SkyTint->DefaultValue = FLinearColor::White;
            auto *V = NewObject<UMaterialExpressionMultiply>(Material);
            V->A.Connect(0, Multiply);
            V->B.Connect(0, Vertex);
            auto *S = NewObject<UMaterialExpressionMultiply>(Material);
            S->A.Connect(0, V);
            S->B.Connect(0, SkyTint);
            auto *Alpha = NewObject<UMaterialExpressionMultiply>(Material);
            Alpha->A.Connect(4, Texture);
            Alpha->B.Connect(4, Vertex);
            auto *AlphaTint = NewObject<UMaterialExpressionMultiply>(Material);
            AlphaTint->A.Connect(0, Alpha);
            AlphaTint->B.Connect(4, SkyTint);
            for (UMaterialExpression *E :
                 {(UMaterialExpression *)Vertex, (UMaterialExpression *)SkyTint, (UMaterialExpression *)V,
                  (UMaterialExpression *)S, (UMaterialExpression *)Alpha, (UMaterialExpression *)AlphaTint})
                Expressions.AddExpression(E);
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, S);
            Material->GetEditorOnlyData()->Opacity.Connect(0, AlphaTint);
        }
        else
            Material->GetEditorOnlyData()->BaseColor.Connect(0, Multiply);
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
            TEXT("4"), *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("AC2/Materials/template-version.txt"))))
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
