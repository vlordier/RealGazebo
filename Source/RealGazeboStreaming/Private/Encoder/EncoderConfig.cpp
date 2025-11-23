// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoder/EncoderConfig.h"
#include "VideoEncoder.h"  // For FVideoEncoder::H264Profile, RateControlMode, etc.

//----------------------------------------------------------
// AVEncoder API Mapping (UE 5.1)
//----------------------------------------------------------

AVEncoder::FVideoEncoder::H264Profile FEncoderConfig::GetAVEncoderProfile() const
{
	// Always use BASELINE profile for maximum compatibility
	// Baseline = no B-frames, no CABAC, simple decode
	// Supported by all H.264 decoders including embedded/mobile
	return AVEncoder::FVideoEncoder::H264Profile::BASELINE;
}

AVEncoder::FVideoEncoder::RateControlMode FEncoderConfig::GetRateControlMode() const
{
	// Use Constant Bitrate (CBR) for consistent, predictable latency
	// CBR maintains steady bitrate → steady network usage → predictable latency
	// Alternative VBR (Variable Bitrate) would give better quality but unpredictable latency
	return bConstantBitrate
		? AVEncoder::FVideoEncoder::RateControlMode::CBR
		: AVEncoder::FVideoEncoder::RateControlMode::VBR;
}

AVEncoder::FVideoEncoder::MultipassMode FEncoderConfig::GetMultipassMode() const
{
	// Multipass encoding analyzes video in multiple passes for better quality
	// But adds latency (must buffer frames for analysis)
	// For ultra-low latency, always use single-pass
	return bMultipass
		? AVEncoder::FVideoEncoder::MultipassMode::FULL
		: AVEncoder::FVideoEncoder::MultipassMode::DISABLED;
}

