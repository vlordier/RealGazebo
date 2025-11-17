// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamingSubsystem.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Core/RealGazeboStreamManager.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Pipeline/RealGazeboStreamPipeline.h"
#include "RTSP/RealGazeboRTSPServer.h"
#include "RTSP/RealGazeboH264Source.h"
#include "Threading/RealGazeboEncodingThread.h"
#include "Threading/RealGazeboRTSPThread.h"
#include "Encoding/RealGazeboEncoderFactory.h"
#include "Capture/RealGazeboSceneCapturePool.h"

#include "Engine/GameInstance.h"
#include "EngineUtils.h"  // For TActorIterator

void URealGazeboStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem initializing..."));

	// Use default RTSP port
	RTSPPort = RealGazeboStreamingConstants::RTSP_PORT;

	// Initialize frame pool with small initial size (will grow dynamically)
	FramePool = MakeShared<FRealGazeboFramePool>(10); // Initial size: 10, grows based on active streams
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Frame pool initialized with dynamic sizing (initial: 10 frames)"));

	// Initialize SceneCapture2D component pool to reduce allocation overhead
	SceneCapturePool = MakeShared<FRealGazeboSceneCapturePool>();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("SceneCapture pool initialized with dynamic sizing (initial: %d components)"),
		SceneCapturePool->GetMaxPoolSize());

	// NOTE: RTSP server and threads are NOT initialized here
	// They will be initialized lazily on first camera registration
	// This allows StreamManager to set the RTSP port before server starts

	bIsInitialized = true;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazebo Streaming Subsystem initialized successfully (RTSP server will start on first camera registration)"));
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

	// Clear scene capture pool to release all pooled components
	if (SceneCapturePool.IsValid())
	{
		SceneCapturePool->Clear();
		SceneCapturePool.Reset();
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

	// Lazy initialization: Start RTSP server and threads on first camera registration
	// This allows StreamManager to set RTSP port before server starts
	if (!RTSPServer.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("First camera registration - initializing RTSP server and threads"));

		// Initialize RTSP server with current RTSPPort setting
		if (!InitializeRTSPServer())
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to initialize RTSP server on port %d"), RTSPPort);
			return false;
		}

		// Initialize worker threads AFTER RTSP server
		InitializeThreads();

		UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSP server and threads initialized successfully on port %d"), RTSPPort);
	}

	// Check if already registered (no artificial stream limits)
	{
		FScopeLock Lock(&PipelineMapMutex);
		if (ActivePipelines.Contains(StreamKey))
		{
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Camera already registered: %s"), *StreamKey.ToString());
			return false;
		}

		// NOTE: No artificial concurrent stream limits enforced
		// Number of streams is limited only by:
		//  1. GPU hardware encoder session limits (NVENC: varies by GPU, AMF: typically 8-12+)
		//  2. System performance (GPU/CPU resources, memory bandwidth)
		//  3. Network bandwidth for RTSP streaming
		// Streams will fail gracefully if hardware encoder session limit is reached
	}

	// Create pipeline for this stream with Config settings (MaxQueueSize, bAllowFrameDropping)
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

		// CRITICAL FIX: Configure RTSP queue MaxQueueSize from user config
		// This ensures all three queues (encoding, RTSP thread, RTSP H264Source) honor same limit
		FRealGazeboH264Source::SetMaxQueueSize(StreamKey, Config.MaxQueueSize);
	}

	// Register encoder for this stream (hardware encoding only)
	// NOTE: SPS/PPS will be automatically extracted and set by the encoding thread
	// after the first keyframe is successfully encoded
	if (EncodingThread.IsValid())
	{
		TSharedPtr<IRealGazeboHardwareEncoder> Encoder = FRealGazeboEncoderFactory::CreateEncoder(Config);
		if (Encoder.IsValid())
		{
			// Register encoder with bitrate (for RTSP SDP generation)
			EncodingThread->RegisterEncoder(StreamKey, Encoder, Config.BitrateKbps);

			// CRITICAL FIX (BUG #2): Request immediate keyframe to get SPS/PPS ASAP
			// Without this, clients connecting in first ~50ms get invalid parameter sets
			// First frame will be keyframe anyway, but this ensures timing is predictable
			EncodingThread->RequestKeyFrame(StreamKey);

			UE_LOG(LogRealGazeboStreaming, Log,
				TEXT("Registered encoder for stream %s (%s) - SPS/PPS will be extracted from first keyframe | Bitrate: %d kbps"),
				*StreamKey.ToString(), *Encoder->GetEncoderName(), Config.BitrateKbps);
		}
		else
		{
			// CRITICAL: Encoder creation failed - likely no NVIDIA/AMD GPU or missing drivers
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("Failed to create hardware encoder for stream %s. Possible causes:"),
				*StreamKey.ToString());
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  1. GPU is not NVIDIA (requires NVENC) or AMD (requires AMF)"));
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  2. GPU drivers not installed or out of date"));
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  3. CUDA module not loaded (NVIDIA only)"));
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  Detected GPU: %s"), *GRHIAdapterName);
			FString RHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unknown");
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("  RHI: %s"), *RHIName);

			// Unregister stream from RTSP server since encoder failed
			if (RTSPServer.IsValid())
			{
				RTSPServer->UnregisterStream(StreamKey);
			}

			return false;
		}
	}


	// Add to active pipelines
	{
		FScopeLock Lock(&PipelineMapMutex);
		ActivePipelines.Add(StreamKey, Pipeline);
	}

	// Update pool capacities based on new camera count
	UpdatePoolCapacities();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Camera registered successfully: %s"), *StreamKey.ToString());

	// AUTO-START: Check if StreamManager has bAutoStartStreams enabled
	// If so, automatically start the stream immediately after registration
	// This solves the timing issue where StartAllStreams() is called before cameras register
	if (UWorld* World = Camera->GetWorld())
	{
		// Find StreamManager to check auto-start setting
		for (TActorIterator<class ARealGazeboStreamManager> It(World); It; ++It)
		{
			ARealGazeboStreamManager* StreamManager = *It;
			if (StreamManager && StreamManager->bAutoStartStreams)
			{
				UE_LOG(LogRealGazeboStreaming, Log,
					TEXT("Auto-start enabled - starting stream immediately: %s"), *StreamKey.ToString());

				// Start stream immediately
				if (!StartStream(StreamKey))
				{
					UE_LOG(LogRealGazeboStreaming, Warning,
						TEXT("Auto-start failed for stream: %s"), *StreamKey.ToString());
				}

				break;  // Only need to check first StreamManager
			}
		}
	}

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

	// Update pool capacities based on new camera count
	UpdatePoolCapacities();

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

	// CRITICAL: Fetch encoding stats from encoding thread (not in pipeline stats)
	// Hardware-only architecture: frames go directly from encoding thread to RTSP thread
	if (EncodingThread.IsValid())
	{
		int32 EncodingQueueDepth = 0;
		int64 FramesEncoded = 0;
		int64 FramesDropped = 0;
		float AvgEncodeTimeMs = 0.0f;
		EncodingThread->GetStreamStatistics(StreamKey, EncodingQueueDepth, FramesEncoded,
			FramesDropped, AvgEncodeTimeMs);
		OutStats.EncodingQueueDepth = EncodingQueueDepth;
		OutStats.TotalFramesEncoded = FramesEncoded;

		// Set configured target bitrate (hardware encoders maintain target bitrate)
		// In hardware-only path, per-frame bitrate measurement is not available as frames
		// bypass the pipeline and go directly from encoding thread to RTSP thread
		const FRealGazeboStreamConfig& Config = Pipeline->GetConfig();
		OutStats.CurrentBitrateMbps = Config.BitrateKbps / 1000.0f;  // Convert kbps to Mbps
		OutStats.AverageBitrateMbps = OutStats.CurrentBitrateMbps;
	}

	// Populate backpressure status
	OutStats.bIsBackpressured = IsStreamBackpressured(StreamKey);

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
			OutStats.TotalFramesEncoded += PipelineStats.TotalFramesEncoded;
			OutStats.KeyFrameCount += PipelineStats.KeyFrameCount;

			// Sum queue depths
			// CRITICAL: Fetch encoding queue depth from encoding thread (not in pipeline stats)
			if (EncodingThread.IsValid())
			{
				int32 EncodingQueueDepth = 0;
				int64 FramesEncoded = 0;
				int64 FramesDropped = 0;
				float AvgEncodeTimeMs = 0.0f;
				EncodingThread->GetStreamStatistics(Pair.Key, EncodingQueueDepth, FramesEncoded,
					FramesDropped, AvgEncodeTimeMs);
				OutStats.EncodingQueueDepth += EncodingQueueDepth;
			}
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
}

