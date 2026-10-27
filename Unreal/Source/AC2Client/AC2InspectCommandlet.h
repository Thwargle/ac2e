#pragma once
#include "Commandlets/Commandlet.h"
#include "AC2InspectCommandlet.generated.h"

UCLASS()
class UAC2InspectCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    UAC2InspectCommandlet();
    virtual int32 Main(const FString &Params) override;
};
