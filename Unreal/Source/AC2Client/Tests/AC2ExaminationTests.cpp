#include "AC2GameMode.h"
#include "AC2GameUI.h"
#include "AC2Network.h"
#include "AC2CharacterUI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderingThread.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LiveExaminationTest,"AC2.Data.LiveExamination",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LiveExaminationTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->GetWorldSettings()->DefaultGameMode=AAC2GameMode::StaticClass();
    World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
    auto* Mode=World->GetAuthGameMode<AAC2GameMode>();Mode->FrontEnd=false;
    Mode->DatDirectory=AC2::ResolveDatDirectory(TEXT(""));
    auto* PC=World->SpawnActor<AAC2PlayerController>();PC->MyHUD=World->SpawnActor<AAC2HUD>();
    PC->MyHUD->SetOwner(PC);
    FAC2CharacterProfile Player;Player.Id=TEXT("101");Player.Name=TEXT("Inspector");
    Mode->Characters.Characters.Add(Player);Mode->Characters.ActiveId=Player.Id;
    Mode->Network=MakeUnique<FAC2Network>(Mode);
    auto& Net=*Mode->Network;Net.Phase=TEXT("world");Net.PlayerId=Player.Id;
    for(const TCHAR* Id:{TEXT("101"),TEXT("202"),TEXT("303")})
        Net.Objects.Add(Id,MakeUnique<FAC2Network::FObject>());
    void* Commands=nullptr;
    if(const auto UI=SNew(SAC2GameUI).Mode(Mode);
        TestTrue(TEXT("Native examination HUD loaded its DAT body font"),UI->BodyFont.IsValid()) &&
        TestTrue(TEXT("Capture real bridge commands"),FPlatformProcess::CreatePipe(Commands,Net.Write,true)))
    {
        Cast<AAC2HUD>(PC->MyHUD)->GameUI=UI;
        TestTrue(TEXT("Native examination HUD is visible"),UI->VisibleWorld());
        const auto Geometry=FGeometry::MakeRoot(FVector2D(1280,720),FSlateLayoutTransform());
        auto Tick=[&](){UI->Tick(Geometry,FPlatformTime::Seconds(),.016f);};
        auto ReadCommands=[&](){return FPlatformProcess::ReadPipe(Commands);};
        auto Due=[&](){Net.ExaminationStarted=FPlatformTime::Seconds()-.6;};
        auto Event=[](const TCHAR* Type,const TCHAR* Id){auto J=MakeShared<FJsonObject>();
            J->SetStringField(TEXT("event"),Type);J->SetStringField(TEXT("id"),Id);return J;};
        auto ReceiveVital=[&](const TCHAR* Id,uint32 Key,int32 Value){auto J=Event(TEXT("quality"),Id);
            J->SetNumberField(TEXT("table"),4);J->SetNumberField(TEXT("key"),Key);J->SetNumberField(TEXT("value"),Value);Net.Handle(J);};
        auto Profile=[&](const TCHAR* Id,const TCHAR* Name,const TCHAR* Stat){
            auto J=Event(TEXT("examination"),Id);TArray<TSharedPtr<FJsonValue>> Nodes;
            auto Row=[&](const FString& Line){
                auto N=MakeShared<FJsonObject>();N->SetNumberField(TEXT("kind"),0x40000004);
                auto Text=MakeShared<FJsonObject>();Text->SetStringField(TEXT("literal"),Line);
                N->SetObjectField(TEXT("text"),Text);Nodes.Add(MakeShared<FJsonValueObject>(N));
                auto Break=MakeShared<FJsonObject>();Break->SetNumberField(TEXT("kind"),0x40000006);
                Nodes.Add(MakeShared<FJsonValueObject>(Break));
            };
            Row(Name);Row(Stat);
            // Overflow the native panel to exercise retained scroll position.
            for(int32 I=0;I<40;++I)Row(FString::Printf(TEXT("Description line %d"),I));
            J->SetArrayField(TEXT("nodes"),Nodes);Net.Handle(J);
        };
        auto Shows=[&](const TCHAR* Text){return UI->Native.Nodes.ContainsByPredicate(
            [&](const auto& N){return N.Text==Text;});};

        // Feed the real network decoder, then inspect the actual DAT-styled
        // controls. The former row concatenation lost every style after a label.
        {
            UI->InspectTarget(TEXT("303"));ReadCommands();
            Net.Handle([&](){auto J=Event(TEXT("examination"),TEXT("303"));
                TArray<TSharedPtr<FJsonValue>> Nodes;
                auto Add=[&](uint32 Kind,uint32 Appearance=0,const TCHAR* Text=TEXT(""),int32 Count=0){
                    auto N=MakeShared<FJsonObject>();N->SetNumberField(TEXT("kind"),Kind);N->SetNumberField(TEXT("appearance"),Appearance);
                    auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("literal"),Text);N->SetObjectField(TEXT("text"),T);N->SetNumberField(TEXT("value"),Count);
                    Nodes.Add(MakeShared<FJsonValueObject>(N));return N;
                };
                auto Icon=Add(0x40000009);TArray<TSharedPtr<FJsonValue>> Layers;
                auto L=MakeShared<FJsonObject>();L->SetNumberField(TEXT("layer"),1);L->SetNumberField(TEXT("image"),0x41003d05);L->SetNumberField(TEXT("color"),0xffabcdefu);
                Layers.Add(MakeShared<FJsonValueObject>(L));Icon->SetArrayField(TEXT("layers"),Layers);
                Add(0x40000004,0x6005ff6b,TEXT("Focus"));Add(0x40000006);
                Add(0x40000004,0,TEXT("Weapon Stats:"));Add(0x40000006);
                Add(0x40000007,0,TEXT(""),2);Add(0x40000004,0,TEXT("Damage: "));Add(0x40000004,0xbe5b6513,TEXT("12 - 18"));Add(0x40000006);Add(0x40000006);
                Add(0x40000004,0x22649835,TEXT("Cold:"));Add(0x40000006);
                Add(0x40000007,0,TEXT(""),1);Add(0x40000004,0xfefefefe,TEXT("Unknown appearance keeps readable body text."));
                J->SetArrayField(TEXT("nodes"),Nodes);return J;
            }());Tick();
            auto FindText=[&](const TCHAR* T){return UI->Native.Nodes.FindByPredicate([&](const auto& N){return N.Text==T;});};
            const auto* Title=FindText(TEXT("Focus"));const auto* Heading=FindText(TEXT("Weapon Stats:"));
            const auto* Label=FindText(TEXT("Damage: "));const auto* Number=FindText(TEXT("12 - 18"));const auto* Effect=FindText(TEXT("Cold:"));
            const auto List=UI->Native.Bounds(TEXT("ExaminationListBox"));
            TestFalse(TEXT("Object profile does not overlay the alternate examination tabs"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Id==0x8f26b636;}));
            if(TestTrue(TEXT("Rich examination renders separate spans"),Title&&Heading&&Label&&Number&&Effect)){
                TestTrue(TEXT("Name uses native blue appearance"),Title->Float(TEXT("font_color_blue"))>Title->Float(TEXT("font_color_red")));
                TestEqual(TEXT("Name has a four-pixel gap beside icon"),Title->Rect.Min.X,List.Min.X+36);
                TestEqual(TEXT("Name is vertically centered on the icon row"),Title->Rect.Min.Y,List.Min.Y+FMath::Max(0,(32-Title->Rect.Height())/2));
                TestTrue(TEXT("Following row clears full icon height"),Heading->Rect.Min.Y>=List.Min.Y+32);
                TestEqual(TEXT("Two tabs indent 32 native pixels"),Label->Rect.Min.X,List.Min.X+32);
                TestEqual(TEXT("Bold value follows label inline"),Number->Rect.Min.X,Label->Rect.Max.X);
                TestEqual(TEXT("Value and label share Y"),Number->Rect.Min.Y,Label->Rect.Min.Y);
                TestNotEqual(TEXT("Bold value retains its own DAT font"),Number->Attr(TEXT("font")),Label->Attr(TEXT("font")));
                TestTrue(TEXT("Explicit blank line retains section spacing"),Effect->Rect.Min.Y>=Number->Rect.Max.Y+16);
                TestTrue(TEXT("Effect heading uses native orange"),Effect->Float(TEXT("font_color_red"))>Effect->Float(TEXT("font_color_green")) && Effect->Float(TEXT("font_color_green"))>Effect->Float(TEXT("font_color_blue")));
                TestEqual(TEXT("Orange display RGB is not brightened by Slate gamma"),Effect->TextColor().ToFColorSRGB(),FColor(255,127,63));
                TestEqual(TEXT("Blue display RGB is not brightened by Slate gamma"),Title->TextColor().ToFColorSRGB(),FColor(12,161,238));
            }
            TestTrue(TEXT("Icon tint survives network and layout"),UI->Icons.ContainsByPredicate([](const auto& I){return I.Image==0x41003d05 && I.Tint==FLinearColor(FColor(0xffabcdefu)) && I.NativeNode!=INDEX_NONE;}));
            TestTrue(TEXT("Unknown appearance falls back to native body"),Shows(TEXT("Unknown appearance keeps readable body text.")));
