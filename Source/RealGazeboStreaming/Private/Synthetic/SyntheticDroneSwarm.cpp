#include "Synthetic/SyntheticDroneSwarm.h"
#include "Camera/VehicleCameraComponent.h"
#include "Vehicles/VehicleBasePawn.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/GameStateBase.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ASyntheticDroneSwarm::ASyntheticDroneSwarm()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void ASyntheticDroneSwarm::BeginPlay()
{
	Super::BeginPlay();
	SpawnDrones();
	if (bAutoStartStreaming)
	{
		StartAllStreams();
	}
}

void ASyntheticDroneSwarm::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;
	UpdateDroneTransforms(DeltaTime);
}

void ASyntheticDroneSwarm::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Drones will be destroyed by the world
	Drones.Empty();
	Super::EndPlay(EndPlayReason);
}

void ASyntheticDroneSwarm::SpawnDrones()
{
	if (!DroneClass || DroneCount < 1) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Generate deterministic phase offsets
	PhaseOffsets.Empty();
	PhaseOffsets.Reserve(DroneCount);
	for (int32 i = 0; i < DroneCount; ++i)
	{
		// Deterministic phase: evenly spaced around the circle
		const float Phase = (2.0f * PI * i) / DroneCount;
		PhaseOffsets.Add(Phase);
	}

	// Spawn drones
	for (int32 i = 0; i < DroneCount; ++i)
	{
		// Distribute spawn positions in a ring
		const float Angle = (2.0f * PI * i) / DroneCount;
		const FVector SpawnLocation = SpawnCenter + FVector(
			FMath::Cos(Angle) * SpawnRadius * 0.5f,
			FMath::Sin(Angle) * SpawnRadius * 0.5f,
			0.0f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.bNoFail = true;

		AVehicleBasePawn* Drone = World->SpawnActor<AVehicleBasePawn>(DroneClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
		if (!Drone)
		{
			UE_LOG(LogTemp, Warning, TEXT("SyntheticDroneSwarm: Failed to spawn drone %d"), i);
			continue;
		}

		// Set vehicle identity
		Drone->VehicleID = FVehicleID(static_cast<uint8>(i + 1), 0); // VehicleNum=i+1, VehicleType=0
		Drone->VehicleType = 0;
		Drone->VehicleTypeName = TEXT("synthetic");

		// Set mesh if provided
		if (DroneMesh && Drone->VehicleMesh)
		{
			Drone->VehicleMesh->SetStaticMesh(DroneMesh);
		}

		// Activate the drone
		Drone->SetActiveVehicle(true);

		// Add VehicleCameraComponent for FPV
		UVehicleCameraComponent* FPV = NewObject<UVehicleCameraComponent>(Drone);
		if (FPV)
		{
			FPV->CameraID = TEXT("fpv");
			FPV->bEnableStreaming = true;
			FPV->SetupAttachment(Drone->RootSceneComponent);
			FPV->RegisterComponent();

			// Set capture parameters
			FPV->SetRelativeLocation(FVector(0, 0, 0)); // Center of drone
			FPV->bCaptureEveryFrame = false;
			FPV->bCaptureOnMovement = false;
			FPV->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_LegacySceneCapture;
			FPV->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;

			// Set render target size from stream config
			int32 ResW = 1024, ResH = 768;
			switch (StreamResolution)
			{
				case 0: ResW = 640;  ResH = 480;  break;
				case 1: ResW = 800;  ResH = 600;  break;
				case 2: ResW = 1024; ResH = 768;  break;
				case 3: ResW = 1280; ResH = 960;  break;
				case 4: ResW = 1600; ResH = 1200; break;
			}
			FPV->TextureTarget = NewObject<UTextureRenderTarget2D>(FPV);
			if (FPV->TextureTarget)
			{
				FPV->TextureTarget->InitAutoFormat(ResW, ResH);
				FPV->TextureTarget->RenderTargetFormat = RTF_RGBA8;
				FPV->TextureTarget->bGPUSharedFlag = true;
				FPV->TextureTarget->UpdateResource();
			}
		}

		Drones.Add(Drone);
		UE_LOG(LogTemp, Log, TEXT("SyntheticDroneSwarm: Spawned drone %d/%d (VehicleNum=%d)"),
		       i + 1, DroneCount, i + 1);
	}
}

void ASyntheticDroneSwarm::UpdateDroneTransforms(float DeltaTime)
{
	const float T = ElapsedTime * Speed;

	for (int32 i = 0; i < Drones.Num(); ++i)
	{
		AVehicleBasePawn* Drone = Drones[i].Get();
		if (!Drone) continue;

		const float Phase = PhaseOffsets.IsValidIndex(i) ? PhaseOffsets[i] : 0.0f;

		// Lissajous trajectory
		const float X = FMath::Sin(FreqX * T + Phase) * Amplitude;
		const float Y = FMath::Sin(FreqY * T + Phase * 1.3f) * Amplitude;
		const float Z = BaseAltitude + FMath::Sin(FreqZ * T + Phase * 0.7f) * AltitudeAmplitude;

		const FVector Position = SpawnCenter + FVector(X, Y, Z);

		// Calculate heading from velocity (numerical derivative)
		const float Eps = 0.01f;
		const float Vx = FMath::Sin(FreqX * (T + Eps) + Phase) - FMath::Sin(FreqX * (T - Eps) + Phase);
		const float Vy = FMath::Sin(FreqY * (T + Eps) + Phase * 1.3f) - FMath::Sin(FreqY * (T - Eps) + Phase * 1.3f);
		const float Yaw = FMath::Atan2(Vy, Vx) * (180.0f / PI);

		// Pitch follows vertical velocity
		const float Vz = (FMath::Sin(FreqZ * (T + Eps) + Phase * 0.7f) -
		                  FMath::Sin(FreqZ * (T - Eps) + Phase * 0.7f)) * AltitudeAmplitude / Eps;
		const float Pitch = -FMath::Atan2(Vz, FMath::Sqrt(Vx * Vx + Vy * Vy)) * (180.0f / PI);

		Drone->SetActorLocationAndRotation(Position, FRotator(Pitch, Yaw, 0.0f));
	}
}

void ASyntheticDroneSwarm::StartAllStreams()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance()) return;

	URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SyntheticDroneSwarm: Streaming subsystem not available"));
		return;
	}

	// Start RTSP server
	if (!Subsystem->IsRTSPServerRunning())
	{
		if (!Subsystem->StartRTSPServer(RTSPPort))
		{
			UE_LOG(LogTemp, Error, TEXT("SyntheticDroneSwarm: Failed to start RTSP server on port %d"), RTSPPort);
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("SyntheticDroneSwarm: RTSP server started on port %d"), RTSPPort);
	}

	// The VehicleCameraComponents will auto-register and start streaming
	// via their TickComponent polling loop
	UE_LOG(LogTemp, Log, TEXT("SyntheticDroneSwarm: %d drones spawned, cameras will auto-start streaming"), Drones.Num());
}