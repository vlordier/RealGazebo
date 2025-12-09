// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * FPooledFrame
 *
 * Represents a single reusable GPU texture frame in the pool.
 * Contains the RHI texture resource along with metadata for tracking usage and timing.
 *
 * Memory Management:
 * - GPU texture allocated once during pool initialization
 * - Reused many times throughout the stream's lifetime
 * - Automatic cleanup via FTexture2DRHIRef smart pointer
 *
 * Usage Tracking:
 * - bInUse flag: Atomic boolean prevents simultaneous use by multiple frames
 * - AcquireTime: Tracks when frame was last acquired (for debugging stalls)
 * - FrameNumber: Sequence number for debugging and ordering
 *
 * Platform Compatibility:
 * - RHI-agnostic: Works with Direct3D 11, Direct3D 12, Vulkan
 * - Texture format: RGBA8 (4 bytes per pixel, 8 bits per channel)
 */
struct FPooledFrame
{
	/** GPU texture resource - RHI reference ensures automatic cleanup */
	FTexture2DRHIRef Texture;

	/** Sequential frame number assigned when acquired (for debugging) */
	uint64 FrameNumber = 0;

	/** Timestamp when this frame was last acquired from the pool */
	double AcquireTime = 0.0;

	/** Atomic flag: Is this frame currently in use by the encoder? */
	std::atomic<bool> bInUse{false};

	/** Texture width in pixels */
	int32 Width = 0;

	/** Texture height in pixels */
	int32 Height = 0;

	/** Default constructor */
	FPooledFrame() = default;

	/**
	 * Check if this pooled frame is valid and ready for use.
	 * @return True if texture is allocated and has valid dimensions
	 */
	bool IsValid() const
	{
		return Texture.IsValid() && Width > 0 && Height > 0;
	}

	/**
	 * Calculate GPU memory size used by this texture.
	 * @return Memory size in bytes (Width x Height x 4 for RGBA8)
	 */
	uint64 GetMemorySize() const
	{
		// RGBA8 format = 4 bytes per pixel (8 bits per channel x 4 channels)
		return static_cast<uint64>(Width) * Height * 4;
	}
};

/**
 * FFramePool
 *
 * Per-stream GPU texture pool that enables efficient zero-copy encoding by reusing
 * a small set of pre-allocated GPU textures. Eliminates the overhead of allocating
 * and deallocating GPU memory for every frame.
 *
 * CRITICAL ISOLATION REQUIREMENT:
 * Each stream MUST have its own isolated pool. Never share pools between streams as
 * this would cause texture access conflicts when streams render and encode in parallel.
 *
 * Design Principles:
 * - Small pool size: Default 3 frames (~100ms buffer at 30 FPS) minimizes latency
 * - Thread-safe operations: Acquire/release can be called from different threads
 * - Lazy allocation: Textures created on render thread during Initialize()
 * - Zero-copy guarantee: Textures never leave GPU memory
 * - Automatic cleanup: Smart pointers manage texture lifetime
 *
 * Memory Efficiency:
 * - Pre-allocation eliminates per-frame allocation overhead
 * - Texture reuse reduces GPU memory fragmentation
 * - Small pool size keeps GPU memory footprint minimal
 * - Example: 1024x768 RGBA8 x 3 frames = ~9 MB GPU memory
 *
 * Pool State Machine:
 * - Empty: No textures allocated (initial state)
 * - Initializing: Allocating GPU textures on render thread
 * - Ready: Textures available for capture and encoding
 * - Exhausted: All frames in use (AcquireFrame blocks until release)
 * - Shutdown: Releasing all GPU resources
 *
 * Blocking Behavior:
 * If all frames are in use, AcquireFrame() blocks the calling thread with a timeout.
 * This should rarely happen with proper pool sizing (pool size = ~130ms buffer).
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
	 * Acquire a free frame from the pool for capture or encoding.
	 * Blocks the calling thread if all frames are currently in use, waiting up to
	 * TimeoutSeconds for a frame to become available.
	 *
	 * Under normal operation, this should return immediately as the pool is sized to
	 * maintain a ~130ms buffer. Blocking only occurs if encoding falls behind rendering.
	 *
	 * Thread Safety: Can be called from game thread (capture) or render thread
	 *
	 * @param OutFrame - Output frame reference (set to valid frame if successful)
	 * @param TimeoutSeconds - Maximum time to wait for a free frame (default 0.1s = 100ms)
	 * @return True if frame acquired successfully, false if timeout or shutdown
	 */
	bool AcquireFrame(TSharedPtr<FPooledFrame>& OutFrame, double TimeoutSeconds = 0.1);

	/**
	 * Release a frame back to the pool after encoding completes.
	 * Marks the frame as no longer in use, making it available for the next capture.
	 *
	 * This is typically called by the encoding thread after the hardware encoder
	 * finishes reading the texture data.
	 *
	 * Thread Safety: Can be called from any thread (typically encoding thread)
	 *
	 * @param Frame - Frame to release back to the pool
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
