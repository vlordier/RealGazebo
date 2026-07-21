// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "EncoderConfig.h"
#include "FramePool.h"
#include "FrameCapture.h"
#include "HardwareEncoderWrapper.h"
#include "EncodingThread.h"
#include "H264StreamSource.h"
#include "Transport/EncodedVideoFanout.h"

class USceneCaptureComponent2D;
class FRTSPServerWrapper;
class FRTSPEncodedVideoSink;

/** Per-camera GPU capture -> hardware encode -> multi-transport pipeline. */
class FStreamingPipeline
{
public:
	FStreamingPipeline(const FStreamIdentifier& InStreamID,
		USceneCaptureComponent2D* InSceneCapture,
		TSharedPtr<FRTSPServerWrapper> InRTSPServer);
	~FStreamingPipeline();

	bool Initialize(const FStreamConfig& Config, FString& OutErrorMessage);
	void Shutdown();
	bool Start(FString& OutRTSPURL);
	void Stop();
	bool CaptureFrame();
	bool UpdateConfiguration(const FStreamConfig& NewConfig, FString& OutErrorMessage);

	/** Register additional encoded-video consumers such as STANAG 4609 or WebRTC. */
	void AddEncodedVideoSink(const TSharedRef<IEncodedVideoSink>& Sink);
	void RemoveEncodedVideoSink(const FString& SinkName);

	bool IsInitialized() const { return bInitialized; }
	bool IsStreaming() const { return bStreaming; }
	const FStreamIdentifier& GetStreamID() const { return StreamID; }
	const FStreamConfig& GetConfig() const { return Config; }
	EEncoderType GetEncoderType() const;
	FString GetRTSPURL() const { return RTSPURL; }
	FStreamInfo GetStreamInfo() const;
	FString GetStatsString() const;

private:
	bool InitializeComponents(FString& OutErrorMessage);
	void CleanupComponents();
	void OnNALUnitsEncoded(const TArray<FEncodedNALUnit>& NALUnits);
	FEncodedVideoMetadata BuildMetadata(const TArray<FEncodedNALUnit>& NALUnits) const;

	FStreamIdentifier StreamID;
	FStreamConfig Config;
	FEncoderConfig EncoderConfig;
	TWeakObjectPtr<USceneCaptureComponent2D> SceneCapture;
	TSharedPtr<FRTSPServerWrapper> RTSPServer;
	FString RTSPURL;

	TSharedPtr<FFramePool> FramePool;
	TUniquePtr<FFrameCapture> FrameCapture;
	TSharedPtr<FHardwareEncoderWrapper> HardwareEncoder;
	TUniquePtr<FEncodingThread> EncodingThread;
	TUniquePtr<FH264StreamSource> H264Source;

	TUniquePtr<FEncodedVideoFanout> EncodedVideoFanout;
	TSharedPtr<FRTSPEncodedVideoSink> RTSPSink;

	std::atomic<bool> bInitialized{false};
	std::atomic<bool> bStreaming{false};
	std::atomic<bool> bShuttingDown{false};
	std::atomic<uint64> FrameCounter{0};
	std::atomic<uint64> TotalFramesCaptured{0};
	std::atomic<uint64> CaptureFailureCount{0};
	FDateTime StartTime;
	double LastCaptureTime = 0.0;
};
