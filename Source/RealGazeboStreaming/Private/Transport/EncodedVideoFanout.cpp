// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Transport/EncodedVideoFanout.h"
#include "Transport/STANAG4609Sink.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FEncodedVideoFanout::FEncodedVideoFanout()
{
	FString Host;
	int32 Port = 0;
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Value(Cmd, TEXT("RealGazeboStanagHost="), Host) &&
		FParse::Value(Cmd, TEXT("RealGazeboStanagPort="), Port) &&
		!Host.IsEmpty() && Port > 0 && Port <= 65535)
	{
		AddSink(MakeShared<FSTANAG4609Sink>(Host, Port));
		UE_LOG(LogTemp, Log, TEXT("EncodedVideoFanout: STANAG output configured for udp://%s:%d"), *Host, Port);
	}
}

void FEncodedVideoFanout::AddSink(const TSharedRef<IEncodedVideoSink>& Sink)
{
	FScopeLock Lock(&Mutex);
	for (const TSharedRef<IEncodedVideoSink>& Existing : Sinks)
	{
		if (Existing->GetName().Equals(Sink->GetName(), ESearchCase::IgnoreCase))
		{
			return;
		}
	}
	Sinks.Add(Sink);
}

void FEncodedVideoFanout::RemoveSink(const FString& SinkName)
{
	FScopeLock Lock(&Mutex);
	for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
	{
		if (Sinks[Index]->GetName().Equals(SinkName, ESearchCase::IgnoreCase))
		{
			Sinks[Index]->Stop();
			Sinks.RemoveAt(Index);
		}
	}
}

void FEncodedVideoFanout::Clear()
{
	StopAll();
	FScopeLock Lock(&Mutex);
	Sinks.Empty();
}

bool FEncodedVideoFanout::StartAll(FString& OutErrorMessage)
{
	FScopeLock Lock(&Mutex);
	for (const TSharedRef<IEncodedVideoSink>& Sink : Sinks)
	{
		FString SinkError;
		if (!Sink->Start(SinkError))
		{
			OutErrorMessage = FString::Printf(TEXT("Failed to start encoded-video sink '%s': %s"), *Sink->GetName(), *SinkError);
			return false;
		}
	}
	return true;
}

void FEncodedVideoFanout::StopAll()
{
	FScopeLock Lock(&Mutex);
	for (const TSharedRef<IEncodedVideoSink>& Sink : Sinks)
	{
		Sink->Stop();
	}
}

bool FEncodedVideoFanout::WantsFrames() const
{
	FScopeLock Lock(&Mutex);
	for (const TSharedRef<IEncodedVideoSink>& Sink : Sinks)
	{
		if (Sink->WantsFrames())
		{
			return true;
		}
	}
	return false;
}

void FEncodedVideoFanout::Push(const TArray<FEncodedNALUnit>& NALUnits, const FEncodedVideoMetadata& Metadata)
{
	TArray<TSharedRef<IEncodedVideoSink>> Snapshot;
	{
		FScopeLock Lock(&Mutex);
		Snapshot = Sinks;
	}
	for (const TSharedRef<IEncodedVideoSink>& Sink : Snapshot)
	{
		if (Sink->WantsFrames())
		{
			Sink->PushEncodedVideo(NALUnits, Metadata);
		}
	}
}

int32 FEncodedVideoFanout::NumSinks() const
{
	FScopeLock Lock(&Mutex);
	return Sinks.Num();
}
