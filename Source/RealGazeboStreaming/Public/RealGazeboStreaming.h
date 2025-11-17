// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * RealGazeboStreaming Module
 * Hardware-accelerated H.264 video streaming via RTSP for vehicle camera feeds
 *
 * Architecture:
 * - GameInstanceSubsystem for persistence across level changes
 * - 2-thread pipeline: Game Thread -> Encoding Thread (Hardware) -> RTSP Thread
 * - Zero-copy GPU texture encoding with no CPU readback or color conversion
 * - Hardware encoding ONLY: NVENC for NVIDIA GPUs and AMF for AMD GPUs
 * - Direct GPU texture input via CUDA, Vulkan, or DirectX interop
 * - Frame pooling system to minimize memory allocations
 * - Adaptive quality with dynamic bitrate adjustment based on queue depth
 * - Live555-based RTSP server listening on port 8554
 * - Multi-camera support per vehicle using camera identifiers
 *
 * System Requirements:
 * - NVIDIA GPU with NVENC support: GeForce GTX 600 series or newer, Quadro K-series or newer
 * - AMD GPU with AMF support: Radeon HD 7000 series or newer, GCN architecture or newer
 * - Software encoders are NOT supported, hardware acceleration is mandatory
 *
 * Integration with RealGazeboBridge:
 * 1. Place ARealGazeboStreamManager actor in level (one per level)
 * 2. Add URealGazeboStreamingCamera component to vehicle blueprints
 * 3. Set CameraID property for each camera (REQUIRED) - e.g., "front", "right", "gimbal"
 * 4. Vehicle ID is automatically detected from VehicleBasePawn owner
 * 5. Streams auto-register and auto-start when vehicle spawns from PX4-Gazebo
 *
 * RTSP URL Format (CameraID always required):
 * - Format: rtsp://localhost:8554/<vehicle_type_name>/<camera_id>
 * - Example: rtsp://localhost:8554/x500_0/front (front camera on first X500)
 * - Example: rtsp://localhost:8554/x500_0/right (right camera on first X500)
 * - Example: rtsp://localhost:8554/lc62_2/front (front camera on third LC62)
 *
 * Configuration:
 * - Users configure ONLY via StreamManager Blueprint actor (no console commands or .ini files)
 * - StreamManager settings: Resolution, Frame Rate, Aspect Ratio
 * - Auto-computed settings: Bitrate, GOP Size, H.264 Profile (Baseline for ultra-low latency)
 */
class REALGAZEBOSTREAMING_API FRealGazeboStreamingModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */ 
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the module instance */
	static FRealGazeboStreamingModule& Get();

	/** Check if the module is loaded */
	static bool IsAvailable();

private:
	/** Module instance for singleton access */
	static FRealGazeboStreamingModule* ModuleInstance;
};
