#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"

bool SAC2GameUI::ShopOwnsPointer(FVector2D Point) const
{
    return Native.Bounds(TEXT("Shop")).Contains(FIntPoint(Point.X,Point.Y));
}
bool SAC2GameUI::ShopButton(FName Name) const
{
    const FString Key=Name.ToString();
    return Key.StartsWith(TEXT("Offer")) || Key.StartsWith(TEXT("Shop")) || Name==TEXT("PurchaseButton");
}

// Retail Shop/Sale ElementDesc templates in local_English.dat 22000005.
// The server owns prices, identities, payment and inventory changes.
void SAC2GameUI::BuildShop()
{
    if(!Mode.IsValid() || !Mode->Network || Mode->Network->ShopVendor.IsEmpty())return;
    auto& Net=*Mode->Network;
    const auto T=[](const TCHAR* Name){return FAC2UILayout::Token(Name);};
    Native.Text.Add(T(TEXT("ShopName")),Net.ObjectName(Net.ShopVendor));
    Native.Text.Add(T(TEXT("ShopDesc")),TEXT("Select stock to purchase. Drag a backpack item here to sell."));
    Native.Visibility.Add(T(TEXT("SortText")),false);
    const int32 X=FMath::Max(0,FMath::Min(32,FMath::RoundToInt(Size.X)-384));
    const int32 Y=FMath::Max(0,FMath::RoundToInt((Size.Y-569)*.5f));
    const FIntRect Window(X,Y,X+384,Y+569),Clip(0,0,FMath::RoundToInt(Size.X),FMath::RoundToInt(Size.Y));
    Native.States.Add(T(TEXT("PurchaseButton")),T(Net.ShopOffers.IsValidIndex(SelectedOffer) &&
        Net.ShopOffers[SelectedOffer].Price<=Net.Gold && !Net.Combat && !Net.Dead?TEXT("normal"):TEXT("ghosted")));
    Native.Template(0x22000005,TEXT("Shop"),Window,Clip);
    Bind(TEXT("ShopCloseButton"),TEXT("ShopClose"));
    Bind(TEXT("PurchaseButton"),TEXT("ShopBuy"));
    const auto* Root=Native.Find(TEXT("Shop"));
    const auto* List=Root?Native.Descendant(*Root,TEXT("ItemList")):nullptr;
    if(!List)return;
    // Sale's native authored row height is 42 physical pixels.
    const FIntRect ListRect=List->Rect,ListClip=List->Clip;
    const int32 Rows=FMath::Max(1,ListRect.Height()/42);
    ShopPage=FMath::Clamp(ShopPage,0,FMath::Max(0,(Net.ShopOffers.Num()-1)/Rows));
    Bind(TEXT("ShopPrevButton"),TEXT("ShopPrev"));Bind(TEXT("ShopNextButton"),TEXT("ShopNext"));
    for(int32 Row=0;Row<Rows;++Row)
    {
        const int32 Index=ShopPage*Rows+Row;if(!Net.ShopOffers.IsValidIndex(Index))break;
        const auto& Offer=Net.ShopOffers[Index];
        Native.States.Add(T(TEXT("Sale")),T(Index==SelectedOffer?TEXT("selected"):TEXT("stateless")));
        Native.Text.Add(T(TEXT("ProductDesc")),Offer.Name);
        Native.Text.Add(T(TEXT("CostQuantity")),FString::Printf(TEXT("%.0f"),Offer.Price));
        Native.Text.Add(T(TEXT("StockQuantity")),FString::FromInt(Offer.Stock));
        const FIntRect Rect(ListRect.Min.X,ListRect.Min.Y+Row*42,ListRect.Max.X,ListRect.Min.Y+(Row+1)*42);
        const int32 First=Native.Nodes.Num();
        Native.Template(0x22000005,TEXT("Sale"),Rect,ListClip);
        Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Offer.Currency);
        const auto* Currency=Mode->CharacterCatalog.Items.Find(Offer.Currency);
        for(int32 I=First;I<Native.Nodes.Num();++I)
        {
            const auto& N=Native.Nodes[I];
            if(N.Id==T(TEXT("ProductIcon")))Icons.Add({N.Rect,N.Clip,Offer.Icon});
            if(N.Id==T(TEXT("CostIcon")) && Currency)Icons.Add({N.Rect,N.Clip,Currency->Icon});
        }
        const FName Key(*FString::Printf(TEXT("Offer%d"),Index));
        Buttons.Add(Key,FSlateRect(Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y));
        Hints.Add(Key,FString::Printf(TEXT("%s — %.0f gold. Select and Purchase, or double-click."),*Offer.Name,Offer.Price));
    }
    Native.Text.Remove(T(TEXT("ProductDesc")));Native.Text.Remove(T(TEXT("CostQuantity")));Native.Text.Remove(T(TEXT("StockQuantity")));
}
bool SAC2GameUI::ShopAction(FName Name)
{
    if(!Mode.IsValid() || !Mode->Network)return false;
    const FString Key=Name.ToString();
    if(Key.StartsWith(TEXT("Offer"))){SelectedOffer=FCString::Atoi(*Key.Mid(5));return true;}
    if(Name==TEXT("ShopBuy")){Mode->Network->BuyOffer(SelectedOffer);return true;}
    if(Name==TEXT("ShopClose")){Mode->Network->CloseShop();return true;}
    if(Name==TEXT("ShopPrev")){ShopPage=FMath::Max(0,ShopPage-1);return true;}
    if(Name==TEXT("ShopNext")){++ShopPage;return true;}
    return false;
}

