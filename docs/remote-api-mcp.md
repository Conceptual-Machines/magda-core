# MCP over the MAGDA remote API

MAGDA exposes the remote API as a Model Context Protocol server so an AI host can
inspect and control the session that is running right now. The contract itself is
in [remote-api-contract.md](remote-api-contract.md); this describes only how it
is projected into MCP and carried over Streamable HTTP.

Nothing here is a second implementation of anything. A tool is a
`magda::remote::OperationRegistry` operation, a resource is one of its read
operations addressed by URI, and every call goes through the same
`RemoteApiService` the WebSocket transport uses — the same validation, the same
revisions, the same one-undo-step-per-mutation, the same error taxonomy.

## Connecting

The MCP endpoint is off unless the remote API is enabled, and it binds loopback
only. When MAGDA starts it writes a discovery record beside its other app data:

```
remote-api-<pid>.json
{"port":51734,"token":"…","url":"ws://127.0.0.1:51734/rpc",
 "mcpPort":51735,"mcpUrl":"http://127.0.0.1:51735/mcp","pid":4021}
```

The record is per process, because MAGDA allows more than one instance; a client
that finds several is looking at several running instances and has to pick. Both
transports appear in one record and take the same token — it authenticates the
user at the keyboard, not a protocol.

### Turning it on

**AI Settings → Remote.** The toggle opens and closes the listener immediately
rather than at the next launch, and the page shows the exact JSON to paste into
an MCP host, composed from this install's own paths. Copy it, paste it, done —
there is nothing to look up.

Everything below is what that page is doing, for anyone wiring it by hand.

### Where the bridge lives

The build stages `magda-mcp` beside the MAGDA executable, which is the one
location that means something on all three platforms:

| Platform | Path |
|---|---|
| macOS | `/Applications/MAGDA.app/Contents/MacOS/magda-mcp` |
| Windows | `C:\Program Files\MAGDA\magda-mcp.exe` |
| Linux | published as a **separate release asset**, `magda-mcp-<version>-Linux-x86_64` |

Linux is the exception because an AppImage is a single squashfs file: a path
inside it exists only while it is mounted, and a mount point is different every
run, so nothing inside can go in a host's config. The bridge is in the AppImage
too — beside the binary, for an extracted install — but the asset is what a user
should point at. Drop it anywhere on `PATH`; it locates MAGDA through the
discovery record and does not care where it was run from.

The settings page knows this: running from an AppImage it prints a placeholder
and disables Copy rather than publishing its own `/tmp/.mount_…` path, which
would stop existing the moment MAGDA quit.

It is signed on macOS and Windows in its own right, not just as part of the
bundle. An MCP host launches it directly, so Gatekeeper and SmartScreen judge it
on its own.

### Use the bridge, not the URL

**Do not put `mcpUrl` and `token` into a host's config.** They are correct only
until MAGDA next restarts: the port is ephemeral by default, and the token is
regenerated on every launch and deleted on shutdown. A host is configured once
and expects that to keep working, so a static entry breaks the first time the
user quits the DAW.

`magda-mcp` exists for exactly this. It is a stdio-to-HTTP bridge: the host
launches it, and it resolves the port and token from the discovery record on each
request. Nothing in the host's config changes when MAGDA restarts.

```bash
claude mcp add magda -- /path/to/magda-mcp
```

```json
{
  "mcpServers": {
    "magda": { "command": "/path/to/magda-mcp" }
  }
}
```

It takes no arguments. `MAGDA_DATA_DIR` overrides where it looks for records, the
same variable MAGDA itself honours. Diagnostics go to stderr, which the stdio
transport reserves for exactly that; stdout carries nothing but MCP messages.

It also reaches hosts that speak only stdio and have no Streamable HTTP client at
all — the follow-on #1858 anticipated, which this turned out to be.

A host that *does* speak Streamable HTTP can still point straight at `mcpUrl`
with an `Authorization: Bearer` header, and it will work for as long as that
MAGDA instance is up. That is the right shape for a one-off probe with `curl`,
and the wrong one for a configured host.

Ports are ephemeral by default. `remoteApi.port` and `remoteApi.mcpPort` in
config pin them, which makes the URL stable — but not the token, so it does not
remove the need for the bridge.

## Two protocol eras

Revision `2026-07-28` made MCP stateless: no `initialize`, no `Mcp-Session-Id`,
no standalone GET stream, and `subscriptions/listen` in place of
`resources/subscribe`. Hosts have not all moved, so MAGDA serves both shapes on
one endpoint and decides per request:

