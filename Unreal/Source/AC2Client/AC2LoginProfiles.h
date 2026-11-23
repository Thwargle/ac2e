#pragma once
#include "CoreMinimal.h"

struct FAC2ServerProfile
{
    FString Id,Name,Host,Endpoint,Protocol,DatDirectory;
    int32 Port=9000;
    bool Icefire=false;
    FString Scope() const;
};
struct FAC2AccountProfile
{
    FString Id,ServerId,Username,CredentialScope;
};
// Only names and connection settings go in JSON. Secrets belong to the current
// Windows user's credential vault, bound to the exact server/account identity.
class FAC2LoginProfiles
{
public:
    TArray<FAC2ServerProfile> Servers;
    TArray<FAC2AccountProfile> Accounts;
    FString SelectedServer,SelectedAccount;
    bool Load(const FString& File,FString& Error);
    bool Save(FString& Error) const;
    const FString& Filename() const {return Path;}
    void Defaults(const FString& DefaultDat);
    bool StorePassword(FAC2AccountProfile& Account,const FAC2ServerProfile& Server,const FString& Password,FString& Error) const;
    bool ReadPassword(const FAC2AccountProfile& Account,const FAC2ServerProfile& Server,FString& Password) const;
    bool ForgetPassword(FAC2AccountProfile& Account,FString& Error) const;
    static bool Validate(const FAC2ServerProfile& Server,FString& Error);
    static bool ValidateData(const FString& Directory,FString& Error);
private:
    FString Path;
    bool Writable=true;
    FString CredentialTarget(const FAC2AccountProfile& Account) const;
};
