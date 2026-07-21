// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Transport/EncodedVideoSink.h"

class FH264StreamSource;

/**
 * Adapter exposing the existing per-stream Live555 H.264 source through the
 * transport-neutral encoded-video sink contract.
 *
 * Ownership stays with FStreamingPipeline; this adapter never owns the source.
 */
class FRTSPEncodedVideoSink final : public IEncodedVideoSink
{
public:
	explicit FRTSPEncodedVideoSink(FH264StreamSource* InSource)
		: Source(InSource)
	{
	}

	virtual bool Start(FString& OutError) override;
	virtual void Stop() override;
	virtual bool PushEncodedVideo(
		const TArray<FEncodedNALUnit>& NALUnits,
		const FEncodedVideoMetadata& Metadata,
		FString& OutError) override;
	virtual bool WantsFrames() const override;
	virtual FString GetName() const override { return TEXT("RTSP"); }

	void SetSource(FH264StreamSource* InSource) { Source = InSource; }

private:
	FH264StreamSource* Source = nullptr;
	std::atomic<bool> bStarted{false};
};
