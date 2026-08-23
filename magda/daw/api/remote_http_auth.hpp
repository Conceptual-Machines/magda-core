#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda {
namespace remote {

/**
 * @brief The admission rules every remote transport shares.
 *
 * The WebSocket transport (#1856) and the MCP endpoint (#1858) are two listeners
 * on two ports, and they must not be two security policies. Both present the
 * same per-run bearer token and both refuse the same browser origins, so the
 * checks live here rather than in whichever adapter happened to need them first.
 *
 * Nothing here knows about HTTP. Callers extract the header values and decide
 * what status to answer with, because the two transports refuse at different
 * points — one before a protocol upgrade, the other before a request body is
 * parsed.
 */

/**
 * @brief Compare two secrets in time independent of where they diverge.
 *
 * A byte-at-a-time `==` returns sooner the earlier it finds a difference, which
 * over enough samples tells an attacker how much of a guessed token was right.
 * This always walks the longer of the two.
 */
bool secureEquals(const juce::String& a, const juce::String& b);

/**
 * @brief Whether `authorizationHeader` presents `token` as a bearer credential.
 *
 * Empty `token` is never accepted, whatever the header says: an empty
 * configured token means the listener should not have opened at all, and
 * treating it as "no credential required" is how an unauthenticated port
 * happens.
 */
bool isAuthorised(const juce::String& authorizationHeader, const juce::String& token);

/**
 * @brief Whether a request carrying this `Origin` may proceed.
 *
 * `originPresent` is separate from the value because absent and unrecognised are
 * different requests. Only a browser sends `Origin`; a native client sends none
 * and must not be locked out, while a page MAGDA did not authorise must be. An
 * empty allow-list therefore means "no browser may connect", not "no client
 * may".
 */
bool isOriginAllowed(bool originPresent, const juce::String& origin,
                     const std::vector<juce::String>& allowedOrigins);

}  // namespace remote
}  // namespace magda
