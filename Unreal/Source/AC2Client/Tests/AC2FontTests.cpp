#include "AC2Font.h"
#include "AC2UILayout.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2FontTest, "AC2.Data.NativeFonts",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2FontTest::RunTest(const FString &)
{
    FAC2DatArchive Portal, Local;
    FString Error, Dir = AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions"));
    if (!TestTrue(TEXT("font source"), Portal.Open(FPaths::Combine(Dir, TEXT("portal.dat")), 1, Error)))
        return false;
    TArray<FAC2DatEntry> Entries;
    Portal.Enumerate(Entries, Error);
    int32 Count = 0;
    for (const auto &E : Entries)
        if (E.Id >> 24 == 0x42)
        {
            TArray<uint8> Bytes;
            FAC2FontData Font;
            if (!TestTrue(FString::Printf(TEXT("DAT font %08X"), E.Id),
                          Portal.Read(E.Id, Bytes, Error) && Font.Decode(E.Id, Bytes)))
                continue;
            ++Count;
            const auto *A = Font.Glyph('A');
            TestTrue(TEXT("authored glyph, not replacement"), A && A->Unicode == 'A');
            int32 Ink = 0;
            if (A)
                for (uint32 Y = 0; Y < Font.Height; ++Y)
                    for (uint32 X = 0; X < A->Width; ++X)
                        Ink += Font.Pixel(A->Offset + X, Y) > 0;
            TestTrue(TEXT("native glyph coverage"), Ink > 0);
            TestEqual(TEXT("multiline native advances"), Font.Width(TEXT("Hi\nthere")),
                      Font.Width(TEXT("there")));
            TestEqual(TEXT("missing glyph uses native question mark"), Font.Glyph(0xffff)->Unicode,
                      uint16('?'));
            Bytes.Pop();
            TestFalse(TEXT("truncated font rejected"), Font.Decode(E.Id, Bytes));
        }
    TestTrue(TEXT("all installed font families examined"), Count >= 50);
    TArray<uint8> Bytes;
    FAC2UILayout Layout;
    TestTrue(TEXT("native creator layout"),
             Local.Open(FPaths::Combine(Dir, TEXT("local_English.dat")), 3, Error) &&
                 Local.Read(0x22000007, Bytes, Error) && Layout.Decode(0x22000007, Bytes));
    TestEqual(TEXT("creator button resolves referenced native font"), Layout.Font(TEXT("RandomizeAllButton")),
              0x4200001du);
    TestTrue(TEXT("native naming layout"),
             Local.Read(0x22000008, Bytes, Error) && Layout.Decode(0x22000008, Bytes));
    TestEqual(TEXT("name entry has distinct native font"), Layout.Font(TEXT("NameEntry")), 0x42000024u);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2NameInputTest, "AC2.Data.NativeNameEntry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2NameInputTest::RunTest(const FString &)
{
    auto Entry = SNew(SAC2TextEntry).Text(TEXT("A"));
    TestTrue(TEXT("native alphabet"), Entry->ReplaceSelection(TEXT("ri's-Name")));
    TestEqual(TEXT("text insertion"), Entry->Value(), FString(TEXT("Ari's-Name")));
    TestFalse(TEXT("digits rejected"), Entry->ReplaceSelection(TEXT("1")));
    TestFalse(TEXT("pasted multiline rejected"), Entry->ReplaceSelection(TEXT("\nTest")));
    TestFalse(TEXT("sixteen character native limit"), Entry->ReplaceSelection(TEXT(" Too Long")));
    return true;
}
#endif
