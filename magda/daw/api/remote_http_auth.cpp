#include "remote_http_auth.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace magda {
namespace remote {

bool secureEquals(const juce::String& a, const juce::String& b) {
    const auto* lhs = a.toRawUTF8();
    const auto* rhs = b.toRawUTF8();
    const auto lhsLength = std::strlen(lhs);
    const auto rhsLength = std::strlen(rhs);

    unsigned char difference = lhsLength == rhsLength ? 0 : 1;
    for (std::size_t i = 0, count = std::max(lhsLength, rhsLength); i < count; ++i) {
        const auto left = i < lhsLength ? lhs[i] : '\0';
        const auto right = i < rhsLength ? rhs[i] : '\0';
        difference |= static_cast<unsigned char>(left ^ right);
    }
    return difference == 0;
}

bool isAuthorised(const juce::String& authorizationHeader, const juce::String& token) {
    if (token.isEmpty())
        return false;
    return secureEquals(authorizationHeader, "Bearer " + token);
}

bool isOriginAllowed(bool originPresent, const juce::String& origin,
                     const std::vector<juce::String>& allowedOrigins) {
    if (!originPresent)
        return true;
    return std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) != allowedOrigins.end();
}

}  // namespace remote
}  // namespace magda
