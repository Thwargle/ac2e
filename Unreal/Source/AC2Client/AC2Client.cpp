#include "AC2Fog.h"
#include "Modules/ModuleManager.h"

class FAC2ClientModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
        StartupAC2Fog();
    }
    virtual void ShutdownModule() override
    {
        ShutdownAC2Fog();
        FDefaultGameModuleImpl::ShutdownModule();
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FAC2ClientModule, AC2Client, "AC2Unreal");
