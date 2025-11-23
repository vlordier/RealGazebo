// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
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
 * Represents a captured frame ready for encoding.
 * Contains pooled texture + GPU fence for synchronization.
 */
struct FCaptureFrame
{
	/** Pooled frame from the frame pool */
	TSharedPtr<FPooledFrame> PooledFrame;

	/** GPU fence for synchronization (per-stream, not shared!) */
	FGPUFenceRHIRef GPUFence;

	/** Frame timestamp (game thread time) */
	double Timestamp = 0.0;

	/** Frame number */
	uint64 FrameNumber = 0;

	/** Is frame ready for encoding? (GPU fence signaled) */
	bool IsReady() const
	{
		return PooledFrame && PooledFrame->IsValid() && GPUFence && GPUFence->Poll();
	}

	/** Wait for GPU fence (blocks until render thread completes) */
	void WaitForGPU()
	{
		if (GPUFence)
		{
			// In UE 5.1, use Poll() in a loop to wait
			while (!GPUFence->Poll())
			{
				FPlatformProcess::Sleep(0.001f); // 1ms sleep
			}
		}
	}
};

/**
 * FFrameCapture
 *
 * Captures camera view to GPU texture for zero-copy encoding.
 * Each stream has its own isolated capture instance with dedicated GPU fence.
 *
 * CRITICAL: Never share capture instances between streams!
 *
 * Pipeline:
 * 1. SceneCaptureComponent2D renders to RenderTarget
 * 2. CopyTexture() transfers RenderTarget → PooledFrame texture (GPU only)
 * 3. InsertGPUFence() signals when copy completes
 * 4. Encoder waits for fence before encoding
 *
 * Zero-Copy Guarantee:
 * - All operations on GPU
 * - No CPU readback
 * - No intermediate buffers
 * - Direct RHI texture → Encoder texture
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
	 * Capture current frame from SceneCaptureComponent2D to GPU texture.
	 *
	 * @param SceneCapture - The UE scene capture component
	 * @param OutFrame - Captured frame (valid if returned true)
	 * @return True if frame captured successfully
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
