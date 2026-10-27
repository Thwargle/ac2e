#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
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
        SAssignNew(GameUI, SAC2GameUI).Mode(M);
        Viewport->AddViewportWidgetContent(GameUI.ToSharedRef(), 10);
    }

}
void AAC2HUD::EndPlay(const EEndPlayReason::Type Reason)
{
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
