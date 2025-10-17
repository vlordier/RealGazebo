// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RealGazeboH264Source.h"
#include "Core/RealGazeboStreamingLogger.h"

// Live555 includes
#include "FramedSource.hh"
#include "UsageEnvironment.hh"

// Static member initialization
TMap<FStreamKey, FRealGazeboH264Source::FFrameBuffer> FRealGazeboH264Source::StreamFrameBuffers;
FCriticalSection FRealGazeboH264Source::FrameBufferMutex;

/**
 * Internal Live555 FramedSource implementation
 */
class FRealGazeboH264Source::FH264LiveFramedSource : public FramedSource
{
public:
	static FH264LiveFramedSource* CreateNew(UsageEnvironment& env, const FStreamKey& InStreamKey)
	{
		return new FH264LiveFramedSource(env, InStreamKey);
	}

protected:
	FH264LiveFramedSource(UsageEnvironment& env, const FStreamKey& InStreamKey)
		: FramedSource(env)
		, StreamKey(InStreamKey)
	{
	}

	virtual ~FH264LiveFramedSource()
	{
	}

	virtual void doGetNextFrame() override
	{
		// Check if frame data is available
		if (!FRealGazeboH264Source::HasFrameData(StreamKey))
		{
			// No data available, schedule retry
			nextTask() = envir().taskScheduler().scheduleDelayedTask(10000,  // 10ms
				(TaskFunc*)FramedSource::afterGetting, this);
			return;
		}

		// Get frame data
		FScopeLock Lock(&FRealGazeboH264Source::FrameBufferMutex);

		FFrameBuffer* FrameBuffer = FRealGazeboH264Source::StreamFrameBuffers.Find(StreamKey);
		if (!FrameBuffer || !FrameBuffer->bHasNewFrame)
		{
			// No frame available
			nextTask() = envir().taskScheduler().scheduleDelayedTask(10000,  // 10ms
				(TaskFunc*)FramedSource::afterGetting, this);
			return;
		}

		// Copy frame data to output buffer
		if (FrameBuffer->Data.Num() > (int32)fMaxSize)
		{
			fFrameSize = fMaxSize;
			fNumTruncatedBytes = FrameBuffer->Data.Num() - fMaxSize;
		}
		else
		{
			fFrameSize = FrameBuffer->Data.Num();
			fNumTruncatedBytes = 0;
		}

		FMemory::Memcpy(fTo, FrameBuffer->Data.GetData(), fFrameSize);

		// Set presentation time (microseconds)
		gettimeofday(&fPresentationTime, nullptr);

		// Mark frame as consumed
		FrameBuffer->bHasNewFrame = false;

		// Trigger afterGetting
		FramedSource::afterGetting(this);
	}

private:
	FStreamKey StreamKey;
};

FramedSource* FRealGazeboH264Source::CreateNew(UsageEnvironment& env, const FStreamKey& StreamKey)
{
	return FH264LiveFramedSource::CreateNew(env, StreamKey);
}

void FRealGazeboH264Source::PushFrameData(const FStreamKey& StreamKey, const TArray<uint8>& FrameData,
                                          double Timestamp, bool bIsKeyFrame)
{
	FScopeLock Lock(&FrameBufferMutex);

	FFrameBuffer& FrameBuffer = StreamFrameBuffers.FindOrAdd(StreamKey);
	FrameBuffer.Data = FrameData;
	FrameBuffer.Timestamp = Timestamp;
	FrameBuffer.bIsKeyFrame = bIsKeyFrame;
	FrameBuffer.bHasNewFrame = true;
}

bool FRealGazeboH264Source::HasFrameData(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&FrameBufferMutex);

	const FFrameBuffer* FrameBuffer = StreamFrameBuffers.Find(StreamKey);
	return FrameBuffer && FrameBuffer->bHasNewFrame;
}

void FRealGazeboH264Source::ClearAllFrameBuffers()
{
	FScopeLock Lock(&FrameBufferMutex);
	StreamFrameBuffers.Empty();
}
