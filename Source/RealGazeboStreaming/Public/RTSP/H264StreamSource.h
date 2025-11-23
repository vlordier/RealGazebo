// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "HardwareEncoderWrapper.h"

// Live555 includes
#include "FramedSource.hh"
#include "UsageEnvironment.hh"

/**
 * FH264StreamSource
 *
 * Per-stream H.264 NAL unit source for Live555 RTSP server.
 *
 * CRITICAL FIX FOR STREAM CROSSTALK
 *
 * OLD IMPLEMENTATION (BROKEN):
 * - Used static TMap<FStreamKey, NALQueue> → All streams shared same map
 * - Single global mutex → Blocking/latency
 * - FStreamKey collisions → Stream A's data ends up in Stream B
 *
 * NEW IMPLEMENTATION (FIXED):
 * - Each instance has its OWN private NAL queue (not static!)
 * - Each instance has its OWN private mutex (no global contention!)
 * - Unique FStreamIdentifier prevents collisions
 * - Complete stream isolation guaranteed
 *
 * Features:
 * - Per-instance NAL queue (TQueue<FEncodedNALUnit>)
 * - Per-instance mutex (FCriticalSection)
 * - SPS/PPS storage and injection before keyframes
 * - 64KB NAL size limit (not 512KB)
 * - Mid-stream join support (clients get SPS/PPS on next keyframe)
 *
 * Integration with Live555:
 * - Inherits from Live555's FramedSource
 * - doGetNextFrame() called by Live555 to fetch NAL data
 * - deliverFrame() delivers NAL data to Live555's output buffer
 */
class FH264StreamSource
{
public:
	//----------------------------------------------------------
	// Construction
	//----------------------------------------------------------

	/**
	 * Constructor
	 *
	 * @param InStreamID - Unique stream identifier (VehicleID + CameraID)
	 * @param InEnv - Live555 usage environment
	 * @param InMaxNALQueueSize - Maximum NAL queue size (FPS-aware, maintains 1 second buffer)
	 * @param InMaxNALSizeBytes - Maximum NAL unit size (resolution-aware, prevents I-frame rejection)
	 */
	FH264StreamSource(const FStreamIdentifier& InStreamID, UsageEnvironment& InEnv, int32 InMaxNALQueueSize, int32 InMaxNALSizeBytes);

	/** Destructor */
	~FH264StreamSource();

	//----------------------------------------------------------
	// NAL Unit Input (from EncodingThread)
	//----------------------------------------------------------

	/**
	 * Push encoded NAL units to the queue.
	 * Called by EncodingThread when encoder outputs data.
	 * Thread-safe.
	 *
	 * @param NALUnits - Encoded NAL units to queue
	 */
	void PushNALUnits(const TArray<FEncodedNALUnit>& NALUnits);

	/**
	 * Push a single NAL unit.
	 * Thread-safe.
	 *
	 * @param NALUnit - NAL unit to queue
	 */
	void PushNALUnit(const FEncodedNALUnit& NALUnit);

	//----------------------------------------------------------
	// Live555 Integration
	//----------------------------------------------------------

	/**
	 * Create Live555 FramedSource for this stream.
	 * Live555 will call this source to fetch NAL data.
	 *
	 * @return FramedSource instance (managed by Live555)
	 */
	FramedSource* CreateFramedSource();

	/**
	 * Generate SDP lines for RTSP DESCRIBE response.
	 * Includes base64-encoded SPS/PPS for client decoder initialization.
	 *
	 * @return SDP format string (a=fmtp:96 profile-level-id=...; sprop-parameter-sets=...)
	 */
	FString GenerateSDPLines() const;

	//----------------------------------------------------------
	// SPS/PPS Management
	//----------------------------------------------------------

	/**
	 * Check if SPS/PPS are available.
	 * Required for SDP generation and mid-stream joins.
	 *
	 * @return True if SPS and PPS are stored
	 */
	bool HasSPSPPS() const { return bHasSPSPPS; }

