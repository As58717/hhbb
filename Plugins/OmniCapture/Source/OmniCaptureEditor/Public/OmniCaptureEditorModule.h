#pragma once

#include "Delegates/DelegateHandle.h"
#include "Modules/ModuleInterface.h"

class FSpawnTabArgs;
class SDockTab;

class FOmniCaptureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnCaptureTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
    void HandleOpenPanel();

    FDelegateHandle MenuRegistrationHandle;
};
