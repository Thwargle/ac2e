#include "AC2AttachmentStreak.h"
#include "AC2AssetReader.h"
#include "AC2Assets.h"
#include "AC2NativeMaterial.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

FAC2StreakDefinition FAC2StreakDefinition::Read(FAC2AssetReader& Reader)
{
    FAC2StreakDefinition D;
    D.Enabled = Reader.Word() != 0;
    D.Color = FColor(Reader.Word());
    D.Edge0 = Reader.Vector() * 100;
    D.Edge1 = Reader.Vector() * 100;
    D.Material = Reader.Word();
    D.Life = Reader.Float();
    D.Duration = Reader.Float();
    D.MinTime = Reader.Float();
    D.MinDistance = Reader.Float() * 100;
    D.TextureScale = Reader.Float();
    D.Enabled &= Reader.Good && D.Duration > 0 && D.Life >= 0 && D.MinTime >= 0 && D.MinDistance >= 0;
    return D;
}

void FAC2AttachmentStreak::Activate(bool On)
{
    if (On && !Active) Samples.Reset();
    Active = On;
}

void FAC2AttachmentStreak::Release()
{
    if (Mesh.IsValid()) Mesh->DestroyComponent();
    Mesh.Reset();
}

void FAC2AttachmentStreak::Update(double Time, USceneComponent& Root, const FVector& ModelScale, FAC2AssetLibrary& Assets, bool WorldLighting)
{
    if (Definition.Life > 0 && Time - Created > Definition.Life) Active = false;
    Samples.RemoveAll([&](const FSample& S) { return Time - S.Time > Definition.Duration + .1; });
    if (Active)
    {
        const FTransform& T = Root.GetComponentTransform();
        const double Scale = (ModelScale * T.GetScale3D()).GetAbsMax();
        if (Samples.IsEmpty()) Origin = T.GetLocation();
        const FSample Tip = {T.TransformPositionNoScale(Definition.Edge0 * Scale) - Origin,
                             T.TransformPositionNoScale(Definition.Edge1 * Scale) - Origin, Time, 0};
        if (Samples.Num() < 2)
        {
            if (Samples.IsEmpty()) Samples.Add(Tip);
            Samples.Add(Tip);
        }
        FSample& Live = Samples.Last();
        const FSample& Last = Samples.Last(1);
        const double Distance = FMath::Max(FVector::Distance(Tip.A, Last.A), FVector::Distance(Tip.B, Last.B));
        Live = Tip;
        Live.Distance = Last.Distance + Distance * .01;
        if (Distance > Definition.MinDistance || (Definition.MinTime > 0 && Time - Last.Time >= Definition.MinTime))
        {
            // Retail StreakScene has 4000 vertices. Keep the newest samples.
            const FSample NewTip = Live;
            if (Samples.Num() == 2000) Samples.RemoveAt(0, 1, EAllowShrinking::No);
            Samples.Add(NewTip);
        }
    }
    if (Samples.Num() < 2 || Samples.Last().Distance == Samples[0].Distance)
    {
        if (Mesh.IsValid() && Mesh->GetNumSections()) Mesh->ClearAllMeshSections();
        return;
    }
    if (!Mesh.IsValid())
    {
        auto* Material = Assets.Material(Definition.Material);
        if (!Material) return;
        auto* Component = NewObject<UProceduralMeshComponent>(Root.GetOwner());
        Root.GetOwner()->AddInstanceComponent(Component);
        Component->SetupAttachment(&Root);
        Component->SetAbsolute(true,true,true);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCastShadow(false);
        Component->SetMaterial(0, Material);
        Component->RegisterComponent();
        if (WorldLighting) RegisterAC2WorldMaterial(Cast<UMaterialInstanceDynamic>(Material), Component, false);
        Mesh = Component;
    }
    Mesh->SetWorldLocation(Origin);
    FProcMeshSection Section;
    Section.ProcVertexBuffer.Reserve(Samples.Num() * 2);
    Section.ProcIndexBuffer.Reserve((Samples.Num() - 1) * 6);
    for (const FSample& S : Samples)
    {
        const double Fade = FMath::Clamp(1.0 - (Time - S.Time) / Definition.Duration, 0.0, 1.0);
        FProcMeshVertex V;
        V.Normal = FVector::UpVector;
        V.Color = Definition.Color;
        V.Color.A = FMath::RoundToInt(V.Color.A * Fade * Fade);
        V.UV0 = FVector2D(S.Distance * Definition.TextureScale, 0);
        V.Position = S.B;
        const int32 Index = Section.ProcVertexBuffer.Add(V);
        V.Position = S.A;
        V.UV0.Y = 1;
        Section.ProcVertexBuffer.Add(V);
        Section.SectionLocalBox += S.A;
        Section.SectionLocalBox += S.B;
        if (Index > 0)
            Section.ProcIndexBuffer.Append({uint32(Index-2), uint32(Index-1), uint32(Index),
                                            uint32(Index), uint32(Index-1), uint32(Index+1)});
    }
    Mesh->SetProcMeshSection(0, Section);
}
