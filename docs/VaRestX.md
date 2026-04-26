# VaRestX

REST / HTTP / JSON for Unreal Engine, exposed to Blueprints. Drop a request node, point it at a URL, parse the response as a `JsonObject`. Built on top of UE's `HTTP` and `Json` modules.

- **Engine:** UE 5.7
- **Modules:** `VaRest` (Runtime), `VaRestEditor` (UncookedOnly)
- **License:** see `LICENSE`

> Module names remain `VaRest` / `VaRestEditor` for backward compatibility with existing Blueprints, asset references, and `DefaultEngine.ini` entries from previous installs. Only the plugin display name and settings page are branded as **VaRestX**.

---

## Quick start

```
1. Construct request          → Construct VaRest Request (Subsystem)
2. Set verb / content type    → Set Verb (POST), Set Content Type (json)
3. Build JSON body            → Set String/Number/Bool/Object Field
4. (optional) Auth            → Set Bearer Token  /  Set Basic Auth
5. (optional) Timeout         → Set Timeout (e.g. 10s)
6. Bind events                → On Request Complete / On Request Fail
7. Fire                       → Process URL ("https://api.example.com/x")
```

For one-shot fire-and-forget requests use `UVaRestSubsystem::CallURL`.

---

## Settings

Project Settings → Plugins → **VaRestX**.

| Setting | Default | Notes |
|---|---|---|
| Extended Log | off | When on, request body / URL params are logged. Leave off in shipping builds — security risk if requests carry secrets. |
| Use Chunked Parser | off | Custom incremental JSON parser. Lower memory footprint but has known issues with hex-encoded UTF‑8. Default UE parser is recommended. |

---

## Core API

### `UVaRestRequestJSON`

The main request object. Construct via `UVaRestSubsystem::ConstructVaRestRequest` (or the `Ext` variant that pre-fills verb/content type).

#### Configuration

| Method | Description |
|---|---|
| `SetVerb(EVaRestRequestVerb)` | GET / POST / PUT / DELETE / CUSTOM |
| `SetCustomVerb(FString)` | Used when verb is `CUSTOM` |
| `SetContentType(EVaRestRequestContentType)` | `x_www_form_urlencoded_url`, `x_www_form_urlencoded_body`, `json`, `binary`, `multipart_form_data` |
| `SetBinaryContentType(FString)` | For `binary` requests, e.g. `"image/png"` |
| `SetBinaryRequestContent(TArray<uint8>)` | Raw payload for `binary` |
| `SetStringRequestContent(FString)` | Raw string payload, applied for `urlencoded` |
| `SetHeader(FString, FString)` | Arbitrary header |
| `SetTimeout(float Seconds)` | Per-request timeout. Pass `0` to clear and use engine default. |
| `SetRequestObject(UVaRestJsonObject*)` | Use an existing JSON object as the request body |

#### Authentication shortcuts

```cpp
Request->SetBearerToken(TEXT("eyJhbGci..."));
// → Authorization: Bearer eyJhbGci...

Request->SetBasicAuth(TEXT("alice"), TEXT("hunter2"));
// → Authorization: Basic YWxpY2U6aHVudGVyMg==
```

Both are thin wrappers over `SetHeader`. `SetBasicAuth` Base64-encodes the UTF‑8 of `user:password`.

#### Multipart uploads

For file uploads with mixed text / binary fields:

```cpp
Request->SetContentType(EVaRestRequestContentType::multipart_form_data);
Request->AddMultipartTextField(TEXT("description"), TEXT("My screenshot"));
Request->AddMultipartFileField(
    TEXT("file"),
    TEXT("screenshot.png"),
    PNGBytes,
    TEXT("image/png")
);
Request->ProcessURL(TEXT("https://api.example.com/upload"));
```

Each request gets a fresh GUID-derived boundary. `AddMultipartFileField` defaults the content type to `application/octet-stream` if you pass an empty string.

#### Tags

```cpp
Request->AddTag(TEXT("auth"));
// later, anywhere in the project:
int32 N = UVaRestRequestJSON::CancelRequestsByTag(TEXT("auth"));
```

| Method | Description |
|---|---|
| `AddTag(FName)` | Tag a request for later lookup |
| `RemoveTag(FName)` / `HasTag(FName)` | Manage tags |
| `static GetRequestsByTag(FName)` | Returns every in-flight request with the tag |
| `static CancelRequestsByTag(FName)` | Cancels every in-flight request with the tag, returns count |

In-flight requests are auto-registered in `ProcessRequest` and pruned on completion or cancel.

#### Lifecycle