TArray<FStreamKey> URealGazeboStreamingSubsystem::GetAllStreamKeys() const
{
	FScopeLock Lock(&PipelineMapMutex);

	TArray<FStreamKey> Keys;
	ActivePipelines.GetKeys(Keys);


	return Keys;
}

FString URealGazeboStreamingSubsystem::GetRTSPURL(const FStreamKey& StreamKey) const
{
	return GenerateRTSPURL(StreamKey);
}

int32 URealGazeboStreamingSubsystem::GetRTSPPort() const
{
	return RTSPPort;
}

bool URealGazeboStreamingSubsystem::SetRTSPPort(int32 Port)
{
	// Validate port range
	if (Port < 1024 || Port > 65535)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Invalid RTSP port %d (must be 1024-65535)"), Port);
		return false;
	}

	// If port is already set to requested value, return success (idempotent)
	if (RTSPPort == Port)
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSP port already set to %d"), Port);
		return true;
	}

	// Cannot change port if RTSP server is already running
	if (RTSPServer.IsValid() && RTSPServer->IsRunning())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Cannot change RTSP port while server is running (current: %d, requested: %d)"), RTSPPort, Port);
		return false;
	}

	RTSPPort = Port;
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSP port set to %d"), RTSPPort);
	return true;
}

bool URealGazeboStreamingSubsystem::IsRTSPServerRunning() const
{
	return RTSPServer.IsValid() && RTSPServer->IsRunning();
}

