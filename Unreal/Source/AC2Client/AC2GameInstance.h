#pragma once
#include "Engine/GameInstance.h"
#include "AC2Network.h"
#include "AC2GameInstance.generated.h"

// A map reload discards every DAT-backed renderer, font, model and catalog.
// Only the pending connection survives, in memory; never in travel URL/options.
UCLASS()
class UAC2GameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    FString DatDirectory,AccountEndpoint;
    TOptional<FAC2ConnectionSettings> PendingLogin;
    virtual void Shutdown() override { PendingLogin.Reset();Super::Shutdown(); }
};
