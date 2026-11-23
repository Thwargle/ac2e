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
uint32 SAC2GameUI::RecipeIcon(uint32 Recipe) const
{
    const auto* Net=Mode->Network.Get();if(!Net)return 0;
    const auto R=Net->CraftCatalog.FindRef(Recipe);if(!R)return 0;
    // gmCraftUI::GetRecipeIcon falls back to the profession for a zero recipe icon.
    const uint32 Icon=Num(R,TEXT("icon"));
    return Icon?Icon:Num(Net->CraftSkills.FindRef(Num(R,TEXT("skill"))),TEXT("icon"));
}
void SAC2GameUI::InspectRecipe(uint32 Recipe)
{
    ExaminedRecipe=Recipe;ExaminedSkill=0;ShowStats=true;RaiseWindow(FAC2UILayout::Token(TEXT("ExaminationField")));ExamineScroll=0;
    SelectedItem=INDEX_NONE;SelectedNetworkItem.Reset();InspectedObject.Reset();InspectedMob.Reset();Rebuild();
}
TArray<FString> SAC2GameUI::RecipeExamination(uint32 Recipe)
{
    // Recipe examination is a data-ID request in retail, not an item instance.
    // Use the recovered catalog delivered with this character's registry, with
    // the same curves as the ingredient window and no validation/use side effects.
    if(!RecipeShortcutAllowed(Recipe))return {TEXT("Recipe unavailable"),TEXT("This recipe is not in the current character's crafting registry.")};
    const auto* Net=Mode->Network.Get();const auto R=Net->CraftCatalog.FindRef(Recipe);
    const auto Skill=Net->CraftSkills.FindRef(Num(R,TEXT("skill")));
    const auto Progress=Net->CraftProgress.FindRef(Num(R,TEXT("skill")));
    const uint32 Min=Num(R,TEXT("minimum")),Max=Num(R,TEXT("maximum"));
    const uint32 Level=Net->OpenCraftRecipe==Recipe?Net->CraftSpinner:Min;
    TArray<FString> Lines={Str(R,TEXT("name")),Str(R,TEXT("description"))};
    if(Skill)Lines.Add(TEXT("Profession: ")+Str(Skill,TEXT("name")));
    if(Progress)Lines.Add(FString::Printf(TEXT("Profession rank: %u"),Num(Progress,TEXT("level"))));
    if(Max>Min)Lines.Add(FString::Printf(TEXT("Recipe levels: %u - %u (showing %u)"),Min,Max,Level));
    Lines.Add(FString::Printf(TEXT("Difficulty: %llu"),Curve(R,TEXT("difficulty"),Level)));
    Lines.Add(FString::Printf(TEXT("Cost: %llu gold"),Curve(R,TEXT("cost"),Level)));
    if(Num(R,TEXT("workshop")))Lines.Add(TEXT("Requires a crafting workshop."));
    const TArray<TSharedPtr<FJsonValue>>* Ingredients=nullptr;
    if(R->TryGetArrayField(TEXT("ingredients"),Ingredients))for(const auto& V:*Ingredients){
        const auto I=V->AsObject();if(!I || Level<Num(I,TEXT("minimum")))continue;
        FString Name=Str(I,TEXT("name"));
        const TArray<TSharedPtr<FJsonValue>>* Choices=nullptr;
        if(Name.IsEmpty() && I->TryGetArrayField(TEXT("choices"),Choices)){
            TArray<FString> Names;for(const auto& D:*Choices)Names.AddUnique(CraftItemName(uint32(D->AsNumber())));
            Name=FString::Join(Names,TEXT(" or "));
        }
        Lines.Add(FString::Printf(TEXT("Ingredient: %llu x %s"),Curve(I,TEXT("quantities"),Level),*Name));
    }
    const TArray<TSharedPtr<FJsonValue>>* Outputs=nullptr;TSet<uint32> Seen;
    if(R->TryGetArrayField(TEXT("outputs"),Outputs))for(const auto& V:*Outputs){
        const uint32 Item=Num(V->AsObject(),TEXT("item"));if(Item && !Seen.Contains(Item)){
            Seen.Add(Item);Lines.Add(TEXT("Possible result: ")+CraftItemName(Item));
        }
    }
    Lines.Add(TEXT("Click the recipe to open its ingredient window. Drag it to the hotbar for quick access."));
    Lines.RemoveAll([](const FString& Line){return Line.IsEmpty();});return Lines;
}
bool SAC2GameUI::CraftOwnsPointer(FVector2D Point) const
{
    return WindowOwns(IngredientWindow,Point);
}
void SAC2GameUI::BuildCrafting()
{
    const auto* Net=Mode->Network.Get();const auto* Panel=Native.Find(0x839fa006);if(!Net||!Panel)return;
    const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};const FIntRect Box=Panel->Rect,Clip=Panel->Clip;const uint32 Parent=Panel->Id;
    if(!Net->CraftSkills.Contains(CraftSkill)){
        TArray<uint32> Known;Net->CraftSkills.GetKeys(Known);Known.Sort();
        CraftSkill=Known.IsEmpty()?0:Known[0];CraftProfessions=true;CraftPage=0;
    }
    const auto Skill=Net->CraftSkills.FindRef(CraftSkill),Progress=Net->CraftProgress.FindRef(CraftSkill);
    const uint32 Level=Num(Progress,TEXT("level"));const uint64 Cost=Level<Num(Skill,TEXT("cap"))?Curve(Skill,TEXT("xp"),Level+1)-Curve(Skill,TEXT("xp"),Level):0;
    const bool CanRaise=Progress&&Level<Num(Skill,TEXT("cap"))&&Cost>0&&WideNum(Progress,TEXT("available"))>=Cost&&!Net->Combat&&!Net->Dead;
    Native.States.Add(T(TEXT("CraftPage")),T(TEXT("recipeview")));
    Native.Visibility.Add(0xe44235ffu,!CraftProfessions);
    // Our profession/back navigation occupies the list header. The native
    // category expander is otherwise stretched over the list on resized pages.
    Native.Visibility.Add(T(TEXT("RecipeHeader")),false);
    // The native Raise button exists only in the selected state of this field.
    Native.States.Add(0xcd54ce86,T(CraftProfessions?TEXT("stateless"):TEXT("selected")));
    Native.States.Add(T(TEXT("RaiseCraftButton")),T(CanRaise?TEXT("normal"):TEXT("ghosted")));
    Native.Text.Add(T(TEXT("Name")),CraftProfessions?TEXT("Crafting professions"):Str(Skill,TEXT("name")));
    Native.Text.Add(T(TEXT("SelectedName")),CraftProfessions?TEXT("Choose profession"):Str(Skill,TEXT("name")));
    Native.Template(0x22000001,TEXT("CraftPage"),FIntRect(Box.Min+FIntPoint(0,21),Box.Max),Clip,Parent);
    Native.Visibility.Remove(0xe44235ffu);
    Native.Visibility.Remove(T(TEXT("RecipeHeader")));
    Native.States.Remove(0xcd54ce86);
    Native.Text.Remove(T(TEXT("Name")));
    const auto* List=Native.Find(TEXT("RecipeList"));if(!List)return;
    FIntRect Area=List->Rect,ListClip=List->Clip;
    auto Button=[&](FName Key,const FIntRect& R,const FString& Hint){Buttons.Add(Key,FSlateRect(R.Min.X,R.Min.Y,R.Max.X,R.Max.Y));Hints.Add(Key,Hint);};
    auto Label=[&](const FIntRect& R,const FString& Text){const int32 First=Native.Nodes.Num();Native.Template(0x22000001,TEXT("ExaminationTextTemplate"),R,Clip);if(Native.Nodes.IsValidIndex(First))Native.Nodes[First].Text=Text;};
    // Profession navigation stays available even when a discipline has no recipes.
    FIntRect Nav(FIntPoint(Area.Min.X,Area.Min.Y),FIntPoint(Area.Max.X,Area.Min.Y+20));
    Label(Nav,CraftProfessions?TEXT("Crafting professions"):TEXT("< All professions"));if(!CraftProfessions)Button(TEXT("CraftBrowse"),Nav,TEXT("Select a crafting profession"));Area.Min.Y+=21;
    TArray<uint32> Rows;
    if(CraftProfessions){Net->CraftSkills.GetKeys(Rows);Rows.Sort([&](uint32 A,uint32 B){return Str(Net->CraftSkills.FindRef(A),TEXT("name"))<Str(Net->CraftSkills.FindRef(B),TEXT("name"));});}
    else for(uint32 Id:Net->CraftRecipes){auto R=Net->CraftCatalog.FindRef(Id);if(R&&Num(R,TEXT("skill"))==CraftSkill)Rows.Add(Id);}
    if(!CraftProfessions)Rows.Sort([&](uint32 A,uint32 B){return Str(Net->CraftCatalog.FindRef(A),TEXT("name"))<Str(Net->CraftCatalog.FindRef(B),TEXT("name"));});
    const int32 Count=FMath::Max(1,(Area.Height()-47)/43),Pages=FMath::Max(1,FMath::DivideAndRoundUp(Rows.Num(),Count));CraftPage=FMath::Clamp(CraftPage,0,Pages-1);
    JournalScroll=CraftPage;JournalMaxScroll=Pages-1;
    UpdateScrollbar(List->Attributes.FindRef(0x2c1a95c1),JournalScroll,JournalMaxScroll,JournalBar,JournalThumb,TEXT("SkillScrollUp"),TEXT("SkillScrollDown"));
    for(int32 Row=0;Row<Count&&CraftPage*Count+Row<Rows.Num();++Row){uint32 Id=Rows[CraftPage*Count+Row];auto R=CraftProfessions?Net->CraftSkills.FindRef(Id):Net->CraftCatalog.FindRef(Id);
        FString Name=Str(R,TEXT("name"));if(CraftProfessions)Name+=FString::Printf(TEXT(" - %u"),Num(Net->CraftProgress.FindRef(Id),TEXT("level")));
        Native.Text.Add(T(TEXT("Name")),Name);Native.States.Add(T(TEXT("RecipeTemplate")),T(Net->OpenCraftRecipe==Id?TEXT("selected"):TEXT("stateless")));
        const FIntRect Rect(Area.Min+FIntPoint(0,Row*43),FIntPoint(Area.Max.X,Area.Min.Y+(Row+1)*43));const int32 First=Native.Nodes.Num();
        Native.Template(0x22000001,TEXT("RecipeTemplate"),Rect,ListClip);
        for(int32 I=First;I<Native.Nodes.Num();++I)if(Native.Nodes[I].Id==T(TEXT("RecipeIcon")))Icons.Add({Native.Nodes[I].Rect,Native.Nodes[I].Clip,CraftProfessions?Num(R,TEXT("icon")):RecipeIcon(Id)});
        const FName Key(*(CraftProfessions?FString::Printf(TEXT("CraftProfession%u"),Id):FString::Printf(TEXT("CraftRecipe%u"),Id)));
        Button(Key,Rect,Name+TEXT("\n")+Str(R,TEXT("description"))+(CraftProfessions?TEXT(""):TEXT("\nClick to open; right-click to examine; drag to the hotbar.")));
    }
    Native.Text.Remove(T(TEXT("Name")));Native.States.Remove(T(TEXT("RecipeTemplate")));
    const FIntRect Prev(FIntPoint(Area.Min.X,Area.Max.Y-44),FIntPoint(Area.Min.X+60,Area.Max.Y-24));
    const FIntRect Next(FIntPoint(Area.Max.X-60,Area.Max.Y-44),FIntPoint(Area.Max.X,Area.Max.Y-24));
    Label(Prev,TEXT("< Prev"));Label(Next,TEXT("Next >"));Button(TEXT("CraftPrev"),Prev,TEXT("Previous page"));Button(TEXT("CraftNext"),Next,TEXT("Next page"));
    Label(FIntRect(FIntPoint(Area.Min.X+65,Area.Max.Y-44),FIntPoint(Area.Max.X-60,Area.Max.Y-24)),FString::Printf(TEXT("%d / %d"),CraftPage+1,Pages));
    FString Status=CraftProfessions?TEXT("Select a profession to view recipes."):FString::Printf(TEXT("Rank %u   XP %llu   Next %llu"),Level,WideNum(Progress,TEXT("available")),Cost);
    Label(FIntRect(FIntPoint(Area.Min.X,Area.Max.Y-23),Area.Max),Status);
    if(Rows.IsEmpty())Label(FIntRect(Area.Min,FIntPoint(Area.Max.X,Area.Min.Y+100)),Net->CraftSkills.IsEmpty()?TEXT("Waiting for the server's crafting registry."):TEXT("No known recipes for this profession."));
    Bind(TEXT("RaiseCraftButton"),TEXT("CraftRaise"));Hints.Add(TEXT("RaiseCraftButton"),CanRaise?TEXT("Spend crafting XP to raise this profession."):TEXT("Earn crafting XP by making suitable items. Your level and XP are saved."));
}
void SAC2GameUI::BuildIngredients()
{
    auto* Net=Mode->Network.Get();if(!Net||!Net->OpenCraftRecipe)return;auto Recipe=Net->CraftCatalog.FindRef(Net->OpenCraftRecipe);if(!Recipe)return;
    const auto Inputs=Net->CraftInputs();const auto T=[](const TCHAR* S){return FAC2UILayout::Token(S);};const FString Name=Str(Recipe,TEXT("name"));
    const uint32 Skill=Num(Recipe,TEXT("skill")),Level=Num(Net->CraftProgress.FindRef(Skill),TEXT("level"));
    const int64 Difficulty=Curve(Recipe,TEXT("difficulty"),Net->CraftSpinner),Delta=int64(Level)-Difficulty;
    const int32 ChanceIndex=int32(FMath::Clamp<int64>(Delta,-500,500))+500;
    const int32 Chance=Str(Recipe,TEXT("kind"))==TEXT("refine")||Str(Recipe,TEXT("kind"))==TEXT("dye")||Str(Recipe,TEXT("kind"))==TEXT("survey")?100:Net->CraftSuccess.IsValidIndex(ChanceIndex)?int32(Net->CraftSuccess[ChanceIndex]):0;
    FString Title=Name;while(Title.Len()>1&&TitleFont&&TitleFont->Width(Title+TEXT("..."))>202)Title.LeftChopInline(1);
    if(Title!=Name)Title+=TEXT("...");
    Native.Text.Add(0x5cabf830,Title);Native.Text.Add(0x2b917f5b,!Net->CraftSuccess.IsValidIndex(ChanceIndex)||Str(Recipe,TEXT("kind"))==TEXT("native")?FString::Printf(TEXT("Difficulty %lld"),Difficulty):FString::Printf(TEXT("Difficulty %lld   Base chance %d%%"),Difficulty,Chance));
    Native.Text.Add(0x3257a864,FString::Printf(TEXT("Cost %llu gold"),Curve(Recipe,TEXT("cost"),Net->CraftSpinner)));
    Native.Text.Add(0x5dcd7380,Net->Combat?TEXT("Return to peace mode."):Net->CraftNotice);
    // The server adds an equipped creation tool's bonus. Base chance alone must
    // not disable an attempt that becomes possible with the player's tool.
    Native.States.Add(T(TEXT("CraftButton")),T(Net->CanCraft()?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(T(TEXT("AutoPopulateButton")),T(!Net->CraftPending&&!Net->Combat&&!Net->Dead?TEXT("normal"):TEXT("ghosted")));
    // Retail's recipe level spinner lives in the footer. Keep its native
    // arrows and numeric field aligned with the frame when the window grows.
    for(uint32 Id:{0xe9dc483eu,0x82155597u,0x9ba1f88fu,0xbff888f4u,0xf594c22eu})Native.Visibility.Add(Id,true);
    const bool ChangeLevel=!Net->CraftPending;
    Native.States.Add(0x82155597,T(ChangeLevel&&Net->CraftSpinner<Num(Recipe,TEXT("maximum"))?TEXT("normal"):TEXT("ghosted")));
    Native.States.Add(0x9ba1f88f,T(ChangeLevel&&Net->CraftSpinner>Num(Recipe,TEXT("minimum"))?TEXT("normal"):TEXT("ghosted")));
    Native.Text.Add(0xf594c22e,TEXT("Craft level"));Native.Text.Add(0xbff888f4,FString::FromInt(Net->CraftSpinner));
    // The original frame is fixed at 256 pixels; center its actual native size.
    const int32 Height=FMath::Clamp(220+Inputs.Num()*48,300,FMath::Max(300,int32(Size.Y)-80));const FIntPoint Origin(FMath::Max(0,FMath::RoundToInt((Size.X-256)*.5)),FMath::Max(0,FMath::RoundToInt((Size.Y-Height)*.5)));
    Native.Template(0x22000005,IngredientWindow,FIntRect(Origin,Origin+FIntPoint(256,Height)),FIntRect(0,0,Size.X,Size.Y));
    const auto* Window=Native.Find(IngredientWindow);
    if(!Window)return;
    const auto WindowRect=Window->Rect;
    const auto* HeaderClose=Native.Find(IngredientClose);const int32 HeaderRight=HeaderClose?HeaderClose->Rect.Min.X-4:Origin.X+218;
    for(auto& Node:Native.Nodes)if(Node.Text==Title&&Node.Window==IngredientWindow&&Node.Rect.Min.Y<WindowRect.Min.Y+35){
        Node.Rect.Min.X=WindowRect.Min.X+12;Node.Rect.Max.X=FMath::Min(Node.Rect.Max.X,HeaderRight);Node.Clip=Node.Rect;
        const auto Font=Fonts.Get(Node.Attr(TEXT("font"),0x4200001d));FString Fit=Name;
        if(Font){while(Fit.Len()>1&&Font->Width(Fit+TEXT("..."))>Node.Rect.Width())Fit.LeftChopInline(1);}
        Node.Text=Fit==Name?Name:Fit+TEXT("...");
    }
    for(auto& Node:Native.Nodes)if(Node.Id==0x2b917f5b && Str(Recipe,TEXT("kind"))!=TEXT("native") && Net->CraftSuccess.IsValidIndex(ChanceIndex)){
        auto Color=[&](const TCHAR* Key,float Value){uint32 Bits;FMemory::Memcpy(&Bits,&Value,sizeof(Bits));Node.Attributes.Add(T(Key),Bits);};
        Color(TEXT("font_color_red"),Chance<50?1.f:.55f);Color(TEXT("font_color_green"),Chance<50?.35f:1.f);Color(TEXT("font_color_blue"),.35f);
    }
    if(const auto* Number=Native.Find(0xbff888f4))if(const auto* Up=Native.Find(0x82155597))
        for(auto& Node:Native.Nodes)if(Node.Id==0xf594c22e){
            // The footer bitmap includes the numeric recess. Reserve its left
            // bevel as well as the arrow button so the caption cannot overlap it.
            Node.Rect=FIntRect(WindowRect.Min.X+20,Number->Rect.Min.Y,Up->Rect.Min.X-40,Number->Rect.Max.Y);
            Node.Clip=Node.Rect;Node.Clip.Clip(WindowRect);
        }
    Bind(TEXT("CraftButton"),TEXT("CraftExecute"));Bind(TEXT("AutoPopulateButton"),TEXT("CraftFill"));
    for(const auto& Control:TArray<TPair<uint32,FName>>{{0x82155597,TEXT("CraftLevelUp")},{0x9ba1f88f,TEXT("CraftLevelDown")}})
        if(const auto* N=Native.Find(Control.Key);N&&N->State!=T(TEXT("ghosted")))Buttons.Add(Control.Value,FSlateRect(N->Clip.Min.X,N->Clip.Min.Y,N->Clip.Max.X,N->Clip.Max.Y));
    if(const auto* Close=Native.Find(IngredientClose)){Buttons.Add(TEXT("CraftClose"),FSlateRect(Close->Clip.Min.X,Close->Clip.Min.Y,Close->Clip.Max.X,Close->Clip.Max.Y));Hints.Add(TEXT("CraftClose"),TEXT("Close recipe"));}
    Hints.Add(TEXT("CraftButton"),TEXT("Attempt this recipe. Base chance excludes the bonus from an equipped creation tool. Failed attempts consume materials. Rejected requests consume nothing."));Hints.Add(TEXT("AutoPopulateButton"),TEXT("Fill ingredient slots from your backpack."));
    const auto* List=Native.Find(TEXT("IngredientListBox"));if(!List)return;FIntRect Area=List->Rect,Clip=List->Clip;
    Clip.Clip(Area);
    struct FRow {int32 Index,Height,Ordinal;FString Text;uint32 IconDefinition;};TArray<FRow> Rows;int32 Total=0;
    const auto Font=Fonts.Get(0x4200000c);
    for(const auto& F:Inputs){const int32 Index=F.Ordinal-1;FString IngredientName=F.Name;
        const auto* Player=Mode->Characters.Active();const auto& Instance=Net->CraftIngredients[Index];
        const auto* Selected=Player&&!Instance.IsEmpty()?Player->Items.FindByPredicate([&](const auto& I){return I.NetworkId==Instance;}):nullptr;
        if(Selected)IngredientName=CraftItemName(Selected->Definition);else if(IngredientName.IsEmpty()&&!F.Choices.IsEmpty())IngredientName=CraftItemName(F.Choices[0]);
        FString Text=FString::Printf(TEXT("%d x %s"),F.Quantity,*IngredientName);
        Text+=Net->CraftIngredients[Index].IsEmpty()?TEXT("\nDrag an item here"):Net->CraftAccepted[Index]?TEXT("\nReady - click to change"):TEXT("\nAwaiting validation");
        const int32 RowHeight=FMath::Max(38,Font?Font->Lines(Text,FMath::Max(1,Area.Width()-44)).Num()*int32(Font->Height)+6:48);
        // An empty slot has an unambiguous preview only for a single accepted material.
        uint32 IconDefinition=Selected?Selected->Definition:0;
        if(Instance.IsEmpty()&&F.Choices.Num()==1)IconDefinition=F.Choices[0];
        Rows.Add({Index,RowHeight,F.Ordinal,Text,IconDefinition});Total+=RowHeight;
    }
    IngredientMaxScroll=FMath::Max(0,Total-Area.Height());IngredientScroll=FMath::Clamp(IngredientScroll,0,IngredientMaxScroll);
    const uint32 Bar=List->Attributes.FindRef(0x2c1a95c1);
    UpdateScrollbar(Bar,IngredientScroll,IngredientMaxScroll,IngredientBar,IngredientThumb,TEXT("CraftScrollUp"),TEXT("CraftScrollDown"));
    int32 Y=Area.Min.Y-IngredientScroll;
    for(const auto& R:Rows){const FIntRect Row(Area.Min.X,Y,Area.Max.X,Y+R.Height);Y+=R.Height;
        if(Row.Max.Y<=Clip.Min.Y||Row.Min.Y>=Clip.Max.Y)continue;
        Native.Text.Add(T(TEXT("IngredientLabel")),R.Text);const int32 First=Native.Nodes.Num();
        Native.Template(0x22000005,TEXT("IngredientTemplate"),Row,Clip);
        for(int32 I=First;I<Native.Nodes.Num();++I){auto& Node=Native.Nodes[I];
            if(Node.Id==T(TEXT("IngredientLabel"))){Node.Rect=FIntRect(Row.Min+FIntPoint(37,2),Row.Max-FIntPoint(5,2));Node.Clip=Node.Rect;Node.Clip.Clip(Clip);Node.Attributes.Add(T(TEXT("center_vertically")),0);}
            if(Node.Id==T(TEXT("ItemIcon"))){
                Node.Rect=FIntRect(Row.Min+FIntPoint(2,2),Row.Min+FIntPoint(34,34));Node.Clip=Node.Rect;Node.Clip.Clip(Clip);
                const auto& Instance=Net->CraftIngredients[R.Index];
                auto Layers=Instance.IsEmpty()?TArray<FAC2IconLayer>():Net->ItemIconLayers(Instance,*Assets);
                if(Layers.IsEmpty()&&R.IconDefinition){
                    Mode->CharacterCatalog.LoadNetworkItem(Mode->DatDirectory,R.IconDefinition);
                    const auto* Item=Mode->CharacterCatalog.Items.Find(R.IconDefinition);
                    if(Item&&Item->Icon)Layers.Add({0,Item->Icon});
                }
                for(const auto& L:Layers){
                    FLinearColor Tint(FColor(L.Color));if(Instance.IsEmpty())Tint.A*=.55f;
                    Icons.Add({Node.Rect,Node.Clip,L.Image,1,Tint});
                }
            }
        }
        auto Hit=Row;Hit.Clip(Clip);const FName Key(*FString::Printf(TEXT("CraftIngredient%u"),R.Ordinal));
        Buttons.Add(Key,FSlateRect(Hit.Min.X,Hit.Min.Y,Hit.Max.X,Hit.Max.Y));
        Hints.Add(Key,TEXT("Drag an item from your backpack, or click to cycle candidates. The server checks whether it is suitable."));
    }Native.Text.Remove(T(TEXT("IngredientLabel")));
}
void SAC2GameUI::DragIngredients(float Y)
{
    const int32 Travel=IngredientBar.Height()-IngredientThumb.Height();
    IngredientScroll=Travel>0?FMath::Clamp(FMath::RoundToInt((Y-IngredientGrab-IngredientBar.Min.Y)/Travel*IngredientMaxScroll),0,IngredientMaxScroll):0;
    Rebuild();
}

bool SAC2GameUI::CraftAction(FName Name)
{
    auto* Net=Mode->Network.Get();if(!Net)return false;const FString Key=Name.ToString();
    if(Name==TEXT("CraftScrollUp")||Name==TEXT("CraftScrollDown")){IngredientScroll=FMath::Clamp(IngredientScroll+(Name==TEXT("CraftScrollUp")?-38:38),0,IngredientMaxScroll);return true;}
    if(Key.StartsWith(TEXT("CraftIngredient"))){Net->CycleCraftIngredient(FCString::Atoi(*Key.Mid(15)));return true;}
    if(Key.StartsWith(TEXT("CraftRecipe"))){Net->OpenCraft(FCString::Strtoui64(*Key.Mid(11),nullptr,10));IngredientScroll=0;RaiseWindow(IngredientWindow);return true;}
    if(Key.StartsWith(TEXT("CraftProfession"))){CraftSkill=FCString::Strtoui64(*Key.Mid(15),nullptr,10);CraftProfessions=false;CraftPage=0;return true;}
    if(Name==TEXT("CraftBrowse")){CraftProfessions=true;CraftPage=0;return true;}
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
        const auto Recipe=Net.CraftCatalog.FindRef(0x6f001f17u);
        if(!Recipe)return Fail(TEXT("Fixture recipe unavailable"));
        const FName Profession(*FString::Printf(TEXT("CraftProfession%u"),Num(Recipe,TEXT("skill"))));
        while(!Buttons.Contains(Profession) && CraftPage<JournalMaxScroll){
            if(!Click(TEXT("CraftNext")))return Fail(TEXT("Unable to page professions"));Rebuild();
        }
        if(!Click(Profession))return Fail(TEXT("Unable to select fixture profession"));Rebuild();
        if(!Buttons.Contains(*FString::Printf(TEXT("CraftRecipe%u"),0x6f001f17u)) || Net.CraftRecipes.Num()<250)
            return Fail(TEXT("Granted recipe rows missing"));
        // Leave the recipe book and hotbar clear of each other at the test
        // resolution, independently of window positions saved by the player.
        const uint32 SkillsWindow=FAC2UILayout::Token(TEXT("SkillField"));
        Native.RootSizes.Remove(SkillsWindow);
        Rebuild();
        const auto SkillBounds=Native.Bounds(TEXT("SkillField"));
        Native.RootOffsets.FindOrAdd(SkillsWindow)+=FIntPoint(650,50)-SkillBounds.Min;
        TestingShortcuts=true;ShortcutSet=9;Shortcuts[99]={};Rebuild();
        const FName RecipeRow(*FString::Printf(TEXT("CraftRecipe%u"),0x6f001f17u));
        const auto* From=Buttons.Find(RecipeRow);
        const auto* To=Buttons.Find(TEXT("ShortcutSlot9"));
        if(!From || !To)return Fail(TEXT("Recipe shortcut drag targets missing"));
        const FVector2D RecipeCenter((From->Left+From->Right)*.5,(From->Top+From->Bottom)*.5);
        const FVector2D ShortcutCenter((To->Left+To->Right)*.5,(To->Top+To->Bottom)*.5);
        if(!AllowsPanelButton(RecipeRow,RecipeCenter) || !AllowsPanelButton(TEXT("ShortcutSlot9"),ShortcutCenter))
            return Fail(TEXT("A window covers the recipe drag or hotbar slot"));
        const auto A=GetCachedGeometry().LocalToAbsolute(RecipeCenter*Scale);
        const auto B=GetCachedGeometry().LocalToAbsolute(ShortcutCenter*Scale);
        auto Path=App.LocateWindowUnderMouse(A,{Window.ToSharedRef()},false);
        if(!Path.IsValid())return Fail(TEXT("Recipe row pointer path missing"));
        App.RoutePointerDownEvent(Path,FPointerEvent(0,A,A,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(DragBinding.Recipe!=0x6f001f17u)return Fail(TEXT("Recipe row did not start a recipe drag"));
        OnMouseMove(GetCachedGeometry(),FPointerEvent(0,B,A,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,B,B,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        if(Shortcuts[99].Recipe!=0x6f001f17 || Net.OpenCraftRecipe)return Fail(TEXT("Recipe drag did not create an inert shortcut"));
        const auto Layers=ShortcutLayers(Shortcuts[99]);
        if(Layers.IsEmpty() || !Native.Brush(Layers[0].Image))return Fail(TEXT("Recipe shortcut icon did not resolve from DAT"));
        Path=App.LocateWindowUnderMouse(B,{Window.ToSharedRef()},false);
        App.RoutePointerDownEvent(Path,FPointerEvent(0,B,B,{EKeys::RightMouseButton},EKeys::RightMouseButton,0,FModifierKeysState()));
        App.RoutePointerUpEvent(Path,FPointerEvent(0,B,B,{},EKeys::RightMouseButton,0,FModifierKeysState()));
        if(ExaminedRecipe!=0x6f001f17 || Net.OpenCraftRecipe)return Fail(TEXT("Recipe shortcut right-click did not inspect without opening"));
    }
    if(Step==1){
        ShowSkills=false;ShowStats=false;Rebuild();
        if(!Click(TEXT("ShortcutSlot9")) || Net.OpenCraftRecipe!=0x6f001f17u)
            return Fail(TEXT("Recipe shortcut did not open ingredient window with book closed"));Rebuild();
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
        ActivateShortcut(9);
        if(!Net.CraftPending || Net.OpenCraftRecipe!=0x6f001f17)return Fail(TEXT("Repeated recipe shortcut reset pending craft"));
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
        ShowSkills=true;Rebuild();
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
    if(Step==9){
        if(Shortcuts[99].Recipe!=0x6f001f17)return Fail(TEXT("Crafting consumed the recipe shortcut"));
        ShowSkills=false;InspectShortcut(9);
        if(!ShowStats || !Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text==TEXT("Ingredient: 100 x Tallow");}))
            return Fail(TEXT("Recipe examination did not retain its ingredient requirements"));
    }
    Rebuild();
    if(Step==1 || Step==7){
        const auto* Title=Native.Find(0x5cabf830);const auto* Close=Native.Find(IngredientClose);
        const auto Font=Title?Fonts.Get(Title->Attr(TEXT("font"),0x4200001d)):nullptr;
        if(!Title || !Close || !Font || Font->Width(Title->Text)>Title->Rect.Width() || Title->Rect.Max.X>=Close->Rect.Min.X)
            return Fail(TEXT("Recipe title overlaps the close control"));
    }
    return true;
}
