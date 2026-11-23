#include "AC2Fog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RendererInterface.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "ShaderParameterStruct.h"

namespace
{
class FAC2FogPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FAC2FogPS);
    SHADER_USE_PARAMETER_STRUCT(FAC2FogPS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FogSceneColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FogSceneDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FogCustomDepth)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, FogCustomStencil)
        SHADER_PARAMETER(FVector4f, FogRange)
        SHADER_PARAMETER(FVector3f, FogColor)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FAC2FogPS, "/AC2/Private/AC2Fog.usf", "MainPS", SF_Pixel);

// Never dereference a UWorld on the render thread. The scene identity and a
// value-copy of its settings are sufficient for every main/capture view.
FCriticalSection SceneMutex;
TMap<const FSceneInterface*, FAC2FogParameters> SceneParameters;
TWeakObjectPtr<UMaterialParameterCollection> FogCollection;
FDelegateHandle PostOpaqueHandle, WorldCleanupHandle, WorldTickHandle;

// Material flags are per section, while UE stencil state is per primitive.
// Most exempt meshes can write their own custom depth. A mixed procedural mesh
// uses a depth-only child containing just the exempt sections. Static copies
// are retained; CPU skinning explicitly marks them dirty when vertices change.
constexpr int32 FogExemptStencil = 0x80;
struct FComponentFogMask
{
    bool OriginalCustomDepth = false;
    int32 OriginalStencil = 0;
    ERendererStencilMask OriginalWriteMask = ERendererStencilMask::ERSM_Default;
    bool OriginalMarked = false;
    bool GeometryDirty = true;
    TWeakObjectPtr<UProceduralMeshComponent> Proxy;
    TArray<int32> Sections;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
};
TMap<TWeakObjectPtr<UMeshComponent>, FComponentFogMask> ComponentMasks;

void RestoreOriginal(UMeshComponent* Component, FComponentFogMask& State)
{
    if (!State.OriginalMarked) return;
    Component->SetRenderCustomDepth(State.OriginalCustomDepth);
    Component->SetCustomDepthStencilValue(State.OriginalStencil);
    Component->SetCustomDepthStencilWriteMask(State.OriginalWriteMask);
    State.OriginalMarked = false;
}

void DestroyProxy(FComponentFogMask& State)
{
    if (auto* Proxy = State.Proxy.Get()) Proxy->DestroyComponent();
    State.Proxy.Reset();
}

