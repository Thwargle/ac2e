#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "AC2Mob.h"
#include "Components/CapsuleComponent.h"
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
    const bool Select=E.GetEffectingButton()==EKeys::LeftMouseButton && CameraLeft && !CameraMoved && !CameraRight;
    if(E.GetEffectingButton()==EKeys::LeftMouseButton)CameraLeft=false;
    if(E.GetEffectingButton()==EKeys::RightMouseButton)CameraRight=false;
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    if(PC){PC->CameraSteersPlayer=CameraRight;PC->MouseForward=CameraLeft && CameraRight;PC->FreeOrbit=CameraDragging && CameraLeft && !CameraRight;}
    if(CameraLeft || CameraRight)return FReply::Handled();
    const FVector2D Point=CameraScreenPress;
    CameraDragging=CameraPrecision=false;CameraTravel=0;
    if(PC)PC->RestoreCursor();
    Mouse=CameraPress;
    if(Select)WorldClick(EKeys::LeftMouseButton,false);
    if(Inspect)WorldClick(EKeys::RightMouseButton,false);
    return FReply::Handled().ReleaseMouseCapture().SetMousePos(FIntPoint(FMath::RoundToInt(Point.X),FMath::RoundToInt(Point.Y)));
}

bool SAC2GameUI::OwnsPointer(FVector2D Point) const
{
    if(Frontend || MapUI.OwnsPointer(Point) || CraftOwnsPointer(Point))return true;
    for(const TCHAR* Panel:{TEXT("ToolbarField"),TEXT("ShortcutField"),TEXT("ChatWindow1"),
        TEXT("StatusbarField"),TEXT("LeftSidePanel"),TEXT("RightSidePanel"),TEXT("Shop"),TEXT("ExternalContainerField")})
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
        // Moving is supported; resizing is not yet implemented.
        if(Spec.Image>=0x41005998 && Spec.Image<=0x4100599b)Spec={};
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
    for(const auto& Name:WorldNameRects)if(Mouse.X>=Name.Value.Left && Mouse.X<Name.Value.Right && Mouse.Y>=Name.Value.Top && Mouse.Y<Name.Value.Bottom)
    {HoverObject=Name.Key;HoverMob.Reset();return;}
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
        // Self is deliberately excluded from combat target cycling. A short
        // right-click on the local capsule must still be able to inspect it.
        if(Button==EKeys::RightMouseButton)if(auto* Pawn=Cast<AAC2Explorer>(PC->GetPawn())){
            FHitResult SelfHit;
            if(Pawn->GetCapsuleComponent()->LineTraceComponent(SelfHit,Origin,Origin+Direction*15000,FCollisionQueryParams())){
                InspectTarget(Mode->Network->PlayerId);return true;
            }
        }
        FString Id;
        for(const auto& Name:WorldNameRects)if(Mouse.X>=Name.Value.Left && Mouse.X<Name.Value.Right && Mouse.Y>=Name.Value.Top && Mouse.Y<Name.Value.Bottom){Id=Name.Key;break;}
        if(Id.IsEmpty())Id=Mode->Network->WorldTargetAtRay(Origin,Direction);
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
void SAC2GameUI::BuildWorldText()
{
    WorldNameRects.Reset();
    if(Frontend || !Mode.IsValid() || Mode->WantsPortalSpace() || !Mode->Network || !Mode->Network->IsWorld())return;
    auto* Net=Mode->Network.Get();auto* PC=Mode->GetWorld()->GetFirstPlayerController();
    if(!PC || !PC->GetPawn())return;
    const FIntRect View(0,0,Size.X,Size.Y);
    const auto* Reticle=Native.Layouts[0x22000005].Find(TEXT("TargettingReticle"));
    auto Tuning=[&](const TCHAR* Key,int32 Default){
        if(Reticle)for(const auto& State:Reticle->States)
            if(State.Id==Reticle->DefaultState || State.Id==FAC2UILayout::Token(TEXT("stateless")))
                if(const auto* Value=State.Attributes.Find(FAC2UILayout::Token(Key)))return int32(*Value);
        return Default;
    };
    const int32 Attempts=FMath::Clamp(Tuning(TEXT("MaxNamePlacementAttempts"),50),1,100);
    const int32 Travel=FMath::Clamp(Tuning(TEXT("FloatyTextTravel"),3),0,20);
    TArray<FIntRect> Occupied;
    auto Place=[&](const TCHAR* Template,const FString& Text,FVector2D Center,float Opacity)->bool
    {
        const auto Font=Fonts.Get(Native.Layouts[0x22000005].Font(Template,0x4200000c));
        if(!Font)return false;
        const int32 Width=FMath::CeilToInt(Font->Width(Text))+8,Height=Font->Height*FMath::Max(1,Font->Lines(Text,0).Num())+4;
        FIntRect Rect(Center.X-Width/2,Center.Y-Height,Center.X+Width/2+1,Center.Y);
        for(int32 Attempt=0;Attempt<Attempts && Occupied.ContainsByPredicate([&](const FIntRect& R){return R.Intersect(Rect);});++Attempt)
            Rect-=FIntPoint(0,Height);
        if(Rect.Min.X<0 || Rect.Max.X>Size.X || Rect.Min.Y<0 || Rect.Max.Y>Size.Y || OwnsPointer(FVector2D(Rect.Min+Rect.Size()/2)))return false;
        if(Occupied.ContainsByPredicate([&](const FIntRect& R){return R.Intersect(Rect);}))return false;
        Occupied.Add(Rect);
        const int32 First=Native.Nodes.Num();Native.Template(0x22000005,Template,Rect,View);
        for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Kind==FAC2UILayout::Token(TEXT("text")))
        {
            auto& N=Native.Nodes[I];N.Text=Text;N.Opacity=Opacity;N.Attributes.Add(FAC2UILayout::Token(TEXT("one_line")),1);
        }
        return true;
    };
    const double Now=Mode->GetWorld()->GetTimeSeconds();
    // DoStory damage hooks are authoritative. Snapshot health changes must never
    // produce a second number (or numbers when loading a wounded character).
    for(const auto& Hit:Net->DamageTexts)
    {
        const float Age=Now-Hit.Born;
        const bool Personal=Hit.Target==Net->PlayerId || Hit.Attacker==Net->PlayerId;
        if(Age<0 || Age>=2 || !(Personal?DamageNumbers:ThirdPartyDamage))continue;
        FVector2D Point;FBox2D Bounds;
        if(Hit.Target==Net->PlayerId)
        {if(!PC->ProjectWorldLocationToScreen(PC->GetPawn()->GetActorLocation()+FVector(0,0,100),Point))continue;}
        else if(Net->ObjectScreenBounds(Hit.Target,Bounds))Point=FVector2D(Bounds.GetCenter().X,Bounds.Min.Y);
        else continue;
        if(!Personal)
        {
            const auto Contacts=Net->RadarContacts();
            const auto* Contact=Contacts.FindByPredicate([&](const auto& C){return C.Id==Hit.Target;});
            if(!Contact || !Contact->Actor.IsValid() || FVector::DistSquared(Contact->Actor->GetActorLocation(),PC->GetPawn()->GetActorLocation())>FMath::Square(15000.f*ThirdPartyDamageRange))continue;
        }
        const auto Font=Fonts.Get(Native.Layouts[0x22000005].Font(TEXT("DamageTextTemplate"),0x4200000c));
        const float Rise=(Font?Font->Height:17)*Travel*Age/2;
        // Keep selected-object damage above the reticle's name, as gm3DTextUI does.
        Point.Y-=(Hit.Target==Net->TargetId?(Reticle?Reticle->Rect.Height()+1:20)+34:12)+Rise;
        Place(TEXT("DamageTextTemplate"),FString::Printf(TEXT("%lld"),Hit.Damage),Point,1-Age/2);
    }
    if(!ShowObjectNames)return;
    auto Contacts=Net->RadarContacts();const FVector Origin=PC->GetPawn()->GetActorLocation();
    Contacts.Sort([&](const auto& A,const auto& B){return FVector::DistSquared(A.Actor->GetActorLocation(),Origin)<FVector::DistSquared(B.Actor->GetActorLocation(),Origin);});
    int32 Count=0;
    for(const auto& Contact:Contacts)
    {
        if(Contact.Id==Net->TargetId || Contact.Name.IsEmpty())continue;
        if(++Count>64)break;
        FBox2D Bounds;if(!Net->ObjectScreenBounds(Contact.Id,Bounds))continue;
        if(Place(TEXT("NameTextTemplate"),Contact.Name,FVector2D(Bounds.GetCenter().X,Bounds.Min.Y-12),1)){
            const auto& R=Occupied.Last();WorldNameRects.Add(Contact.Id,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));
        }
    }
}
bool SAC2GameUI::SmokeDamageText(FString& Error)
{
    auto* Net=Mode->Network.Get();
    if(!Net || Net->DamageTexts.IsEmpty()){Error=TEXT("No authoritative damage text events received");return false;}
    const bool Previous=DamageNumbers,PreviousThird=ThirdPartyDamage;
    DamageNumbers=false;ThirdPartyDamage=false;Rebuild();
    const bool Hidden=Native.Find(TEXT("DamageTextTemplate"))==nullptr;
    DamageNumbers=true;Rebuild();
    const bool Visible=Native.Find(TEXT("DamageTextTemplate"))!=nullptr;
    DamageNumbers=Previous;ThirdPartyDamage=PreviousThird;Rebuild();
    if(!Hidden || !Visible){Error=FString::Printf(TEXT("Damage number toggle: hidden=%d visible=%d"),Hidden,Visible);return false;}
    return true;
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
        const TCHAR* Template=Center?TEXT("SelectionName"):TEXT("NameTextTemplate");
        const auto Font=Fonts.Get(Native.Layouts[0x22000005].Font(Template,0x4200000c));
        const int32 Width=FMath::Min(int32(Size.X),FMath::Max(80,FMath::CeilToInt((Font?Font:BodyFont)->Width(Text))+12));
        const int32 Height=(Font?Font:BodyFont)->Height*FMath::Max(1,(Font?Font:BodyFont)->Lines(Text,0).Num())+4;
        if(Center)Point.X-=Width*.5;
        Point.X=FMath::Clamp(Point.X,0.,FMath::Max(0.,Size.X-Width));
        Point.Y=FMath::Clamp(Point.Y,0.,FMath::Max(0.,Size.Y-Height));
        const int32 Start=Native.Nodes.Num();
        Native.Template(0x22000005,Template,
            FIntRect(Point.X,Point.Y,Point.X+Width,Point.Y+Height),FIntRect(0,0,Size.X,Size.Y));
        for(int32 I=Start;I<Native.Nodes.Num();++I)
            if(Native.Nodes[I].Kind==FAC2UILayout::Token(TEXT("text"))){Native.Nodes[I].Text=Text;Native.Nodes[I].Attributes.Add(FAC2UILayout::Token(TEXT("one_line")),1);}
    };
    auto BuildHint=[&](const FString& Text,FVector2D Point)
    {
        if(Text.IsEmpty())return;
        // Use the authored tooltip field (background, border and DAT font), not
        // the centered, fixed-height world-name template. Size to all wrapped lines.
        constexpr uint32 Tooltip=0xf7c721aa,TooltipText=0xd5a32d4d;
        const FIntRect View(0,0,Size.X,Size.Y);
        const int32 Start=Native.Nodes.Num();
        if(!Native.Template(0x22000005,Tooltip,FIntRect(0,0,360,100),View))return;
        const auto* TextNode=Native.Find(TooltipText);
        const auto Font=TextNode?Fonts.Get(TextNode->Attr(TEXT("font"),0x4200000c)):BodyFont;
        const int32 Left=TextNode?TextNode->Attr(TEXT("left_margin")):2;
        const int32 Right=TextNode?TextNode->Attr(TEXT("right_margin")):2;
        const int32 Top=TextNode?TextNode->Attr(TEXT("top_margin")):0;
        const int32 Bottom=TextNode?TextNode->Attr(TEXT("bottom_margin")):0;
        Native.Nodes.SetNum(Start);
        if(!Font)return;
        const int32 MaxWidth=FMath::Max(16,FMath::Min(360,int32(Size.X)-8));
        int32 TextWidth=0;
        for(const auto& Line:Font->Lines(Text))TextWidth=FMath::Max(TextWidth,FMath::CeilToInt(Font->Width(Line)));
        const int32 Width=FMath::Clamp(TextWidth+Left+Right+8,16,MaxWidth);
        const int32 Height=FMath::Min(int32(Size.Y),Font->Lines(Text,FMath::Max(1,Width-Left-Right-8)).Num()*int32(Font->Height)+Top+Bottom+8);
        if(Point.X+Width>Size.X)Point.X=Mouse.X-Width-12;
        if(Point.Y+Height>Size.Y)Point.Y=Mouse.Y-Height-12;
        const FIntPoint Origin(FMath::Clamp(int32(Point.X),0,FMath::Max(0,int32(Size.X)-Width)),
                               FMath::Clamp(int32(Point.Y),0,FMath::Max(0,int32(Size.Y)-Height)));
        Native.Template(0x22000005,Tooltip,FIntRect(Origin,Origin+FIntPoint(Width,Height)),View);
        for(int32 I=Start;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==TooltipText)
        {
            auto& N=Native.Nodes[I];N.Text=Text;
            N.Rect.Min+=FIntPoint(4,4);N.Rect.Max-=FIntPoint(4,4);N.Clip=N.Rect;
        }
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
                // The DAT tip points northwest. In screen order TL, BL, TR, BR
                // turn it inward: southeast, northeast, southwest, northwest.
                const float Turns[]={2,1,3,0};
                for(int32 N=Start;N<Native.Nodes.Num();++N)Native.Nodes[N].Rotation=Turns[I]*HALF_PI;
            }
            Label(Net->ObjectName(Net->TargetId),FVector2D(Bounds.GetCenter().X,Bounds.Min.Y-Corner.Y-30),true);
        }
        if(!HoverObject.IsEmpty() && HoverObject!=Net->TargetId)Label(Net->ObjectName(HoverObject),Mouse+FVector2D(18,22));
    }
    if(!Mode->Network && HoverMob.IsValid())Label(HoverMob->DisplayName,Mouse+FVector2D(18,22));
    if(OwnsPointer(Mouse))for(const auto& Hint:Hints)
        if(const auto* R=Buttons.Find(Hint.Key);R && AllowsPanelButton(Hint.Key,Mouse) && Mouse.X>=R->Left && Mouse.X<R->Right && Mouse.Y>=R->Top && Mouse.Y<R->Bottom)
        {BuildHint(Hint.Value,Mouse+FVector2D(18,22));break;}
}
