#include "OmniCaptureEditorModule.h"

#include "LevelEditor.h"
#include "OmniCaptureEditorSettings.h"
#include "SOmniCaptureControlPanel.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

static const FName OmniCapturePanelTabName(TEXT("OmniCapturePanel"));

void FOmniCaptureEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OmniCapturePanelTabName, FOnSpawnTab::CreateRaw(this, &FOmniCaptureEditorModule::SpawnCaptureTab))
        .SetDisplayName(NSLOCTEXT("OmniCaptureEditor", "CapturePanelTitle", "Omni Capture"))
        .SetTooltipText(NSLOCTEXT("OmniCaptureEditor", "CapturePanelTooltip", "Open the Omni Capture control panel"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

    MenuRegistrationHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOmniCaptureEditorModule::RegisterMenus));

    if (const UOmniCaptureEditorSettings* Settings = GetDefault<UOmniCaptureEditorSettings>())
    {
        if (Settings->bAutoOpenPanel)
        {
            FGlobalTabmanager::Get()->TryInvokeTab(OmniCapturePanelTabName);
        }
    }
}

void FOmniCaptureEditorModule::ShutdownModule()
{
    if (UToolMenus::IsAvailable())
    {
        if (MenuRegistrationHandle.IsValid())
        {
            UToolMenus::UnregisterStartupCallback(MenuRegistrationHandle);
        }
        UToolMenus::UnregisterOwner(this);
    }

    MenuRegistrationHandle = FDelegateHandle();

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OmniCapturePanelTabName);
}

TSharedRef<SDockTab> FOmniCaptureEditorModule::SpawnCaptureTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SOmniCaptureControlPanel)
        ];
}

void FOmniCaptureEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar"))
    {
        FToolMenuSection& Section = Menu->FindOrAddSection("OmniCapture");
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("OmniCaptureToggle"),
            FUIAction(FExecuteAction::CreateRaw(this, &FOmniCaptureEditorModule::HandleOpenPanel)),
            NSLOCTEXT("OmniCaptureEditor", "ToolbarLabel", "Omni Capture"),
            NSLOCTEXT("OmniCaptureEditor", "ToolbarTooltip", "Open the Omni Capture control panel"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details")));
    }
}

void FOmniCaptureEditorModule::HandleOpenPanel()
{
    FGlobalTabmanager::Get()->TryInvokeTab(OmniCapturePanelTabName);
}

IMPLEMENT_MODULE(FOmniCaptureEditorModule, OmniCaptureEditor)
