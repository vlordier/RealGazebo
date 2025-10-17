// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Pipeline/RealGazeboStreamPipeline.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "Core/RealGazeboStreamingSettings.h"

FRealGazeboStreamPipeline::FRealGazeboStreamPipeline(const FStreamKey& InStreamKey,
                                                     const FRealGazeboStreamConfig& InConfig,
                                                     TSharedPtr<FRealGazeboFramePool> InFramePool)
	: StreamKey(InStreamKey)
	, Config(InConfig)
	, FramePool(InFramePool)
	, CurrentState(EStreamState::Stopped)
	, FrameSequence(0)
	, RTSPQueue(URealGazeboStreamingSettings::Get()->MaxQueueSize, FString::Printf(TEXT("%s_RTSP"), *StreamKey.ToString()))
	, LastStatsUpdateTime(0.0)
	, BackpressureStartTime(0.0)
	, AdaptiveQualityFactor(1.0f)
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream pipeline created (hardware-only): %s"), *StreamKey.ToString());
}

FRealGazeboStreamPipeline::~FRealGazeboStreamPipeline()
{
	Stop();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream pipeline destroyed: %s"), *StreamKey.ToString());
}

bool FRealGazeboStreamPipeline::Start()
{
	EStreamState ExpectedState = EStreamState::Stopped;
	if (!CurrentState.compare_exchange_strong(ExpectedState, EStreamState::Starting))
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("Cannot start stream %s - not in Stopped state"), *StreamKey.ToString());
		return false;
	}

	// Reset sequence counter
	FrameSequence.store(0, std::memory_order_relaxed);

	// Clear RTSP queue (hardware-only: encoding queue managed by encoding thread)
	RTSPQueue.Clear();

	// Reset statistics
	ResetStats();

	// Transition to streaming state
	CurrentState.store(EStreamState::Streaming, std::memory_order_release);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream started: %s (%dx%d @ %d fps, %d kbps)"),
		*StreamKey.ToString(),
		Config.Dimensions.X, Config.Dimensions.Y,
		Config.FPSValue, Config.BitrateKbps);

	return true;
}

void FRealGazeboStreamPipeline::Stop()
{
	EStreamState PreviousState = CurrentState.exchange(EStreamState::Stopped, std::memory_order_acq_rel);

	if (PreviousState == EStreamState::Stopped)
	{
		return; // Already stopped
	}

	// Clear RTSP queue (hardware-only: encoding queue managed by encoding thread)
	RTSPQueue.Clear();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream stopped: %s (Total frames: %llu, Dropped: %llu)"),
		*StreamKey.ToString(),
		Stats.TotalFramesCaptured,
		Stats.TotalFramesDropped);
}

void FRealGazeboStreamPipeline::Pause()
{
	EStreamState ExpectedState = EStreamState::Streaming;
	if (CurrentState.compare_exchange_strong(ExpectedState, EStreamState::Paused))
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream paused: %s"), *StreamKey.ToString());
	}
}

void FRealGazeboStreamPipeline::Resume()
{
	EStreamState ExpectedState = EStreamState::Paused;
	if (CurrentState.compare_exchange_strong(ExpectedState, EStreamState::Streaming))
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream resumed: %s"), *StreamKey.ToString());
	}
}

bool FRealGazeboStreamPipeline::IsActive() const
{
	EStreamState State = CurrentState.load(std::memory_order_acquire);
	return State == EStreamState::Streaming || State == EStreamState::Paused;
}

EStreamState FRealGazeboStreamPipeline::GetState() const
{
	return CurrentState.load(std::memory_order_acquire);
}

bool FRealGazeboStreamPipeline::SubmitEncodedFrame(TSharedPtr<FEncodedFrameData> Frame)
{
	if (!IsActive())
	{
		return false;
	}

	const bool bEnqueued = RTSPQueue.Enqueue(Frame, URealGazeboStreamingSettings::Get()->bAllowFrameDropping);

	if (bEnqueued)
	{
		FScopeLock Lock(&StatsMutex);
		Stats.TotalFramesEncoded++;

		if (Frame->bIsKeyFrame)
		{
			Stats.KeyFrameCount++;
		}

		// Update bitrate
		if (Config.FPSValue > 0)
		{
			Stats.CurrentBitrateMbps = Frame->GetBitrateMbps(static_cast<float>(Config.FPSValue));
		}
	}

	return bEnqueued;
}

