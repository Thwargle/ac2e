#pragma once
#include "CoreMinimal.h"
struct FAC2AccountReply {uint32 Status=0;FString Body;};
// Worker-thread request; no redirects, system certificate validation, bounded response and timeout.
FAC2AccountReply AC2AccountLogin(const FString& URL,const FString& JSON);
