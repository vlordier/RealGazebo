// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SpawnCommandSender.generated.h"

class FSocket;
class FInternetAddr;

/**
 * Minimal outbound UDP client for runtime vehicle spawn/despawn commands.
 *
 * Sends RealGazebo wire packets to the simulation manager (default port
 * 5006): the MessageID=1 pose packet doubles as the SPAWN command and a
 * header-only MessageID=4 as DESPAWN. Coordinates are converted from
 * Unreal world space to the Gazebo frame — the exact inverse of the
 * inbound conversion in UDataStreamProcessor (both axes maps are
 * involutions, so the same component flips apply).
 *
 * Fire-and-forget by design: there is no ACK. A successful spawn shows up
 * as the vehicle's normal pose stream (which auto-spawns the visual pawn);
 * a missing stream after a timeout means the spawn failed sim-side.
 */
UCLASS()
class REALGAZEBOBRIDGE_API USpawnCommandSender : public UObject
{
    GENERATED_BODY()

public:
    /** Set/replace the manager destination. Returns false on an invalid IP. */
    bool SetDestination(const FString& IPAddress, int32 Port);

    bool HasDestination() const { return Destination.IsValid(); }

    /** Spawn command: [Num][Type][0x01] + 7 little-endian floats
     *  (gz position x,y,z in meters + gz quaternion x,y,z,w). */
    bool SendSpawn(uint8 VehicleNum, uint8 VehicleTypeCode,
                   const FVector& UnrealLocation, const FQuat& UnrealRotation);

    /** Despawn command: header-only [Num][Type][0x04]. */
    bool SendDespawn(uint8 VehicleNum, uint8 VehicleTypeCode);

    virtual void BeginDestroy() override;

private:
    bool EnsureSocket();
    bool SendPacket(const TArray<uint8>& Packet);
    static void AppendFloatLE(TArray<uint8>& Out, float Value);

    FSocket* SendSocket = nullptr;
    TSharedPtr<FInternetAddr> Destination;
};
