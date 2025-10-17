// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Pipeline/RealGazeboFramePool.h"
#include "Core/RealGazeboStreamingLogger.h"

FRealGazeboFramePool::FRealGazeboFramePool(int32 InMaxPoolSize)
	: MaxPoolSize(InMaxPoolSize)
	, ActiveEncodedFrames(0)
	, TotalEncodedFramesCreated(0)
	, TotalEncodedFramesReused(0)
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Frame pool initialized with size: %d"), MaxPoolSize);
}

FRealGazeboFramePool::~FRealGazeboFramePool()
{
	ClearPool();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Frame pool destroyed. Total encoded frames created: %llu, reused: %llu"),
		TotalEncodedFramesCreated.load(), TotalEncodedFramesReused.load());
}

TSharedPtr<FEncodedFrameData> FRealGazeboFramePool::AcquireEncodedFrame(uint64 FrameNumber, bool bIsKeyFrame)
{
	FScopeLock Lock(&EncodedPoolMutex);

	TSharedPtr<FEncodedFrameData> Frame;

	// Try to reuse from pool
	if (EncodedFramePool.Num() > 0)
	{
		Frame = EncodedFramePool.Pop();
		Frame->FrameNumber = FrameNumber;
		Frame->bIsKeyFrame = bIsKeyFrame;
		Frame->CaptureTimestamp = 0.0;
		Frame->EncodingTimestamp = 0.0;
		Frame->PresentationTimeUs = 0;
		TotalEncodedFramesReused.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		// Create new frame
		Frame = MakeShared<FEncodedFrameData>(FrameNumber, bIsKeyFrame);
		TotalEncodedFramesCreated.fetch_add(1, std::memory_order_relaxed);
	}

	ActiveEncodedFrames.fetch_add(1, std::memory_order_relaxed);
	return Frame;
}

void FRealGazeboFramePool::ReleaseEncodedFrame(TSharedPtr<FEncodedFrameData> Frame)
{
	if (!Frame.IsValid())
	{
		return;
	}

	FScopeLock Lock(&EncodedPoolMutex);

	// Only pool if below max size
	if (EncodedFramePool.Num() < MaxPoolSize)
	{
		Frame->Reset();
		EncodedFramePool.Add(Frame);
	}

	ActiveEncodedFrames.fetch_sub(1, std::memory_order_relaxed);
}

void FRealGazeboFramePool::ClearPool()
{
	FScopeLock Lock(&EncodedPoolMutex);
	EncodedFramePool.Empty();
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("Frame pool cleared"));
}

void FRealGazeboFramePool::ShrinkPool()
{
	FScopeLock Lock(&EncodedPoolMutex);
	if (EncodedFramePool.Num() > MaxPoolSize)
	{
		EncodedFramePool.SetNum(MaxPoolSize);
	}
}

void FRealGazeboFramePool::GetPoolStats(int32& OutEncodedPooled, int32& OutEncodedActive, float& OutEstimatedMemoryMB) const
{
	{
		FScopeLock Lock(&EncodedPoolMutex);
		OutEncodedPooled = EncodedFramePool.Num();
	}

	OutEncodedActive = ActiveEncodedFrames.load(std::memory_order_relaxed);
	OutEstimatedMemoryMB = GetEstimatedMemoryMB();
}

int32 FRealGazeboFramePool::GetTotalActiveFrames() const
{
	return ActiveEncodedFrames.load(std::memory_order_relaxed);
}

int32 FRealGazeboFramePool::GetTotalPooledFrames() const
{
	FScopeLock Lock(&EncodedPoolMutex);
	return EncodedFramePool.Num();
}

float FRealGazeboFramePool::GetEstimatedMemoryMB() const
{
	float TotalBytes = 0.0f;

	// Estimate encoded frame memory
	{
		FScopeLock Lock(&EncodedPoolMutex);
		for (const auto& Frame : EncodedFramePool)
		{
			TotalBytes += Frame->GetDataSize();
		}
	}

	return TotalBytes / (1024.0f * 1024.0f);
}

FString FRealGazeboFramePool::GetDebugString() const
{
	int32 EncodedPooled, EncodedActive;
	float MemoryMB;

	GetPoolStats(EncodedPooled, EncodedActive, MemoryMB);

	return FString::Printf(
		TEXT("FramePool: Active=%d, Pooled=%d, Memory=%.2fMB, Created=%llu, Reused=%llu"),
		EncodedActive,
		EncodedPooled,
		MemoryMB,
		TotalEncodedFramesCreated.load(),
		TotalEncodedFramesReused.load()
	);
}
