#include "SOmniCaptureControlPanel.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureEditorSettings.h"
#include "OmniCaptureSubsystem.h"
#include "PropertyEditorModule.h"
#include "Styling/CoreStyle.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OmniCaptureControlPanel"

namespace
{
    FText CodecToText(EOmniCaptureCodec Codec)
    {
        switch (Codec)
        {
        case EOmniCaptureCodec::HEVC: return LOCTEXT("CodecHEVC", "HEVC");
        case EOmniCaptureCodec::H264:
        default: return LOCTEXT("CodecH264", "H.264");
        }
    }

    FText FormatToText(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::NV12: return LOCTEXT("FormatNV12", "NV12");
        case EOmniCaptureColorFormat::P010: return LOCTEXT("FormatP010", "P010");
        case EOmniCaptureColorFormat::BGRA:
        default: return LOCTEXT("FormatBGRA", "BGRA");
        }
    }
}

void SOmniCaptureControlPanel::Construct(const FArguments& InArgs)
{
    UOmniCaptureEditorSettings* Settings = GetMutableDefault<UOmniCaptureEditorSettings>();
    SettingsObject = Settings;

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = true;
    DetailsArgs.bHideSelectionTip = true;
    DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    SettingsView = PropertyEditor.CreateDetailView(DetailsArgs);
    SettingsView->SetObject(Settings);

    WarningListView = SNew(SListView<TSharedPtr<FString>>)
        .ListItemsSource(&WarningItems)
        .OnGenerateRow(this, &SOmniCaptureControlPanel::GenerateWarningRow)
        .SelectionMode(ESelectionMode::None);

    ChildSlot
    [
        SNew(SBorder)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 0.f, 0.f, 8.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("StartCapture", "Start Capture"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnStartCapture)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanStartCapture)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("CaptureStill", "Capture Still"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnCaptureStill)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanCaptureStill)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 0.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(this, &SOmniCaptureControlPanel::GetPauseButtonText)
                    .OnClicked(this, &SOmniCaptureControlPanel::OnTogglePause)
                    .IsEnabled(this, &SOmniCaptureControlPanel::IsPauseButtonEnabled)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("StopCapture", "Stop"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnStopCapture)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanStopCapture)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8.f, 0.f, 0.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("OpenLastOutput", "Open Output"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnOpenLastOutput)
                    .IsEnabled(this, &SOmniCaptureControlPanel::CanOpenLastOutput)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(StatusTextBlock, STextBlock)
                .Text(LOCTEXT("StatusIdle", "Status: Idle"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(ActiveConfigTextBlock, STextBlock)
                .Text(LOCTEXT("ConfigInactive", "Codec: - | Format: - | Zero Copy: -"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(LastStillTextBlock, STextBlock)
                .Text(LOCTEXT("LastStillInactive", "Last Still: -"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.f, 4.f, 8.f, 0.f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("BrowseOutputDirectory", "Set Output Folder"))
                    .OnClicked(this, &SOmniCaptureControlPanel::OnBrowseOutputDirectory)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.f)
                .VAlign(VAlign_Center)
                [
                    SAssignNew(OutputDirectoryTextBlock, STextBlock)
                    .Text(LOCTEXT("OutputDirectoryInactive", "Output Folder: -"))
                    .AutoWrapText(true)
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(FrameRateTextBlock, STextBlock)
                .Text(LOCTEXT("FrameRateInactive", "Frame Rate: 0.00 FPS"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(RingBufferTextBlock, STextBlock)
                .Text(LOCTEXT("RingBufferStats", "Ring Buffer: Pending 0 | Dropped 0 | Blocked 0"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(AudioTextBlock, STextBlock)
                .Text(LOCTEXT("AudioStats", "Audio Drift: 0 ms"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 8.f)
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("WarningsHeader", "Environment & Warnings"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 4.f)
            [
                SNew(SBox)
                .HeightOverride(96.f)
                [
                    WarningListView.ToSharedRef()
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.f, 8.f)
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SettingsView.ToSharedRef()
            ]
        ]
    ];

    RefreshStatus();
    UpdateOutputDirectoryDisplay();
    ActiveTimerHandle = RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SOmniCaptureControlPanel::HandleActiveTimer));
}

FReply SOmniCaptureControlPanel::OnStartCapture()
{
    if (!SettingsObject.IsValid())
    {
        return FReply::Handled();
    }

    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->BeginCapture(SettingsObject->CaptureSettings);
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnCaptureStill()
{
    if (!SettingsObject.IsValid())
    {
        return FReply::Handled();
    }

    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        FString OutputPath;
        Subsystem->CapturePanoramaStill(SettingsObject->CaptureSettings, OutputPath);
    }

    RefreshStatus();
    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnStopCapture()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->EndCapture(true);
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnTogglePause()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        if (Subsystem->IsPaused())
        {
            if (Subsystem->CanResume())
            {
                Subsystem->ResumeCapture();
            }
        }
        else if (Subsystem->CanPause())
        {
            Subsystem->PauseCapture();
        }
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnOpenLastOutput()
{
    if (UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        const FString OutputPath = Subsystem->GetLastFinalizedOutputPath();
        if (!OutputPath.IsEmpty() && FPaths::FileExists(OutputPath))
        {
            FPlatformProcess::LaunchFileInDefaultExternalApplication(*OutputPath);
        }
    }

    return FReply::Handled();
}

FReply SOmniCaptureControlPanel::OnBrowseOutputDirectory()
{
    if (!SettingsObject.IsValid())
    {
        return FReply::Handled();
    }

    UOmniCaptureEditorSettings* Settings = SettingsObject.Get();
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        return FReply::Handled();
    }

    FString DefaultPath = Settings->CaptureSettings.OutputDirectory;
    if (DefaultPath.IsEmpty())
    {
        DefaultPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OmniCaptures"));
    }

    void* ParentWindowHandle = nullptr;
    if (FSlateApplication::IsInitialized())
    {
        ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    }

    FString ChosenDirectory;
    const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
        ParentWindowHandle,
        TEXT("Choose Capture Output Folder"),
        DefaultPath,
        ChosenDirectory
    );

    if (bOpened)
    {
        const FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(ChosenDirectory);
        Settings->Modify();
        Settings->CaptureSettings.OutputDirectory = AbsoluteDirectory;
        Settings->SaveConfig();

        if (SettingsView.IsValid())
        {
            SettingsView->ForceRefresh();
        }

        UpdateOutputDirectoryDisplay();
    }

    return FReply::Handled();
}

bool SOmniCaptureControlPanel::CanStartCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return !Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanStopCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanCaptureStill() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return !Subsystem->IsCapturing();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanPauseCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->CanPause();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanResumeCapture() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->CanResume();
    }
    return false;
}

bool SOmniCaptureControlPanel::CanOpenLastOutput() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        const FString OutputPath = Subsystem->GetLastFinalizedOutputPath();
        return Subsystem->HasFinalizedOutput() && !OutputPath.IsEmpty() && FPaths::FileExists(OutputPath);
    }
    return false;
}

