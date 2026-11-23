#include "AC2LoginProfiles.h"
#include "AC2DatArchive.h"
#include "AC2CharacterData.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2LoginProfilesTest,"AC2.Login.ProfilePersistenceAndCredentials",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2LoginProfilesTest::RunTest(const FString&)
{
    const FString File=FPaths::ProjectSavedDir()/TEXT("Diagnostics/Login-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".json");
    FAC2LoginProfiles Profiles;FString Error;
    TestTrue(TEXT("Load absent profiles"),Profiles.Load(File,Error));Profiles.Defaults(AC2::ResolveDatDirectory(TEXT("")));
    TestEqual(TEXT("Separate AC2E and Icefire defaults"),Profiles.Servers.Num(),2);
    auto& Server=Profiles.Servers[1];TestTrue(TEXT("Icefire direct GLS"),Server.Icefire);TestEqual(TEXT("Icefire game port"),Server.Port,8610);
    FAC2AccountProfile Account{FGuid::NewGuid().ToString(EGuidFormats::Digits),Server.Id,TEXT("ProfileTest"),TEXT("")};
    const FString Secret=FGuid::NewGuid().ToString()+TEXT("-OnlyForThisTest");
    TestTrue(TEXT("Store in current user's credential vault"),Profiles.StorePassword(Account,Server,Secret,Error));
    Profiles.Accounts.Add(Account);Profiles.SelectedServer=Server.Id;Profiles.SelectedAccount=Account.Id;
    TestTrue(TEXT("Save metadata"),Profiles.Save(Error));
    FString Json;FFileHelper::LoadFileToString(Json,*File);TestFalse(TEXT("JSON never contains the password"),Json.Contains(Secret));
    TestFalse(TEXT("JSON has no password field"),Json.Contains(TEXT("\"password\"")));
    FAC2LoginProfiles Reloaded;TestTrue(TEXT("Reload profiles"),Reloaded.Load(File,Error));
    TestEqual(TEXT("Restore selected server"),Reloaded.SelectedServer,Server.Id);TestEqual(TEXT("Restore selected account"),Reloaded.SelectedAccount,Account.Id);
    FString Read;TestTrue(TEXT("Read vault after restart"),Reloaded.ReadPassword(Reloaded.Accounts[0],Reloaded.Servers[1],Read));TestEqual(TEXT("Password roundtrip"),Read,Secret);Read.Reset();
    auto Changed=Server;Changed.Host=TEXT("other.example");TestFalse(TEXT("Never reuse saved password on a different host"),Reloaded.ReadPassword(Account,Changed,Read));
    Changed=Server;Changed.Port++;TestFalse(TEXT("Never reuse saved password on a different port"),Reloaded.ReadPassword(Account,Changed,Read));
    Changed=Server;Changed.Icefire=false;TestFalse(TEXT("Never reuse saved password across server types"),Reloaded.ReadPassword(Account,Changed,Read));
    auto Other=Account;Other.Username=TEXT("OtherUser");TestFalse(TEXT("Never reuse a credential for a changed username"),Reloaded.ReadPassword(Other,Server,Read));
    Changed=Profiles.Servers[0];FAC2AccountProfile Local{FGuid::NewGuid().ToString(EGuidFormats::Digits),Changed.Id,TEXT("localtest"),TEXT("")};
    TestTrue(TEXT("AC2E credential"),Profiles.StorePassword(Local,Changed,Secret,Error));
    Changed.Endpoint=TEXT("https://other.example");TestFalse(TEXT("Account service URL is part of credential identity"),Profiles.ReadPassword(Local,Changed,Read));
    TestTrue(TEXT("Remove AC2E credential"),Profiles.ForgetPassword(Local,Error));
    TestTrue(TEXT("Remove Icefire credential"),Reloaded.ForgetPassword(Reloaded.Accounts[0],Error));
    TestFalse(TEXT("Removed password cannot be read"),Profiles.ReadPassword(Account,Server,Read));
    FFileHelper::SaveStringToFile(TEXT("broken json"),*File);FAC2LoginProfiles Broken;
    TestFalse(TEXT("Reject corrupt file"),Broken.Load(File,Error));Broken.Defaults(TEXT("unused"));TestFalse(TEXT("Preserve corrupt file instead of overwriting"),Broken.Save(Error));
    FFileHelper::LoadFileToString(Json,*File);TestEqual(TEXT("Original damaged content retained"),Json,FString(TEXT("broken json")));
    IFileManager::Get().Delete(*File);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAC2MultipleDatProfilesTest,"AC2.Login.MultipleDatProfiles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAC2MultipleDatProfilesTest::RunTest(const FString&)
{
    const FString Original=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Asheron's Call 2 - Legions"));
    const FString Icefire=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Saved/IcefireClient"));
    FString Error;TestTrue(TEXT("Original DAT set remains available"),FAC2LoginProfiles::ValidateData(Original,Error));
    if(!IFileManager::Get().FileExists(*(Icefire/TEXT("portal.dat")))){AddInfo(TEXT("Icefire optional DAT fixture is not installed"));return true;}
    TestTrue(TEXT("Icefire DAT set is valid"),FAC2LoginProfiles::ValidateData(Icefire,Error));
    TestTrue(TEXT("Explicit server path beats command line default"),FPaths::IsSamePath(AC2::ResolveDatDirectory(Icefire),Icefire));
    FAC2DatArchive A,B;TArray<FAC2DatEntry> AE,BE;
    TestTrue(TEXT("Open and enumerate both portal archives"),A.Open(Original/TEXT("portal.dat"),1,Error)&&B.Open(Icefire/TEXT("portal.dat"),1,Error)&&A.Enumerate(AE,Error)&&B.Enumerate(BE,Error));
    TestTrue(TEXT("Different data sets actually loaded"),AE.Num()!=BE.Num());
    FAC2CharacterCatalog First,Second;
    TestTrue(TEXT("Original character catalog"),First.Open(Original,Error));
    TestTrue(TEXT("Icefire character catalog"),Second.Open(Icefire,Error));
    return true;
}
#endif
