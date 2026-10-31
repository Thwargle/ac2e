#include "AC2CharacterUI.h"
#include "AC2Cursor.h"
#include "Framework/Application/SlateApplication.h"
#include "AC2GameUI.h"
#include "AC2PortalSpace.h"
#include "Engine/GameViewportClient.h"
#include "AC2GameMode.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

void AAC2HUD::BeginPlay()
{
    Super::BeginPlay();
    auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>();
    if (!M)
        return;
    if (auto *Viewport = GetWorld()->GetGameViewport())
    {
        SAssignNew(MouseCursor,SAC2Cursor).Directory(M->DatDirectory).Icon_Lambda([Weak=TWeakObjectPtr<AAC2HUD>(this)]() {
            if(!Weak.IsValid())return FAC2CursorIcon();
            const FVector2D Point=FSlateApplication::Get().GetCursorPos();
            if(Weak->CharacterUI && Weak->CharacterUI->IsOpen())return Weak->CharacterUI->CursorAt(Point);
            return Weak->GameUI?Weak->GameUI->CursorAt(Point):FAC2CursorIcon();
        });
        if(MouseCursor->Ready())
        {
            Viewport->SetUseSoftwareCursorWidgets(true);
            for(auto Type:{EMouseCursor::Default,EMouseCursor::Hand,EMouseCursor::TextEditBeam,
                EMouseCursor::GrabHand,EMouseCursor::GrabHandClosed,EMouseCursor::Crosshairs})
                Viewport->SetSoftwareCursorWidget(Type,MouseCursor);
        }
        SAssignNew(GameUI, SAC2GameUI).Mode(M);
        Viewport->AddViewportWidgetContent(GameUI.ToSharedRef(), 10);
        PortalEffect=GetWorld()->SpawnActor<AAC2PortalSpace>(FVector(0,0,-20000000),FRotator::ZeroRotator);
        if(PortalEffect){PortalEffect->AddTickPrerequisiteActor(M);SAssignNew(PortalUI,SAC2PortalSpace).Effect(PortalEffect);Viewport->AddViewportWidgetContent(PortalUI.ToSharedRef(),30);}
    }

}
void AAC2HUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if(PortalUI)if(auto* V=GetWorld()->GetGameViewport())V->RemoveViewportWidgetContent(PortalUI.ToSharedRef());
    PortalUI.Reset();if(PortalEffect)PortalEffect->Destroy();PortalEffect=nullptr;
    if(auto* Viewport=GetWorld()->GetGameViewport())
        for(auto Type:{EMouseCursor::Default,EMouseCursor::Hand,EMouseCursor::TextEditBeam,
            EMouseCursor::GrabHand,EMouseCursor::GrabHandClosed,EMouseCursor::Crosshairs})
            if(Viewport->GetSoftwareCursorWidget(Type)==MouseCursor)
                Viewport->SetSoftwareCursorWidget(Type,TSharedPtr<SWidget>());
    MouseCursor.Reset();
    if (GameUI)
    {
        if (auto *Viewport = GetWorld()->GetGameViewport())
            Viewport->RemoveViewportWidgetContent(GameUI.ToSharedRef());
        GameUI.Reset();
    }
    if (CharacterUI)
    {
        if (auto *Viewport = GetWorld()->GetGameViewport())
            Viewport->RemoveViewportWidgetContent(CharacterUI.ToSharedRef());
        CharacterUI->Shutdown();
        CharacterUI.Reset();
    }
    Super::EndPlay(Reason);
}
void AAC2HUD::InitializeCharacterUI()
{
    if (CharacterUI)
        return;
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        if (auto *Viewport = GetWorld()->GetGameViewport())
        {
            SAssignNew(CharacterUI, SAC2CharacterUI).Mode(M);
            Viewport->AddViewportWidgetContent(CharacterUI.ToSharedRef(), 20);
        }
}
void AAC2HUD::DrawHUD()
{
    Super::DrawHUD();
    // Conceal the world behind the native login and character scenes.
    if (Canvas)
        if (auto *Mode = GetWorld()->GetAuthGameMode<AAC2GameMode>(); Mode && !Mode->InWorld())
            DrawRect(FLinearColor::Black, 0, 0, Canvas->SizeX, Canvas->SizeY);
}
