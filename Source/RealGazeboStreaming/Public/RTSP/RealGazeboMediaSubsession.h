// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamingTypes.h"

// Live555 includes
#include "Boolean.hh"

// Live555 forward declarations
class ServerMediaSubsession;
class UsageEnvironment;

/**
 * H264 Live Video Subsession for Live555 RTSP Server
 *
 * ServerMediaSubsession that provides H.264 video stream to RTSP clients.
 * Manages RTP packetization and streaming of H.264 NAL units.
 *
 * Each stream gets its own subsession instance.
 */
class REALGAZEBOSTREAMING_API FRealGazeboMediaSubsession
{
public:
	/**
	 * Create new H264 live subsession for RTSP server
	 * @param env Live555 usage environment
	 * @param StreamKey Stream identifier
	 * @param reuseFirstSource Whether to reuse source for multiple clients
	 * @return ServerMediaSubsession instance
	 */
	static ServerMediaSubsession* CreateNew(UsageEnvironment& env,
	                                        const FStreamKey& StreamKey,
	                                        Boolean reuseFirstSource = False);

	/**
	 * Set SPS/PPS for specific stream (must be called before clients connect)
	 * @param StreamKey Target stream
	 * @param SPS Sequence Parameter Set
	 * @param PPS Picture Parameter Set
	 */
	static void SetSPSPPS(const FStreamKey& StreamKey, const TArray<uint8>& SPS, const TArray<uint8>& PPS);

	/**
	 * Get cached SPS for stream
	 * @param StreamKey Stream to query
	 * @return SPS data (copy for thread safety)
	 */
	static TArray<uint8> GetSPS(const FStreamKey& StreamKey);

	/**
	 * Get cached PPS for stream
	 * @param StreamKey Stream to query
	 * @return PPS data (copy for thread safety)
	 */
	static TArray<uint8> GetPPS(const FStreamKey& StreamKey);

	/**
	 * Get estimated bitrate for stream (for RTSP SDP)
	 * @param StreamKey Stream to query
	 * @return Estimated bitrate in kbps
	 */
	static unsigned GetEstimatedBitrate(const FStreamKey& StreamKey);

	/**
	 * Update cached bitrate for stream (called when config changes)
	 * @param StreamKey Target stream
	 * @param BitrateKbps New bitrate in kbps
	 */
	static void UpdateBitrate(const FStreamKey& StreamKey, int32 BitrateKbps);

private:
	// Per-stream SPS/PPS cache with bitrate
	struct FSPSPPSData
	{
		TArray<uint8> SPS;
		TArray<uint8> PPS;
		int32 BitrateKbps = 3250;  // Cached bitrate for SDP generation (default: 720p@30fps)
	};

	// Cached SPS/PPS for all streams
	static TMap<FStreamKey, FSPSPPSData> StreamSPSPPS;
	static FCriticalSection SPSPPSMutex;

	// Friend class to allow access from internal subsession
	friend class FH264LiveServerMediaSubsession;
};