FText SOmniCaptureControlPanel::GetPauseButtonText() const
{
    if (const UOmniCaptureSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->IsPaused() ? LOCTEXT("ResumeCapture", "Resume") : LOCTEXT("PauseCapture", "Pause");
    }
    return LOCTEXT("PauseCapture", "Pause");
}

bool SOmniCaptureControlPanel::IsPauseButtonEnabled() const
{
    return CanPauseCapture() || CanResumeCapture();
}

UOmniCaptureSubsystem* SOmniCaptureControlPanel::GetSubsystem() const
{
    if (!GEditor)
    {
        return nullptr;
    }

    const FWorldContext& WorldContext = GEditor->GetEditorWorldContext();
    UWorld* World = WorldContext.World();
    return World ? World->GetSubsystem<UOmniCaptureSubsystem>() : nullptr;
}

EActiveTimerReturnType SOmniCaptureControlPanel::HandleActiveTimer(double InCurrentTime, float InDeltaTime)
{
    RefreshStatus();
    return EActiveTimerReturnType::Continue;
}

void SOmniCaptureControlPanel::RefreshStatus()
{
    if (!StatusTextBlock.IsValid())
    {
        return;
    }

    UOmniCaptureSubsystem* Subsystem = GetSubsystem();
    if (!Subsystem)
    {
        StatusTextBlock->SetText(LOCTEXT("StatusNoWorld", "Status: No active editor world"));
        ActiveConfigTextBlock->SetText(LOCTEXT("ConfigUnavailable", "Codec: - | Format: - | Zero Copy: -"));
        if (LastStillTextBlock.IsValid())
        {
            LastStillTextBlock->SetText(LOCTEXT("LastStillInactive", "Last Still: -"));
        }
        if (FrameRateTextBlock.IsValid())
        {
            FrameRateTextBlock->SetText(LOCTEXT("FrameRateInactive", "Frame Rate: 0.00 FPS"));
        }
        RingBufferTextBlock->SetText(FText::GetEmpty());
        AudioTextBlock->SetText(FText::GetEmpty());
        UpdateOutputDirectoryDisplay();
        RebuildWarningList(TArray<FString>());
        return;
    }

    StatusTextBlock->SetText(FText::FromString(Subsystem->GetStatusString()));

    const bool bCapturing = Subsystem->IsCapturing();
    const FOmniCaptureSettings& Settings = bCapturing ? Subsystem->GetActiveSettings() : (SettingsObject.IsValid() ? SettingsObject->CaptureSettings : FOmniCaptureSettings());

    const FText ConfigText = FText::Format(LOCTEXT("ConfigFormat", "Codec: {0} | Format: {1} | Zero Copy: {2}"),
        CodecToText(Settings.Codec),
        FormatToText(Settings.NVENCColorFormat),
        Settings.bZeroCopy ? LOCTEXT("ZeroCopyYes", "Yes") : LOCTEXT("ZeroCopyNo", "No"));
    ActiveConfigTextBlock->SetText(ConfigText);

    if (LastStillTextBlock.IsValid())
    {
        const FString LastStillPath = Subsystem->GetLastStillImagePath();
        LastStillTextBlock->SetText(LastStillPath.IsEmpty()
            ? LOCTEXT("LastStillInactive", "Last Still: -")
            : FText::Format(LOCTEXT("LastStillFormat", "Last Still: {0}"), FText::FromString(LastStillPath)));
    }

    if (FrameRateTextBlock.IsValid())
    {
        const double CurrentFps = Subsystem->GetCurrentFrameRate();
        FNumberFormattingOptions FpsFormat;
        FpsFormat.SetMinimumFractionalDigits(2);
        FpsFormat.SetMaximumFractionalDigits(2);
        const FText FrameText = FText::Format(LOCTEXT("FrameRateFormat", "Frame Rate: {0} FPS"), FText::AsNumber(CurrentFps, &FpsFormat));
        FrameRateTextBlock->SetText(FrameText);
        FrameRateTextBlock->SetColorAndOpacity(Subsystem->IsPaused() ? FSlateColor(FLinearColor::Gray) : FSlateColor::UseForeground());
    }

    const FOmniCaptureRingBufferStats RingStats = Subsystem->GetRingBufferStats();
    const FText RingText = FText::Format(LOCTEXT("RingStatsFormat", "Ring Buffer: Pending {0} | Dropped {1} | Blocked {2}"),
        FText::AsNumber(RingStats.PendingFrames),
        FText::AsNumber(RingStats.DroppedFrames),
        FText::AsNumber(RingStats.BlockedPushes));
    RingBufferTextBlock->SetText(RingText);

    const FOmniAudioSyncStats AudioStats = Subsystem->GetAudioSyncStats();
    const FString DriftString = FString::Printf(TEXT("%.2f"), AudioStats.DriftMilliseconds);
    const FString MaxString = FString::Printf(TEXT("%.2f"), AudioStats.MaxObservedDriftMilliseconds);
    const FText AudioText = FText::Format(LOCTEXT("AudioStatsFormat", "Audio Drift: {0} ms (Max {1} ms) Pending {2}"),
        FText::FromString(DriftString),
        FText::FromString(MaxString),
        FText::AsNumber(AudioStats.PendingPackets));
    AudioTextBlock->SetText(AudioText);
    AudioTextBlock->SetColorAndOpacity(AudioStats.bInError ? FSlateColor(FLinearColor::Red) : FSlateColor::UseForeground());

    UpdateOutputDirectoryDisplay();
    RebuildWarningList(Subsystem->GetActiveWarnings());
}

