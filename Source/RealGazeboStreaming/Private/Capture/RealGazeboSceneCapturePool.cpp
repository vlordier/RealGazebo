// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Capture/RealGazeboSceneCapturePool.h"
#include "Core/RealGazeboStreamingTypes.h"

FRealGazeboSceneCapturePool::FRealGazeboSceneCapturePool()
	: MaxPoolSize(10) // Initial size, will grow automatically
{
	const int32 InitialSize = 10;
	AvailableComponents.Reserve(InitialSize);
	ActiveComponents.Reserve(InitialSize);

	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("SceneCapturePool: Initialized (Initial size: %d, dynamic sizing enabled)"), InitialSize);
}

FRealGazeboSceneCapturePool::~FRealGazeboSceneCapturePool()
{
	Clear();
}

USceneCaptureComponent2D* FRealGazeboSceneCapturePool::Acquire(AActor* Owner)
{
	if (!Owner)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("SceneCapturePool: Cannot acquire component without an owner actor"));
		return nullptr;
	}

	FScopeLock Lock(&PoolMutex);

	USceneCaptureComponent2D* Component = nullptr;

	// Attempt to reuse a component from the pool if available
	if (AvailableComponents.Num() > 0)
	{
		Component = AvailableComponents.Pop();

		// CRITICAL FIX: Validate component before reuse (GC may have destroyed it)
		if (!IsValid(Component) || !Component->IsValidLowLevel())
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("SceneCapturePool: Pooled component was invalid (GC destroyed), creating new"));
			Component = nullptr;  // Fall through to create new component
		}
		else
		{
			// CRITICAL FIX: Re-parent component to new owner to prevent GC collection
			// Without this, the component's original owner may be destroyed, leaving
			// a dangling pointer in the pool that GC will collect
			Component->Rename(nullptr, Owner, REN_DontCreateRedirectors | REN_DoNotDirty);
			Component->UnregisterComponent();  // Unregister from old owner
			Component->RegisterComponent();     // Re-register with new owner

			ResetComponent(Component);

			UE_LOG(LogRealGazeboStreaming, VeryVerbose,
				TEXT("SceneCapturePool: Reused component from pool (Available: %d, Active: %d)"),
				AvailableComponents.Num(), ActiveComponents.Num() + 1);
		}
	}
	else
	{
		// No pooled components available, create a new one
		Component = NewObject<USceneCaptureComponent2D>(Owner, USceneCaptureComponent2D::StaticClass());
		if (Component)
		{
			Component->RegisterComponent();
			UE_LOG(LogRealGazeboStreaming, Verbose,
				TEXT("SceneCapturePool: Created new component (Active: %d)"),
				ActiveComponents.Num() + 1);
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("SceneCapturePool: Failed to create new component"));
			return nullptr;
		}
	}

	if (Component)
	{
		ActiveComponents.Add(Component);
	}

	return Component;
}

