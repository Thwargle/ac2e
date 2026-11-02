#include "AC2GameUI.h"
#include "AC2GameMode.h"
#include "Framework/Application/SlateApplication.h"

namespace {
constexpr uint32 IngredientWindow=0x0e036a6c,IngredientClose=0xacae3cde;
uint32 Num(const TSharedPtr<FJsonObject>& J,const TCHAR* K){double N=0;if(J)J->TryGetNumberField(K,N);return uint32(N);}
uint64 WideNum(const TSharedPtr<FJsonObject>& J,const TCHAR* K){double N=0;if(J)J->TryGetNumberField(K,N);return uint64(N);}
FString Str(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString S;if(J)J->TryGetStringField(K,S);return S;}
uint64 Curve(const TSharedPtr<FJsonObject>& J,const TCHAR* K,uint32 Level){const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!J||!J->TryGetArrayField(K,A)||A->IsEmpty())return 0;int32 I=A->Num()==1?0:int32(Level);return A->IsValidIndex(I)?uint64((*A)[I]->AsNumber()):0;}
}
FString SAC2GameUI::CraftItemName(uint32 Definition)
{
    Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,Definition);
    const auto* Item=Mode->CharacterCatalog.Items.Find(Definition);return Item?Item->Name:TEXT("Crafting material");
}
bool SAC2GameUI::CraftOwnsPointer(FVector2D Point) const
{
    const auto* Root=Native.Find(IngredientWindow);return Root&&Root->Clip.Contains(FIntPoint(Point.X,Point.Y));
}
void SAC2GameUI::BuildCrafting()
{
    const auto* Net=Mode->Network.Get();const auto* Panel=Native.Find(0x839fa006);if(!Net||!Panel)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};const FIntRect Box=Panel->Rect,Clip=Panel->Clip;const uint32 Parent=Panel->Id;
    const auto Skill=Net->CraftSkills.FindRef(CraftSkill),Progress=Net->CraftProgress.FindRef(CraftSkill);
    const uint32 Level=Num(Progress,TEXT("level"));const uint64 Cost=Level<Num(Skill,TEXT("cap"))?Curve(Skill,TEXT("xp"),Level+1)-Curve(Skill,TEXT("xp"),Level):0;
    const bool CanRaise=Progress&&Level<Num(Skill,TEXT("cap"))&&Cost>0&&WideNum(Progress,TEXT("available"))>=Cost&&!Net->Combat&&!Net->Dead;
    Native.States.Add(T(TEXT("CraftPage")),T(TEXT("recipeview")));
    // The native Raise button exists only in the selected state of this field.
    Native.States.Add(0xcd54ce86,T(CraftProfessions?TEXT("stateless"):TEXT("selected")));
    Native.States.Add(T(TEXT("RaiseCraftButton")),T(CanRaise?TEXT("normal"):TEXT("ghosted")));
    Native.Text.Add(T(TEXT("Name")),CraftProfessions?TEXT("Crafting professions"):Str(Skill,TEXT("name")));
    Native.Text.Add(T(TEXT("SelectedName")),CraftProfessions?TEXT("Choose profession"):Str(Skill,TEXT("name")));
    Native.Template(0x22000001,TEXT("CraftPage"),FIntRect(Box.Min+FIntPoint(0,21),Box.Max),Clip,Parent);
    Native.States.Remove(0xcd54ce86);
    Native.Text.Remove(T(TEXT("Name")));
    const auto* List=Native.Find(TEXT("RecipeList"));if(!List)return;
    FIntRect Area=List->Rect,ListClip=List->Clip;
    auto Button=[&](FName Key,const FIntRect& R,const FString& Hint){Buttons.Add(Key,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));Hints.Add(Key,Hint);};
    auto Label=[&](const FIntRect& R,const FString& Text){const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),R,Clip);if(Native.Nodes.IsValidIndex(First))Native.Nodes[First].Text=Text;};
    // Profession navigation stays available even when a discipline has no recipes.
    FIntRect Nav(FIntPoint(Area.Min.X,Area.Min.Y),FIntPoint(Area.Max.X,Area.Min.Y+20));
    Label(Nav,CraftProfessions?TEXT("Crafting professions"):TEXT("< All professions"));Button(TEXT("CraftBrowse"),Nav,TEXT("Select a crafting profession"));Area.Min.Y+=21;
    TArray<uint32> Rows;
    if(CraftProfessions){Net->CraftProgress.GetKeys(Rows);Rows.Sort([&](uint32 A,uint32 B){return Str(Net->CraftSkills.FindRef(A),TEXT("name"))<Str(Net->CraftSkills.FindRef(B),TEXT("name"));});}
    else for(uint32 Id:Net->CraftRecipes){auto R=Net->CraftCatalog.FindRef(Id);if(R&&Num(R,TEXT("skill"))==CraftSkill)Rows.Add(Id);}
    if(!CraftProfessions)Rows.Sort([&](uint32 A,uint32 B){return Str(Net->CraftCatalog.FindRef(A),TEXT("name"))<Str(Net->CraftCatalog.FindRef(B),TEXT("name"));});
    const int32 Count=FMath::Max(1,(Area.Height()-47)/43),Pages=FMath::Max(1,FMath::DivideAndRoundUp(Rows.Num(),Count));CraftPage=FMath::Clamp(CraftPage,0,Pages-1);
    for(int32 Row=0;Row<Count&&CraftPage*Count+Row<Rows.Num();++Row){uint32 Id=Rows[CraftPage*Count+Row];auto R=CraftProfessions?Net->CraftSkills.FindRef(Id):Net->CraftCatalog.FindRef(Id);
        FString Name=Str(R,TEXT("name"));if(CraftProfessions)Name+=FString::Printf(TEXT(" - %u"),Num(Net->CraftProgress.FindRef(Id),TEXT("level")));
        Native.Text.Add(T(TEXT("Name")),Name);Native.States.Add(T(TEXT("RecipeTemplate")),T(Net->OpenCraftRecipe==Id?TEXT("selected"):TEXT("stateless")));
        const FIntRect Rect(Area.Min+FIntPoint(0,Row*43),FIntPoint(Area.Max.X,Area.Min.Y+(Row+1)*43));const int32 First=Native.Nodes.Num();
        Native.Template(0x22000001,TEXT("RecipeTemplate"),Rect,ListClip);
        for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==T(TEXT("RecipeIcon")))Icons.Add({Native.Nodes[I].Rect,Native.Nodes[I].Clip,Num(R,TEXT("icon"))});
        const FName Key(*(CraftProfessions?FString::Printf(TEXT("CraftProfession%u"),Id):FString::Printf(TEXT("CraftRecipe%u"),Id)));Button(Key,Rect,Name+TEXT("\n")+Str(R,TEXT("description")));
    }
    Native.Text.Remove(T(TEXT("Name")));Native.States.Remove(T(TEXT("RecipeTemplate")));
    const FIntRect Prev(FIntPoint(Area.Min.X,Area.Max.Y-44),FIntPoint(Area.Min.X+60,Area.Max.Y-24));
    const FIntRect Next(FIntPoint(Area.Max.X-60,Area.Max.Y-44),FIntPoint(Area.Max.X,Area.Max.Y-24));
    Label(Prev,TEXT("< Prev"));Label(Next,TEXT("Next >"));Button(TEXT("CraftPrev"),Prev,TEXT("Previous page"));Button(TEXT("CraftNext"),Next,TEXT("Next page"));
    Label(FIntRect(FIntPoint(Area.Min.X+65,Area.Max.Y-44),FIntPoint(Area.Max.X-60,Area.Max.Y-24)),FString::Printf(TEXT("%d / %d"),CraftPage+1,Pages));
    FString Status=CraftProfessions?TEXT("Select a profession to view recipes."):FString::Printf(TEXT("Rank %u   XP %llu   Next %llu"),Level,WideNum(Progress,TEXT("available")),Cost);
    Label(FIntRect(FIntPoint(Area.Min.X,Area.Max.Y-23),Area.Max),Status);
    if(!CraftProfessions&&Rows.IsEmpty())Label(FIntRect(Area.Min,FIntPoint(Area.Max.X,Area.Min.Y+100)),CraftSkill==0x6f002560?TEXT("Select a corpse from your kill, then use a hunting knife from your backpack."):CraftSkill==0x6f00282d?TEXT("Enchantment extraction and totem application are not implemented yet."):TEXT("Create a map using Surveying. Find the site, use the map, then use the matching tool on the deposit."));
    Bind(TEXT("RaiseCraftButton"),TEXT("CraftRaise"));Hints.Add(TEXT("RaiseCraftButton"),CanRaise?TEXT("Spend crafting XP to raise this profession."):TEXT("Earn crafting XP by making suitable items. Your level and XP are saved."));
}
void SAC2GameUI::BuildIngredients()
{
    auto* Net=Mode->Network.Get();if(!Net||!Net->OpenCraftRecipe)return;auto Recipe=Net->CraftCatalog.FindRef(Net->OpenCraftRecipe);if(!Recipe)return;
    const auto Inputs=Net->CraftInputs();const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};const FString Name=Str(Recipe,TEXT("name"));
    const uint32 Skill=Num(Recipe,TEXT("skill")),Level=Num(Net->CraftProgress.FindRef(Skill),TEXT("level"));
    const int64 Difficulty=Curve(Recipe,TEXT("difficulty"),Net->CraftSpinner),Delta=int64(Level)-Difficulty;
    const int32 ChanceIndex=int32(FMath::Clamp<int64>(Delta,-500,500))+500;
    const int32 Chance=Skill==0x6f0028d7||Str(Recipe,TEXT("kind"))==TEXT("refine")||Str(Recipe,TEXT("kind"))==TEXT("dye")||Str(Recipe,TEXT("kind"))==TEXT("survey")?100:Net->CraftSuccess.IsValidIndex(ChanceIndex)?int32(Net->CraftSuccess[ChanceIndex]):0;
    FString Title=Name;while(Title.Len()>1&&TitleFont&&TitleFont->Width(Title+TEXT("..."))>202)Title.LeftChopInline(1);
    if(Title!=Name)Title+=TEXT("...");
    Native.Text.Add(0x5cabf830,Title);Native.Text.Add(0x2b917f5b,FString::Printf(TEXT("Difficulty %lld   Base chance %d%%"),Difficulty,Chance));
    Native.Text.Add(0x3257a864,FString::Printf(TEXT("Level %u   Cost %llu gold"),Net->CraftSpinner,Curve(Recipe,TEXT("cost"),Net->CraftSpinner)));
    Native.Text.Add(0x5dcd7380,Net->Combat?TEXT("Return to peace mode."):Net->CraftNotice);
    // The server adds an equipped creation tool's bonus. Base chance alone must
    // not disable an attempt that becomes possible with the player's tool.
    Native.States.Add(T(TEXT("CraftButton")),T(Net->CanCraft()?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("AutoPopulateButton")),T(!Net->CraftPending&&!Net->Combat&&!Net->Dead?TEXT("normal"):TEXT("ghosted")));
    Native.Visibility.Add(T(TEXT("SpinnerField")),false);
    // The original frame is fixed at 256 pixels; center its actual native size.
    const int32 Height=FMath::Max(300,220+Inputs.Num()*38);const FIntPoint Origin(FMath::Max(0,FMath::RoundToInt((Size.X-256)*.5)),FMath::Max(0,FMath::RoundToInt((Size.Y-Height)*.5)));
    Native.Template(0x22000005,IngredientWindow,FIntRect(Origin,Origin+FIntPoint(256,Height)),FIntRect(0,0,Size.X,Size.Y));
    const auto* HeaderClose=Native.Find(IngredientClose);const int32 HeaderRight=HeaderClose?HeaderClose->Rect.Min.X-4:Origin.X+218;
    for(auto& Node:Native.Nodes)if(Node.Text==Title&&Node.Rect.Min.Y<Origin.Y+35){
        Node.Rect.Max.X=FMath::Min(Node.Rect.Max.X,HeaderRight);Node.Clip=Node.Rect;
        const auto Font=Fonts.Get(Node.Attr(TEXT("font"),0x4200001d));FString Fit=Name;
        if(Font){while(Fit.Len()>1&&Font->Width(Fit+TEXT("..."))>Node.Rect.Width())Fit.LeftChopInline(1);}
        Node.Text=Fit==Name?Name:Fit+TEXT("...");
    }
    for(auto& Node:Native.Nodes)if(Node.Id==0x2b917f5b){
        auto Color=[&](const TCHAR* Key,float Value){uint32 Bits;FMemory::Memcpy(&Bits,&Value,sizeof(Bits));Node.Attributes.Add(T(Key),Bits);};
        Color(TEXT("font_color_red"),Chance<50?1.f:.55f);Color(TEXT("font_color_green"),Chance<50?.35f:1.f);Color(TEXT("font_color_blue"),.35f);
    }
    Bind(TEXT("CraftButton"),TEXT("CraftExecute"));Bind(TEXT("AutoPopulateButton"),TEXT("CraftFill"));
    if(const auto* Close=Native.Find(IngredientClose)){Buttons.Add(TEXT("CraftClose"),FSlateRect(Close->Clip.Min.X,Close->Clip.Min.Y,Close->Clip.Max.X,Close->Clip.Max.Y));Hints.Add(TEXT("CraftClose"),TEXT("Close recipe"));}
    Hints.Add(TEXT("CraftButton"),TEXT("Attempt this recipe. Base chance excludes the bonus from an equipped creation tool. Failed attempts consume materials. Rejected requests consume nothing."));Hints.Add(TEXT("AutoPopulateButton"),TEXT("Fill ingredient slots from your backpack."));
    const auto* List=Native.Find(TEXT("IngredientListBox"));if(!List)return;FIntRect Area=List->Rect,Clip=List->Clip;
    auto LevelButton=[&](const TCHAR* Key,const TCHAR* Text,int32 X){FIntRect R(FIntPoint(X,Area.Min.Y),FIntPoint(X+38,Area.Min.Y+20));int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),R,Clip);if(Native.Nodes.IsValidIndex(First))Native.Nodes[First].Text=Text;Buttons.Add(Key,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));};
    if(Num(Recipe,TEXT("maximum"))>Num(Recipe,TEXT("minimum"))){LevelButton(TEXT("CraftLevelDown"),TEXT("[-]"),Area.Min.X+10);LevelButton(TEXT("CraftLevelUp"),TEXT("[+]"),Area.Max.X-48);Area.Min.Y+=22;}
    int32 Row=0;for(const auto& F:Inputs){int32 Index=F.Ordinal-1;FString IngredientName=F.Name;
        const auto* Player=Mode->Characters.Active();const auto* Selected=Player?Player->Items.FindByPredicate([&](const auto& I){return I.NetworkId==Net->CraftIngredients[Index];}):nullptr;
        if(Selected)IngredientName=CraftItemName(Selected->Definition);else if(IngredientName.IsEmpty()&&!F.Choices.IsEmpty())IngredientName=CraftItemName(F.Choices[0]);
        FString Text=FString::Printf(TEXT("%d x %s"),F.Quantity,*IngredientName);Text+=Net->CraftIngredients[Index].IsEmpty()?TEXT("\nMissing"):Net->CraftAccepted[Index]?TEXT("\nReady - click to change"):TEXT("\nChecking...");
        Native.Text.Add(T(TEXT("IngredientLabel")),Text);int32 First=Native.Nodes.Num();Native.Template(0x22000005,TEXT("IngredientTemplate"),FIntRect(Area.Min+FIntPoint(0,Row*38),FIntPoint(Area.Max.X,Area.Min.Y+(Row+1)*38)),Clip);
        for(int32 I=First;I<Native.Nodes.Num();++I){auto& Node=Native.Nodes[I];if(Node.Id==T(TEXT("IngredientLabel"))){Node.Rect.Max.X=Area.Max.X-5;Node.Clip=Node.Rect;Node.Clip.Clip(Clip);}if(Node.Id==T(TEXT("ItemIcon"))&&!Net->CraftIngredients[Index].IsEmpty())for(const auto& L:Net->ItemIconLayers(Net->CraftIngredients[Index],*Assets))Icons.Add({Node.Rect,Node.Clip,L.Image,1,FLinearColor(FColor(L.Color))});}
        const FName Key(*FString::Printf(TEXT("CraftIngredient%u"),F.Ordinal));Buttons.Add(Key,FSlateRect(Area.Min.X,Area.Min.Y+Row*38,Area.Max.X,Area.Min.Y+(Row+1)*38));Hints.Add(Key,TEXT("Click to choose another matching item from your backpack. Required quantities may span stacks."));++Row;
    }Native.Text.Remove(T(TEXT("IngredientLabel")));
}
bool SAC2GameUI::CraftAction(FName Name)
{
    auto* Net=Mode->Network.Get();if(!Net)return false;const FString Key=Name.ToString();
    if(Key.StartsWith(TEXT("CraftIngredient"))){Net->CycleCraftIngredient(FCString::Atoi(*Key.Mid(15)));return true;}
    if(Key.StartsWith(TEXT("CraftRecipe"))){Net->OpenCraft(FCString::Strtoui64(*Key.Mid(11),nullptr,10));return true;}
    if(Key.StartsWith(TEXT("CraftProfession"))){CraftSkill=FCString::Strtoui64(*Key.Mid(15),nullptr,10);CraftProfessions=false;CraftPage=0;return true;}
    if(Name==TEXT("CraftBrowse")){CraftProfessions=!CraftProfessions;CraftPage=0;return true;}
    if(Name==TEXT("CraftPrev")){CraftPage=FMath::Max(0,CraftPage-1);return true;}
    if(Name==TEXT("CraftNext")){++CraftPage;return true;}
    if(Name==TEXT("CraftRaise")){Net->RaiseCraft(CraftSkill);return true;}
    if(Name==TEXT("CraftLevelDown")){Net->CraftLevel(-1);return true;}
    if(Name==TEXT("CraftLevelUp")){Net->CraftLevel(1);return true;}
    if(Name==TEXT("CraftExecute")){Net->ExecuteCraft();return true;}
    if(Name==TEXT("CraftFill")){Net->PopulateCraft();return true;}
    if(Name==TEXT("CraftClose")){Net->CloseCraft();return true;}return false;
}

