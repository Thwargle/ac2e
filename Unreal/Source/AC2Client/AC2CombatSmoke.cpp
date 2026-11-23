#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "AC2Explorer.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "ProceduralMeshComponent.h"

// Exercise the rendered HUD's hit-test path and the viewport keyboard bindings,
// rather than calling SetCombat/Attack directly (which bypassed these regressions).
bool SAC2GameUI::SmokeCombatInput(int32 Step, FString& Error)
{
    auto* PC=Mode.IsValid()?Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController()):nullptr;
    auto* P=PC?Cast<AAC2Explorer>(PC->GetPawn()):nullptr;
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    if(!P || !Mode->InWorld())return Fail(TEXT("Combat input requires a world pawn"));
    auto& App=FSlateApplication::Get();
    auto Key=[&](FKey K){
        App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));
        App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));
    };
    auto Click=[&](const TCHAR* Name){
        Rebuild();
        const auto* R=Buttons.Find(Name);
        if(!R)return false;
        const FVector2D Absolute=GetCachedGeometry().LocalToAbsolute(FVector2D((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5)*Scale);
        const auto Window=App.FindWidgetWindow(SharedThis(this));
        if(!Window)return false;
        auto Path=App.LocateWindowUnderMouse(Absolute,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return false;
        const TSet<FKey> None,Down={EKeys::LeftMouseButton};
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,Absolute,Absolute,None,FKey(),0,FModifierKeysState()),false);
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Absolute,Absolute,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,Absolute,Absolute,None,EKeys::LeftMouseButton,0,FModifierKeysState()));
        return true;
    };
    auto Stance=[&](bool Expected){
        Rebuild();
        return Mode->InCombat()==Expected && (P->GetNativeMode()!=AC2Motion::Peace)==Expected &&
            Buttons.Contains(TEXT("StartAttackButton"))!=Expected && Buttons.Contains(TEXT("StopAttackButton"))==Expected;
    };
    switch(Step)
    {
    case 0: PC->SetCursor(true); break;
    case 1:
        if(!Stance(false) || !Click(TEXT("StartAttackButton")))return Fail(TEXT("Native start button unavailable in peace"));
        break;
    case 2:
        if(!Stance(true) || !Click(TEXT("StopAttackButton")))return Fail(TEXT("Start click did not enter combat / stop button unavailable"));
        break;
    case 3:
        if(!Stance(false))return Fail(TEXT("Stop click did not restore peace"));
        PC->SetCursor(false);App.SetAllUserFocusToGameViewport();Key(EKeys::Z);break;
    case 4:
        if(!Stance(true))return Fail(TEXT("Z did not enter combat through viewport input"));
        Key(EKeys::Z);break;
    case 5:
        if(!Stance(false))return Fail(TEXT("Z did not return to peace"));
        BeginChat();Key(EKeys::Z);break;
    case 6:
        if(!Stance(false))return Fail(TEXT("Typing Z in chat changed combat stance"));
        EndChat();Key(EKeys::One);break;
    case 7:
        if(!Stance(false) || !Mode->Notice.StartsWith(TEXT("Select a creature first")))
            return Fail(TEXT("Attack key did not report missing target after closing chat"));
        break;
    case 8: Key(EKeys::One);break; // Caller has selected a visible creature.
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 combat input step %d passed; mode=%08X combat=%d"),Step,P->GetNativeMode(),Mode->InCombat());
    return true;
}

