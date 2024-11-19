// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#pragma once

#include "Engine/LatentActionManager.h"
#include "Http.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "LatentActions.h"

#include "VaRestTypes.h"

#include "VaRestRequestJSON.generated.h"

class UVaRestJsonValue;
class UVaRestJsonObject;
class UVaRestSettings;

/**
 * @author Original latent action class by https://github.com/unktomi
 */
template <class T>
class FVaRestLatentAction : public FPendingLatentAction
{
public:
	virtual void Call(const T& Value)
	{
		Result = Value;
		Called = true;
	}

	void operator()(const T& Value)
	{
		Call(Value);
	}

	void Cancel();

	FVaRestLatentAction(FWeakObjectPtr RequestObj, T& ResultParam, const FLatentActionInfo& LatentInfo)
		: Called(false)
		, Request(RequestObj)
		, ExecutionFunction(LatentInfo.ExecutionFunction)
		, OutputLink(LatentInfo.Linkage)
		, CallbackTarget(LatentInfo.CallbackTarget)
		, Result(ResultParam)
	{
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		Response.FinishAndTriggerIf(Called, ExecutionFunction, OutputLink, CallbackTarget);
	}

	virtual void NotifyObjectDestroyed()
	{
		Cancel();
	}

	virtual void NotifyActionAborted()
	{
		Cancel();
	}

private:
	bool Called;
	FWeakObjectPtr Request;

public:
	const FName ExecutionFunction;
	const int32 OutputLink;
	const FWeakObjectPtr CallbackTarget;
	T& Result;
};

/** Generate a delegates for callback events */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRequestComplete, class UVaRestRequestJSON*, Request);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRequestFail, class UVaRestRequestJSON*, Request);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnStaticRequestComplete, class UVaRestRequestJSON*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStaticRequestFail, class UVaRestRequestJSON*);

/**
 * General helper class http requests via blueprints
 */
UCLASS(BlueprintType, Blueprintable)
class VAREST_API UVaRestRequestJSON : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	//////////////////////////////////////////////////////////////////////////
	// Construction

	/** Set verb to the request */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetVerb(EVaRestRequestVerb Verb);

	/** Set custom verb to the request */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetCustomVerb(FString Verb);

	/** Set content type to the request. If you're using the x-www-form-urlencoded,
	 * params/constaints should be defined as key=ValueString pairs from Json data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetContentType(EVaRestRequestContentType ContentType);

	/** Set content type of the request for binary post data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetBinaryContentType(const FString& ContentType);

	/** Set content of the request for binary post data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetBinaryRequestContent(const TArray<uint8>& Content);

	/** Set content of the request as a plain string */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetStringRequestContent(const FString& Content);

	/** Sets optional header info */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetHeader(const FString& HeaderName, const FString& HeaderValue);

	//////////////////////////////////////////////////////////////////////////
	// Destruction and reset

	/** Reset all internal saved data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Utility")
	void ResetData();

	/** Reset saved request data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void ResetRequestData();

	/** Reset saved response data */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	void ResetResponseData();

	/** Cancel latent response waiting  */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	void Cancel();

	//////////////////////////////////////////////////////////////////////////
	// JSON data accessors

	/** Get the Request Json object */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	UVaRestJsonObject* GetRequestObject() const;

	/** Set the Request Json object */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetRequestObject(UVaRestJsonObject* JsonObject);

	/** Get the Response Json object */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	UVaRestJsonObject* GetResponseObject() const;

	/** Set the Response Json object */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	void SetResponseObject(UVaRestJsonObject* JsonObject);

	/** Get the Response Json value */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	UVaRestJsonValue* GetResponseValue() const;

	///////////////////////////////////////////////////////////////////////////
	// Request/response data access

	/** Get url of http request */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Request")
	FString GetURL() const;

	/** Get verb to the request */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Request")
	EVaRestRequestVerb GetVerb() const;

	/** Get status of http request */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Request")
	EVaRestRequestStatus GetStatus() const;

	/** Get the response code of the last query */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Response")
	int32 GetResponseCode() const;

	/** Get value of desired response header */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Response")
	FString GetResponseHeader(const FString& HeaderName);

	/** Get list of all response headers */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Response")
	TArray<FString> GetAllResponseHeaders() const;

	/**
	 * Shortcut to get the Content-Length header value. Will not always return non-zero.
	 * If you want the real length of the payload, get the payload and check it's length.
	 *
	 * @return the content length (if available)
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Response")
	int32 GetResponseContentLength() const;

	/**
	 * Get the content payload of the request or response.
	 *
	 * @param Content - array that will be filled with the content.
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Response")
	const TArray<uint8>& GetResponseContent() const;

	//////////////////////////////////////////////////////////////////////////
	// URL processing

public:
	/** Setting request URL */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	void SetURL(const FString& Url = TEXT("https://xcibe95x.com/VaRestX.json"));

	/** Open URL with current setup */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	virtual void ProcessURL(const FString& Url = TEXT("https://xcibe95x.com/VaRestX.json"));

	/** Open URL in latent mode */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request", meta = (Latent, LatentInfo = "LatentInfo", HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	virtual void ApplyURL(const FString& Url, UVaRestJsonObject*& Result, UObject* WorldContextObject, struct FLatentActionInfo LatentInfo);

	/** Check URL and execute process request */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Request")
	virtual void ExecuteProcessRequest();

