// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RealGazeboRTSPServer.h"
#include "RTSP/RealGazeboH264Source.h"
#include "RTSP/RealGazeboMediaSubsession.h"
#include "Core/RealGazeboStreamingLogger.h"

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

	// Increase RTP packet buffer size for high bitrate streams
	OutPacketBuffer::maxSize = 1536000;  // 1.5MB
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Set OutPacketBuffer::maxSize to 1.5MB"));

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
	StopRequestedFlag.store(0);
	ServerThread = FRunnableThread::Create(this, TEXT("RTSPServerThread"), 0, TPri_Normal);

	if (!ServerThread)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("RTSPServer: Failed to create server thread"));
		return false;
	}

	ServerStartTime = FPlatformTime::Seconds();
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

	// Stop thread
	Stop();
	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	// Clean up streams
	{
		FScopeLock Lock(&StreamsMutex);
		Streams.Empty();
	}

	// Clean up Live555
	if (RtspServer)
	{
		Medium::close(RtspServer);
		RtspServer = nullptr;
	}

	if (AuthDatabase)
	{
		delete AuthDatabase;
		AuthDatabase = nullptr;
	}

	if (Environment)
	{
		Environment->reclaim();
		Environment = nullptr;
	}

	if (Scheduler)
	{
		delete Scheduler;
		Scheduler = nullptr;
	}

	bIsRunning.store(false);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Shut down complete"));
}

bool FRealGazeboRTSPServer::RegisterStream(const FStreamKey& StreamKey, const FString& StreamPath)
{
	if (!RtspServer || !Environment)
	{
		return false;
	}

	FScopeLock Lock(&StreamsMutex);

	if (Streams.Contains(StreamKey))
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("RTSPServer: Stream already registered: %s"), *StreamKey.ToString());
		return false;
	}

	FStreamData StreamData;
	StreamData.StreamPath = StreamPath;

	if (!CreateMediaSession(StreamKey, StreamData))
	{
		return false;
	}

	Streams.Add(StreamKey, StreamData);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Registered stream %s at path '%s'"),
		*StreamKey.ToString(), *StreamPath);

	return true;
}

void FRealGazeboRTSPServer::UnregisterStream(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&StreamsMutex);

	FStreamData* StreamData = Streams.Find(StreamKey);
	if (StreamData && StreamData->MediaSession)
	{
		RtspServer->removeServerMediaSession(StreamData->MediaSession);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Unregistered stream %s"), *StreamKey.ToString());
	}

	Streams.Remove(StreamKey);
}

bool FRealGazeboRTSPServer::PushFrame(const FStreamKey& StreamKey, TSharedPtr<FEncodedFrameData> EncodedFrame)
{
	if (!EncodedFrame.IsValid())
	{
		return false;
	}

	FScopeLock Lock(&StreamsMutex);

	FStreamData* StreamData = Streams.Find(StreamKey);
	if (!StreamData)
	{
		return false;
	}

	// Queue frame for Live555 source
	if (StreamData->FrameQueue.IsValid())
	{
		StreamData->FrameQueue->Enqueue(EncodedFrame);
	}

	// Push to H264 source
	FRealGazeboH264Source::PushFrameData(StreamKey, EncodedFrame->EncodedData,
		EncodedFrame->EncodingTimestamp, EncodedFrame->bIsKeyFrame);

	// Update stats
	StreamData->FramesSent++;
	StreamData->BytesSent += EncodedFrame->EncodedData.Num();

	return true;
}

void FRealGazeboRTSPServer::SetSPSPPS(const FStreamKey& StreamKey, const TArray<uint8>& SPS, const TArray<uint8>& PPS)
{
	FScopeLock Lock(&StreamsMutex);

	FStreamData* StreamData = Streams.Find(StreamKey);
	if (StreamData)
	{
		StreamData->SPS = SPS;
		StreamData->PPS = PPS;

		// Also set in media subsession
		FRealGazeboMediaSubsession::SetSPSPPS(StreamKey, SPS, PPS);

		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSPServer: Set SPS/PPS for stream %s (SPS: %d bytes, PPS: %d bytes)"),
			*StreamKey.ToString(), SPS.Num(), PPS.Num());
	}
}

FString FRealGazeboRTSPServer::GetStreamURL(const FStreamKey& StreamKey) const
{
	FScopeLock Lock(&StreamsMutex);

	const FStreamData* StreamData = Streams.Find(StreamKey);
	if (StreamData)
	{
		return FString::Printf(TEXT("rtsp://localhost:%d/%s"), ServerConfig.Port, *StreamData->StreamPath);
	}

	return FString();
}

FRealGazeboRTSPStats FRealGazeboRTSPServer::GetStatistics() const
{
	FScopeLock Lock(&StreamsMutex);

	FRealGazeboRTSPStats Stats = Statistics;
	Stats.UptimeSeconds = FPlatformTime::Seconds() - ServerStartTime;

	// Aggregate stream stats
	Stats.TotalFramesSent = 0;
	Stats.TotalBytesSent = 0;
	for (const auto& Pair : Streams)
	{
		Stats.TotalFramesSent += Pair.Value.FramesSent;
		Stats.TotalBytesSent += Pair.Value.BytesSent;
	}

	return Stats;
}

uint32 FRealGazeboRTSPServer::Run()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Thread started"));

	while (StopRequestedFlag.load() == 0)
	{
		if (Scheduler && Environment)
		{
			// Run Live555 event loop with short timeout (non-blocking)
			Scheduler->doEventLoop(&StopRequestedFlag);
		}
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPServer: Thread stopped"));
	return 0;
}

void FRealGazeboRTSPServer::Stop()
{
	StopRequestedFlag.store(1);
}

bool FRealGazeboRTSPServer::CreateMediaSession(const FStreamKey& StreamKey, FStreamData& StreamData)
{
	if (!Environment || !RtspServer)
	{
		return false;
	}

	const char* StreamPathCStr = TCHAR_TO_ANSI(*StreamData.StreamPath);
	const FString Description = FString::Printf(TEXT("H.264 stream from %s"), *StreamKey.ToString());
	const char* DescriptionCStr = TCHAR_TO_ANSI(*Description);

	StreamData.MediaSession = ServerMediaSession::createNew(*Environment,
		StreamPathCStr, StreamPathCStr, DescriptionCStr);

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

void FRealGazeboRTSPServer::UpdateStatistics()
{
	// TODO: Update statistics from Live555
}
