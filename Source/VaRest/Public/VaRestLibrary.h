// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "VaRestTypes.h"

#include "VaRestLibrary.generated.h"

class UVaRestSettings;
class UVaRestJsonObject;

/**
 * Useful tools for REST communications
 */
UCLASS()
class VAREST_API UVaRestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	//////////////////////////////////////////////////////////////////////////
	// Data Accessors
public:
	/** Direct access to the plugin settings */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Common")
	static UVaRestSettings* GetVaRestSettings();

	//////////////////////////////////////////////////////////////////////////
	// Helpers

public:
	/** Applies percent-encoding to text */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility")
	static FString PercentEncode(const FString& Source);

	/**
	 * Encodes a FString into a Base64 string
	 *
	 * @param Source	The string data to convert
	 * @return			A string that encodes the binary data in a way that can be safely transmitted via various Internet protocols
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64 Encode"))
	static FString Base64Encode(const FString& Source);

	/**
	 * Decodes a Base64 string into a FString
	 *
	 * @param Source	The stringified data to convert
	 * @param Dest		The out buffer that will be filled with the decoded data
	 * @return			True if the buffer was decoded, false if it failed to decode
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64 Decode"))
	static bool Base64Decode(const FString& Source, FString& Dest);

	/**
	 * Encodes a byte array into a Base64 string
	 *
	 * @param Dara		The data to convert
	 * @return			A string that encodes the binary data in a way that can be safely transmitted via various Internet protocols
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64 Encode Data"))
	static bool Base64EncodeData(const TArray<uint8>& Data, FString& Dest);

	/**
	 * Decodes a Base64 string into a byte array
	 *
	 * @param Source	The stringified data to convert
	 * @param Dest		The out buffer that will be filled with the decoded data
	 * @return			True if the buffer was decoded, false if it failed to decode
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64 Decode Data"))
	static bool Base64DecodeData(const FString& Source, TArray<uint8>& Dest);

	/**
	 * Helper to perform the very common case of hashing an ASCII string into a hex representation.
	 *
	 * @param String	Hex representation of the hash (32 lower-case hex digits)
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "String to MD5"))
	static FString StringToMd5(const FString& StringToHash);

	/**
	 * Helper to perform the SHA1 hash operation on string.
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "String to SHA1"))
	static FString StringToSha1(const FString& StringToHash);

	/**
	 * Helper method to convert a status code from HTTP to an enum for easier readability
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "HTTP Status Int To Enum"))
	static FORCEINLINE EVaRestHttpStatusCode::Type HTTPStatusIntToEnum(int32 StatusCode) { return (EVaRestHttpStatusCode::Type)StatusCode; }

	/**
	 * Get the plugin's version
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Get VaRest Version"))
	static FString GetVaRestVersion();

	//////////////////////////////////////////////////////////////////////////
	// Common Network Helpers

public:
	/**
	 * Get the URL that was used when loading this World
	 */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (WorldContext = "WorldContextObject"))
	static FVaRestURL GetWorldURL(UObject* WorldContextObject);

	//////////////////////////////////////////////////////////////////////////
	// OAuth / JWT primitives (RFC 4648 §5, 6749, 7519, 7636)
	//
	// These are stateless helpers — no token storage, no refresh loops, no
	// signature verification. Callers own the token lifecycle.

public:
	/** Base64URL encode the UTF-8 bytes of Source (no padding). */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64Url Encode"))
	static FString Base64UrlEncode(const FString& Source);

	/** Base64URL decode to a UTF-8 string. Returns true on success. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64Url Decode"))
	static bool Base64UrlDecode(const FString& Source, FString& Dest);

	/** Base64URL encode raw bytes (no padding). */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64Url Encode Data"))
	static FString Base64UrlEncodeData(const TArray<uint8>& Data);

	/** Base64URL decode to raw bytes. Returns true on success. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Base64Url Decode Data"))
	static bool Base64UrlDecodeData(const FString& Source, TArray<uint8>& Dest);

	/** SHA-256 over the UTF-8 bytes of StringToHash. Returns 64 lower-case hex digits. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "String to SHA256"))
	static FString StringToSha256(const FString& StringToHash);

	/** SHA-256 over a byte array. Returns 64 lower-case hex digits. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|Utility", meta = (DisplayName = "Bytes to SHA256"))
	static FString BytesToSha256(const TArray<uint8>& Data);

	/** Split a JWT into its three parts. HeaderJson and PayloadJson are Base64URL-decoded UTF-8; SignatureBase64Url is the raw third segment. Returns false on malformed token. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "Get JWT Segments"))
	static bool GetJwtSegments(const FString& Token, FString& HeaderJson, FString& PayloadJson, FString& SignatureBase64Url);

	/** Decode and parse the JWT header into a JSON object (nullptr on failure). Does NOT verify the signature. */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|OAuth", meta = (DisplayName = "Decode JWT Header"))
	static UVaRestJsonObject* DecodeJwtHeader(const FString& Token);

	/** Decode and parse the JWT payload into a JSON object (nullptr on failure). Does NOT verify the signature. */
	UFUNCTION(BlueprintCallable, Category = "VaRestX|OAuth", meta = (DisplayName = "Decode JWT Payload"))
	static UVaRestJsonObject* DecodeJwtPayload(const FString& Token);

	/** True if the JWT 'exp' claim is in the past by more than LeewaySeconds. False if the claim is missing or token is malformed. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "Is JWT Expired"))
	static bool IsJwtExpired(const FString& Token, int32 LeewaySeconds = 0);

	/** Generate a PKCE code verifier per RFC 7636 (43–128 unreserved chars). Length is clamped to that range. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "Generate PKCE Verifier"))
	static FString GeneratePkceVerifier(int32 Length = 64);

	/** Compute the S256 PKCE challenge: Base64URL(SHA256(verifier)). */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "PKCE Challenge S256"))
	static FString PkceChallengeS256(const FString& Verifier);

	/** Build an OAuth authorization URL by appending percent-encoded query parameters to Endpoint. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "Build Authorization URL"))
	static FString BuildAuthorizationUrl(const FString& Endpoint, const TMap<FString, FString>& Params);

	/** Parse an application/x-www-form-urlencoded string (e.g. token endpoint response or redirect query) into a map. A leading '?' is tolerated. */
	UFUNCTION(BlueprintPure, Category = "VaRestX|OAuth", meta = (DisplayName = "Parse Form URL Encoded"))
	static TMap<FString, FString> ParseFormUrlEncoded(const FString& Body);
};
