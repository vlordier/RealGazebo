// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Pipeline/RealGazeboFrameData.h"
#include "Core/RealGazeboStreamingTypes.h"
#include <atomic>

// Live555 forward declarations
class TaskScheduler;
class UsageEnvironment;
class RTSPServer;
class ServerMediaSession;
class UserAuthenticationDatabase;

/**
 * RTSP Server Configuration
 */
struct REALGAZEBOSTREAMING_API FRealGazeboRTSPConfig
{
	/** Server port (default: 8554 - standard RTSP port) */
	int32 Port = 8554;

	/** Enable authentication (requires username/password) */
	bool bEnableAuthentication = false;

	/** Username for authentication (if enabled) */
	FString Username = TEXT("admin");

	/** Password for authentication (if enabled) */
	FString Password = TEXT("password");

	/** Allow RTP-over-TCP streaming (if false, only UDP) */
	bool bAllowRTPOverTCP = true;

	/** Session reclamation timeout in seconds (0 = disabled, recommended: 65) */
	int32 ReclamationTimeoutSeconds = 65;
};

/**
 * RealGazebo RTSP Server
 *
 * Multi-stream RTSP server using Live555 for H.264 video streaming.
 * Supports multiple concurrent camera streams with unique stream paths.
 *
 * Features:
 * - Multi-stream support with unique paths per camera
 * - RTP/RTCP streaming with Live555
 * - Optional authentication
 * - RTP-over-TCP and UDP support
 * - Session reclamation
 * - Thread-safe frame delivery
 */
class REALGAZEBOSTREAMING_API FRealGazeboRTSPServer : public FRunnable
{
public:
	FRealGazeboRTSPServer();
	virtual ~FRealGazeboRTSPServer();

	/**
	 * Initialize RTSP server
	 * @param Config Server configuration
	 * @return True if initialized successfully
	 */
	bool Initialize(const FRealGazeboRTSPConfig& Config);

	/**
	 * Shutdown RTSP server
	 */
	void Shutdown();

	/**
	 * Register a new stream
	 * @param StreamKey Unique stream identifier
	 * @param StreamPath URL path (e.g., "iris_0/fpv")
	 * @return True if registered successfully
	 */
	bool RegisterStream(const FStreamKey& StreamKey, const FString& StreamPath);

	/**
	 * Unregister stream
	 * @param StreamKey Stream to unregister
	 */
	void UnregisterStream(const FStreamKey& StreamKey);

	/**
	 * Push encoded frame to stream
	 * @param StreamKey Target stream
	 * @param EncodedFrame H.264 encoded frame
	 * @return True if pushed successfully
	 */
	bool PushFrame(const FStreamKey& StreamKey, TSharedPtr<FEncodedFrameData> EncodedFrame);

	/**
	 * Set SPS/PPS for stream (must be called before clients connect)
	 * @param StreamKey Target stream
	 * @param SPS Sequence Parameter Set
	 * @param PPS Picture Parameter Set
	 */
	void SetSPSPPS(const FStreamKey& StreamKey, const TArray<uint8>& SPS, const TArray<uint8>& PPS);

	/**
	 * Check if server is running
	 */
	bool IsRunning() const { return bIsRunning; }

	//~ Begin FRunnable Interface
	virtual uint32 Run() override;
	virtual void Stop() override;
	//~ End FRunnable Interface

private:
	/** Per-stream data */
	struct FStreamData
	{
		FString StreamPath;
		ServerMediaSession* MediaSession = nullptr;
		TArray<uint8> SPS;
		TArray<uint8> PPS;
		// NOTE: Client connection tracking delegated to FRealGazeboH264Source (no local tracking needed)
	};

	/** Server configuration */
	FRealGazeboRTSPConfig ServerConfig;

	/** Live555 components */
	TaskScheduler* Scheduler = nullptr;
	UsageEnvironment* Environment = nullptr;
	RTSPServer* RtspServer = nullptr;
	UserAuthenticationDatabase* AuthDatabase = nullptr;

	/** Registered streams (active, with SPS/PPS) */
	TMap<FStreamKey, FStreamData> Streams;
	mutable FCriticalSection StreamsMutex;

	/** Pending streams (waiting for SPS/PPS before Live555 registration) */
	TMap<FStreamKey, FStreamData> PendingStreams;
	mutable FCriticalSection PendingStreamsMutex;

	/** Server thread control */
	FRunnableThread* ServerThread = nullptr;
	std::atomic<bool> bIsRunning{false};
	std::atomic<char> StopRequestedFlag{0};  // Live555 EventLoopWatchVariable compatibility

	/** Create media session for stream */
	bool CreateMediaSession(const FStreamKey& StreamKey, FStreamData& StreamData);
};
