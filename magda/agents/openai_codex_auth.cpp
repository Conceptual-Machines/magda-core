#include "openai_codex_auth.hpp"

#include <juce_core/juce_core.h>
#include <windows.h>
#include <wincrypt.h>
#include <cstring>

#pragma comment(lib, "advapi32.lib")

namespace magda::codex_auth {

namespace {

constexpr auto kClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr auto kAuthorizeUrl = "https://auth.openai.com/oauth/authorize";
constexpr auto kTokenUrl = "https://auth.openai.com/oauth/token";
constexpr auto kDeviceCodeUrl = "https://auth.openai.com/oauth/device/code";
constexpr auto kRedirectUri = "http://localhost:1455/auth/callback";
constexpr auto kScope = "openid profile email offline_access";

juce::String postForm(const juce::String& urlString, const juce::StringPairArray& formFields,
                      int& statusCode) {
    juce::StringPairArray headers;
    headers.set("Content-Type", "application/x-www-form-urlencoded");

    juce::String body;
    auto keys = formFields.getAllKeys();
    for (int i = 0; i < keys.size(); ++i) {
        if (i > 0)
            body << "&";
        body << juce::URL::addEscapeChars(keys[i], true) << "="
             << juce::URL::addEscapeChars(formFields[keys[i]], true);
    }

    juce::String headerString;
    for (auto& key : headers.getAllKeys())
        headerString += key + ": " + headers[key] + "\r\n";

    auto url = juce::URL(urlString).withPOSTData(body);
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders(headerString)
                       .withHttpRequestCmd("POST")
                       .withStatusCode(&statusCode)
                       .withConnectionTimeoutMs(15000);

    auto stream = url.createInputStream(options);
    if (stream == nullptr)
        return {};
    return stream->readEntireStreamAsString();
}

juce::Result parseTokenResponse(const juce::String& responseBody, int statusCode, TokenSet& out) {
    if (statusCode < 200 || statusCode >= 300)
        return juce::Result::fail("HTTP " + juce::String(statusCode) + ": " + responseBody);

    auto json = juce::JSON::parse(responseBody);
    auto accessToken = json["access_token"].toString();
    if (accessToken.isEmpty())
        return juce::Result::fail("Token response missing access_token");

    out.accessToken = accessToken;
    out.refreshToken = json["refresh_token"].toString();
    out.idToken = json["id_token"].toString();
    auto expiresIn = static_cast<int64_t>(json["expires_in"]);
    if (expiresIn > 0)
        out.expiresAtUnixSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000 + expiresIn;
    else
        out.expiresAtUnixSeconds = 0;
    return juce::Result::ok();
}

juce::String urlEncode(const juce::String& input) {
    return juce::URL::addEscapeChars(input, true);
}

juce::String base64UrlEncode(const void* data, size_t size) {
    auto encoded = juce::Base64::toBase64(data, size);
    encoded = encoded.replaceCharacter('+', '-').replaceCharacter('/', '_');
    return encoded.trimCharactersAtEnd("=");
}

juce::MemoryBlock sha256Bytes(const void* data, size_t size) {
    juce::MemoryBlock hash;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hashHandle = 0;

    if (!CryptAcquireContext(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return hash;

    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hashHandle)) {
        CryptReleaseContext(provider, 0);
        return hash;
    }

    if (!CryptHashData(hashHandle, static_cast<const BYTE*>(data), static_cast<DWORD>(size), 0)) {
        CryptDestroyHash(hashHandle);
        CryptReleaseContext(provider, 0);
        return hash;
    }

    DWORD hashSize = 0;
    DWORD hashSizeLen = sizeof(hashSize);
    if (!CryptGetHashParam(hashHandle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashSize),
                           &hashSizeLen, 0)) {
        CryptDestroyHash(hashHandle);
        CryptReleaseContext(provider, 0);
        return hash;
    }

    hash.setSize(hashSize, false);
    if (!CryptGetHashParam(hashHandle, HP_HASHVAL, static_cast<BYTE*>(hash.getData()), &hashSize,
                           0)) {
        hash.reset();
    }

