// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#include "VaRestLibrary.h"

#include "VaRestX.h"
#include "VaRestDefines.h"
#include "VaRestJsonObject.h"
#include "VaRestRequestJSON.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
// Self-contained SHA-256 (FIPS 180-4). Avoids coupling to engine-version-specific hash classes.
void Sha256Hash(const uint8* Data, int32 Len, uint8 OutHash[32])
{
	static const uint32 K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

	uint32 H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

	const uint64 BitLen = (uint64)Len * 8;
	TArray<uint8> Buf;
	Buf.Reserve(Len + 72);
	Buf.Append(Data, Len);
	Buf.Add(0x80);
	while (Buf.Num() % 64 != 56)
		Buf.Add(0);
	for (int32 i = 7; i >= 0; --i)
		Buf.Add((uint8)(BitLen >> (i * 8)));

	auto Rot = [](uint32 x, uint32 n) { return (x >> n) | (x << (32 - n)); };

	for (int32 Block = 0; Block < Buf.Num(); Block += 64)
	{
		uint32 W[64];
		for (int32 i = 0; i < 16; ++i)
		{
			W[i] = ((uint32)Buf[Block + i * 4] << 24) | ((uint32)Buf[Block + i * 4 + 1] << 16) | ((uint32)Buf[Block + i * 4 + 2] << 8) | ((uint32)Buf[Block + i * 4 + 3]);
		}
		for (int32 i = 16; i < 64; ++i)
		{
			const uint32 s0 = Rot(W[i - 15], 7) ^ Rot(W[i - 15], 18) ^ (W[i - 15] >> 3);
			const uint32 s1 = Rot(W[i - 2], 17) ^ Rot(W[i - 2], 19) ^ (W[i - 2] >> 10);
			W[i] = W[i - 16] + s0 + W[i - 7] + s1;
		}

		uint32 a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
		for (int32 i = 0; i < 64; ++i)
		{
			const uint32 S1 = Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25);
			const uint32 ch = (e & f) ^ ((~e) & g);
			const uint32 t1 = h + S1 + ch + K[i] + W[i];
			const uint32 S0 = Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22);
			const uint32 mj = (a & b) ^ (a & c) ^ (b & c);
			const uint32 t2 = S0 + mj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		H[0] += a;
		H[1] += b;
		H[2] += c;
		H[3] += d;
		H[4] += e;
		H[5] += f;
		H[6] += g;
		H[7] += h;
	}

	for (int32 i = 0; i < 8; ++i)
	{
		OutHash[i * 4] = (uint8)(H[i] >> 24);
		OutHash[i * 4 + 1] = (uint8)(H[i] >> 16);
		OutHash[i * 4 + 2] = (uint8)(H[i] >> 8);
		OutHash[i * 4 + 3] = (uint8)(H[i]);
	}
}

FString VaRestBytesToHexLower(const uint8* Data, int32 Len)
{
	FString Out;
	Out.Reserve(Len * 2);
	for (int32 i = 0; i < Len; ++i)
	{
		Out += FString::Printf(TEXT("%02x"), Data[i]);
	}
	return Out;
}

FString ToBase64Url(const TArray<uint8>& Data)
{
	FString S = FBase64::Encode(Data);
	S.ReplaceCharInline(TEXT('+'), TEXT('-'));
	S.ReplaceCharInline(TEXT('/'), TEXT('_'));
	while (S.EndsWith(TEXT("=")))
	{
		S.LeftChopInline(1);
	}
	return S;
}

bool FromBase64Url(const FString& In, TArray<uint8>& Out)
{
	FString S = In;
	S.ReplaceCharInline(TEXT('-'), TEXT('+'));
	S.ReplaceCharInline(TEXT('_'), TEXT('/'));
	const int32 Mod = S.Len() % 4;
	if (Mod == 2)
	{
		S += TEXT("==");
	}
	else if (Mod == 3)
	{
		S += TEXT("=");
	}
	else if (Mod == 1)
	{
		return false;
	}
	return FBase64::Decode(S, Out);
}
} // namespace

UVaRestSettings* UVaRestLibrary::GetVaRestSettings()
{
	return FVaRestModule::Get().GetSettings();
}

FString UVaRestLibrary::PercentEncode(const FString& Source)
{
	return FGenericPlatformHttp::UrlEncode(Source);
}