void SyncFogComponent(UMeshComponent* Component)
{
    if (!Component || Component->IsBeingDestroyed()) return;
    auto* Procedural = Cast<UProceduralMeshComponent>(Component);
    TArray<int32> Exempt;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
    int32 Fogged = 0;
    bool HasExemptMaterial = false;
    for (int32 Section = 0; Section < Component->GetNumMaterials(); ++Section)
    {
        if (Procedural)
        {
            const auto* Geometry = Procedural->GetProcMeshSection(Section);
            if (!Geometry || Geometry->ProcIndexBuffer.IsEmpty()) continue;
        }
        auto* Material = Component->GetMaterial(Section);
        if (!Material || (Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked)) continue;
        FLinearColor Mode(1, 0, 0, 0);
        Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("AC2FogMode")), Mode);
        // RenderDeviceD3D::SetMaterial enables RGB depth fog only when
        // depthFog is true and diffuseFog is false. DiffuseFog affects alpha.
        const bool ExemptMaterial = Mode.R < .5f || Mode.B > .5f;
        HasExemptMaterial |= ExemptMaterial;
        if (Procedural && !Procedural->GetProcMeshSection(Section)->bSectionVisible) continue;
        if (ExemptMaterial) { Exempt.Add(Section); Materials.Add(Material); }
        else ++Fogged;
    }
    if (!HasExemptMaterial)
    {
        if (auto* State = ComponentMasks.Find(Component))
        {
            RestoreOriginal(Component, *State);
            DestroyProxy(*State);
            ComponentMasks.Remove(Component);
        }
        return;
    }
    auto* Existing = ComponentMasks.Find(Component);
    if (!Existing)
    {
        auto& Added = ComponentMasks.Add(Component);
        Added.OriginalCustomDepth = Component->bRenderCustomDepth;
        Added.OriginalStencil = Component->CustomDepthStencilValue;
        Added.OriginalWriteMask = Component->CustomDepthStencilWriteMask;
        Existing = &Added;
    }
    auto& State = *Existing;
    if (Exempt.IsEmpty())
    {
        // Visibility can change without changing a material ID. Retain the
        // authored exemption so a hidden section is restored on the next tick.
        RestoreOriginal(Component, State);
        DestroyProxy(State);
        return;
    }
    if (Fogged == 0)
    {
        DestroyProxy(State);
        Component->SetCustomDepthStencilValue(State.OriginalStencil | FogExemptStencil);
        Component->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_128);
        Component->SetRenderCustomDepth(true);
        State.OriginalMarked = true;
        return;
    }
    RestoreOriginal(Component, State);
    // World DAT geometry is procedural. Never exempt a whole mixed component
    // merely because one of its material slots opts out of fog.
    if (!Procedural || !Component->GetOwner()) return;
    auto* Proxy = State.Proxy.Get();
    if (!Proxy)
    {
        Proxy = NewObject<UProceduralMeshComponent>(Component->GetOwner(), NAME_None, RF_Transient);
        Component->GetOwner()->AddInstanceComponent(Proxy);
        Proxy->SetupAttachment(Component);
        Proxy->SetRelativeTransform(FTransform::Identity);
        Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Proxy->SetCollisionProfileName(TEXT("NoCollision"));
        Proxy->SetGenerateOverlapEvents(false);
        Proxy->SetCanEverAffectNavigation(false);
        Proxy->SetCastShadow(false);
        Proxy->SetRenderInMainPass(false);
        Proxy->SetRenderInDepthPass(false);
        Proxy->SetReceivesDecals(false);
        Proxy->SetCustomDepthStencilValue(FogExemptStencil);
        Proxy->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_128);
        Proxy->SetRenderCustomDepth(true);
        Proxy->RegisterComponent();
        State.Proxy = Proxy;
        State.GeometryDirty = true;
    }
    Proxy->SetVisibility(Component->IsVisible());
    Proxy->SetHiddenInGame(Component->bHiddenInGame);
    if (State.GeometryDirty || State.Sections != Exempt || State.Materials != Materials)
    {
        Proxy->ClearAllMeshSections();
        for (int32 I = 0; I < Exempt.Num(); ++I)
        {
            auto Geometry = *Procedural->GetProcMeshSection(Exempt[I]);
            Geometry.bEnableCollision = false;
            Proxy->SetProcMeshSection(I, Geometry);
            Proxy->SetMaterial(I, Materials[I].Get());
        }
        State.Sections = MoveTemp(Exempt);
        State.Materials = MoveTemp(Materials);
        State.GeometryDirty = false;
    }
}

void SyncFogWorld(UWorld* World)
{
    TArray<TWeakObjectPtr<UMeshComponent>> Components;
    ComponentMasks.GetKeys(Components);
    for (auto Weak : Components)
    {
        auto* Component = Weak.Get();
        if (!Component || Component->IsBeingDestroyed())
        {
            if (auto* State = ComponentMasks.Find(Weak)) DestroyProxy(*State);
            ComponentMasks.Remove(Weak);
        }
        else if (Component->GetWorld() == World) SyncFogComponent(Component);
    }
}

void ForgetScene(UWorld* World)
{
    if (!World) return;
    FScopeLock Lock(&SceneMutex);
    SceneParameters.Remove(World->Scene);
}

class FAC2FogFamilyData : public ISceneViewFamilyExtentionData
{
public:
    inline static const TCHAR* GSubclassIdentifier = TEXT("AC2NativeFog");
    virtual const TCHAR* GetSubclassIdentifier() const override { return GSubclassIdentifier; }
    TOptional<FAC2FogParameters> Fog;
};

class FAC2FogViewExtension : public FSceneViewExtensionBase
{
public:
    FAC2FogViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

    virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        // UE calls SetupView after EndFinalPostprocessSettings in LocalPlayer
        // and SceneCaptureRendering. Snapshot settings with the view family so
        // a later game-thread update cannot mix next frame's opaque fog with
        // this frame's material collection. Family copies retain this data.
        auto* Data = Family.GetOrCreateExtentionData<FAC2FogFamilyData>();
        Data->Fog.Reset();
        {
            FScopeLock Lock(&SceneMutex);
            if (const auto* Parameters = SceneParameters.Find(Family.Scene)) Data->Fog = *Parameters;
        }
        View.FinalPostProcessSettings.UserFlags &= ~AC2FogViewFlag;
        if (Data->Fog.IsSet() && Family.EngineShowFlags.Fog)
            View.FinalPostProcessSettings.UserFlags |= AC2FogViewFlag;
    }

    virtual void PreRenderView_RenderThread(FRDGBuilder&, FSceneView& View) override
    {
        // Real UI/portal scene captures disable the Fog show flag. Ordinary
        // world captures and reflected world views retain the same fog rules.
        View.FinalPostProcessSettings.UserFlags &= ~AC2FogViewFlag;
        if (!View.Family || !View.Family->EngineShowFlags.Fog) return;
        const auto* Data = View.Family->GetExtentionData<FAC2FogFamilyData>();
        if (Data && Data->Fog.IsSet())
        {
            View.FinalPostProcessSettings.UserFlags |= AC2FogViewFlag;
            if (Data->Fog->Enabled) Views.Add(&View, FSnapshot{&View, Data->Fog.GetValue()});
        }
    }
    virtual void PostRenderViewFamily_RenderThread(FRDGBuilder&, FSceneViewFamily& Family) override
    {
        // A depth-only or otherwise abbreviated renderer may skip post-opaque.
        for (auto It = Views.CreateIterator(); It; ++It)
            if (It.Value().View->Family == &Family) It.RemoveCurrent();
    }

    void RenderOpaque(FPostOpaqueRenderParameters& Render)
    {
        FSnapshot Snapshot;
        if (!Views.RemoveAndCopyValue(Render.Uid, Snapshot)) return;
        const FSceneView& View = *Snapshot.View;
        if (!Render.GraphBuilder || !Render.ColorTexture || !Render.DepthTexture ||
            !Render.SceneTexturesUniformParams || Render.ViewportRect.IsEmpty() || View.GetFeatureLevel() < ERHIFeatureLevel::SM5)
            return;
        auto& GraphBuilder = *Render.GraphBuilder;
        // Read a copy of the lit opaque scene, then write its fogged result back
        // before translucency. Fogging a final composite would incorrectly use
        // the riverbed depth for water, particles and other transparent objects.
        auto Desc = Render.ColorTexture->Desc;
        Desc.Flags |= TexCreate_ShaderResource;
        auto* Original = GraphBuilder.CreateTexture(Desc, TEXT("AC2.FogOpaqueSource"));
        AddCopyTexturePass(GraphBuilder, Render.ColorTexture, Original,
            Render.ViewportRect.Min, Render.ViewportRect.Min, Render.ViewportRect.Size());
        auto* Parameters = GraphBuilder.AllocParameters<FAC2FogPS::FParameters>();
        Parameters->View = View.ViewUniformBuffer;
        Parameters->FogSceneColor = Original;
        Parameters->FogSceneDepth = Render.DepthTexture;
        const auto* SceneTextures = Render.SceneTexturesUniformParams->GetContents();
        Parameters->FogCustomDepth = SceneTextures->CustomDepthTexture;
        Parameters->FogCustomStencil = SceneTextures->CustomStencilTexture;
        Parameters->FogRange = FVector4f(Snapshot.Fog.Start * 100, Snapshot.Fog.End * 100, 1, 0);
        Parameters->FogColor = FVector3f(Snapshot.Fog.Color.R, Snapshot.Fog.Color.G, Snapshot.Fog.Color.B);
        Parameters->RenderTargets[0] = FRenderTargetBinding(Render.ColorTexture, ERenderTargetLoadAction::ELoad);
        const auto* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
        const TShaderMapRef<FAC2FogPS> Shader(ShaderMap);
        FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("AC2 Native Linear Fog"),
            Shader, Parameters, Render.ViewportRect);
    }

protected:
    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext&) const override
    {
        return true;
    }

private:
    struct FSnapshot { const FSceneView* View = nullptr; FAC2FogParameters Fog; };
    TMap<const void*, FSnapshot> Views;
};
TSharedPtr<FAC2FogViewExtension, ESPMode::ThreadSafe> FogViews;