    CryptDestroyHash(hashHandle);
    CryptReleaseContext(provider, 0);
    return hash;
}

juce::String randomBase64Url(int numBytes) {
    juce::MemoryBlock bytes(static_cast<size_t>(numBytes), true);
    auto* raw = static_cast<unsigned char*>(bytes.getData());
    auto& rng = juce::Random::getSystemRandom();
    for (int i = 0; i < numBytes; ++i)
        raw[i] = static_cast<unsigned char>(rng.nextInt(256));
    return base64UrlEncode(bytes.getData(), bytes.getSize());
}

juce::String parseQueryValue(const juce::String& query, const juce::String& key) {
    auto parts = juce::StringArray::fromTokens(query, "&", "");
    for (const auto& part : parts) {
        auto pair = juce::StringArray::fromTokens(part, "=", "");
        if (pair.isEmpty())
            continue;
        auto decodedKey = juce::URL::removeEscapeChars(pair[0]);
        if (decodedKey != key)
            continue;
        auto decodedValue = pair.size() > 1 ? juce::URL::removeEscapeChars(pair[1]) : juce::String();
        return decodedValue;
    }
    return {};
}

juce::Result parseOAuthError(const juce::String& query) {
    auto error = parseQueryValue(query, "error");
    if (error.isEmpty())
        return juce::Result::ok();

    auto desc = parseQueryValue(query, "error_description");
    if (desc.isEmpty())
        desc = "OAuth authorization failed";
    return juce::Result::fail("OpenAI OAuth error: " + error + " (" + desc + ")");
}

juce::String decodeBase64Url(const juce::String& input) {
    auto base64 = input.replaceCharacter('-', '+').replaceCharacter('_', '/');
    while ((base64.length() % 4) != 0)
        base64 << "=";

    juce::MemoryOutputStream output;
    if (!juce::Base64::convertFromBase64(output, base64))
        return {};
    auto data = output.getMemoryBlock();
    return juce::String::fromUTF8(static_cast<const char*>(data.getData()),
                                  static_cast<int>(data.getSize()));
}

}  // namespace

PkceState generatePkceState() {
    PkceState pkce;
    pkce.codeVerifier = randomBase64Url(64);

    auto verifierUtf8 = pkce.codeVerifier.toRawUTF8();
    auto raw = sha256Bytes(verifierUtf8, std::strlen(verifierUtf8));
    if (raw.isEmpty())
        return {};
    pkce.codeChallenge = base64UrlEncode(raw.getData(), raw.getSize());
    pkce.state = randomBase64Url(24);
    return pkce;
}

juce::String buildAuthorizeUrl(const PkceState& pkce) {
    juce::StringArray parts;
    parts.add("response_type=code");
    parts.add("client_id=" + urlEncode(kClientId));
    parts.add("redirect_uri=" + urlEncode(kRedirectUri));
    parts.add("scope=" + urlEncode(kScope));
    parts.add("code_challenge=" + urlEncode(pkce.codeChallenge));
    parts.add("code_challenge_method=S256");
    parts.add("state=" + urlEncode(pkce.state));
    parts.add("codex_cli_simplified_flow=true");
    parts.add("id_token_add_organizations=true");
    return juce::String(kAuthorizeUrl) + "?" + parts.joinIntoString("&");
}

juce::Result parseCodeFromRedirect(const juce::String& input, const juce::String& expectedState,
                                   juce::String& outCode) {
    auto trimmed = input.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail("No OAuth code provided");

    auto query = trimmed.containsChar('?') ? trimmed.fromFirstOccurrenceOf("?", false, false)
                                           : trimmed;

    if (auto errorResult = parseOAuthError(query); errorResult.failed())
        return errorResult;

    auto state = parseQueryValue(query, "state");
    auto code = parseQueryValue(query, "code");
    auto looksLikeCallback =
        trimmed.containsChar('?') || state.isNotEmpty() || code.isNotEmpty() ||
        parseQueryValue(query, "error").isNotEmpty();

    if (expectedState.isNotEmpty()) {
        if (state.isNotEmpty()) {
            if (state != expectedState)
                return juce::Result::fail("OAuth state mismatch");
        } else if (looksLikeCallback) {
            return juce::Result::fail("Missing OAuth state in callback");
        }
    }

    if (code.isNotEmpty()) {
        outCode = code;
        return juce::Result::ok();
    }

    if (!looksLikeCallback) {
        outCode = trimmed;
        return juce::Result::ok();
    }

    return juce::Result::fail("Missing OAuth code in callback");
}

