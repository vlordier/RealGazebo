// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Transport/STANAG4609Sink.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	constexpr uint16 ProgramNumber = 1;
	constexpr uint16 PmtPid = 0x100;
	constexpr uint16 VideoPid = 0x101;
	constexpr uint16 KlvPid = 0x102;
	constexpr double EarthRadiusMeters = 6378137.0;

	uint32 MpegCrc32(const uint8* Data, int32 Length)
	{
		uint32 Crc = 0xFFFFFFFFu;
		for (int32 I = 0; I < Length; ++I)
		{
			Crc ^= static_cast<uint32>(Data[I]) << 24;
			for (int32 Bit = 0; Bit < 8; ++Bit)
				Crc = (Crc & 0x80000000u) ? (Crc << 1) ^ 0x04C11DB7u : (Crc << 1);
		}
		return Crc;
	}

	void U16(TArray<uint8>& O, uint16 V) { O.Add(V >> 8); O.Add(V); }
	void U32(TArray<uint8>& O, uint32 V) { O.Add(V >> 24); O.Add(V >> 16); O.Add(V >> 8); O.Add(V); }
	void U64(TArray<uint8>& O, uint64 V) { for (int S = 56; S >= 0; S -= 8) O.Add(V >> S); }

	void Ber(TArray<uint8>& O, int32 L)
	{
		if (L < 0x80) O.Add(static_cast<uint8>(L));
		else if (L <= 0xFF) { O.Add(0x81); O.Add(static_cast<uint8>(L)); }
		else { O.Add(0x82); U16(O, static_cast<uint16>(L)); }
	}

	void Tag(TArray<uint8>& O, uint8 T, const TArray<uint8>& V)
	{
		O.Add(T); Ber(O, V.Num()); O.Append(V);
	}

	void Pts(TArray<uint8>& O, uint64 P)
	{
		P &= ((1ULL << 33) - 1);
		O.Add(0x20 | (((P >> 30) & 7) << 1) | 1);
		O.Add(P >> 22);
		O.Add((((P >> 15) & 0x7F) << 1) | 1);
		O.Add(P >> 7);
		O.Add(((P & 0x7F) << 1) | 1);
	}

	uint64 EpochUs()
	{
		static const FDateTime Epoch(1970, 1, 1);
		return static_cast<uint64>((FDateTime::UtcNow() - Epoch).GetTicks() / 10);
	}

	int32 MapSigned(double Value, double Min, double Max)
	{
		const double C = FMath::Clamp(Value, Min, Max);
		return static_cast<int32>(FMath::RoundToDouble((C - Min) / (Max - Min) * 4294967294.0 - 2147483647.0));
	}

	uint16 MapU16(double Value, double Min, double Max)
	{
		return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt((Value - Min) * 65535.0 / (Max - Min)), 0, 65535));
	}
}

FSTANAG4609Sink::FSTANAG4609Sink(FString InHost, int32 InPort)
	: Host(MoveTemp(InHost)), Port(InPort)
{
	FMemory::Memzero(Continuity, sizeof(Continuity));
	const TCHAR* Cmd = FCommandLine::Get();
	bHasGeoOrigin =
		FParse::Value(Cmd, TEXT("RealGazeboGeoOriginLat="), OriginLatitudeDeg) &&
		FParse::Value(Cmd, TEXT("RealGazeboGeoOriginLon="), OriginLongitudeDeg) &&
		FParse::Value(Cmd, TEXT("RealGazeboGeoOriginAlt="), OriginAltitudeMslMeters);
}

FSTANAG4609Sink::~FSTANAG4609Sink() { Stop(); }

bool FSTANAG4609Sink::Start(FString& OutErrorMessage)
{
	if (bStarted.load()) return true;
	ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!S) { OutErrorMessage = TEXT("Socket subsystem unavailable"); return false; }
	Socket = S->CreateSocket(NAME_DGram, TEXT("RealGazebo-STANAG4609"), false);
	if (!Socket) { OutErrorMessage = TEXT("Failed to create STANAG UDP socket"); return false; }
	Destination = S->CreateInternetAddr();
	bool Valid = false;
	Destination->SetIp(*Host, Valid);
	Destination->SetPort(Port);
	if (!Valid)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid STANAG destination: %s"), *Host);
		S->DestroySocket(Socket); Socket = nullptr; Destination.Reset(); return false;
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
		if (ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) S->DestroySocket(Socket);
		Socket = nullptr;
	}
	Destination.Reset();
}

