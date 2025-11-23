// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "HAL/Runnable.h"

// Forward declarations for Live555
class RTSPServer;
class UsageEnvironment;
class TaskScheduler;
class ServerMediaSession;

/**
 * FRTSPServerWrapper
 *
 * Wrapper for Live555 RTSP server.
 * Manages single RTSP server instance on specified port.
 *
 * Features:
 * - Runs on background thread (non-blocking)
 * - Supports multiple concurrent streams
 * - Dynamic stream addition/removal
 * - URL format: rtsp://localhost:PORT/vehicle_type_num/camera_id
 *
 * Example URLs:
 * - rtsp://localhost:8554/x500_0/front
 * - rtsp://localhost:8554/x500_0/bottom
 * - rtsp://localhost:8554/iris_1/fpv
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
