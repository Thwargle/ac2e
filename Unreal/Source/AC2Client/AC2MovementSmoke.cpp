#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"

bool SAC2GameUI::SmokeMovementInput(int32 Step,FString& Error)
{
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!P)return Fail(TEXT("Missing movement pawn"));
    auto& App=FSlateApplication::Get();
    auto Key=[&](FKey K,bool Down){PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,1.f,false,FPlatformTime::Cycles64()));};
    auto Mouse4=[&](){
        const auto Window=App.FindWidgetWindow(SharedThis(this));
        if(!Window)return false;
        const auto Point=GetCachedGeometry().LocalToAbsolute(FVector2D(Size.X*.5,Size.Y*.35)*Scale);
        auto Path=App.LocateWindowUnderMouse(Point,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return false;
        const TSet<FKey> None,Down={EKeys::ThumbMouseButton};
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Point,Point,Down,EKeys::ThumbMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,Point,Point,None,EKeys::ThumbMouseButton,0,FModifierKeysState()));
        return true;
    };
    switch(Step)
    {
    case 0:
        PC->FlushPressedKeys();PC->SetCursor(true);App.SetAllUserFocusToGameViewport();
        if(!P->IsRunning() || P->IsAutoRunning())return Fail(TEXT("Default movement is not manual running"));
        Key(EKeys::LeftShift,true);break;
    case 1:
        if(P->IsRunning())return Fail(TEXT("Shift did not switch to walking"));
        Key(EKeys::RightShift,true);Key(EKeys::LeftShift,false);break;
    case 2:
        if(P->IsRunning())return Fail(TEXT("Releasing one Shift while holding the other stopped walking"));
        Key(EKeys::RightShift,false);break;
    case 3:
        if(!P->IsRunning())return Fail(TEXT("Releasing Shift did not restore running"));
        if(!Mouse4())return Fail(TEXT("Could not route Mouse4 to visible-pointer UI"));
        UE_LOG(LogTemp,Display,TEXT("AC2 Mouse4 routed: auto=%d held=%d ignored=%d"),P->IsAutoRunning(),P->IsHeld(),PC->IsMoveInputIgnored());
        break;
    case 4:
        // Authored trees may block this test's path after the earlier movement
        // stages. Verify sustained input, independently of scenery collision.
        UE_LOG(LogTemp,Display,TEXT("AC2 autorun: auto=%d input=%s velocity=%s"),P->IsAutoRunning(),*P->GetLastMovementInputVector().ToString(),*P->GetVelocity().ToString());
        if(!P->IsAutoRunning() || P->GetLastMovementInputVector().Size2D()<.5)return Fail(TEXT("Mouse4 did not start sustained movement input"));
        if(!Mouse4())return false;break;
    case 5:
        if(P->IsAutoRunning())return Fail(TEXT("Second Mouse4 did not stop autorun"));
        PC->SetCursor(false);Key(EKeys::ThumbMouseButton,true);Key(EKeys::ThumbMouseButton,false);break;
    case 6:
        if(!P->IsAutoRunning())return Fail(TEXT("Viewport Mouse4 binding did not start autorun"));
        Key(EKeys::S,true);break;
    case 7:
        if(P->IsAutoRunning())return Fail(TEXT("Backward input failed to cancel autorun"));
        Key(EKeys::S,false);P->ToggleAutoRun();BeginChat();break;
    case 8:
        if(P->IsAutoRunning())return Fail(TEXT("Chat left autorun active"));
        EndChat();P->ToggleAutoRun();PC->FlushPressedKeys();break;
    case 9:
        if(P->IsAutoRunning() || !P->IsRunning())return Fail(TEXT("Focus-loss key flush left movement latched"));
        P->ToggleAutoRun();P->SetHeld(true);break;
    case 10:
        if(P->IsAutoRunning())return Fail(TEXT("Travel did not cancel autorun"));
        P->ToggleAutoRun();if(P->IsAutoRunning())return Fail(TEXT("Held player could start autorun"));
        P->SetHeld(false);P->ToggleAutoRun();P->SetDead(true);break;
    case 11:
        if(P->IsAutoRunning())return Fail(TEXT("Death did not cancel autorun"));
        P->SetDead(false);PC->SetCursor(true);break;
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 movement input step %d PASS"),Step);
    return true;
}