protected:
	/** Apply current internal setup to request and process it */
	void ProcessRequest();

	//////////////////////////////////////////////////////////////////////////
	// Request callbacks

private:
	/** Internal bind function for the IHTTPRequest::OnProcessRequestCompleted() event */
	void OnProcessRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

public:
	/** Event occured when the request has been completed */
	UPROPERTY(BlueprintAssignable, Category = "VaRestX|Event")
	FOnRequestComplete OnRequestComplete;

	/** Event occured when the request wasn't successfull */
	UPROPERTY(BlueprintAssignable, Category = "VaRestX|Event")
	FOnRequestFail OnRequestFail;

	/** Event occured when the request has been completed */
	FOnStaticRequestComplete OnStaticRequestComplete;

	/** Event occured when the request wasn't successfull */
	FOnStaticRequestFail OnStaticRequestFail;

	//////////////////////////////////////////////////////////////////////////
	// Tags

public:
	/** Add tag to this request */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Utility")
	void AddTag(FName Tag);

	/**
	 * Remove tag from this request
	 *
	 * @return Number of removed elements
	 */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Utility")
	int32 RemoveTag(FName Tag);

	/** See if this request contains the supplied tag */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Utility")
	bool HasTag(FName Tag) const;

protected:
	/** Array of tags that can be used for grouping and categorizing */
	TArray<FName> Tags;

	//////////////////////////////////////////////////////////////////////////
	// Data

public:
	/**
	 * Get request response stored as a string
	 * @param bCacheResponseContent - Set true if you plan to use it few times to prevent deserialization each time
	 */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|Response")
	FString GetResponseContentAsString(bool bCacheResponseContent = true);

public:
	/** Response size */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VaRestX|Response")
	int32 ResponseSize;

	/** DEPRECATED: Please use GetResponseContentAsString() instead */
	// UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VaRestX|Response")
	// FString ResponseContent;

	/** Is the response valid JSON? */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VaRestX|Response")
	bool bIsValidJsonResponse;

protected:
	/** Response json content */
	FString ResponseContent;

	/** Latent action helper */
	FVaRestLatentAction<UVaRestJsonObject*>* ContinueAction;

	/** Internal request data stored as JSON */
	UPROPERTY()
	UVaRestJsonObject* RequestJsonObj;

	TArray<uint8> RequestBytes;
	FString BinaryContentType;

	/** Raw response storage */
	TArray<uint8> ResponseBytes;
	int32 ResponseContentLength;

	/** Used for special cases when used wants to have plain string data in request.
	 * Attn.! Content-type x-www-form-urlencoded only. */
	FString StringRequestContent;


	/** Response data stored as JSON */
	UPROPERTY()
	UVaRestJsonObject* ResponseJsonObj;

	/** Response data stored as JSON value */
	UPROPERTY()
	UVaRestJsonValue* ResponseJsonValue;

	/** Verb for making request (GET,POST,etc) */
	EVaRestRequestVerb RequestVerb;

	/** Content type to be applied for request */
	EVaRestRequestContentType RequestContentType;

	/** Mapping of header section to values. Used to generate final header string for request */
	TMap<FString, FString> RequestHeaders;

	/** Cached key/value header pairs. Parsed once request completes */
	TMap<FString, FString> ResponseHeaders;

	/** Http Response code */
	int32 ResponseCode;

	/** Custom verb that will be used with RequestContentType == CUSTOM */
	FString CustomVerb;

	/** Request we're currently processing */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

public:
	/** Returns reference to internal request object */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> GetHttpRequest() const { return HttpRequest; };
};
