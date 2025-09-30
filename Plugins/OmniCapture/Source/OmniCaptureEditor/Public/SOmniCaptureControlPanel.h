#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SListViewBase;
template<typename ItemType> class SListView;
class IDetailsView;
class UOmniCaptureEditorSettings;
class UOmniCaptureSubsystem;

class OMNICAPTUREEDITOR_API SOmniCaptureControlPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SOmniCaptureControlPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply OnStartCapture();
    FReply OnStopCapture();
    FReply OnCaptureStill();
    FReply OnTogglePause();
    FReply OnOpenLastOutput();
    FReply OnBrowseOutputDirectory();
    bool CanStartCapture() const;
    bool CanStopCapture() const;
    bool CanCaptureStill() const;
    bool CanPauseCapture() const;
    bool CanResumeCapture() const;
    bool CanOpenLastOutput() const;
    FText GetPauseButtonText() const;
    bool IsPauseButtonEnabled() const;

    UOmniCaptureSubsystem* GetSubsystem() const;
    EActiveTimerReturnType HandleActiveTimer(double InCurrentTime, float InDeltaTime);
    void RefreshStatus();
    void UpdateOutputDirectoryDisplay();

    void RebuildWarningList(const TArray<FString>& Warnings);
    TSharedRef<ITableRow> GenerateWarningRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);

private:
    TWeakObjectPtr<UOmniCaptureEditorSettings> SettingsObject;
    TSharedPtr<IDetailsView> SettingsView;
    TSharedPtr<STextBlock> StatusTextBlock;
    TSharedPtr<STextBlock> ActiveConfigTextBlock;
    TSharedPtr<STextBlock> RingBufferTextBlock;
    TSharedPtr<STextBlock> AudioTextBlock;
    TSharedPtr<STextBlock> FrameRateTextBlock;
    TSharedPtr<STextBlock> LastStillTextBlock;
    TSharedPtr<STextBlock> OutputDirectoryTextBlock;
    TArray<TSharedPtr<FString>> WarningItems;
    TSharedPtr<SListView<TSharedPtr<FString>>> WarningListView;
    FDelegateHandle ActiveTimerHandle;
};
