// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Pipeline/FrameCapture.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "HAL/PlatformTime.h"

//----------------------------------------------------------
// Construction & Initialization
//----------------------------------------------------------

FFrameCapture::FFrameCapture(int32 InWidth, int32 InHeight, TSharedPtr<FFramePool> InFramePool)
	: Width(InWidth)
	, Height(InHeight)
	, FramePool(InFramePool)
{
	check(Width > 0 && Height > 0);
	check(FramePool.IsValid());

	UE_LOG(LogTemp, Log, TEXT("FrameCapture: Created capture %dx%d"), Width, Height);
}

FFrameCapture::~FFrameCapture()
{
	Shutdown();
}

bool FFrameCapture::Initialize()
{
	if (bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Already initialized"));
		return true;
	}

	if (!FramePool || !FramePool->IsReady())
	{
		UE_LOG(LogTemp, Error, TEXT("FrameCapture: Frame pool not ready"));
		return false;
	}

	// Verify dimensions match frame pool
	int32 PoolWidth, PoolHeight;
	FramePool->GetDimensions(PoolWidth, PoolHeight);
	if (PoolWidth != Width || PoolHeight != Height)
	{
		UE_LOG(LogTemp, Error, TEXT("FrameCapture: Dimension mismatch - Capture: %dx%d, Pool: %dx%d"),
			Width, Height, PoolWidth, PoolHeight);
		return false;
	}

	bInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("FrameCapture: Initialized successfully %dx%d"), Width, Height);
	return true;
}

void FFrameCapture::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("FrameCapture: Shutting down..."));
	bShuttingDown = true;

	// Wait for any pending render thread operations
	FlushRenderingCommands();

	bInitialized = false;
	bShuttingDown = false;

	UE_LOG(LogTemp, Log, TEXT("FrameCapture: Shutdown complete (Captured: %llu, Failures: %llu)"),
		TotalFramesCaptured.load(), CaptureFailureCount.load());
}

//----------------------------------------------------------
// Frame Capture
//----------------------------------------------------------

bool FFrameCapture::CaptureFrame(USceneCaptureComponent2D* SceneCapture, FCaptureFrame& OutFrame)
{
	if (!bInitialized || bShuttingDown)
	{
		CaptureFailureCount++;
		return false;
	}

	if (!SceneCapture || !SceneCapture->TextureTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Invalid scene capture or render target"));
		CaptureFailureCount++;
		return false;
	}

	return CaptureFromRenderTarget(SceneCapture->TextureTarget, OutFrame);
}

bool FFrameCapture::CaptureFromRenderTarget(UTextureRenderTarget2D* RenderTarget, FCaptureFrame& OutFrame)
{
	if (!bInitialized || bShuttingDown)
	{
		CaptureFailureCount++;
		return false;
	}

	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Null render target"));
		CaptureFailureCount++;
		return false;
	}

	// Acquire pooled frame
	TSharedPtr<FPooledFrame> PooledFrame;
	if (!FramePool->AcquireFrame(PooledFrame, 0.1))
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Failed to acquire frame from pool"));
		CaptureFailureCount++;
		return false;
	}

	// Prepare output frame
	OutFrame.PooledFrame = PooledFrame;
	OutFrame.Timestamp = FPlatformTime::Seconds();
	OutFrame.FrameNumber = FrameCounter++;

	// Get render target resource using game-thread-safe accessor
	// CRITICAL: GetRenderTargetResource() must be called from render thread
	// Use GameThread_GetRenderTargetResource() from game thread instead
	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Render target resource not available"));
		FramePool->ReleaseFrame(PooledFrame);
		CaptureFailureCount++;
		return false;
	}

	FRHITexture* DestTextureRHI = PooledFrame->Texture;

	// Create GPU fence on game thread BEFORE the lambda
	// CRITICAL: We must create fence here and capture by value, NOT capture OutFrame pointer
	// because ENQUEUE_RENDER_COMMAND is async and OutFrame may be invalid when lambda runs
	if (!OutFrame.GPUFence)
	{
		OutFrame.GPUFence = RHICreateGPUFence(TEXT("FrameCaptureFence"));
	}
	FGPUFenceRHIRef Fence = OutFrame.GPUFence;  // Copy ref-counted pointer for lambda capture

	// Capture texture on render thread
	ENQUEUE_RENDER_COMMAND(CaptureFrameToTexture)(
		[this, RenderTargetResource, DestTextureRHI, Fence]
		(FRHICommandListImmediate& RHICmdList)
		{
			// Get source RHI texture on render thread (safe here)
			FRHITexture* SourceTextureRHI = RenderTargetResource->GetRenderTargetTexture();
			if (!SourceTextureRHI)
			{
				UE_LOG(LogTemp, Warning, TEXT("FrameCapture: Source texture not available on render thread"));
				return;
			}

			// Copy source render target → destination pool texture (GPU only)
			CopyTextureGPU(RHICmdList, SourceTextureRHI, DestTextureRHI);

			// Write GPU fence to signal when copy completes
			// Fence was created on game thread and captured by value (ref-counted)
			if (Fence)
			{
				RHICmdList.WriteGPUFence(Fence);
			}
		}
	);

	LastCaptureTime = OutFrame.Timestamp;
	TotalFramesCaptured++;

	return true;
}

//----------------------------------------------------------
// Internal Helpers
//----------------------------------------------------------

void FFrameCapture::CopyTextureGPU(FRHICommandListImmediate& RHICmdList,
                                   FRHITexture* SourceTexture,
                                   FRHITexture* DestTexture)
{
	if (!SourceTexture || !DestTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("FrameCapture: Null texture in CopyTextureGPU"));
		return;
	}

	// Transition source to Copy Source
	RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));

	// Transition dest to Copy Dest
	RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest));

	// Copy texture (GPU only - no CPU involvement)
	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(Width, Height, 1);
	RHICmdList.CopyTexture(SourceTexture, DestTexture, CopyInfo);

	// Transition dest to SRV for encoder
	RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
}

void FFrameCapture::InsertGPUFence(FRHICommandListImmediate& RHICmdList, FGPUFenceRHIRef& OutFence)
{
	// Create GPU fence if not exists
	if (!OutFence)
	{
		OutFence = RHICreateGPUFence(TEXT("FrameCaptureFence"));
	}

	// Insert fence into command stream
	// Fence will signal when all prior GPU commands complete
	RHICmdList.WriteGPUFence(OutFence);
}

//----------------------------------------------------------
// Statistics
//----------------------------------------------------------

FString FFrameCapture::GetStatsString() const
{
	const double CurrentTime = FPlatformTime::Seconds();
	const double TimeSinceLastCapture = CurrentTime - LastCaptureTime;

	return FString::Printf(
		TEXT("FrameCapture: %dx%d, Captured=%llu, Failures=%llu, LastCapture=%.3fs ago"),
		Width, Height,
		TotalFramesCaptured.load(),
		CaptureFailureCount.load(),
		TimeSinceLastCapture
	);
}