#if WITH_EDITOR
            if(FParse::Param(FCommandLine::Get(),TEXT("AC2ExaminationRender"))){
                // Exercise the same Slate paint path as the HUD without login,
                // player movement, a server connection, or persisted UI changes.
                FWidgetRenderer Renderer(true);
                TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
                // Slate's shader writes display RGB. Match the window backbuffer
                // rather than applying a second sRGB conversion on the target.
                Target->ClearColor=FLinearColor::Transparent;
                Target->InitCustomFormat(1280,720,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);
                Renderer.DrawWidget(Target.Get(),UI,FVector2D(1280,720),0.f);
                FlushRenderingCommands();TArray<FColor> Pixels;
                FReadSurfaceDataFlags ReadFlags;ReadFlags.SetLinearToGamma(false); // Slate has already encoded display RGB.
                if(TestTrue(TEXT("Read examination rendering"),Target.IsValid() && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,ReadFlags))){
                    auto HasColor=[&](FIntRect Area,FColor Expected){
                        Area.Clip(FIntRect(0,0,1280,720));
                        for(int32 Y=Area.Min.Y;Y<Area.Max.Y;++Y)for(int32 X=Area.Min.X;X<Area.Max.X;++X){
                            const auto C=Pixels[Y*1280+X];
                            if(FMath::Abs(int32(C.R)-Expected.R)<=2 && FMath::Abs(int32(C.G)-Expected.G)<=2 && FMath::Abs(int32(C.B)-Expected.B)<=2)return true;
                        }
                        return false;
                    };
                    if(Title&&Effect){
                        TestTrue(TEXT("Rendered title keeps retail blue"),HasColor(Title->Rect,FColor(12,161,238)));
                        TestTrue(TEXT("Rendered effect keeps retail orange"),HasColor(Effect->Rect,FColor(255,127,63)));
                    }
                    for(auto& Pixel:Pixels)Pixel.A=255;
                    TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1280,720,Pixels,PNG);
                    TestTrue(TEXT("Save examination rendering"),FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Screenshots/AC2-examination-layout.png"))));
                }
            }
