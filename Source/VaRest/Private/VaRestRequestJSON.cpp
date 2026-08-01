// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#include "VaRestRequestJSON.h"

#include "VaRestDefines.h"
#include "VaRestJsonObject.h"
#include "VaRestJsonValue.h"
#include "VaRestLibrary.h"
#include "VaRestSSEArchive.h"
#include "VaRestSettings.h"

#include "Engine/Engine.h"
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TArray<TWeakObjectPtr<UVaRestRequestJSON>> UVaRestRequestJSON::InflightRequests;

template <class T>
void FVaRestLatentAction<T>::Cancel()
{
	UObject* Obj = Request.Get();
	if (Obj != nullptr)
	{
		((UVaRestRequestJSON*)Obj)->Cancel();
	}
}

UVaRestRequestJSON::UVaRestRequestJSON(const class FObjectInitializer& PCIP)
	: Super(PCIP)
	, BinaryContentType(TEXT("application/octet-stream"))
{
	ContinueAction = nullptr;

	RequestVerb = EVaRestRequestVerb::GET;
	RequestContentType = EVaRestRequestContentType::x_www_form_urlencoded_url;

	ResetData();
}

void UVaRestRequestJSON::SetVerb(EVaRestRequestVerb Verb)
{
	RequestVerb = Verb;
}

void UVaRestRequestJSON::SetCustomVerb(FString Verb)
{
	CustomVerb = Verb;
}

void UVaRestRequestJSON::SetContentType(EVaRestRequestContentType ContentType)
{
	RequestContentType = ContentType;
}

void UVaRestRequestJSON::SetBinaryContentType(const FString& ContentType)
{
	BinaryContentType = ContentType;
}

void UVaRestRequestJSON::SetBinaryRequestContent(const TArray<uint8>& Bytes)
{
	RequestBytes = Bytes;
}

void UVaRestRequestJSON::SetStringRequestContent(const FString& Content)
{
	StringRequestContent = Content;
}

void UVaRestRequestJSON::SetHeader(const FString& HeaderName, const FString& HeaderValue)
{
	RequestHeaders.Add(HeaderName, HeaderValue);
}

void UVaRestRequestJSON::SetTimeout(float Seconds)
{
	if (Seconds > 0.f)
	{
		HttpRequest->SetTimeout(Seconds);
	}
	else
	{
		HttpRequest->ClearTimeout();
	}
}

void UVaRestRequestJSON::SetBearerToken(const FString& Token)
{
	SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Token));
}

void UVaRestRequestJSON::SetBasicAuth(const FString& Username, const FString& Password)
{
	const FString Pair = FString::Printf(TEXT("%s:%s"), *Username, *Password);
	const FTCHARToUTF8 Utf8(*Pair);
	const FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Basic %s"), *Encoded));
}

void UVaRestRequestJSON::AddMultipartTextField(const FString& FieldName, const FString& Value)
{
	FMultipartPart Part;
	Part.FieldName = FieldName;
	const FTCHARToUTF8 Utf8(*Value);
	Part.Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	MultipartParts.Add(MoveTemp(Part));
}

void UVaRestRequestJSON::AddMultipartFileField(const FString& FieldName, const FString& FileName, const TArray<uint8>& FileData, const FString& ContentType)
{
	FMultipartPart Part;
	Part.FieldName = FieldName;
	Part.FileName = FileName;
	Part.ContentType = ContentType.IsEmpty() ? TEXT("application/octet-stream") : ContentType;
	Part.Data = FileData;
	MultipartParts.Add(MoveTemp(Part));
}

void UVaRestRequestJSON::SetStreamResponse(bool bEnabled)
{
	bStreamResponse = bEnabled;
}

void UVaRestRequestJSON::BroadcastStreamEvent(const FString& EventType, const FString& Data, const FString& Id)
{
	OnStreamEvent.Broadcast(this, EventType, Data, Id);
	if (!Data.IsEmpty())
	{
		OnStreamChunk.Broadcast(this, Data);
	}
}

//////////////////////////////////////////////////////////////////////////
// Destruction and reset

void UVaRestRequestJSON::ResetData()
{
	ResetRequestData();
	ResetResponseData();
}

