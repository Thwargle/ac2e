#include "AC2CharacterUI.h"
#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Explorer.h"
#include "AC2Portrait.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"

namespace
{

}
void SAC2CharacterUI::Construct(const FArguments &Args)
{
    Mode = Args._Mode;
    Assets = MakeUnique<FAC2AssetLibrary>();
    FString Error;
    Assets->Open(Mode->DatDirectory, Error);
    Fonts.Open(Mode->DatDirectory, Error);
    CreatorLayout.Open(Mode->DatDirectory, Error);
    FAC2DatArchive Local;
    TArray<uint8> Bytes;
    if (Local.Open(FPaths::Combine(Mode->DatDirectory, TEXT("local_English.dat")), 3, Error))
    {
        if (Local.Read(0x22000006, Bytes, Error))
            ChooseLayout.Decode(0x22000006, Bytes);
        if (Local.Read(0x22000007, Bytes, Error))
            ModifyLayout.Decode(0x22000007, Bytes);
        if (Local.Read(0x22000008, Bytes, Error))
            ConfirmLayout.Decode(0x22000008, Bytes);
    }
    ChildSlot[SNew(SBorder)
                  .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                  .BorderBackgroundColor_Lambda(
                      [this]() { return IsCreation() ? FLinearColor::Black : FLinearColor::Transparent; })
                  .Padding_Lambda([this]() {
                      return IsCreation() ? FMargin(0)
                                          : FMargin(0, GetCachedGeometry().GetLocalSize().Y * .08f);
                  })[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SAssignNew(PageSize, SBox)
                                                                       .WidthOverride(1100)
                                                                       .HeightOverride(
                                                                           720)[SAssignNew(Content, SBox)]]]];
    SetVisibility(EVisibility::Collapsed);
}
SAC2CharacterUI::~SAC2CharacterUI()
{
    Shutdown();
}
void SAC2CharacterUI::Shutdown()
{
    if (Portrait.IsValid())
        Portrait->Destroy();
    Portrait.Reset();
    if (Selection.IsValid()) Selection->Destroy();
    Selection.Reset();
    SelectionBrush.SetResourceObject(nullptr);
    if (Lineup.IsValid())
        Lineup->Destroy();
    Lineup.Reset();
    LineupBrush.SetResourceObject(nullptr);
    PortraitBrush.SetResourceObject(nullptr);
    Brushes.Reset();
    Assets.Reset();
}
const FSlateBrush *SAC2CharacterUI::Art(uint32 Id)
{
    if (auto *B = Brushes.Find(Id))
        return B->Get();
    if (Assets)
        if (auto *Texture = Assets->Texture(Id))
        {
            auto Brush = MakeShared<FSlateBrush>();
            Brush->SetResourceObject(Texture);
            Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            Brush->DrawAs = ESlateBrushDrawType::Image;
            Brushes.Add(Id, Brush);
            return &Brush.Get();
        }
    return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
}
void SAC2CharacterUI::Open(EScreen Next,const FString& InitialMessage)
{
    if (!Mode.IsValid())
        return;
    if ((Next == EScreen::Inventory || Next == EScreen::Stats) && Mode->Characters.Active())
    {
        Close();
        if (auto *PC = Mode->GetWorld()->GetFirstPlayerController())
            if (auto *HUD = Cast<AAC2HUD>(PC->GetHUD()); HUD && HUD->GameUI)
            {
                if (Next == EScreen::Inventory)
                    HUD->GameUI->ToggleInventory();
                else
                    HUD->GameUI->ToggleStats();
            }
        return;
    }
    if (auto *PC = Mode->GetWorld()->GetFirstPlayerController())
        if (auto *HUD = Cast<AAC2HUD>(PC->GetHUD()))
            if (HUD->GameUI)
                HUD->GameUI->EndChat();
    if (!Mode->Characters.Active() && (Next == EScreen::Inventory || Next == EScreen::Stats))
        Next = EScreen::Select;
    if (Lineup.IsValid())
    {
        Lineup->SetActorTickEnabled(false);
        Lineup->Capture->bCaptureEveryFrame = false;
    }
    Screen = Next;
    if (Portrait.IsValid() && Next!=EScreen::Create)
    {Portrait->SetActorTickEnabled(false);Portrait->Capture->bCaptureEveryFrame=false;}
    ShowSelectionOptions = false;
    DeletePending.Empty();
    CreatorLayout.Hovered = CreatorLayout.Pressed = 0;
    if (Selection.IsValid()) { Selection->Destroy(); Selection.Reset(); }
    if(Next==EScreen::Select)
    {
        const int32 Active=Mode->Characters.Characters.IndexOfByPredicate([this](const auto& C){return C.Id==Mode->Characters.ActiveId;});
        Page=FMath::Clamp(Active<0?0:Active/5*5,0,FMath::Max(0,Mode->Characters.Characters.Num()-5));
    }
    Message = InitialMessage.IsEmpty()?Mode->CharacterError:InitialMessage;
    if (Next == EScreen::Create)
    {
        DraftName.Empty();
        CreationStep = 0;
        AppearanceTab = 1;
        HeadZoom = false;
        Rotation = 0;
        ResetAppearance();
    }
    else if (auto *P = Mode->Characters.Active())
    {
        Appearance = P->Appearance;
        Race = P->Race;
        Sex = P->Sex;
        const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
        if (A)
        {
            HeightIndex =
                A->Heights.IndexOfByPredicate([&](float V) { return FMath::IsNearlyEqual(V, P->Height); });
            BuildIndex =
                A->Builds.IndexOfByPredicate([&](float V) { return FMath::IsNearlyEqual(V, P->Build); });
        }
    }
    if(Next!=EScreen::Login)PreviewDraft();
    SetVisibility(EVisibility::Visible);
    Refresh();
    if (auto *PC = Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))
    {
        PC->SetCursor(true);
        PC->ResetIgnoreMoveInput();
        PC->SetIgnoreMoveInput(true);
        PC->FlushPressedKeys();
        if (auto *P = Cast<AAC2Explorer>(PC->GetPawn()))
            P->GetCharacterMovement()->StopMovementImmediately();
        FInputModeUIOnly Input;
        Input.SetWidgetToFocus(AsShared());
        Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Input);
    }
    FSlateApplication::Get().SetKeyboardFocus(AsShared());
}
void SAC2CharacterUI::Close()
{
    if(Screen==EScreen::Login)return;
    if(Mode.IsValid() && Mode->Network && !Mode->Network->IsWorld()) return;
    if (!Mode.IsValid() || !Mode->Characters.Active())
        return;
    if (IsCreation())
    {
        Enter();
        return;
    }
    if (Lineup.IsValid())
    {
        Lineup->SetActorTickEnabled(false);
        Lineup->Capture->bCaptureEveryFrame = false;
    }
    Screen = EScreen::Closed;
    SetVisibility(EVisibility::Collapsed);
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(false);
        Portrait->Capture->bCaptureEveryFrame = false;
    }
    if (Selection.IsValid()) { Selection->Destroy(); Selection.Reset(); }
    if (auto *PC = Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))
    {
        PC->ResetIgnoreMoveInput();
        PC->SetCursor(false);
    }
}
FReply SAC2CharacterUI::OnKeyDown(const FGeometry &, const FKeyEvent &Event)
{
    if (Screen==EScreen::Credits) {Screen=EScreen::Select;Refresh();return FReply::Handled();}
    if (Event.GetKey() == EKeys::Escape)
    {
        if (Screen == EScreen::Select && (ShowSelectionOptions || !DeletePending.IsEmpty()))
        {ShowSelectionOptions=false; DeletePending.Empty(); Message.Empty(); Refresh(); return FReply::Handled();}
        if (Screen == EScreen::Create && CreationStep > 0)
            SetCreatorStep(CreationStep - 1);
        else if (Screen == EScreen::Create)
            Open(EScreen::Select);
        else if(Screen==EScreen::Select)
            Mode->SignOut();
        return FReply::Handled();
    }
    if (Screen == EScreen::Select && !ShowSelectionOptions)
    {
        if (Event.GetKey() == EKeys::Enter && DeletePending.IsEmpty()) { Enter(); return FReply::Handled(); }
        if (Event.GetKey() == EKeys::Left || Event.GetKey() == EKeys::Right || Event.GetKey() == EKeys::Up || Event.GetKey() == EKeys::Down)
        {
            const int32 N=Mode->Characters.Characters.Num();
            const int32 Current=Mode->Characters.Characters.IndexOfByPredicate([this](const auto& C){return C.Id==Mode->Characters.ActiveId;});
            if (N) SelectCharacter((Current + (Event.GetKey()==EKeys::Left || Event.GetKey()==EKeys::Up ? N-1 : 1)) % N);
            return FReply::Handled();
        }
    }
    if (!IsCreation())
    {
        if (Event.GetKey() == EKeys::I)
        {
            Screen == EScreen::Inventory ? Close() : Open(EScreen::Inventory);
            return FReply::Handled();
        }
        if (Event.GetKey() == EKeys::C)
        {
            Screen == EScreen::Stats ? Close() : Open(EScreen::Stats);
            return FReply::Handled();
        }
    }
    return FReply::Unhandled();
}
void SAC2CharacterUI::ResetAppearance()
{
    if (const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex))
    {
        auto Nearest = [](const TArray<float> &V) {
            int32 Best = 0;
            for (int32 I = 1; I < V.Num(); ++I)
                if (FMath::Abs(V[I] - 1) < FMath::Abs(V[Best] - 1))
                    Best = I;
            return Best;
        };
        Appearance = A->Defaults();
        HeightIndex = Nearest(A->Heights);
        BuildIndex = Nearest(A->Builds);
    }
}
void SAC2CharacterUI::PreviewDraft()
{
    if (Screen == EScreen::Select)
    {
        TArray<FAC2SelectionFigure> Figures;
        int32 Selected=0;
        for (const auto &P:Mode->Characters.Characters)
        {
            const auto *A=Mode->CharacterCatalog.Avatar(P.Race,P.Sex);
            if (!A) { Message=TEXT("Character appearance is unavailable."); return; }
            if (P.Id==Mode->Characters.ActiveId) Selected=Figures.Num();
            FAC2SelectionFigure F;
            F.Physics=Mode->Network ? Mode->Network->Visual(P.Id) : A->Physics;
            F.Height=P.Height; F.Build=P.Build;
            F.Appearance=Mode->Network ? Mode->Network->Appearance(P.Id) : A->Resolve(P.Appearance,P.ClothingMask(Mode->CharacterCatalog));
            Figures.Add(MoveTemp(F));
        }
        Selection=Mode->GetWorld()->SpawnActor<AAC2Portrait>(FVector(0,0,-700000),FRotator::ZeroRotator);
        if (!Selection.IsValid() || !Selection->ShowSelection(Mode->DatDirectory,Figures,Selected,Mode->Network?Mode->Network->AllowedCharacters:8))
            Message=TEXT("Native character selection scene could not be loaded.");
        if (Selection.IsValid())
        {
            SelectionBrush.SetResourceObject(Selection->Image);
            SelectionBrush.ImageSize=FVector2D(706,768);
            SelectionBrush.DrawAs=ESlateBrushDrawType::Image;
        }
        return;
    }
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A)
        return;
    HeightIndex = FMath::Clamp(HeightIndex, 0, A->Heights.Num() - 1);
    BuildIndex = FMath::Clamp(BuildIndex, 0, A->Builds.Num() - 1);
    if (!Portrait.IsValid())
        Portrait = Mode->GetWorld()->SpawnActor<AAC2Portrait>(FVector(0, 0, -500000), FRotator::ZeroRotator);
    if (Portrait.IsValid())
    {
        Portrait->SetActorTickEnabled(true);
        Portrait->Capture->bCaptureEveryFrame = true;
        const auto* Selected=Mode->Characters.Active();
        const bool ServerPreview=Mode->Network && Selected && Screen==EScreen::Select;
        if (!Portrait->Show(
                Mode->DatDirectory, ServerPreview ? Mode->Network->Visual(Selected->Id) : A->Physics,
                ServerPreview ? Selected->Height : A->Heights[HeightIndex], ServerPreview ? Selected->Build : A->Builds[BuildIndex],
                ServerPreview ? Mode->Network->Appearance(Selected->Id) : A->Resolve(Appearance, Screen == EScreen::Create || !Mode->Characters.Active()
                                           ? 0x700
                                           : Mode->Characters.Active()->ClothingMask(Mode->CharacterCatalog)),
                Screen == EScreen::Create &&
                        ModifyLayout.Find(FAC2CharacterCatalog::RaceName(Race).ToLower() + TEXT("Viewport"))
                    ? ModifyLayout.Find(FAC2CharacterCatalog::RaceName(Race).ToLower() + TEXT("Viewport"))
                          ->Scene
                : Screen == EScreen::Select ? 0x21000001
                                            : 0))
            Message = TEXT("This native model could not be loaded.");
        Portrait->SetCamera(Screen == EScreen::Create && CreationStep == 1 && HeadZoom, Rotation);
        PortraitBrush.SetResourceObject(Portrait->Image);
        PortraitBrush.ImageSize = FVector2D(640, 674);
        PortraitBrush.DrawAs = ESlateBrushDrawType::Image;
    }
}
bool SAC2CharacterUI::PreviewReady() const
{
    if (Screen == EScreen::Select) return Selection.IsValid() && Selection->Ready;
    return Portrait.IsValid() && Portrait->Ready;
}
void SAC2CharacterUI::SelectCharacter(int32 Index)
{
    if (!Mode->Characters.Characters.IsValidIndex(Index)) return;
    Mode->Characters.ActiveId=Mode->Characters.Characters[Index].Id;
    if (Index<Page) Page=Index;
    if (Index>=Page+5) Page=Index-4;
    DeletePending.Empty(); Message.Empty();
    if (Selection.IsValid()) Selection->SelectCharacter(Index);
}
void SAC2CharacterUI::Refresh()
{
    if (!Mode.IsValid())
        return;
    PageSize->SetWidthOverride(1024);
    PageSize->SetHeightOverride(768);
    if(Screen==EScreen::Login){Content->SetContent(Login());return;}
    if (Screen == EScreen::Create)
    {
        Content->SetContent(Creation());
        return;
    }
    Content->SetContent(Creation());
}
bool SAC2CharacterUI::Persist(const FAC2CharacterStore &Previous)
{
    if(Mode->Network) return false;
    if (!Mode->CharacterDataReady || !Mode->Characters.Save(Mode->CharacterSavePath, Message))
    {
        Mode->Characters = Previous;
        return false;
    }
    return true;
}
void SAC2CharacterUI::Create()
{
    const auto *A = Mode->CharacterCatalog.Avatar(Race, Sex);
    if (!A || !Mode->CharacterDataReady)
        return;
    if(Mode->Network) {Mode->Network->Create(DraftName,*A,A->Heights[HeightIndex],A->Builds[BuildIndex],Appearance);return;}
    auto Previous = Mode->Characters;
    if (!Mode->Characters.Create(DraftName, Race, Sex, A->Heights[HeightIndex], A->Builds[BuildIndex],
                                 Mode->CharacterCatalog, Message, &Appearance) ||
        !Persist(Previous))
    {
        Refresh();
        return;
    }
    Enter();
}
void SAC2CharacterUI::Enter()
{
    auto *P = Mode->Characters.Active();
    if (!P || !Mode->CharacterDataReady)
        return;
    if(Mode->Network) {Mode->Network->Enter(P->Id);return;}
    const auto *A = Mode->CharacterCatalog.Avatar(P->Race, P->Sex);
    auto *PC = Mode->GetWorld()->GetFirstPlayerController();
    auto *Pawn = PC ? Cast<AAC2Explorer>(PC->GetPawn()) : nullptr;
    if (!A || !Pawn ||
        !Pawn->ApplyAvatar(A->Physics, P->Height, P->Build,
                           A->Resolve(P->Appearance, P->ClothingMask(Mode->CharacterCatalog))))
    {
        Message = TEXT("The character model could not be loaded.");
        Refresh();
        return;
    }
    if (!Mode->EnterOfflineCharacter())
    {
        Message = TEXT("No clear space for this character here. Choose a smaller body or return after "
                       "traveling to an open area.");
        Refresh();
        return;
    }
    if (!Mode->Characters.Save(Mode->CharacterSavePath, Message))
    {
        Refresh();
        return;
    }
    Mode->Notice = TEXT("Welcome, ") + P->Name;
    if(!Mode->InWorld())return; // Opaque selection remains until placement and streaming finish.
    Screen = EScreen::Stats;
    Close();
}
bool SAC2CharacterUI::SmokeCreate(FString &Error)
{
    DraftName = TEXT("UI Adventurer");
    Create();
    if ((IsOpen() && Mode->InWorld()) || !Mode->Characters.Active())
    {
        Error = Message;
        return false;
    }
    return true;
}
bool SAC2CharacterUI::SmokePreview(int32 Index)
{
    if (!Mode->CharacterCatalog.Avatars.IsValidIndex(Index))
        return false;
    const auto &A = Mode->CharacterCatalog.Avatars[Index];
    Race = A.Race;
    Sex = A.Sex;
    ResetAppearance();
    PreviewDraft();
    if (!PreviewReady())
        return false;
    const FTransform Camera = Portrait->Capture->GetRelativeTransform();
    // Exercise actual rendered geometry/material changes for each race and sex, not just stored indices.
    for (auto &Choice : Appearance)
        Choice.Value = A.Choices[Choice.Key].Num() - 1;
    TArray<uint32> Types;
    A.Choices.GetKeys(Types);
    Types.Sort();
    for (uint32 Type : Types)
    {
        int32 Count = A.Choices[Type].Num();
        if (Count < 2)
            continue;
        Appearance[Type] = 0;
        PreviewDraft();
        uint32 Before = Portrait->Fingerprint();
        Appearance[Type] = Count - 1;
        PreviewDraft();
        if (!PreviewReady() || Before == Portrait->Fingerprint() ||
            !Portrait->Capture->GetRelativeTransform().Equals(Camera))
        {
            UE_LOG(LogTemp, Error, TEXT("AC2 avatar appearance unchanged: race=%u sex=%u type=%u"), Race, Sex,
                   Type);
            return false;
        }
    }
    HeightIndex = A.Heights.Num() - 1;
    BuildIndex = A.Builds.Num() - 1;
    PreviewDraft();
    if (!PreviewReady() || !Portrait->Capture->GetRelativeTransform().Equals(Camera) ||
        !FMath::IsNearlyEqual(Portrait->Body->GetRelativeScale3D().Z, double(A.Heights.Last())) ||
        !FMath::IsNearlyEqual(Portrait->Body->GetRelativeScale3D().X, double(A.Builds.Last())))
        return false;
    ResetAppearance();
    PreviewDraft();
    Refresh();
    return PreviewReady();
}