bool SAC2GameUI::SmokeShopInput(int32 Step,FString& Error)
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    static FIntPoint SavedInventoryOffset;
    const uint32 InventoryRoot=FAC2UILayout::Token(TEXT("RightSidePanel"));
    if(Step==0){
        // A user's saved inventory may be underneath the shop. Exercise an
        // unobscured drag, without changing their persisted window position.
        SavedInventoryOffset=Native.RootOffsets.FindRef(InventoryRoot);
        Native.RootOffsets.Remove(InventoryRoot);Rebuild();
    }
    auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));
    if(!Window)return Fail(TEXT("Shop window unavailable"));
    auto Point=[&](FSlateRect R){return GetCachedGeometry().LocalToAbsolute(FVector2D((R.Left+R.Right)*.5,(R.Top+R.Bottom)*.5)*Scale);};
    auto Click=[&](FName Key){
        const auto* R=Buttons.Find(Key);if(!R)return false;const auto P=Point(*R);
        auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    if(Step==0)
    {
        if(!Native.Find(TEXT("Shop")) || !Buttons.Contains(TEXT("Offer0")) || !Native.Find(TEXT("ShopName")))return Fail(TEXT("Native Shop/Sale templates missing"));
        auto P=Mode->Characters.Active();const auto* Item=P?P->Items.FindByPredicate([](const auto& I){return I.Definition==0x470007ed && !I.Equipped;}):nullptr;
        if(!Item)return Fail(TEXT("Sale fixture missing"));
        const auto* Rect=Buttons.Find(*FString::Printf(TEXT("Cell%d"),Item->Cell));if(!Rect)return Fail(TEXT("Sale cell not visible"));
        const auto ShopRect=Native.Bounds(TEXT("Shop"));
        const FVector2D Start=Point(*Rect),End=Point(FSlateRect(ShopRect.Min.X,ShopRect.Min.Y,ShopRect.Max.X,ShopRect.Max.Y));
        auto Path=App.LocateWindowUnderMouse(Start,{Window.ToSharedRef()},false);if(!Path.IsValid())return Fail(TEXT("Sale drag path missing"));
        App.RoutePointerDownEvent(Path,FPointerEvent(0,Start,Start,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerMoveEvent(Path,FPointerEvent(0,End,Start,{EKeys::LeftMouseButton},FKey(),0,FModifierKeysState()),false);
        App.RoutePointerUpEvent(Path,FPointerEvent(0,End,Start,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
    }
    if(Step==1)
    {
        const int32 Index=Mode->Network->ShopOffers.IndexOfByPredicate([](const auto& O){return O.Entity==0x47001744;});
        if(Index==INDEX_NONE || !Click(*FString::Printf(TEXT("Offer%d"),Index)))return Fail(TEXT("Offer selection failed"));
        Rebuild();if(!Click(TEXT("PurchaseButton")))return Fail(TEXT("Native purchase button unavailable"));
    }
    if(Step==2){
        const bool Closed=Click(TEXT("ShopCloseButton"));
        Native.RootOffsets.Add(InventoryRoot,SavedInventoryOffset);Rebuild();
        if(!Closed)return Fail(TEXT("Native close button unavailable"));
    }
    UE_LOG(LogTemp,Display,TEXT("AC2 shop pointer step %d passed"),Step);return true;
}
