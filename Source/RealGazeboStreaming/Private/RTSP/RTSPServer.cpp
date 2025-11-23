// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RTSPServer.h"
#include "RTSP/H264StreamSource.h"
#include "RTSP/H264ServerMediaSubsession.h"
#include "HAL/PlatformProcess.h"

// Live555 includes
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

//----------------------------------------------------------
// Construction & Initialization
//----------------------------------------------------------

FRTSPServerWrapper::FRTSPServerWrapper()
{
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Created"));
}

FRTSPServerWrapper::~FRTSPServerWrapper()
{
	Stop();
}

bool FRTSPServerWrapper::Start(int32 InPort, FString* OutErrorMessage)
{
	if (bRunning)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("RTSP server already running");
		}
		return true;
	}

	Port = InPort;
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Starting on port %d..."), Port);

	// Initialize Live555
	if (!InitializeLive555())
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Failed to initialize Live555 environment");
		}
		return false;
	}

	// Create server thread
	Thread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(
		this,
		TEXT("RTSPServerThread"),
		0,
		TPri_Normal
	));

	if (!Thread)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Failed to create RTSP server thread");
		}
		CleanupLive555();
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Started successfully on rtsp://localhost:%d"), Port);
	return true;
}

void FRTSPServerWrapper::Stop()
{
	if (!bRunning && !Thread)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Stopping..."));
	bStopRequested = true;

	if (Thread)
	{
		Thread->WaitForCompletion();
		Thread.Reset();
	}

	CleanupLive555();

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Stopped (Added: %llu, Removed: %llu streams)"),
		TotalStreamsAdded.load(), TotalStreamsRemoved.load());
}

//----------------------------------------------------------
// Internal Helpers
//----------------------------------------------------------

bool FRTSPServerWrapper::InitializeLive555()
{
	// CRITICAL: Increase OutPacketBuffer::maxSize BEFORE creating any RTP sinks
	// Default 60KB is too small for large I-frames (can exceed 150KB at high resolutions)
	// This MUST be done before RTSPServer::createNew()
	OutPacketBuffer::maxSize = 200000; // 200KB - handles 1600x1200 I-frames safely
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Set OutPacketBuffer::maxSize to %u bytes"), OutPacketBuffer::maxSize);

	// Create Live555 task scheduler
	Scheduler = BasicTaskScheduler::createNew();
	if (!Scheduler)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Failed to create task scheduler"));
		return false;
	}

	// Create Live555 usage environment
	Env = BasicUsageEnvironment::createNew(*Scheduler);
	if (!Env)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Failed to create usage environment"));
		delete Scheduler;
		Scheduler = nullptr;
		return false;
	}

	// Create RTSP server
	Server = RTSPServer::createNew(*Env, Port);
	if (!Server)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Failed to create RTSP server on port %d"), Port);
		Env->reclaim();
		Env = nullptr;
		delete Scheduler;
		Scheduler = nullptr;
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Live555 initialized successfully"));
	return true;
}

void FRTSPServerWrapper::CleanupLive555()
{
	// Remove all media sessions
	{
		FScopeLock Lock(&StreamMutex);
		for (auto& Pair : MediaSessions)
		{
			if (Server && Pair.Value)
			{
				Server->removeServerMediaSession(Pair.Value);
			}
		}
		MediaSessions.Empty();
	}

	// Cleanup Live555 components
	if (Server)
	{
		Medium::close(Server);
		Server = nullptr;
	}

	if (Env)
	{
		Env->reclaim();
		Env = nullptr;
	}

	if (Scheduler)
	{
		delete Scheduler;
		Scheduler = nullptr;
	}
}

//----------------------------------------------------------
// Stream Management
//----------------------------------------------------------

