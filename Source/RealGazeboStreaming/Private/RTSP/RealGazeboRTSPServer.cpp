// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RealGazeboRTSPServer.h"
#include "RTSP/RealGazeboH264Source.h"
#include "RTSP/RealGazeboMediaSubsession.h"
#include "Core/RealGazeboStreamingTypes.h"

// Live555 includes
#include "BasicUsageEnvironment.hh"
#include "RTSPServer.hh"
#include "ServerMediaSession.hh"

FRealGazeboRTSPServer::FRealGazeboRTSPServer()
{
}

FRealGazeboRTSPServer::~FRealGazeboRTSPServer()
{
	Shutdown();
}

bool FRealGazeboRTSPServer::Initialize(const FRealGazeboRTSPConfig& Config)
{
	ServerConfig = Config;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Initializing on port %d"), Config.Port);

	// Create Live555 scheduler and environment
	Scheduler = BasicTaskScheduler::createNew();
	if (!Scheduler)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create task scheduler"));
		return false;
	}

	Environment = BasicUsageEnvironment::createNew(*Scheduler);
	if (!Environment)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create usage environment"));
		delete Scheduler;
		Scheduler = nullptr;
		return false;
	}

	// CRITICAL FIX (2025-11-12): Increase RTP buffer to 6MB for complex scenes (moving sky, etc.)
	// Previous 1.5MB buffer was insufficient for large keyframes from NVENC with high-motion content
	// 6MB handles worst-case keyframes: 720p @ 4.5 Mbps (ultra-low latency) = ~100KB max per keyframe with moving sky
	// H264VideoStreamFramer automatically fragments large NAL units for RTP transmission (MTU ~1400 bytes)
	OutPacketBuffer::maxSize = 6291456;  // 6MB
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Set OutPacketBuffer::maxSize to 6MB (supports complex scenes with large keyframes)"));

	// Create authentication database if enabled
	if (Config.bEnableAuthentication)
	{
		AuthDatabase = new UserAuthenticationDatabase(nullptr, False);
		AuthDatabase->addUserRecord(TCHAR_TO_ANSI(*Config.Username), TCHAR_TO_ANSI(*Config.Password));
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Authentication enabled"));
	}

	// Create RTSP server
	unsigned ReclamationSeconds = Config.ReclamationTimeoutSeconds > 0 ? Config.ReclamationTimeoutSeconds : 0;
	RtspServer = RTSPServer::createNew(*Environment, Config.Port, AuthDatabase, ReclamationSeconds);
	if (!RtspServer)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create RTSP server on port %d"), Config.Port);
		if (AuthDatabase)
		{
			delete AuthDatabase;
			AuthDatabase = nullptr;
		}
		Environment->reclaim();
		Environment = nullptr;
		delete Scheduler;
		Scheduler = nullptr;
		return false;
	}

	// Configure RTP-over-TCP
	if (!Config.bAllowRTPOverTCP)
	{
		RtspServer->disableStreamingRTPOverTCP();
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Server created successfully"));

	// Start server thread
	StopRequestedFlag = 0;
	ServerThread = FRunnableThread::Create(this, TEXT("RTSPServerThread"), 0, TPri_Normal);

	if (!ServerThread)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create server thread"));
		return false;
	}

	bIsRunning.store(true);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Initialized successfully"));
	return true;
}