bool URealGazeboStreamingSubsystem::UpdateStreamConfig(const FStreamKey& StreamKey, const FRealGazeboStreamConfig& NewConfig)
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Cannot update config - pipeline not found: %s"), *StreamKey.ToString());
		return false;
	}

	// Validate new configuration
	FString ErrorMessage;
	if (!ValidateStreamConfig(NewConfig, ErrorMessage))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Invalid stream configuration: %s"), *ErrorMessage);
		return false;
	}

	// Update pipeline configuration (pipeline must be stopped)
	if (Pipeline->IsActive())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Cannot update config while stream is active. Stop stream first: %s"), *StreamKey.ToString());
		return false;
	}

	if (!Pipeline->UpdateConfig(NewConfig))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Failed to update pipeline configuration: %s"), *StreamKey.ToString());
		return false;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream configuration updated: %s"), *StreamKey.ToString());
	return true;
}

bool URealGazeboStreamingSubsystem::GetStreamConfig(const FStreamKey& StreamKey, FRealGazeboStreamConfig& OutConfig) const
{
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (!Pipeline.IsValid())
	{
		return false;
	}

	OutConfig = Pipeline->GetConfig();
	return true;
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

	// Create hardware encoding thread with RTSP server reference for SPS/PPS setting
	EncodingThread = MakeShared<FRealGazeboEncodingThread>(FramePool, RTSPThread, RTSPServer);
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

bool URealGazeboStreamingSubsystem::IsStreamBackpressured(const FStreamKey& StreamKey) const
{
	// Check both encoding queue and RTSP queue for backpressure (>75% full)
	// Encoding queue is managed by encoding thread, RTSP queue by pipeline

	bool bEncodingBackpressure = false;
	bool bRTSPBackpressure = false;

	// Check encoding queue depth from encoding thread
	if (EncodingThread.IsValid())
	{
		int32 EncodingQueueDepth = 0;
		int64 FramesEncoded = 0;
		int64 FramesDropped = 0;
		float AvgEncodeTimeMs = 0.0f;
		EncodingThread->GetStreamStatistics(StreamKey, EncodingQueueDepth, FramesEncoded,
			FramesDropped, AvgEncodeTimeMs);

		// CRITICAL FIX: Use per-stream dynamic MaxQueueSize instead of static constant
		// MaxQueueSize scales with frame rate (FPS * 10), so backpressure threshold must too
		// Get MaxQueueSize from pipeline config
		TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
		int32 MaxQueueSize = RealGazeboStreamingConstants::MAX_QUEUE_SIZE;  // Fallback default
		if (Pipeline.IsValid())
		{
			MaxQueueSize = Pipeline->GetConfig().MaxQueueSize;
		}

		// Queue is backpressured if >75% full (using dynamic MaxQueueSize)
		bEncodingBackpressure = (EncodingQueueDepth > (MaxQueueSize * 3 / 4));
	}

	// Check RTSP queue from pipeline
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (Pipeline.IsValid())
	{
		bRTSPBackpressure = Pipeline->IsBackpressured();
	}

	// Stream is backpressured if EITHER queue is experiencing backpressure
	return bEncodingBackpressure || bRTSPBackpressure;
}

bool URealGazeboStreamingSubsystem::SubmitTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
                                                        int64 TimestampUs, uint64 FrameNumber)
{
	if (!EncodingThread.IsValid())
	{
		return false;
	}

	return EncodingThread->EnqueueTextureFrame(StreamKey, Texture, TimestampUs, FrameNumber);
}