bool FRealGazeboStreamPipeline::GetNextEncodedFrame(TSharedPtr<FEncodedFrameData>& OutFrame)
{
	if (!IsActive())
	{
		return false;
	}

	return RTSPQueue.Dequeue(OutFrame);
}

bool FRealGazeboStreamPipeline::UpdateConfig(const FRealGazeboStreamConfig& NewConfig)
{
	if (IsActive())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Cannot update config while stream is active: %s"), *StreamKey.ToString());
		return false;
	}

	FString ErrorMessage;
	if (!NewConfig.IsValid(ErrorMessage))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("Invalid config: %s"), *ErrorMessage);
		return false;
	}

	Config = NewConfig;
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Stream config updated: %s"), *StreamKey.ToString());
	return true;
}

FStreamingStats FRealGazeboStreamPipeline::GetStats() const
{
	FScopeLock Lock(&StatsMutex);

	// Update queue depths in stats (hardware-only: no conversion or encoding queues in pipeline)
	FStreamingStats CurrentStats = Stats;
	CurrentStats.RTSPQueueDepth = RTSPQueue.GetDepth();
	// Note: EncodingQueueDepth is managed by encoding thread per-stream encoder

	// Get pool stats if frame pool is valid (hardware-only: encoded frames only)
	if (FramePool.IsValid())
	{
		int32 EncodedPooled, EncodedActive;
		float MemoryMB;

		FramePool->GetPoolStats(EncodedPooled, EncodedActive, MemoryMB);

		CurrentStats.PooledFrameCount = EncodedPooled;
		CurrentStats.ActiveFrameCount = EncodedActive;
		CurrentStats.EstimatedMemoryMB = MemoryMB;
	}

	return CurrentStats;
}

void FRealGazeboStreamPipeline::ResetStats()
{
	FScopeLock Lock(&StatsMutex);
	Stats.Reset();
	RTSPQueue.ResetStats();
	LastStatsUpdateTime = FPlatformTime::Seconds();
}

void FRealGazeboStreamPipeline::UpdateStats()
{
	const double CurrentTime = FPlatformTime::Seconds();

	FScopeLock Lock(&StatsMutex);

	// Update timing metrics (these will be set by individual stages)

	// Update queue depth (hardware-only: RTSP queue only in pipeline)
	// Note: Encoding queue is managed by encoding thread per-stream encoder
	Stats.RTSPQueueDepth = RTSPQueue.GetDepth();

	// Update average bitrate (exponential moving average)
	const float Alpha = 0.1f; // Smoothing factor
	Stats.AverageBitrateMbps = Stats.AverageBitrateMbps * (1.0f - Alpha) + Stats.CurrentBitrateMbps * Alpha;

	LastStatsUpdateTime = CurrentTime;
}

bool FRealGazeboStreamPipeline::IsBackpressured() const
{
	// Check if RTSP queue is experiencing backpressure (>75% full)
	// Hardware-only: Encoding queue managed by encoding thread
	return RTSPQueue.IsBackpressured();
}

float FRealGazeboStreamPipeline::GetAdaptiveQualityFactor() const
{
	return AdaptiveQualityFactor.load(std::memory_order_relaxed);
}

bool FRealGazeboStreamPipeline::ShouldDropFrame(bool bIsKeyFrame) const
{
	// Never drop keyframes
	if (bIsKeyFrame)
	{
		return false;
	}

	// Don't drop if adaptive quality is disabled
	if (!Config.bEnableAdaptiveQuality)
	{
		return false;
	}

	// Check backpressure
	if (!IsBackpressured())
	{
		// Reset backpressure timer
		const_cast<FRealGazeboStreamPipeline*>(this)->BackpressureStartTime = 0.0;
		const_cast<FRealGazeboStreamPipeline*>(this)->AdaptiveQualityFactor.store(1.0f, std::memory_order_relaxed);
		return false;
	}

	// Backpressure detected
	const double CurrentTime = FPlatformTime::Seconds();

	if (BackpressureStartTime == 0.0)
	{
		// First detection of backpressure
		const_cast<FRealGazeboStreamPipeline*>(this)->BackpressureStartTime = CurrentTime;
		return false;
	}

	const double BackpressureDuration = CurrentTime - BackpressureStartTime;

	// After 1 second of backpressure, start dropping frames
	if (BackpressureDuration > 1.0)
	{
		// Drop every other frame when under backpressure
		return (FrameSequence.load(std::memory_order_relaxed) % 2) == 1;
	}

	return false;
}
