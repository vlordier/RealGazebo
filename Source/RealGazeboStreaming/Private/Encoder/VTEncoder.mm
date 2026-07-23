// Objective-C++ bridge for VideoToolbox encoding.
// Uses dlopen/dlsym to avoid header conflicts with UE's FVector type.

#include <dlfcn.h>
#include <mutex>
#include <queue>
#include <vector>

#define CFSTR_LITERAL(s) ((void*)__builtin___CFStringMakeConstantString(s))
typedef int (*CreateSessionFunc)(void*, int, int, void*, void*, void*, void*, void*, void*, void*);
typedef int (*EncodeFrameFunc)(void*, void*, int64_t, int32_t, int64_t, int64_t, int32_t, int64_t, void*, void*, void*);
typedef void (*InvalidateSessionFunc)(void*);
typedef void* (*CreateDictFunc)(void*, int64_t, void*, void*);
typedef void (*DictSetValueFunc)(void*, void*, void*);
typedef void* (*CreateNumFunc)(void*, int, void*);
typedef int (*CreatePixelBufferFunc)(void*, int, int, int, void*, void*);
typedef int (*LockBaseAddrFunc)(void*, int);
typedef int (*UnlockBaseAddrFunc)(void*, int);
typedef void* (*GetBaseAddrFunc)(void*);
typedef int (*GetBytesPerRowFunc)(void*);
typedef void (*ReleaseFunc)(void*);
typedef int (*SessionSetPropFunc)(void*, void*, void*);

static struct FVTFunctions
{
    void* LibHandle = nullptr;
    bool bLoaded = false;

    CreateSessionFunc CreateSession = nullptr;
    EncodeFrameFunc EncodeFrame = nullptr;
    InvalidateSessionFunc InvalidateSession = nullptr;
    CreateDictFunc CreateDict = nullptr;
    DictSetValueFunc DictSetValue = nullptr;
    CreateNumFunc CreateNum = nullptr;
    CreatePixelBufferFunc CreatePixelBuffer = nullptr;
    LockBaseAddrFunc LockBaseAddr = nullptr;
    UnlockBaseAddrFunc UnlockBaseAddr = nullptr;
    GetBaseAddrFunc GetBaseAddr = nullptr;
    GetBytesPerRowFunc GetBytesPerRow = nullptr;
    ReleaseFunc Release = nullptr;
    SessionSetPropFunc SessionSetProp = nullptr;

    bool Load()
    {
        if (bLoaded) return true;
        LibHandle = dlopen("/System/Library/Frameworks/VideoToolbox.framework/VideoToolbox", RTLD_LAZY | RTLD_LOCAL);
        if (!LibHandle) return false;

        CreateSession = (CreateSessionFunc)dlsym(LibHandle, "VTCompressionSessionCreate");
        EncodeFrame = (EncodeFrameFunc)dlsym(LibHandle, "VTCompressionSessionEncodeFrame");
        InvalidateSession = (InvalidateSessionFunc)dlsym(LibHandle, "VTCompressionSessionInvalidate");
        CreateDict = (CreateDictFunc)dlsym(LibHandle, "CFDictionaryCreateMutable");
        DictSetValue = (DictSetValueFunc)dlsym(LibHandle, "CFDictionarySetValue");
        CreateNum = (CreateNumFunc)dlsym(LibHandle, "CFNumberCreate");
        CreatePixelBuffer = (CreatePixelBufferFunc)dlsym(LibHandle, "CVPixelBufferCreate");
        LockBaseAddr = (LockBaseAddrFunc)dlsym(LibHandle, "CVPixelBufferLockBaseAddress");
        UnlockBaseAddr = (UnlockBaseAddrFunc)dlsym(LibHandle, "CVPixelBufferUnlockBaseAddress");
        GetBaseAddr = (GetBaseAddrFunc)dlsym(LibHandle, "CVPixelBufferGetBaseAddress");
        GetBytesPerRow = (GetBytesPerRowFunc)dlsym(LibHandle, "CVPixelBufferGetBytesPerRow");
        Release = (ReleaseFunc)dlsym(LibHandle, "CFRelease");
        SessionSetProp = (SessionSetPropFunc)dlsym(LibHandle, "VTSessionSetProperty");

        bLoaded = (CreateSession != nullptr);
        return bLoaded;
    }
} GVT;

struct FEncodedPacketVT
{
    std::vector<uint8_t> Data;
    bool bIsKeyframe = false;
    int64_t TimestampUs = 0;
};