void FRealGazeboSceneCapturePool::Release(USceneCaptureComponent2D* Component)
{
	if (!Component)
	{
		return;
	}

	FScopeLock Lock(&PoolMutex);

	// Remove the component from the active list
	const int32 RemovedCount = ActiveComponents.Remove(Component);
	if (RemovedCount == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("SceneCapturePool: Attempted to release a component that was not in the active list"));
		return;
	}

	// Reset the component state for reuse
	ResetComponent(Component);

	// CRITICAL: Purge invalid components from pool before returning new one
	// This prevents accumulation of dangling pointers from GC-destroyed components
	for (int32 Index = AvailableComponents.Num() - 1; Index >= 0; --Index)
	{
		if (!IsValid(AvailableComponents[Index]) || !AvailableComponents[Index]->IsValidLowLevel())
		{
			UE_LOG(LogRealGazeboStreaming, Verbose,
				TEXT("SceneCapturePool: Purged invalid component from pool (index %d)"), Index);
			AvailableComponents.RemoveAtSwap(Index);
		}
	}

	// Return the component to the pool if there is space available
	const int32 CurrentMaxPoolSize = MaxPoolSize.load(std::memory_order_relaxed);
	if (AvailableComponents.Num() < CurrentMaxPoolSize)
	{
		AvailableComponents.Add(Component);
		UE_LOG(LogRealGazeboStreaming, VeryVerbose,
			TEXT("SceneCapturePool: Returned component to pool (Available: %d, Active: %d, MaxSize: %d)"),
			AvailableComponents.Num(), ActiveComponents.Num(), CurrentMaxPoolSize);
	}
	else
	{
		// Pool is at capacity, destroy the component instead
		Component->DestroyComponent();
		UE_LOG(LogRealGazeboStreaming, VeryVerbose,
			TEXT("SceneCapturePool: Pool at capacity, destroyed component (Available: %d, Active: %d, MaxSize: %d)"),
			AvailableComponents.Num(), ActiveComponents.Num(), CurrentMaxPoolSize);
	}
}

void FRealGazeboSceneCapturePool::Clear()
{
	FScopeLock Lock(&PoolMutex);

	// Destroy all components currently in the pool
	for (USceneCaptureComponent2D* Component : AvailableComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	AvailableComponents.Empty();

	// Warn if there are still active components when clearing
	if (ActiveComponents.Num() > 0)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("SceneCapturePool: Clear called while %d components are still active (potential memory leak)"),
			ActiveComponents.Num());
	}
	ActiveComponents.Empty();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("SceneCapturePool: All components have been cleared"));
}

void FRealGazeboSceneCapturePool::GetPoolStats(int32& OutAvailable, int32& OutActive) const
{
	FScopeLock Lock(&PoolMutex);
	OutAvailable = AvailableComponents.Num();
	OutActive = ActiveComponents.Num();
}

void FRealGazeboSceneCapturePool::ResetComponent(USceneCaptureComponent2D* Component)
{
	if (!Component)
	{
		return;
	}

	// Reset the component to a clean default state for reuse
	Component->TextureTarget = nullptr;
	Component->bCaptureEveryFrame = false;
	Component->bCaptureOnMovement = false;

	// CRITICAL FIX (2025-11-14): Reset VSM persistence to prevent pool overflow with 10+ cameras
	// Without this reset, pooled components retain bAlwaysPersistRenderingState=true from previous owner,
	// causing Virtual Shadow Map page pool overflow when many cameras are active.
	// Must be set to false to prevent VSM overflow.
	Component->bAlwaysPersistRenderingState = false;

	// ShowFlags optimized for H.264 video streaming (following PixelStreaming best practices)
	Component->ShowFlags.SetTemporalAA(true);   // Smooth motion, reduces flickering
	Component->ShowFlags.SetMotionBlur(false);  // Avoid double blur (H.264 has motion compensation)
	Component->ShowFlags.SetGrain(false);       // Film grain is noise for encoders (wastes bitrate)
	Component->ShowFlags.SetVignette(false);    // Edge darkening can confuse encoders

	Component->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Component->FOVAngle = 90.0f;
}

void FRealGazeboSceneCapturePool::UpdateCapacity(int32 ActiveCameraCount)
{
	// Calculate required pool size based on active cameras
	// Add 20% buffer for camera registration bursts
	const int32 RequiredPoolSize = FMath::Max(10, static_cast<int32>(ActiveCameraCount * 1.2f));

	const int32 CurrentMaxPoolSize = MaxPoolSize.load(std::memory_order_relaxed);

	if (RequiredPoolSize != CurrentMaxPoolSize)
	{
		MaxPoolSize.store(RequiredPoolSize, std::memory_order_relaxed);
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("SceneCapturePool: Capacity updated: %d -> %d components (Active cameras: %d)"),
			CurrentMaxPoolSize, RequiredPoolSize, ActiveCameraCount);
	}
}
