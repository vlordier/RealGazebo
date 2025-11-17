// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoding/RealGazeboEncoderFactory.h"
#include "Encoding/RealGazeboNVENCEncoder.h"
#include "Encoding/RealGazeboAMFEncoder.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "RHI.h"
#include "RHIDefinitions.h"

// Static member initialization
EGPUVendor FRealGazeboEncoderFactory::CachedGPUVendor = EGPUVendor::Unknown;
bool FRealGazeboEncoderFactory::bVendorDetected = false;

TSharedPtr<IRealGazeboHardwareEncoder> FRealGazeboEncoderFactory::CreateEncoder(const FRealGazeboStreamConfig& Config)
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: Creating encoder (%dx%d @ %d kbps)"),
		Config.Dimensions.X, Config.Dimensions.Y, Config.BitrateKbps);

	// Detect GPU vendor if not already done
	if (!bVendorDetected)
	{
		CachedGPUVendor = DetectGPUVendor();
		bVendorDetected = true;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: Detected GPU vendor: %s"),
		*GetVendorName(CachedGPUVendor));

	// HARDWARE ENCODING ONLY
	// NVENC for NVIDIA, AMF for AMD
	// No software fallback - requires hardware encoder

	TSharedPtr<IRealGazeboHardwareEncoder> Encoder;

	// NVIDIA: Try NVENC with CUDA interop
	if (CachedGPUVendor == EGPUVendor::NVIDIA)
	{
		Encoder = TryCreateNVENC(Config);
		if (Encoder.IsValid())
		{
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: Using NVENC hardware encoder"));
			return Encoder;
		}

		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("EncoderFactory: NVENC hardware encoder failed to initialize"));
	}

	// AMD: Try AMF with native RHI
	if (CachedGPUVendor == EGPUVendor::AMD)
	{
		Encoder = TryCreateAMF(Config);
		if (Encoder.IsValid())
		{
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: Using AMF hardware encoder"));
			return Encoder;
		}

		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("EncoderFactory: AMF hardware encoder failed to initialize"));
	}

	// No hardware encoder available - fail gracefully
	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("EncoderFactory: No hardware encoder available (GPU: %s)"),
		*GetVendorName(CachedGPUVendor));
	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("EncoderFactory: RealGazeboStreaming requires hardware encoding (NVENC or AMF)"));
	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("EncoderFactory: Supported GPUs:"));
	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("EncoderFactory:   - NVIDIA: GeForce GTX 600+ (Kepler or newer) with NVENC"));
	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("EncoderFactory:   - AMD: Radeon RX 400+ (Polaris or newer) with AMF"));

	return nullptr;
}

EGPUVendor FRealGazeboEncoderFactory::DetectGPUVendor()
{
	// Get adapter description from RHI
	const FString AdapterName = GRHIAdapterName;
	const FString AdapterNameLower = AdapterName.ToLower();

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("EncoderFactory: GPU Adapter: %s"), *AdapterName);

	// Detect vendor from adapter name
	if (AdapterNameLower.Contains(TEXT("nvidia")) || AdapterNameLower.Contains(TEXT("geforce")) ||
		AdapterNameLower.Contains(TEXT("quadro")) || AdapterNameLower.Contains(TEXT("tesla")))
	{
		return EGPUVendor::NVIDIA;
	}

	if (AdapterNameLower.Contains(TEXT("amd")) || AdapterNameLower.Contains(TEXT("radeon")) ||
		AdapterNameLower.Contains(TEXT("ati")))
	{
		return EGPUVendor::AMD;
	}

	// Intel GPUs do not support hardware encoding in RealGazeboStreaming
	// Only NVIDIA (NVENC) and AMD (AMF) are supported
	return EGPUVendor::Unknown;
}

FString FRealGazeboEncoderFactory::GetVendorName(EGPUVendor Vendor)
{
	switch (Vendor)
	{
	case EGPUVendor::NVIDIA:
		return TEXT("NVIDIA");
	case EGPUVendor::AMD:
		return TEXT("AMD");
	default:
		return TEXT("Unknown (No Hardware Encoder)");
	}
}

FString FRealGazeboEncoderFactory::GetRecommendedEncoderName()
{
	if (!bVendorDetected)
	{
		CachedGPUVendor = DetectGPUVendor();
		bVendorDetected = true;
	}

	switch (CachedGPUVendor)
	{
	case EGPUVendor::NVIDIA:
		return TEXT("NVENC (Hardware)");
	case EGPUVendor::AMD:
		return TEXT("AMF (Hardware)");
	default:
		return TEXT("None - No compatible GPU (NVIDIA or AMD required)");
	}
}

TSharedPtr<IRealGazeboHardwareEncoder> FRealGazeboEncoderFactory::TryCreateNVENC(const FRealGazeboStreamConfig& Config)
{
	// Check if NVENC is available on this system
	if (!FRealGazeboNVENCEncoder::IsAvailable())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("EncoderFactory: NVENC not available on this system"));
		return nullptr;
	}

	// Create NVENC encoder instance
	TSharedPtr<FRealGazeboNVENCEncoder> Encoder = MakeShared<FRealGazeboNVENCEncoder>();

	// Initialize with configuration
	if (!Encoder->Initialize(Config))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("EncoderFactory: Failed to initialize NVENC encoder"));
		return nullptr;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: NVENC encoder created successfully"));
	return Encoder;
}

TSharedPtr<IRealGazeboHardwareEncoder> FRealGazeboEncoderFactory::TryCreateAMF(const FRealGazeboStreamConfig& Config)
{
	// Check if AMF is available on this system
	if (!FRealGazeboAMFEncoder::IsAvailable())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("EncoderFactory: AMF not available on this system"));
		return nullptr;
	}

	// Create AMF encoder instance
	TSharedPtr<FRealGazeboAMFEncoder> Encoder = MakeShared<FRealGazeboAMFEncoder>();

	// Initialize with configuration
	if (!Encoder->Initialize(Config))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("EncoderFactory: Failed to initialize AMF encoder"));
		return nullptr;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncoderFactory: AMF encoder created successfully"));
	return Encoder;
}

bool FRealGazeboEncoderFactory::IsHardwareEncodingAvailable()
{
	// Detect GPU vendor if not already done
	if (!bVendorDetected)
	{
		CachedGPUVendor = DetectGPUVendor();
		bVendorDetected = true;
	}

	// Check if any hardware encoder is available
	if (CachedGPUVendor == EGPUVendor::NVIDIA)
	{
		return FRealGazeboNVENCEncoder::IsAvailable();
	}

	if (CachedGPUVendor == EGPUVendor::AMD)
	{
		return FRealGazeboAMFEncoder::IsAvailable();
	}

	return false;  // No compatible GPU
}