bool SAC2GameUI::SmokeWorldTarget(const FString& Id,FString& Error,FKey Button,bool DoubleClick,bool CenterAim)
{
    auto* PC=Cast<AAC2PlayerController>(Mode->GetWorld()->GetFirstPlayerController());
    if(!PC || !Mode->Network)return false;
    const auto Contacts=Mode->Network->RadarContacts();
    const auto* Contact=Contacts.FindByPredicate([&](const auto& C){return C.Id==Id;});
    if(!Contact || !Contact->Actor.IsValid()){Error=TEXT("World target is hidden/unplaced");return false;}
    // Use a visible surface pixel: the center of an Idol Spawn's bounds is
    // empty space between its animated limbs, and other actors can occlude it.
    FVector2D Screen;
    FVector SurfacePoint;
    bool Found=false;
    TInlineComponentArray<UProceduralMeshComponent*> Meshes(Contact->Actor.Get());
    for(auto* Mesh:Meshes)
    {
        if(!Mesh->IsVisible() || Mesh->bHiddenInGame)continue;
        for(int32 S=0;S<Mesh->GetNumSections() && !Found;++S)
        {
            const auto* Part=Mesh->GetProcMeshSection(S);
            if(!Part || !Part->bSectionVisible)continue;
            for(int32 I=0;I+2<Part->ProcIndexBuffer.Num() && !Found;I+=3)
            {
                FVector Center=FVector::ZeroVector;
                for(int32 J=0;J<3;++J)Center+=Part->ProcVertexBuffer[Part->ProcIndexBuffer[I+J]].Position/3.;
                if(!PC->ProjectWorldLocationToScreen(Mesh->GetComponentTransform().TransformPosition(Center),Screen,true) ||
                    Screen.X<0 || Screen.Y<0 || Screen.X>=Size.X || Screen.Y>=Size.Y)continue;
                const bool Covered=OwnsPointer(Screen);
                FVector Origin,Direction;
                Found=!Covered && PC->DeprojectScreenPositionToWorld(Screen.X,Screen.Y,Origin,Direction) &&
                    Mode->Network->WorldTargetAtRay(Origin,Direction)==Id;
                if(Found)SurfacePoint=Mesh->GetComponentTransform().TransformPosition(Center);
            }
        }
        if(Found)break;
    }
    if(!Found){Error=TEXT("Creature has no unobstructed surface pixel to click");return false;}
    if(CenterAim)
    {
        auto* P=Cast<AAC2Explorer>(PC->GetPawn());
        PC->SetControlRotation((SurfacePoint-P->Camera->GetComponentLocation()).Rotation());
        P->Boom->TickComponent(0,LEVELTICK_All,nullptr);
        PC->PlayerCameraManager->UpdateCamera(0);
        Screen=Size*.5;
    }
    const FVector2D Absolute=GetCachedGeometry().LocalToAbsolute(Screen*Scale);
    auto& App=FSlateApplication::Get();
    const auto Window=App.FindWidgetWindow(SharedThis(this));
    if(!Window)return false;
    const auto Path=App.LocateWindowUnderMouse(Absolute,{Window.ToSharedRef()},false);
    if(!Path.IsValid()){Error=TEXT("World pointer has no viewport path");return false;}
    const TSet<FKey> None,Down={Button};
    App.SetCursorPos(Absolute);
    App.RoutePointerMoveEvent(Path,FPointerEvent(0,Absolute,Absolute,None,FKey(),0,FModifierKeysState()),false);
    NextHover=0;UpdateHover(GetCachedGeometry(),FPlatformTime::Seconds());
    const auto Spec=CursorAt(Absolute);
    if(!CenterAim && (HoverObject!=Id || Spec.Image!=0x41000870 || Spec.Hotspot!=FIntPoint::ZeroValue))
    {Error=TEXT("World hover did not retain the native default pointer");return false;}
    if(DoubleClick)App.RoutePointerDoubleClickEvent(Path,FPointerEvent(0,Absolute,Absolute,Down,Button,0,FModifierKeysState()));
    else App.RoutePointerDownEvent(Path,FPointerEvent(0,Absolute,Absolute,Down,Button,0,FModifierKeysState()));
    App.RoutePointerUpEvent(Path,FPointerEvent(0,Absolute,Absolute,None,Button,0,FModifierKeysState()));
    if(Mode->Network->TargetId!=Id){Error=FString::Printf(TEXT("World click at %.0f,%.0f did not select %s"),Screen.X,Screen.Y,*Id);return false;}
    UE_LOG(LogTemp,Display,TEXT("AC2 combat world pointer selected %s at %.0f,%.0f"),*Contact->Name,Screen.X,Screen.Y);
    return true;
}
