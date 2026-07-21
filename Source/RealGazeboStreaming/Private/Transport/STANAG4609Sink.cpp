// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Transport/STANAG4609Sink.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/DateTime.h"

namespace
{
	constexpr uint16 ProgramNumber = 1;
	constexpr uint16 PmtPid = 0x100;
	constexpr uint16 VideoPid = 0x101;
	constexpr uint16 KlvPid = 0x102;

	uint32 MpegCrc32(const uint8* Data, int32 Length)
	{
		uint32 Crc = 0xFFFFFFFFu;
		for (int32 I = 0; I < Length; ++I)
		{
			Crc ^= static_cast<uint32>(Data[I]) << 24;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				Crc = (Crc & 0x80000000u) ? (Crc << 1) ^ 0x04C11DB7u : (Crc << 1);
			}
		}
		return Crc;
	}

	void AppendU16(TArray<uint8>& Out, uint16 V)
	{
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V));
	}

	void AppendU32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V >> 24));
		Out.Add(static_cast<uint8>(V >> 16));
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V));
	}

	void AppendU64(TArray<uint8>& Out, uint64 V)
	{
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Out.Add(static_cast<uint8>(V >> Shift));
		}
	}

	void AppendPts(TArray<uint8>& Out, uint64 Pts)
	{
		Pts &= ((1ULL << 33) - 1);
		Out.Add(static_cast<uint8>(0x20 | (((Pts >> 30) & 0x07) << 1) | 1));
		Out.Add(static_cast<uint8>(Pts >> 22));
		Out.Add(static_cast<uint8>((((Pts >> 15) & 0x7F) << 1) | 1));
		Out.Add(static_cast<uint8>(Pts >> 7));
		Out.Add(static_cast<uint8>(((Pts & 0x7F) << 1) | 1));
	}

	void AppendBerLength(TArray<uint8>& Out, int32 Length)
	{
		if (Length < 0x80)
		{
			Out.Add(static_cast<uint8>(Length));
		}
		else if (Length <= 0xFF)
		{
			Out.Add(0x81);
			Out.Add(static_cast<uint8>(Length));
		}
		else
		{
			Out.Add(0x82);
			AppendU16(Out, static_cast<uint16>(Length));
		}
	}

	void AddLocalTag(TArray<uint8>& Out, uint8 Tag, const TArray<uint8>& Value)
	{
		Out.Add(Tag);
		AppendBerLength(Out, Value.Num());
		Out.Append(Value);
	}

	uint64 UnixEpochMicroseconds()
	{
		static const FDateTime UnixEpoch(1970, 1, 1);
		return static_cast<uint64>((FDateTime::UtcNow() - UnixEpoch).GetTicks() / 10);
	}
}

FSTANAG4609Sink::FSTANAG4609Sink(FString InHost, int32 InPort)
	: Host(MoveTemp(InHost)), Port(InPort)
{
	FMemory::Memzero(Continuity, sizeof(Continuity));
}

FSTANAG4609Sink::~FSTANAG4609Sink()
{
	Stop();
}

bool FSTANAG4609Sink::Start(FString& OutErrorMessage)
{
	if (bStarted.load())
	{
		return true;
	}
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem)
	{
		OutErrorMessage = TEXT("Socket subsystem unavailable");
		return false;
	}
	Socket = Subsystem->CreateSocket(NAME_DGram, TEXT("RealGazebo-STANAG4609"), false);
	if (!Socket)
	{
		OutErrorMessage = TEXT("Failed to create STANAG UDP socket");
		return false;
	}
	Destination = Subsystem->CreateInternetAddr();
	bool bValid = false;
	Destination->SetIp(*Host, bValid);
	Destination->SetPort(Port);
	if (!bValid)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid STANAG destination: %s"), *Host);
		Subsystem->DestroySocket(Socket);
		Socket = nullptr;
		Destination.Reset();
		return false;
	}
	bStarted.store(true);
	EmitProgramTables();
	return true;
}