FString UVaRestLibrary::Base64Encode(const FString& Source)
{
	const FTCHARToUTF8 Utf8(*Source);
	return FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

bool UVaRestLibrary::Base64Decode(const FString& Source, FString& Dest)
{
	TArray<uint8> ByteArray;
	const bool Success = FBase64::Decode(Source, ByteArray);

	const FUTF8ToTCHAR StringSrc = FUTF8ToTCHAR((const ANSICHAR*)ByteArray.GetData(), ByteArray.Num());
	Dest = FString();
	Dest.AppendChars(StringSrc.Get(), StringSrc.Length());

	return Success;
}

bool UVaRestLibrary::Base64EncodeData(const TArray<uint8>& Data, FString& Dest)
{
	if (Data.Num() > 0)
	{
		Dest = FBase64::Encode(Data);
		return true;
	}

	Dest = FString();
	return false;
}

bool UVaRestLibrary::Base64DecodeData(const FString& Source, TArray<uint8>& Dest)
{
	return FBase64::Decode(Source, Dest);
}

FString UVaRestLibrary::StringToMd5(const FString& StringToHash)
{
	return FMD5::HashAnsiString(*StringToHash);
}

FString UVaRestLibrary::StringToSha1(const FString& StringToHash)
{
	FSHA1 Sha1Gen;

	const FTCHARToUTF8 Utf8(*StringToHash);
	Sha1Gen.Update((const uint8*)Utf8.Get(), Utf8.Length());
	Sha1Gen.Final();

	FString Sha1String;
	for (int32 i = 0; i < 20; i++)
	{
		Sha1String += FString::Printf(TEXT("%02x"), Sha1Gen.m_digest[i]);
	}

	return Sha1String;
}

FString UVaRestLibrary::GetVaRestVersion()
{
	auto PluginRef = IPluginManager::Get().FindPlugin("VaRestX");
	if (!PluginRef.IsValid())
	{
		// Fall back to the legacy plugin name so installations that still ship as "VaRest" report a version.
		PluginRef = IPluginManager::Get().FindPlugin("VaRest");
	}

	return !PluginRef.IsValid() ? FString("invalid") : PluginRef->GetDescriptor().VersionName;
}

FVaRestURL UVaRestLibrary::GetWorldURL(UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (UWorld* World = WorldContextObject->GetWorld())
		{
			return FVaRestURL(World->URL);
		}
	}

	return FVaRestURL();
}

//////////////////////////////////////////////////////////////////////////
// OAuth / JWT primitives

FString UVaRestLibrary::Base64UrlEncode(const FString& Source)
{
	const FTCHARToUTF8 Utf8(*Source);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return ToBase64Url(Bytes);
}

bool UVaRestLibrary::Base64UrlDecode(const FString& Source, FString& Dest)
{
	TArray<uint8> Bytes;
	if (!FromBase64Url(Source, Bytes))
	{
		Dest = FString();
		return false;
	}
	const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	Dest = FString();
	Dest.AppendChars(Conv.Get(), Conv.Length());
	return true;
}

FString UVaRestLibrary::Base64UrlEncodeData(const TArray<uint8>& Data)
{
	if (Data.Num() == 0)
	{
		return FString();
	}
	return ToBase64Url(Data);
}

bool UVaRestLibrary::Base64UrlDecodeData(const FString& Source, TArray<uint8>& Dest)
{
	return FromBase64Url(Source, Dest);
}

FString UVaRestLibrary::StringToSha256(const FString& StringToHash)
{
	const FTCHARToUTF8 Utf8(*StringToHash);
	uint8 Hash[32];
	Sha256Hash(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Hash);
	return VaRestBytesToHexLower(Hash, 32);
}

FString UVaRestLibrary::BytesToSha256(const TArray<uint8>& Data)
{
	uint8 Hash[32];
	Sha256Hash(Data.GetData(), Data.Num(), Hash);
	return VaRestBytesToHexLower(Hash, 32);
}