void UVaRestRequestJSON::ResetRequestData(bool bClearHeaders)
{
	if (RequestJsonObj != nullptr)
	{
		RequestJsonObj->Reset();
	}
	else
	{
		RequestJsonObj = NewObject<UVaRestJsonObject>();
	}

	RequestBytes.Empty();
	StringRequestContent.Empty();
	MultipartParts.Empty();

	if (bClearHeaders)
	{
		RequestHeaders.Empty();
	}
}

void UVaRestRequestJSON::ResetResponseData()
{
	if (ResponseJsonObj != nullptr)
	{
		ResponseJsonObj->Reset();
	}
	else
	{
		ResponseJsonObj = NewObject<UVaRestJsonObject>();
	}

	if (ResponseJsonValue != nullptr)
	{
		ResponseJsonValue->Reset();
	}
	else
	{
		ResponseJsonValue = NewObject<UVaRestJsonValue>();
	}

	ResponseHeaders.Empty();
	ResponseCode = -1;
	ResponseSize = 0;

	bIsValidJsonResponse = false;

	// #127 Reset response content
	ResponseContent = TEXT("{}");
	bResponseContentCached = false;

	ResponseBytes.Empty();
	ResponseContentLength = 0;
}

void UVaRestRequestJSON::Cancel()
{
	ContinueAction = nullptr;

	if (HttpRequest->GetStatus() == EHttpRequestStatus::Processing)
	{
		HttpRequest->CancelRequest();
	}

	InflightRequests.RemoveAllSwap([this](const TWeakObjectPtr<UVaRestRequestJSON>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == this; });

	StreamArchive.Reset();

	ResetResponseData();
}

//////////////////////////////////////////////////////////////////////////
// JSON data accessors

UVaRestJsonObject* UVaRestRequestJSON::GetRequestObject() const
{
	check(RequestJsonObj);
	return RequestJsonObj;
}

void UVaRestRequestJSON::SetRequestObject(UVaRestJsonObject* JsonObject)
{
	if (JsonObject == nullptr)
	{
		UE_LOG(LogVaRest, Error, TEXT("%s: Provided JsonObject is nullptr"), *VA_FUNC_LINE);
		return;
	}

	RequestJsonObj = JsonObject;
}

UVaRestJsonObject* UVaRestRequestJSON::GetResponseObject() const
{
	check(ResponseJsonObj);
	return ResponseJsonObj;
}

void UVaRestRequestJSON::SetResponseObject(UVaRestJsonObject* JsonObject)
{
	if (JsonObject == nullptr)
	{
		UE_LOG(LogVaRest, Error, TEXT("%s: Provided JsonObject is nullptr"), *VA_FUNC_LINE);
		return;
	}

	ResponseJsonObj = JsonObject;
}

UVaRestJsonValue* UVaRestRequestJSON::GetResponseValue() const
{
	check(ResponseJsonValue);
	return ResponseJsonValue;
}

///////////////////////////////////////////////////////////////////////////
// Response data access

FString UVaRestRequestJSON::GetURL() const
{
	return HttpRequest->GetURL();
}

EVaRestRequestVerb UVaRestRequestJSON::GetVerb() const
{
	return RequestVerb;
}

EVaRestRequestStatus UVaRestRequestJSON::GetStatus() const
{
	return EVaRestRequestStatus((uint8)HttpRequest->GetStatus());
}

int32 UVaRestRequestJSON::GetResponseCode() const
{
	return ResponseCode;
}

FString UVaRestRequestJSON::GetResponseHeader(const FString& HeaderName)
{
	FString Result;

	FString* Header = ResponseHeaders.Find(HeaderName);
	if (Header != nullptr)
	{
		Result = *Header;
	}

	return Result;
}

TArray<FString> UVaRestRequestJSON::GetAllResponseHeaders() const
{
	TArray<FString> Result;
	for (TMap<FString, FString>::TConstIterator It(ResponseHeaders); It; ++It)
	{
		Result.Add(It.Key() + TEXT(": ") + It.Value());
	}
	return Result;
}

int32 UVaRestRequestJSON::GetResponseContentLength() const
{
	return ResponseContentLength;
}

const TArray<uint8>& UVaRestRequestJSON::GetResponseContent() const
{
	return ResponseBytes;
}