void SetCollection(UWorld* World, const FAC2FogParameters& Parameters, bool Load, bool Active = true)
{
    if (!World) return;
    if (Load && !FogCollection.IsValid())
        FogCollection = LoadObject<UMaterialParameterCollection>(nullptr,
            TEXT("/Game/AC2/Materials/AC2Fog.AC2Fog"));
    if (auto* Collection = FogCollection.Get())
        if (auto* Instance = World->GetParameterCollectionInstance(Collection))
        {
            Instance->SetVectorParameterValue(TEXT("Range"),
                FLinearColor(Parameters.Start * 100, Parameters.End * 100, Parameters.Enabled ? 1.f : 0.f, 0));
            Instance->SetVectorParameterValue(TEXT("Color"), Parameters.Color);
            // Native c2 alpha fog remains active even when c1 world fog is NONE.
            Instance->SetVectorParameterValue(TEXT("AlphaRange"),
                FLinearColor(Parameters.AlphaStart * 100, Parameters.AlphaEnd * 100, Active ? 1.f : 0.f, 0));
        }
}
}

void StartupAC2Fog()
{
    const FString ShaderDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
    if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/AC2")))
        AddShaderSourceDirectoryMapping(TEXT("/AC2"), ShaderDirectory);
    if (!WorldTickHandle.IsValid())
        WorldTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda(
            [](UWorld* World, ELevelTick, float) { SyncFogWorld(World); });
    if (!WorldCleanupHandle.IsValid())
        WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda(
            [](UWorld* World, bool, bool) { ForgetScene(World); });
}

void ShutdownAC2Fog()
{
    if (WorldTickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(WorldTickHandle);
    WorldTickHandle.Reset();
    if (WorldCleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
    WorldCleanupHandle.Reset();
    if (PostOpaqueHandle.IsValid())
    {
        if (auto* Renderer = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
            Renderer->RemovePostOpaqueRenderDelegate(PostOpaqueHandle);
        PostOpaqueHandle.Reset();
    }
    // Module reload can happen while world components are still alive. Undo
    // both the stencil overrides and mixed-section proxy components before
    // forgetting their original state.
    for (auto& Pair : ComponentMasks)
    {
        if (auto* Component = Pair.Key.Get()) RestoreOriginal(Component, Pair.Value);
        DestroyProxy(Pair.Value);
    }
    ComponentMasks.Reset();
    // The extension and its cached view pointers are render-thread resources.
    FlushRenderingCommands();
    FogViews.Reset();
    FogCollection.Reset();
    FScopeLock Lock(&SceneMutex);
    SceneParameters.Reset();
}

void UpdateAC2Fog(UWorld* World, const FAC2FogParameters& InParameters)
{
    check(IsInGameThread());
    if (!World || !World->Scene) return;
    FAC2FogParameters Parameters = InParameters;
    if (!FMath::IsFinite(Parameters.Start) || !FMath::IsFinite(Parameters.End) ||
        !FMath::IsFinite(Parameters.AlphaStart) || !FMath::IsFinite(Parameters.AlphaEnd) ||
        !FMath::IsFinite(Parameters.Color.R) || !FMath::IsFinite(Parameters.Color.G) || !FMath::IsFinite(Parameters.Color.B))
    {
        Parameters = FAC2FogParameters();
        Parameters.Enabled = false;
    }
    {
        FScopeLock Lock(&SceneMutex);
        SceneParameters.Add(World->Scene, Parameters);
    }
    SetCollection(World, Parameters, true);
    if (!FogViews && FApp::CanEverRender())
    {
        FogViews = FSceneViewExtensions::NewExtension<FAC2FogViewExtension>();
        auto& Renderer = FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"));
        PostOpaqueHandle = Renderer.RegisterPostOpaqueRenderDelegate(
            FPostOpaqueRenderDelegate::CreateRaw(FogViews.Get(), &FAC2FogViewExtension::RenderOpaque));
    }
}

void ReleaseAC2Fog(UWorld* World)
{
    check(IsInGameThread());
    FAC2FogParameters Disabled;
    Disabled.Enabled = false;
    SetCollection(World, Disabled, false, false);
    ForgetScene(World);
}

bool GetAC2Fog(UWorld* World, FAC2FogParameters& OutParameters)
{
    check(IsInGameThread());
    if (!World) return false;
    FScopeLock Lock(&SceneMutex);
    if (const auto* Parameters = SceneParameters.Find(World->Scene))
    {
        OutParameters = *Parameters;
        return true;
    }
    return false;
}



void RegisterAC2FogComponent(UMeshComponent* Component)
{
    check(IsInGameThread());
    SyncFogComponent(Component);
}

void MarkAC2FogGeometryDirty(UMeshComponent* Component)
{
    check(IsInGameThread());
    if (auto* State = ComponentMasks.Find(Component)) State->GeometryDirty = true;
}




