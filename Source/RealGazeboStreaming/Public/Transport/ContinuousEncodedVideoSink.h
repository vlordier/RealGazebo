// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Transport/EncodedVideoSink.h"

/**
 * Minimal always-on sink base for recorder/STANAG pipelines.
 * It deliberately reports demand continuously while started, so rendering is
 * not coupled to interactive RTSP/WebRTC client presence.
 */
class FContinuousEncodedVideoSink : public IEncodedVideoSink
{
public:
	virtual bool Start(FString& OutError) override
	{
		bStarted.store(true);
		return true;
	}

	virtual void Stop() override
	{
		bStarted.store(false);
	}

	virtual bool WantsFrames() const override
	{
		return bStarted.load();
	}

protected:
	std::atomic<bool> bStarted{false};
};
