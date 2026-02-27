// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "RTSP/H264StreamSource.h"
#include "RTSP/Live555Types.h"
#include "Misc/Base64.h"
#include "HAL/PlatformTime.h"

//----------------------------------------------------------
// FLive555H264Source Implementation (Live555 Adapter)
//----------------------------------------------------------

/**
 * FLive555H264Source
 *
 * Live555 FramedSource wrapper for FH264StreamSource.
 * This is the actual Live555-compatible class that fetches NAL data.
 *
 * CRITICAL: This class is ONLY defined in .cpp to prevent Live555 types
 * from leaking into public headers (which would cause BufferedPacket collision).
 */
class FLive555H264Source : public FramedSource
{
public:
	static FLive555H264Source* CreateNew(UsageEnvironment& Env, FH264StreamSource* StreamSource);

protected:
	FLive555H264Source(UsageEnvironment& Env, FH264StreamSource* StreamSource);
	virtual ~FLive555H264Source();

	// Live555 interface
	virtual void doGetNextFrame() override;

private:
	static void DeliverFrameStub(void* ClientData);
	void DeliverFrame();

	FH264StreamSource* H264Source;
	FEncodedNALUnit CurrentNAL;
};

FLive555H264Source* FLive555H264Source::CreateNew(UsageEnvironment& Env, FH264StreamSource* StreamSource)
{
	return new FLive555H264Source(Env, StreamSource);
}

FLive555H264Source::FLive555H264Source(UsageEnvironment& Env, FH264StreamSource* StreamSource)
	: FramedSource(Env)
	, H264Source(StreamSource)
{
}

FLive555H264Source::~FLive555H264Source()
{
}

void FLive555H264Source::doGetNextFrame()
{
	// Schedule frame delivery on Live555's event loop
	// This avoids blocking the RTSP thread
	envir().taskScheduler().scheduleDelayedTask(0, DeliverFrameStub, this);
}

void FLive555H264Source::DeliverFrameStub(void* ClientData)
{
	((FLive555H264Source*)ClientData)->DeliverFrame();
}

void FLive555H264Source::DeliverFrame()
{
	// CRITICAL: Check if source is valid and not shutting down
	if (!H264Source || H264Source->IsShuttingDown())
	{
		// Source is gone or shutting down - do nothing
		// Don't reschedule, just return safely
		return;
	}

	// Fetch next NAL from queue
	if (!H264Source->FetchNextNAL(CurrentNAL))
	{
		// Check again before rescheduling (source may have shutdown during FetchNextNAL)
		if (!H264Source || H264Source->IsShuttingDown())
		{
			return;
		}
		// No NAL available - reschedule
		envir().taskScheduler().scheduleDelayedTask(1000, DeliverFrameStub, this); // 1ms delay
		return;
	}

	// CRITICAL: H264VideoStreamDiscreteFramer expects NAL data WITHOUT start codes!
	// The encoder outputs NALs WITH start codes (0x00000001 or 0x000001).
	// We MUST strip the start code before passing to Live555, otherwise it causes:
	// "H264or5VideoStreamDiscreteFramer error: MPEG 'start code' seen in the input"
	const uint8* NALData = CurrentNAL.Data.GetData();
	int32 NALSize = CurrentNAL.Data.Num();
	int32 StartCodeOffset = 0;

	// Detect and skip start code (4-byte 0x00000001 or 3-byte 0x000001)
	if (NALSize >= 4 && NALData[0] == 0x00 && NALData[1] == 0x00 && NALData[2] == 0x00 && NALData[3] == 0x01)
	{
		StartCodeOffset = 4;
	}
	else if (NALSize >= 3 && NALData[0] == 0x00 && NALData[1] == 0x00 && NALData[2] == 0x01)
	{
		StartCodeOffset = 3;
	}

	// Adjust data pointer and size to skip start code
	NALData += StartCodeOffset;
	NALSize -= StartCodeOffset;

	// Copy NAL data (without start code) to Live555's output buffer
	const int32 CopySize = FMath::Min(NALSize, (int32)fMaxSize);
	FMemory::Memcpy(fTo, NALData, CopySize);

	fFrameSize = CopySize;
	fNumTruncatedBytes = NALSize - CopySize;

	// Set presentation time
	fPresentationTime.tv_sec = CurrentNAL.TimestampMs / 1000;
	fPresentationTime.tv_usec = (CurrentNAL.TimestampMs % 1000) * 1000;

	// Notify Live555 that frame is ready
	FramedSource::afterGetting(this);
}

//----------------------------------------------------------
// FH264StreamSource Implementation
//----------------------------------------------------------

