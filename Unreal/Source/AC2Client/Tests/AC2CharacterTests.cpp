#include "AC2CharacterData.h"
#include "AC2DatArchive.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAC2CharacterCatalog Fixture()
{
    FAC2CharacterCatalog C;
    FAC2AvatarChoice A;
    A.Heights = {.95f, 1.f, 1.05f};
    A.Builds = {.9f, 1.f, 1.1f};
    A.Choices.Add(4, {{0x2000000c, 1, 0.f}, {0x2000000c, 1, .5f}});
    C.Avatars.Add(A);
    for (uint32 I = 1; I <= 4; ++I)
    {
        FAC2ItemDefinition D;
        D.Id = 0x47000000 + I;
        D.Name = TEXT("Fixture item");
        D.Slots = D.Preferred = I == 4 ? 2 : 64;
        D.HasDamage = true;
        D.Damage = 9;
        C.Items.Add(D.Id, D);
        C.Starters.FindOrAdd(1).Add(D.Id);
    }
    return C;
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CharacterNativeTest, "AC2.Data.NativeCharacterMatrix",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CharacterNativeTest::RunTest(const FString &)
{
    FAC2CharacterCatalog C;
    FString Error;
    const FString Dir = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if (!TestTrue(TEXT("installed native matrix and item metadata"), C.Open(Dir, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("five races / both sexes"), C.Avatars.Num(), 10);
    TestEqual(TEXT("Human male native entity"), C.Avatar(1, 4096)->Physics, 0x470000cdu);
    TestEqual(TEXT("Lugian female native entity"), C.Avatar(2, 8192)->Physics, 0x470000d1u);
    const auto &Human = *C.Avatar(1, 4096);
    TestEqual(TEXT("native head shapes"), Human.Choices[16].Num(), 12);
    TestEqual(TEXT("native face textures"), Human.Choices[2048].Num(), 11);
    TestEqual(TEXT("head mesh appearance table"), Human.Choices[16][0].Table, 0x2000000du);
    TestEqual(TEXT("native hair selector"), Human.Choices[32][0].Key, 12u);
    TestFalse(TEXT("native appearance labels localized"), Human.Labels[16].IsEmpty());
    TestEqual(TEXT("Human starter records"), C.Starters[1].Num(), 6);
    TestEqual(TEXT("Drudge starter records"), C.Starters[8].Num(), 3);
    // Starter DIDs were reassigned between client profiles. Resolve the sword
    // through this archive's Empyrean starter list, not a stock-only TMap key.
    const FAC2ItemDefinition* Sword=nullptr;
    if(const auto* Starters=C.Starters.Find(16))for(uint32 Id:*Starters)
        if(const auto* Item=C.Items.Find(Id);Item && Item->Name==TEXT("Decrepit Short Sword"))Sword=Item;
    if(TestNotNull(TEXT("native Empyrean starter sword"),Sword))
    {
        TestTrue(TEXT("native Empyrean weapon damage"), Sword->HasDamage);
        TestEqual(TEXT("native weapon damage value"), Sword->Damage, 9);
        TestTrue(TEXT("native item localization"), !Sword->Name.Contains(TEXT("wsl_")));
        TestEqual(TEXT("grammar suffix removed"), Sword->Name, TEXT("Decrepit Short Sword"));
    }
    // The scroll and armor families reuse template numbers in different archives.
    // Identify each sample by its localized noun before validating equipment data.
    static const struct {const TCHAR* Name;uint32 Stock,Icefire,Race,Slot;int32 Rating;} Samples[]={
        {TEXT("Reinforced Boots"),0x47001951,0x4700086e,1,16,3},
        {TEXT("Reinforced Greaves"),0x47001952,0x47000870,1,8,6},
        {TEXT("Reinforced Jacket"),0x47001954,0x47000872,1,2,12},
        {TEXT("Guardian Kilt"),0x4700195e,0x4700087c,2,8,6},
        {TEXT("Guardian Pauldron"),0x4700195f,0x4700087d,2,2,12},
        {TEXT("Guardian Sandals"),0x47001960,0x4700087e,2,16,3},
        {TEXT("Reed Breastplate"),0x47001968,0x47000886,4,2,12}
    };
    for(const auto& Sample:Samples)
    {
        int32 Matches=0;
        for(uint32 Id:{Sample.Stock,Sample.Icefire})
        {
            C.LoadNetworkItem(Dir,Id);
            const auto* Item=C.Items.Find(Id);
            if(!Item)continue;
            if(Item->Name.EndsWith(TEXT(" Scroll")))
                TestTrue(TEXT("Unwearable scroll has no armor or equipment slot"),
                    !Item->HasArmor && Item->Preferred==0 && Item->Slots==0 && Item->Race==0);
            if(Item->Name!=Sample.Name)continue;
            ++Matches;
            TestTrue(Sample.Name,Item->Race==Sample.Race && Item->Preferred==Sample.Slot &&
                (Item->Slots&Sample.Slot)!=0 && Item->HasArmor && Item->Armor==Sample.Rating);
        }
        TestEqual(Sample.Name,Matches,1);
    }
    FAC2DatArchive Portal;
    TArray<uint8> Bytes;
    TestTrue(TEXT("read source"), Portal.Open(FPaths::Combine(Dir, TEXT("portal.dat")), 1, Error) &&
                                      Portal.Read(0x70000390, Bytes, Error));
    TestFalse(TEXT("truncated package trailer is rejected"),
              C.DecodeMatrix(TConstArrayView<uint8>(Bytes.GetData(), Bytes.Num() - 1)));
    Bytes.SetNum(Bytes.Num() / 2);
    TestFalse(TEXT("truncated graph is rejected"), C.DecodeMatrix(Bytes));
    Bytes = {0x90, 3, 0, 0x70, 0xff, 0xff, 0xff, 0xff};
    TestFalse(TEXT("oversized graph is rejected"), C.DecodeMatrix(Bytes));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CharacterInventoryTest, "AC2.Data.CharacterInventoryTransactions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CharacterInventoryTest::RunTest(const FString &)
{
    auto C = Fixture();
    FAC2CharacterStore S;
    FString E;
    TestTrue(TEXT("create"), S.Create(TEXT("Test Adventurer"), 1, 4096, 1, 1, C, E));
    auto &P = *S.Active();
    TestTrue(TEXT("equip weapon"), P.Equip(0, C, E));
    TestEqual(TEXT("weapon slot"), P.Items[0].Equipped, 64u);
    TestEqual(TEXT("combat damage follows equipped native item"), P.Damage(C), 9);
    TestTrue(TEXT("replace weapon"), P.Equip(1, C, E));
    TestEqual(TEXT("old weapon returned to pack"), P.Items[0].Equipped, 0u);
    TestTrue(TEXT("replacement preserves valid inventory"), FAC2CharacterStore::Validate(P, C));
    TestTrue(TEXT("move across backpack pages"), P.Move(0, 95, E));
    TestEqual(TEXT("last page destination"), P.Items[0].Cell, 95);
    TestFalse(TEXT("equipped items cannot be moved into packs without unequipping"), P.Move(1, 40, E));
    TestTrue(TEXT("unequip"), P.Equip(1, C, E));
    P.Items.Reset();
    for (int32 I = 0; I < 96; ++I)
        P.Items.Add({0x47000001, 0, I});
    P.Items.Add({0x47000002, 64, INDEX_NONE});
    TestFalse(TEXT("full bag prevents item loss"), P.Equip(96, C, E));
    TestEqual(TEXT("failed transaction leaves equipment intact"), P.Items[96].Equipped, 64u);
    TestTrue(TEXT("full inventory remains valid"), FAC2CharacterStore::Validate(P, C));
    P.Items[1].Cell = 0;
    TestFalse(TEXT("duplicate cells rejected"), FAC2CharacterStore::Validate(P, C));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2CharacterPersistenceTest, "AC2.Data.CharacterPersistence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2CharacterPersistenceTest::RunTest(const FString &)
{
    auto C = Fixture();
    FAC2CharacterStore S;
    FString E;
    TestTrue(TEXT("trim and accept native name characters"),
             S.Create(TEXT("  Oren's-Scout  "), 1, 4096, 1, 1, C, E));
    TestFalse(TEXT("case insensitive duplicate"), S.Create(TEXT("oren's-scout"), 1, 4096, 1, 1, C, E));
    TestFalse(TEXT("reject path as name"), S.Create(TEXT("../Invalid"), 1, 4096, 1, 1, C, E));
    TestFalse(TEXT("reject unsupported appearance"), S.Create(TEXT("Other Scout"), 1, 4096, 9, 1, C, E));
    S.Active()->Appearance[4] = 1;
    S.Active()->Equip(0, C, E);
    S.Active()->Move(2, 75, E);
    FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
                                   FGuid::NewGuid().ToString() + TEXT(".json"));
    TestTrue(TEXT("save"), S.Save(Path, E));
    FAC2CharacterStore Restored;
    TestTrue(TEXT("load"), Restored.Load(Path, C, E));
    if (Restored.Active())
    {
        TestEqual(TEXT("appearance roundtrip"), Restored.Active()->Appearance[4], 1);
        TestEqual(TEXT("name roundtrip"), Restored.Active()->Name, TEXT("Oren's-Scout"));
        TestEqual(TEXT("equipped roundtrip"), Restored.Active()->Items[0].Equipped, 64u);
        TestEqual(TEXT("pack position roundtrip"), Restored.Active()->Items[2].Cell, 75);
    }
    else
        AddError(TEXT("Active character missing after load"));
    FString Legacy;
    FFileHelper::LoadFileToString(Legacy, *Path);
    Legacy.ReplaceInline(TEXT("\"version\": 2"), TEXT("\"version\": 1"));
    FFileHelper::SaveStringToFile(Legacy, *Path);
    FAC2CharacterStore Migrated;
    TestTrue(TEXT("version one profile migration"), Migrated.Load(Path, C, E));
    if (Migrated.Active())
        TestEqual(TEXT("migrated native appearance default"), Migrated.Active()->Appearance[4], 0);
    auto Invalid = *S.Active();
    Invalid.Appearance[4] = 999;
    TestFalse(TEXT("out of range appearance rejected"), FAC2CharacterStore::Validate(Invalid, C));
    Invalid.Appearance = {{128, 0}};
    TestFalse(TEXT("unsupported appearance type rejected"), FAC2CharacterStore::Validate(Invalid, C));
    FFileHelper::SaveStringToFile(TEXT("{\"version\":99}"), *Path);
    TestFalse(TEXT("unknown schema rejected"), Restored.Load(Path, C, E));
    TestEqual(TEXT("failed load preserves memory"), Restored.Characters.Num(), 1);
    FString Saved;
    FFileHelper::LoadFileToString(Saved, *Path);
    TestEqual(TEXT("failed load preserves disk"), Saved, TEXT("{\"version\":99}"));
    IFileManager::Get().Delete(*Path);
    return true;
}
#endif
