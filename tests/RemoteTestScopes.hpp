#pragma once

#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/api/remote_clients.hpp"

namespace magda::test {

/**
 * @brief A request context standing in for a client the user granted everything.
 *
 * `RequestContext::scopes` is empty by default and empty denies everything
 * (#1860), which is the behaviour the permission tests exist to pin down. Every
 * *other* remote test is about revisions, idempotency, deadlines, or schemas,
 * and would otherwise have to restate a full grant at each call site — burying
 * the thing each test is actually asserting under permission setup that is never
 * the point.
 *
 * So: tests that care about permissions build their own context or registry, and
 * tests that do not use this. A test that accidentally relied on it to paper
 * over a missing grant would still fail, because it asks for every scope rather
 * than skipping the check.
 */
inline remote::RequestContext fullyGrantedContext() {
    remote::RequestContext context;
    context.scopes = remote::allScopes();
    return context;
}

/**
 * @brief A client registry that grants every scope to everyone.
 *
 * The transport equivalent of the above, for the WebSocket and MCP server tests:
 * both refuse everything without a registry, so a server under test needs one
 * before it can answer anything at all.
 *
 * It is a real `RemoteClientRegistry` with a real grant rather than a bypass,
 * so what these tests drive is the same lookup production uses. Owned by the
 * caller and must outlive the server.
 */
inline void grantEverything(remote::RemoteClientRegistry& registry,
                            const juce::String& clientName = remote::ANONYMOUS_CLIENT) {
    registry.setScopes(clientName, remote::allScopes());
}

/**
 * @brief A shared registry granting everything to the anonymous client.
 *
 * The default a transport test's options point at, so tests about framing,
 * limits, and lifecycle do not each have to declare a registry and outlive their
 * server with it. Permission tests pass their own instead, which is why the
 * option is a parameter rather than something baked into the server.
 *
 * `unknown` is the name a client that sends no identity gets, and none of these
 * tests send one — so granting that one entry is what makes the whole file's
 * traffic admissible.
 *
 * A function-local static in an inline function: one instance across every test
 * translation unit, constructed on first use and destroyed after the last test.
 * Sharing is safe because the servers under test register and clear their
 * disconnect handlers by transport name in `start()` and `stop()`, and Catch2
 * runs test cases one at a time.
 */
inline remote::RemoteClientRegistry& permissiveRegistry() {
    static remote::RemoteClientRegistry registry;
    static const bool granted = [] {
        // Grants are keyed by the name the client declares, so this has to name
        // the clients these tests actually are. `unknown` covers the WebSocket
        // tests, which send no `?client=`; `catch2` is the `clientInfo.name`
        // the MCP tests send.
        //
        // A name missing from this list does not silently pass: it gets the
        // read-only default, so its reads work and its writes come back
        // `permission_denied` — which is a clear failure pointing here rather
        // than a mysterious one.
        grantEverything(registry, remote::ANONYMOUS_CLIENT);
        grantEverything(registry, "catch2");
        return true;
    }();
    juce::ignoreUnused(granted);
    return registry;
}

}  // namespace magda::test