juce::Result receiveLoopbackCode(const juce::String& expectedState, int timeoutMs,
                                 juce::String& outCode) {
    juce::StreamingSocket listener;
    if (!listener.createListener(1455, "127.0.0.1"))
        return juce::Result::fail("Failed to bind callback listener at 127.0.0.1:1455");

    auto deadline = juce::Time::getMillisecondCounter() + static_cast<uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (auto client = std::unique_ptr<juce::StreamingSocket>(listener.waitForNextConnection())) {
            char buffer[8192] = {};
            auto bytesRead = client->read(buffer, static_cast<int>(sizeof(buffer) - 1), false);
            if (bytesRead <= 0)
                return juce::Result::fail("Failed to read callback request");

            auto request = juce::String::fromUTF8(buffer, bytesRead);
            auto firstLine = request.upToFirstOccurrenceOf("\r\n", false, false);
            if (firstLine.isEmpty())
                firstLine = request.upToFirstOccurrenceOf("\n", false, false);
            if (firstLine.isEmpty())
                return juce::Result::fail("Malformed callback request");

            auto tokens = juce::StringArray::fromTokens(firstLine, " ", "");
            if (tokens.size() < 2)
                return juce::Result::fail("Callback request missing path");

            auto parseResult = parseCodeFromRedirect(tokens[1], expectedState, outCode);

            juce::String body;
            juce::String statusLine;
            if (parseResult.wasOk()) {
                statusLine = "HTTP/1.1 200 OK\r\n";
                body = "<html><body><h2>MAGDA login complete</h2><p>You can close this tab.</p></body></html>";
            } else {
                statusLine = "HTTP/1.1 400 Bad Request\r\n";
                body = "<html><body><h2>MAGDA login failed</h2><p>You can return to the app.</p></body></html>";
            }

            auto response = statusLine +
                            "Content-Type: text/html; charset=utf-8\r\nContent-Length: " +
                            juce::String(body.getNumBytesAsUTF8()) +
                            "\r\nConnection: close\r\n\r\n" + body;
            client->write(response.toRawUTF8(), response.getNumBytesAsUTF8());
            return parseResult;
        }

        juce::Thread::sleep(50);
    }

    return juce::Result::fail("Timed out waiting for browser callback");
}

juce::Result exchangeCodeForTokens(const juce::String& code, const PkceState& pkce,
                                   TokenSet& out) {
    juce::StringPairArray form;
    form.set("grant_type", "authorization_code");
    form.set("code", code);
    form.set("client_id", kClientId);
    form.set("redirect_uri", kRedirectUri);
    form.set("code_verifier", pkce.codeVerifier);

    int statusCode = 0;
    auto responseBody = postForm(kTokenUrl, form, statusCode);
    return parseTokenResponse(responseBody, statusCode, out);
}

juce::Result startDeviceCodeFlow(DeviceCodeStart& out) {
    juce::StringPairArray form;
    form.set("client_id", kClientId);
    form.set("scope", kScope);

    int statusCode = 0;
    auto responseBody = postForm(kDeviceCodeUrl, form, statusCode);
    if (statusCode < 200 || statusCode >= 300)
        return juce::Result::fail("HTTP " + juce::String(statusCode) + ": " + responseBody);

    auto json = juce::JSON::parse(responseBody);
    out.deviceCode = json["device_code"].toString();
    out.userCode = json["user_code"].toString();
    out.verificationUri = json["verification_uri"].toString();
    out.verificationUriComplete = json["verification_uri_complete"].toString();
    out.expiresInSeconds = static_cast<int>(json["expires_in"]);
    out.intervalSeconds = juce::jmax(1, static_cast<int>(json["interval"]));

    if (out.deviceCode.isEmpty() || out.userCode.isEmpty() || out.verificationUri.isEmpty())
        return juce::Result::fail("Device-code response missing required fields");
    return juce::Result::ok();
}