static std::queue<FEncodedPacketVT> GPacketQueue;
static std::mutex GPacketMutex;

// C callback that VideoToolbox calls from its encoding thread
static void VTEncoderOutputCallback(void* Encoder, void* Params, int Status, void* InfoFlags, void* SampleBuffer)
{
    if (Status != 0 || !SampleBuffer) return;

    // Get data buffer from sample buffer
    // We need CMSampleBufferGetDataBuffer - load it dynamically
    static void* CMHandle = dlopen("/System/Library/Frameworks/CoreMedia.framework/CoreMedia", RTLD_LAZY | RTLD_LOCAL);
    static auto GetDataBuffer = (void* (*)(void*))dlsym(CMHandle, "CMSampleBufferGetDataBuffer");
    static auto GetDataPointer = (int (*)(void*, int, int*, size_t*, char**))dlsym(CMHandle, "CMBlockBufferGetDataPointer");
    static auto GetAttachments = (void* (*)(void*, int))dlsym(CMHandle, "CMSampleBufferGetSampleAttachmentsArray");
    static auto GetValueAtIndex = (void* (*)(void*, int))dlsym(CMHandle, "CFArrayGetValueAtIndex");
    static auto DictContainsKey = (int (*)(void*, void*))dlsym(CMHandle, "CFDictionaryContainsKey");
    static auto GetPTS = (void* (*)(void*))dlsym(CMHandle, "CMSampleBufferGetPresentationTimeStamp");

    if (!GetDataBuffer || !GetDataPointer) return;

    void* BlockBuffer = GetDataBuffer(SampleBuffer);
    if (!BlockBuffer) return;

    size_t TotalLength = 0;
    char* DataPtr = nullptr;
    GetDataPointer(BlockBuffer, 0, nullptr, &TotalLength, &DataPtr);

    if (TotalLength == 0 || !DataPtr) return;

    FEncodedPacketVT Packet;
    Packet.Data.assign(DataPtr, DataPtr + TotalLength);

    // Check if keyframe
    if (GetAttachments && GetValueAtIndex && DictContainsKey)
    {
        void* Attachments = GetAttachments(SampleBuffer, 0);
        if (Attachments)
        {
            void* Attachment = GetValueAtIndex(Attachments, 0);
            // kCMSampleAttachmentKey_NotSync = CFSTR_LITERAL("NotSync")
            Packet.bIsKeyframe = !DictContainsKey(Attachment, CFSTR_LITERAL("NotSync"));
        }
    }

    // Get timestamp
    if (GetPTS)
    {
        void* PTS = GetPTS(SampleBuffer);
        // CMTimeGetSeconds via dlsym
        static auto GetSeconds = (double (*)(void*))dlsym(CMHandle, "CMTimeGetSeconds");
        if (GetSeconds && PTS)
        {
            Packet.TimestampUs = (int64_t)(GetSeconds(PTS) * 1000000.0);
        }
    }

    {
        std::lock_guard<std::mutex> Lock(GPacketMutex);
        GPacketQueue.push(std::move(Packet));
    }
}