void FRealGazeboRTSPServer::Shutdown()
{
	if (!bIsRunning.load())
	{
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Shutting down"));

	// CRITICAL FIX (Bug #1): Proper Live555 cleanup order to prevent memory leaks
	// Reference: VLC live555.cpp cleanup pattern

	// Step 1: Stop Live555 event loop FIRST (signals thread to exit)
	Stop();  // Sets StopRequestedFlag = 1

	// Step 2: Wait for thread to finish event loop processing
	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	// Step 3: Clear pending streams FIRST (they have no MediaSession)
	{
		FScopeLock Lock(&PendingStreamsMutex);
		if (PendingStreams.Num() > 0)
		{
			UE_LOG(LogRealGazeboStreaming, Verbose,
				TEXT("RTSPServer: Clearing %d pending streams"), PendingStreams.Num());
			PendingStreams.Empty();
		}
	}

	// Step 4: Remove all active media sessions BEFORE deleting RTSP server
	// IMPORTANT: Sessions must be removed while server is still valid
	{
		FScopeLock Lock(&StreamsMutex);

		// Unregister all media sessions from Live555
		for (auto& Pair : Streams)
		{
			if (Pair.Value.MediaSession && RtspServer)
			{
				// Remove from server (Live555 will delete the session)
				RtspServer->removeServerMediaSession(Pair.Value.MediaSession);
				UE_LOG(LogRealGazeboStreaming, Verbose,
					TEXT("RTSPServer: Removed media session for stream %s"),
					*Pair.Key.ToString());

				// Clear pointer (session is now owned by Live555 cleanup)
				Pair.Value.MediaSession = nullptr;
			}
		}

		// Now safe to empty the map
		Streams.Empty();
	}

	// Step 5: Delete RTSP server (after sessions removed)
	if (RtspServer)
	{
		Medium::close(RtspServer);
		RtspServer = nullptr;
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSPServer: RTSP server closed"));
	}

	// Step 6: Delete authentication database
	if (AuthDatabase)
	{
		delete AuthDatabase;
		AuthDatabase = nullptr;
	}

	// Step 7: Reclaim environment (after server deleted)
	if (Environment)
	{
		Environment->reclaim();
		Environment = nullptr;
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSPServer: Environment reclaimed"));
	}

	// Step 8: Delete scheduler last (after environment reclaimed)
	if (Scheduler)
	{
		delete Scheduler;
		Scheduler = nullptr;
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSPServer: Scheduler deleted"));
	}

	bIsRunning.store(false);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Shut down complete (proper cleanup order)"));
}

bool FRealGazeboRTSPServer::RegisterStream(const FStreamKey& StreamKey, const FString& StreamPath)
{
	if (!RtspServer || !Environment)
	{
		return false;
	}

	// Check if already registered in active streams
	{
		FScopeLock Lock(&StreamsMutex);
		if (FStreamData* ExistingStream = Streams.Find(StreamKey))
		{
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("RTSPServer: Duplicate StreamKey detected in active streams - refusing to register. ")
				TEXT("Incoming=%s | IncomingPath='%s' | ExistingPath='%s'. ")
				TEXT("Ensure each camera has a unique (VehicleID, VehicleTypeName, CameraID) combination."),
				*StreamKey.ToDebugString(), *StreamPath, *ExistingStream->StreamPath);
			return false;
		}
	}

	// Check if already in pending streams
	{
		FScopeLock Lock(&PendingStreamsMutex);
		if (FStreamData* ExistingStream = PendingStreams.Find(StreamKey))
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("RTSPServer: Stream already pending SPS/PPS: %s at path '%s'"),
				*StreamKey.ToString(), *StreamPath);
			return true;  // Not an error, just already pending
		}

		// DEFERRED REGISTRATION: Add to pending streams instead of creating MediaSession
		// Stream will be activated when SetSPSPPS() is called with valid parameters
		// This prevents RTSP clients from connecting before SPS/PPS is available
		FStreamData StreamData;
		StreamData.StreamPath = StreamPath;
		PendingStreams.Add(StreamKey, StreamData);

		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("RTSPServer: Stream registered as PENDING (waiting for SPS/PPS): %s at path '%s'"),
			*StreamKey.ToString(), *StreamPath);
	}

	return true;
}

void FRealGazeboRTSPServer::UnregisterStream(const FStreamKey& StreamKey)
{
	// Remove from active streams
	{
		FScopeLock Lock(&StreamsMutex);
		FStreamData* StreamData = Streams.Find(StreamKey);
		if (StreamData && StreamData->MediaSession)
		{
			RtspServer->removeServerMediaSession(StreamData->MediaSession);
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Unregistered active stream %s"), *StreamKey.ToString());
		}
		Streams.Remove(StreamKey);
	}

	// Also remove from pending streams if it's there
	{
		FScopeLock Lock(&PendingStreamsMutex);
		if (PendingStreams.Remove(StreamKey) > 0)
		{
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Removed pending stream %s"), *StreamKey.ToString());
		}
	}
}