FH264StreamSource::FH264StreamSource(const FStreamIdentifier& InStreamID, UsageEnvironment& InEnv, int32 InMaxNALQueueSize, int32 InMaxNALSizeBytes)
	: StreamID(InStreamID)
	, Env(InEnv)
	, MaxNALQueueSize(InMaxNALQueueSize)
	, MaxNALSizeBytes(InMaxNALSizeBytes)
{
	UE_LOG(LogTemp, Log, TEXT("H264StreamSource: Created for stream %s"), *StreamID.ToString());
	UE_LOG(LogTemp, Log, TEXT("  Per-instance NAL queue (MaxSize=%d, not static!)"), MaxNALQueueSize);
	UE_LOG(LogTemp, Log, TEXT("  Per-instance mutex (no global contention!)"));
	UE_LOG(LogTemp, Log, TEXT("  NAL size limit: %d KB (resolution-aware)"), MaxNALSizeBytes / 1024);
}

FH264StreamSource::~FH264StreamSource()
{
	// Signal shutdown FIRST - prevents Live555 thread from accessing queue
	bShuttingDown.store(true);

	// Give Live555 thread time to see the shutdown flag
	FPlatformProcess::Sleep(0.05f); // 50ms

	UE_LOG(LogTemp, Log, TEXT("H264StreamSource: Destroyed for stream %s (Pushed: %llu, Fetched: %llu, Overflows: %llu)"),
		*StreamID.ToString(),
		TotalNALsPushed.load(),
		TotalNALsFetched.load(),
		QueueOverflowCount.load());
}

//----------------------------------------------------------
// NAL Unit Input
//----------------------------------------------------------

void FH264StreamSource::PushNALUnits(const TArray<FEncodedNALUnit>& NALUnits)
{
	for (const FEncodedNALUnit& NALUnit : NALUnits)
	{
		PushNALUnit(NALUnit);
	}
}

void FH264StreamSource::PushNALUnit(const FEncodedNALUnit& NALUnit)
{
	if (!NALUnit.IsValid())
	{
		return;
	}

	// Validate NAL size (resolution-aware limit)
	if (NALUnit.GetSize() > MaxNALSizeBytes)
	{
		UE_LOG(LogTemp, Warning, TEXT("H264StreamSource: NAL too large (%d > %d) for stream %s - dropping"),
			NALUnit.GetSize(), MaxNALSizeBytes, *StreamID.ToString());
		return;
	}

	// Extract SPS/PPS if this is a parameter set NAL
	if (NALUnit.IsSPS() || NALUnit.IsPPS())
	{
		ExtractSPSPPS(NALUnit);
	}

	// Check queue size to prevent unbounded growth
	if (NALQueueSize >= MaxNALQueueSize)
	{
		// Queue overflow - drop oldest NAL (dequeue and discard)
		FScopeLock Lock(&PrivateMutex);
		FEncodedNALUnit DroppedNAL;
		if (PrivateNALQueue.Dequeue(DroppedNAL))
		{
			NALQueueSize--;
		}
		QueueOverflowCount++;

		UE_LOG(LogTemp, Warning, TEXT("H264StreamSource: NAL queue overflow for stream %s (QueueSize=%d/%d)"),
			*StreamID.ToString(), NALQueueSize.load(), MaxNALQueueSize);
	}

	// Thread-safe queue push
	{
		FScopeLock Lock(&PrivateMutex);
		PrivateNALQueue.Enqueue(NALUnit);
	}
	NALQueueSize++;

	TotalNALsPushed++;
	TotalBytesPushed += NALUnit.GetSize();
}

//----------------------------------------------------------
// Internal NAL Processing
//----------------------------------------------------------

