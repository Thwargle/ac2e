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
    auto DragCell=[&](int32 From,int32 To){
        Pack=0;PackRow=0;Rebuild();
        const auto* A=Buttons.Find(*FString::Printf(TEXT("Cell%d"),From));
        const auto* B=Buttons.Find(*FString::Printf(TEXT("Cell%d"),To));if(!A||!B)return false;
        auto Point=[&](const FSlateRect& R){return GetCachedGeometry().LocalToAbsolute(FVector2D((R.Left+R.Right)*.5,(R.Top+R.Bottom)*.5)*Scale);};
        const auto Start=Point(*A),End=Point(*B);auto& App=FSlateApplication::Get();
        auto Window=App.FindWidgetWindow(SharedThis(this));if(!Window)return false;
        auto Path=App.LocateWindowUnderMouse(Start,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Start,Start,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,End,Start,{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState()),false);
        App.RoutePointerUpEvent(Path,FPointerEvent(0,End,Start,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
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
    if(Sword==INDEX_NONE || Staff==INDEX_NONE)return Fail(TEXT("Starter equipment is missing"));
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
        if(Profile->Items[Staff].Equipped!=0x40 || Profile->Items[Sword].Equipped)return Fail(TEXT("Second weapon replacement failed"));
        Rebuild();
        for(const auto& Icon:Icons)if(!Native.Brush(Icon.Image)){Error=FString::Printf(TEXT("Inventory icon texture missing: %08X"),Icon.Image);return false;}
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2Inventory-icons-tooltip.png"),true,false);
        break;
    case 4:
        Pack=0;PackRow=4;Rebuild();
        if(!Buttons.Contains(TEXT("Cell41")) || Buttons.Contains(TEXT("Cell42")))return Fail(TEXT("Main bag end is not accessible"));
        for(int32 I=1;I<4;++I)
        {
            Pack=I;PackRow=0;Rebuild();
            if(PackStart()!=42+(I-1)*12 || PackCount()!=12 || !Buttons.Contains(*FString::Printf(TEXT("Cell%d"),PackStart()+11)))
                return Fail(TEXT("Side pack capacity mismatch"));
        }
        Pack=0;PackRow=0;Rebuild();break;
    case 5:
        OriginalSwordCell=Profile->Items[Sword].Cell;
        if(OriginalSwordCell<=0 || !DragCell(OriginalSwordCell,0))return Fail(TEXT("Native bag drag did not route"));break;
    case 6:
        if(Profile->Items[Sword].Cell!=0)return Fail(TEXT("Bag drag did not update final cell"));
        if(!DragCell(0,OriginalSwordCell+1))return Fail(TEXT("Reverse bag drag did not route"));break;
    case 7:
    {
        if(Profile->Items[Sword].Cell!=OriginalSwordCell)return Fail(TEXT("Reverse bag drag has wrong final cell"));
        TSet<int32> Cells;
        for(const auto& I:Profile->Items)if(!I.Equipped){if(Cells.Contains(I.Cell))return Fail(TEXT("Bag contains overlapping item cells"));Cells.Add(I.Cell);}
        if(SelectedItem==INDEX_NONE || Profile->Items[SelectedItem].NetworkId!=SelectedNetworkItem)return Fail(TEXT("Selected item changed during object recreation"));
        ClosePanels();break;
    }
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
