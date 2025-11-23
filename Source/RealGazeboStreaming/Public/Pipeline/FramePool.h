// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * FPooledFrame
 *
 * Represents a single GPU texture frame in the pool.
 * Contains the RHI texture resource and metadata for tracking usage.
 *
 * Key Features:
 * - GPU texture (RHI-agnostic - works with D3D11/D3D12/Vulkan)
 * - In-use tracking flag
 * - Frame timestamp
 * - Automatic cleanup on destruction
 */
struct FPooledFrame
{
	/** GPU texture resource (2D, RGBA8) */
	FTexture2DRHIRef Texture;

	/** Frame number (for debugging/ordering) */
	uint64 FrameNumber = 0;

	/** Timestamp when frame was acquired */
	double AcquireTime = 0.0;

	/** Is this frame currently in use by encoder? */
	std::atomic<bool> bInUse{false};

	/** Frame width */
	int32 Width = 0;

	/** Frame height */
	int32 Height = 0;

	/** Default constructor */
	FPooledFrame() = default;

	/** Check if frame is valid */
	bool IsValid() const
	{
		return Texture.IsValid() && Width > 0 && Height > 0;
	}

	/** Get memory size in bytes */
	uint64 GetMemorySize() const
	{
		// RGBA8 = 4 bytes per pixel
		return static_cast<uint64>(Width) * Height * 4;
	}
};

/**
 * FFramePool
 *
 * Per-stream GPU texture pool for zero-copy encoding.
 * Pre-allocates a small number of GPU textures and reuses them.
 *
 * CRITICAL: Each stream MUST have its own isolated pool - never share!
 *
 * Design Principles:
 * - Small pool size (3 frames) for low latency
 * - Thread-safe acquire/release
 * - Automatic texture creation on-demand
 * - GPU fence synchronization support
 * - No CPU readback (zero-copy)
 *
 * Pool States:
 * - Empty: No textures allocated yet
 * - Initializing: Allocating textures
 * - Ready: Textures available for use
 * - Full: All textures in use (blocks until one is released)
 */
class FFramePool
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/** Constructor */
	FFramePool(int32 InWidth, int32 InHeight, int32 InPoolSize = 3);

	/** Destructor - releases all GPU resources */
	~FFramePool();

	/** Initialize the pool (allocates GPU textures on render thread) */
	bool Initialize();

	/** Shutdown and release all resources */
	void Shutdown();

	//----------------------------------------------------------
	// Frame Acquisition & Release
	//----------------------------------------------------------

	/**
	 * Acquire a free frame from the pool.
	 * Blocks if all frames are in use (should rarely happen with pool size = 3).
	 *
	 * @param OutFrame - Acquired frame (nullptr if failed)
	 * @param TimeoutSeconds - Max time to wait for a frame (default 0.1s)
	 * @return True if frame acquired, false if timeout
	 */
	bool AcquireFrame(TSharedPtr<FPooledFrame>& OutFrame, double TimeoutSeconds = 0.1);

	/**
	 * Release a frame back to the pool for reuse.
	 * Thread-safe - can be called from encoder thread.
	 *
	 * @param Frame - Frame to release
	 */
	void ReleaseFrame(TSharedPtr<FPooledFrame> Frame);

	//----------------------------------------------------------
	// Status & Statistics
	//----------------------------------------------------------

	/** Get number of frames currently in use */
	int32 GetNumFramesInUse() const;

	/** Get number of free frames available */
	int32 GetNumFramesFree() const;

	/** Get total pool size */
	int32 GetPoolSize() const { return PoolSize; }

	/** Get pool dimensions */
	void GetDimensions(int32& OutWidth, int32& OutHeight) const
	{
		OutWidth = Width;
		OutHeight = Height;
	}

	/** Get total memory usage in bytes */
	uint64 GetTotalMemoryUsage() const;

	/** Is pool initialized and ready? */
	bool IsReady() const { return bInitialized; }

	//----------------------------------------------------------
	// Debug & Logging
	//----------------------------------------------------------

	/** Get pool statistics as string */
	FString GetStatsString() const;

private:
	//----------------------------------------------------------
	// Internal Helpers
	//----------------------------------------------------------

	/** Create a single pooled frame (must be called on render thread) */
	TSharedPtr<FPooledFrame> CreateFrame();

	/** Find a free frame in the pool (returns nullptr if all in use) */
	TSharedPtr<FPooledFrame> FindFreeFrame();

	//----------------------------------------------------------
	// Pool Configuration
	//----------------------------------------------------------

	/** Target frame width */
	int32 Width = 0;

	/** Target frame height */
	int32 Height = 0;

	/** Number of frames in pool (default 3 for low latency) */
	int32 PoolSize = 3;

	//----------------------------------------------------------
	// Pool Storage
	//----------------------------------------------------------

	/** Pool of pre-allocated frames */
	TArray<TSharedPtr<FPooledFrame>> Frames;

	/** Mutex for thread-safe access */
	mutable FCriticalSection PoolMutex;

	/** Frame counter (for debugging) */
	std::atomic<uint64> FrameCounter{0};

	/** Is pool initialized? */
	std::atomic<bool> bInitialized{false};

	/** Is pool shutting down? */
	std::atomic<bool> bShuttingDown{false};

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total frames acquired since initialization */
	std::atomic<uint64> TotalFramesAcquired{0};

	/** Total frames released since initialization */
	std::atomic<uint64> TotalFramesReleased{0};

	/** Number of times acquire blocked waiting for free frame */
	std::atomic<uint64> AcquireBlockCount{0};
};