bool FRealGazeboRTSPServer::PushFrame(const FStreamKey& StreamKey, TSharedPtr<FEncodedFrameData> EncodedFrame)
{
	if (!EncodedFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("RTSPServer: PushFrame - Invalid EncodedFrame for %s"),
			*StreamKey.ToDebugString());
		return false;
	}

	// CRITICAL DEBUG (2025-11-17): Log entry to verify function is called
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("RTSPServer: PushFrame ENTRY - %s | Frame %llu | Size: %d bytes | Keyframe: %s"),
		*StreamKey.ToDebugString(), EncodedFrame->FrameNumber, EncodedFrame->EncodedData.Num(),
		EncodedFrame->bIsKeyFrame ? TEXT("YES") : TEXT("NO"));

	FScopeLock Lock(&StreamsMutex);

	FStreamData* StreamData = Streams.Find(StreamKey);
	if (!StreamData)
	{
		// CRITICAL ERROR: Stream not registered in RTSP server!
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("RTSPServer: PushFrame FAILED - Stream NOT REGISTERED: %s | Total registered streams: %d"),
			*StreamKey.ToDebugString(), Streams.Num());

		// DEBUG: List all registered streams
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Registered streams:"));
		for (const auto& Pair : Streams)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  - %s"), *Pair.Key.ToDebugString());
		}

		return false;
	}

	// FRAME POOL MANAGEMENT (2025-11-11): Proper frame lifecycle with queue-based architecture
	// - EncodedFrame is enqueued in H264Source's immutable frame queue
	// - Frame remains in queue until Live555 fully transmits it
	// - TSharedPtr automatically releases frame when dequeued and processed
	// - This prevents mid-transmission overwrites that caused ffplay corruption

	// CRITICAL FIX (2025-11-14): Use PresentationTimeUs for RTP timestamps, NOT encoding duration
	// BUG: GetEncodingTimeMs() returns encoding DURATION (2-3ms), causing RTP timestamp collapse
	// FIX: Use PresentationTimeUs which contains proper capture time for monotonic RTP timestamps
	// This restores smooth playback on VLC/ffplay (30 fps instead of perceived ~0 fps)
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("RTSPServer: Calling H264Source::PushFrameData for %s | Frame %llu"),
		*StreamKey.ToDebugString(), EncodedFrame->FrameNumber);

	FRealGazeboH264Source::PushFrameData(StreamKey, EncodedFrame->EncodedData,
		EncodedFrame->PresentationTimeUs / 1000000.0,  // Presentation time in seconds
		EncodedFrame->bIsKeyFrame);

	// DEBUG: Log successful push
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("RTSPServer: PushFrame SUCCESS - %s | Frame %llu pushed to H264Source"),
		*StreamKey.ToDebugString(), EncodedFrame->FrameNumber);

	// Frame pool release happens automatically when TSharedPtr ref count drops to zero
	// after Live555 completes transmission (doGetNextFrame resets CurrentFrame)

	return true;
}