bool UVaRestRequestJSON::SaveResponseToFile(const FString& FilePath) const
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogVaRest, Error, TEXT("%s: SaveResponseToFile called with empty path"), *VA_FUNC_LINE);
		return false;
	}

	if (ResponseBytes.Num() > 0)
	{
		return FFileHelper::SaveArrayToFile(ResponseBytes, *FilePath);
	}

	// Fall back to the cached string content (e.g. for non-JSON text responses)
	return FFileHelper::SaveStringToFile(ResponseContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

//////////////////////////////////////////////////////////////////////////
// URL processing

void UVaRestRequestJSON::SetURL(const FString& Url)
{
	// Be sure to trim URL because it can break links on iOS
	FString TrimmedUrl = Url;

	TrimmedUrl.TrimStartInline();
	TrimmedUrl.TrimEndInline();

	HttpRequest->SetURL(TrimmedUrl);
}

void UVaRestRequestJSON::ProcessURL(const FString& Url)
{
	SetURL(Url);
	ProcessRequest();
}

void UVaRestRequestJSON::ApplyURL(const FString& Url, UVaRestJsonObject*& Result, UObject* WorldContextObject, FLatentActionInfo LatentInfo)
{
	// Be sure to trim URL because it can break links on iOS
	FString TrimmedUrl = Url;

	TrimmedUrl.TrimStartInline();
	TrimmedUrl.TrimEndInline();

	HttpRequest->SetURL(TrimmedUrl);

	// Prepare latent action
	if (UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject))
	{
		FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
		FVaRestLatentAction<UVaRestJsonObject*>* Kont = LatentActionManager.FindExistingAction<FVaRestLatentAction<UVaRestJsonObject*>>(LatentInfo.CallbackTarget, LatentInfo.UUID);

		if (Kont != nullptr)
		{
			Kont->Cancel();
			LatentActionManager.RemoveActionsForObject(LatentInfo.CallbackTarget);
		}

		LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID, ContinueAction = new FVaRestLatentAction<UVaRestJsonObject*>(this, Result, LatentInfo));
	}

	ProcessRequest();
}

void UVaRestRequestJSON::ExecuteProcessRequest()
{
	if (HttpRequest->GetURL().Len() == 0)
	{
		UE_LOG(LogVaRest, Error, TEXT("Request execution attempt with empty URL"));
		return;
	}

	ProcessRequest();
}

