// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "HAL/Runnable.h"

// Forward declarations for Live555 types
// Note: These are in global namespace (Live555 library compiled without namespace)
class RTSPServer;
class UsageEnvironment;
class TaskScheduler;
class ServerMediaSession;

/**
 * FRTSPServerWrapper
 *
 * Wrapper for Live555 RTSP server running on a dedicated background thread.
 * Manages a single RTSP server instance on the specified port.
 *
 * SHARED vs ISOLATED:
 * - The RTSP server itself is SHARED across all streams (single instance, single port)
 * - Stream data (NAL queues, encoders) are ISOLATED per-stream (no sharing)
 * - Streams register/unregister dynamically with the shared server
 *
 * Features:
 * - Background thread: Non-blocking event loop using Live555's task scheduler
 * - Multi-stream support: Handles multiple concurrent H.264 video streams
 * - Dynamic management: Add/remove streams at runtime without restart
 * - Standard RTSP: Compatible with VLC, FFmpeg, GStreamer, and all RTSP clients
 *
 * URL Format:
 * rtsp://[host]:[port]/[vehicle_type]_[vehicle_num]/[camera_id]
 *
 * Example URLs:
 * - rtsp://localhost:8554/x500_0/front (X500 vehicle #0, front camera)
 * - rtsp://localhost:8554/x500_0/bottom (X500 vehicle #0, bottom camera)
 * - rtsp://localhost:8554/iris_1/fpv (Iris vehicle #1, FPV camera)
 */
class FRTSPServerWrapper : public FRunnable
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/** Constructor */
	FRTSPServerWrapper();

	/** Destructor */
	virtual ~FRTSPServerWrapper() override;

	/**
	 * Start RTSP server on specified port.
	 *
	 * @param Port - RTSP port (default 8554)
	 * @param OutErrorMessage - Error message if failed (optional)
	 * @return True if started successfully
	 */
	bool Start(int32 Port = 8554, FString* OutErrorMessage = nullptr);

	//----------------------------------------------------------
	// Stream Management
	//----------------------------------------------------------

	/**
	 * Add a new stream to the RTSP server.
	 *
	 * @param StreamID - Unique stream identifier
	 * @param StreamSource - H.264 stream source
	 * @param OutRTSPURL - Generated RTSP URL
	 * @return True if stream added successfully
	 */
	bool AddStream(const FStreamIdentifier& StreamID, class FH264StreamSource* StreamSource, FString& OutRTSPURL);

	/**
	 * Remove a stream from the RTSP server.
	 *
	 * @param StreamID - Stream identifier to remove
	 */
	void RemoveStream(const FStreamIdentifier& StreamID);

	//----------------------------------------------------------
	// FRunnable Interface
	//----------------------------------------------------------

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	//----------------------------------------------------------
	// Status
	//----------------------------------------------------------

	/** Is server running? */
	bool IsRunning() const { return bRunning; }

	/** Get server port */
	int32 GetPort() const { return Port; }

	/** Get number of active streams */
	int32 GetStreamCount() const;

	/** Get statistics string */
	FString GetStatsString() const;

	/** Get Live555 usage environment for creating stream sources */
	UsageEnvironment* GetEnvironment() const { return Env; }

private:
	//----------------------------------------------------------
	// Internal Helpers
	//----------------------------------------------------------

	/** Initialize Live555 environment */
	bool InitializeLive555();

	/** Cleanup Live555 environment */
	void CleanupLive555();

	/** Generate RTSP URL for stream */
	FString GenerateRTSPURL(const FStreamIdentifier& StreamID) const;

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** RTSP server port */
	int32 Port = 8554;

	//----------------------------------------------------------
	// Live555 Components
	//----------------------------------------------------------

	/** Live555 task scheduler */
	TaskScheduler* Scheduler = nullptr;

	/** Live555 usage environment */
	UsageEnvironment* Env = nullptr;

	/** Live555 RTSP server */
	RTSPServer* Server = nullptr;

	//----------------------------------------------------------
	// Threading
	//----------------------------------------------------------

	/** Server thread */
	TUniquePtr<FRunnableThread> Thread;

	/** Is server running? */
	std::atomic<bool> bRunning{false};

	/** Should thread exit? */
	std::atomic<bool> bStopRequested{false};

	//----------------------------------------------------------
	// Stream Tracking
	//----------------------------------------------------------

	/** Active media sessions (keyed by stream ID string) */
	TMap<FString, ServerMediaSession*> MediaSessions;

	/** Mutex for stream management */
	mutable FCriticalSection StreamMutex;

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total streams added */
	std::atomic<uint64> TotalStreamsAdded{0};

	/** Total streams removed */
	std::atomic<uint64> TotalStreamsRemoved{0};
};
