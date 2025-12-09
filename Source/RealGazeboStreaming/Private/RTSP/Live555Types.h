// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

/**
 * Live555Types.h
 *
 * Centralized header for Live555 type definitions.
 * Only included in .cpp files to prevent header pollution.
 *
 * CRITICAL BUFFEREDPACKET NAME COLLISION FIX:
 * - Live555's MultiFramedRTPSource.hh defines "class BufferedPacket"
 * - Unreal's PacketHandler.h defines "struct BufferedPacket"
 * - We rename Live555's BufferedPacket to Live555BufferedPacket via preprocessor
 * - This is safe because BufferedPacket is internal to Live555 (not part of its public API)
 */

// CRITICAL: Rename Live555's BufferedPacket to avoid collision with Unreal's BufferedPacket
#define BufferedPacket Live555BufferedPacket

THIRD_PARTY_INCLUDES_START
// Base Live555 headers
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "H264VideoStreamDiscreteFramer.hh"
#include "H264VideoRTPSink.hh"
THIRD_PARTY_INCLUDES_END

// Restore original name after Live555 headers
#undef BufferedPacket