bool UVaRestLibrary::GetJwtSegments(const FString& Token, FString& HeaderJson, FString& PayloadJson, FString& SignatureBase64Url)
{
	HeaderJson.Reset();
	PayloadJson.Reset();
	SignatureBase64Url.Reset();

	TArray<FString> Parts;
	Token.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 3)
	{
		return false;
	}

	TArray<uint8> HeaderBytes;
	TArray<uint8> PayloadBytes;
	if (!FromBase64Url(Parts[0], HeaderBytes) || !FromBase64Url(Parts[1], PayloadBytes))
	{
		return false;
	}

	auto BytesToUtf8 = [](const TArray<uint8>& B) {
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(B.GetData()), B.Num());
		FString S;
		S.AppendChars(Conv.Get(), Conv.Length());
		return S;
	};

	HeaderJson = BytesToUtf8(HeaderBytes);
	PayloadJson = BytesToUtf8(PayloadBytes);
	SignatureBase64Url = Parts[2];
	return true;
}

namespace
{
UVaRestJsonObject* DecodeJwtSegmentToJson(const FString& Token, bool bPayload)
{
	FString HeaderJson, PayloadJson, Sig;
	if (!UVaRestLibrary::GetJwtSegments(Token, HeaderJson, PayloadJson, Sig))
	{
		return nullptr;
	}
	UVaRestJsonObject* Obj = NewObject<UVaRestJsonObject>();
	if (!Obj->DecodeJson(bPayload ? PayloadJson : HeaderJson))
	{
		return nullptr;
	}
	return Obj;
}
} // namespace

UVaRestJsonObject* UVaRestLibrary::DecodeJwtHeader(const FString& Token)
{
	return DecodeJwtSegmentToJson(Token, false);
}

UVaRestJsonObject* UVaRestLibrary::DecodeJwtPayload(const FString& Token)
{
	return DecodeJwtSegmentToJson(Token, true);
}

bool UVaRestLibrary::IsJwtExpired(const FString& Token, int32 LeewaySeconds)
{
	FString HeaderJson, PayloadJson, Sig;
	if (!GetJwtSegments(Token, HeaderJson, PayloadJson, Sig))
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return false;
	}

	double ExpSeconds = 0.0;
	if (!JsonObj->TryGetNumberField(TEXT("exp"), ExpSeconds))
	{
		return false;
	}

	const int64 NowSec = FDateTime::UtcNow().ToUnixTimestamp();
	return static_cast<int64>(ExpSeconds) + LeewaySeconds < NowSec;
}

FString UVaRestLibrary::GeneratePkceVerifier(int32 Length)
{
	Length = FMath::Clamp(Length, 43, 128);

	TArray<uint8> Bytes;
	Bytes.Reserve(Length + 16);
	while (Bytes.Num() < Length)
	{
		const FGuid G = FGuid::NewGuid();
		Bytes.Append(reinterpret_cast<const uint8*>(&G), sizeof(FGuid));
	}

	FString S = ToBase64Url(Bytes);
	if (S.Len() > Length)
	{
		S.LeftInline(Length);
	}
	return S;
}

FString UVaRestLibrary::PkceChallengeS256(const FString& Verifier)
{
	const FTCHARToUTF8 Utf8(*Verifier);
	uint8 Hash[32];
	Sha256Hash(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Hash);
	TArray<uint8> Arr;
	Arr.Append(Hash, 32);
	return ToBase64Url(Arr);
}

FString UVaRestLibrary::BuildAuthorizationUrl(const FString& Endpoint, const TMap<FString, FString>& Params)
{
	FString Url = Endpoint;
	bool bFirst = !Endpoint.Contains(TEXT("?"));
	for (const auto& Pair : Params)
	{
		Url += bFirst ? TEXT("?") : TEXT("&");
		bFirst = false;
		Url += FGenericPlatformHttp::UrlEncode(Pair.Key);
		Url += TEXT("=");
		Url += FGenericPlatformHttp::UrlEncode(Pair.Value);
	}
	return Url;
}

TMap<FString, FString> UVaRestLibrary::ParseFormUrlEncoded(const FString& Body)
{
	TMap<FString, FString> Out;
	FString Trimmed = Body;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.StartsWith(TEXT("?")))
	{
		Trimmed.RightChopInline(1);
	}

	TArray<FString> Pairs;
	Trimmed.ParseIntoArray(Pairs, TEXT("&"), true);
	for (const FString& Pair : Pairs)
	{
		FString Key, Value;
		if (Pair.Split(TEXT("="), &Key, &Value))
		{
			Out.Add(FGenericPlatformHttp::UrlDecode(Key), FGenericPlatformHttp::UrlDecode(Value));
		}
		else if (!Pair.IsEmpty())
		{
			Out.Add(FGenericPlatformHttp::UrlDecode(Pair), FString());
		}
	}
	return Out;
}
