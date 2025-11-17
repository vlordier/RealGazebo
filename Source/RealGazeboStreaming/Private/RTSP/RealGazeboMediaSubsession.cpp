// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/RealGazeboMediaSubsession.h"
#include "RTSP/RealGazeboH264Source.h"
#include "Core/RealGazeboStreamingTypes.h"

// Live555 includes
#include "H264VideoRTPSink.hh"
#include "H264VideoStreamFramer.hh"
#include "OnDemandServerMediaSubsession.hh"
#include "UsageEnvironment.hh"

// Static member initialization
TMap<FStreamKey, FRealGazeboMediaSubsession::FSPSPPSData> FRealGazeboMediaSubsession::StreamSPSPPS;
FCriticalSection FRealGazeboMediaSubsession::SPSPPSMutex;

/**
 * Internal Live555 ServerMediaSubsession implementation
 */
class FH264LiveServerMediaSubsession : public OnDemandServerMediaSubsession
{
public:
	static FH264LiveServerMediaSubsession* CreateNew(UsageEnvironment& env,
	                                                  const FStreamKey& InStreamKey,
	                                                  Boolean reuseFirstSource)
	{
		return new FH264LiveServerMediaSubsession(env, InStreamKey, reuseFirstSource);
	}

protected:
	FH264LiveServerMediaSubsession(UsageEnvironment& env,
	                                const FStreamKey& InStreamKey,
	                                Boolean reuseFirstSource)
		: OnDemandServerMediaSubsession(env, reuseFirstSource)
		, StreamKey(InStreamKey)
	{
	}

	virtual ~FH264LiveServerMediaSubsession()
	{
	}

	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
	                                            unsigned& estBitrate) override
	{
		// CRITICAL DEBUG (2025-11-17): Log which StreamKey is being used for source creation
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("MediaSubsession: createNewStreamSource CALLED - ClientSession %u | %s"),
			clientSessionId, *StreamKey.ToDebugString());

		// Get actual bitrate from stream configuration
		// NOTE: This is called when a client connects, so we need to query the current stream config
		estBitrate = FRealGazeboMediaSubsession::GetEstimatedBitrate(StreamKey);

		// Create H.264 source
		FramedSource* H264Source = FRealGazeboH264Source::CreateNew(envir(), StreamKey);
		if (!H264Source)
		{
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("MediaSubsession: FAILED to create H264Source for %s"),
				*StreamKey.ToDebugString());
			return nullptr;
		}

		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("MediaSubsession: Created H264Source successfully for %s"),
			*StreamKey.ToDebugString());

		// Wrap in H.264 framer
		// OutPacketBuffer::maxSize is set to 6MB in RTSPServer::Initialize() for RTP output
		return H264VideoStreamFramer::createNew(envir(), H264Source);
	}

	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
	                                  unsigned char rtpPayloadTypeIfDynamic,
	                                  FramedSource* inputSource) override
	{
		// Get SPS/PPS for this stream
		TArray<uint8> SPS = FRealGazeboMediaSubsession::GetSPS(StreamKey);
		TArray<uint8> PPS = FRealGazeboMediaSubsession::GetPPS(StreamKey);

		// Convert to uint8_t* for Live555
		uint8_t* SPSData = SPS.Num() > 0 ? SPS.GetData() : nullptr;
		uint8_t* PPSData = PPS.Num() > 0 ? PPS.GetData() : nullptr;
		unsigned SPSSize = SPS.Num();
		unsigned PPSSize = PPS.Num();

		// Create H.264 RTP sink
		// NOTE: OutPacketBuffer::maxSize is set to 6MB in RTSPServer::Initialize() to handle
		// large NAL units. H264VideoStreamFramer automatically fragments NAL units >MTU.
		// The "StreamParser::afterGettingBytes() warning" is informational and does NOT indicate
		// actual corruption - it's Live555's way of reporting fragmented packets.
		return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
		                                   SPSData, SPSSize, PPSData, PPSSize);
	}

private:
	FStreamKey StreamKey;
};

ServerMediaSubsession* FRealGazeboMediaSubsession::CreateNew(UsageEnvironment& env,
                                                              const FStreamKey& StreamKey,
                                                              Boolean reuseFirstSource)
{
	return FH264LiveServerMediaSubsession::CreateNew(env, StreamKey, reuseFirstSource);
}

void FRealGazeboMediaSubsession::SetSPSPPS(const FStreamKey& StreamKey, const TArray<uint8>& SPS, const TArray<uint8>& PPS)
{
	FScopeLock Lock(&SPSPPSMutex);

	FSPSPPSData& Data = StreamSPSPPS.FindOrAdd(StreamKey);
	Data.SPS = SPS;
	Data.PPS = PPS;
	// BitrateKbps retains previous value or default (3250) if first time

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("MediaSubsession: Set SPS/PPS for stream %s (SPS: %d bytes, PPS: %d bytes, Bitrate: %d kbps)"),
		*StreamKey.ToString(), SPS.Num(), PPS.Num(), Data.BitrateKbps);
}

TArray<uint8> FRealGazeboMediaSubsession::GetSPS(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&SPSPPSMutex);

	const FSPSPPSData* Data = StreamSPSPPS.Find(StreamKey);
	return Data ? Data->SPS : TArray<uint8>();
}

TArray<uint8> FRealGazeboMediaSubsession::GetPPS(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&SPSPPSMutex);

	const FSPSPPSData* Data = StreamSPSPPS.Find(StreamKey);
	return Data ? Data->PPS : TArray<uint8>();
}

unsigned FRealGazeboMediaSubsession::GetEstimatedBitrate(const FStreamKey& StreamKey)
{
	// Retrieve cached bitrate set alongside SPS/PPS
	// This is called when a client connects to populate the RTSP SDP
	FScopeLock Lock(&SPSPPSMutex);

	const FSPSPPSData* Data = StreamSPSPPS.Find(StreamKey);
	if (Data)
	{
		return static_cast<unsigned>(Data->BitrateKbps);
	}

	// Fallback to conservative estimate if stream not yet registered
	// 3250 kbps = 720p @ 30fps default from StreamingTypes.h
	return 3250;
}

void FRealGazeboMediaSubsession::UpdateBitrate(const FStreamKey& StreamKey, int32 BitrateKbps)
{
	FScopeLock Lock(&SPSPPSMutex);

	FSPSPPSData& Data = StreamSPSPPS.FindOrAdd(StreamKey);
	Data.BitrateKbps = BitrateKbps;

	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("MediaSubsession: Updated cached bitrate for stream %s to %d kbps"),
		*StreamKey.ToString(), BitrateKbps);
}