void UVaRestRequestJSON::ProcessRequest()
{
	// Force add to root once request is launched
	AddToRoot();

	// Set verb
	switch (RequestVerb)
	{
	case EVaRestRequestVerb::GET:
		HttpRequest->SetVerb(TEXT("GET"));
		break;

	case EVaRestRequestVerb::POST:
		HttpRequest->SetVerb(TEXT("POST"));
		break;

	case EVaRestRequestVerb::PUT:
		HttpRequest->SetVerb(TEXT("PUT"));
		break;

	case EVaRestRequestVerb::DEL:
		HttpRequest->SetVerb(TEXT("DELETE"));
		break;

	case EVaRestRequestVerb::CUSTOM:
		HttpRequest->SetVerb(CustomVerb);
		break;

	default:
		break;
	}

	// Set content-type
	switch (RequestContentType)
	{
	case EVaRestRequestContentType::x_www_form_urlencoded_url:
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));

		FString UrlParams = "";
		uint16 ParamIdx = 0;

		// Loop through all the values and prepare additional url part
		for (auto RequestIt = RequestJsonObj->GetRootObject()->Values.CreateIterator(); RequestIt; ++RequestIt)
		{
			FString Key = FString(*RequestIt.Key());
			FString Value = RequestIt.Value().Get()->AsString();

			if (!Key.IsEmpty() && !Value.IsEmpty())
			{
				UrlParams += ParamIdx == 0 ? "?" : "&";
				UrlParams += UVaRestLibrary::PercentEncode(Key) + "=" + UVaRestLibrary::PercentEncode(Value);
			}

			ParamIdx++;
		}

		// Apply params
		HttpRequest->SetURL(HttpRequest->GetURL() + UrlParams);

		// Add optional string content
		if (!StringRequestContent.IsEmpty())
		{
			HttpRequest->SetContentAsString(StringRequestContent);
		}

		// Check extended log to avoid security vulnerability (#133)
		if (UVaRestLibrary::GetVaRestSettings()->bExtendedLog)
		{
			UE_LOG(LogVaRest, Log, TEXT("%s: Request (urlencoded): %s %s %s %s"), *VA_FUNC_LINE, *HttpRequest->GetVerb(), *HttpRequest->GetURL(), *UrlParams, *StringRequestContent);
		}
		else
		{
			UE_LOG(LogVaRest, Log, TEXT("%s: Request (urlencoded): %s %s (enable Project Settings -> Plugins -> VaRestX -> Extended Log to see body)"), *VA_FUNC_LINE, *HttpRequest->GetVerb(), *HttpRequest->GetURL());
		}

		break;
	}
	case EVaRestRequestContentType::x_www_form_urlencoded_body:
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));

		FString UrlParams = "";
		uint16 ParamIdx = 0;

		// Add optional string content
		if (!StringRequestContent.IsEmpty())
		{
			UrlParams = StringRequestContent;
		}
		else
		{
			// Loop through all the values and prepare additional url part
			for (auto RequestIt = RequestJsonObj->GetRootObject()->Values.CreateIterator(); RequestIt; ++RequestIt)
			{
				FString Key = FString(*RequestIt.Key());
				FString Value = RequestIt.Value().Get()->AsString();

				if (!Key.IsEmpty() && !Value.IsEmpty())
				{
					UrlParams += ParamIdx == 0 ? "" : "&";
					UrlParams += UVaRestLibrary::PercentEncode(Key) + "=" + UVaRestLibrary::PercentEncode(Value);
				}

				ParamIdx++;
			}
		}

		// Apply params
		HttpRequest->SetContentAsString(UrlParams);

		// Check extended log to avoid security vulnerability (#133)
		if (UVaRestLibrary::GetVaRestSettings()->bExtendedLog)
		{
			UE_LOG(LogVaRest, Log, TEXT("%s: Request (url body): %s %s %s"), *VA_FUNC_LINE, *HttpRequest->GetVerb(), *HttpRequest->GetURL(), *UrlParams);
		}
		else
		{
			UE_LOG(LogVaRest, Log, TEXT("%s: Request (url body): %s %s (enable Project Settings -> Plugins -> VaRestX -> Extended Log to see body)"), *VA_FUNC_LINE, *HttpRequest->GetVerb(), *HttpRequest->GetURL());
		}

		break;
	}
	case EVaRestRequestContentType::binary:
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), BinaryContentType);
		HttpRequest->SetContent(RequestBytes);

		UE_LOG(LogVaRest, Log, TEXT("Request (binary): %s %s"), *HttpRequest->GetVerb(), *HttpRequest->GetURL());

		break;
	}
	case EVaRestRequestContentType::multipart_form_data:
	{
		const FString Boundary = FString::Printf(TEXT("----VaRestXBoundary%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		HttpRequest->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));

		const FString DashBoundary = FString::Printf(TEXT("--%s\r\n"), *Boundary);
		const FString CloseBoundary = FString::Printf(TEXT("--%s--\r\n"), *Boundary);

		auto AppendString = [](TArray<uint8>& Out, const FString& Str)
		{
			const FTCHARToUTF8 Utf8(*Str);
			Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		};

		TArray<uint8> Body;
		for (const FMultipartPart& Part : MultipartParts)
		{
			AppendString(Body, DashBoundary);
			if (Part.FileName.IsEmpty())
			{
				AppendString(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"\r\n\r\n"), *Part.FieldName));
			}
			else
			{
				AppendString(Body, FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"), *Part.FieldName, *Part.FileName));
				AppendString(Body, FString::Printf(TEXT("Content-Type: %s\r\n\r\n"), *Part.ContentType));
			}
			Body.Append(Part.Data);
			AppendString(Body, TEXT("\r\n"));
		}
		AppendString(Body, CloseBoundary);

		HttpRequest->SetContent(Body);

		UE_LOG(LogVaRest, Log, TEXT("Request (multipart, %d parts): %s %s"), MultipartParts.Num(), *HttpRequest->GetVerb(), *HttpRequest->GetURL());

		break;
	}
	case EVaRestRequestContentType::json:
	{
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

		// Body is ignored for get requests, so we shouldn't place json into it even if it's empty
		if (RequestVerb == EVaRestRequestVerb::GET)
		{
			break;
		}

		// Serialize data to json string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(RequestJsonObj->GetRootObject(), Writer);

		// Set Json content
		HttpRequest->SetContentAsString(OutputString);

		if (UVaRestLibrary::GetVaRestSettings()->bExtendedLog)
		{
			UE_LOG(LogVaRest, Log, TEXT("Request (json): %s %s %sJSON(%s%s%s)JSON"), *HttpRequest->GetVerb(), *HttpRequest->GetURL(), LINE_TERMINATOR, LINE_TERMINATOR, *OutputString, LINE_TERMINATOR);
		}
		else
		{
			UE_LOG(LogVaRest, Log, TEXT("Request (json): %s %s (enable Project Settings -> Plugins -> VaRestX -> Extended Log to see body)"), *HttpRequest->GetVerb(), *HttpRequest->GetURL());
		}

		break;
	}

	default:
		break;
	}

	// Apply additional headers
	for (TMap<FString, FString>::TConstIterator It(RequestHeaders); It; ++It)
	{
		HttpRequest->SetHeader(It.Key(), It.Value());
	}

	// SSE streaming setup: install a body-receive archive that parses events as bytes arrive,
	// and add the conventional Accept/Cache-Control headers if the user didn't override them.
	if (bStreamResponse)
	{
		if (!RequestHeaders.Contains(TEXT("Accept")))
		{
			HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
		}
		if (!RequestHeaders.Contains(TEXT("Cache-Control")))
		{
			HttpRequest->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
		}

		StreamArchive = MakeShared<FVaRestSSEArchive, ESPMode::ThreadSafe>(this);
		HttpRequest->SetResponseBodyReceiveStream(StreamArchive.ToSharedRef());
	}

	// Bind event
	HttpRequest->OnProcessRequestComplete().BindUObject(this, &UVaRestRequestJSON::OnProcessRequestComplete);
	HttpRequest->OnRequestProgress64().BindUObject(this, &UVaRestRequestJSON::OnHttpRequestProgress);

	// Track for tag-based queries / cancellation
	InflightRequests.AddUnique(TWeakObjectPtr<UVaRestRequestJSON>(this));

	// Execute the request
	HttpRequest->ProcessRequest();
}

