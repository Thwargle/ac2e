#include "AC2CharacterData.h"
#include "AC2DatArchive.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CharacterReferenceTest, "AC2.Retail.CharacterDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CharacterReferenceTest::RunTest(const FString&)
{
    FString Path, Json, Error;
    TSharedPtr<FJsonObject> Oracle;
    if (!FParse::Value(FCommandLine::Get(), TEXT("AC2CharacterOracle="), Path) ||
        !FFileHelper::LoadFileToString(Json, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Oracle) || !Oracle)
    {
        AddError(TEXT("Run Tools/RetailProbe and supply its JSON with -AC2CharacterOracle=<path>."));
        return false;
    }
    FAC2CharacterCatalog Catalog;
    if (!TestTrue(TEXT("Native matrix"), Catalog.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")), Error)))
    { AddError(Error); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Avatars;
    if (!TestTrue(TEXT("Reference contains avatars"), Oracle->TryGetArrayField(TEXT("avatars"), Avatars))) return false;
    TestEqual(TEXT("All native race/sex pairs covered"), Avatars->Num(), Catalog.Avatars.Num());
    FAC2CharacterRandom Random;
    for (const auto& Row : *Avatars)
    {
        const auto& Ref = Row->AsObject();
        if (!Ref) return false;
        const uint32 Race = Ref->GetIntegerField(TEXT("race")), Sex = Ref->GetIntegerField(TEXT("sex"));
        const auto* Avatar = Catalog.Avatar(Race, Sex);
        if (!TestNotNull(TEXT("Reference avatar"), Avatar)) continue;
        TestEqual(TEXT("Avatar physics matches native script"), Avatar->Physics, uint32(Ref->GetIntegerField(TEXT("physics"))));
        const auto& Held=Ref->GetArrayField(TEXT("held_objects"));
        TestEqual(TEXT("Preview held-object count matches native script, including empty lists"),Avatar->PreviewHeldObjects.Num(),Held.Num());
        for(const auto& Object:Held)
        {
            const auto& Pair=Object->AsArray();
            if(!TestEqual(TEXT("Holding/object pair"),Pair.Num(),2))continue;
            const auto* Entity=Avatar->PreviewHeldObjects.Find(uint32(Pair[0]->AsNumber()));
            if(TestNotNull(TEXT("Native holding location"),Entity))
                TestEqual(TEXT("Native preview entity"),*Entity,uint32(Pair[1]->AsNumber()));
        }
        const FAC2CharacterDraft Draft = FAC2CharacterDraft::Create(*Avatar, Random);
        for (const auto& Choice : Ref->GetArrayField(TEXT("choices")))
        {
            const auto& C = Choice->AsObject();
            const uint32 Kind = C->GetIntegerField(TEXT("kind"));
            const FString Label = FString::Printf(TEXT("race=%u sex=%u physique=%u"), Race, Sex, Kind);
            TArray<float> Values;
            int32 Index = 0;
            if (Kind == 1) { Values = Avatar->Builds; Index = Draft.Build; }
            else if (Kind == 2) { Values = Avatar->Heights; Index = Draft.Height; }
            else if (const auto* Options = Avatar->Choices.Find(Kind))
            {
                for (const auto& Option : *Options) Values.Add(Option.Value);
                Index = Draft.Appearance.FindRef(Kind);
            }
            const auto& NativeValues = C->GetArrayField(TEXT("values"));
            TestEqual(Label + TEXT(" list count matches script"), Values.Num(), NativeValues.Num());
            for (int32 I = 0; I < FMath::Min(Values.Num(), NativeValues.Num()); ++I)
                TestEqual(Label + FString::Printf(TEXT(" list value %d"), I), Values[I], float(NativeValues[I]->AsNumber()));
            const float Value = Values.IsValidIndex(Index) ? Values[Index] : 0.f;
            TestEqual(Label + TEXT(" initial value matches SetCGRDefaults"), Value, float(C->GetNumberField(TEXT("value"))));
            if (!Values.IsEmpty()) TestEqual(Label + TEXT(" initial index matches script"), Index, C->GetIntegerField(TEXT("index")));
        }
        // A tab-scoped randomization must not reset another tab or body scale.
        auto Clothes = Draft;
        auto TabRandom = Random;
        Clothes.Randomize(*Avatar, 0x700, TabRandom);
        TestEqual(TEXT("Clothing randomization retains height"), Clothes.Height, Draft.Height);
        TestEqual(TEXT("Clothing randomization retains build"), Clothes.Build, Draft.Build);
        for (const auto& Pair : Draft.Appearance)
            if (!(Pair.Key & 0x700)) TestEqual(TEXT("Clothing randomization retains head/skin"), Clothes.Appearance[Pair.Key], Pair.Value);
    }
    return true;
}
#endif
