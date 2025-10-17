// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamingSubsystem.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "Core/RealGazeboStreamingSettings.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Pipeline/RealGazeboStreamPipeline.h"
#include "RTSP/RealGazeboRTSPServer.h"
#include "Threading/RealGazeboEncodingThread.h"
#include "Threading/RealGazeboRTSPThread.h"
#include "Encoding/RealGazeboEncoderFactory.h"

#include "Engine/GameInstance.h"

void URealGazeboStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem initializing..."));

	// Get settings
	const URealGazeboStreamingSettings* Settings = URealGazeboStreamingSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to get streaming settings!"));
		return;
	}

	// Check if streaming is enabled
	if (!Settings->bEnableStreaming)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Streaming is disabled in settings"));
		return;
	}

	// Store RTSP port
	RTSPPort = Settings->RTSPPort;

	// Initialize frame pool
	FramePool = MakeShared<FRealGazeboFramePool>(Settings->FramePoolSize);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Frame pool initialized with size: %d"), Settings->FramePoolSize);

	// Initialize RTSP server FIRST (threads depend on it)
	if (!InitializeRTSPServer())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to initialize RTSP server"));
		FramePool.Reset();
		return;
	}

	// Initialize worker threads AFTER RTSP server
	InitializeThreads();

	bIsInitialized = true;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem initialized successfully"));
}

void URealGazeboStreamingSubsystem::Deinitialize()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem shutting down..."));

	// Stop all streams first
	StopAllStreams();

	// Shutdown threads BEFORE RTSP server (threads depend on server)
	ShutdownThreads();

	// Shutdown RTSP server AFTER threads
	ShutdownRTSPServer();

	// Clear pipelines
	{
		FScopeLock Lock(&PipelineMapMutex);
		ActivePipelines.Empty();
	}

	// Release frame pool
	if (FramePool.IsValid())
	{
		FramePool->ClearPool();
		FramePool.Reset();
	}

	bIsInitialized = false;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem shut down complete"));

	Super::Deinitialize();
}

URealGazeboStreamingSubsystem* URealGazeboStreamingSubsystem::GetStreamingSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<URealGazeboStreamingSubsystem>();
}

bool URealGazeboStreamingSubsystem::RegisterCamera(URealGazeboStreamingCamera* Camera, const FRealGazeboStreamConfig& Config)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Subsystem not initialized"));
		return false;
	}

	if (!Camera)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Cannot register null camera"));
		return false;
	}

	// Validate configuration
	FString ErrorMessage;
	if (!ValidateStreamConfig(Config, ErrorMessage))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Invalid stream configuration: %s"), *ErrorMessage);
		return false;
	}

	// Create stream key from camera
	FStreamKey StreamKey = Camera->GetStreamKey();

	// Check if already registered
	{
		FScopeLock Lock(&PipelineMapMutex);
		if (ActivePipelines.Contains(StreamKey))
		{
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Camera already registered: %s"), *StreamKey.ToString());
			return false;
		}
	}

	// Create pipeline for this stream
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = MakeShared<FRealGazeboStreamPipeline>(
		StreamKey, Config, FramePool);

	// Register with RTSP server
	if (RTSPServer.IsValid())
	{
		const FString StreamPath = StreamKey.ToString();
		if (!RTSPServer->RegisterStream(StreamKey, StreamPath))
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to register stream with RTSP server: %s"), *StreamKey.ToString());
			return false;
		}
	}

	// Register encoder for this stream (hardware encoding only)
	if (EncodingThread.IsValid())
	{
		TSharedPtr<IRealGazeboHardwareEncoder> Encoder = FRealGazeboEncoderFactory::CreateEncoder(Config);
		if (Encoder.IsValid())
		{
			EncodingThread->RegisterEncoder(StreamKey, Encoder);
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("Failed to create hardware encoder for stream %s. Only NVENC/AMF supported."),
				*StreamKey.ToString());
			return false;
		}
	}


	// Add to active pipelines
	{
		FScopeLock Lock(&PipelineMapMutex);
		ActivePipelines.Add(StreamKey, Pipeline);
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Camera registered successfully: %s"), *StreamKey.ToString());
	return true;
}

