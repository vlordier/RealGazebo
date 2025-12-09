// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * RealGazeboStreaming Module
 *
 * Provides zero-copy GPU-based H.264 RTSP streaming for multi-camera vehicle streaming
 * with ultra-low latency. Integrates with RealGazeboBridge for vehicle lifecycle management.
 *
 * Key Features:
 * - Hardware-accelerated encoding (NVENC/AMF)
 * - Live555-based RTSP server (port 8554)
 * - Zero-copy GPU texture encoding
 * - Runtime configurable resolution/FPS/bitrate
 * - Support for 8+ concurrent streams
 * - Ultra-low latency (< 100ms end-to-end)
 *
 * Usage:
 * 1. Drag ARealGazeboStreamingManager into level
 * 2. Configure RTSP port, max streams, quality settings
 * 3. Add UVehicleCameraComponent to vehicle Blueprints
 * 4. Streams auto-start when vehicles spawn
 *
 * RTSP URL Format:
 * - rtsp://localhost:8554/<vehicle_type_num>/<camera_id>
 * - Example: rtsp://localhost:8554/x500_0/front
 */
class FRealGazeboStreamingModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Singleton-like access to this module's interface. This is just for convenience!
	 * Beware of calling this during the shutdown phase, though. Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline FRealGazeboStreamingModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FRealGazeboStreamingModule>("RealGazeboStreaming");
	}

	/**
	 * Checks to see if this module is loaded and ready. It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("RealGazeboStreaming");
	}
};
