#include "AC2Chat.h"
#include "AC2Font.h"
#include "AC2CharacterData.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ChatTest, "AC2.Data.LocalChat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2ChatTest::RunTest(const FString &)
{
    auto Say = FAC2ChatRequest::Parse(TEXT(" /S Hello Dereth! "));
    TestTrue(TEXT("native say alias"), Say.Command == EAC2ChatCommand::Say);
    TestEqual(TEXT("trimmed speech"), Say.Text, FString(TEXT("Hello Dereth!")));
    TestTrue(TEXT("emote"), FAC2ChatRequest::Parse(TEXT("/me waves")).Command == EAC2ChatCommand::Emote);
    TestTrue(TEXT("offline channels explicit"),
             FAC2ChatRequest::Parse(TEXT("/tell Ari Hi")).Command == EAC2ChatCommand::Unavailable);
    TestTrue(TEXT("no console escape"),
             FAC2ChatRequest::Parse(TEXT("/quit")).Command == EAC2ChatCommand::Invalid);
    TestTrue(TEXT("no multiline"),
             FAC2ChatRequest::Parse(TEXT("hi\nquit")).Command == EAC2ChatCommand::Invalid);
    TestTrue(TEXT("bounded message"),
             FAC2ChatRequest::Parse(FString::ChrN(513, 'A')).Command == EAC2ChatCommand::Invalid);
    FAC2ChatLog Log;
    Log.Remember(TEXT("one"));
    Log.Remember(TEXT("two"));
    Log.Remember(TEXT("two"));
    TestEqual(TEXT("history deduplicates"), Log.History.Num(), 2);
    TestEqual(TEXT("previous"), Log.Recall(-1, TEXT("draft")), FString(TEXT("two")));
    TestEqual(TEXT("older"), Log.Recall(-1, TEXT("two")), FString(TEXT("one")));
    TestEqual(TEXT("oldest clamped"), Log.Recall(-1, TEXT("one")), FString(TEXT("one")));
    Log.Recall(1, TEXT("one"));
    TestEqual(TEXT("unsent draft restored"), Log.Recall(1, TEXT("two")), FString(TEXT("draft")));
    for (int32 I = 0; I < 250; ++I)
    {
        Log.Add(EAC2ChatType::Local, FString::FromInt(I));
        Log.Remember(FString::FromInt(I));
    }
    TestEqual(TEXT("log bounded"), Log.Lines.Num(), 200);
    TestEqual(TEXT("newest retained"), Log.Lines.Last().Text, FString(TEXT("249")));
    TestEqual(TEXT("history bounded"), Log.History.Num(), 50);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2ChatFontTest, "AC2.Data.GameTextGlyphs",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAC2ChatFontTest::RunTest(const FString &)
{
    FAC2Fonts Fonts;
    FString Error;
    if (!TestTrue(TEXT("DAT fonts open"),
                  Fonts.Open(AC2::ResolveDatDirectory(TEXT("../Asheron's Call 2 - Legions")), Error)))
        return false;
    auto Font = Fonts.Get(0x4200000c);
    if (!TestTrue(TEXT("chat font"), Font.IsValid()))
        return false;
    auto Entry = SNew(SAC2TextEntry).Font(Font).NameOnly(false).MaxCharacters(512);
    TestTrue(TEXT("chat punctuation and numbers"),
             Entry->ReplaceSelection(TEXT("/say Hello! 123: (ready?)")));
    TestFalse(TEXT("no unsupported replacement glyphs"), Entry->ReplaceSelection(FString::Chr(0xffff)));
    TestFalse(TEXT("no newlines"), Entry->ReplaceSelection(TEXT("\n")));
    Entry->SetText(FString::ChrN(512, 'A'));
    TestEqual(TEXT("512 characters accepted"), Entry->Value().Len(), 512);
    TestFalse(TEXT("513 rejected"), Entry->ReplaceSelection(TEXT("B")));
    auto Label = Fonts.Get(0x42000020);
    if (!TestTrue(TEXT("selection font"), Label.IsValid()))
        return false;
    for (uint32 Race : {1u, 2u, 4u, 8u, 16u})
        for (auto Sex : {TEXT(" / Male"), TEXT(" / Female")})
            for (TCHAR C : FAC2CharacterCatalog::RaceName(Race) + Sex)
                TestTrue(TEXT("selection labels never use fallback"), Label->Map.Contains(uint16(C)));
    return true;
}
#endif
