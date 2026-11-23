#include "AC2StoryScene.h"
#include "AC2Audio.h"
#include "AC2Explorer.h"
#include "AC2GameMode.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif
#endif

class SAC2StoryScene : public SCompoundWidget
{
    friend class FAC2MenhirPresentationTest;
public:
    SLATE_BEGIN_ARGS(SAC2StoryScene) {}
        SLATE_ARGUMENT(UWorld*, World)
        SLATE_ARGUMENT(FString, Directory)
        SLATE_ARGUMENT(TArray<FAC2StoryFrame>, Frames)
        SLATE_EVENT(FSimpleDelegate, OnDone)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        World = Args._World;
        Frames = Args._Frames;
        OnDone = Args._OnDone;
        FString Error;
        if (!Library.Open(Args._Directory, Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("AC2 story assets: %s"), *Error);
            return;
        }
        for (const FAC2StoryFrame& Frame : Frames)
        {
            if (!(Frame.Tracks & 1) || !Frame.Video || Images.Contains(Frame.Video)) continue;
            if (UTexture2D* Texture = Library.Texture(Frame.Video))
            {
                FSlateBrush& Brush = Images.Add(Frame.Video);
                Brush.SetResourceObject(Texture);
                Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            }
            else UE_LOG(LogTemp, Warning, TEXT("AC2 story picture %08X is unavailable"), Frame.Video);
        }
        // StoryField1/2 in layout 22000005 use an 800 x 600 canvas. Each has a
        // 450-pixel image area and the subtitle occupies the bottom 75 pixels.
        ChildSlot
        [
            SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor::Black)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [
                    SNew(SBox).WidthOverride(800).HeightOverride(600)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0,75,0,0)
                        [
                            SNew(SBox).HeightOverride(450)
                            [
                                SNew(SOverlay)
                                + SOverlay::Slot()[SAssignNew(OldImage,SImage).Image(nullptr)]
                                + SOverlay::Slot()[SAssignNew(NewImage,SImage).Image(nullptr)]
                            ]
                        ]
                        + SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center)
                        [
                            SAssignNew(Caption,STextBlock).Justification(ETextJustify::Center)
                            .ColorAndOpacity(FLinearColor::White).WrapTextAt(790)
                        ]
                    ]
                ]
            ]
        ];
        SetCursor(EMouseCursor::None);
        if (Ready()) Advance();
    }

    bool Ready() const { return !Frames.IsEmpty() && !Images.IsEmpty() && Caption.IsValid(); }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (Event.GetKey() == EKeys::Escape && !Event.IsRepeat()) Complete();
        return FReply::Handled();
    }
    virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

    virtual void Tick(const FGeometry& Geometry, double Now, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Now, Delta);
        if (Done || !Ready()) return;
        double Elapsed = Delta;
        while (Elapsed >= Remaining)
        {
            Elapsed -= Remaining;
            if (NextFrame == Frames.Num()) { Complete(); return; }
            Advance();
        }
        Remaining -= Elapsed;
        Fade = FMath::Min(Fade + float(Elapsed), 1.f);
        NewImage->SetRenderOpacity(Fade);
        if (Music && MusicId)
            if (auto* Audio = FAC2Audio::Get(World.Get()); Audio && Audio->Finished(Music))
                Music = Audio->Play(MusicId);
    }

    void Stop()
    {
        Done = true;
        if (auto* Audio = FAC2Audio::Get(World.Get()))
        {
            Audio->Stop(Voice, 2);
            Audio->Stop(Music, 2);
        }
        Voice = Music = 0;
    }
private:
    void Complete()
    {
        if (Done) return;
        Done = true;
        const auto Callback = OnDone;
        Callback.ExecuteIfBound();
    }
    void Advance()
    {
        const auto& Frame = Frames[NextFrame++];
        if ((Frame.Tracks & 1) && ImageId != Frame.Video)
        {
            OldImage->SetImage(Images.Find(ImageId));
            ImageId = Frame.Video;
            NewImage->SetImage(Images.Find(ImageId));
            Fade = 0;
            NewImage->SetRenderOpacity(0);
        }
        if (Frame.Tracks & 16) Caption->SetText(FText::FromString(Frame.Subtitle));
        if (auto* Audio = FAC2Audio::Get(World.Get()))
        {
            if (Frame.Tracks & 2)
            {
                Audio->Stop(Music, 2);
                MusicId = Frame.Audio;
                Music = MusicId ? Audio->Play(MusicId) : 0;
            }
            if (Frame.Tracks & 4)
            {
                Audio->Stop(Voice, 2);
                Voice = Frame.Voice ? Audio->Play(Frame.Voice) : 0;
            }
        }
        Remaining = Frame.Duration < 0 ? 5.0 : Frame.Duration;
    }
    FAC2AssetLibrary Library;
    TWeakObjectPtr<UWorld> World;
    TMap<uint32,FSlateBrush> Images;
    TArray<FAC2StoryFrame> Frames;
    TSharedPtr<SImage> OldImage, NewImage;
    TSharedPtr<STextBlock> Caption;
    FSimpleDelegate OnDone;
    int32 NextFrame = 0;
    double Remaining = 0;
    float Fade = 0;
    uint32 ImageId = 0, Voice = 0, MusicId = 0, Music = 0;
    bool Done = false;
};

