#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2Mob.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

void SAC2GameUI::CancelCameraGesture()
{
    const bool Active=CameraLeft || CameraRight;
    CameraLeft=CameraRight=CameraDragging=CameraMoved=CameraPrecision=CameraRecapturing=false;CameraTravel=0;
    if(auto* PC=Mode.IsValid()?Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()):nullptr)
    {
        PC->CameraSteersPlayer=PC->MouseForward=PC->FreeOrbit=false;
        if(Active){PC->RestoreCursor();FSlateApplication::Get().SetCursorPos(CameraScreenPress);}
    }
}
FReply SAC2GameUI::CameraButtonDown(const FPointerEvent& E)
{
    const auto Button=E.GetEffectingButton();
    if(Button!=EKeys::LeftMouseButton && Button!=EKeys::RightMouseButton)return FReply::Handled();
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    if(!PC || Editing || Frontend || !Mode->InWorld())return FReply::Unhandled();
    if(!CameraLeft && !CameraRight)
    {
        CameraPress=Mouse;CameraScreenPress=E.GetScreenSpacePosition();CameraTravel=0;CameraMoved=false;CameraPrecision=false;
        if(Button==EKeys::LeftMouseButton)WorldClick(Button,false);
    }
    if(Button==EKeys::LeftMouseButton)CameraLeft=true;else CameraRight=true;
    CameraDragging|=CameraRight;
    PC->CameraSteersPlayer=CameraRight;
    PC->MouseForward=CameraRight && CameraLeft;
    PC->FreeOrbit=CameraDragging && !CameraRight;
    CameraMoved|=PC->MouseForward;
    if(CameraDragging && !CameraPrecision)
    {
        PC->bShowMouseCursor=false;
        CameraRecapturing=HasMouseCapture();CameraPrecision=true;
        return FReply::Handled().UseHighPrecisionMouseMovement(SharedThis(this));
    }
    if(HasMouseCapture())return FReply::Handled();
    return FReply::Handled().CaptureMouse(SharedThis(this));
}
FReply SAC2GameUI::CameraMouseMove(const FPointerEvent& E)
{
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    if(!P || Editing || Frontend || !Mode->InWorld())
    {CancelCameraGesture();return FReply::Handled().ReleaseMouseCapture();}
    const FVector2D Delta=E.GetCursorDelta();
    CameraTravel+=FMath::Abs(Delta.X)+FMath::Abs(Delta.Y);
    CameraMoved|=CameraTravel>4;
    if(!CameraDragging && CameraMoved)
    {
        CameraDragging=true;PC->bShowMouseCursor=false;PC->FreeOrbit=!CameraRight;
    }
    if(CameraDragging)
    {
        // AC1's held RMB steers, LMB drag orbits, and the two-button chord walks.
        // Apply Slate's relative delta once; the ignored pawn mouse axes must not add it again.
        auto Rotation=PC->GetControlRotation();
        const float Sensitivity=.2f*P->MouseSensitivity;
        Rotation.Yaw+=Delta.X*Sensitivity;
        Rotation.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch)-Delta.Y*Sensitivity*(P->InvertMouse?-1:1),-80.f,80.f);
        PC->SetControlRotation(Rotation);
        if(!CameraPrecision)
        {
            CameraRecapturing=HasMouseCapture();CameraPrecision=true;
            return FReply::Handled().UseHighPrecisionMouseMovement(SharedThis(this));
        }
    }
    return FReply::Handled();
}
FReply SAC2GameUI::CameraButtonUp(const FPointerEvent& E)
{
    const bool Inspect=E.GetEffectingButton()==EKeys::RightMouseButton && CameraRight && !CameraMoved;
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)CameraLeft=false;
    if(E.GetEffectingButton()==EKeys::RightMouseButton)CameraRight=false;
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    if(PC){PC->CameraSteersPlayer=CameraRight;PC->MouseForward=CameraLeft && CameraRight;PC->FreeOrbit=CameraDragging && CameraLeft && !CameraRight;}
    if(CameraLeft || CameraRight)return FReply::Handled();
    const FVector2D Point=CameraScreenPress;
    CameraDragging=CameraPrecision=false;CameraTravel=0;
    if(PC)PC->RestoreCursor();
    Mouse=CameraPress;
    if(Inspect)WorldClick(EKeys::RightMouseButton,false);
    return FReply::Handled().ReleaseMouseCapture().SetMousePos(FIntPoint(FMath::RoundToInt(Point.X),FMath::RoundToInt(Point.Y)));
}