void URealGazeboStreamingSubsystem::UpdateStreamBitrate(const FStreamKey& StreamKey, int32 NewBitrateKbps)
{
	// Update encoder bitrate dynamically
	if (EncodingThread.IsValid())
	{
		EncodingThread->UpdateBitrate(StreamKey, NewBitrateKbps);
	}

	// Update pipeline config bitrate so GetConfig() returns updated value
	TSharedPtr<FRealGazeboStreamPipeline> Pipeline = GetPipeline(StreamKey);
	if (Pipeline.IsValid())
	{
		Pipeline->UpdateBitrate(NewBitrateKbps);
	}

	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("Subsystem: Updated bitrate for stream %s to %d kbps"),
		*StreamKey.ToString(), NewBitrateKbps);
}

void URealGazeboStreamingSubsystem::RequestKeyFrame(const FStreamKey& StreamKey)
{
	if (!EncodingThread.IsValid())
	{
		return;
	}

	// Request keyframe (I-frame) from encoder
	EncodingThread->RequestKeyFrame(StreamKey);

	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("Subsystem: Requested keyframe for stream %s"),
		*StreamKey.ToString());
}

void URealGazeboStreamingSubsystem::UpdatePoolCapacities()
{
	// Get current registered camera count (not active stream count)
	int32 RegisteredCameraCount = 0;
	int32 ActiveStreamCount = 0;

	{
		FScopeLock Lock(&PipelineMapMutex);
		RegisteredCameraCount = ActivePipelines.Num();

		// Count active streams for frame pool sizing
		for (const auto& Pair : ActivePipelines)
		{
			if (Pair.Value.IsValid() && Pair.Value->IsActive())
			{
				ActiveStreamCount++;
			}
		}
	}

	// Update scene capture pool based on registered cameras
	if (SceneCapturePool.IsValid())
	{
		SceneCapturePool->UpdateCapacity(RegisteredCameraCount);
	}

	// Update frame pool based on active streams (streams that are currently running)
	if (FramePool.IsValid())
	{
		FramePool->UpdateCapacity(ActiveStreamCount > 0 ? ActiveStreamCount : RegisteredCameraCount);
	}
}