bool FRTSPServerWrapper::AddStream(const FStreamIdentifier& StreamID, FH264StreamSource* StreamSource, FString& OutRTSPURL)
{
	if (!Server || !Env || !StreamSource)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Cannot add stream - server not ready"));
		return false;
	}

	FScopeLock Lock(&StreamMutex);

	const FString StreamName = StreamID.ToRTSPPath();

	// Check if stream already exists
	if (MediaSessions.Contains(StreamName))
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSPServerWrapper: Stream already exists: %s"), *StreamName);
		OutRTSPURL = GenerateRTSPURL(StreamID);
		return true;
	}

	// Create media session
	ServerMediaSession* SMS = ServerMediaSession::createNew(*Env, TCHAR_TO_ANSI(*StreamName),
		TCHAR_TO_ANSI(*StreamName), "RealGazebo H.264 Stream");

	if (!SMS)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Failed to create media session for %s"), *StreamName);
		return false;
	}

	// Create H264 server media subsession
	// This handles on-demand stream creation when clients connect
	// reuseFirstSource=True means all clients share the same NAL queue (efficient)
	FH264ServerMediaSubsession* Subsession = FH264ServerMediaSubsession::createNew(
		*Env, StreamSource, True);

	if (!Subsession)
	{
		UE_LOG(LogTemp, Error, TEXT("RTSPServerWrapper: Failed to create media subsession for %s"), *StreamName);
		Medium::close(SMS);
		return false;
	}

	// Add subsession to media session
	SMS->addSubsession(Subsession);

	// Add media session to RTSP server
	Server->addServerMediaSession(SMS);

	// Store session for cleanup
	MediaSessions.Add(StreamName, SMS);

	// Generate RTSP URL
	OutRTSPURL = GenerateRTSPURL(StreamID);

	TotalStreamsAdded++;

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Added stream: %s"), *OutRTSPURL);
	return true;
}

void FRTSPServerWrapper::RemoveStream(const FStreamIdentifier& StreamID)
{
	if (!Server)
	{
		return;
	}

	FScopeLock Lock(&StreamMutex);

	const FString StreamName = StreamID.ToRTSPPath();

	ServerMediaSession** SMS = MediaSessions.Find(StreamName);
	if (SMS && *SMS)
	{
		Server->removeServerMediaSession(*SMS);
		MediaSessions.Remove(StreamName);
		TotalStreamsRemoved++;

		UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Removed stream: %s"), *StreamName);
	}
}

FString FRTSPServerWrapper::GenerateRTSPURL(const FStreamIdentifier& StreamID) const
{
	return FString::Printf(TEXT("rtsp://localhost:%d/%s"), Port, *StreamID.ToRTSPPath());
}

//----------------------------------------------------------
// FRunnable Interface
//----------------------------------------------------------

bool FRTSPServerWrapper::Init()
{
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Thread Init()"));
	bRunning = true;
	return true;
}

uint32 FRTSPServerWrapper::Run()
{
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Thread Run() started"));

	// Run Live555 event loop
	// Note: Live555 doEventLoop blocks until watch variable is set
	// Live555 uses std::atomic<char>* for EventLoopWatchVariable

	// Watch variable for Live555 event loop (atomic for thread safety)
	std::atomic<char> WatchVariable{0};

	while (!bStopRequested && Env)
	{
		// Reset watch variable
		WatchVariable.store(0);

		// Schedule a delayed task to break the event loop after 10ms
		// This allows us to check bStopRequested periodically
		Env->taskScheduler().scheduleDelayedTask(10000, // 10ms in microseconds
			[](void* clientData) {
				std::atomic<char>* watch = static_cast<std::atomic<char>*>(clientData);
				watch->store(1); // Signal to break event loop
			}, &WatchVariable);

		// Process events until watch variable becomes non-zero
		Env->taskScheduler().doEventLoop(&WatchVariable);
	}

	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Thread Run() exiting"));
	return 0;
}

void FRTSPServerWrapper::Exit()
{
	UE_LOG(LogTemp, Log, TEXT("RTSPServerWrapper: Thread Exit()"));
	bRunning = false;
}

//----------------------------------------------------------
// Status
//----------------------------------------------------------

int32 FRTSPServerWrapper::GetStreamCount() const
{
	FScopeLock Lock(&StreamMutex);
	return MediaSessions.Num();
}

FString FRTSPServerWrapper::GetStatsString() const
{
	return FString::Printf(
		TEXT("RTSPServer: Port=%d, Running=%s, Streams=%d, Added=%llu, Removed=%llu"),
		Port,
		bRunning.load() ? TEXT("Yes") : TEXT("No"),
		GetStreamCount(),
		TotalStreamsAdded.load(),
		TotalStreamsRemoved.load()
	);
}
