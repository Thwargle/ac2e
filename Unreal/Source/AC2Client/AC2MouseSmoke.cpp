#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

bool SAC2GameUI::SmokeInventoryInput(int32 Step,FString& Error)
{
    auto* Profile=Mode->Characters.Active();auto* Net=Mode->Network.Get();
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!Profile || !Net)return Fail(TEXT("No network inventory"));
    auto Item=[&](uint32 Definition){return Profile->Items.IndexOfByPredicate([&](const auto& I){return I.Definition==Definition;});};
    static int32 OriginalSwordCell=0;
    static FVector2D DragStart,DragEnd;
    static FString DroppedId;
    auto ReleaseDrag=[&](){
        auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));if(!Window)return false;
        auto Path=App.LocateWindowUnderMouse(DragEnd,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.SetCursorPos(DragEnd);
        App.RoutePointerUpEvent(Path,FPointerEvent(0,DragEnd,DragStart,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    auto DragCell=[&](int32 From,int32 To,bool Hold=false){
        Pack=0;PackRow=0;Rebuild();
        const auto* A=Buttons.Find(*FString::Printf(TEXT("Cell%d"),From));
        const auto* B=Buttons.Find(*FString::Printf(TEXT("Cell%d"),To));if(!A||(To>=0&&!B))return false;
        auto Point=[&](const FSlateRect& R){return GetCachedGeometry().LocalToAbsolute(FVector2D((R.Left+R.Right)*.5,(R.Top+R.Bottom)*.5)*Scale);};
        const auto Start=Point(*A),End=To<0?GetCachedGeometry().LocalToAbsolute(FVector2D(Size.X*.55,Size.Y*.35)*Scale):Point(*B);auto& App=FSlateApplication::Get();
        DragStart=Start;DragEnd=End;
        auto Window=App.FindWidgetWindow(SharedThis(this));if(!Window)return false;
        auto Path=App.LocateWindowUnderMouse(Start,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Start,Start,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,End,Start,{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState()),false);
        App.SetCursorPos(End);
        return Hold || ReleaseDrag();
    };
    auto ClickItem=[&](int32 Index){
        if(!Profile->Items.IsValidIndex(Index))return false;
        const auto& I=Profile->Items[Index];Pack=0;PackRow=I.Equipped?0:FMath::Max(0,I.Cell/6-1);Rebuild();
        const FName Key(*(I.Equipped?FString::Printf(TEXT("Item%d"),Index):FString::Printf(TEXT("Cell%d"),I.Cell)));
        const auto* R=Buttons.Find(Key);if(!R)return false;
        const auto Absolute=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
        auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));if(!Window)return false;
        auto Path=App.LocateWindowUnderMouse(Absolute,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.SetCursorPos(Absolute);
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,Absolute,Absolute,{},FKey(),0,FModifierKeysState()),false);
        App.RoutePointerDoubleClickEvent(Path,FPointerEvent(0,Absolute,Absolute,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,Absolute,Absolute,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    const int32 Sword=Item(0x470007ee),Staff=Item(0x470007e0);
    if((Sword==INDEX_NONE && Step!=11) || Staff==INDEX_NONE)return Fail(TEXT("Starter equipment is missing"));
    switch(Step)
    {
    case 0: Net->SetCombat(false);break;
    case 1:
        if(Net->Combat)return Fail(TEXT("Equipment test did not leave combat"));
        if(!ShowInventory)ToggleInventory();
        if(!ClickItem(Sword))return Fail(TEXT("Sword double-click did not route"));break;
    case 2:
        if(Profile->Items[Sword].Equipped!=0x40 || Profile->Items[Staff].Equipped ||
            SelectedNetworkItem!=Profile->Items[Sword].NetworkId)return Fail(TEXT("Weapon replacement or stable selection failed"));
        if(!ClickItem(Staff))return Fail(TEXT("Staff double-click did not route"));break;
    case 3:
    {
        if(Profile->Items[Staff].Equipped!=0x40 || Profile->Items[Sword].Equipped)return Fail(TEXT("Second weapon replacement failed"));
        Rebuild();
        for(const auto& Icon:Icons)if(!Native.Brush(Icon.Image)){Error=FString::Printf(TEXT("Inventory icon texture missing: %08X"),Icon.Image);return false;}
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Inventory-icons-tooltip.png"),true,false);
        Net->CloseLoot();Rebuild();
        auto* PC=Mode->GetWorld()->GetFirstPlayerController();FVector2D Point;
        if(!PC || !PC->GetPawn() || !PC->ProjectWorldLocationToScreen(PC->GetPawn()->GetActorLocation(),Point))return Fail(TEXT("Cannot project the local player for examination"));
        const auto SavedMouse=Mouse;Mouse=Point;
        const bool Examined=WorldClick(EKeys::RightMouseButton,false) && InspectedObject==Net->PlayerId;
        Mouse=SavedMouse;
        if(!Examined)return Fail(TEXT("Right-clicking the player's body did not request self examination"));
        break;
    }
    case 4:
    {
        if(Net->ExaminationPending || Net->ExaminationId!=Net->PlayerId || !Net->ExaminationRows.ContainsByPredicate([](const auto& R){return R.Text.Contains(TEXT("Vigor:"));}))return Fail(TEXT("Self examination did not receive authoritative vitals"));
        Pack=0;PackRow=0;Rebuild();
        const FIntRect TopThumb=PackThumb;
        if(PackMaxScroll<=0 || TopThumb.IsEmpty() || PackBar.Height()<=TopThumb.Height())return Fail(TEXT("Inventory has no usable scroll thumb"));
        const auto List=Native.Bounds(TEXT("ContainerListBox"));
        const auto WheelPoint=GetCachedGeometry().LocalToAbsolute(FVector2D(List.Min+List.Size()/2)*Scale);
        OnMouseWheel(GetCachedGeometry(),FPointerEvent(0,WheelPoint,WheelPoint,{},EKeys::Invalid,-1,FModifierKeysState()));
        if(PackRow!=1 || PackThumb.Min.Y<=TopThumb.Min.Y)return Fail(TEXT("Inventory wheel did not move its thumb with the rows"));
        Action(TEXT("InventoryUp"));
        if(PackRow!=0 || PackThumb!=TopThumb)return Fail(TEXT("Inventory up button did not restore the first row"));
        const auto ThumbPoint=GetCachedGeometry().LocalToAbsolute(FVector2D(PackThumb.Min+PackThumb.Size()/2)*Scale);
        const auto EndPoint=GetCachedGeometry().LocalToAbsolute(FVector2D(PackThumb.Min.X+PackThumb.Width()/2,PackBar.Max.Y)*Scale);
        OnMouseButtonDown(GetCachedGeometry(),FPointerEvent(0,ThumbPoint,ThumbPoint,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,EndPoint,ThumbPoint,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        OnMouseButtonUp(GetCachedGeometry(),FPointerEvent(0,EndPoint,EndPoint,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(PackRow!=PackMaxScroll)return Fail(TEXT("Inventory thumb drag did not reach the last row"));
        Pack=0;PackRow=4;Rebuild();
        if(!Buttons.Contains(TEXT("Cell41")) || Buttons.Contains(TEXT("Cell42")))return Fail(TEXT("Main bag end is not accessible"));
        for(int32 I=1;I<4;++I)
        {
            Pack=I;PackRow=0;Rebuild();
            if(PackStart()!=42+(I-1)*12 || PackCount()!=12 || !Buttons.Contains(*FString::Printf(TEXT("Cell%d"),PackStart()+11)))
                return Fail(TEXT("Side pack capacity mismatch"));
        }
        Pack=0;PackRow=0;Rebuild();break;
    }
    case 5:
        OriginalSwordCell=Profile->Items[Sword].Cell;
        if(OriginalSwordCell<=0 || !DragCell(OriginalSwordCell,0,true))return Fail(TEXT("Native bag drag did not route"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Inventory-drag.png"),true,false);break;
    case 6:
        if(!HasMouseCapture() || DragItem.IsEmpty())return Fail(TEXT("Inventory drag did not survive frames and HUD rebuilds"));
        if(!ReleaseDrag())return Fail(TEXT("Held inventory release did not route"));break;
    case 7:
        if(Profile->Items[Sword].Cell!=0)return Fail(TEXT("Bag drag did not update final cell"));
        if(!DragCell(0,1))return Fail(TEXT("Adjacent bag drag did not route"));break;
    case 8:
        if(Profile->Items[Sword].Cell!=1)return Fail(TEXT("Adjacent drag was incorrectly a no-op"));
        if(!DragCell(1,OriginalSwordCell))return Fail(TEXT("Reverse bag drag did not route"));break;
    case 9:
    {
        if(Profile->Items[Sword].Cell!=OriginalSwordCell)return Fail(TEXT("Reverse bag drag has wrong final cell"));
        TSet<int32> Cells;
        for(const auto& I:Profile->Items)if(!I.Equipped){if(Cells.Contains(I.Cell))return Fail(TEXT("Bag contains overlapping item cells"));Cells.Add(I.Cell);}
        if(SelectedItem==INDEX_NONE || Profile->Items[SelectedItem].NetworkId!=SelectedNetworkItem)return Fail(TEXT("Selected item changed during object recreation"));
        DroppedId=LexToString(0x8180000000000000ull | (FCString::Strtoui64(*Profile->Items[Sword].NetworkId,nullptr,10)&0x0000ffffffffffffull));
        if(!DragCell(OriginalSwordCell,-1,true))return Fail(TEXT("World drag did not start"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Inventory-world-drag.png"),true,false);break;
    }
    case 10:
        if(!HasMouseCapture() || DragItem.IsEmpty())return Fail(TEXT("World drag lost capture while held"));
        if(!ReleaseDrag())return Fail(TEXT("World drop release did not route"));break;
    case 11:
        if(Sword!=INDEX_NONE || !Net->RadarContacts().ContainsByPredicate([&](const auto& C){return C.Id==DroppedId && C.Actor.IsValid();}))return Fail(TEXT("World drop did not transfer the owned item to a rendered object"));
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Inventory-world-drop.png"),true,false);
        Net->SelectTarget(DroppedId);Net->UseTarget();break;
    case 12:
        if(Sword==INDEX_NONE || Net->RadarContacts().ContainsByPredicate([&](const auto& C){return C.Id==DroppedId;}))return Fail(TEXT("Ground item pickup did not restore inventory and remove its world object"));
        if(!DragCell(Profile->Items[Sword].Cell,0,true))return Fail(TEXT("Cancel test drag did not start"));
        FSlateApplication::Get().ReleaseAllPointerCapture();break;
    case 13:
        if(!DragItem.IsEmpty() || HasMouseCapture())return Fail(TEXT("Capture loss left an inventory drag active"));
        ClosePanels();break;
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 inventory input step %d passed"),Step);return true;
}

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
    static FVector WalkStart;
    auto Gesture=[&](FKey Button,bool Down,FVector2D Delta=FVector2D::ZeroVector) {
        const auto Window=App.FindWidgetWindow(SharedThis(this));if(!Window)return false;
        const auto Path=App.LocateWindowUnderMouse(Pointer,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        TSet<FKey> Keys;if(CameraLeft)Keys.Add(EKeys::LeftMouseButton);if(CameraRight)Keys.Add(EKeys::RightMouseButton);
        if(Button.IsValid()){if(Down)Keys.Add(Button);else Keys.Remove(Button);}
        FPointerEvent Event(0,Pointer+Delta,Pointer,Keys,Button,0,FModifierKeysState());
        if(!Button.IsValid())App.RoutePointerMoveEvent(Path,Event,false);
        else if(Down)App.RoutePointerDownEvent(Path,Event);
        else App.RoutePointerUpEvent(Path,Event);
        return true;
    };
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
        if(!Middle() || !PC->bShowMouseCursor || !PC->IsLookInputIgnored())return Fail(TEXT("Middle click enabled persistent mouse look"));
        App.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftAlt,FModifierKeysState(),0,false,0,0));
        App.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftAlt,FModifierKeysState(),0,false,0,0));
        if(!PC->bShowMouseCursor || !PC->IsLookInputIgnored())return Fail(TEXT("Alt enabled persistent mouse look"));
        Pawn->AddControllerYawInput(12);
        break;
    }
    case 4:
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalYaw,Pawn->GetActorRotation().Yaw))>.5f)
            return Fail(TEXT("Stationary camera orbit changed the network character heading"));
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalView.Yaw,PC->GetControlRotation().Yaw))>.5f)
            return Fail(TEXT("Uncaptured pointer movement turned the camera"));
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
        PC->SetCursor(false);
        if(!SmokeWorldTarget(Id,Error,EKeys::RightMouseButton,false,true))return false;
        if(!ShowStats || InspectedObject!=Id || !Mode->Network->ExaminationPending)
            return Fail(TEXT("Mouse-look center inspection failed"));
        ClosePanels();PC->RestoreCursor();PC->SetControlRotation(OriginalView);
        Pawn->ZoomCamera(-1);
        if(Pawn->Boom->TargetArmLength<100)return Fail(TEXT("Wheel zoom could not leave first person"));
        Pawn->SetCameraDistance(380);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Mouse-pointer.png"),true,false);
        break;
    case 6:
        ClosePanels();
        Mode->Network->SelectTarget(Id);
        Pointer=GetCachedGeometry().LocalToAbsolute(FVector2D(Size.X*.55,Size.Y*.35)*Scale);
        App.SetCursorPos(Pointer);OriginalView=PC->GetControlRotation();OriginalYaw=Pawn->GetActorRotation().Yaw;
        if(!Gesture(EKeys::LeftMouseButton,true) || !Gesture(FKey(),false,FVector2D(40,0)) ||
            !PC->FreeOrbit || PC->CameraSteersPlayer || PC->bShowMouseCursor)
        {Error=FString::Printf(TEXT("LMB drag did not capture free orbit: L=%d R=%d drag=%d orbit=%d steer=%d cursor=%d travel=%g"),CameraLeft,CameraRight,CameraDragging,PC->FreeOrbit,PC->CameraSteersPlayer,PC->bShowMouseCursor,CameraTravel);return false;}
        if(Mode->Network->TargetId!=Id)return Fail(TEXT("Camera drag cleared the selected creature"));
        break;
    case 7:
        if(FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalYaw,Pawn->GetActorRotation().Yaw))>.5f ||
            FMath::Abs(FMath::FindDeltaAngleDegrees(OriginalView.Yaw,PC->GetControlRotation().Yaw))<1)
            return Fail(TEXT("LMB orbit changed character heading or failed to turn view"));
        WalkStart=Pawn->GetActorLocation();
        if(!Gesture(EKeys::RightMouseButton,true) || !PC->MouseForward || !PC->CameraSteersPlayer || PC->FreeOrbit)
            return Fail(TEXT("Both-button chord did not walk and steer"));
        Gesture(FKey(),false,FVector2D(20,0));break;
    case 8:
        if(FVector::Dist2D(WalkStart,Pawn->GetActorLocation())<5)
            return Fail(TEXT("Held mouse chord did not move the pawn"));
        if(!Gesture(EKeys::RightMouseButton,false) || PC->MouseForward || !PC->FreeOrbit)
            return Fail(TEXT("Releasing RMB did not stop walking and resume orbit"));
        if(!Gesture(EKeys::LeftMouseButton,false) || !PC->bShowMouseCursor || PC->FreeOrbit || PC->CameraSteersPlayer ||
            FVector2D::Distance(Pointer,App.GetCursorPos())>3)
            return Fail(TEXT("Gesture release did not restore pointer and camera state"));
        if(Mode->Network->TargetId!=Id)return Fail(TEXT("Camera gesture release cleared the selected creature"));
        PC->SetControlRotation(OriginalView);break;
    case 9:
        if(!Gesture(EKeys::RightMouseButton,true) || PC->bShowMouseCursor)return Fail(TEXT("Held RMB did not capture"));
        App.ReleaseAllPointerCapture();
        if(CameraLeft || CameraRight || PC->MouseForward || PC->CameraSteersPlayer || !PC->bShowMouseCursor)
            return Fail(TEXT("Capture loss left mouse steering active"));
        break;
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 mouse controls step %d passed"),Step);
    return true;
}
