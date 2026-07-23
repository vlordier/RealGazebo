// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#if PLATFORM_MAC

#include "Encoder/HardwareEncoderWrapper.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "RHICommandList.h"
#include "RHIResources.h"

#include <dlfcn.h>

// CFSTR is not available without CoreFoundation headers (which conflict with UE's FVector).
// Use clang's built-in for creating constant CFStringRefs.
#define CFSTR_LITERAL(s) ((void*)__builtin___CFStringMakeConstantString(s))

// We use dlopen/dlsym to access VideoToolbox at runtime, avoiding the FVector
// type conflict that occurs when including CoreVideo headers alongside UE headers.

// Function pointer types for VideoToolbox C API
typedef int (*FVTCompressionSessionCreatePtr)(void*, int, int, unsigned int, void*, void*, void*, void*, void*, void**);
typedef int (*FVTCompressionSessionEncodeFramePtr)(void*, void*, int64_t, int32_t, int64_t, int64_t, int32_t, int64_t, void*, void*, void*);
typedef void (*FVTCompressionSessionInvalidatePtr)(void*);
typedef void* (*FCFDictionaryCreateMutablePtr)(void*, int64_t, void*, void*);
typedef void (*FCFDictionarySetValuePtr)(void*, void*, void*);
typedef void* (*FCFNumberCreatePtr)(void*, int, void*);
typedef int (*FCVPixelBufferCreatePtr)(void*, int, int, int, void*, void*);
typedef int (*FCVPixelBufferLockBaseAddressPtr)(void*, int);
typedef int (*FCVPixelBufferUnlockBaseAddressPtr)(void*, int);
typedef void* (*FCVPixelBufferGetBaseAddressPtr)(void*);
typedef int (*FCVPixelBufferGetBytesPerRowPtr)(void*);
typedef void (*FCFReleasePtr)(void*);
typedef void (*FVTSessionSetPropertyPtr)(void*, void*, void*);

static struct FVideoToolboxLib
{
    void* Handle = nullptr;
    bool bLoaded = false;

    FVTCompressionSessionCreatePtr VTCompressionSessionCreate = nullptr;
    FVTCompressionSessionEncodeFramePtr VTCompressionSessionEncodeFrame = nullptr;
    FVTCompressionSessionInvalidatePtr VTCompressionSessionInvalidate = nullptr;
    FCFDictionaryCreateMutablePtr CFDictionaryCreateMutable = nullptr;
    FCFDictionarySetValuePtr CFDictionarySetValue = nullptr;
    FCFNumberCreatePtr CFNumberCreate = nullptr;
    FCVPixelBufferCreatePtr CVPixelBufferCreate = nullptr;
    FCVPixelBufferLockBaseAddressPtr CVPixelBufferLockBaseAddress = nullptr;
    FCVPixelBufferUnlockBaseAddressPtr CVPixelBufferUnlockBaseAddress = nullptr;
    FCVPixelBufferGetBaseAddressPtr CVPixelBufferGetBaseAddress = nullptr;
    FCVPixelBufferGetBytesPerRowPtr CVPixelBufferGetBytesPerRow = nullptr;
    FCFReleasePtr CFRelease = nullptr;
    FVTSessionSetPropertyPtr VTSessionSetProperty = nullptr;

    bool Load()
    {
        if (bLoaded) return true;
        Handle = dlopen("/System/Library/Frameworks/VideoToolbox.framework/VideoToolbox", RTLD_LAZY | RTLD_LOCAL);
        if (!Handle) return false;

        VTCompressionSessionCreate = (FVTCompressionSessionCreatePtr)dlsym(Handle, "VTCompressionSessionCreate");
        VTCompressionSessionEncodeFrame = (FVTCompressionSessionEncodeFramePtr)dlsym(Handle, "VTCompressionSessionEncodeFrame");
        VTCompressionSessionInvalidate = (FVTCompressionSessionInvalidatePtr)dlsym(Handle, "VTCompressionSessionInvalidate");
        CFDictionaryCreateMutable = (FCFDictionaryCreateMutablePtr)dlsym(Handle, "CFDictionaryCreateMutable");
        CFDictionarySetValue = (FCFDictionarySetValuePtr)dlsym(Handle, "CFDictionarySetValue");
        CFNumberCreate = (FCFNumberCreatePtr)dlsym(Handle, "CFNumberCreate");
        CVPixelBufferCreate = (FCVPixelBufferCreatePtr)dlsym(Handle, "CVPixelBufferCreate");
        CVPixelBufferLockBaseAddress = (FCVPixelBufferLockBaseAddressPtr)dlsym(Handle, "CVPixelBufferLockBaseAddress");
        CVPixelBufferUnlockBaseAddress = (FCVPixelBufferUnlockBaseAddressPtr)dlsym(Handle, "CVPixelBufferUnlockBaseAddress");
        CVPixelBufferGetBaseAddress = (FCVPixelBufferGetBaseAddressPtr)dlsym(Handle, "CVPixelBufferGetBaseAddress");
        CVPixelBufferGetBytesPerRow = (FCVPixelBufferGetBytesPerRowPtr)dlsym(Handle, "CVPixelBufferGetBytesPerRow");
        CFRelease = (FCFReleasePtr)dlsym(Handle, "CFRelease");
        VTSessionSetProperty = (FVTSessionSetPropertyPtr)dlsym(Handle, "VTSessionSetProperty");

        bLoaded = (VTCompressionSessionCreate != nullptr);
        return bLoaded;
    }
} GVideoToolbox;