| Condition | Era |
|---|---|
| `params._meta` names a modern protocol version | Modern, stateless |
| The method is `initialize` | Legacy; a session is minted and returned in `Mcp-Session-Id` |
| An `Mcp-Session-Id` header names a live session | Legacy, at that session's negotiated version |
| None of the above | `400` with `-32602` naming the `_meta` a modern request needs |

Supported versions are `2026-07-28`, `2025-11-25`, and `2025-06-18`, advertised
by `server/discover` and by the `supported` list in an
`UnsupportedProtocolVersionError`. Nothing is pinned to a single version: the
outbound `MCPClient` elsewhere in the tree asks for `2024-11-05`, and that
constant appears nowhere in this server.

That last row is load-bearing rather than a fallback. A dual-era client uses a
`400` carrying a recognisable modern JSON-RPC error to tell "this is a modern
endpoint, send the right thing" apart from "there is no MCP endpoint here, drop
to the deprecated transport".

A modern request that carries `Mcp-Session-Id` anyway is served statelessly and
the header is ignored — which is what `2026-07-28` requires. No session is minted
and none is echoed.

### Modern-era requirements

Every POST must carry `MCP-Protocol-Version`, `Mcp-Method`, and — for
`tools/call` and `resources/read` — `Mcp-Name`, each matching the body. A
disagreement is `-32020` with `400`. The point is not redundancy: intermediaries
are allowed to route and rate-limit on those headers without parsing the body, so
a server that executed the body while a proxy had decided on the header would be
two components acting on two different requests.

`params._meta` must carry `io.modelcontextprotocol/protocolVersion` and
`io.modelcontextprotocol/clientCapabilities`. `clientInfo` is optional and, being
self-reported, is never used for anything but logging.

## Tools

Every registry operation that is not transport-scoped is one tool, under its own
dotted name — `tracks.list`, `transport.seek`, `clips.addMidiNote`. Dots are
valid in MCP tool names, so there is no translation table and one name means one
operation in the WebSocket logs, the audit record, and the tool call alike.

`inputSchema` and `outputSchema` are the registry's schemas verbatim. Inputs are
closed objects: unknown fields are rejected, numbers must be finite and in range.
`annotations` carries only what is derived — `readOnlyHint` from the operation's
access, and `openWorldHint: false`, since nothing here reaches outside the open
project.

A successful call returns the operation's output as `structuredContent`, the same
JSON serialized into a `content` text block, and the committed project revision in
`_meta`:

```json
{
  "resultType": "complete",
  "content": [{"type": "text", "text": "{\"tempo\":132,…}"}],
  "structuredContent": {"tempo": 132.0, "…": "…"},
  "isError": false,
  "_meta": {"com.conceptualmachines.magda/revision": 57}
}
```

### Errors

MCP has two error channels and they mean different things.

An operation that failed for a reason a model can act on — a value out of range,
an id that names nothing, a stale `expectedRevision` — comes back as a **tool
execution error**: `isError: true`, with MAGDA's error envelope (code, message,
and the per-field `issues` list) in the text block. That is what the model needs
in order to correct itself, so it must not be swallowed by the host as a protocol
failure. No `structuredContent` accompanies it: the tool declares an
`outputSchema`, and an error object does not satisfy it.

Something no argument could fix — an unknown tool, a malformed request — is a
**JSON-RPC error**. An unknown *method* answers `404`, which is what separates
"this server lacks that method" from "there is no MCP endpoint at this URL".

## Resources

| URI | Operation |
|---|---|
| `magda://project/current` | `project.get` |
| `magda://tracks` | `tracks.list` |
| `magda://selection` | `selection.get` |
| `magda://transport` | `transport.get` |
| `magda://session` | `session.get` |
| `magda://devices` | `devices.list` |
| `magda://devices/catalog` | `devices.catalog` |

Templates, from `resources/templates/list`:

| Template | Operation |
|---|---|
| `magda://tracks/{track_id}` | `tracks.get` (`-2` is the master track) |
| `magda://tracks/{track_id}/clips` | `clips.list` |
| `magda://tracks/{track_id}/devices` | `devices.list` |
| `magda://clips/{clip_id}` | `clips.get` |

A read returns one `contents` entry, `application/json`, whose `text` is byte for
byte what `tools/call` puts in `structuredContent` for the same operation. That
equivalence is asserted in the tests rather than assumed.

