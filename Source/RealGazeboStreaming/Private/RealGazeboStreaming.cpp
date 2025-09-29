// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboStreaming.h"

DEFINE_LOG_CATEGORY(LogRealGazeboStreaming);

void FRealGazeboStreamingModule::StartupModule()
{
    UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming module starting up"));
}

void FRealGazeboStreamingModule::ShutdownModule()
{
    UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming module shutting down"));
}

IMPLEMENT_MODULE(FRealGazeboStreamingModule, RealGazeboStreaming)