#pragma once

#include "OmniCaptureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OmniCaptureSubsystem.generated.h"

class AOmniCaptureRigActor;
class AOmniCaptureDirectorActor;
class FOmniCaptureRingBuffer;
class FOmniCapturePNGWriter;
class FOmniCaptureAudioRecorder;
class FOmniCaptureNVENCEncoder;
class FOmniCaptureMuxer;
class AOmniCapturePreviewActor;

struct FOmniCaptureSegmentRecord
{
    int32 SegmentIndex = 0;
    FString Directory;
    FString BaseFileName;
    FString AudioPath;
    FString VideoPath;
    TArray<FOmniCaptureFrameMetadata> Frames;
};

UCLASS()
class OMNICAPTURE_API UOmniCaptureSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void BeginCapture(const FOmniCaptureSettings& InSettings);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void EndCapture(bool bFinalize = true);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool IsCapturing() const { return bIsCapturing; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void PauseCapture();

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void ResumeCapture();

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool IsPaused() const { return bIsPaused; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CapturePanoramaStill(const FOmniCaptureSettings& InSettings, FString& OutFilePath);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CanPause() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CanResume() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetStatusString() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    const TArray<FString>& GetActiveWarnings() const { return ActiveWarnings; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FOmniCaptureRingBufferStats GetRingBufferStats() const { return LatestRingBufferStats; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FOmniAudioSyncStats GetAudioSyncStats() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    const FOmniCaptureSettings& GetActiveSettings() const { return ActiveSettings; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    double GetCurrentFrameRate() const { return CurrentCaptureFPS; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool HasFinalizedOutput() const { return !LastFinalizedOutput.IsEmpty(); }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetLastFinalizedOutputPath() const { return LastFinalizedOutput; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetLastStillImagePath() const { return LastStillImagePath; }

private:
    void CreateRig();
    void DestroyRig();

    void CreateTickActor();
    void DestroyTickActor();

    void SpawnPreviewActor();
    void DestroyPreviewActor();
    void InitializeOutputWriters();
    void ShutdownOutputWriters(bool bFinalizeOutputs);
    void FinalizeOutputs(bool bFinalizeOutputs);

    bool ValidateEnvironment();
    bool ApplyFallbacks();

    void InitializeAudioRecording();
    void ShutdownAudioRecording();

    void TickCapture(float DeltaTime);
    void CaptureFrame();
    void FlushRingBuffer();

    void HandleDroppedFrame();

    void ConfigureActiveSegment();
    void RotateSegmentIfNeeded();
    void CompleteActiveSegment(bool bStoreResults);
    int64 CalculateActiveSegmentSizeBytes() const;
    void UpdateRuntimeWarnings();
    void AddWarningUnique(const FString& Warning);
    void RemoveWarning(const FString& Warning);
    void ResetDynamicWarnings();

    FString BuildOutputDirectory() const;
    FString BuildFrameFileName(int32 FrameIndex, const FString& Extension) const;

private:
    friend class AOmniCaptureDirectorActor;

    FOmniCaptureSettings ActiveSettings;
    FOmniCaptureSettings OriginalSettings;

    bool bIsCapturing = false;
    bool bIsPaused = false;
    bool bDroppedFrames = false;

    int32 DroppedFrameCount = 0;

    int32 FrameCounter = 0;
    double CaptureStartTime = 0.0;
    double LastPreviewUpdateTime = 0.0;
    double PreviewFrameInterval = 0.0;
    double CurrentCaptureFPS = 0.0;
    double LastFpsSampleTime = 0.0;
    int32 FramesSinceLastFpsSample = 0;
    double LastRuntimeWarningCheckTime = 0.0;
    double LastSegmentSizeCheckTime = 0.0;
    double CurrentSegmentStartTime = 0.0;
    int32 CurrentSegmentIndex = 0;

    TWeakObjectPtr<AOmniCaptureRigActor> RigActor;
    TWeakObjectPtr<AOmniCaptureDirectorActor> TickActor;
    TWeakObjectPtr<AOmniCapturePreviewActor> PreviewActor;

    TUniquePtr<FOmniCaptureRingBuffer> RingBuffer;
    TUniquePtr<FOmniCapturePNGWriter> PNGWriter;
    TUniquePtr<FOmniCaptureAudioRecorder> AudioRecorder;
    TUniquePtr<FOmniCaptureNVENCEncoder> NVENCEncoder;
    TUniquePtr<FOmniCaptureMuxer> OutputMuxer;

    TArray<FOmniCaptureFrameMetadata> CapturedFrameMetadata;
    TArray<FOmniCaptureSegmentRecord> CompletedSegments;
    FString RecordedAudioPath;
    FString RecordedVideoPath;
    FString LastFinalizedOutput;
    FString LastStillImagePath;
    FString BaseOutputDirectory;
    FString BaseOutputFileName;

    TArray<FString> ActiveWarnings;
    FOmniCaptureRingBufferStats LatestRingBufferStats;
    FOmniAudioSyncStats AudioStats;

    EOmniCaptureState State = EOmniCaptureState::Idle;
};