void AAC2Explorer::BeginShardOrbit(TFunction<bool(FVector&, FVector&)> CameraSockets, double Duration)
{
    if (ShardScene || !CameraSockets || !GetWorld()) return;
    ShardCameraSockets = MoveTemp(CameraSockets);
    ShardCameraUntil = GetWorld()->GetTimeSeconds() + Duration;
}

bool AAC2Explorer::PlayShardScene(const TArray<FAC2StoryFrame>& Frames, TFunction<void()> OnFinished)
{
    auto* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AAC2GameMode>() : nullptr;
    auto* PC = Cast<APlayerController>(GetController());
    if (!Viewport || !Mode || !PC || !FSlateApplication::IsInitialized() || Frames.IsEmpty() || ShardScene)
        return false;

    ShardCameraSockets = nullptr;
    SAssignNew(ShardScene,SAC2StoryScene).World(GetWorld())
        .Directory(AC2::ResolveDatDirectory(Mode->DatDirectory)).Frames(Frames)
        .OnDone(FSimpleDelegate::CreateWeakLambda(this,[this]{FinishShardScene(true);}));
    if (!ShardScene->Ready()) { ShardScene.Reset(); return false; }
    ShardCompleted = MoveTemp(OnFinished);
    ShardViewport = Viewport;
    ShardController = PC;
    ShardPreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
    PC->FlushPressedKeys();
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    Viewport->AddViewportWidgetContent(ShardScene.ToSharedRef(), 45);
    FSlateApplication::Get().SetKeyboardFocus(ShardScene);
    return true;
}

void AAC2Explorer::FinishShardScene(bool Notify)
{
    if (!ShardScene) return;
    auto Widget = MoveTemp(ShardScene);
    Widget->Stop();
    const bool RestoreFocus = FSlateApplication::IsInitialized() && Widget->HasKeyboardFocus();
    if (auto* Viewport = ShardViewport.Get()) Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
    if (auto* PC = ShardController.Get())
    {
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
    }
    if (RestoreFocus)
    {
        auto Focus = ShardPreviousFocus.Pin();
        auto& App = FSlateApplication::Get();
        if (Focus && Focus->GetVisibility().IsVisible()) App.SetKeyboardFocus(Focus);
        else App.SetAllUserFocusToGameViewport();
    }
    ShardPreviousFocus.Reset();
    ShardViewport.Reset();
    ShardController.Reset();
    auto Callback = MoveTemp(ShardCompleted);
    if (Notify && Callback) Callback();
}

