// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#include "VaRestSSEArchive.h"

#include "VaRestRequestJSON.h"

#include "Async/Async.h"

FVaRestSSEArchive::FVaRestSSEArchive(UVaRestRequestJSON* InOwner)
	: Owner(InOwner)
{
	SetIsSaving(true);
	SetIsPersistent(false);
}

void FVaRestSSEArchive::Serialize(void* Data, int64 Length)
{
	if (Length <= 0 || Data == nullptr)
	{
		return;
	}

	TotalReceived += Length;
	Buffer.Append(static_cast<const uint8*>(Data), Length);
	DrainBuffer();
}

void FVaRestSSEArchive::DrainBuffer()
{
	// SSE events are separated by a blank line (\n\n or \r\n\r\n).
	// Walk the buffer looking for terminators, parse each completed event, then keep the tail.
	int32 ScanFrom = 0;
	int32 EventStart = 0;

	auto FindBoundary = [this](int32 From, int32& OutBoundaryStart, int32& OutNextStart) -> bool
	{
		const int32 Num = Buffer.Num();
		for (int32 i = From; i < Num - 1; ++i)
		{
			if (Buffer[i] == '\n' && Buffer[i + 1] == '\n')
			{
				OutBoundaryStart = i;
				OutNextStart = i + 2;
				return true;
			}
			if (i < Num - 3 && Buffer[i] == '\r' && Buffer[i + 1] == '\n' && Buffer[i + 2] == '\r' && Buffer[i + 3] == '\n')
			{
				OutBoundaryStart = i;
				OutNextStart = i + 4;
				return true;
			}
		}
		return false;
	};

	int32 BoundaryStart = 0;
	int32 NextStart = 0;
	while (FindBoundary(ScanFrom, BoundaryStart, NextStart))
	{
		const int32 EventLen = BoundaryStart - EventStart;
		if (EventLen > 0)
		{
			const FUTF8ToTCHAR Chunk(reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + EventStart), EventLen);
			const FString EventBlock(Chunk.Length(), Chunk.Get());

			FString EventType;
			FString DataAccum;
			FString Id;

			TArray<FString> Lines;
			EventBlock.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
			for (const FString& Line : Lines)
			{
				if (Line.IsEmpty() || Line.StartsWith(TEXT(":")))
				{
					continue; // comment or blank
				}

				FString Field, Value;
				if (!Line.Split(TEXT(":"), &Field, &Value))
				{
					Field = Line;
					Value = FString();
				}
				if (Value.StartsWith(TEXT(" ")))
				{
					Value.RemoveAt(0, 1, EAllowShrinking::No);
				}

				if (Field == TEXT("data"))
				{
					if (!DataAccum.IsEmpty())
					{
						DataAccum.AppendChar(TEXT('\n'));
					}
					DataAccum.Append(Value);
				}
				else if (Field == TEXT("event"))
				{
					EventType = Value;
				}
				else if (Field == TEXT("id"))
				{
					Id = Value;
				}
				// retry: ignored — reconnect is out of scope
			}

			if (!DataAccum.IsEmpty() || !EventType.IsEmpty())
			{
				DispatchEvent(EventType, DataAccum, Id);
			}
		}

		EventStart = NextStart;
		ScanFrom = NextStart;
	}

	// Drop consumed bytes, keep the partial tail
	if (EventStart > 0)
	{
		Buffer.RemoveAt(0, EventStart, EAllowShrinking::No);
	}
}

void FVaRestSSEArchive::DispatchEvent(const FString& EventType, const FString& Data, const FString& Id)
{
	TWeakObjectPtr<UVaRestRequestJSON> WeakOwner = Owner;
	AsyncTask(ENamedThreads::GameThread, [WeakOwner, EventType, Data, Id]()
	{
		if (UVaRestRequestJSON* Strong = WeakOwner.Get())
		{
			Strong->BroadcastStreamEvent(EventType, Data, Id);
		}
	});
}
