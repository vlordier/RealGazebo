// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"

/**
 * SceneCapture2D Component Pool
 *
 * This pool reuses SceneCapture2D components to reduce UObject creation overhead
 * and garbage collection pressure during vehicle spawn and despawn operations.
 *
 * Performance benefits include faster vehicle spawning due to eliminated NewObject
 * allocation, reduced garbage collection pauses since components remain allocated,
 * and lower memory fragmentation from component reuse.
 *
 * All methods are thread-safe and protected by FCriticalSection for concurrent access.
 */
class REALGAZEBOSTREAMING_API FRealGazeboSceneCapturePool
{
public:
	FRealGazeboSceneCapturePool();
	~FRealGazeboSceneCapturePool();

	/**
	 * Acquires a scene capture component from the pool. If no components are available,
	 * a new one will be created and registered.
	 * @param Owner Owner actor for the component (required for component registration)
	 * @return A scene capture component ready for use, either pooled or newly created
	 */
	USceneCaptureComponent2D* Acquire(AActor* Owner);

	/**
	 * Releases a component back to the pool for future reuse. The component state
	 * will be reset to default values before being returned to the pool.
	 * @param Component The component to return to the pool
	 */
	void Release(USceneCaptureComponent2D* Component);

	/**
	 * Clears and destroys all pooled components. Note that this does not affect
	 * components that are currently in active use.
	 */
	void Clear();

	/**
	 * Retrieves current pool statistics for monitoring and debugging purposes.
	 * @param OutAvailable The number of components available for reuse in the pool
	 * @param OutActive The number of components currently in active use
	 */
	void GetPoolStats(int32& OutAvailable, int32& OutActive) const;

	/**
	 * Returns the current maximum number of components that can be stored in the pool.
	 */
	int32 GetMaxPoolSize() const { return MaxPoolSize.load(std::memory_order_relaxed); }

	/**
	 * Updates pool capacity based on active camera count.
	 * Pool grows automatically to accommodate new cameras.
	 * @param ActiveCameraCount Number of currently active cameras
	 */
	void UpdateCapacity(int32 ActiveCameraCount);

private:
	/** Components available in the pool and ready for reuse */
	TArray<USceneCaptureComponent2D*> AvailableComponents;

	/** Components currently in active use by cameras */
	TArray<USceneCaptureComponent2D*> ActiveComponents;

	/** Dynamic maximum pool size (grows based on active cameras) */
	std::atomic<int32> MaxPoolSize;

	/** Critical section for thread-safe access to pool operations */
	mutable FCriticalSection PoolMutex;

	/** Resets a component to its default state in preparation for reuse */
	void ResetComponent(USceneCaptureComponent2D* Component);
};
