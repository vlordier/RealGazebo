// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.
#pragma once

#include "CoreMinimal.h"
#include "Encoding/RealGazeboHardwareEncoder.h"
#include "Core/RealGazeboStreamConfig.h"

/**
 * GPU Vendor Detection (Hardware Encoding Support)
 */
enum class EGPUVendor : uint8
{
	Unknown,  // No compatible hardware encoder detected
	NVIDIA,   // NVENC hardware encoder support
	AMD       // AMF hardware encoder support
};

/**
 * Encoder Factory - Hardware Encoding Only
 *
 * Auto-detects GPU vendor and creates appropriate hardware encoder:
 * 1. NVENC (NVIDIA GPUs) - GeForce GTX 600+, Quadro, Tesla via CUDA interop
 * 2. AMF (AMD GPUs) - Radeon RX 400+ via native RHI
 *
 * Requirements:
 * - Compatible GPU (NVIDIA or AMD)
 * - Up-to-date GPU drivers
 * - CUDA support (for NVIDIA)
 *
 * No Software Fallback:
 * - Software encoding is intentionally not supported
 * - Real-time multi-stream encoding requires hardware acceleration
 * - Hardware encoding: <5ms per frame vs CPU encoding: ~30ms per frame
 *
 * If initialization fails, streaming will not start for that camera.
 */
class REALGAZEBOSTREAMING_API FRealGazeboEncoderFactory
{
public:
	/**
	 * Create best available hardware encoder for configuration
	 * @param Config Stream configuration
	 * @return Encoder instance or nullptr if no encoder available
	 */
	static TSharedPtr<IRealGazeboHardwareEncoder> CreateEncoder(const FRealGazeboStreamConfig& Config);

	/**
	 * Create specific encoder type (for debugging/testing)
	 * @param EncoderType "NVENC", "AMF", or "Mock"
	 * @param Config Stream configuration
	 * @return Encoder instance or nullptr if not available
	 */
	static TSharedPtr<IRealGazeboHardwareEncoder> CreateEncoderByName(const FString& EncoderType,
	                                                                   const FRealGazeboStreamConfig& Config);

	/**
	 * Get list of available encoder names
	 * @return Array of available encoder names
	 */
	static TArray<FString> GetAvailableEncoders();

	/**
	 * Check if hardware encoding is available
	 * @return True if NVENC or AMF is available
	 */
	static bool IsHardwareEncodingAvailable();

	/**
	 * Get recommended encoder name for this system
	 * @return Encoder name (NVENC/AMF/Software)
	 */
	static FString GetRecommendedEncoderName();

private:
	/** Detect GPU vendor from RHI */
	static EGPUVendor DetectGPUVendor();

	/** Try to create NVENC encoder (NVIDIA hardware via CUDA interop) */
	static TSharedPtr<IRealGazeboHardwareEncoder> TryCreateNVENC(const FRealGazeboStreamConfig& Config);

	/** Try to create AMF encoder (AMD hardware via native RHI) */
	static TSharedPtr<IRealGazeboHardwareEncoder> TryCreateAMF(const FRealGazeboStreamConfig& Config);

	/** Try to create software encoder (CPU fallback) */
	static TSharedPtr<IRealGazeboHardwareEncoder> TryCreateSoftware(const FRealGazeboStreamConfig& Config);

	/** Get vendor name string */
	static FString GetVendorName(EGPUVendor Vendor);

	/** Check NVENC availability */
	static bool IsNVENCAvailable();

	/** Check AMF availability */
	static bool IsAMFAvailable();

	/** Cached GPU vendor */
	static EGPUVendor CachedGPUVendor;

	/** Whether vendor has been detected */
	static bool bVendorDetected;
};
