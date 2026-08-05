# WebSocket transport for the remote API — dependency decision

Spike outcome for the first scope item on #1856. The contract itself is in
[remote-api-contract.md](remote-api-contract.md); this records only which
library carries it over a socket, and why the alternatives were rejected.

**Decision: cpp-httplib, pinned by us via FetchContent.**

## What the transport has to do

Four of #1856's acceptance criteria constrain the choice. The rest are ours to
implement whatever we pick.

- Refuse a connection with an invalid bearer token or a disallowed `Origin`,
  and refuse it *closed* — the client must not end up with a live socket.
- Drop one misbehaving client without disturbing the others.
- Bound request size, in-flight requests, and rate.
- Leave no blocked threads or queued writes after disconnect or shutdown.

The first one does most of the work: it means the library has to let us see the
upgrade request's headers and say no before the handshake completes. That is
where the obvious candidate falls down.

## Rejected: CHOC `choc_HTTPServer.h` + Boost.Beast

`third_party/tracktion_engine/modules/3rd_party/choc/network/choc_HTTPServer.h`
is already in the tree via the Tracktion Engine submodule, and #1856 named it as
the thing to evaluate. Nothing in the repo compiles it today.

It accepts every upgrade unconditionally. `upgradeToWebsocket` (L465–473) starts
the handshake and *then* calls `upgradedToWebSocket(target)`, which receives the
request path and nothing else — no `Origin`, no `Authorization`, and no return
value to refuse with. The `decorator` at L365 only stamps headers onto a
response that is already going out. `ClientInstance` has no close, so a bad
client can only be dropped by tearing down the whole listener via
`HTTPServer::close()` (L644). `messageBodySizeLimit` (10 KB), `timeoutSeconds`
(30) and `defaultNumThreads` (4) are file-scope constants at L205–207.

So the security criteria need a patched copy of the header — an `allowUpgrade`
hook, per-connection close, configurable limits. That part is cheap and the ISC
licence allows it. The cost that does not go away is Boost: L190–193 `#error`s
without `<boost/beast.hpp>` and `<boost/asio.hpp>`. Boost is in none of
`third_party/`, `vcpkg.json`, or the CI apt list, so this would put brew boost
on macOS, `libboost-dev` on Linux, and a vcpkg port on Windows, and change the
README prerequisites on all three — which currently make a point of needing
vcpkg only on Windows and only for libxml2.

A patched vendored header plus a first-of-its-kind Boost dependency is a lot to
carry for a loopback control socket.

## Rejected: websocketpp

Last release 0.8.2, April 2020. Between then and March 2025 the repository took
one commit, a CMake 4.0 compatibility fix; 483 issues are open. It also needs
Asio — standalone rather than full Boost, so a lighter dependency than Beast,
but still a new one. Its handshake `validate` hook would have satisfied the
security criteria. Maintenance is the objection: this is network-facing code
where we would be the ones fixing protocol bugs.

## Rejected: hand-rolled RFC 6455 on `juce::StreamingSocket`

Considered while every option still looked like it cost a dependency. Zero new
dependencies and total control of the handshake, against roughly 500 lines of
framing, fragmentation, masking, ping/pong and close handling that we would own
and have to fuzz — plus a vendored SHA-1, since juce_cryptography ships MD5,
SHA256 and Whirlpool but not the one the accept key needs. Not worth it once a
maintained library with the right hook turned out to be free.

## Chosen: cpp-httplib, pinned at v0.52.0

MIT, no required dependencies — OpenSSL, zlib, Brotli and zstd are all
`#ifdef`-optional. It is already in the tree at
`third_party/llama.cpp/vendor/cpp-httplib/` (0.50.1), which is how it was found,
but we do not consume it from there (see below).

Version 0.50 has a WebSocket server, and its upgrade path was built for exactly
this case. From `httplib.cpp:8496`:

> Check `pre_routing_handler_` before upgrading so that authentication and other
> middleware can reject the request with an HTTP response (e.g., 401) before the
> protocol switches.

That is the criterion, implemented upstream. The rest follows:

| Need | What it gives us |
|---|---|
| Reject before the 101 | `set_pre_routing_handler`, full `Request` headers, returns an ordinary HTTP status |
| Drop one client | `ws::WebSocket::close(CloseStatus, reason)`, RFC 6455 close codes |
| Request-size bound | `CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH` (16 MB default), `set_payload_max_length` |
| Liveness | server-side ping heartbeat, `set_websocket_ping_interval`, `set_websocket_max_missed_pongs` |
| Integration tests | `ws::WebSocketClient` in the same library — no test-only dependency |

### Validated, not assumed

A throwaway TU built with `clang++ -std=c++20` exercised the criteria end to
end: bind on `127.0.0.1` with an ephemeral port; upgrade with no token refused;
upgrade with a disallowed `Origin` refused; neither refusal reaching the
handler; upgrade with a valid token and allowed `Origin` completing and
round-tripping a JSON-RPC frame; an oversized frame closing the connection; a
server-initiated close seen by the client; and `stop()` joining the listener.

Eleven checks, all passing, against both 0.50.1 (compiled, from llama.cpp's
vendored copy) and 0.52.0 (header-only, from the pinned tag) — so the API we
depend on is stable across the two. Compilation is 2–4 seconds at `-O0`.

## How it is wired

Declared in the root `CMakeLists.txt` with FetchContent at tag `v0.52.0`, the
way `Catch2` already is. Not consumed from llama.cpp's vendor directory: that
copy belongs to a submodule Dependabot bumps, and could change version or move
without warning.

Header-only. `HTTPLIB_COMPILE` splits the header using a Python script, and the
adapter includes it from a single TU behind a pimpl, so there is nothing to gain
from a new build prerequisite. `HTTPLIB_USE_OPENSSL_IF_AVAILABLE`,
`..._ZLIB_...`, `..._BROTLI_...` and `..._ZSTD_...` all default to ON — "link it
if you find it" — which would make the build depend on what happens to be
installed on the machine. All four are forced OFF, along with `HTTPLIB_INSTALL`
and `HTTPLIB_TEST`, so every platform compiles the same thing. There is no TLS
to configure because the listener is loopback-only.

## Constraints this puts on the implementation

**`listen()` blocks; run it on a `juce::Thread`.** Stop it with `stop()` from
the owning thread and join. This suits the requirement to keep parsing, framing
and socket writes off the message thread, because none of it is ever on the
message thread to begin with — the only hop is `RemoteApiService::dispatch`,
which already does its own.

**Thread-per-connection means connections must be capped.** An in-flight limit
is on #1856's scope list anyway; it is load-bearing here rather than merely
good practice.

**WebSocket support is new in cpp-httplib**, and less exercised than Beast's
equivalent. Mitigated by the threat model — loopback-bound, token-gated, hard
frame cap, close on anything unexpected — but it is the one place where the
chosen option is weaker than the rejected one, and worth remembering if
something protocol-level ever looks wrong.
