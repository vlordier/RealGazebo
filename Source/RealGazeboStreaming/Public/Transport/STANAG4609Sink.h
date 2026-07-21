// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Transport/ContinuousEncodedVideoSink.h"

class FSocket;
class FInternetAddr;

/**
 * Native MPEG-TS UDP sink for STANAG 4609-style motion imagery transport.
 * Video is never re-encoded: H.264 NAL units from the hardware encoder are
 * packetized into MPEG-TS alongside a MISB KLV metadata PID.
 */
class REALGAZEBOSTREAMING_API FSTANAG4609Sink final : public FContinuousEncodedVideoSink
{
public:
	FSTANAG4609Sink(FString InHost, int32 InPort);
	virtual ~FSTANAG4609Sink() override;

	virtual bool Start(FString& OutErrorMessage) override;
	virtual void Stop() override;
	virtual void PushEncodedVideo(
		const TArray<FEncodedNALUnit>& NALUnits,
		const FEncodedVideoMetadata& Metadata) override;
	virtual FString GetName() const override { return TEXT("STANAG4609"); }

private:
	void EmitProgramTables();
	void EmitVideo(const TArray<FEncodedNALUnit>& NALUnits, uint64 TimestampUs);
	void EmitMetadata(const FEncodedVideoMetadata& Metadata);
	void EmitPES(uint16 PID, uint8 StreamId, const TArray<uint8>& Payload, uint64 Pts90k, bool bDataAlignment);
	void EmitTSPayload(uint16 PID, const TArray<uint8>& Payload, bool bPayloadUnitStart);
	void SendPacket(const uint8* Packet188);
	TArray<uint8> BuildMISBLocalSet(const FEncodedVideoMetadata& Metadata) const;

	FString Host;
	int32 Port = 0;
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> Destination;
	uint8 Continuity[8192]{};
	uint64 PacketCounter = 0;
};
