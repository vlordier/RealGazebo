// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "SpawnCommandSender.h"
#include "RealGazeboBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
    constexpr uint8 MSG_SPAWN = 1;   // pose packet reused as the spawn command
    constexpr uint8 MSG_DESPAWN = 4; // header-only destroy command
}

bool USpawnCommandSender::SetDestination(const FString& IPAddress, int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        return false;
    }

    TSharedPtr<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*IPAddress, bIsValid);
    if (!bIsValid)
    {
        UE_LOG(LogRealGazeboBridge, Warning,
               TEXT("SpawnCommandSender: invalid manager IP '%s'"), *IPAddress);
        return false;
    }
    Addr->SetPort(Port);
    Destination = Addr;
    return true;
}

bool USpawnCommandSender::SendSpawn(uint8 VehicleNum, uint8 VehicleTypeCode,
                                    const FVector& UnrealLocation, const FQuat& UnrealRotation)
{
    // Exact inverse of UDataStreamProcessor::ConvertGazeboPositionToUnreal /
    // ConvertGazeboQuaternionToUnreal (both involutions): cm -> meters with
    // Y negated; quaternion components (X, -Y, Z, -W).
    const float GzX = static_cast<float>(UnrealLocation.X) / 100.0f;
    const float GzY = static_cast<float>(-UnrealLocation.Y) / 100.0f;
    const float GzZ = static_cast<float>(UnrealLocation.Z) / 100.0f;

    const float GzQx = static_cast<float>(UnrealRotation.X);
    const float GzQy = static_cast<float>(-UnrealRotation.Y);
    const float GzQz = static_cast<float>(UnrealRotation.Z);
    const float GzQw = static_cast<float>(-UnrealRotation.W);

    TArray<uint8> Packet;
    Packet.Reserve(31); // header(3) + 7 floats(28), see EXPECTED_POSE_PACKET_SIZE
    Packet.Add(VehicleNum);
    Packet.Add(VehicleTypeCode);
    Packet.Add(MSG_SPAWN);
    AppendFloatLE(Packet, GzX);
    AppendFloatLE(Packet, GzY);
    AppendFloatLE(Packet, GzZ);
    AppendFloatLE(Packet, GzQx);
    AppendFloatLE(Packet, GzQy);
    AppendFloatLE(Packet, GzQz);
    AppendFloatLE(Packet, GzQw);

    return SendPacket(Packet);
}

bool USpawnCommandSender::SendDespawn(uint8 VehicleNum, uint8 VehicleTypeCode)
{
    TArray<uint8> Packet;
    Packet.Reserve(3);
    Packet.Add(VehicleNum);
    Packet.Add(VehicleTypeCode);
    Packet.Add(MSG_DESPAWN);

    return SendPacket(Packet);
}

bool USpawnCommandSender::EnsureSocket()
{
    if (SendSocket)
    {
        return true;
    }
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        return false;
    }
    SendSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("RealGazeboSpawnSender"), false);
    return SendSocket != nullptr;
}

bool USpawnCommandSender::SendPacket(const TArray<uint8>& Packet)
{
    if (!Destination.IsValid())
    {
        UE_LOG(LogRealGazeboBridge, Warning,
               TEXT("SpawnCommandSender: no destination set"));
        return false;
    }
    if (!EnsureSocket())
    {
        return false;
    }

    int32 BytesSent = 0;
    SendSocket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Destination);
    return BytesSent == Packet.Num();
}

void USpawnCommandSender::AppendFloatLE(TArray<uint8>& Out, float Value)
{
    // Raw IEEE-754 little-endian bytes, mirroring the receiver's BytesToFloat
    union
    {
        float value;
        uint8 bytes[4];
    } Converter;
    Converter.value = Value;
    Out.Append(Converter.bytes, 4);
}

void USpawnCommandSender::BeginDestroy()
{
    if (SendSocket)
    {
        SendSocket->Close();
        if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
        {
            SocketSubsystem->DestroySocket(SendSocket);
        }
        SendSocket = nullptr;
    }
    Super::BeginDestroy();
}