void FSTANAG4609Sink::PushEncodedVideo(const TArray<FEncodedNALUnit>& NALUnits, const FEncodedVideoMetadata& Metadata)
{
	if (!bStarted.load() || !Socket || !Destination.IsValid()) return;
	if ((PacketCounter % 400) == 0) EmitProgramTables();
	EmitMetadata(Metadata);
	EmitVideo(NALUnits, Metadata.TimestampUs);
}

void FSTANAG4609Sink::EmitProgramTables()
{
	TArray<uint8> Pat;
	Pat.Add(0x00); Pat.Add(0xB0); Pat.Add(0x0D); U16(Pat, 1); Pat.Add(0xC1); Pat.Add(0); Pat.Add(0);
	U16(Pat, ProgramNumber); Pat.Add(0xE0 | ((PmtPid >> 8) & 0x1F)); Pat.Add(static_cast<uint8>(PmtPid & 0xFF)); U32(Pat, MpegCrc32(Pat.GetData(), Pat.Num()));
	TArray<uint8> PP; PP.Add(0); PP.Append(Pat); EmitTSPayload(0, PP, true);

	TArray<uint8> Pmt;
	Pmt.Add(0x02); Pmt.Add(0xB0); Pmt.Add(0x1D); U16(Pmt, ProgramNumber); Pmt.Add(0xC1); Pmt.Add(0); Pmt.Add(0);
	Pmt.Add(0xE0 | ((VideoPid >> 8) & 0x1F)); Pmt.Add(static_cast<uint8>(VideoPid & 0xFF)); Pmt.Add(0xF0); Pmt.Add(0);
	Pmt.Add(0x1B); Pmt.Add(0xE0 | ((VideoPid >> 8) & 0x1F)); Pmt.Add(static_cast<uint8>(VideoPid & 0xFF)); Pmt.Add(0xF0); Pmt.Add(0);
	Pmt.Add(0x06); Pmt.Add(0xE0 | ((KlvPid >> 8) & 0x1F)); Pmt.Add(static_cast<uint8>(KlvPid & 0xFF)); Pmt.Add(0xF0); Pmt.Add(0x06);
	Pmt.Add(0x05); Pmt.Add(0x04); Pmt.Add('K'); Pmt.Add('L'); Pmt.Add('V'); Pmt.Add('A');
	U32(Pmt, MpegCrc32(Pmt.GetData(), Pmt.Num()));
	TArray<uint8> MP; MP.Add(0); MP.Append(Pmt); EmitTSPayload(PmtPid, MP, true);
}

void FSTANAG4609Sink::EmitVideo(const TArray<FEncodedNALUnit>& NALUnits, uint64 TimestampUs)
{
	TArray<uint8> B;
	for (const FEncodedNALUnit& N : NALUnits) B.Append(N.Data);
	if (!B.IsEmpty()) EmitPES(VideoPid, 0xE0, B, TimestampUs * 90ULL / 1000ULL, true);
}

void FSTANAG4609Sink::EmitMetadata(const FEncodedVideoMetadata& M)
{
	TArray<uint8> K = BuildMISBLocalSet(M);
	if (!K.IsEmpty()) EmitPES(KlvPid, 0xBD, K, M.TimestampUs * 90ULL / 1000ULL, true);
}

void FSTANAG4609Sink::EmitPES(uint16 PID, uint8 StreamId, const TArray<uint8>& Payload, uint64 Pts90k, bool Align)
{
	TArray<uint8> P;
	P.Add(0); P.Add(0); P.Add(1); P.Add(StreamId);
	const int32 L = 8 + Payload.Num();
	U16(P, (StreamId == 0xE0 || L > 0xFFFF) ? 0 : static_cast<uint16>(L));
	P.Add(0x80 | (Align ? 0x04 : 0)); P.Add(0x80); P.Add(0x05); Pts(P, Pts90k); P.Append(Payload);
	EmitTSPayload(PID, P, true);
}