void SOmniCaptureControlPanel::RebuildWarningList(const TArray<FString>& Warnings)
{
    WarningItems.Reset();
    for (const FString& Warning : Warnings)
    {
        WarningItems.Add(MakeShared<FString>(Warning));
    }

    if (WarningItems.Num() == 0)
    {
        WarningItems.Add(MakeShared<FString>(LOCTEXT("NoWarnings", "No warnings detected").ToString()));
    }

    if (WarningListView.IsValid())
    {
        WarningListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SOmniCaptureControlPanel::GenerateWarningRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
        [
            SNew(STextBlock)
            .Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty())
        ];
}

void SOmniCaptureControlPanel::UpdateOutputDirectoryDisplay()
{
    if (!OutputDirectoryTextBlock.IsValid())
    {
        return;
    }

    FString DisplayPath = TEXT("-");
    if (SettingsObject.IsValid())
    {
        const FString& ConfiguredPath = SettingsObject->CaptureSettings.OutputDirectory;
        if (ConfiguredPath.IsEmpty())
        {
            DisplayPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OmniCaptures"));
        }
        else
        {
            DisplayPath = FPaths::ConvertRelativePathToFull(ConfiguredPath);
        }
    }

    OutputDirectoryTextBlock->SetText(FText::Format(LOCTEXT("OutputDirectoryFormat", "Output Folder: {0}"), FText::FromString(DisplayPath)));
}

#undef LOCTEXT_NAMESPACE
