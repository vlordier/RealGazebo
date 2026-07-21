// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Transport/EncodedVideoSink.h"

/**
 * Fan-out for one encoded camera stream.
 *
 * A frame is encoded exactly once. The resulting NAL units are then delivered
 * to zero or more transport sinks (RTSP, WebRTC/browser, STANAG 4609, recorder).
 * Sink failures are isolated: one failed transport must not stop the others.
 */
class REALGAZEBOSTREAMING_API FEncodedVideoFanout
{
public:
	void AddSink(const TSharedRef<IEncodedVideoSink>& Sink);
	void RemoveSink(const FString& SinkName);
	void Clear();

	bool StartAll(FString& OutErrorMessage);
	void StopAll();

	/** True when at least one registered sink wants encoded frames. */
	bool WantsFrames() const;

	/** Deliver one encoded access unit/NAL batch to all currently interested sinks. */
	void Push(const TArray<FEncodedNALUnit>& NALUnits, const FEncodedVideoMetadata& Metadata);

	int32 NumSinks() const;

private:
	mutable FCriticalSection Mutex;
	TArray<TSharedRef<IEncodedVideoSink>> Sinks;
};