extern "C"
{

void* VTEncoder_Create(int32_t Width, int32_t Height, int32_t Bitrate)
{
    if (!GVT.Load()) return nullptr;

    // Create encoder spec dictionary
    void* EncoderSpec = GVT.CreateDict(nullptr, 1, nullptr, nullptr);
    // kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder = CFSTR_LITERAL("EnableHardwareAcceleratedVideoEncoder")
    GVT.DictSetValue(EncoderSpec, CFSTR_LITERAL("EnableHardwareAcceleratedVideoEncoder"), CFSTR_LITERAL("true"));

    // Create source attributes
    void* SourceAttributes = GVT.CreateDict(nullptr, 3, nullptr, nullptr);
    GVT.DictSetValue(SourceAttributes, CFSTR_LITERAL("MetalCompatibility"), CFSTR_LITERAL("true"));

    void* IOSurfaceProps = GVT.CreateDict(nullptr, 0, nullptr, nullptr);
    GVT.DictSetValue(SourceAttributes, CFSTR_LITERAL("IOSurfaceProperties"), IOSurfaceProps);

    int64_t PixelType = 0x42475241; // kCVPixelFormatType_32BGRA
    void* PixelFormatNum = GVT.CreateNum(nullptr, 12, &PixelType);
    GVT.DictSetValue(SourceAttributes, CFSTR_LITERAL("PixelFormatType"), PixelFormatNum);

    void* Session = nullptr;
    int Status = GVT.CreateSession(nullptr, Width, Height, CFSTR_LITERAL("avc1"),
        EncoderSpec, SourceAttributes, nullptr,
        (void*)&VTEncoderOutputCallback, nullptr, &Session);

    GVT.Release(EncoderSpec);
    GVT.Release(SourceAttributes);

    if (Status != 0 || !Session)
    {
        return nullptr;
    }

    // Configure session
    GVT.SessionSetProp(Session, CFSTR_LITERAL("RealTime"), CFSTR_LITERAL("true"));
    GVT.SessionSetProp(Session, CFSTR_LITERAL("ProfileLevel"), CFSTR_LITERAL("H264_High_AutoLevel"));
    GVT.SessionSetProp(Session, CFSTR_LITERAL("AllowTemporalCompression"), CFSTR_LITERAL("true"));
    GVT.SessionSetProp(Session, CFSTR_LITERAL("AllowFrameReordering"), CFSTR_LITERAL("false"));

    int32_t BitrateVal = Bitrate;
    void* BitrateNum = GVT.CreateNum(nullptr, 3, &BitrateVal);
    GVT.SessionSetProp(Session, CFSTR_LITERAL("AverageBitRate"), BitrateNum);

    int32_t KeyIntervalVal = 60;
    void* KeyIntervalNum = GVT.CreateNum(nullptr, 3, &KeyIntervalVal);
    GVT.SessionSetProp(Session, CFSTR_LITERAL("MaxKeyFrameInterval"), KeyIntervalNum);

    return Session;
}

bool VTEncoder_EncodeFrame(void* Encoder, const uint8_t* Data, int32_t RowPitch,
    int32_t Width, int32_t Height, int64_t TimestampUs, bool bForceKeyframe)
{
    if (!Encoder || !Data || !GVT.CreatePixelBuffer) return false;

    void* PixelBuffer = nullptr;
    int CVResult = GVT.CreatePixelBuffer(nullptr, Width, Height,
        0x42475241, nullptr, &PixelBuffer);

    if (CVResult != 0 || !PixelBuffer) return false;

    CVResult = GVT.LockBaseAddr(PixelBuffer, 0);
    if (CVResult == 0)
    {
        void* Dest = GVT.GetBaseAddr(PixelBuffer);
        int32_t DestRowBytes = GVT.GetBytesPerRow(PixelBuffer);
        int32_t CopyRowBytes = (RowPitch < DestRowBytes) ? RowPitch : DestRowBytes;

        for (int32_t Row = 0; Row < Height; ++Row)
        {
            memcpy((uint8_t*)Dest + Row * DestRowBytes, Data + Row * RowPitch, CopyRowBytes);
        }
        GVT.UnlockBaseAddr(PixelBuffer, 0);
    }

    void* FrameProperties = nullptr;
    if (bForceKeyframe)
    {
        FrameProperties = GVT.CreateDict(nullptr, 1, nullptr, nullptr);
        GVT.DictSetValue(FrameProperties, CFSTR_LITERAL("ForceKeyFrame"), CFSTR_LITERAL("true"));
    }

    int Status = GVT.EncodeFrame(Encoder, PixelBuffer,
        TimestampUs, 1000000, 0,  // PTS: value, timescale, epoch
        0, 0, 0,                  // Duration: invalid
        FrameProperties, nullptr, nullptr);

    if (FrameProperties) GVT.Release(FrameProperties);
    GVT.Release(PixelBuffer);

    return Status == 0;
}

bool VTEncoder_GetPacket(void* Encoder, std::vector<uint8_t>& OutData, bool& bOutIsKeyframe, int64_t& OutTimestampUs)
{
    std::lock_guard<std::mutex> Lock(GPacketMutex);
    if (GPacketQueue.empty()) return false;

    FEncodedPacketVT Packet = std::move(GPacketQueue.front());
    GPacketQueue.pop();

    OutData = std::move(Packet.Data);
    bOutIsKeyframe = Packet.bIsKeyframe;
    OutTimestampUs = Packet.TimestampUs;
    return true;
}

void VTEncoder_Destroy(void* Encoder)
{
    if (Encoder && GVT.InvalidateSession)
    {
        GVT.InvalidateSession(Encoder);
        GVT.Release(Encoder);
    }
}

} // extern "C"