| Method | Description |
|---|---|
| `ProcessURL(FString)` | Fire request against URL |
| `ApplyURL(...)` | Latent variant returning the JSON object via execution pin |
| `ExecuteProcessRequest()` | Fire using the URL set previously via `SetURL`; rejects empty URL |
| `Cancel()` | Cancels the underlying HTTP request and resets response data |
| `ResetData()` | Resets request and response data |
| `ResetRequestData(bool bClearHeaders = false)` | Resets request body. **Headers are preserved by default** for backward compatibility. Pass `true` to wipe them too. |
| `ResetResponseData()` | Resets response data |

#### Events

| Event | When |
|---|---|
| `OnRequestComplete` | Fires when request finishes successfully |
| `OnRequestFail` | Fires on transport failure or non-recoverable error |
| `OnRequestProgress(Request, BytesSent, BytesReceived)` | Fires periodically during the request, on the game thread |
| `OnStreamEvent(Request, EventType, Data, Id)` | Fires per parsed SSE event when streaming is enabled |
| `OnStreamChunk(Request, Data)` | Convenience: fires per SSE `data:` payload regardless of event type |

#### Response

| Method | Description |
|---|---|
| `GetURL()` / `GetVerb()` / `GetStatus()` | Inspect the underlying request |
| `GetResponseCode()` | HTTP status code |
| `GetResponseHeader(FString)` / `GetAllResponseHeaders()` | Read headers |
| `GetResponseObject()` / `GetResponseValue()` | Parsed JSON |
| `GetResponseContentLength()` / `GetResponseContent()` | Raw bytes |
| `GetResponseContentAsString(bool bCache = true)` | Stringified body, cached by default |
| `SaveResponseToFile(FString Path)` | Save raw response payload to disk; falls back to UTF‑8 string for non-binary responses |

---

### `UVaRestJsonObject`

Blueprint-friendly wrapper around UE's `FJsonObject`.

#### Field access

Standard typed getters/setters:

```
GetStringField / GetNumberField / GetBoolField / GetObjectField / GetArrayField
SetStringField / SetNumberField / SetBoolField / SetObjectField / SetArrayField
```

#### Dot-path access

For deeply nested API responses:

```cpp
// JSON: { "user": { "profile": { "name": "Ada" } } }
UVaRestJsonValue* Name = ResponseJson->GetFieldByPath(TEXT("user.profile.name"));
// → string value "Ada", or nullptr if any segment is missing or not an object
```

Replaces chains of `GetObjectField → GetObjectField → GetStringField` in Blueprints.

#### Serialization

| Method | Description |
|---|---|
| `EncodeJson()` | Pretty-printed string |
| `EncodeJsonToSingleString()` | Condensed (no line breaks) |
| `DecodeJson(FString, bool bUseIncrementalParser = true)` | Parse from string |

---

## Streaming (Server-Sent Events)

For APIs that stream responses incrementally — model token streaming, progress feeds, log tailing — VaRestX exposes SSE without you having to touch the UE HTTP module directly.

### Enabling

```cpp
Request->SetVerb(EVaRestRequestVerb::POST);
Request->SetContentType(EVaRestRequestContentType::json);
Request->SetStreamResponse(true);            // ← enable streaming
Request->SetBearerToken(MyToken);
// fill the JSON request body...

Request->OnStreamChunk.AddDynamic(this, &UMyComponent::HandleChunk);
Request->OnRequestComplete.AddDynamic(this, &UMyComponent::HandleEnd);

Request->ProcessURL(TEXT("https://api.example.com/v1/stream"));
```

When `SetStreamResponse(true)` is on, VaRestX:

