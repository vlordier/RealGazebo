// Copyright (c) 2024-2026 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.
#pragma once

#include "CoreMinimal.h"
#include "Encoder/HardwareEncoderWrapper.h"

struct REALGAZEBOSTREAMING_API FEncodedVideoMetadata
{
	uint64 FrameNumber = 0;
	uint64 TimestampUs = 0;

	/** Authoritative local Unreal/Gazebo platform position (centimeters). */
	FVector LocalPositionCm = FVector::ZeroVector;
	bool bHasLocalPosition = false;

	/** WGS-84 platform position, when supplied or derived from an explicit georeference origin. */
	double LatitudeDeg = 0.0;
	double LongitudeDeg = 0.0;
	double AltitudeMslMeters = 0.0;
	bool bHasWGS84Position = false;

	double PlatformRollDeg = 0.0;
	double PlatformPitchDeg = 0.0;
	double PlatformHeadingDeg = 0.0;
	bool bHasPlatformAttitude = false;

	double SensorRelativeRollDeg = 0.0;
	double SensorRelativePitchDeg = 0.0;
	double SensorRelativeYawDeg = 0.0;
	bool bHasSensorAttitude = false;

	double HorizontalFovDeg = 0.0;
	double VerticalFovDeg = 0.0;
	bool bHasFieldOfView = false;
};

class REALGAZEBOSTREAMING_API IEncodedVideoSink
{
public:
	virtual ~IEncodedVideoSink() = default;
	virtual bool Start(FString& OutErrorMessage) = 0;
	virtual void Stop() = 0;
	virtual void PushEncodedVideo(
		const TArray<FEncodedNALUnit>& NALUnits,
		const FEncodedVideoMetadata& Metadata) = 0;
	virtual bool WantsFrames() const { return true; }
	virtual FString GetName() const = 0;
};
