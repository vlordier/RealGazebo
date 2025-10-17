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
 * - 2-thread pipeline: Game Thread to Encoding Thread (Hardware) to RTSP Thread
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
 * 1. Add URealGazeboStreamingCamera component to vehicle blueprints
 * 2. Vehicle ID is automatically detected from VehicleBasePawn owner
 * 3. Set CameraID property for multi-camera vehicles (e.g., "fpv", "gimbal")
 * 4. Configure global defaults in Project Settings > Plugins > RealGazebo Streaming
 * 5. Streams auto-register when vehicle spawns from PX4-Gazebo
 *
 * RTSP URL Format:
 * - Single camera: rtsp://localhost:8554/<vehicle_type_name>_<vehicle_num>
 * - Multi-camera: rtsp://localhost:8554/<vehicle_type_name>_<vehicle_num>/<camera_id>
 * - Example: rtsp://localhost:8554/X500_0 (first X500 vehicle)
 * - Example: rtsp://localhost:8554/X500_0/fpv (FPV camera on first X500)
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
