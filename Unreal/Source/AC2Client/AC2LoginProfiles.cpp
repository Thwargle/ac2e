#include "AC2LoginProfiles.h"
#include "AC2DatArchive.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
namespace {
bool Match(const FString& V,const TCHAR* Pattern){FRegexMatcher M(FRegexPattern(Pattern),V);return M.FindNext()&&M.GetMatchBeginning()==0&&M.GetMatchEnding()==V.Len();}
bool IdValid(const FString& V){FGuid G;return FGuid::ParseExact(V,EGuidFormats::Digits,G);}
FString Id(){return FGuid::NewGuid().ToString(EGuidFormats::Digits);}
}
FString FAC2ServerProfile::Scope() const
{
    FString URL=Endpoint.TrimStartAndEnd().ToLower();URL.RemoveFromEnd(TEXT("/"));
    return FString::Printf(TEXT("%s|%s|%d|%s"),Icefire?TEXT("icefire"):TEXT("ac2e"),*Host.ToLower(),Port,Icefire?TEXT(""):*URL);
}
bool FAC2LoginProfiles::Validate(const FAC2ServerProfile& S,FString& Error)
{
    if(S.Name.TrimStartAndEnd().IsEmpty()||S.Name.Len()>64){Error=TEXT("Give the server a name (1-64 characters).");return false;}
    if(!Match(S.Host,TEXT("[a-zA-Z0-9.-]+"))||S.Port<1||S.Port>65534){Error=TEXT("Enter a host name or IPv4 address and a game port from 1 to 65534.");return false;}
    if(S.Protocol!=TEXT("7148")&&S.Protocol!=TEXT("7160")&&S.Protocol!=TEXT("7230")){Error=TEXT("Client version must be 7148, 7160 or 7230.");return false;}
    if(!S.Icefire && !S.Endpoint.IsEmpty() && !Match(S.Endpoint,TEXT("https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/?")) &&
       !Match(S.Endpoint,TEXT("http://(127\\.0\\.0\\.1|localhost)(:[0-9]{1,5})?/?")))
    {Error=TEXT("AC2E account service needs HTTPS, or local HTTP on localhost/127.0.0.1.");return false;}
    if(S.DatDirectory.IsEmpty()){Error=TEXT("Select the server's DAT folder.");return false;}
    return true;
}
bool FAC2LoginProfiles::ValidateData(const FString& Directory,FString& Error)
{
    for(const auto& Pair:TArray<TPair<FString,uint32>>{{TEXT("portal.dat"),1},{TEXT("cell_1.dat"),2},{TEXT("local_English.dat"),3}}){
        FAC2DatArchive Archive;if(!Archive.Open(Directory/Pair.Key,Pair.Value,Error)){Error=TEXT("DAT folder: ")+Error;return false;}
    }
    return true;
}
void FAC2LoginProfiles::Defaults(const FString& DefaultDat)
{
    if(!Servers.IsEmpty())return;
    Servers.Add({Id(),TEXT("AC2E local"),TEXT("127.0.0.1"),TEXT("http://127.0.0.1:19400"),TEXT("7148"),DefaultDat,9000,false});
    FString IceDat=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Saved/IcefireClient"));
    if(!IFileManager::Get().FileExists(*(IceDat/TEXT("portal.dat"))))IceDat.Reset();
    Servers.Add({Id(),TEXT("Icefire"),TEXT("icefire.portalspace.net"),TEXT(""),TEXT("7160"),IceDat,8610,true});
    SelectedServer=Servers[0].Id;
}
bool FAC2LoginProfiles::Load(const FString& File,FString& Error)
{
    Path=FPaths::ConvertRelativePathToFull(File);Servers.Reset();Accounts.Reset();SelectedServer.Reset();SelectedAccount.Reset();Writable=true;
    if(!IFileManager::Get().FileExists(*Path))return true;
    FString Text;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>> *S=nullptr,*A=nullptr;double Version=0;
    auto Bad=[&](){Writable=false;Servers.Reset();Accounts.Reset();Error=TEXT("Saved sign-in profiles could not be read. The original file has been preserved.");return false;};
    if(IFileManager::Get().FileSize(*Path)>1024*1024||!FFileHelper::LoadFileToString(Text,*Path)||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid()||
       !Root->TryGetNumberField(TEXT("version"),Version)||Version!=1||!Root->TryGetArrayField(TEXT("servers"),S)||
       !Root->TryGetArrayField(TEXT("accounts"),A)||S->Num()>100||A->Num()>500)return Bad();
    TSet<FString> Ids;
    for(const auto& V:*S){
        const TSharedPtr<FJsonObject>* J=nullptr;FAC2ServerProfile P;FString Type;double Port=0;
        if(!V->TryGetObject(J)||!J||!J->IsValid()||!(*J)->TryGetStringField(TEXT("id"),P.Id)||!IdValid(P.Id)||Ids.Contains(P.Id)||
           !(*J)->TryGetStringField(TEXT("name"),P.Name)||!(*J)->TryGetStringField(TEXT("type"),Type)||
           (Type!=TEXT("ac2e")&&Type!=TEXT("icefire"))||!(*J)->TryGetStringField(TEXT("host"),P.Host)||
           !(*J)->TryGetStringField(TEXT("endpoint"),P.Endpoint)||!(*J)->TryGetStringField(TEXT("protocol"),P.Protocol)||
           !(*J)->TryGetStringField(TEXT("dat_directory"),P.DatDirectory)||!(*J)->TryGetNumberField(TEXT("port"),Port)||Port<1||Port>65534||Port!=FMath::FloorToDouble(Port))return Bad();
        P.Icefire=Type==TEXT("icefire");P.Port=int32(Port);Ids.Add(P.Id);Servers.Add(P);
    }
    TSet<FString> AccountIds;
    for(const auto& V:*A){
        const TSharedPtr<FJsonObject>* J=nullptr;FAC2AccountProfile P;
        if(!V->TryGetObject(J)||!J||!J->IsValid()||!(*J)->TryGetStringField(TEXT("id"),P.Id)||!IdValid(P.Id)||AccountIds.Contains(P.Id)||
           !(*J)->TryGetStringField(TEXT("server_id"),P.ServerId)||!Ids.Contains(P.ServerId)||!(*J)->TryGetStringField(TEXT("username"),P.Username)||
           !Match(P.Username,TEXT("[a-zA-Z0-9_]{1,32}"))||!(*J)->TryGetStringField(TEXT("credential_scope"),P.CredentialScope)||P.CredentialScope.Len()>512)return Bad();
        AccountIds.Add(P.Id);Accounts.Add(P);
    }
    Root->TryGetStringField(TEXT("selected_server"),SelectedServer);Root->TryGetStringField(TEXT("selected_account"),SelectedAccount);
    if(!Ids.Contains(SelectedServer))SelectedServer=Servers.IsEmpty()?TEXT(""):Servers[0].Id;
    if(!Accounts.ContainsByPredicate([&](const auto& P){return P.Id==SelectedAccount&&P.ServerId==SelectedServer;}))SelectedAccount.Reset();
    return true;
}
bool FAC2LoginProfiles::Save(FString& Error) const
{
    if(!Writable){Error=TEXT("Cannot overwrite unreadable profiles. Restore or rename Saved/Login/profiles.json first.");return false;}
    if(Servers.Num()>100||Accounts.Num()>500){Error=TEXT("The saved-profile limit is 100 servers and 500 accounts.");return false;}
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),1);
    TArray<TSharedPtr<FJsonValue>> S,A;
    for(const auto& P:Servers){auto J=MakeShared<FJsonObject>();
        J->SetStringField(TEXT("id"),P.Id);J->SetStringField(TEXT("name"),P.Name);J->SetStringField(TEXT("type"),P.Icefire?TEXT("icefire"):TEXT("ac2e"));
        J->SetStringField(TEXT("host"),P.Host);J->SetNumberField(TEXT("port"),P.Port);J->SetStringField(TEXT("endpoint"),P.Endpoint);
        J->SetStringField(TEXT("protocol"),P.Protocol);J->SetStringField(TEXT("dat_directory"),P.DatDirectory);S.Add(MakeShared<FJsonValueObject>(J));}
    for(const auto& P:Accounts){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),P.Id);J->SetStringField(TEXT("server_id"),P.ServerId);
        J->SetStringField(TEXT("username"),P.Username);J->SetStringField(TEXT("credential_scope"),P.CredentialScope);A.Add(MakeShared<FJsonValueObject>(J));}
    Root->SetArrayField(TEXT("servers"),S);Root->SetArrayField(TEXT("accounts"),A);
    Root->SetStringField(TEXT("selected_server"),SelectedServer);Root->SetStringField(TEXT("selected_account"),SelectedAccount);
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    const FString Temp=Path+TEXT(".")+Id()+TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if(!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!IFileManager::Get().Move(*Path,*Temp,true,true,false,true))
    {IFileManager::Get().Delete(*Temp);Error=TEXT("Could not save sign-in profiles.");return false;}
    return true;
}
FString FAC2LoginProfiles::CredentialTarget(const FAC2AccountProfile& Account) const
{
    return TEXT("AC2Unreal/")+FMD5::HashAnsiString(*Path.ToLower())+TEXT("/")+Account.Id;
}
bool FAC2LoginProfiles::StorePassword(FAC2AccountProfile& Account,const FAC2ServerProfile& Server,const FString& Password,FString& Error) const
{
    if(!Writable||Password.IsEmpty()||Password.Len()>128){Error=TEXT("Enter a password before saving it.");return false;}
#if PLATFORM_WINDOWS
    const FString Target=CredentialTarget(Account),Scope=Server.Scope()+TEXT("|")+Account.Username;
    CREDENTIALW C{};C.Type=CRED_TYPE_GENERIC;C.TargetName=const_cast<wchar_t*>(*Target);C.UserName=const_cast<wchar_t*>(*Scope);
    C.CredentialBlobSize=Password.Len()*sizeof(TCHAR);C.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<TCHAR*>(*Password));C.Persist=CRED_PERSIST_LOCAL_MACHINE;
    if(CredWriteW(&C,0)){Account.CredentialScope=Scope;return true;}
