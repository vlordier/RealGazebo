// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RealGazeboH264Source.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "HAL/PlatformTime.h"

// Live555 includes
#include "FramedSource.hh"
#include "UsageEnvironment.hh"

#if PLATFORM_WINDOWS
// Windows doesn't have gettimeofday, use Unreal's cross-platform time
static void GetCurrentTimeval(struct timeval* tv)
{
	double CurrentTime = FPlatformTime::Seconds();
	tv->tv_sec = static_cast<long>(CurrentTime);
	tv->tv_usec = static_cast<long>((CurrentTime - tv->tv_sec) * 1000000.0);
}
#else
// Linux has gettimeofday
static void GetCurrentTimeval(struct timeval* tv)
{
	gettimeofday(tv, nullptr);
}
#endif

// Static member initialization
TMap<FStreamKey, TSharedPtr<FRealGazeboH264Source::FStreamNALQueue>> FRealGazeboH264Source::StreamNALQueues;
FCriticalSection FRealGazeboH264Source::NALQueueMutex;

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
		// PER-NAL-UNIT DELIVERY (2025-11-11): Deliver one NAL unit per call
		// This keeps all deliveries under Live555's buffer size limit (~13KB)
		// H264VideoStreamFramer then handles RTP fragmentation for transmission

		FScopeLock Lock(&FRealGazeboH264Source::NALQueueMutex);

		TSharedPtr<FStreamNALQueue>* QueuePtr = FRealGazeboH264Source::StreamNALQueues.Find(StreamKey);
		if (!QueuePtr || !QueuePtr->IsValid())
		{
			// Stream not registered, schedule retry
			nextTask() = envir().taskScheduler().scheduleDelayedTask(10000,  // 10ms
				(TaskFunc*)FramedSource::afterGetting, this);
			return;
		}

		FStreamNALQueue* Queue = QueuePtr->Get();

		// Try to dequeue next NAL unit
		TSharedPtr<FNALUnitData> CurrentNAL;
		if (!Queue->NALUnits.Dequeue(CurrentNAL))
		{
			// No NAL units available, schedule retry
			nextTask() = envir().taskScheduler().scheduleDelayedTask(10000,  // 10ms
				(TaskFunc*)FramedSource::afterGetting, this);
			return;
		}

		// Decrement queue size counter
		const int32 NewQueueSize = Queue->CurrentSize.fetch_sub(1, std::memory_order_release) - 1;

		// Validate NAL unit
		if (!CurrentNAL.IsValid() || CurrentNAL->Data.Num() == 0)
		{
			// Invalid NAL, schedule retry
			nextTask() = envir().taskScheduler().scheduleDelayedTask(10000,  // 10ms
				(TaskFunc*)FramedSource::afterGetting, this);
			return;
		}

		// DEBUG: Log NAL dequeue to verify Live555 is pulling frames
		const uint8 NALType = CurrentNAL->NALType;

		// Copy entire NAL unit to Live555 buffer
		// NOTE: NAL size is pre-validated at enqueue time (max 100KB) to prevent buffer overflow
		const int32 NALSize = CurrentNAL->Data.Num();
		FMemory::Memcpy(fTo, CurrentNAL->Data.GetData(), NALSize);
		fFrameSize = NALSize;
		fNumTruncatedBytes = 0;  // Complete NAL, no truncation

		// Set presentation timestamp (same for all NALs in a frame)
		const double TimestampSeconds = CurrentNAL->Timestamp;
		fPresentationTime.tv_sec = static_cast<long>(TimestampSeconds);
		fPresentationTime.tv_usec = static_cast<long>((TimestampSeconds - fPresentationTime.tv_sec) * 1000000.0);

		// DEBUG: Log timestamp and frame number to verify FPS
		static double LastTimestamp = 0.0;
		const double DeltaTime = (LastTimestamp > 0.0) ? (TimestampSeconds - LastTimestamp) : 0.0;
		const double FPS = (DeltaTime > 0.0) ? (1.0 / DeltaTime) : 0.0;

		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("H264Source: DEQUEUED NAL - Type %d | Size: %d bytes | Queue: %d | Frame %llu | TS: %.6fs | Delta: %.6fs | FPS: %.1f"),
			NALType, NALSize, NewQueueSize, CurrentNAL->FrameNumber, TimestampSeconds, DeltaTime, FPS);

		LastTimestamp = TimestampSeconds;

		// NAL is consumed, CurrentNAL TSharedPtr goes out of scope and releases

		// Trigger afterGetting to deliver NAL to Live555
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
	// CRITICAL DEBUG (2025-11-17): Log entry to verify function is called
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("H264Source: PushFrameData ENTRY - %s | Size: %d bytes | Keyframe: %s"),
		*StreamKey.ToDebugString(), FrameData.Num(), bIsKeyFrame ? TEXT("YES") : TEXT("NO"));

	FScopeLock Lock(&NALQueueMutex);

	// Get or create queue (using TSharedPtr because TQueue is not copyable)
	TSharedPtr<FStreamNALQueue>* QueuePtr = StreamNALQueues.Find(StreamKey);
	if (!QueuePtr)
	{
		QueuePtr = &StreamNALQueues.Add(StreamKey, MakeShared<FStreamNALQueue>());

		// DEBUG: Log new NAL queue creation to verify stream isolation
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("H264Source: Created new NAL queue - %s (Total queues: %d)"),
			*StreamKey.ToDebugString(), StreamNALQueues.Num());
	}
	FStreamNALQueue& Queue = **QueuePtr;

	// PER-NAL-UNIT PARSING (2025-11-11 v3): Parse frame into individual NAL units
	// Each NAL unit is queued separately and delivered one at a time to Live555
	// This keeps all deliveries under Live555's buffer size limit (~13KB)
	//
	// NVENC Annex-B format: Uses BOTH 3-byte (00 00 01) and 4-byte (00 00 00 01) start codes
	// NAL unit types: SPS=7, PPS=8, I-slice=5, P-slice=1, SEI=6, AUD=9

	const uint8* Data = FrameData.GetData();
	const int32 Size = FrameData.Num();
	int32 Offset = 0;
	int32 NALsEnqueued = 0;
	int32 SPSPPSFiltered = 0;
	const uint64 FrameNumber = Queue.FrameCounter++;

	while (Offset < Size)
	{
		// Look for Annex-B start code: 00 00 01 or 00 00 00 01
		int32 StartCodeSize = 0;
		if (Offset + 2 < Size && Data[Offset] == 0x00 && Data[Offset + 1] == 0x00 && Data[Offset + 2] == 0x01)
		{
			StartCodeSize = 3;
		}
		else if (Offset + 3 < Size && Data[Offset] == 0x00 && Data[Offset + 1] == 0x00 &&
		         Data[Offset + 2] == 0x00 && Data[Offset + 3] == 0x01)
		{
			StartCodeSize = 4;
		}

		if (StartCodeSize > 0)
		{
			const int32 StartCodeOffset = Offset;
			Offset += StartCodeSize;

			if (Offset >= Size)
				break;

			const uint8 NALHeaderByte = Data[Offset];
			const uint8 NALType = NALHeaderByte & 0x1F;

			// Find next start code to determine NAL unit size
			int32 NALEnd = Offset;
			bool bFoundNextStartCode = false;
			while (NALEnd + 2 < Size)
			{
				// Check for 3-byte start code
				if (Data[NALEnd] == 0x00 && Data[NALEnd + 1] == 0x00 && Data[NALEnd + 2] == 0x01)
				{
					bFoundNextStartCode = true;
					break;
				}
				// Check for 4-byte start code
				if (NALEnd + 3 < Size && Data[NALEnd] == 0x00 && Data[NALEnd + 1] == 0x00 &&
				    Data[NALEnd + 2] == 0x00 && Data[NALEnd + 3] == 0x01)
				{
					bFoundNextStartCode = true;
					break;
				}
				NALEnd++;
			}

			// CRITICAL: If no next start code found, NAL extends to end of buffer
			if (!bFoundNextStartCode)
			{
				NALEnd = Size;
			}

			const int32 NALUnitSize = NALEnd - StartCodeOffset;

			// CRITICAL DEBUG (2025-11-17): Log ALL NAL types to diagnose "0 NAL units" issue
			// NAL types: SPS=7, PPS=8, I-slice=5, P-slice=1, SEI=6, AUD=9
			if (bIsKeyFrame && NALsEnqueued == 0)  // Log first NAL of each keyframe
			{
				UE_LOG(LogRealGazeboStreaming, Warning,
					TEXT("H264Source: DEBUG NAL - Frame %llu | Type %d | Size %d bytes | Keyframe: %s"),
					FrameNumber, NALType, NALUnitSize, bIsKeyFrame ? TEXT("YES") : TEXT("NO"));
			}

			// CRITICAL FIX (2025-11-17): Do NOT filter SPS/PPS from frames
			// Problem: Filtering SPS/PPS causes decoder corruption when clients join mid-stream
			// Root cause: H264VideoRTPSink::createNew() is called BEFORE first keyframe arrives,
			//            resulting in NULL SPS/PPS being passed to Live555
			// Solution: Keep SPS/PPS inline in keyframes - Live555 handles them correctly
			// Benefit: Clients get SPS/PPS immediately on next keyframe (robust mid-stream join)
			const bool bIsSPS = (NALType == 7);
			const bool bIsPPS = (NALType == 8);

			// Log SPS/PPS detection for debugging
			if (bIsSPS || bIsPPS)
			{
				SPSPPSFiltered++;  // Count for statistics, but DO NOT filter
				UE_LOG(LogRealGazeboStreaming, Verbose,
					TEXT("H264Source: Detected %s NAL (type %d, %d bytes) in frame %llu - KEEPING INLINE"),
					bIsSPS ? TEXT("SPS") : TEXT("PPS"), NALType, NALUnitSize, FrameNumber);
			}

			// Enqueue ALL NAL types (including SPS/PPS)
			{
				// CRITICAL FIX (2025-11-17): Increased NAL size limit to match OutPacketBuffer::maxSize
				// RealGazeboRTSPServer.cpp sets OutPacketBuffer::maxSize to 6MB for complex scenes
				// NVENC can produce 100-200KB keyframes in complex scenes (lc62 with detailed environment)
				// Live555's H264VideoStreamFramer automatically fragments large NAL units for RTP transmission
				// Safe limit: 512KB (well below 6MB buffer, handles worst-case keyframes)
				static constexpr int32 MAX_NAL_SIZE = 524288;  // 512KB - matches Live555 buffer capacity

				if (NALUnitSize > MAX_NAL_SIZE)
				{
					UE_LOG(LogRealGazeboStreaming, Warning,
						TEXT("H264Source: NAL unit too large (%d bytes > %d limit, type %d) for stream %s - DROPPING (reduce bitrate or resolution)"),
						NALUnitSize, MAX_NAL_SIZE, NALType, *StreamKey.ToString());
					// Don't enqueue - this NAL would corrupt the stream
				}
				else
				{
					// CRITICAL FIX (Bug #6): Atomically check and increment queue size to prevent race
					// Previous code had TOCTOU race: check size -> another thread enqueues -> overflow
					// New code: Increment first, then check if we exceeded limit
					const int32 NewSize = Queue.CurrentSize.fetch_add(1, std::memory_order_acq_rel) + 1;

					if (NewSize > Queue.MaxQueueSize)
					{
						// Exceeded limit - drop this NAL and decrement counter
						Queue.CurrentSize.fetch_sub(1, std::memory_order_release);
						Queue.NALsDropped++;

						// Log warning on first drop per frame
						if (Queue.NALsDropped % 100 == 1)
						{
							UE_LOG(LogRealGazeboStreaming, Warning,
								TEXT("H264Source: NAL queue full (%d/%d) for stream %s - dropping NAL (type %d, %d bytes)"),
								NewSize - 1, Queue.MaxQueueSize, *StreamKey.ToString(), NALType, NALUnitSize);
						}
					}
					else
					{
						// IMPORTANT: H264VideoStreamFramer expects Annex-B byte stream with start codes.
						// We therefore keep the original start code prefix when enqueueing each NAL unit.
						// Format: [start code (3-4 bytes)] [NAL header (1 byte)] [NAL payload]
						const int32 NALCopyOffset = StartCodeOffset;
						const int32 NALCopySize = NALEnd - StartCodeOffset;

						// CRITICAL DEBUG (2025-11-17): Log before enqueue to diagnose "0 NAL units" bug
						UE_LOG(LogRealGazeboStreaming, Warning,
							TEXT("H264Source: About to enqueue NAL - Type %d | Size: %d bytes (including start code) | Queue size: %d/%d | Frame %llu"),
							NALType, NALCopySize, NewSize, Queue.MaxQueueSize, FrameNumber);

						// Within limit - create and enqueue NAL unit (including start code)
						TArray<uint8> NALData;
						NALData.Append(&Data[NALCopyOffset], NALCopySize);

						TSharedPtr<FNALUnitData> NALUnit = MakeShared<FNALUnitData>(
							NALData, Timestamp, bIsKeyFrame, NALType, FrameNumber);

						Queue.NALUnits.Enqueue(NALUnit);
						NALsEnqueued++;

						// CRITICAL DEBUG (2025-11-17): Confirm enqueue succeeded
						UE_LOG(LogRealGazeboStreaming, Warning,
							TEXT("H264Source: Enqueued NAL successfully - NALsEnqueued now: %d | Frame %llu"),
							NALsEnqueued, FrameNumber);
					}
				}
			}

			Offset = NALEnd;
		}
		else
		{
			Offset++;
		}
	}

	// Log summary (only for keyframes to reduce spam)
	if (bIsKeyFrame)
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("H264Source: Parsed keyframe %llu for %s: %d bytes -> %d NAL units (%d SPS/PPS included)"),
			FrameNumber, *StreamKey.ToString(), FrameData.Num(), NALsEnqueued, SPSPPSFiltered);

		// DEBUG: Log detailed StreamKey info for isolation verification
		UE_LOG(LogRealGazeboStreaming, Verbose,
			TEXT("H264Source: PushFrameData - %s | Frame %llu | NALs enqueued: %d"),
			*StreamKey.ToDebugString(), FrameNumber, NALsEnqueued);
	}

	// Warn if no NAL units were enqueued
	if (NALsEnqueued == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("H264Source: Frame %llu produced 0 NAL units for stream %s (original: %d bytes, keyframe: %s)"),
			FrameNumber, *StreamKey.ToString(), FrameData.Num(), bIsKeyFrame ? TEXT("YES") : TEXT("NO"));
	}
}


void FRealGazeboH264Source::SetMaxQueueSize(const FStreamKey& StreamKey, int32 MaxQueueSize)
{
	FScopeLock Lock(&NALQueueMutex);

	TSharedPtr<FStreamNALQueue>* QueuePtr = StreamNALQueues.Find(StreamKey);
	if (QueuePtr && QueuePtr->IsValid())
	{
		FStreamNALQueue* Queue = QueuePtr->Get();
		Queue->MaxQueueSize = MaxQueueSize;

		UE_LOG(LogRealGazeboStreaming, Verbose,
			TEXT("H264Source: Set max NAL queue size for stream %s to %d"),
			*StreamKey.ToString(), MaxQueueSize);
	}
}