void FH264StreamSource::ExtractSPSPPS(const FEncodedNALUnit& NALUnit)
{
	if (NALUnit.IsSPS())
	{
		// Store SPS (without start code)
		const uint8* Data = NALUnit.Data.GetData();
		int32 Size = NALUnit.Data.Num();

		// Skip start code (0x00 0x00 0x00 0x01 or 0x00 0x00 0x01)
		int32 Offset = 0;
		if (Size >= 4 && Data[0] == 0x00 && Data[1] == 0x00 && Data[2] == 0x00 && Data[3] == 0x01)
		{
			Offset = 4;
		}
		else if (Size >= 3 && Data[0] == 0x00 && Data[1] == 0x00 && Data[2] == 0x01)
		{
			Offset = 3;
		}

		StoredSPS.Empty();
		StoredSPS.Append(Data + Offset, Size - Offset);

		UE_LOG(LogTemp, Log, TEXT("H264StreamSource: Stored SPS (%d bytes) for stream %s"),
			StoredSPS.Num(), *StreamID.ToString());
	}
	else if (NALUnit.IsPPS())
	{
		// Store PPS (without start code)
		const uint8* Data = NALUnit.Data.GetData();
		int32 Size = NALUnit.Data.Num();

		int32 Offset = 0;
		if (Size >= 4 && Data[0] == 0x00 && Data[1] == 0x00 && Data[2] == 0x00 && Data[3] == 0x01)
		{
			Offset = 4;
		}
		else if (Size >= 3 && Data[0] == 0x00 && Data[1] == 0x00 && Data[2] == 0x01)
		{
			Offset = 3;
		}

		StoredPPS.Empty();
		StoredPPS.Append(Data + Offset, Size - Offset);

		UE_LOG(LogTemp, Log, TEXT("H264StreamSource: Stored PPS (%d bytes) for stream %s"),
			StoredPPS.Num(), *StreamID.ToString());
	}

	// Mark as having SPS/PPS if both are available
	if (StoredSPS.Num() > 0 && StoredPPS.Num() > 0)
	{
		if (!bHasSPSPPS)
		{
			bHasSPSPPS = true;
			UE_LOG(LogTemp, Log, TEXT("H264StreamSource: SPS/PPS ready for stream %s"), *StreamID.ToString());
		}
	}
}

bool FH264StreamSource::FetchNextNAL(FEncodedNALUnit& OutNAL)
{
	// CRITICAL: Check shutdown flag BEFORE accessing queue
	// This prevents crash when Live555 thread accesses destroyed queue
	if (bShuttingDown.load())
	{
		return false;
	}

	// Update last fetch time for client activity detection
	// Even if queue is empty, a client is actively requesting frames
	LastFetchTime.store(FPlatformTime::Seconds());

	FScopeLock Lock(&PrivateMutex);

	// Double-check after acquiring lock
	if (bShuttingDown.load())
	{
		return false;
	}

	if (PrivateNALQueue.Dequeue(OutNAL))
	{
		NALQueueSize--;
		TotalNALsFetched++;
		return true;
	}

	return false;
}

bool FH264StreamSource::HasActiveClient(double TimeoutSeconds) const
{
	const double CurrentTime = FPlatformTime::Seconds();
	const double LastFetch = LastFetchTime.load();

	// If never fetched, no active client
	if (LastFetch <= 0.0)
	{
		return false;
	}

	// Client is active if last fetch was within timeout period
	return (CurrentTime - LastFetch) < TimeoutSeconds;
}

//----------------------------------------------------------
// Live555 Integration
//----------------------------------------------------------

void* FH264StreamSource::CreateFramedSource()
{
	return FLive555H264Source::CreateNew(Env, this);
}

FString FH264StreamSource::GenerateSDPLines() const
{
	if (!bHasSPSPPS)
	{
		UE_LOG(LogTemp, Warning, TEXT("H264StreamSource: Cannot generate SDP - SPS/PPS not available for stream %s"),
			*StreamID.ToString());
		return TEXT("");
	}

	// Base64 encode SPS and PPS for SDP
	FString SPSBase64 = FBase64::Encode(StoredSPS);
	FString PPSBase64 = FBase64::Encode(StoredPPS);

	// Extract profile-level-id from SPS (bytes 1-3)
	FString ProfileLevelID = TEXT("42001f"); // Default: Baseline profile, level 3.1
	if (StoredSPS.Num() >= 3)
	{
		ProfileLevelID = FString::Printf(TEXT("%02x%02x%02x"),
			StoredSPS[0], StoredSPS[1], StoredSPS[2]);
	}

	// Generate SDP fmtp line
	// Format: a=fmtp:96 profile-level-id=42001f; sprop-parameter-sets=<SPS>,<PPS>
	FString SDPLine = FString::Printf(
		TEXT("a=fmtp:96 profile-level-id=%s; packetization-mode=1; sprop-parameter-sets=%s,%s\r\n"),
		*ProfileLevelID,
		*SPSBase64,
		*PPSBase64
	);

	return SDPLine;
}

//----------------------------------------------------------
// Status
//----------------------------------------------------------

int32 FH264StreamSource::GetQueuedNALCount() const
{
	return NALQueueSize.load();
}

FString FH264StreamSource::GetStatsString() const
{
	return FString::Printf(
		TEXT("H264StreamSource [%s]: Pushed=%llu, Fetched=%llu, Bytes=%.2fMB, Overflows=%llu, SPS/PPS=%s"),
		*StreamID.ToString(),
		TotalNALsPushed.load(),
		TotalNALsFetched.load(),
		TotalBytesPushed.load() / (1024.0 * 1024.0),
		QueueOverflowCount.load(),
		bHasSPSPPS ? TEXT("Yes") : TEXT("No")
	);
}