void URealGazeboStreamingSubsystem::UnregisterCamera(URealGazeboStreamingCamera* Camera)
{
	if (!Camera)
	{
		return;
	}

	// Get stream key from camera
	FStreamKey StreamKey = Camera->GetStreamKey();

	// Stop stream if active
	StopStream(StreamKey);

	// Unregister encoder from encoding thread
	if (EncodingThread.IsValid())
	{
		EncodingThread->UnregisterEncoder(StreamKey);
	}

	// Unregister from RTSP server
	if (RTSPServer.IsValid())
	{
		RTSPServer->UnregisterStream(StreamKey);
	}

	// Remove pipeline
	{
		FScopeLock Lock(&PipelineMapMutex);
		ActivePipelines.Remove(StreamKey);
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Camera unregistered: %s"), *StreamKey.ToString());
}

bool URealGazeboStreamingSubsystem::IsCameraRegistered(const FStreamKey& StreamKey) const
{
	FScopeLock Lock(&PipelineMapMutex);
	return ActivePipelines.Contains(StreamKey);
}

bool URealGazeboStreamingSubsystem::StartStream(const FStreamKey& StreamKey)
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Cannot start stream - pipeline not found: %s"), *StreamKey.ToString());
		return false;
	}

	if (Pipeline->IsActive())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Stream already active: %s"), *StreamKey.ToString());
		return true;
	}

	if (!Pipeline->Start())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to start pipeline: %s"), *StreamKey.ToString());
		return false;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream started: %s"), *StreamKey.ToString());
	return true;
}

void URealGazeboStreamingSubsystem::StopStream(const FStreamKey& StreamKey)
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Cannot stop stream - pipeline not found: %s"), *StreamKey.ToString());
		return;
	}

	Pipeline->Stop();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream stopped: %s"), *StreamKey.ToString());
}

void URealGazeboStreamingSubsystem::PauseStream(const FStreamKey& StreamKey)
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Cannot pause stream - pipeline not found: %s"), *StreamKey.ToString());
		return;
	}

	Pipeline->Pause();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream paused: %s"), *StreamKey.ToString());
}

void URealGazeboStreamingSubsystem::ResumeStream(const FStreamKey& StreamKey)
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Cannot resume stream - pipeline not found: %s"), *StreamKey.ToString());
		return;
	}

	Pipeline->Resume();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream resumed: %s"), *StreamKey.ToString());
}

void URealGazeboStreamingSubsystem::StartAllStreams()
{
	FScopeLock Lock(&PipelineMapMutex);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Starting all streams (%d total)"), ActivePipelines.Num());

	for (const auto& Pair : ActivePipelines)
	{
		StartStream(Pair.Key);
	}
}

void URealGazeboStreamingSubsystem::StopAllStreams()
{
	FScopeLock Lock(&PipelineMapMutex);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stopping all streams (%d total)"), ActivePipelines.Num());

	for (const auto& Pair : ActivePipelines)
	{
		StopStream(Pair.Key);
	}
}

EStreamState URealGazeboStreamingSubsystem::GetStreamState(const FStreamKey& StreamKey) const
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		return EStreamState::Stopped;
	}

	return Pipeline->GetState();
}

int32 URealGazeboStreamingSubsystem::GetActiveStreamCount() const
{
	FScopeLock Lock(&PipelineMapMutex);

	int32 Count = 0;
	for (const auto& Pair : ActivePipelines)
	{
		if (Pair.Value.IsValid() && Pair.Value->IsActive())
		{
			Count++;
		}
	}

	return Count;
}

bool URealGazeboStreamingSubsystem::GetStreamStats(const FStreamKey& StreamKey, FStreamingStats& OutStats) const
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		return false;
	}

	OutStats = Pipeline->GetStats();
	return true;
}

