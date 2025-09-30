#include "OmniCapturePNGWriter.h"

#include "ImageWriteQueue/Public/ImageWriteQueue.h"
#include "ImageWriteQueue/Public/ImageWriteTask.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

FOmniCapturePNGWriter::FOmniCapturePNGWriter()
{
}

FOmniCapturePNGWriter::~FOmniCapturePNGWriter()
{
    Flush();
}

void FOmniCapturePNGWriter::Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory)
{
    OutputDirectory = InOutputDirectory;
    SequenceBaseName = Settings.OutputFileName;

    if (OutputDirectory.IsEmpty())
    {
        OutputDirectory = FPaths::ProjectSavedDir() / TEXT("OmniCaptures");
    }

    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);

    IFileManager::Get().MakeDirectory(*OutputDirectory, true);

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("ImageWriteQueue")))
    {
        FModuleManager::Get().LoadModule(TEXT("ImageWriteQueue"));
    }

    ImageWriteQueue = &FModuleManager::GetModuleChecked<FImageWriteQueueModule>(TEXT("ImageWriteQueue")).GetImageWriteQueue();
}

void FOmniCapturePNGWriter::EnqueueFrame(TUniquePtr<FOmniCaptureFrame>&& Frame, const FString& FrameFileName)
{
    if (!ImageWriteQueue || !Frame.IsValid())
    {
        return;
    }

    TUniquePtr<FImageWriteTask> Task = MakeUnique<FImageWriteTask>();
    Task->Format = EImageFormat::PNG;
    Task->Filename = OutputDirectory / FrameFileName;
    Task->CompressionQuality = static_cast<int32>(EImageCompressionQuality::Uncompressed);
    Task->bOverwriteFile = true;
    Task->PixelData = MoveTemp(Frame->PixelData);
    Task->bSupports16Bit = Frame->bLinearColor;

    ImageWriteQueue->Enqueue(MoveTemp(Task));

    FScopeLock Lock(&MetadataCS);
    CapturedMetadata.Add(Frame->Metadata);
}

void FOmniCapturePNGWriter::Flush()
{
    if (ImageWriteQueue)
    {
        ImageWriteQueue->Flush();
        ImageWriteQueue = nullptr;
    }
}

TArray<FOmniCaptureFrameMetadata> FOmniCapturePNGWriter::ConsumeCapturedFrames()
{
    FScopeLock Lock(&MetadataCS);
    TArray<FOmniCaptureFrameMetadata> Result = MoveTemp(CapturedMetadata);
    CapturedMetadata.Reset();
    return Result;
}