#endif
    Error=TEXT("Windows could not save this password in Credential Manager.");return false;
}
bool FAC2LoginProfiles::ReadPassword(const FAC2AccountProfile& Account,const FAC2ServerProfile& Server,FString& Password) const
{
    Password.Reset();const FString Scope=Server.Scope()+TEXT("|")+Account.Username;
    if(Account.CredentialScope!=Scope)return false;
#if PLATFORM_WINDOWS
    PCREDENTIALW C=nullptr;
    if(CredReadW(*CredentialTarget(Account),CRED_TYPE_GENERIC,0,&C)){
        const bool OK=C->UserName&&Scope==C->UserName&&C->CredentialBlobSize>0&&C->CredentialBlobSize<=128*sizeof(TCHAR)&&C->CredentialBlobSize%sizeof(TCHAR)==0;
        if(OK)Password=FString(C->CredentialBlobSize/sizeof(TCHAR),reinterpret_cast<TCHAR*>(C->CredentialBlob));
        if(C->CredentialBlob)FMemory::Memzero(C->CredentialBlob,C->CredentialBlobSize);CredFree(C);return OK;
    }
#endif
    return false;
}
bool FAC2LoginProfiles::ForgetPassword(FAC2AccountProfile& Account,FString& Error) const
{
#if PLATFORM_WINDOWS
    if(CredDeleteW(*CredentialTarget(Account),CRED_TYPE_GENERIC,0)||GetLastError()==ERROR_NOT_FOUND){Account.CredentialScope.Reset();return true;}
#endif
    Error=TEXT("Windows could not remove the saved password.");return false;
}
