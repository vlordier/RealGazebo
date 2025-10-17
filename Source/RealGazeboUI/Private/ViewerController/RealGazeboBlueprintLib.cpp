// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "ViewerController/RealGazeboBlueprintLib.h"
#include "GameFramework/PlayerController.h"
#include "RealGazeboUI.h"

void URealGazeboBlueprintLib::EnableInput(AActor* Actor)
{
    if (!Actor)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("RealGazeboBlueprintLib::EnableInput - Actor is null"));
        return;
    }

    APlayerController* PC = GetPlayerController(Actor);
    if (PC)
    {
        Actor->EnableInput(PC);
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("RealGazeboBlueprintLib::EnableInput - No PlayerController found"));
    }
}

APlayerController* URealGazeboBlueprintLib::GetPlayerController(AActor* Actor)
{
    if (!Actor)
    {
        return nullptr;
    }

    UWorld* World = Actor->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    return World->GetFirstPlayerController();
}