- Adds `Accept: text/event-stream` and `Cache-Control: no-cache` (only if you haven't set them yourself).
- Installs an internal `FArchive` on the HTTP request that parses SSE events as bytes arrive on the worker thread.
- Dispatches each parsed event to the **game thread** via `AsyncTask`, so delegate handlers can safely touch UMG, spawn actors, etc.
- Skips the bulk JSON parse at completion (body was already consumed by the archive).

### What you get

- **`OnStreamEvent(Request, EventType, Data, Id)`** — full SSE event. `EventType` is whatever the server sent in `event:` (often empty for default `message`). `Id` is the `id:` field if present.
- **`OnStreamChunk(Request, Data)`** — convenience that fires for every event with non-empty `data:`. Use this if you don't care about event types.
- **`OnRequestComplete`** — fires when the stream closes. Treat it as the "stream ended" signal.
- **`OnRequestFail`** — fires on transport error.

### Notes

- The `Data` payload is delivered as-is (UTF‑8 decoded to `FString`). If the server sends JSON, call `Subsystem->DecodeJsonValue(Data)` from the handler. VaRestX does not auto-parse, since some endpoints emit plain text or sentinels like `[DONE]`.
- Multi-line `data:` fields are joined with `\n` per the SSE spec.
- Comments (lines starting with `:`) and the `retry:` field are ignored.
- Auto-reconnect with `Last-Event-ID` is intentionally **not** implemented — re-call `ProcessRequest` yourself if the stream drops and you want to resume.

---

## `UVaRestSubsystem` (Engine subsystem)

One-shot helper, JSON construction, file loading.

| Method | Description |
|---|---|
| `CallURL(URL, Verb, ContentType, JSON, Callback)` | Fire-and-forget. Subsystem owns the request lifetime and routes the result to a `FVaRestCallDelegate`. |
| `ConstructVaRestRequest()` / `ConstructVaRestRequestExt(Verb, ContentType)` | Make a request you keep a reference to |
| `ConstructVaRestJsonObject()` | Empty object |
| `ConstructJsonValue*` | Typed value helpers (number / string / bool / array / object) |
| `DecodeJsonObject(FString)` / `DecodeJsonValue(FString)` | Parse a string into objects |
| `LoadJsonFromFile(Path, bIsRelativeToContentDir)` | Load JSON from disk |

---

## `UVaRestLibrary` (BP function library)

| Function | Notes |
|---|---|
| `GetVaRestSettings()` | Direct access to the settings UObject |
| `PercentEncode(FString)` | URL encoding via `FGenericPlatformHttp` |
| `Base64Encode(FString)` / `Base64Decode(FString, FString&)` | UTF‑8 round-trip |
| `Base64EncodeData(TArray<uint8>, FString&)` / `Base64DecodeData(...)` | Binary variants |
| `StringToMd5(FString)` | MD5 (ASCII input — for non-ASCII, MD5 is computed over the ANSI-converted bytes; do not use for security purposes) |
| `StringToSha1(FString)` | SHA‑1 over UTF‑8 bytes (Unicode-safe) |
| `HTTPStatusIntToEnum(int32)` | Cast HTTP status code to readable enum |
| `GetVaRestVersion()` | Plugin version string |
| `GetWorldURL(WorldContext)` | Wraps the active `FURL` |

> **Hashing note.** Both MD5 and SHA‑1 are cryptographically broken. Use them for non-security purposes (cache keys, deduplication, content hashes). For HMAC-style authentication, use SHA‑256 or stronger.

### OAuth / JWT primitives

Stateless helpers for OAuth 2.0 (RFC 6749), JWT inspection (RFC 7519), and PKCE (RFC 7636). VaRestX deliberately does not handle token storage, refresh loops, or signature verification — those are app-level concerns and lock you into a specific flow / key-management strategy.

| Function | Notes |
|---|---|
| `Base64UrlEncode` / `Base64UrlDecode` | UTF‑8 round-trip with the URL-safe alphabet (RFC 4648 §5), no padding |
| `Base64UrlEncodeData` / `Base64UrlDecodeData` | Binary variants |
| `StringToSha256(FString)` / `BytesToSha256(TArray<uint8>)` | SHA‑256 over UTF‑8 / raw bytes; returns 64 hex digits |
| `GetJwtSegments(Token, Header&, Payload&, Sig&)` | Splits a JWT and Base64URL-decodes the first two segments to UTF‑8 strings. No signature verification. |
| `DecodeJwtHeader(Token)` / `DecodeJwtPayload(Token)` | Returns a `UVaRestJsonObject*` (nullptr on malformed input). No signature verification. |
| `IsJwtExpired(Token, LeewaySeconds = 0)` | True if `exp` is in the past beyond the leeway. False if `exp` is missing or token is malformed. |
| `GeneratePkceVerifier(Length = 64)` | Random verifier in `[A-Z][a-z][0-9]-_`. Length is clamped to 43–128. |
| `PkceChallengeS256(Verifier)` | `Base64URL(SHA256(verifier))` |
| `BuildAuthorizationUrl(Endpoint, Params)` | Appends percent-encoded `Params` as a query string. Tolerates an `Endpoint` that already has a `?`. |
| `ParseFormUrlEncoded(Body)` | Parses an `application/x-www-form-urlencoded` string (token endpoint response, redirect query) into a map. Leading `?` is stripped. |

> **Verification.** `DecodeJwt*` does not check the signature. Treat decoded claims as untrusted unless your transport guarantees authenticity (e.g. you fetched the token from your own server over TLS). For RS256/ES256 verification with JWKS rotation, integrate a dedicated library.

---

## Backward compatibility

Existing projects upgrading from earlier VaRest / VaRestX versions:

- **Module names** (`VaRest`, `VaRestEditor`) and **C++ class names** (`UVaRestJsonObject`, `UVaRestRequestJSON`, …) are unchanged. Saved Blueprint references to `class'VaRest.VaRestJsonObject'` continue to load.
- **Settings INI path** (`/Script/VaRest.VaRestSettings`) is unchanged. Existing `DefaultEngine.ini` values carry over.
- `EVaRestRequestContentType::multipart_form_data` was **appended** at the end of the enum. Indices for existing values (`x_www_form_urlencoded_url`, `…_body`, `json`, `binary`) are unchanged, so saved property values remain valid.
- `ResetRequestData()` defaults to **preserving** `RequestHeaders`, matching the prior behavior. Opt in to header clearing with `ResetRequestData(true)`.
- `Cancel()` now calls the underlying `IHttpRequest::CancelRequest()` in addition to resetting response data. Previously the in-flight request was left running.

If your `.uproject` enables the plugin under the legacy name `"VaRest"`, change it to `"VaRestX"`. There is no shim for this — UE matches by `.uplugin` filename.

---

## Examples

### POST JSON with auth and timeout

```cpp
auto* Subsystem = GEngine->GetEngineSubsystem<UVaRestSubsystem>();
auto* Req = Subsystem->ConstructVaRestRequestExt(
    EVaRestRequestVerb::POST,
    EVaRestRequestContentType::json
);

Req->SetBearerToken(MyToken);
Req->SetTimeout(10.f);
Req->GetRequestObject()->SetStringField(TEXT("query"), TEXT("hello"));

Req->OnRequestComplete.AddDynamic(this, &UMyComponent::OnSearchDone);
Req->OnRequestFail.AddDynamic(this, &UMyComponent::OnSearchFail);

Req->ProcessURL(TEXT("https://api.example.com/v1/search"));
```

### Upload an image

```cpp
TArray<uint8> PNG;
FFileHelper::LoadFileToArray(PNG, *PngPath);

auto* Req = Subsystem->ConstructVaRestRequest();
Req->SetVerb(EVaRestRequestVerb::POST);
Req->SetContentType(EVaRestRequestContentType::multipart_form_data);
Req->AddMultipartTextField(TEXT("title"), TEXT("Avatar"));
Req->AddMultipartFileField(TEXT("file"), TEXT("avatar.png"), PNG, TEXT("image/png"));

Req->OnRequestProgress.AddDynamic(this, &UMyComponent::OnUploadProgress);
Req->ProcessURL(TEXT("https://api.example.com/v1/avatars"));
```

### Cancel all in-flight auth requests on logout

```cpp
UVaRestRequestJSON::CancelRequestsByTag(TEXT("auth"));
```

### Stream model tokens into a UMG widget

```cpp
auto* Req = Subsystem->ConstructVaRestRequestExt(
    EVaRestRequestVerb::POST,
    EVaRestRequestContentType::json
);
Req->SetBearerToken(ApiKey);
Req->SetStreamResponse(true);
Req->GetRequestObject()->SetBoolField(TEXT("stream"), true);
// ... add prompt fields ...

Req->OnStreamChunk.AddDynamic(this, &UChatWidget::AppendDelta);
Req->OnRequestComplete.AddDynamic(this, &UChatWidget::OnStreamEnded);

Req->ProcessURL(TEXT("https://api.example.com/v1/messages"));
```

`UChatWidget::AppendDelta` is invoked on the game thread for each chunk; safe to update text widgets directly.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `Request execution attempt with empty URL` error | `ExecuteProcessRequest` called without `SetURL` first. Use `ProcessURL(URL)` instead. |
| Logs say `(enable Project Settings → Plugins → VaRestX → Extended Log to see body)` | Working as intended — that's the actionable hint to enable verbose request logging. |
| Streaming returns no events | Server may not be sending `text/event-stream`. Verify with `OnRequestComplete` — `GetResponseCode()` should be 200 and `Content-Type` should contain `event-stream`. |
| Unicode strings hash differently than legacy code expected | `StringToSha1` now hashes UTF‑8 bytes. Old code hashed ANSI-mangled bytes (lossy). Re-compute stored hashes once. |
| Reused request sends old auth header | Old behavior preserved by default. To wipe, use `ResetRequestData(true)`. |

---

## File map

```
Source/
├── VaRest/
│   ├── Public/
│   │   ├── VaRestRequestJSON.h     ← request object, delegates, stream API
│   │   ├── VaRestJsonObject.h      ← JSON object wrapper, dot-path getter
│   │   ├── VaRestJsonValue.h
│   │   ├── VaRestSubsystem.h       ← engine subsystem entrypoint
│   │   ├── VaRestLibrary.h         ← BP function library (encoding, hashing)
│   │   ├── VaRestSettings.h
│   │   └── VaRestTypes.h           ← enums (verb, content type, status)
│   └── Private/
│       ├── VaRestSSEArchive.h/.cpp ← internal SSE parser (worker thread)
│       └── ...
└── VaRestEditor/
    ├── Public/VaRest_BreakJson.h   ← Make/Break Json BP nodes
    └── Private/...
```