void FSTANAG4609Sink::EmitTSPayload(uint16 PID, const TArray<uint8>& Payload, bool Start)
{
	int32 Off = 0; bool First = true;
	while (Off < Payload.Num())
	{
		uint8 Packet[188]; FMemory::Memset(Packet, 0xFF, 188);
		Packet[0] = 0x47; Packet[1] = ((First && Start) ? 0x40 : 0) | ((PID >> 8) & 0x1F); Packet[2] = PID;
		const int32 Copy = FMath::Min(Payload.Num() - Off, 184);
		int32 PO = 4;
		if (Copy < 184)
		{
			Packet[3] = 0x30 | (Continuity[PID]++ & 0x0F);
			const int32 AL = 183 - Copy; Packet[4] = AL; if (AL > 0) Packet[5] = 0; PO = 5 + AL;
		}
		else Packet[3] = 0x10 | (Continuity[PID]++ & 0x0F);
		FMemory::Memcpy(Packet + PO, Payload.GetData() + Off, Copy); Off += Copy; SendPacket(Packet); First = false;
	}
}

void FSTANAG4609Sink::SendPacket(const uint8* Packet188)
{
	if (!Socket || !Destination.IsValid()) return;
	int32 Sent = 0; Socket->SendTo(Packet188, 188, Sent, *Destination); ++PacketCounter;
}

bool FSTANAG4609Sink::ResolveWGS84(const FEncodedVideoMetadata& M, double& Lat, double& Lon, double& Alt) const
{
	if (M.bHasWGS84Position)
	{
		Lat = M.LatitudeDeg; Lon = M.LongitudeDeg; Alt = M.AltitudeMslMeters; return true;
	}
	if (!bHasGeoOrigin || !M.bHasLocalPosition) return false;
	// Explicit convention for the local georeference bridge: Unreal X=East, Y=North, Z=Up.
	const double East = M.LocalPositionCm.X / 100.0;
	const double North = M.LocalPositionCm.Y / 100.0;
	const double Up = M.LocalPositionCm.Z / 100.0;
	Lat = OriginLatitudeDeg + FMath::RadiansToDegrees(North / EarthRadiusMeters);
	const double CosLat = FMath::Cos(FMath::DegreesToRadians(OriginLatitudeDeg));
	Lon = OriginLongitudeDeg + FMath::RadiansToDegrees(East / (EarthRadiusMeters * FMath::Max(0.01, FMath::Abs(CosLat))));
	Alt = OriginAltitudeMslMeters + Up;
	return true;
}

TArray<uint8> FSTANAG4609Sink::BuildMISBLocalSet(const FEncodedVideoMetadata& M) const
{
	TArray<uint8> L;
	{ TArray<uint8> V; U64(V, EpochUs()); Tag(L, 2, V); }
	if (M.bHasPlatformAttitude)
	{
		const double Hdg = FMath::Fmod(M.PlatformHeadingDeg + 360.0, 360.0);
		{ TArray<uint8> V; U16(V, MapU16(Hdg, 0, 360)); Tag(L, 5, V); }
		{ TArray<uint8> V; U16(V, static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(M.PlatformPitchDeg * 32767.0 / 20.0), -32767, 32767))); Tag(L, 6, V); }
		{ TArray<uint8> V; U16(V, static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(M.PlatformRollDeg * 32767.0 / 50.0), -32767, 32767))); Tag(L, 7, V); }
	}
	double Lat = 0, Lon = 0, Alt = 0;
	if (ResolveWGS84(M, Lat, Lon, Alt))
	{
		{ TArray<uint8> V; U32(V, static_cast<uint32>(MapSigned(Lat, -90, 90))); Tag(L, 13, V); }
		{ TArray<uint8> V; U32(V, static_cast<uint32>(MapSigned(Lon, -180, 180))); Tag(L, 14, V); }
		{ TArray<uint8> V; U16(V, MapU16(Alt, -900, 19000)); Tag(L, 15, V); }
	}
	if (M.bHasFieldOfView)
	{
		{ TArray<uint8> V; U16(V, MapU16(M.HorizontalFovDeg, 0, 180)); Tag(L, 16, V); }
		{ TArray<uint8> V; U16(V, MapU16(M.VerticalFovDeg, 0, 180)); Tag(L, 17, V); }
	}
	static const uint8 Key[16] = {0x06,0x0E,0x2B,0x34,0x02,0x0B,0x01,0x01,0x0E,0x01,0x03,0x01,0x01,0,0,0};
	TArray<uint8> R; R.Append(Key, 16); Ber(R, L.Num()); R.Append(L); return R;
}