void URealGazeboStreamingSubsystem::GetAggregatedStats(FStreamingStats& OutStats) const
{
	FScopeLock Lock(&PipelineMapMutex);

	OutStats.Reset();

	// Aggregate stats from all pipelines
	for (const auto& Pair : ActivePipelines)
	{
		if (Pair.Value.IsValid())
		{
			FStreamingStats PipelineStats = Pair.Value->GetStats();

			// Sum counters
			OutStats.TotalFramesCaptured += PipelineStats.TotalFramesCaptured;
			OutStats.TotalFramesEncoded += PipelineStats.TotalFramesEncoded;
			OutStats.TotalFramesDropped += PipelineStats.TotalFramesDropped;
			OutStats.KeyFrameCount += PipelineStats.KeyFrameCount;

			// Sum timing (will be averaged later)
			OutStats.CaptureTimeMs += PipelineStats.CaptureTimeMs;
			OutStats.EncodingTimeMs += PipelineStats.EncodingTimeMs;
			OutStats.RTSPTimeMs += PipelineStats.RTSPTimeMs;
			OutStats.TotalLatencyMs += PipelineStats.TotalLatencyMs;

			// Sum queue depths (hardware-only: no conversion queue)
			OutStats.EncodingQueueDepth += PipelineStats.EncodingQueueDepth;
			OutStats.RTSPQueueDepth += PipelineStats.RTSPQueueDepth;

			// Sum bitrate
			OutStats.CurrentBitrateMbps += PipelineStats.CurrentBitrateMbps;
			OutStats.AverageBitrateMbps += PipelineStats.AverageBitrateMbps;

			// Sum memory stats
			OutStats.PooledFrameCount += PipelineStats.PooledFrameCount;
			OutStats.ActiveFrameCount += PipelineStats.ActiveFrameCount;
			OutStats.EstimatedMemoryMB += PipelineStats.EstimatedMemoryMB;
		}
	}

	// Average timing if we have active streams
	int32 ActiveCount = GetActiveStreamCount();
	if (ActiveCount > 0)
	{
		OutStats.CaptureTimeMs /= ActiveCount;
		OutStats.EncodingTimeMs /= ActiveCount;
		OutStats.RTSPTimeMs /= ActiveCount;
		OutStats.TotalLatencyMs /= ActiveCount;
	}
}

TArray<FStreamKey> URealGazeboStreamingSubsystem::GetAllStreamKeys() const
{
	FScopeLock Lock(&PipelineMapMutex);

	TArray<FStreamKey> Keys;
	ActivePipelines.GetKeys(Keys);


	return Keys;
}

TMap<FStreamKey, FStreamingStats> URealGazeboStreamingSubsystem::GetAllStreamStats() const
{
	TMap<FStreamKey, FStreamingStats> StatsMap;

	// TODO: Collect stats from all pipelines

	return StatsMap;
}

FString URealGazeboStreamingSubsystem::GetRTSPURL(const FStreamKey& StreamKey) const
{
	return GenerateRTSPURL(StreamKey);
}

int32 URealGazeboStreamingSubsystem::GetRTSPPort() const
{
	return RTSPPort;
}

bool URealGazeboStreamingSubsystem::IsRTSPServerRunning() const
{
	// TODO: Check RTSP server state
	return RTSPServer.IsValid();
}

bool URealGazeboStreamingSubsystem::UpdateStreamConfig(const FStreamKey& StreamKey, const FRealGazeboStreamConfig& NewConfig)
{
	// TODO: Update pipeline configuration (must be stopped first)
	return false;
}

bool URealGazeboStreamingSubsystem::GetStreamConfig(const FStreamKey& StreamKey, FRealGazeboStreamConfig& OutConfig) const
{
	// TODO: Get config from pipeline
	return false;
}

TSharedPtr<FRealGazeboStreamPipeline> URealGazeboStreamingSubsystem::GetPipeline(const FStreamKey& StreamKey) const
{
	FScopeLock Lock(&PipelineMapMutex);

	const TSharedPtr<FRealGazeboStreamPipeline>* FoundPipeline = ActivePipelines.Find(StreamKey);
	return FoundPipeline ? *FoundPipeline : nullptr;
}