void AAC2Explorer::CancelShardPresentation()
{
    ShardCameraSockets = nullptr;
    FinishShardScene(false);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MenhirPresentationTest, "AC2.Data.MenhirPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2MenhirPresentationTest::RunTest(const FString&)
{
    const FString Directory = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    FAC2DatArchive Portal, English;
    FString Error;
    if (!TestTrue(TEXT("Open selected portal and English archives"),
        Portal.Open(Directory/TEXT("portal.dat"),1,Error) && English.Open(Directory/TEXT("local_English.dat"),3,Error)))
        return false;
    // The same Menhir pictures and narration have different DIDs in Icefire's DAT.
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Identify the stock or alternate build"), Portal.Read(0x410064fa,Bytes,Error) && Bytes.Num() >= 24))
        return false;
    const bool Stock = AC2::U32(Bytes.GetData()+16) == 500;
    const uint32 Black = Stock ? 0x4100497d : 0x410049e6;
    const uint32 Art = Stock ? 0x410064fa : 0x41004acd;
    const uint32 Voice = Stock ? 0x2c000311 : 0x2c000190;
    const uint32 Wave = Stock ? 0x61000033 : 0x61000021;
    TestTrue(TEXT("Narrator descriptor resolves to the native localized voice recording"),
        Portal.Read(Voice,Bytes,Error) && Bytes.Num() == 24 && AC2::U32(Bytes.GetData()+20)==Wave &&
        English.Read(Wave,Bytes,Error) && Bytes.Num() > 1000000 && FMemory::Memcmp(Bytes.GetData()+8,"RIFF",4)==0);
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor = World->SpawnActor<AActor>();
    auto* Root = NewObject<USceneComponent>(Actor);
    Actor->SetRootComponent(Root);
    Root->RegisterComponent();
    {
        FAC2AssetLibrary Shard;
        if (TestTrue(TEXT("Shard has its native activation motion"), Shard.Open(Directory,Error) &&
            Shard.DrawModel(0x1f00031c,Actor,Root) && Shard.PlayBehavior(0x410000a5)))
        {
            TestTrue(TEXT("Camera motion lasts the authored ten seconds"),
                FMath::IsNearlyEqual(Shard.BehaviorDuration(0x410000a5),10.4167f,.05f));
            FTransform Eye, Target;
            Shard.Animate(0,0);
            if (TestTrue(TEXT("Native camera eye and target sockets resolve"),
                Shard.AnimatedHoldingLocation(0x40000004,Eye) && Shard.AnimatedHoldingLocation(0x40000005,Target)))
            {
                const FVector FirstEye = Eye.GetLocation();
                const FVector FirstDirection = (Target.GetLocation()-FirstEye).GetSafeNormal();
                double MaxMotion = 0;
                double MinDirectionDot = 1;
                for (int32 Frame = 1; Frame <= 600; ++Frame)
                {
                    Shard.Animate(Frame/60.f,0);
                    Shard.AnimatedHoldingLocation(0x40000004,Eye);
                    Shard.AnimatedHoldingLocation(0x40000005,Target);
                    MaxMotion = FMath::Max(MaxMotion,FVector::Distance(FirstEye,Eye.GetLocation()));
                    MinDirectionDot = FMath::Min(MinDirectionDot,FVector::DotProduct(FirstDirection,
                        (Target.GetLocation()-Eye.GetLocation()).GetSafeNormal()));
                }
                AddInfo(FString::Printf(TEXT("Authored camera travels %.1f cm from start; minimum heading dot %.3f"),MaxMotion,MinDirectionDot));
                TestTrue(TEXT("Camera moves and turns during the activation"), MaxMotion > 100 && MinDirectionDot < .9);
            }
        }
        // First three native video, voice, subtitle and duration masks. Frame three keeps the prior subtitle.
        TArray<FAC2StoryFrame> Frames{{Black,0,Voice,21,3,TEXT("Opening")},{Art,0,0,17,3,TEXT("Menhir")},
                                     {Art+1,0,0,1,5,{}}};
        int32 Completions = 0;
        auto BuildWidget = [&] { return SNew(SAC2StoryScene).World(World).Directory(Directory).Frames(Frames)
            .OnDone(FSimpleDelegate::CreateLambda([&]{ ++Completions; })); };
        auto Scene = BuildWidget();
        if (TestTrue(TEXT("Native scene assets prepared for Slate"),Scene->Ready()))
        {
            const FGeometry Geometry;
            auto Advance = [&](float Seconds){ Scene->Tick(Geometry,0,Seconds); };
            Advance(3.25f);
            Scene->SlatePrepass(1.f);
            const auto* Picture = Scene->Images.Find(Scene->ImageId);
            const auto* Texture = Picture ? Cast<UTexture2D>(Picture->GetResourceObject()) : nullptr;
            TestTrue(TEXT("Native full-size artwork replaces the opening black image"),
                Texture && Scene->ImageId==Art && Texture->GetSizeX()==800 && Texture->GetSizeY()==450 &&
                Scene->NewImage->GetDesiredSize().Equals(FVector2D(800,450)));
            TestTrue(TEXT("New picture blends from the previous picture"),FMath::IsNearlyEqual(Scene->NewImage->GetRenderOpacity(),.25f,.001f));
#if WITH_EDITOR
            if (FParse::Param(FCommandLine::Get(), TEXT("AC2StoryRender")))
            {
                Advance(.75f);
                FWidgetRenderer Renderer(true);
                TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
                Target->InitCustomFormat(800,600,PF_B8G8R8A8,true);
                Target->UpdateResourceImmediate(true);
                Renderer.DrawWidget(Target.Get(),Scene,FVector2D(800,600),0.f);
                FlushRenderingCommands();
                TArray<FColor> Pixels;
                FReadSurfaceDataFlags Flags;
                Flags.SetLinearToGamma(false);
                if (TestTrue(TEXT("Read the cutscene as displayed by Slate"),
                    Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags) && Pixels.Num()==800*600))
                {
                    // The first Menhir illustration has a golden highlight at (60,260).
                    // Its bright red, warm green, and darker blue distinguish the actual artwork from an empty brush.
                    const FColor Highlight = Pixels[(75+260)*800+60];
                    TestTrue(TEXT("The first Menhir illustration is visible in the native frame"),
                        Highlight.R>150 && Highlight.G>90 && Highlight.B+25<Highlight.G);
                    const FColor Letterbox = Pixels[30*800+60];
                    TestTrue(TEXT("Black border above the illustration"), Letterbox.R<3 && Letterbox.G<3 && Letterbox.B<3);
                }
            }
#endif
            Advance(3);
            TestEqual(TEXT("Unchanged subtitle survives the next image"),Scene->Caption->GetText().ToString(),FString(TEXT("Menhir")));
            Advance(5);
            Advance(1);
            TestEqual(TEXT("Natural completion fires once"),Completions,1);
            Scene = BuildWidget();
            FKeyEvent Escape(EKeys::Escape,FModifierKeysState(),0,false,0,0);
            Scene->OnKeyDown(Geometry,Escape);
            Scene->OnKeyDown(Geometry,Escape);
            Advance(20);
            TestEqual(TEXT("Skip completes immediately and only once"),Completions,2);
            Scene->Stop();
        }
    }
    Actor->Destroy();
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
