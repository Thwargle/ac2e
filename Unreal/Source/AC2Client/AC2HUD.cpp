#include "AC2Explorer.h"
#include "AC2CharacterUI.h"
#include "AC2GameUI.h"
#include "Engine/GameViewportClient.h"
#include "AC2GameMode.h"
#include "AC2UILayout.h"
#include "AC2Font.h"
#include "AC2WorldGrid.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace
{
const FLinearColor Ink(.83f, .86f, .82f), Gold(.82f, .69f, .43f), Muted(.47f, .57f, .57f);
}
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
    FString Error;
    FAC2DatArchive Local;
    TArray<uint8> Bytes;
    FAC2UILayout Layout;
    FString Dir = AC2::ResolveDatDirectory(M->DatDirectory);
    UIAssets = MakeUnique<FAC2AssetLibrary>();
    Fonts = MakeShared<FAC2Fonts>();
    Fonts->Open(Dir, Error);
    if (!UIAssets->Open(Dir, Error) ||
        !Local.Open(FPaths::Combine(Dir, TEXT("local_English.dat")), 3, Error) ||
        !Local.Read(0x22000001, Bytes, Error) || !Layout.Decode(0x22000001, Bytes))
        return;
    // Identities verified against the in-game 7160 LayoutDesc, decoded directly from Surface records.
    const uint32 Images[] = {0x41006321, 0x4100631b, 0x4100631c, 0x410023d6, 0x410023d4,
                             0x410025e7, 0x41000ceb, 0x410002e0, 0x410009cf, 0x410009cd,
                             0x410009d1, 0x410027dc, 0x410002b9, 0x41000adf};
    for (uint32 Id : Images)
        if (Layout.Art.ContainsByPredicate([&](const auto &A) { return A.Image == Id; }))
            if (auto *T = UIAssets->Texture(Id))
                Art.Add(Id, T);
    ArtReady = Art.Num() == UE_ARRAY_COUNT(Images);
    UE_LOG(LogTemp, Display, TEXT("AC2 native HUD: %d elements / %d media / %d selected images, ready=%d"),
           Layout.Elements, Layout.Art.Num(), Art.Num(), ArtReady);
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
    Art.Empty();
    UIAssets.Reset();
    Fonts.Reset();
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
void AAC2HUD::Text(const FString &Value, float X, float Y, FLinearColor Color, float Size)
{
    if (Fonts)
        if (auto Font = Fonts->Get(Size < .95f ? 0x4200001b : 0x4200000c))
        {
            Font->DrawHUD(*this, Value, FVector2D(X + 1, Y + 1) * UIScale, FLinearColor(0, 0, 0, Color.A),
                          UIScale);
            Font->DrawHUD(*this, Value, FVector2D(X, Y) * UIScale, Color, UIScale);
        }
}
void AAC2HUD::Rect(FLinearColor C, float X, float Y, float W, float H)
{
    DrawRect(C, X * UIScale, Y * UIScale, W * UIScale, H * UIScale);
}
void AAC2HUD::Picture(uint32 Id, float X, float Y, float W, float H, FLinearColor Tint)
{
    if (auto **T = Art.Find(Id))
        DrawTexture(*T, X * UIScale, Y * UIScale, W * UIScale, H * UIScale, 0, 0, 1, 1, Tint,
                    BLEND_Translucent);
}
void AAC2HUD::Panel(const FString &Title, float X, float Y, float W, float H)
{
    Rect(FLinearColor(.03f, .035f, .04f, .94f), X, Y, W, H);
    Rect(FLinearColor(.37f, .31f, .19f, .9f), X, Y, W, 1);
    Rect(FLinearColor(.37f, .31f, .19f, .9f), X, Y + H, W, 1);
    Rect(FLinearColor(.37f, .31f, .19f, .9f), X, Y, 1, H);
    Rect(FLinearColor(.37f, .31f, .19f, .9f), X + W, Y, 1, H);
    Picture(0x41006321, X + 10, Y + 6, W - 20, 26, FLinearColor(.7f, .7f, .65f));
    Rect(FLinearColor(.015f, .02f, .02f, .7f), X + 25, Y + 10, W - 50, 19);
    Text(Title, X + 32, Y + 12, Ink);
}
void AAC2HUD::Button(FName Action, const FString &Label, float X, float Y, float W, float H, uint32 Icon)
{
    const bool Over = Hover == Action;
    Rect(Over ? FLinearColor(.19f, .21f, .19f, .95f) : FLinearColor(.07f, .09f, .09f, .95f), X, Y, W, H);
    Rect(Over ? Gold : FLinearColor(.3f, .28f, .2f), X, Y + H - 1, W, 1);
    if (Icon)
        Picture(Icon, X + 7, Y + 5, H - 10, H - 10);
    Text(Label, X + (Icon ? H : 10), Y + H / 2 - 6, Over ? FLinearColor::White : Ink);
    AddHitBox(FVector2D(X, Y) * UIScale, FVector2D(W, H) * UIScale, Action, true, 1);
}
void AAC2HUD::NotifyHitBoxClick(FName Name)
{
    if (Name == TEXT("Close"))
    {
        if (auto *PC = Cast<AAC2PlayerController>(PlayerOwner))
            PC->CloseUI();
        return;
    }
    if (auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>())
        M->UIAction(Name);
}
void AAC2HUD::NotifyHitBoxBeginCursorOver(FName Name)
{
    Hover = Name;
}
void AAC2HUD::NotifyHitBoxEndCursorOver(FName Name)
{
    if (Hover == Name)
        Hover = NAME_None;
}
void AAC2HUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner)
        return;
    auto *M = GetWorld()->GetAuthGameMode<AAC2GameMode>();
    auto *P = Cast<AAC2Explorer>(PlayerOwner->GetPawn());
    if (!M || !P)
        return;
    if(!M->InWorld())
    {
        DrawRect(FLinearColor::Black,0,0,Canvas->SizeX,Canvas->SizeY);
        return;
    }
    if (CharacterUI && CharacterUI->IsOpen())
        return;
    UIScale = FMath::Max(.5f, FMath::Min(Canvas->SizeX / 1600.f, Canvas->SizeY / 900.f));
    float W = Canvas->SizeX / UIScale, H = Canvas->SizeY / UIScale;
    if (P->IsSwimming())
        Rect(FLinearColor(.03f, .2f, .28f, .12f), 0, 0, W, H);
    uint32 Block = 0;
    AC2::WorldBlock(P->GetActorLocation(), Block);
    if (M->bWorldPanel || M->bFXPanel)
    {
        float X = W - 386, Y = 140;
        Panel(M->bWorldPanel ? TEXT("WORLD & WEATHER") : TEXT("NATIVE EFFECTS"), X, Y, 366,
              M->bWorldPanel ? 390 : 342);
        Button(TEXT("Close"), TEXT("x"), X + 326, Y + 7, 28, 23);
        if (M->bWorldPanel)
        {
            Text(M->WeatherStatus(), X + 18, Y + 49, Ink);
            Button(TEXT("Earlier"), TEXT("- 2 hours"), X + 16, Y + 96, 106, 32);
            Button(TEXT("Day"), TEXT("Next sky"), X + 130, Y + 96, 106, 32);
            Button(TEXT("Later"), TEXT("+ 2 hours"), X + 244, Y + 96, 106, 32);
            Button(TEXT("NativeRain"), TEXT("Native rain"), X + 16, Y + 138, 106, 32);
            Button(TEXT("Dry"), TEXT("Clear"), X + 130, Y + 138, 106, 32);
            Button(TEXT("Rain"), TEXT("Heavy rain"), X + 244, Y + 138, 106, 32);
            Text(TEXT("Travel"), X + 18, Y + 191, Gold, 1.1f);
            Button(TEXT("Mountains"), TEXT("Mountains"), X + 16, Y + 218, 160, 34);
            Button(TEXT("Campfire"), TEXT("Campfire"), X + 190, Y + 218, 160, 34);
            Button(TEXT("Coast"), TEXT("Coast"), X + 16, Y + 262, 160, 34);
            Button(TEXT("Dungeon"), TEXT("Dungeon"), X + 190, Y + 262, 160, 34);
            Button(TEXT("SpawnWorld"), TEXT("Spawn test ") + M->NextCreatureName(), X + 16, Y + 306, 334, 34);
            Text(TEXT("Local previews / 180 m creature draw distance"), X + 18, Y + 362, Muted, .9f);
        }
        else
        {
            Picture(0x410002e0, X + 150, Y + 50, 66, 66);
            Text(FString::Printf(TEXT("PSDesc %08X"), M->PreviewEffect), X + 108, Y + 129, Gold, 1.2f);
            Button(TEXT("PrevFX"), TEXT("Previous"), X + 16, Y + 165, 106, 34);
            Button(TEXT("PreviewCast"), TEXT("Play effect"), X + 130, Y + 165, 106, 34);
            Button(TEXT("NextFX"), TEXT("Next"), X + 244, Y + 165, 106, 34);
            Text(TEXT("All authored emitters run together.\nScenery effects start automatically nearby.\n\n2 "
                      "plays the "
                      "selected effect in the world.\nPreview emission ends within 15 seconds."),
                 X + 18, Y + 223, Ink);
        }
    }
    if (M->bHelp)
    {
        float X = W / 2 - 345, Y = 140;
        Panel(TEXT("EXPLORER CONTROLS"), X, Y, 690, 390);
        Text(
            TEXT("WASD move   Mouse look   Shift run   Space jump\nF walk / fly   Q E descend / ascend   Tab "
                 "camera\nAlt toggle cursor   Esc close panels   M world map\nEnter local chat   /help chat "
                 "commands\nI inventory   C character stats   F4 characters\nT select NPC   1 strike   2 "
                 "effect   R "
                 "spawn local NPC\nF2 world/weather panel   F3 native effect browser\n~ console: AC2Block "
                 "5667 / "
                 "AC2Cell 59600110 / AC2Time 18"),
            X + 20, Y + 48, Ink);
        Text(M->Status, X + 20, Y + 235, FLinearColor(.5f, .75f, .6f), .94f);
        Text(ArtReady ? TEXT("Native HUD art loaded from the 7160 in-game layout")
                      : TEXT("Native UI art unavailable: basic controls remain active"),
             X + 20, Y + 361, Muted, .9f);
    }
}
