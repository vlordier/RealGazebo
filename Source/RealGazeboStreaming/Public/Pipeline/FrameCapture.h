// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "HAL/PlatformProcess.h"
#include "FramePool.h"

// Forward declarations
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * FCaptureFrame
 *
 * Represents a captured frame ready for encoding with GPU synchronization.
 * Combines a pooled GPU texture with a GPU fence to ensure the render thread
 * has completed writing to the texture before the encoder reads it.
 *
 * GPU Synchronization:
 * The GPU fence ensures that:
 * 1. Render thread finishes writing the frame texture
 * 2. Encoding thread waits for fence signal before reading
 * 3. No race conditions between rendering and encoding
 * 4. Zero-copy pipeline maintained (no CPU involvement)
 */
struct FCaptureFrame
{
	/** Pooled frame texture from the frame pool (reusable GPU memory) */
	TSharedPtr<FPooledFrame> PooledFrame;

	/** GPU fence for render-to-encode synchronization (per-stream, isolated) */
	FGPUFenceRHIRef GPUFence;

	/** Frame capture timestamp from game thread (for FPS calculation) */
	double Timestamp = 0.0;

	/** Sequential frame number for this stream */
	uint64 FrameNumber = 0;

	/**
	 * Check if frame is ready for encoding.
	 * Returns true when GPU fence is signaled (rendering complete).
	 * @return True if frame can be safely encoded
	 */
	bool IsReady() const
	{
		return PooledFrame && PooledFrame->IsValid() && GPUFence && GPUFence->Poll();
	}

	/**
	 * Wait for GPU to finish rendering this frame.
	 * Blocks the calling thread until the GPU fence is signaled.
	 * Used by encoder thread before reading texture data.
	 */
	void WaitForGPU()
	{
		if (GPUFence)
		{
			// UE 5.1: Poll fence in a loop with small sleeps to avoid busy-waiting
			while (!GPUFence->Poll())
			{
				FPlatformProcess::Sleep(0.001f); // 1ms sleep to yield CPU
			}
		}
	}
};

/**
 * FFrameCapture
 *
 * Per-stream frame capture system that transfers rendered camera views to GPU textures
 * for zero-copy hardware encoding. Each stream has its own isolated capture instance
 * with a dedicated GPU fence to ensure proper render-to-encode synchronization.
 *
 * CRITICAL ISOLATION REQUIREMENT:
 * Never share capture instances between streams. Each stream needs independent GPU fence
 * synchronization to prevent race conditions when multiple streams render in parallel.
 *
 * Capture Pipeline:
 * 1. SceneCaptureComponent2D renders the camera view to a RenderTarget (Render Thread)
 * 2. CopyTextureGPU() transfers RenderTarget -> PooledFrame texture (GPU-to-GPU copy)
 * 3. InsertGPUFence() places a fence command in the GPU command buffer
 * 4. GPU fence signals when rendering and copy operations complete
 * 5. Encoder thread waits for fence before submitting texture to NVENC/AMF
 *
 * Zero-Copy Architecture:
 * - All operations occur entirely on the GPU (no CPU involvement)
 * - No CPU readback or intermediate CPU buffers
 * - Direct GPU memory flow: Render Target -> Pooled Texture -> Hardware Encoder
 * - Minimizes latency and maximizes throughput
 * - Reduces memory bandwidth usage
 *
 * Thread Safety:
 * - CaptureFrame() called from game thread
 * - GPU operations executed on render thread via ENQUEUE_RENDER_COMMAND
 * - GPU fence provides synchronization between render and encoder threads
 */
class FFrameCapture
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/** Constructor */
	FFrameCapture(int32 InWidth, int32 InHeight, TSharedPtr<FFramePool> InFramePool);

	/** Destructor */
	~FFrameCapture();

	/** Initialize capture resources */
	bool Initialize();

	/** Shutdown and release resources */
	void Shutdown();

	//----------------------------------------------------------
	// Frame Capture
	//----------------------------------------------------------

	/**
	 * Capture current frame from SceneCaptureComponent2D to a pooled GPU texture.
	 * This is the main entry point called once per frame from the game thread.
	 *
	 * Process:
	 * 1. Acquire a free frame from the pool
	 * 2. Copy the scene capture's render target to the pooled texture (GPU-only)
	 * 3. Insert GPU fence to signal when copy completes
	 * 4. Return the captured frame for encoding
	 *
	 * @param SceneCapture - Unreal Engine scene capture component to capture from
	 * @param OutFrame - Output captured frame with GPU fence (valid if return is true)
	 * @return True if frame captured successfully, false if pool exhausted or error
	 */
	bool CaptureFrame(USceneCaptureComponent2D* SceneCapture, FCaptureFrame& OutFrame);

	//----------------------------------------------------------
	// Status
	//----------------------------------------------------------

	/** Is capture initialized? */
	bool IsReady() const { return bInitialized; }

	/** Get capture dimensions */
	void GetDimensions(int32& OutWidth, int32& OutHeight) const
	{
		OutWidth = Width;
		OutHeight = Height;
	}

	/** Get statistics string */
	FString GetStatsString() const;

private:
	//----------------------------------------------------------
	// Internal Helpers
	//----------------------------------------------------------

	/**
	 * Internal implementation for capturing from render target.
	 * Called by CaptureFrame() after validation.
	 *
	 * @param RenderTarget - Source render target
	 * @param OutFrame - Captured frame
	 * @return True if captured successfully
	 */
	bool CaptureFromRenderTarget(UTextureRenderTarget2D* RenderTarget, FCaptureFrame& OutFrame);

	/**
	 * Copy texture from source to pooled frame (GPU only).
	 * Must be called on render thread.
	 *
	 * @param RHICmdList - RHI command list
	 * @param SourceTexture - Source texture (from render target)
	 * @param DestTexture - Destination texture (from frame pool)
	 */
	void CopyTextureGPU(FRHICommandListImmediate& RHICmdList,
	                    FRHITexture* SourceTexture,
	                    FRHITexture* DestTexture);

	/**
	 * Insert GPU fence to signal when texture copy completes.
	 * Must be called on render thread after CopyTextureGPU.
	 *
	 * @param RHICmdList - RHI command list
	 * @param OutFence - Created GPU fence
	 */
	void InsertGPUFence(FRHICommandListImmediate& RHICmdList, FGPUFenceRHIRef& OutFence);

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Capture width */
	int32 Width = 0;

	/** Capture height */
	int32 Height = 0;

	/** Frame pool (shared with encoder, but isolated per stream!) */
	TSharedPtr<FFramePool> FramePool;

	//----------------------------------------------------------
	// State
	//----------------------------------------------------------

	/** Is capture initialized? */
	std::atomic<bool> bInitialized{false};

	/** Is capture shutting down? */
	std::atomic<bool> bShuttingDown{false};

	/** Frame counter */
	std::atomic<uint64> FrameCounter{0};

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total frames captured */
	std::atomic<uint64> TotalFramesCaptured{0};

	/** Total capture failures */
	std::atomic<uint64> CaptureFailureCount{0};

	/** Last capture timestamp */
	double LastCaptureTime = 0.0;

	/** Mutex for thread safety */
	mutable FCriticalSection CaptureMutex;
};
