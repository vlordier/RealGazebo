#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SyntheticDroneSwarm.generated.h"

class UVehicleCameraComponent;
class AVehicleBasePawn;

/**
 * ASyntheticDroneSwarm
 *
 * Spawns N synthetic drones with deterministic Lissajous trajectories
 * for FPV streaming validation without requiring Gazebo/ArduPilot.
 *
 * Each drone gets a VehicleCameraComponent that auto-registers with
 * the streaming subsystem for SceneCapture → VideoToolbox → RTSP.
 *
 * Trajectory: Lissajous curves with phase offsets for collision-free
 * motion within the Bordeaux airport area.
 */
UCLASS(Blueprintable, meta=(DisplayName="Synthetic Drone Swarm"))
class REALGAZEBOSTREAMING_API ASyntheticDroneSwarm : public AActor
{
	GENERATED_BODY()

public:
	ASyntheticDroneSwarm();

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Number of synthetic drones to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm",
	          meta = (ClampMin = "1", ClampMax = "64"))
	int32 DroneCount = 5;

	/** Drone class to spawn (must have VehicleCameraComponent) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm")
	TSubclassOf<AVehicleBasePawn> DroneClass;

	/** Static mesh to use for drone visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm")
	TObjectPtr<UStaticMesh> DroneMesh;

	/** Spawn area center (UE coordinates, near airport origin) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm")
	FVector SpawnCenter = FVector(0.0f, 0.0f, 5000.0f);

	/** Spawn area radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm")
	float SpawnRadius = 10000.0f;

	/** Lissajous frequency multiplier (X axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float FreqX = 0.5f;

	/** Lissajous frequency multiplier (Y axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float FreqY = 0.4f;

	/** Lissajous frequency multiplier (Z axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float FreqZ = 0.3f;

	/** Amplitude of motion (UE cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float Amplitude = 15000.0f;

	/** Altitude variation amplitude (UE cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float AltitudeAmplitude = 3000.0f;

	/** Base altitude (UE cm above ground) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float BaseAltitude = 5000.0f;

	/** Speed multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Trajectory")
	float Speed = 0.3f;

	/** Auto-start streaming when drones spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Streaming")
	bool bAutoStartStreaming = true;

	/** RTSP port */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Streaming")
	int32 RTSPPort = 8554;

	/** Stream resolution preset (0=VGA, 1=SVGA, 2=XGA, 3=SXGA, 4=UXGA) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Streaming")
	int32 StreamResolution = 2; // XGA 1024x768

	/** Stream frame rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synthetic Swarm|Streaming")
	int32 StreamFPS = 30;

	//----------------------------------------------------------
	// Runtime
	//----------------------------------------------------------

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Get number of active drones */
	UFUNCTION(BlueprintPure, Category = "Synthetic Swarm")
	int32 GetActiveDroneCount() const { return Drones.Num(); }

	/** Get drone at index */
	UFUNCTION(BlueprintPure, Category = "Synthetic Swarm")
	AVehicleBasePawn* GetDrone(int32 Index) const
	{
		return Drones.IsValidIndex(Index) ? Drones[Index].Get() : nullptr;
	}

private:
	/** Spawn all drones */
	void SpawnDrones();

	/** Update drone transforms with Lissajous math */
	void UpdateDroneTransforms(float DeltaTime);

	/** Start streaming for all drones */
	void StartAllStreams();

	/** Spawned drones */
	UPROPERTY()
	TArray<TWeakObjectPtr<AVehicleBasePawn>> Drones;

	/** Elapsed time for trajectory calculation */
	float ElapsedTime = 0.0f;

	/** Phase offsets per drone (deterministic) */
	TArray<float> PhaseOffsets;
};