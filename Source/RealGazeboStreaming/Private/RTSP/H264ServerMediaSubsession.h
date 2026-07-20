// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"

// Include Live555 types
#include "RTSP/Live555Types.h"

// Forward declarations
class FH264StreamSource;

/**
 * FH264ServerMediaSubsession
 *
 * Live555 OnDemandServerMediaSubsession for H.264 streaming.
 * Creates FramedSource and RTPSink on-demand when clients connect.
 *
 * Integration flow:
 * 1. RTSP client connects and sends DESCRIBE
 * 2. Live555 calls sdpLines() for SDP generation
 * 3. Client sends SETUP, Live555 calls createNewStreamSource() and createNewRTPSink()
 * 4. Client sends PLAY, streaming begins
 *
 * Each client gets their own FramedSource instance, but they all
 * share the same FH264StreamSource NAL queue (thread-safe).
 */
class FH264ServerMediaSubsession : public OnDemandServerMediaSubsession
{
public:
	/**
	 * Create a new H264 server media subsession.
	 *
	 * @param env - Live555 usage environment
	 * @param StreamSource - H264 stream source (provides NAL units)
	 * @param reuseFirstSource - If true, reuse single source for all clients
	 * @return New subsession instance
	 */
	static FH264ServerMediaSubsession* createNew(UsageEnvironment& env,
	                                              FH264StreamSource* StreamSource,
	                                              Boolean reuseFirstSource = True);

protected:
	FH264ServerMediaSubsession(UsageEnvironment& env,
	                           FH264StreamSource* StreamSource,
	                           Boolean reuseFirstSource);

	virtual ~FH264ServerMediaSubsession();

	//----------------------------------------------------------
	// OnDemandServerMediaSubsession overrides
	//----------------------------------------------------------

	/**
	 * Create a new FramedSource for a client.
	 * Called when client sends SETUP.
	 *
	 * @param clientSessionId - Client session ID
	 * @param estBitrate - Estimated bitrate (output)
	 * @return FramedSource for this client
	 */
	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
	                                            unsigned& estBitrate) override;

	/**
	 * Create a new RTP sink for a client.
	 * Called after createNewStreamSource().
	 *
	 * @param rtpGroupsock - RTP groupsock for transmission
	 * @param rtpPayloadTypeIfDynamic - RTP payload type
	 * @param inputSource - The FramedSource from createNewStreamSource()
	 * @return RTPSink for this client
	 */
	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
	                                  unsigned char rtpPayloadTypeIfDynamic,
	                                  FramedSource* inputSource) override;

	/**
	 * Get auxiliary SDP line for SPS/PPS.
	 * Called during SDP generation (DESCRIBE response).
	 *
	 * @param rtpSink - The RTP sink (may be NULL during initial call)
	 * @param inputSource - The input source (may be NULL during initial call)
	 * @return SDP line with SPS/PPS parameters
	 */
	virtual char const* getAuxSDPLine(RTPSink* rtpSink,
	                                  FramedSource* inputSource) override;

private:
	/** H264 stream source (provides NAL units) */
	FH264StreamSource* H264Source;

	/** Cached aux SDP line */
	char* fAuxSDPLine;

	/** Done flag for async SDP line generation */
	char fDoneFlag;
};
