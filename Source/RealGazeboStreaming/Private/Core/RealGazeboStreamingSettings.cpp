// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamingSettings.h"

URealGazeboStreamingSettings::URealGazeboStreamingSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("RealGazebo Streaming");
}

FName URealGazeboStreamingSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}

FName URealGazeboStreamingSettings::GetSectionName() const
{
	return FName(TEXT("RealGazebo Streaming"));
}

const URealGazeboStreamingSettings* URealGazeboStreamingSettings::Get()
{
	return GetDefault<URealGazeboStreamingSettings>();
}