FHardwareEncoderWrapper::FHardwareEncoderWrapper()
{
    UE_LOG(LogTemp, Log, TEXT("HardwareEncoderWrapper: Created (macOS VideoToolbox via dlopen)"));
}

FHardwareEncoderWrapper::~FHardwareEncoderWrapper()
{
    Shutdown();
}

bool FHardwareEncoderWrapper::Initialize(const FEncoderConfig& InConfig, FString& OutErrorMessage)
{
    if (bInitialized) return true;
    if (!InConfig.Validate(OutErrorMessage)) return false;
    Config = InConfig;

    if (!GVideoToolbox.Load())
    {
        OutErrorMessage = TEXT("Failed to load VideoToolbox framework via dlopen");
        return false;
    }

    // Create compression session
    void* EncoderSpec = GVideoToolbox.CFDictionaryCreateMutable(nullptr, 1, nullptr, nullptr);
    GVideoToolbox.CFDictionarySetValue(EncoderSpec, CFSTR_LITERAL("EnableHardwareAcceleratedVideoEncoder"), CFSTR_LITERAL("true"));

    void* SourceAttributes = GVideoToolbox.CFDictionaryCreateMutable(nullptr, 3, nullptr, nullptr);
    GVideoToolbox.CFDictionarySetValue(SourceAttributes, CFSTR_LITERAL("MetalCompatibility"), CFSTR_LITERAL("true"));
    void* IOSurfaceProps = GVideoToolbox.CFDictionaryCreateMutable(nullptr, 0, nullptr, nullptr);
    GVideoToolbox.CFDictionarySetValue(SourceAttributes, CFSTR_LITERAL("IOSurfaceProperties"), IOSurfaceProps);

    int64_t PixelType = 0x42475241; // kCVPixelFormatType_32BGRA
    void* PixelFormatNum = GVideoToolbox.CFNumberCreate(nullptr, 12, &PixelType);
    GVideoToolbox.CFDictionarySetValue(SourceAttributes, CFSTR_LITERAL("PixelFormatType"), PixelFormatNum);

    void* Session = nullptr;
    int32_t CodecType = 0x61766331; // 'avc1' = kCMVideoCodecType_H264
    int Status = GVideoToolbox.VTCompressionSessionCreate(
        nullptr, Config.Width, Config.Height, CodecType,
        EncoderSpec, SourceAttributes, nullptr,
        nullptr, nullptr, &Session);

    GVideoToolbox.CFRelease(EncoderSpec);
    GVideoToolbox.CFRelease(SourceAttributes);

    if (Status != 0 || !Session)
    {
        OutErrorMessage = FString::Printf(TEXT("VTCompressionSessionCreate failed: %d"), Status);
        return false;
    }

    EncoderHandle = Session;

    // Configure session
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("RealTime"), CFSTR_LITERAL("true"));
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("ProfileLevel"), CFSTR_LITERAL("H264_High_AutoLevel"));
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("AllowTemporalCompression"), CFSTR_LITERAL("true"));
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("AllowFrameReordering"), CFSTR_LITERAL("false"));

    int32_t BitrateVal = static_cast<int32_t>(Config.Bitrate);
    void* BitrateNum = GVideoToolbox.CFNumberCreate(nullptr, 3, &BitrateVal);
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("AverageBitRate"), BitrateNum);

    int32_t KeyIntervalVal = 60;
    void* KeyIntervalNum = GVideoToolbox.CFNumberCreate(nullptr, 3, &KeyIntervalVal);
    GVideoToolbox.VTSessionSetProperty(EncoderHandle, CFSTR_LITERAL("MaxKeyFrameInterval"), KeyIntervalNum);

    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("HardwareEncoderWrapper: Initialized %dx%d H.264 VideoToolbox"), Config.Width, Config.Height);
    return true;
}

void FHardwareEncoderWrapper::Shutdown()
{
    if (!bInitialized) return;
    bShuttingDown = true;

    if (EncoderHandle)
    {
        GVideoToolbox.VTCompressionSessionInvalidate(EncoderHandle);
        GVideoToolbox.CFRelease(EncoderHandle);
        EncoderHandle = nullptr;
    }

    bInitialized = false;
    bShuttingDown = false;
}