void UVaRestRequestJSON::OnHttpRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	OnRequestProgress.Broadcast(this, static_cast<int64>(BytesSent), static_cast<int64>(BytesReceived));
}

//////////////////////////////////////////////////////////////////////////
// Request callbacks

void UVaRestRequestJSON::OnProcessRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	// Remove from root on completion
	RemoveFromRoot();

	// Drop from inflight registry
	InflightRequests.RemoveAllSwap([this](const TWeakObjectPtr<UVaRestRequestJSON>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == this; });

	// Release the SSE parser (if any). Worker thread is done emitting by now.
	const bool bWasStreaming = StreamArchive.IsValid();
	StreamArchive.Reset();

	// Be sure that we have no data from previous response
	ResetResponseData();

	// Check we have a response and save response code as int32
	if (Response.IsValid())
	{
		ResponseCode = Response->GetResponseCode();
	}

	// Check we have result to process futher
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogVaRest, Error, TEXT("Request failed (%d): %s"), ResponseCode, *Request->GetURL());

		// Broadcast the result event
		OnRequestFail.Broadcast(this);
		OnStaticRequestFail.Broadcast(this);

		return;
	}

#if PLATFORM_DESKTOP
	// Log response state
	UE_LOG(LogVaRest, Log, TEXT("Response (%d): %sJSON(%s%s%s)JSON"), ResponseCode, LINE_TERMINATOR, LINE_TERMINATOR, *Response->GetContentAsString(), LINE_TERMINATOR);