bool SAC2GameUI::SmokeCraftInput(int32 Step,FString& Error)
{
    auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
    auto& Net=*Mode->Network;auto& App=FSlateApplication::Get();auto Window=App.FindWidgetWindow(SharedThis(this));
    if(!Window)return Fail(TEXT("Craft window unavailable"));
    auto Click=[&](FName Key){
        const auto* R=Buttons.Find(Key);if(!R)return false;
        const FVector2D Local((R->Left+R->Right)*.5,(R->Top+R->Bottom)*.5);
        if(!AllowsPanelButton(Key,Local))return false;
        const auto P=GetCachedGeometry().LocalToAbsolute(Local*Scale);
        auto Path=App.LocateWindowUnderMouse(P,{Window.ToSharedRef()},false);if(!Path.IsValid())return false;
        App.RoutePointerDownEvent(Path,FPointerEvent(0,P,P,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,P,P,{},EKeys::LeftMouseButton,0,FModifierKeysState()));return true;
    };
    if(Step==0){
        if(!ShowSkills && !Click(TEXT("SkillButton")))return Fail(TEXT("Skills button unavailable"));Rebuild();
        if(!Click(TEXT("CraftTab")) || SkillTab!=4)return Fail(TEXT("Craft tab did not open"));Rebuild();
        if(!Buttons.Contains(*FString::Printf(TEXT("CraftRecipe%u"),0x6f001f17u)) || Net.CraftRecipes.Num()<250)
            return Fail(TEXT("Granted recipe rows missing"));
    }
    if(Step==1){
        if(!Click(*FString::Printf(TEXT("CraftRecipe%u"),0x6f001f17u)) || Net.OpenCraftRecipe!=0x6f001f17u)
            return Fail(TEXT("Recipe click did not open ingredient window"));Rebuild();
        const auto* Root=Native.Find(IngredientWindow);const auto* Close=Native.Find(IngredientClose);
        if(!Root || !Close || !Native.Find(TEXT("IngredientLabel")))return Fail(TEXT("Native ingredient frame/rows missing"));
        if(Close->Rect.Min.X<Root->Rect.Min.X || Close->Rect.Max.X>Root->Rect.Max.X || Close->Rect.Max.Y>Root->Rect.Max.Y)
            return Fail(TEXT("Ingredient close control is outside frame"));
        const FVector2D Center(Root->Rect.Min.X+128,Root->Rect.Min.Y+80);
        if(AllowsPanelButton(TEXT("CraftRecipe1862270743"),Center))return Fail(TEXT("Recipe window allows clicks through to underlying rows"));
    }
    if(Step==2){
        if(!Net.CanCraft())return Fail(TEXT("Server did not approve both owned ingredients"));
        for(const auto& Icon:Icons)if(Icon.Image==0x4100034b && CraftOwnsPointer(FVector2D(Icon.Rect.Min)))
            return Fail(TEXT("Ingredient uses unknown-item placeholder instead of native artwork"));
        if(!Click(TEXT("CraftButton")) || !Net.CraftPending)return Fail(TEXT("Craft button did not send request"));
        if(Net.CanCraft())return Fail(TEXT("Craft remained enabled while request pending"));
    }
    if(Step==3){
        if(Net.CraftPending || !Net.CraftNotice.StartsWith(TEXT("Crafted!")))return Fail(TEXT("Craft completion missing"));
        if(Net.CanCraft() || !Net.CraftIngredients[0].IsEmpty() || !Net.CraftIngredients[1].IsEmpty())return Fail(TEXT("Consumed ingredients remain enabled"));
        const auto* Player=Mode->Characters.Active();if(!Player || !Player->Items.ContainsByPredicate([](const auto& I){return I.Definition==0x47001745;}))
            return Fail(TEXT("Crafted potion did not arrive in backpack"));
        const auto* Notice=Native.Find(0x5dcd7380);
        const auto Font=Notice?Fonts.Get(Notice->Attr(TEXT("font"),0x4200000c)):nullptr;
        if(!Font || Font->Width(Net.CraftNotice)>Notice->Rect.Width())return Fail(TEXT("Craft status text clips the ingredient window"));
    }
    if(Step==4){if(!Click(TEXT("CraftClose")) || Net.OpenCraftRecipe)return Fail(TEXT("Ingredient close button did not close"));}
    if(Step==5){
        if(!Click(TEXT("CraftBrowse")) || !CraftProfessions)return Fail(TEXT("Profession browser did not open"));Rebuild();
        if(!Click(*FString::Printf(TEXT("CraftProfession%u"),0x6f0023a8u)) || CraftSkill!=0x6f0023a8 || CraftProfessions)
            return Fail(TEXT("Human armor profession did not open"));
        Rebuild();
        const auto* Raise=Native.Find(TEXT("RaiseCraftButton"));
        if(!Raise || Raise->Clip.Width()<=0 || Raise->Clip.Height()<=0 ||
            (Raise->State!=FAC2UILayout::Token(TEXT("ghosted"))&&!Buttons.Contains(TEXT("RaiseCraftButton"))))
            return Fail(TEXT("Profession rank control is missing or clipped"));
    }
    if(Step==6){
        bool Opened=false;
        for(uint32 Id:Net.CraftRecipes){auto R=Net.CraftCatalog.FindRef(Id);const FName Key(*FString::Printf(TEXT("CraftRecipe%u"),Id));
            if(R&&Num(R,TEXT("skill"))==CraftSkill&&Str(R,TEXT("kind"))==TEXT("produce")&&Num(R,TEXT("maximum"))>Num(R,TEXT("minimum"))&&Buttons.Contains(Key)){
                Opened=Click(Key)&&Net.OpenCraftRecipe==Id;break;
            }
        }if(!Opened)return Fail(TEXT("Profession recipe did not open"));
    }
    if(Step==7){const uint32 Before=Net.CraftSpinner;if(!Click(TEXT("CraftLevelUp"))||Net.CraftSpinner!=Before+1)return Fail(TEXT("Recipe level control did not update"));}
    if(Step==8){if(!Click(TEXT("CraftClose"))||Net.OpenCraftRecipe)return Fail(TEXT("Profession recipe did not close"));}
    Rebuild();
    if(Step==1 || Step==7){
        const auto* Title=Native.Find(0x5cabf830);const auto* Close=Native.Find(IngredientClose);
        const auto Font=Title?Fonts.Get(Title->Attr(TEXT("font"),0x4200001d)):nullptr;
        if(!Title || !Close || !Font || Font->Width(Title->Text)>Title->Rect.Width() || Title->Rect.Max.X>=Close->Rect.Min.X)
            return Fail(TEXT("Recipe title overlaps the close control"));
    }
    return true;
}
