#pragma once

#include <juce_core/juce_core.h>

#include "../daw/core/Config.hpp"

namespace magda::codex_auth {

struct PkceState {
    juce::String codeVerifier;
    juce::String codeChallenge;
    juce::String state;
};

struct DeviceCodeStart {
    juce::String deviceCode;
    juce::String userCode;
    juce::String verificationUri;
    juce::String verificationUriComplete;
    int expiresInSeconds = 0;
    int intervalSeconds = 5;
};

struct TokenSet {
    juce::String accessToken;
    juce::String refreshToken;
    juce::String idToken;
    int64_t expiresAtUnixSeconds = 0;
};

PkceState generatePkceState();
juce::String buildAuthorizeUrl(const PkceState& pkce);
juce::Result receiveLoopbackCode(const juce::String& expectedState, int timeoutMs,
                                 juce::String& outCode);
juce::Result exchangeCodeForTokens(const juce::String& code, const PkceState& pkce,
                                   TokenSet& out);
juce::Result startDeviceCodeFlow(DeviceCodeStart& out);
juce::Result pollDeviceCodeTokens(const DeviceCodeStart& device, TokenSet& out);
juce::Result refreshAccessToken(const juce::String& refreshToken, TokenSet& out);
juce::Result parseCodeFromRedirect(const juce::String& input, const juce::String& expectedState,
                                   juce::String& outCode);
juce::String extractAccountIdFromJwt(const juce::String& token);
bool isExpiredOrNearExpiry(int64_t expiresAtUnixSeconds, int skewSeconds = 90);
juce::Result ensureValidProviderConfig(Config::AIProviderConfig& providerConfig);

}  // namespace magda::codex_auth
