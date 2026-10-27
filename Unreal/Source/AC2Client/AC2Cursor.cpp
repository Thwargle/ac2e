#include "AC2Cursor.h"
#include "Rendering/DrawElements.h"

void SAC2Cursor::Construct(const FArguments& Args)
{
    Icon=Args._Icon;
    FString Error;
    if(!Assets.Open(Args._Directory,Error) || !Images.Open(Args._Directory,Error))
    { UE_LOG(LogTemp,Error,TEXT("Native cursors: %s"),*Error);return; }
    Images.LoadImage(0x41000870,Assets);
    Images.LoadImage(0x4100751e,Assets);
    for(const auto& Layout:Images.Layouts)
        for(const auto& Art:Layout.Value.Art)
            if(Art.Kind==10) Images.LoadImage(Art.Image,Assets);
    SetVisibility(EVisibility::HitTestInvisible);
}
int32 SAC2Cursor::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,
    FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const
{
    auto Spec=Icon.Get();
    auto* Brush=Images.Brush(Spec.Image);
    if(!Brush){Spec={};Brush=Images.Brush(Spec.Image);}
    if(Brush)
    {
        // Software cursor position is the click point. Preserve DAT pixels at any desktop DPI.
        const float PixelScale=1.f/FMath::Max(.1f,G.GetAccumulatedLayoutTransform().GetScale());
        const auto Pixels=G.MakeChild(Brush->ImageSize,FSlateLayoutTransform(PixelScale));
        FSlateDrawElement::MakeBox(Out,++Layer,Pixels.ToPaintGeometry(Brush->ImageSize,
            FSlateLayoutTransform(-FVector2D(Spec.Hotspot))),Brush);
    }
    return Layer;
}