void FRealGazeboRTSPServer::SetSPSPPS(const FStreamKey& StreamKey, const TArray<uint8>& SPS, const TArray<uint8>& PPS)
{
	// CRITICAL FIX: Activate pending stream when SPS/PPS arrives
	// This ensures RTSP clients can only connect AFTER codec parameters are available

	// Check if stream is pending activation
	FStreamData PendingStreamData;
	bool bWasPending = false;
	{
		FScopeLock Lock(&PendingStreamsMutex);
		if (FStreamData* FoundPending = PendingStreams.Find(StreamKey))
		{
			// Move pending stream data to local variable
			PendingStreamData = *FoundPending;
			bWasPending = true;

			// Remove from pending (will be added to active streams)
			PendingStreams.Remove(StreamKey);

			UE_LOG(LogRealGazeboStreaming, Log,
				TEXT("RTSPServer: Activating pending stream %s (received SPS: %d bytes, PPS: %d bytes)"),
				*StreamKey.ToString(), SPS.Num(), PPS.Num());
		}
	}

	// If stream was pending, activate it now (create MediaSession)
	if (bWasPending)
	{
		// Set SPS/PPS BEFORE creating MediaSession (MediaSubsession needs it)
		FRealGazeboMediaSubsession::SetSPSPPS(StreamKey, SPS, PPS);

		// Store in stream data
		PendingStreamData.SPS = SPS;
		PendingStreamData.PPS = PPS;

		// Create MediaSession and add to Live555
		if (CreateMediaSession(StreamKey, PendingStreamData))
		{
			// Add to active streams
			FScopeLock Lock(&StreamsMutex);
			Streams.Add(StreamKey, PendingStreamData);

			UE_LOG(LogRealGazeboStreaming, Log,
				TEXT("RTSPServer: Stream ACTIVATED and ready for clients: %s at path '%s'"),
				*StreamKey.ToString(), *PendingStreamData.StreamPath);
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("RTSPServer: Failed to activate pending stream: %s"),
				*StreamKey.ToString());
		}
	}
	else
	{
		// Stream already active, just update SPS/PPS
		FScopeLock Lock(&StreamsMutex);
		FStreamData* StreamData = Streams.Find(StreamKey);
		if (StreamData)
		{
			StreamData->SPS = SPS;
			StreamData->PPS = PPS;
			FRealGazeboMediaSubsession::SetSPSPPS(StreamKey, SPS, PPS);

			UE_LOG(LogRealGazeboStreaming, Verbose,
				TEXT("RTSPServer: Updated SPS/PPS for active stream %s (SPS: %d bytes, PPS: %d bytes)"),
				*StreamKey.ToString(), SPS.Num(), PPS.Num());
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("RTSPServer: SetSPSPPS called for unknown stream: %s"),
				*StreamKey.ToString());
		}
	}
}

uint32 FRealGazeboRTSPServer::Run()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Thread started"));

	// Run Live555 event loop - doEventLoop() will check StopRequestedFlag internally
	if (Scheduler && Environment)
	{
		Scheduler->doEventLoop(&StopRequestedFlag);
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Thread stopped"));
	return 0;
}

void FRealGazeboRTSPServer::Stop()
{
	// Set flag to stop Live555 event loop
	// Live555's doEventLoop() checks this flag periodically
	StopRequestedFlag = 1;
}

bool FRealGazeboRTSPServer::CreateMediaSession(const FStreamKey& StreamKey, FStreamData& StreamData)
{
	if (!Environment || !RtspServer)
	{
		return false;
	}

	// CRITICAL: Use StringCast<ANSICHAR> directly to ensure proper lifetime
	// TCHAR_TO_ANSI macro creates temporaries that may be destroyed before Live555 copies them
	// By storing the StringCast objects, we keep the converted buffers alive during createNew() call
	auto StreamPathAnsi = StringCast<ANSICHAR>(*StreamData.StreamPath);
	const FString Description = FString::Printf(TEXT("H.264 stream from %s"), *StreamKey.ToString());
	auto DescriptionAnsi = StringCast<ANSICHAR>(*Description);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("CreateMediaSession: StreamPath='%s', Description='%s'"),
		*StreamData.StreamPath, *Description);

	StreamData.MediaSession = ServerMediaSession::createNew(*Environment,
		StreamPathAnsi.Get(), StreamPathAnsi.Get(), DescriptionAnsi.Get());

	if (!StreamData.MediaSession)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create media session"));
		return false;
	}

	// Create H.264 subsession
	ServerMediaSubsession* H264Subsession = FRealGazeboMediaSubsession::CreateNew(*Environment, StreamKey, False);
	if (!H264Subsession)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create H.264 subsession"));
		return false;
	}

	StreamData.MediaSession->addSubsession(H264Subsession);
	RtspServer->addServerMediaSession(StreamData.MediaSession);

	// Log URL
	char* Url = RtspServer->rtspURL(StreamData.MediaSession);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Stream available at %s"), ANSI_TO_TCHAR(Url));
	delete[] Url;

	return true;
}