bool FHardwareEncoderWrapper::EncodeFrame(FRHITexture* Texture, uint64 FrameNumber, bool bForceKeyframe)
{
    if (!bInitialized || bShuttingDown || !EncoderHandle || !Texture || !Texture->IsValid())
    {
        EncodingFailureCount++;
        return false;
    }

    // Use FRHIGPUTextureReadback to copy texture to CPU
    FRHIGPUTextureReadback Readback(TEXT("HardwareEncoderReadback"));
    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

    Readback.EnqueueCopy(RHICmdList, Texture, FIntVector::ZeroValue, 0,
        FIntVector(Config.Width, Config.Height, 0));

    RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

    if (!Readback.IsReady())
    {
        FPlatformProcess::Sleep(0.001f);
        if (!Readback.IsReady())
        {
            FPlatformProcess::Sleep(0.005f);
            if (!Readback.IsReady())
            {
                EncodingFailureCount++;
                return false;
            }
        }
    }

    int32 RowPitch = 0;
    void* Data = Readback.Lock(RowPitch);
    if (!Data)
    {
        EncodingFailureCount++;
        return false;
    }

    // Create CVPixelBuffer from readback data
    void* PixelBuffer = nullptr;
    int CVResult = GVideoToolbox.CVPixelBufferCreate(
        nullptr, Config.Width, Config.Height,
        0x42475241, nullptr, &PixelBuffer);

    if (CVResult != 0 || !PixelBuffer)
    {
        Readback.Unlock();
        EncodingFailureCount++;
        return false;
    }

    CVResult = GVideoToolbox.CVPixelBufferLockBaseAddress(PixelBuffer, 0);
    if (CVResult == 0)
    {
        void* Dest = GVideoToolbox.CVPixelBufferGetBaseAddress(PixelBuffer);
        const uint8* Src = static_cast<const uint8*>(Data);
        int32 DestRowBytes = GVideoToolbox.CVPixelBufferGetBytesPerRow(PixelBuffer);
        int32 CopyRowBytes = FMath::Min(RowPitch, DestRowBytes);

        for (int32 Row = 0; Row < Config.Height; ++Row)
        {
            FMemory::Memcpy(static_cast<uint8*>(Dest) + Row * DestRowBytes, Src + Row * RowPitch, CopyRowBytes);
        }
        GVideoToolbox.CVPixelBufferUnlockBaseAddress(PixelBuffer, 0);
    }
    Readback.Unlock();

    const bool bForce = bForceKeyframe || bForceNextKeyframe.exchange(false);
    const int64 TimestampUs = static_cast<int64>(FPlatformTime::Seconds() * 1000000.0);

    void* FrameProperties = nullptr;
    if (bForce)
    {
        FrameProperties = GVideoToolbox.CFDictionaryCreateMutable(nullptr, 1, nullptr, nullptr);
        GVideoToolbox.CFDictionarySetValue(FrameProperties, CFSTR_LITERAL("ForceKeyFrame"), CFSTR_LITERAL("true"));
    }

    int Status = GVideoToolbox.VTCompressionSessionEncodeFrame(
        EncoderHandle, PixelBuffer,
        TimestampUs, 1000000, 0,
        0, 0, 0,
        FrameProperties, nullptr, nullptr);

    if (FrameProperties) GVideoToolbox.CFRelease(FrameProperties);
    GVideoToolbox.CFRelease(PixelBuffer);

    if (Status != 0)
    {
        EncodingFailureCount++;
        return false;
    }

    CurrentFrameNumber = FrameNumber;
    LastEncodeTime = FPlatformTime::Seconds();
    TotalFramesEncoded++;
    return true;
}

bool FHardwareEncoderWrapper::GetEncodedData(TArray<FEncodedNALUnit>& OutNALUnits)
{
    FScopeLock Lock(&NALQueueMutex);
    while (!NALOutputQueue.IsEmpty())
    {
        FEncodedNALUnit NAL;
        if (NALOutputQueue.Dequeue(NAL))
        {
            OutNALUnits.Add(MoveTemp(NAL));
        }
    }
    return OutNALUnits.Num() > 0;
}

void FHardwareEncoderWrapper::ForceKeyframe()
{
    bForceNextKeyframe.store(true);
}

FString FHardwareEncoderWrapper::GetStats() const
{
    return FString::Printf(TEXT("MacVideoToolbox: %llu frames, %llu keyframes, %llu bytes, %llu failures"),
        TotalFramesEncoded.load(), TotalKeyframesEncoded.load(),
        TotalBytesOutput.load(), EncodingFailureCount.load());
}

#endif // PLATFORM_MAC