void URealGazeboStreamingSubsystem::InitializeThreads()
{
	// Create RTSP thread first (encoding thread needs it)
	RTSPThread = MakeShared<FRealGazeboRTSPThread>(RTSPServer);
	RTSPThreadHandle = FRunnableThread::Create(
		RTSPThread.Get(),
		TEXT("RealGazeboRTSP"),
		0,
		TPri_Normal  // Normal priority for RTSP delivery
	);

	// Create hardware encoding thread (NVENC/AMF only)
	EncodingThread = MakeShared<FRealGazeboEncodingThread>(FramePool, RTSPThread);
	EncodingThreadHandle = FRunnableThread::Create(
		EncodingThread.Get(),
		TEXT("RealGazeboEncoding"),
		0,
		TPri_Highest  // Highest priority for encoding
	);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Worker threads initialized (Hardware Encoding + RTSP)"));
}

void URealGazeboStreamingSubsystem::ShutdownThreads()
{
	// Stop encoding thread first (depends on RTSP thread)
	if (EncodingThread.IsValid())
	{
		EncodingThread->Stop();
		if (EncodingThreadHandle)
		{
			EncodingThreadHandle->WaitForCompletion();
			delete EncodingThreadHandle;
			EncodingThreadHandle = nullptr;
		}
		EncodingThread.Reset();
	}

	// Stop and cleanup RTSP thread
	if (RTSPThread.IsValid())
	{
		RTSPThread->Stop();
		if (RTSPThreadHandle)
		{
			RTSPThreadHandle->WaitForCompletion();
			delete RTSPThreadHandle;
			RTSPThreadHandle = nullptr;
		}
		RTSPThread.Reset();
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Worker threads shut down"));
}

bool URealGazeboStreamingSubsystem::InitializeRTSPServer()
{
	if (RTSPServer.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("RTSP server already initialized"));
		return true;
	}

	// Create RTSP server configuration
	FRealGazeboRTSPConfig RTSPConfig;
	RTSPConfig.Port = RTSPPort;
	RTSPConfig.bEnableAuthentication = false;  // Disabled for local testing
	RTSPConfig.bAllowRTPOverTCP = true;        // Allow TCP transport
	RTSPConfig.ReclamationTimeoutSeconds = 65; // Default timeout

	// Create and initialize RTSP server
	RTSPServer = MakeShared<FRealGazeboRTSPServer>();
	if (!RTSPServer->Initialize(RTSPConfig))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to initialize RTSP server on port %d"), RTSPPort);
		RTSPServer.Reset();
		return false;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSP server initialized successfully on port %d"), RTSPPort);
	return true;
}

void URealGazeboStreamingSubsystem::ShutdownRTSPServer()
{
	if (RTSPServer.IsValid())
	{
		RTSPServer->Shutdown();
		RTSPServer.Reset();
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSP server shut down"));
	}
}

bool URealGazeboStreamingSubsystem::ValidateStreamConfig(const FRealGazeboStreamConfig& Config, FString& OutErrorMessage) const
{
	return Config.IsValid(OutErrorMessage);
}

FString URealGazeboStreamingSubsystem::GenerateRTSPURL(const FStreamKey& StreamKey) const
{
	// Format: rtsp://localhost:PORT/stream_path
	// Example: rtsp://localhost:8554/iris_0/fpv
	return FString::Printf(TEXT("rtsp://localhost:%d/%s"), RTSPPort, *StreamKey.ToString());
}

bool URealGazeboStreamingSubsystem::SupportsTextureEncoding(const FStreamKey& StreamKey) const
{
	if (!EncodingThread.IsValid())
	{
		return false;
	}

	return EncodingThread->SupportsTextureEncoding(StreamKey);
}

bool URealGazeboStreamingSubsystem::SubmitTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
                                                        double Timestamp, uint64 FrameNumber)
{
	if (!EncodingThread.IsValid())
	{
		return false;
	}

	return EncodingThread->EnqueueTextureFrame(StreamKey, Texture, Timestamp, FrameNumber);
}
