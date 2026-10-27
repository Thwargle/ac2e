#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

bool SAC2GameUI::SmokeMouseControls(int32 Step,const FString& Id,FString& Error)
{
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    auto* Pawn=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(!Pawn || !Mode->Network)return false;
    auto Fail=[&](const TCHAR* Text){Error=Text;return false;};
    auto& App=FSlateApplication::Get();
    static float OriginalYaw=0;
    static FRotator OriginalView;
    static FVector2D Pointer;
    auto Middle=[&]() {
        const auto Window=App.FindWidgetWindow(SharedThis(this));
        if(!Window)return false;
        const auto Path=App.LocateWindowUnderMouse(Pointer,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Pointer,Pointer,{EKeys::MiddleMouseButton},EKeys::MiddleMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,Pointer,Pointer,{},EKeys::MiddleMouseButton,0,FModifierKeysState()));
        return true;
    };
    switch(Step)
    {
    case 0:
        if(!Mode->GetWorld()->GetGameViewport()->HasSoftwareCursor(EMouseCursor::Default))return Fail(TEXT("DAT software cursor not installed"));
        if(!SmokeWorldTarget(Id,Error,EKeys::RightMouseButton))return false;
        if(!ShowStats || InspectedObject!=Id || !Mode->Network->ExaminationPending)return Fail(TEXT("Right click did not request native examination"));
        break;
    case 1:
        if(Mode->Network->ExaminationPending || !Mode->Network->ExaminationError.IsEmpty() ||
            Mode->Network->ExaminationRows.IsEmpty() || Mode->Network->ExaminationId!=Id)
            return Fail(TEXT("Native server examination profile did not reach the panel"));
        Rebuild();
        if(!Native.Nodes.ContainsByPredicate([&](const auto& N){return N.Text==Mode->Network->ExaminationRows[0].Text;}))
            return Fail(TEXT("Server examination name missing from native text nodes"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Mouse-inspect.png"),true,false);
        break;
    case 2:
        ClosePanels();
        if(!SmokeWorldTarget(Id,Error,EKeys::LeftMouseButton,true))return false;
        if(ShowStats)return Fail(TEXT("Double click unexpectedly opened examination"));
        Pointer=App.GetCursorPos();
        break;
    case 3:
    {
        if(Mode->InCombat())return Fail(TEXT("Double-click use unexpectedly started an attack"));
        const float Distance=Pawn->Boom->TargetArmLength;
        OnMouseWheel(GetCachedGeometry(),FPointerEvent(0,Pointer,Pointer,{},FKey(),1,FModifierKeysState()));
        if(Pawn->Boom->TargetArmLength!=Distance-50)return Fail(TEXT("World wheel did not zoom camera"));
        const auto Panel=Native.Bounds(TEXT("ChatWindow1"));
        const auto OverChat=GetCachedGeometry().LocalToAbsolute(FVector2D(Panel.Min+FIntPoint(15,15))*Scale);
        OnMouseWheel(GetCachedGeometry(),FPointerEvent(0,OverChat,OverChat,{},FKey(),1,FModifierKeysState()));
        if(Pawn->Boom->TargetArmLength!=Distance-50)return Fail(TEXT("Chat scrolling leaked into camera zoom"));
        Pawn->SetCameraDistance(Distance);
        OriginalYaw=Pawn->GetActorRotation().Yaw;OriginalView=PC->GetControlRotation();
        if(!Middle() || PC->bShowMouseCursor || PC->IsLookInputIgnored())return Fail(TEXT("Middle click did not enter mouse look"));
        Rebuild();
        if(!Native.Find(TEXT("MouseLookCursorField")))return Fail(TEXT("Native camera crosshair missing in mouse look"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Mouse-look.png"),true,false);
        Pawn->AddControllerYawInput(12);
        break;
    }
    case 4:
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalYaw,Pawn->GetActorRotation().Yaw))>.5f)
            return Fail(TEXT("Stationary camera orbit changed the network character heading"));
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalView.Yaw,PC->GetControlRotation().Yaw))<1)
            return Fail(TEXT("Mouse look did not turn the camera"));
        if(!Middle())return Fail(TEXT("Middle-click exit could not be routed"));
        break;
    case 5:
        if(!PC->bShowMouseCursor || !PC->IsLookInputIgnored())return Fail(TEXT("Mouse-look exit did not restore pointer input"));
        if(FVector2D::Distance(Pointer,App.GetCursorPos())>3)return Fail(TEXT("Mouse-look exit lost the saved pointer position"));
        PC->SetControlRotation(OriginalView);
        Pawn->ZoomCamera(100);
        if(Pawn->Boom->TargetArmLength!=0)return Fail(TEXT("Wheel zoom failed to enter first person"));
        // Aim at an actual rendered triangle, then route right-click through the viewport
        // with the pointer captured. This must inspect through the native center crosshair.
        Pawn->Boom->TickComponent(0,LEVELTICK_All,nullptr);
        PC->Cursor();
        if(!SmokeWorldTarget(Id,Error,EKeys::RightMouseButton,false,true))return false;
        if(!ShowStats || InspectedObject!=Id || !Mode->Network->ExaminationPending)
            return Fail(TEXT("Mouse-look center inspection failed"));
        ClosePanels();PC->Cursor();PC->SetControlRotation(OriginalView);
        Pawn->ZoomCamera(-1);
        if(Pawn->Boom->TargetArmLength<100)return Fail(TEXT("Wheel zoom could not leave first person"));
        Pawn->SetCameraDistance(380);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Mouse-pointer.png"),true,false);
        break;
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 mouse controls step %d passed"),Step);
    return true;
}
