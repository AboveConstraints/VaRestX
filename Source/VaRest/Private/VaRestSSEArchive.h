// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

class UVaRestRequestJSON;

/**
 * Internal FArchive plumbed into IHttpRequest::SetResponseBodyReceiveStream.
 * Receives bytes on the HTTP worker thread, parses Server-Sent Events,
 * and dispatches each completed event to the owning request on the game thread.
 *
 * Not exposed to Blueprints; users observe events via the UVaRestRequestJSON delegates.
 */
class FVaRestSSEArchive : public FArchive
{
public:
	explicit FVaRestSSEArchive(UVaRestRequestJSON* InOwner);

	virtual void Serialize(void* Data, int64 Length) override;
	virtual int64 Tell() override { return TotalReceived; }
	virtual int64 TotalSize() override { return TotalReceived; }
	virtual FString GetArchiveName() const override { return TEXT("FVaRestSSEArchive"); }

private:
	void DrainBuffer();
	void DispatchEvent(const FString& EventType, const FString& Data, const FString& Id);

	TWeakObjectPtr<UVaRestRequestJSON> Owner;
	TArray<uint8> Buffer;
	int64 TotalReceived = 0;
};