bool SAC2GameUI::OwnsPointer(FVector2D Point) const
{
    if(Frontend || MapUI.OwnsPointer(Point))return true;
    for(const TCHAR* Panel:{TEXT("ToolbarField"),TEXT("ShortcutField"),TEXT("ChatWindow1"),
        TEXT("StatusbarField"),TEXT("LeftSidePanel"),TEXT("RightSidePanel"),TEXT("Shop")})
        if(Native.Bounds(Panel).Contains(FIntPoint(Point.X,Point.Y)))return true;
    return false;
}
FAC2CursorIcon SAC2GameUI::CursorAt(FVector2D ScreenPoint) const
{
    FAC2CursorIcon Spec;
    const auto Point=GetCachedGeometry().AbsoluteToLocal(ScreenPoint)/Scale;
    if(MapUI.OwnsPointer(Point))MapUI.CursorAt(Point,Spec.Image,Spec.Hotspot);
    else if(OwnsPointer(Point))
    {
        Native.CursorAt(Point,Spec.Image,Spec.Hotspot);
        // These fields are fixed in the current HUD. Do not advertise unsupported dragging/resizing.
        if(Spec.Image==0x41000d0b || (Spec.Image>=0x41005998 && Spec.Image<=0x4100599b))Spec={};
    }
    else if(!HoverObject.IsEmpty() || HoverMob.IsValid()) {Spec.Image=0x4100751e;Spec.Hotspot=FIntPoint(11,11);}
    return Spec;
}
void SAC2GameUI::UpdateHover(const FGeometry& G,double Time)
{
    auto* PC=Mode.IsValid()?Mode->GetWorld()->GetFirstPlayerController():nullptr;
    if(!PC || !PC->bShowMouseCursor || !VisibleWorld() || Frontend)
    {HoverObject.Reset();HoverMob.Reset();return;}
    Mouse=G.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())/Scale;
    if(OwnsPointer(Mouse) || Mouse.X<0 || Mouse.Y<0 || Mouse.X>=Size.X || Mouse.Y>=Size.Y)
    {HoverObject.Reset();HoverMob.Reset();return;}
    if(Time<NextHover)return;
    NextHover=Time+.075;
    FVector Origin,Direction;
    HoverObject.Reset();
    HoverMob.Reset();
    if(Mode->Network && PC->DeprojectScreenPositionToWorld(Mouse.X,Mouse.Y,Origin,Direction))
        HoverObject=Mode->Network->WorldTargetAtRay(Origin,Direction);
    else if(!Mode->Network && PC->DeprojectScreenPositionToWorld(Mouse.X,Mouse.Y,Origin,Direction))
    {
        FHitResult Hit;
        if(Mode->GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*15000,ECC_Visibility))
            HoverMob=Cast<AAC2Mob>(Hit.GetActor());
    }
}
void SAC2GameUI::InspectTarget(const FString& Id)
{
    if(!Mode.IsValid() || !Mode->Network || Id.IsEmpty())return;
    EndChat();
    InspectedObject=Id;SelectedItem=INDEX_NONE;ExamineScroll=0;
    ShowStats=true;ShowOptions=false;Mode->bMap=false;MapUI.Close();
    Mode->Network->Examine(Id);
    if(auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()))PC->SetCursor(true);
    Rebuild();
}
bool SAC2GameUI::WorldClick(FKey Button,bool DoubleClick)
{
    if(!Mode.IsValid() || OwnsPointer(Mouse))return false;
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    FVector Origin,Direction;
    if(!PC || !PC->DeprojectScreenPositionToWorld(Mouse.X,Mouse.Y,Origin,Direction))return false;
    // gmSelectionUI::Finish3DSelection (Legions): left selects, double-left uses,
    // a short right-click selects and examines after the camera gesture releases.
    if(Mode->Network)
    {
        const FString Id=Mode->Network->WorldTargetAtRay(Origin,Direction);
        if(Id.IsEmpty()){Mode->Network->ClearTarget();return true;}
        Mode->Network->SelectTarget(Id);
        if(Button==EKeys::RightMouseButton)InspectTarget(Id);
        else if(DoubleClick)Mode->Network->UseTarget();
        return true;
    }
    FHitResult Hit;
    if(Mode->GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*15000,ECC_Visibility))
        if(auto* Mob=Cast<AAC2Mob>(Hit.GetActor()))
        {
            Mode->Target=Mob;Mode->Notice=TEXT("Target: ")+Mob->DisplayName;
            if(Button==EKeys::RightMouseButton)
            {
                InspectedMob=Mob;SelectedItem=INDEX_NONE;ExamineScroll=0;ShowStats=true;ShowOptions=false;
                if(auto* Controller=Cast<AAC2PlayerController>(PC))Controller->SetCursor(true);
                Rebuild();
            }
            return true;
        }
    Mode->Target=nullptr;
    return true;
}
void SAC2GameUI::ClickWorldCenter(FKey Button,bool DoubleClick)
{
    if(!VisibleWorld() || !Mode.IsValid() || !Mode->InWorld())return;
    const auto Previous=Mouse;Mouse=Size*.5f;
    WorldClick(Button,DoubleClick);
    Mouse=Previous;
}
void SAC2GameUI::BuildPointerFeedback()
{
    if(Frontend || !Mode.IsValid())return;
    const auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    // gmCameraUI::UpdateCamera: the authored, centered crosshair is visible in mouse look.
    if(PC && !PC->bShowMouseCursor)Native.Root(0x22000005,TEXT("MouseLookCursorField"));
    auto Label=[&](const FString& Text,FVector2D Point,bool Center=false)
    {
        if(Text.IsEmpty())return;
        const int32 Width=FMath::Min(420,FMath::Max(80,FMath::CeilToInt(BodyFont->Width(Text))+12));
        if(Center)Point.X-=Width*.5;
        Point.X=FMath::Clamp(Point.X,0.,FMath::Max(0.,Size.X-Width));
        Point.Y=FMath::Clamp(Point.Y,0.,FMath::Max(0.,Size.Y-36));
        const int32 Start=Native.Nodes.Num();
        Native.Template(0x22000005,TEXT("NameTextTemplate"),
            FIntRect(Point.X,Point.Y,Point.X+Width,Point.Y+36),FIntRect(0,0,Size.X,Size.Y));
        for(int32 I=Start;I<Native.Nodes.Num();++I)
            if(Native.Nodes[I].Kind==FAC2UILayout::Token(TEXT("text")))Native.Nodes[I].Text=Text;
    };
    if(!DragItem.IsEmpty() && FVector2D::Distance(Mouse,ItemPressPoint)>6)
    {
        if(const auto* Profile=Mode->Characters.Active())
            if(const auto* Item=Profile->Items.FindByPredicate([&](const auto& I){return I.NetworkId==DragItem;}))
                if(const auto* Definition=Mode->CharacterCatalog.Items.Find(Item->Definition))
                    Label(Definition->Name,Mouse+FVector2D(20,22));
        return;
    }
    if(auto* Net=Mode->Network.Get())
    {
        FBox2D Bounds;
        if(Net->ObjectScreenBounds(Net->TargetId,Bounds) && Bounds.Max.X>=0 && Bounds.Min.X<Size.X && Bounds.Max.Y>=0 && Bounds.Min.Y<Size.Y)
        {
            // gm3DTextUI::InitReticle / UpdateReticleInternal: four copies of the DAT corner,
            // rotated in 90-degree steps, with the selected object's name above them.
            const auto* Template=Native.Layouts[0x22000005].Find(TEXT("TargettingReticle"));
            const FIntPoint Corner=Template?Template->Rect.Size()+FIntPoint(1,1):FIntPoint(20,20);
            for(int32 I=0;I<4;++I)
            {
                const FIntPoint Point(FMath::Clamp(int32(I<2?Bounds.Min.X-Corner.X:Bounds.Max.X),0,int32(Size.X)-Corner.X),
                    FMath::Clamp(int32(I%2?Bounds.Max.Y:Bounds.Min.Y-Corner.Y),0,int32(Size.Y)-Corner.Y));
                const int32 Start=Native.Nodes.Num();
                Native.Template(0x22000005,TEXT("TargettingReticle"),FIntRect(Point,Point+Corner),FIntRect(0,0,Size.X,Size.Y));
                for(int32 N=Start;N<Native.Nodes.Num();++N)Native.Nodes[N].Rotation=I*HALF_PI;
            }
            Label(Net->ObjectName(Net->TargetId),FVector2D(Bounds.GetCenter().X,Bounds.Min.Y-Corner.Y-30),true);
        }
        if(!HoverObject.IsEmpty() && HoverObject!=Net->TargetId)Label(Net->ObjectName(HoverObject),Mouse+FVector2D(18,22));
    }
    if(!Mode->Network && HoverMob.IsValid())Label(HoverMob->DisplayName,Mouse+FVector2D(18,22));
    if(OwnsPointer(Mouse))for(const auto& Hint:Hints)
        if(const auto* R=Buttons.Find(Hint.Key);R && (!ShopOwnsPointer(Mouse) || ShopButton(Hint.Key)) && Mouse.X>=R->Left && Mouse.X<R->Right && Mouse.Y>=R->Top && Mouse.Y<R->Bottom)
        {Label(Hint.Value,Mouse+FVector2D(18,22));break;}
}
