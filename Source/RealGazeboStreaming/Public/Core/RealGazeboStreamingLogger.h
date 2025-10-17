// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazeboStreaming, Log, All);

// Verbose logging helpers (compiled out in shipping builds)
#if REALGAZEBO_STREAMING_VERBOSE_LOGGING
	#define UE_LOG_STREAMING_VERBOSE(Format, ...) \
		UE_LOG(LogRealGazeboStreaming, Verbose, Format, ##__VA_ARGS__)
#else
	#define UE_LOG_STREAMING_VERBOSE(Format, ...)
#endif

// Debug logging helpers
#if REALGAZEBO_STREAMING_DEBUG
	#define UE_LOG_STREAMING_DEBUG(Format, ...) \
		UE_LOG(LogRealGazeboStreaming, Log, Format, ##__VA_ARGS__)
#else
	#define UE_LOG_STREAMING_DEBUG(Format, ...)
#endif

// Performance timing helpers
#if REALGAZEBO_STREAMING_DEBUG
	#define STREAMING_SCOPE_CYCLE_COUNTER(Name) SCOPE_CYCLE_COUNTER(Name)
#else
	#define STREAMING_SCOPE_CYCLE_COUNTER(Name)
#endif