`magda://devices` lists devices that exist on tracks; `magda://devices/catalog`
lists device *kinds* that can be added, addressed by `catalogId`. The two are not
interchangeable, and neither carries a filesystem path.

A URI that names nothing, or a well-formed URI for something that does not exist,
returns `-32602` — never the `-32002` earlier revisions used, which this revision
forbids emitting.

## Subscriptions

An MCP resource update carries a URI and nothing else: the client re-reads. That
makes the whole delta/snapshot machinery in `SubscriptionHub` invisible here —
every kind of change collapses to the same "re-read this", and a client that fell
behind recovers by doing what it was going to do anyway. There is nothing to
resync and nothing to replay, so `Last-Event-ID` resumption is not offered.

**Modern.** POST `subscriptions/listen` with a `notifications.resourceSubscriptions`
list; the response is an SSE stream that stays open. The first frame is
`notifications/subscriptions/acknowledged` carrying the filter the server actually
agreed to, then one `notifications/resources/updated` per URI whose state moved,
with `:` keep-alive comments while it is quiet. Closing the stream is the
cancellation.

**Legacy.** `resources/subscribe` / `resources/unsubscribe` against the session,
delivered on the session's `GET` stream. Subscriptions are recorded whether or not
a stream is attached, but only reach the hub while one is — an update has nowhere
to go otherwise. One stream per session; a second `GET` gets `409`.

The acknowledged filter is the honest one, not an echo. A URI that names nothing,
or names something nothing can invalidate, is dropped and therefore absent from
it — so a client comparing the acknowledgment against its request learns exactly
what it will and will not hear about.

Two things are deliberately not subscribable. `magda://devices/catalog` changes
only on a plugin rescan, which nothing in the model publishes. And `meters` and
`playhead` are not resources at all: a `resources/updated` at 20 Hz is abusive to
a host, and a URI notification carrying no value is useless for a meter. Both
remain available over the WebSocket transport, which can carry a payload.

## Optimistic concurrency and retries

MCP's envelope has nowhere to hang the things `RequestContext` carries, so they
ride in `_meta` under a vendor prefix. (Any prefix whose second label is
`modelcontextprotocol` or `mcp` is reserved by the specification.)

- `com.conceptualmachines.magda/expectedRevision` — reject the write unless the
  project is still at this revision. The committed revision comes back as
  `com.conceptualmachines.magda/revision` in every result's `_meta`, so a client
  can seed its next write without a second round trip.
- `com.conceptualmachines.magda/requestId` — an idempotency key. Repeating a
  completed write with the same key returns the first response instead of
  applying the mutation twice.

Idempotency is **opt-in, and the key is the client's to make unique** — use a
UUID. The WebSocket transport scopes its keys by connection, which it can because
a connection is a client. MCP has no such thing: a modern request may arrive on a
fresh socket every time, and every client behind the one bearer token looks
identical from here. Deriving a key from the JSON-RPC id would therefore let two
clients that both counted from 1 receive each other's cached writes — a silent
wrong answer, which is worse than no caching at all. So without a key there is no
caching.

## Limits and threading

Loopback only, bearer token required (`401`), `Origin` validated when present
(`403`) — the last is a MUST in the transport spec and is what stops a page the
user merely visited from reaching this listener by DNS rebinding.

Bodies, concurrent requests, concurrent streams, sessions, and request rate are
all bounded. The rate limit is a single bucket for the endpoint rather than
per-client, and honestly so: everything arrives from 127.0.0.1 presenting the same
token, so nothing here can tell two local processes apart.

The MCP endpoint is a second `httplib::Server` on a second port rather than routes
on the WebSocket's. An SSE stream holds a connection thread for its whole
lifetime, so its budget has to be sized separately from a WebSocket connection
cap; and a protocol bug in one transport should not take the other's listener
down. The thread pool is sized `maxStreams + maxConcurrentRequests` for that
reason — with cpp-httplib's default, enough subscribers would starve ordinary
requests of threads and the endpoint would stall while still accepting
connections.

The message thread is never used for I/O. A request handler blocks its pool thread
until `RemoteApiService::dispatch` completes on the message thread, then writes
from the pool thread it was already on. No `MessageManagerLock` is taken and no
socket write happens on the message thread.

## Adding to the surface

Adding an operation to `OperationRegistry` adds a tool, with no change here.
Adding a *resource* means adding a row to the table in `remote_mcp.cpp`, naming
the operation it reads and the topic that invalidates it — and the tests assert
that every advertised resource resolves to an operation that exists, so a row that
names a typo fails the build's test run rather than a client's read.
