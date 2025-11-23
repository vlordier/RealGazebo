// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Pipeline/FramePool.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "HAL/PlatformTime.h"

//----------------------------------------------------------
// Construction & Initialization
//----------------------------------------------------------

FFramePool::FFramePool(int32 InWidth, int32 InHeight, int32 InPoolSize)
	: Width(InWidth)
	, Height(InHeight)
	, PoolSize(FMath::Clamp(InPoolSize, 2, 8)) // Min 2, max 8 frames
{
	check(Width > 0 && Height > 0);
	check(PoolSize > 0);

	UE_LOG(LogTemp, Log, TEXT("FramePool: Created pool %dx%d with %d frames"), Width, Height, PoolSize);
}

FFramePool::~FFramePool()
{
	Shutdown();
}

bool FFramePool::Initialize()
{
	if (bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("FramePool: Already initialized"));
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("FramePool: Initializing pool %dx%d..."), Width, Height);

	// Pre-allocate frame array
	Frames.Reserve(PoolSize);

	// Create all frames on render thread
	bool bSuccess = true;
	for (int32 i = 0; i < PoolSize; ++i)
	{
		TSharedPtr<FPooledFrame> Frame = CreateFrame();
		if (Frame && Frame->IsValid())
		{
			Frames.Add(Frame);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("FramePool: Failed to create frame %d/%d"), i + 1, PoolSize);
			bSuccess = false;
			break;
		}
	}

	if (bSuccess && Frames.Num() == PoolSize)
	{
		bInitialized = true;
		UE_LOG(LogTemp, Log, TEXT("FramePool: Initialized successfully with %d frames (%.2f MB total)"),
			Frames.Num(), GetTotalMemoryUsage() / (1024.0 * 1024.0));
		return true;
	}

	// Cleanup on failure
	UE_LOG(LogTemp, Error, TEXT("FramePool: Initialization failed"));
	Shutdown();
	return false;
}

void FFramePool::Shutdown()
{
	if (!bInitialized && Frames.Num() == 0)
	{
		return; // Already shut down
	}

	UE_LOG(LogTemp, Log, TEXT("FramePool: Shutting down pool..."));
	bShuttingDown = true;

	// Wait for all frames to be released (timeout after 1 second)
	const double StartTime = FPlatformTime::Seconds();
	while (GetNumFramesInUse() > 0)
	{
		if (FPlatformTime::Seconds() - StartTime > 1.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("FramePool: Timeout waiting for frames to be released (%d still in use)"),
				GetNumFramesInUse());
			break;
		}
		FPlatformProcess::Sleep(0.001f); // 1ms
	}

	// Release all frames on render thread
	ENQUEUE_RENDER_COMMAND(ReleaseFramePool)(
		[Frames = MoveTemp(Frames)](FRHICommandListImmediate& RHICmdList)
		{
			// Frames will be automatically released when TSharedPtr goes out of scope
			// RHI resources will be released on render thread
		}
	);

	Frames.Empty();
	bInitialized = false;
	bShuttingDown = false;

	UE_LOG(LogTemp, Log, TEXT("FramePool: Shutdown complete (Acquired: %llu, Released: %llu, Blocks: %llu)"),
		TotalFramesAcquired.load(), TotalFramesReleased.load(), AcquireBlockCount.load());
}

//----------------------------------------------------------
// Internal Helpers
//----------------------------------------------------------

TSharedPtr<FPooledFrame> FFramePool::CreateFrame()
{
	TSharedPtr<FPooledFrame> Frame = MakeShared<FPooledFrame>();
	Frame->Width = Width;
	Frame->Height = Height;
	Frame->FrameNumber = 0;
	Frame->bInUse = false;

	// Create RHI texture on render thread (blocking call)
	ENQUEUE_RENDER_COMMAND(CreatePoolTexture)(
		[Frame, Width = this->Width, Height = this->Height](FRHICommandListImmediate& RHICmdList)
		{
			// Create 2D texture for GPU encoding
			// - Format: PF_B8G8R8A8 (standard RGBA8, GPU-friendly)
			// - Flags: TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_External | TexCreate_Shared
			//   - External: Required for CUDA/Vulkan interop (external memory export)
			//   - Shared: Ensures Vulkan allocates with exportable memory handles
			// - NumMips: 1 (no mipmaps needed for video)
			// - NumSamples: 1 (no MSAA)
			FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(TEXT("FramePoolTexture"))
				.SetExtent(Width, Height)
				.SetFormat(PF_B8G8R8A8)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::UAV | ETextureCreateFlags::External | ETextureCreateFlags::Shared)
				.SetInitialState(ERHIAccess::SRVMask);

			Frame->Texture = RHICreateTexture(Desc);

			if (!Frame->Texture.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("FramePool: Failed to create RHI texture %dx%d"), Width, Height);
			}
		}
	);

	// Wait for render thread to complete texture creation
	FlushRenderingCommands();

	if (!Frame->IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("FramePool: Frame creation failed"));
		return nullptr;
	}

	return Frame;
}