	/**
	 * Get stored SPS data.
	 *
	 * @return SPS byte array
	 */
	const TArray<uint8>& GetSPS() const { return StoredSPS; }

	/**
	 * Get stored PPS data.
	 *
	 * @return PPS byte array
	 */
	const TArray<uint8>& GetPPS() const { return StoredPPS; }

	//----------------------------------------------------------
	// Status
	//----------------------------------------------------------

	/** Get stream identifier */
	const FStreamIdentifier& GetStreamID() const { return StreamID; }

	/** Get number of NAL units currently queued */
	int32 GetQueuedNALCount() const;

	/** Get statistics string */
	FString GetStatsString() const;

	/** Signal shutdown - prevents access after destruction begins */
	void SignalShutdown() { bShuttingDown.store(true); }

	/** Check if shutting down */
	bool IsShuttingDown() const { return bShuttingDown.load(); }

	/**
	 * Fetch next NAL unit from queue.
	 * Called by FLive555H264Source::doGetNextFrame().
	 * Public so Live555 wrapper can access it.
	 *
	 * @param OutNAL - Output NAL unit
	 * @return True if NAL fetched successfully
	 */
	bool FetchNextNAL(FEncodedNALUnit& OutNAL);

private:
	//----------------------------------------------------------
	// Internal NAL Processing
	//----------------------------------------------------------

	/**
	 * Extract and store SPS/PPS from NAL unit.
	 * Called automatically when SPS/PPS NAL units are pushed.
	 *
	 * @param NALUnit - NAL unit to inspect
	 */
	void ExtractSPSPPS(const FEncodedNALUnit& NALUnit);

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Unique stream identifier (VehicleID + CameraID) */
	FStreamIdentifier StreamID;

	/** Live555 usage environment */
	UsageEnvironment& Env;

	//----------------------------------------------------------
	// PER-INSTANCE Data (NOT STATIC - This is the fix!)
	//----------------------------------------------------------

	/**
	 * PER-INSTANCE NAL queue (not static shared!)
	 * Each stream has its OWN queue - complete isolation.
	 */
	TQueue<FEncodedNALUnit> PrivateNALQueue;

	/**
	 * PER-INSTANCE mutex (not global shared!)
	 * Each stream has its OWN mutex - no cross-stream blocking.
	 */
	mutable FCriticalSection PrivateMutex;

	/**
	 * Current queue size (atomic tracking since TQueue has no size() method)
	 */
	std::atomic<int32> NALQueueSize{0};

	//----------------------------------------------------------
	// SPS/PPS Storage
	//----------------------------------------------------------

	/** Stored SPS (Sequence Parameter Set) */
	TArray<uint8> StoredSPS;

	/** Stored PPS (Picture Parameter Set) */
	TArray<uint8> StoredPPS;

	/** Have we received SPS/PPS? */
	bool bHasSPSPPS = false;

	/** Shutdown flag - set when source is being destroyed */
	std::atomic<bool> bShuttingDown{false};

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total NAL units pushed */
	std::atomic<uint64> TotalNALsPushed{0};

	/** Total NAL units fetched */
	std::atomic<uint64> TotalNALsFetched{0};

	/** Total bytes pushed */
	std::atomic<uint64> TotalBytesPushed{0};

	/** NAL queue overflow count (too many queued, oldest dropped) */
	std::atomic<uint64> QueueOverflowCount{0};

	//----------------------------------------------------------
	// Limits (FPS and Resolution Aware)
	//----------------------------------------------------------

	/** Maximum NAL queue size (FPS-aware, maintains ~1 second buffer) */
	int32 MaxNALQueueSize;

	/** Maximum NAL unit size in bytes (resolution-aware, prevents I-frame rejection) */
	int32 MaxNALSizeBytes;
};

/**
 * FLive555H264Source
 *
 * Live555 FramedSource wrapper for FH264StreamSource.
 * This is the actual Live555-compatible class that fetches NAL data.
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
