// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "RealGazeboBlueprintLib.generated.h"

/**
 * Blueprint function library for RealGazebo runtime input utilities
 *
 * Provides runtime input binding for camera switching (M/F/B keys)
 */
UCLASS()
class REALGAZEBOUI_API URealGazeboBlueprintLib : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    //----------------------------------------------------------
    // Input Binding Functions
    //----------------------------------------------------------

    /** Enable input for an actor (required before binding keys) */
    static void EnableInput(AActor* Actor);

    /** Bind an action to a key at runtime */
    template<class UserClass>
    static FInputActionBinding& BindActionToKey(const FName ActionName, const FKey InKey, UserClass* Actor,
        void(UserClass::*Func)())
    {
        if (!Actor)
        {
            UE_LOG(LogTemp, Warning, TEXT("RealGazeboBlueprintLib: Cannot bind action - Actor or EnableInput is null"));
            static FInputActionBinding EmptyBinding;
            return EmptyBinding;
        }

        APlayerController* PC = GetPlayerController(Actor);
        if (!PC || !PC->InputComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("RealGazeboBlueprintLib: Cannot bind action - No PlayerController or InputComponent"));
            static FInputActionBinding EmptyBinding;
            return EmptyBinding;
        }

        // Add action mapping to player input
        FInputActionKeyMapping ActionMapping(ActionName, InKey);
        PC->PlayerInput->AddActionMapping(ActionMapping);

        // Bind the function
        return PC->InputComponent->BindAction(ActionName, IE_Pressed, Actor, Func);
    }


    //----------------------------------------------------------
    // Utility Functions
    //----------------------------------------------------------

    /** Get the player controller for an actor */
    static APlayerController* GetPlayerController(AActor* Actor);
};