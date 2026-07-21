// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Transport/RTSPEncodedVideoSink.h"
#include "RTSP/H264StreamSource.h"

bool FRTSPEncodedVideoSink::Start(FString& OutError)
{
	if (!Source)
	{
		OutError = TEXT("RTSP sink has no H264 stream source");
		return false;
	}
	bStarted.store(true);
	return true;
}

void FRTSPEncodedVideoSink::Stop()
{
	bStarted.store(false);
}

bool FRTSPEncodedVideoSink::PushEncodedVideo(
	const TArray<FEncodedNALUnit>& NALUnits,
	const FEncodedVideoMetadata& Metadata,
	FString& OutError)
{
	(void)Metadata;
	if (!bStarted.load() || !Source || Source->IsShuttingDown())
	{
		OutError = TEXT("RTSP sink is not available");
		return false;
	}

	Source->PushNALUnits(NALUnits);
	return true;
}

bool FRTSPEncodedVideoSink::WantsFrames() const
{
	return bStarted.load() && Source && !Source->IsShuttingDown() && Source->HasActiveClient(2.0);
}