TSharedPtr<FPooledFrame> FFramePool::FindFreeFrame()
{
	FScopeLock Lock(&PoolMutex);

	for (TSharedPtr<FPooledFrame>& Frame : Frames)
	{
		if (Frame && !Frame->bInUse.load())
		{
			return Frame;
		}
	}

	return nullptr; // All frames in use
}

//----------------------------------------------------------
// Frame Acquisition & Release
//----------------------------------------------------------

bool FFramePool::AcquireFrame(TSharedPtr<FPooledFrame>& OutFrame, double TimeoutSeconds)
{
	if (!bInitialized || bShuttingDown)
	{
		UE_LOG(LogTemp, Warning, TEXT("FramePool: Cannot acquire frame - pool not ready"));
		OutFrame = nullptr;
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	TSharedPtr<FPooledFrame> Frame = nullptr;

	// Try to find a free frame (with timeout)
	while (!Frame)
	{
		Frame = FindFreeFrame();

		if (!Frame)
		{
			// All frames in use - wait a bit and retry
			if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
			{
				AcquireBlockCount++;
				UE_LOG(LogTemp, Warning, TEXT("FramePool: Acquire timeout - all %d frames in use"), PoolSize);
				OutFrame = nullptr;
				return false;
			}

			FPlatformProcess::Sleep(0.001f); // 1ms sleep before retry
		}
	}

	// Mark frame as in use
	Frame->bInUse = true;
	Frame->FrameNumber = FrameCounter++;
	Frame->AcquireTime = FPlatformTime::Seconds();

	TotalFramesAcquired++;

	OutFrame = Frame;
	return true;
}

void FFramePool::ReleaseFrame(TSharedPtr<FPooledFrame> Frame)
{
	if (!Frame)
	{
		UE_LOG(LogTemp, Warning, TEXT("FramePool: Attempted to release null frame"));
		return;
	}

	if (!Frame->bInUse.load())
	{
		UE_LOG(LogTemp, Warning, TEXT("FramePool: Attempted to release frame that was not in use"));
		return;
	}

	// Mark as free
	Frame->bInUse = false;
	TotalFramesReleased++;
}

//----------------------------------------------------------
// Status & Statistics
//----------------------------------------------------------

int32 FFramePool::GetNumFramesInUse() const
{
	FScopeLock Lock(&PoolMutex);

	int32 Count = 0;
	for (const TSharedPtr<FPooledFrame>& Frame : Frames)
	{
		if (Frame && Frame->bInUse.load())
		{
			Count++;
		}
	}
	return Count;
}

int32 FFramePool::GetNumFramesFree() const
{
	return PoolSize - GetNumFramesInUse();
}

uint64 FFramePool::GetTotalMemoryUsage() const
{
	FScopeLock Lock(&PoolMutex);

	uint64 TotalBytes = 0;
	for (const TSharedPtr<FPooledFrame>& Frame : Frames)
	{
		if (Frame && Frame->IsValid())
		{
			TotalBytes += Frame->GetMemorySize();
		}
	}
	return TotalBytes;
}

FString FFramePool::GetStatsString() const
{
	return FString::Printf(
		TEXT("FramePool: %dx%d, Pool=%d, InUse=%d, Free=%d, Acquired=%llu, Released=%llu, Blocks=%llu, Mem=%.2fMB"),
		Width, Height,
		PoolSize,
		GetNumFramesInUse(),
		GetNumFramesFree(),
		TotalFramesAcquired.load(),
		TotalFramesReleased.load(),
		AcquireBlockCount.load(),
		GetTotalMemoryUsage() / (1024.0 * 1024.0)
	);
}