void FSTANAG4609Sink::Stop()
{
	bStarted.store(false);
	if (Socket)
	{
		if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Subsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	Destination.Reset();
}

void FSTANAG4609Sink::PushEncodedVideo(
	const TArray<FEncodedNALUnit>& NALUnits,
	const FEncodedVideoMetadata& Metadata)
{
	if (!bStarted.load() || !Socket || !Destination.IsValid())
	{
		return;
	}
	if ((PacketCounter % 400) == 0)
	{
		EmitProgramTables();
	}
	EmitMetadata(Metadata);
	EmitVideo(NALUnits, Metadata.TimestampUs);
}

void FSTANAG4609Sink::EmitProgramTables()
{
	TArray<uint8> Pat;
	Pat.Add(0x00);
	Pat.Add(0xB0); Pat.Add(0x0D);
	AppendU16(Pat, 1);
	Pat.Add(0xC1); Pat.Add(0x00); Pat.Add(0x00);
	AppendU16(Pat, ProgramNumber);
	Pat.Add(static_cast<uint8>(0xE0 | ((PmtPid >> 8) & 0x1F)));
	Pat.Add(static_cast<uint8>(PmtPid));
	AppendU32(Pat, MpegCrc32(Pat.GetData(), Pat.Num()));
	TArray<uint8> PatPayload;
	PatPayload.Add(0x00);
	PatPayload.Append(Pat);
	EmitTSPayload(0x0000, PatPayload, true);

	TArray<uint8> Pmt;
	Pmt.Add(0x02);
	Pmt.Add(0xB0); Pmt.Add(0x1D);
	AppendU16(Pmt, ProgramNumber);
	Pmt.Add(0xC1); Pmt.Add(0x00); Pmt.Add(0x00);
	Pmt.Add(static_cast<uint8>(0xE0 | ((VideoPid >> 8) & 0x1F)));
	Pmt.Add(static_cast<uint8>(VideoPid));
	Pmt.Add(0xF0); Pmt.Add(0x00);
	Pmt.Add(0x1B);
	Pmt.Add(static_cast<uint8>(0xE0 | ((VideoPid >> 8) & 0x1F)));
	Pmt.Add(static_cast<uint8>(VideoPid));
	Pmt.Add(0xF0); Pmt.Add(0x00);
	Pmt.Add(0x06);
	Pmt.Add(static_cast<uint8>(0xE0 | ((KlvPid >> 8) & 0x1F)));
	Pmt.Add(static_cast<uint8>(KlvPid));
	Pmt.Add(0xF0); Pmt.Add(0x06);
	Pmt.Add(0x05); Pmt.Add(0x04);
	Pmt.Add(static_cast<uint8>('K')); Pmt.Add(static_cast<uint8>('L'));
	Pmt.Add(static_cast<uint8>('V')); Pmt.Add(static_cast<uint8>('A'));
	AppendU32(Pmt, MpegCrc32(Pmt.GetData(), Pmt.Num()));
	TArray<uint8> PmtPayload;
	PmtPayload.Add(0x00);
	PmtPayload.Append(Pmt);
	EmitTSPayload(PmtPid, PmtPayload, true);
}

void FSTANAG4609Sink::EmitVideo(const TArray<FEncodedNALUnit>& NALUnits, uint64 TimestampUs)
{
	TArray<uint8> AnnexB;
	for (const FEncodedNALUnit& Nal : NALUnits)
	{
		AnnexB.Append(Nal.Data);
	}
	if (!AnnexB.IsEmpty())
	{
		EmitPES(VideoPid, 0xE0, AnnexB, (TimestampUs * 90ULL) / 1000ULL, true);
	}
}

void FSTANAG4609Sink::EmitMetadata(const FEncodedVideoMetadata& Metadata)
{
	TArray<uint8> Klv = BuildMISBLocalSet(Metadata);
	if (!Klv.IsEmpty())
	{
		EmitPES(KlvPid, 0xBD, Klv, (Metadata.TimestampUs * 90ULL) / 1000ULL, true);
	}
}

void FSTANAG4609Sink::EmitPES(
	uint16 PID, uint8 StreamId, const TArray<uint8>& Payload, uint64 Pts90k, bool bDataAlignment)
{
	TArray<uint8> Pes;
	Pes.Add(0x00); Pes.Add(0x00); Pes.Add(0x01); Pes.Add(StreamId);
	const int32 PesPayloadLength = 3 + 5 + Payload.Num();
	const uint16 DeclaredLength = StreamId == 0xE0 || PesPayloadLength > 0xFFFF
		? 0 : static_cast<uint16>(PesPayloadLength);
	AppendU16(Pes, DeclaredLength);
	Pes.Add(static_cast<uint8>(0x80 | (bDataAlignment ? 0x04 : 0x00)));
	Pes.Add(0x80);
	Pes.Add(0x05);
	AppendPts(Pes, Pts90k);
	Pes.Append(Payload);
	EmitTSPayload(PID, Pes, true);
}

void FSTANAG4609Sink::EmitTSPayload(uint16 PID, const TArray<uint8>& Payload, bool bPayloadUnitStart)
{
	int32 Offset = 0;
	bool bFirst = true;
	while (Offset < Payload.Num())
	{
		uint8 Packet[188];
		FMemory::Memset(Packet, 0xFF, sizeof(Packet));
		Packet[0] = 0x47;
		Packet[1] = static_cast<uint8>(((bFirst && bPayloadUnitStart) ? 0x40 : 0x00) | ((PID >> 8) & 0x1F));
		Packet[2] = static_cast<uint8>(PID);
		const int32 Remaining = Payload.Num() - Offset;
		const int32 CopyBytes = FMath::Min(Remaining, 184);
		const bool bNeedsStuffing = CopyBytes < 184;
		int32 PayloadOffset = 4;
		if (bNeedsStuffing)
		{
			Packet[3] = static_cast<uint8>(0x30 | (Continuity[PID]++ & 0x0F));
			const int32 AdaptationLength = 183 - CopyBytes;
			Packet[4] = static_cast<uint8>(AdaptationLength);
			if (AdaptationLength > 0)
			{
				Packet[5] = 0x00;
			}
			PayloadOffset = 5 + AdaptationLength;
		}
		else
		{
			Packet[3] = static_cast<uint8>(0x10 | (Continuity[PID]++ & 0x0F));
		}
		FMemory::Memcpy(Packet + PayloadOffset, Payload.GetData() + Offset, CopyBytes);
		Offset += CopyBytes;
		SendPacket(Packet);
		bFirst = false;
	}
}

void FSTANAG4609Sink::SendPacket(const uint8* Packet188)
{
	if (!Socket || !Destination.IsValid())
	{
		return;
	}
	int32 Sent = 0;
	Socket->SendTo(Packet188, 188, Sent, *Destination);
	++PacketCounter;
}

TArray<uint8> FSTANAG4609Sink::BuildMISBLocalSet(const FEncodedVideoMetadata& Metadata) const
{
	TArray<uint8> Local;
	{
		TArray<uint8> V;
		AppendU64(V, UnixEpochMicroseconds());
		AddLocalTag(Local, 2, V);
	}
	if (Metadata.bHasPlatformAttitude)
	{
		const double Heading = FMath::Fmod(Metadata.PlatformHeadingDeg + 360.0, 360.0);
		const uint16 HeadingRaw = static_cast<uint16>(FMath::RoundToInt(Heading * 65535.0 / 360.0));
		TArray<uint8> H; AppendU16(H, HeadingRaw); AddLocalTag(Local, 5, H);
		const int16 PitchRaw = static_cast<int16>(FMath::Clamp(
			FMath::RoundToInt(Metadata.PlatformPitchDeg * 32767.0 / 20.0), -32767, 32767));
		TArray<uint8> P; AppendU16(P, static_cast<uint16>(PitchRaw)); AddLocalTag(Local, 6, P);
		const int16 RollRaw = static_cast<int16>(FMath::Clamp(
			FMath::RoundToInt(Metadata.PlatformRollDeg * 32767.0 / 50.0), -32767, 32767));
		TArray<uint8> R; AppendU16(R, static_cast<uint16>(RollRaw)); AddLocalTag(Local, 7, R);
	}
	if (Metadata.bHasFieldOfView)
	{
		const uint16 HRaw = static_cast<uint16>(FMath::Clamp(
			FMath::RoundToInt(Metadata.HorizontalFovDeg * 65535.0 / 180.0), 0, 65535));
		TArray<uint8> H; AppendU16(H, HRaw); AddLocalTag(Local, 16, H);
		const uint16 VRaw = static_cast<uint16>(FMath::Clamp(
			FMath::RoundToInt(Metadata.VerticalFovDeg * 65535.0 / 180.0), 0, 65535));
		TArray<uint8> V; AppendU16(V, VRaw); AddLocalTag(Local, 17, V);
	}
	static const uint8 UasLocalSetKey[16] = {
		0x06,0x0E,0x2B,0x34,0x02,0x0B,0x01,0x01,
		0x0E,0x01,0x03,0x01,0x01,0x00,0x00,0x00
	};
	TArray<uint8> Result;
	Result.Append(UasLocalSetKey, UE_ARRAY_COUNT(UasLocalSetKey));
	AppendBerLength(Result, Local.Num());
	Result.Append(Local);
	return Result;
}
