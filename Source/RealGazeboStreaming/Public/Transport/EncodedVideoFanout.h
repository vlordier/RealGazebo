// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Transport/EncodedVideoSink.h"

class REALGAZEBOSTREAMING_API FEncodedVideoFanout
{
public:
	void AddSink(const TSharedRef<IEncodedVideoSink>& Sink);
	void RemoveSink(const FString& SinkName);
	void Clear();
	bool StartAll(FString& OutErrorMessage);
	void StopAll();
	bool WantsFrames() const;
	void Push(const TArray<FEncodedNALUnit>& NALUnits, const FEncodedVideoMetadata& Metadata);
	int32 NumSinks() const;

private:
	mutable FCriticalSection Mutex;
	TArray<TSharedRef<IEncodedVideoSink>> Sinks;
};
