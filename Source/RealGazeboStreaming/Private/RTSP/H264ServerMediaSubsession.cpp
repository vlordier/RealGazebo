// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RTSP/H264ServerMediaSubsession.h"
#include "RTSP/H264StreamSource.h"

// Live555 includes
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "H264VideoStreamDiscreteFramer.hh"
#include "H264VideoRTPSink.hh"

//----------------------------------------------------------
// Construction
//----------------------------------------------------------

FH264ServerMediaSubsession* FH264ServerMediaSubsession::createNew(
	UsageEnvironment& env,
	FH264StreamSource* StreamSource,
	Boolean reuseFirstSource)
{
	return new FH264ServerMediaSubsession(env, StreamSource, reuseFirstSource);
}

FH264ServerMediaSubsession::FH264ServerMediaSubsession(
	UsageEnvironment& env,
	FH264StreamSource* StreamSource,
	Boolean reuseFirstSource)
	: OnDemandServerMediaSubsession(env, reuseFirstSource)
	, H264Source(StreamSource)
	, fAuxSDPLine(nullptr)
	, fDoneFlag(0)
{
	UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Created for stream %s"),
		*StreamSource->GetStreamID().ToString());
}

FH264ServerMediaSubsession::~FH264ServerMediaSubsession()
{
	delete[] fAuxSDPLine;
	UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Destroyed"));
}

//----------------------------------------------------------
// OnDemandServerMediaSubsession overrides
//----------------------------------------------------------

FramedSource* FH264ServerMediaSubsession::createNewStreamSource(
	unsigned clientSessionId,
	unsigned& estBitrate)
{
	// Estimated bitrate in kbps (for RTCP bandwidth calculation)
	// Conservative estimate - actual may vary
	estBitrate = 2000; // 2 Mbps

	UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Creating stream source for client %u"),
		clientSessionId);

	if (!H264Source)
	{
		UE_LOG(LogTemp, Error, TEXT("H264ServerMediaSubsession: H264Source is null"));
		return nullptr;
	}

	// Create the FramedSource from our H264StreamSource
	FramedSource* source = H264Source->CreateFramedSource();
	if (!source)
	{
		UE_LOG(LogTemp, Error, TEXT("H264ServerMediaSubsession: Failed to create FramedSource"));
		return nullptr;
	}

	// Wrap in H264VideoStreamDiscreteFramer for proper NAL unit parsing
	// This framer handles discrete NAL units (our encoder outputs complete NAL units)
	return H264VideoStreamDiscreteFramer::createNew(envir(), source);
}

RTPSink* FH264ServerMediaSubsession::createNewRTPSink(
	Groupsock* rtpGroupsock,
	unsigned char rtpPayloadTypeIfDynamic,
	FramedSource* inputSource)
{
	UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Creating RTP sink"));

	// Get SPS/PPS from our stream source for RTP packetization
	u_int8_t const* sps = nullptr;
	unsigned spsSize = 0;
	u_int8_t const* pps = nullptr;
	unsigned ppsSize = 0;

	if (H264Source && H264Source->HasSPSPPS())
	{
		const TArray<uint8>& SPSData = H264Source->GetSPS();
		const TArray<uint8>& PPSData = H264Source->GetPPS();

		if (SPSData.Num() > 0)
		{
			sps = SPSData.GetData();
			spsSize = SPSData.Num();
		}

		if (PPSData.Num() > 0)
		{
			pps = PPSData.GetData();
			ppsSize = PPSData.Num();
		}

		UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Using SPS (%u bytes), PPS (%u bytes)"),
			spsSize, ppsSize);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("H264ServerMediaSubsession: No SPS/PPS available yet"));
	}

	// Create H264 RTP sink
	// Note: SPS/PPS can be null - they'll be extracted from the stream
	return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
		sps, spsSize, pps, ppsSize);
}

char const* FH264ServerMediaSubsession::getAuxSDPLine(
	RTPSink* rtpSink,
	FramedSource* inputSource)
{
	// Return cached SDP line if available
	if (fAuxSDPLine != nullptr)
	{
		return fAuxSDPLine;
	}

	// Try to get SPS/PPS from our stream source
	if (H264Source && H264Source->HasSPSPPS())
	{
		// Generate SDP line from our stream source
		FString SDPLine = H264Source->GenerateSDPLines();
		if (!SDPLine.IsEmpty())
		{
			// Convert FString to char* for Live555
			FTCHARToUTF8 UTF8String(*SDPLine);
			const int32 Length = UTF8String.Length();

			fAuxSDPLine = new char[Length + 1];
			FMemory::Memcpy(fAuxSDPLine, UTF8String.Get(), Length);
			fAuxSDPLine[Length] = '\0';

			UE_LOG(LogTemp, Log, TEXT("H264ServerMediaSubsession: Generated SDP line"));
			return fAuxSDPLine;
		}
	}

	// If no SPS/PPS yet, let the RTPSink generate SDP from stream data
	// This is the normal Live555 approach - it extracts from first few frames
	if (rtpSink != nullptr)
	{
		const char* sdpLine = rtpSink->auxSDPLine();
		if (sdpLine != nullptr)
		{
			const size_t Length = strlen(sdpLine);
			fAuxSDPLine = new char[Length + 1];
			FMemory::Memcpy(fAuxSDPLine, sdpLine, Length);
			fAuxSDPLine[Length] = '\0';
			return fAuxSDPLine;
		}
	}

	// No SDP line available yet
	UE_LOG(LogTemp, Warning, TEXT("H264ServerMediaSubsession: No aux SDP line available yet"));
	return nullptr;
}