juce::Result pollDeviceCodeTokens(const DeviceCodeStart& device, TokenSet& out) {
    auto started = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    auto expiresAt = started + device.expiresInSeconds;
    auto intervalSeconds = juce::jmax(1, device.intervalSeconds);

    for (;;) {
        auto now = juce::Time::getCurrentTime().toMilliseconds() / 1000;
        if (now >= expiresAt)
            return juce::Result::fail("Device-code flow timed out before authorization completed");

        juce::Thread::sleep(intervalSeconds * 1000);

        juce::StringPairArray form;
        form.set("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
        form.set("device_code", device.deviceCode);
        form.set("client_id", kClientId);

        int statusCode = 0;
        auto responseBody = postForm(kTokenUrl, form, statusCode);
        if (statusCode >= 200 && statusCode < 300)
            return parseTokenResponse(responseBody, statusCode, out);

        auto json = juce::JSON::parse(responseBody);
        auto error = json["error"].toString();
        if (error == "authorization_pending")
            continue;
        if (error == "slow_down") {
            intervalSeconds += 5;
            continue;
        }
        if (error == "access_denied")
            return juce::Result::fail("OpenAI device-code authorization was denied");
        if (error == "expired_token")
            return juce::Result::fail("OpenAI device-code expired");

        return juce::Result::fail("HTTP " + juce::String(statusCode) + ": " + responseBody);
    }
}

juce::Result refreshAccessToken(const juce::String& refreshToken, TokenSet& out) {
    if (refreshToken.isEmpty())
        return juce::Result::fail("Missing refresh token");

    juce::StringPairArray form;
    form.set("grant_type", "refresh_token");
    form.set("refresh_token", refreshToken);
    form.set("client_id", kClientId);

    int statusCode = 0;
    auto responseBody = postForm(kTokenUrl, form, statusCode);
    auto result = parseTokenResponse(responseBody, statusCode, out);
    if (result.wasOk() && out.refreshToken.isEmpty())
        out.refreshToken = refreshToken;
    return result;
}

juce::String extractAccountIdFromJwt(const juce::String& token) {
    auto parts = juce::StringArray::fromTokens(token, ".", "");
    if (parts.size() < 2)
        return {};

    auto payloadText = decodeBase64Url(parts[1]);
    auto json = juce::JSON::parse(payloadText);
    auto auth = json["https://api.openai.com/auth"];
    auto accountId = auth["chatgpt_account_id"].toString();
    if (accountId.isNotEmpty())
        return accountId;
    return auth["chatgpt_user_id"].toString();
}

bool isExpiredOrNearExpiry(int64_t expiresAtUnixSeconds, int skewSeconds) {
    if (expiresAtUnixSeconds <= 0)
        return false;
    auto now = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    return expiresAtUnixSeconds <= now + skewSeconds;
}

juce::Result ensureValidProviderConfig(Config::AIProviderConfig& providerConfig) {
    if (!isExpiredOrNearExpiry(providerConfig.expiresAtUnixSeconds))
        return juce::Result::ok();
    if (providerConfig.refreshToken.empty())
        return juce::Result::ok();

    TokenSet refreshed;
    auto result = refreshAccessToken(providerConfig.refreshToken, refreshed);
    if (result.failed())
        return result;

    providerConfig.apiKey = refreshed.accessToken.toStdString();
    providerConfig.refreshToken = refreshed.refreshToken.toStdString();
    providerConfig.expiresAtUnixSeconds = refreshed.expiresAtUnixSeconds;
    auto accountId = extractAccountIdFromJwt(refreshed.accessToken);
    if (accountId.isNotEmpty())
        providerConfig.accountId = accountId.toStdString();
    return juce::Result::ok();
}

}  // namespace magda::codex_auth