#endif
            UI->ClosePanels();Due();ReadCommands();
        }

        {
            const FString Id=TEXT("404");
            auto Item=MakeUnique<FAC2Network::FObject>();
            Item->Entity=0x47000f20;Item->Visual=0x1f0002c4;
            Net.Objects.Add(Id,MoveTemp(Item));
            auto Container=Event(TEXT("container_state"),TEXT("405"));
            Container->SetBoolField(TEXT("open"),true);
            Container->SetArrayField(TEXT("items"),{MakeShared<FJsonValueString>(Id)});
            Net.Handle(Container);
            UI->InspectTarget(Id);ReadCommands();
            Profile(*Id,TEXT("Skewer"),TEXT("Weapon damage: 14"));
            FAC2Network::FExaminationRow Placeholder;
            Placeholder.Kind=0x40000005;Placeholder.Image=0x4100034b;
            Net.ExaminationRows.Insert(Placeholder,0);
            auto Leave=Event(TEXT("world_leave"),*Id);Leave->SetNumberField(TEXT("position_stamp"),1);
            Net.Handle(Leave);
            TestTrue(TEXT("World departure preserves an item listed in the open corpse"),Net.LootItems.Contains(Id) && Net.Objects.Contains(Id));
            const auto Layers=Net.ItemIconLayers(Id,*UI->Assets);
            TestFalse(TEXT("Retail Skewer visual has inventory artwork"),Layers.IsEmpty());
            Tick();
            for(const auto& L:Layers)TestTrue(TEXT("Corpse Examine displays the same tinted layer as inventory"),
                UI->Icons.ContainsByPredicate([&](const auto& I){return I.NativeNode!=INDEX_NONE && I.Image==L.Image && I.Tint==FLinearColor(FColor(L.Color));}));
            TestFalse(TEXT("Placeholder descriptor does not obscure Skewer artwork"),
                UI->Icons.ContainsByPredicate([](const auto& I){return I.NativeNode!=INDEX_NONE && I.Image==0x4100034b;}));
            TestTrue(TEXT("Corpse profile retains server damage"),Shows(TEXT("Weapon damage: 14")));
            Net.Handle(Event(TEXT("remove"),*Id));
            TestFalse(TEXT("Destroying a loot item removes its open-container entry"),Net.LootItems.Contains(Id));
            TestFalse(TEXT("Destroyed loot no longer has an examine profile"),Net.ExaminationError.IsEmpty());
            Container->SetBoolField(TEXT("open"),false);Net.Handle(Container);
            UI->ClosePanels();Due();ReadCommands();
        }

        Net.Handle(Event(TEXT("open_examination"),TEXT("303")));
        const FString ForcedCommands=ReadCommands();
        TestEqual(TEXT("Server read-object notice selects its object"),Net.TargetId,FString(TEXT("303")));
        TestEqual(TEXT("Server read-object notice opens examination for the object"),UI->InspectedObject,FString(TEXT("303")));
        TestTrue(TEXT("Server notice requests native examination"),ForcedCommands.Contains(TEXT("\"command\":\"examine\"")));
        Net.Handle(Event(TEXT("open_examination"),TEXT("999")));
        TestTrue(TEXT("Unknown server examination target sends no requests"),ReadCommands().IsEmpty());
        UI->ClosePanels();Due();

        UI->InspectTarget(TEXT("202"));
        TestTrue(TEXT("Opening panel requests authoritative object profile"),ReadCommands().Contains(TEXT("\"id\":\"202\"")));
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 100 / 100"));Tick();
        TestTrue(TEXT("Initial native rows are displayed"),Shows(TEXT("Health: 100 / 100")));
        TestTrue(TEXT("Every-frame UI rebuild does not poll every frame"),ReadCommands().IsEmpty());
        Due();Tick();TestTrue(TEXT("Unchanged open examination never polls the server"),ReadCommands().IsEmpty());
        TestTrue(TEXT("Long profiles have a real scrollbar"),UI->ExamineMaxScroll>0 && !UI->ExamineThumb.IsEmpty());
        const auto TopThumb=UI->ExamineThumb;
        UI->ExamineGrab=0;UI->DragExamine(UI->ExamineBar.Max.Y);
        TestEqual(TEXT("Dragging inspection thumb reaches final row"),UI->ExamineScroll,UI->ExamineMaxScroll);
        TestTrue(TEXT("Inspection thumb follows scroll position"),UI->ExamineThumb.Min.Y>TopThumb.Min.Y);
        TestTrue(TEXT("Last profile row is visible"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text==TEXT("Description line 39") && N.Clip.Height()>0;}));
        UI->Action(TEXT("ExamineUp"));
        TestTrue(TEXT("Native inspection up arrow scrolls"),UI->ExamineScroll<UI->ExamineMaxScroll);
        UI->ExamineScroll=32;ReceiveVital(TEXT("202"),0x102,40);Due();Tick();
        TestTrue(TEXT("Visible panel requests live update"),ReadCommands().Contains(TEXT("\"command\":\"examine\"")));
        TestTrue(TEXT("Old rows stay visible during refresh"),Shows(TEXT("Health: 100 / 100")));
        TestFalse(TEXT("Refresh never replaces rows with a loading message"),Shows(TEXT("Examining...")));
        TestEqual(TEXT("Refresh preserves scroll"),UI->ExamineScroll,32);
        ReceiveVital(TEXT("202"),0x102,35);Due();Tick();
        TestTrue(TEXT("Slow response cannot create overlapping polls"),ReadCommands().IsEmpty());
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 40 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Damage appears without another user examination"),Shows(TEXT("Health: 40 / 100")));
        TestFalse(TEXT("Previous health is replaced"),Shows(TEXT("Health: 100 / 100")));
        TestEqual(TEXT("New server rows preserve scroll"),UI->ExamineScroll,32);
        Due();Tick();TestFalse(TEXT("Change while reply was pending schedules one follow-up"),ReadCommands().IsEmpty());
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 35 / 100"));
        Due();Tick();TestTrue(TEXT("Follow-up ends when object stops changing"),ReadCommands().IsEmpty());

        UI->ExamineScroll=0;
        ReceiveVital(TEXT("202"),0x102,0);Due();Tick();ReadCommands();
        Profile(TEXT("202"),TEXT("Corpse of Red Soldier Wasp"),TEXT("Health: 0 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Death updates both name and health"),Shows(TEXT("Corpse of Red Soldier Wasp")) && Shows(TEXT("Health: 0 / 100")));

        // The selected combat target is independent from the inspected object.
        Net.TargetId=TEXT("303");ReceiveVital(TEXT("202"),0x103,0);Due();Tick();
        TestTrue(TEXT("Inspection continues on its own object"),ReadCommands().Contains(TEXT("\"id\":\"202\"")));
        UI->InspectTarget(TEXT("101"));
        TestTrue(TEXT("Rapid target switch waits for server cooldown"),ReadCommands().IsEmpty());
        Profile(TEXT("202"),TEXT("Old target"),TEXT("Health: 0 / 100"));
        TestTrue(TEXT("Late old target response leaves new request pending"),Net.ExaminationPending);
        TestTrue(TEXT("Changing objects clears previous object's rows"),Net.ExaminationRows.IsEmpty());
        Due();Tick();
        TestTrue(TEXT("Queued target switch sends after cooldown"),ReadCommands().Contains(TEXT("\"id\":\"101\"")));
        Profile(TEXT("101"),TEXT("Inspector"),TEXT("Vigor: 20 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Self examination uses authoritative vigor"),Shows(TEXT("Vigor: 20 / 100")));
        TestFalse(TEXT("Servers without the quest point quality show no synthetic rating"),Net.QuestPoints.IsSet());
        ReceiveVital(TEXT("101"),0x7ac20003,123456789);Due();Tick();
        TestTrue(TEXT("Quest point update requests the open player profile"),ReadCommands().Contains(TEXT("\"command\":\"examine\"")));
        TestEqual(TEXT("Quest point balance retains integer precision"),Net.QuestPoints.Get(0),123456789u);
        Profile(TEXT("101"),TEXT("Inspector"),TEXT("Quest points: 123456789"));Tick();
        TestTrue(TEXT("Player examination displays the server rating"),Shows(TEXT("Quest points: 123456789")));
        ReceiveVital(TEXT("101"),0x103,45);Due();Tick();ReadCommands();
        Profile(TEXT("101"),TEXT("Inspector"),TEXT("Vigor: 45 / 100"));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Self regeneration updates live"),Shows(TEXT("Vigor: 45 / 100")));

        Due();UI->InspectTarget(TEXT("303"));ReadCommands();
        Profile(TEXT("303"),TEXT("Boots"),TEXT("In your inventory."));
        auto Equip=Event(TEXT("inventory_result"),TEXT("303"));Equip->SetNumberField(TEXT("status"),0);
        Equip->SetNumberField(TEXT("equipped"),1);Net.Handle(Equip);
        Due();Tick();TestFalse(TEXT("Equipment change refreshes inspected item"),ReadCommands().IsEmpty());Profile(TEXT("303"),TEXT("Boots"),TEXT("Equipped."));
        Net.ExaminationStarted=FPlatformTime::Seconds();Tick();
        TestTrue(TEXT("Equipment location updates live"),Shows(TEXT("Equipped.")));
        UI->ClosePanels();Due();Tick();
        TestTrue(TEXT("Closed panel stops polling"),ReadCommands().IsEmpty());
        auto Name=Event(TEXT("quality"),TEXT("303"));Name->SetNumberField(TEXT("table"),10);Name->SetNumberField(TEXT("key"),0x100);
        auto Value=MakeShared<FJsonObject>();Value->SetStringField(TEXT("literal"),TEXT("Renamed boots"));Name->SetObjectField(TEXT("value"),Value);
        Net.Handle(Name);Tick();
        TestTrue(TEXT("Name updates do not poll a closed panel"),ReadCommands().IsEmpty());

        UI->InspectTarget(TEXT("202"));ReadCommands();
        Profile(TEXT("202"),TEXT("Red Soldier Wasp"),TEXT("Health: 100 / 100"));Due();Tick();ReadCommands();
        Net.Handle(Event(TEXT("remove"),TEXT("202")));Tick();
        TestTrue(TEXT("Removed object clears stale statistics"),Net.ExaminationRows.IsEmpty());
        TestTrue(TEXT("Removed object shows unavailable message"),Shows(TEXT("That object is no longer available to examine.")));
        Profile(TEXT("202"),TEXT("Late removed object"),TEXT("Health: 100 / 100"));
        TestTrue(TEXT("Late response cannot restore removed object"),Net.ExaminationRows.IsEmpty());
        Due();Tick();TestTrue(TEXT("Removed object stops polling"),ReadCommands().IsEmpty());

        Due();UI->InspectTarget(TEXT("101"));ReadCommands();
        Net.ExaminationStarted=FPlatformTime::Seconds()-9;Net.Tick(0);Tick();
        TestFalse(TEXT("Timed out request finishes"),Net.ExaminationPending);
        TestFalse(TEXT("Timed out request reports an error"),Net.ExaminationError.IsEmpty());
        TestTrue(TEXT("Timeout does not hammer the server"),ReadCommands().IsEmpty());
        UI->InspectTarget(TEXT("101"));TestFalse(TEXT("Explicit retry recovers after a timeout"),ReadCommands().IsEmpty());
        UI->ExaminedSkill=85;UI->InspectTarget(TEXT("101"));
        TestEqual(TEXT("Object inspection replaces skill details"),UI->ExaminedSkill,0u);
        UI->ExamineScroll=200;Net.ExaminationRows={{TEXT("Short refreshed profile")}};Net.ExaminationError.Reset();Tick();
        TestEqual(TEXT("Short refresh clamps scroll immediately"),UI->ExamineScroll,0);
        TestTrue(TEXT("Short refresh remains visible in the same frame"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Text==TEXT("Short refreshed profile") && N.Clip.Height()>0;}));

        // Reproduce the blocked shared mastery through the actual native panel,
        // not just the eligibility helper. Actions must still await server replies.
        UI->ClosePanels();UI->ShowSkills=true;UI->SkillTab=0;UI->SelectedSkill=4;
        Mode->Characters.Characters[0].Race=1;Mode->Characters.Characters[0].Level=50;
        Net.SkillCredits=30;Net.AvailableXp=100000;
        for(uint32 Id=0x80000004;Id<=0x80000052;++Id)
            if(UI->SkillData.Panel(Id).Nodes.ContainsByPredicate([](const auto& N){return N.Id==4;})){UI->SkillPanel=Id;break;}
        UI->Native.RootSizes.Add(FAC2UILayout::Token(TEXT("SkillField")),FIntPoint(859,1086));
        Tick();ReadCommands();
        TestTrue(TEXT("Restored skill window fits smaller viewport"),UI->Native.Bounds(TEXT("SkillField")).Max.Y<=720);
        UI->Action(TEXT("TrainButton"));
        TestTrue(TEXT("Shared mastery Train sends native command"),ReadCommands().Contains(TEXT("\"command\":\"skill_train\"")));
        TestTrue(TEXT("Training waits for server confirmation"),Net.Skills.IsEmpty());
        Net.Skills.Add({4,true,TEXT("0")});Tick();UI->Action(TEXT("RaiseButton"));
        TestTrue(TEXT("Shared mastery Raise sends native command"),ReadCommands().Contains(TEXT("\"command\":\"skill_raise\"")));
        Net.AvailableXp=0;Tick();UI->Action(TEXT("RaiseButton"));
        TestTrue(TEXT("Disabled raise sends nothing"),ReadCommands().IsEmpty());
        TestTrue(TEXT("Disabled raise explains missing XP"),UI->Hints.FindRef(TEXT("RaiseButton")).Contains(TEXT("XP")));
        Net.Skills.Reset();Net.SkillCredits=0;Tick();UI->Action(TEXT("TrainButton"));
        TestTrue(TEXT("Disabled training sends nothing"),ReadCommands().IsEmpty());
        TestTrue(TEXT("Disabled training explains credits"),UI->Hints.FindRef(TEXT("TrainButton")).Contains(TEXT("skill credits")));

        Net.TargetId=TEXT("303");Net.CastSkill(0x55);
        TestTrue(TEXT("Ease Suffering preserves an explicit player target"),ReadCommands().Contains(TEXT("\"target\":\"303\"")));
        Net.TargetId.Reset();Net.CastSkill(0x55);
        TestTrue(TEXT("No selection lets server resolve self-healing"),ReadCommands().Contains(TEXT("\"target\":\"0\"")));
        Net.TargetId=TEXT("303");Net.CastSkill(0x343);
        TestTrue(TEXT("Lifestone recall remains self-only"),ReadCommands().Contains(TEXT("\"target\":\"101\"")));

        // Invitation buttons are distinct instances of the creation/leave
        // templates. Verify they hit the invitation, not the hidden form.
        UI->ClosePanels();UI->SocialAction(TEXT("Social"));ReadCommands();
        auto Invite=Event(TEXT("fellowship_invitation"),TEXT("202"));
        auto Literal=[](const TCHAR* Text){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("literal"),Text);return V;};
        Invite->SetObjectField(TEXT("inviter"),Literal(TEXT("Recruiter")));
        Invite->SetObjectField(TEXT("name"),Literal(TEXT("Dereth Travelers")));
        Net.Handle(Invite);Tick();UI->SlatePrepass();
        TestTrue(TEXT("Invitation has accept button"),UI->Buttons.Contains(TEXT("FellowshipAccept")));
        TestTrue(TEXT("Invitation has decline button"),UI->Buttons.Contains(TEXT("FellowshipDecline")));
        TestFalse(TEXT("Invitation replaces creation form"),UI->Buttons.Contains(TEXT("FellowshipCreateButton")));
        TestTrue(TEXT("Invitation name remains visible"),Shows(TEXT("Recruiter invites you to Dereth Travelers")));
        TestNull(TEXT("Invitation removes the creation form's entry bounds"),UI->Native.Find(TEXT("NoFellowshipNameEntryText")));
        UI->Action(TEXT("FellowshipAccept"));
        TestTrue(TEXT("Accept sends native fellowship command"),ReadCommands().Contains(TEXT("\"action\":\"accept\"")));
        Net.Handle(Invite);Tick();UI->Action(TEXT("FellowshipDecline"));
        TestTrue(TEXT("Decline sends native fellowship command"),ReadCommands().Contains(TEXT("\"action\":\"decline\"")));
        Net.Handle(Invite);Net.FellowshipInvitationUntil=FPlatformTime::Seconds()-1;Tick();
        TestTrue(TEXT("Expired invitation restores creation"),UI->Buttons.Contains(TEXT("FellowshipCreateButton")) && !UI->Buttons.Contains(TEXT("FellowshipAccept")));
        UI->FellowshipEntry->SetText(TEXT(""));Tick();
        TestTrue(TEXT("Name editing can clear the field"),UI->FellowshipEntry->Value().IsEmpty());
        Net.FellowshipInviter.Reset();Net.FellowshipName=TEXT("Dereth Travelers");Net.FellowshipLeader=TEXT("101");
        Net.Fellows={{TEXT("101"),TEXT("Inspector with a long name"),14,100,100,80,100,0}};Tick();
        ReadCommands();UI->Action(TEXT("Fellow:101"));
        TestEqual(TEXT("Roster click selects self"),Net.TargetId,FString(TEXT("101")));
        TestTrue(TEXT("Roster selection uses native targeting"),ReadCommands().Contains(TEXT("\"command\":\"target\"")));
        Net.CastSkill(0x55);
        TestTrue(TEXT("Selected self receives healing command"),ReadCommands().Contains(TEXT("\"target\":\"101\"")));
        Net.Fellows.Add({TEXT("909"),TEXT("Distant member"),14,100,100,80,100,0});
        UI->Action(TEXT("Fellow:909"));
        TestTrue(TEXT("Distant roster member cannot bypass scene visibility"),ReadCommands().IsEmpty());
        TestTrue(TEXT("Distant member gives feedback"),Mode->Notice.Contains(TEXT("not nearby")));
        Net.Fellows.SetNum(1);Tick();
        TestTrue(TEXT("Long member name fits its native column"),UI->Native.Nodes.ContainsByPredicate([](const auto& N){return N.Id==FAC2UILayout::Token(TEXT("FellowTemplateNameText")) && N.Text.EndsWith(TEXT("..."));}));
        auto Vital=Event(TEXT("fellowship_vital"),TEXT("101"));Vital->SetStringField(TEXT("field"),TEXT("health"));Vital->SetNumberField(TEXT("value"),73);
        Net.Handle(Vital);Tick();TestTrue(TEXT("Fellow health updates live"),Shows(TEXT("73 / 100")));
        Net.FellowshipLeader=TEXT("202");Tick();
        TestFalse(TEXT("Nonleaders cannot dismiss through native UI"),UI->Buttons.Contains(TEXT("FellowshipDismissButton")));
        for(int32 I=1;I<8;++I)Net.Fellows.Add({FString::FromInt(101+I),FString::Printf(TEXT("Member %d"),I),14,100,100,80,100,0});
        Tick();TestTrue(TEXT("Eight members fit via scrolling"),UI->JournalMaxScroll>0);
        UI->JournalGrab=0;UI->DragJournal(UI->JournalBar.Max.Y);
        TestTrue(TEXT("Final fellow can be reached"),Shows(TEXT("Member 7")));
        ReadCommands();UI->Action(TEXT("SocialAllegiance"));Tick();
        TestTrue(TEXT("Allegiance tab requests native profile"),ReadCommands().Contains(TEXT("\"command\":\"allegiance\"")));
        auto Allegiance=Event(TEXT("allegiance"),TEXT("101"));Allegiance->SetObjectField(TEXT("name"),Literal(TEXT("Travelers")));Allegiance->SetNumberField(TEXT("total"),2);
        auto Liege=[&](const TCHAR* Id,const TCHAR* Name){auto M=MakeShared<FJsonObject>();M->SetStringField(TEXT("id"),Id);M->SetObjectField(TEXT("name"),Literal(Name));M->SetNumberField(TEXT("level"),14);M->SetBoolField(TEXT("online"),true);return M;};
        Allegiance->SetObjectField(TEXT("patron"),Liege(TEXT("202"),TEXT("Patron")));Allegiance->SetObjectField(TEXT("monarch"),Liege(TEXT("202"),TEXT("Patron")));Allegiance->SetObjectField(TEXT("me"),Liege(TEXT("101"),TEXT("Inspector")));
        Net.Handle(Allegiance);Tick();
        TestEqual(TEXT("Allegiance coalesces monarch and patron"),Net.Lieges.Num(),2);
        TestTrue(TEXT("Allegiance renders server profile"),Shows(TEXT("Travelers"))&&Shows(TEXT("Patron")));
        UI->Action(TEXT("Liege:202"));Tick();UI->Action(TEXT("BreakButton"));
        TestTrue(TEXT("Break targets selected relationship"),ReadCommands().Contains(TEXT("\"id\":\"202\"")));
        auto Swear=Event(TEXT("allegiance_invitation"),TEXT("303"));Swear->SetObjectField(TEXT("name"),Literal(TEXT("Applicant")));Net.Handle(Swear);Tick();
        TestTrue(TEXT("Swear request exposes consent controls"),UI->Buttons.Contains(TEXT("AllegianceAccept"))&&UI->Buttons.Contains(TEXT("AllegianceDecline")));
        UI->Action(TEXT("AllegianceAccept"));TestTrue(TEXT("Acceptance returns to server"),ReadCommands().Contains(TEXT("\"action\":\"accept\"")));
        UI->Action(TEXT("SocialFellowship"));Tick();TestTrue(TEXT("Fellowship survives tab changes"),!Net.Fellows.IsEmpty());
        Net.ClearWorld();
        TestTrue(TEXT("Logout clears allegiance profile"),Net.Lieges.IsEmpty()&&!Net.AllegianceReceived&&Net.AllegianceApplicant.IsEmpty());
        TestFalse(TEXT("Logout clears the quest point balance"),Net.QuestPoints.IsSet());
        TestTrue(TEXT("Logout clears examination state"),Net.ExaminationId.IsEmpty() && Net.ExaminationRows.IsEmpty() && !Net.ExaminationPending);
    }
    FPlatformProcess::ClosePipe(Commands,nullptr);
    Cast<AAC2HUD>(PC->MyHUD)->GameUI.Reset();
    Mode->Network.Reset();
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