#endif

	// Process response headers
	TArray<FString> Headers = Response->GetAllHeaders();
	for (FString Header : Headers)
	{
		FString Key;
		FString Value;
		if (Header.Split(TEXT(": "), &Key, &Value))
		{
			ResponseHeaders.Add(Key, Value);
		}
	}

	if (bWasStreaming)
	{
		// Streamed body was consumed by the SSE parser — nothing to deserialize.
		ResponseSize = 0;
	}
	else if (UVaRestLibrary::GetVaRestSettings()->bUseChunkedParser)
	{
		// Try to deserialize data to JSON
		const TArray<uint8>& Bytes = Response->GetContent();
		ResponseSize = ResponseJsonObj->DeserializeFromUTF8Bytes((const ANSICHAR*)Bytes.GetData(), Bytes.Num());

		// Log errors
		if (ResponseSize == 0)
		{
			// As we assume it's recommended way to use current class, but not the only one,
			// it will be the warning instead of error
			UE_LOG(LogVaRest, Warning, TEXT("JSON could not be decoded!"));
		}
	}
	else
	{
		// Use default unreal one
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*Response->GetContentAsString());
		TSharedPtr<FJsonValue> OutJsonValue;
		if (FJsonSerializer::Deserialize(Reader, OutJsonValue))
		{
			ResponseJsonValue->SetRootValue(OutJsonValue);

			if (ResponseJsonValue->GetType() == EVaJson::Object)
			{
				ResponseJsonObj->SetRootObject(ResponseJsonValue->GetRootValue()->AsObject());
				ResponseSize = Response->GetContentLength();
			}
		}
	}

	// Decide whether the request was successful
	bIsValidJsonResponse = bWasSuccessful && (ResponseSize > 0);

	if (!bIsValidJsonResponse)
	{
		// Save response data as a string
		ResponseContent = Response->GetContentAsString();
		ResponseSize = ResponseContent.GetAllocatedSize();

		ResponseBytes = Response->GetContent();
		ResponseContentLength = Response->GetContentLength();
	}

	// Broadcast the result events on next tick
	OnRequestComplete.Broadcast(this);
	OnStaticRequestComplete.Broadcast(this);

	// Finish the latent action
	if (ContinueAction)
	{
		FVaRestLatentAction<UVaRestJsonObject*>* K = ContinueAction;
		ContinueAction = nullptr;

		K->Call(ResponseJsonObj);
	}
}

//////////////////////////////////////////////////////////////////////////
// Tags

void UVaRestRequestJSON::AddTag(FName Tag)
{
	if (Tag != NAME_None)
	{
		Tags.AddUnique(Tag);
	}
}

int32 UVaRestRequestJSON::RemoveTag(FName Tag)
{
	return Tags.Remove(Tag);
}

bool UVaRestRequestJSON::HasTag(FName Tag) const
{
	return (Tag != NAME_None) && Tags.Contains(Tag);
}

TArray<UVaRestRequestJSON*> UVaRestRequestJSON::GetRequestsByTag(FName Tag)
{
	TArray<UVaRestRequestJSON*> Result;
	if (Tag == NAME_None)
	{
		return Result;
	}

	for (const TWeakObjectPtr<UVaRestRequestJSON>& WeakReq : InflightRequests)
	{
		if (UVaRestRequestJSON* Req = WeakReq.Get())
		{
			if (Req->HasTag(Tag))
			{
				Result.Add(Req);
			}
		}
	}
	return Result;
}

int32 UVaRestRequestJSON::CancelRequestsByTag(FName Tag)
{
	TArray<UVaRestRequestJSON*> Targets = GetRequestsByTag(Tag);
	for (UVaRestRequestJSON* Req : Targets)
	{
		Req->Cancel();
	}
	return Targets.Num();
}

//////////////////////////////////////////////////////////////////////////
// Data

FString UVaRestRequestJSON::GetResponseContentAsString(bool bCacheResponseContent)
{
	// Check we have valid json response
	if (!bIsValidJsonResponse)
	{
		// We've cached response content in OnProcessRequestComplete()
		return ResponseContent;
	}

	// Check we have valid response object
	if (!ResponseJsonObj || !ResponseJsonObj->IsValidLowLevel())
	{
		// Discard previous cached string if we had one
		ResponseContent = TEXT("{}");
		bResponseContentCached = false;

		return TEXT("Invalid response");
	}

	// Check if we should re-genetate it in runtime
	if (!bCacheResponseContent)
	{
		UE_LOG(LogVaRest, Verbose, TEXT("%s: Use of uncached getter could be slow"), *VA_FUNC_LINE);
		return ResponseJsonObj->EncodeJson();
	}

	// Check that we haven't cached content yet
	if (!bResponseContentCached)
	{
		UE_LOG(LogVaRest, Verbose, TEXT("%s: Response content string is cached"), *VA_FUNC_LINE);
		ResponseContent = ResponseJsonObj->EncodeJson();
		bResponseContentCached = true;
	}

	// Return previously cached content now
	return ResponseContent;
}
