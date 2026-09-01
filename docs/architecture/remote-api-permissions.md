# Remote API permissions, auditing, and limits

What a remote client is allowed to do, how the user changes it, and what MAGDA
records about it. Issue #1860. The surface being protected is in
[remote-api-contract.md](remote-api-contract.md); the socket underneath it is in
[remote-api-transport.md](remote-api-transport.md).

Everything here applies identically to the WebSocket API and the MCP endpoint.
That is the point of the layering: both are transports over one
`RemoteApiService`, and the permission check lives in the dispatcher rather than
in either adapter, so there is one answer to "may this client do this" and one
place it can be wrong.

## What this is, and what it is not

**It is** the user's intent about a named client, recorded so a well-behaved one
cannot exceed it by accident, and so the user can see what has been asking for
what.

**It is not** a sandbox, and it is not a defence against a hostile process on the
machine. Three facts, stated plainly because a reader who assumes otherwise will
build on it wrongly:

1. Admission is a per-run bearer token, and nothing else.
2. The tokens live in `remote-api-<pid>.json` beside MAGDA's other app data.
   It is written owner-only, which stops *other users* reading it — but every
   process running as this user can.
3. The client's name is self-declared. A caller holding a token can claim any
   name and receive whatever that name was granted.

Each transport has its own token (#2142) — `token` for the WebSocket, `mcpToken`
for MCP. That exists so either can be rotated on its own, and adds nothing here:
both sit in the same file, so anything that can read one can read the other, and
both admit a caller to the same operations under the same grants.

So a process that can read your home directory can already do anything the
remote API can do. Per-client secrets would not change that: they would be
published in the same readable file for the same reason — a client has to be
able to find its credential without configuration.

What the model *does* buy is real, and it is the thing that actually goes wrong
in practice: an AI assistant you gave read access does not silently delete a
track, a monitoring script cannot start the transport, and when something is
refused you can see it, understand it, and grant it in one place.

If MAGDA ever binds anywhere but loopback, none of this is sufficient and the
threat model has to be rewritten before that happens.

## Scopes

Five words, shared by both transports. An operation requires exactly one.

| Scope | Covers |
| --- | --- |
| `read` | Every read operation, and subscribing to any topic. Every client has this — it is what being admitted means. |
| `edit` | Project content: tempo, time signature, tracks, clips, notes, devices, racks, automation, and the user's selection. Device parameter writes and opening plugin editor windows are edits too. |
| `transport` | The timeline: play, stop, record-arm, loop, seek. |
| `session` | Launching and stopping session clips and scenes. |
| `hardware-midi` | Physical MIDI ports, including SysEx. |

The split is by what a user would regret, not by which manager the code calls.
Driving the transport is separate from editing because a remote that only starts
and stops playback is a thing people want and it must not also be able to delete
a track. Session launch is neither: a performance controller firing scenes
changes no project content and does not move the playhead.

`selection.set` is an `edit`. It changes nothing durable, but it changes what the
user is looking at and what their next keystroke acts on, which is not something
a read-only client should reach.

**`hardware-midi` has no operations yet.** The registry exposes no hardware MIDI
surface. The scope is declared now because grants are persisted: a word invented
later would read as "not granted" on every existing client — the correct answer,
but only if the word already exists when those grants are written. It also gives
the settings UI a stable place to show the permission before there is anything
behind it. When a MIDI-out or SysEx operation lands, it declares this scope and
the enforcement already works.

### Where the mapping lives

`OperationRegistry`, in one contiguous table (`kWriteScopes` in
`remote_api.cpp`). One table rather than an argument on forty declarations,
because the question a reviewer needs to answer is not "does this operation
declare a scope" but "is the whole division of the API into scopes the one we
meant" — and that is only answerable by reading the policy in one piece.

Reads are absent from the table: `read` is the descriptor default. Writes are
never absent — one that is keeps the default, and the registry constructor turns
that into a startup failure. `test_remote_permissions.cpp` asserts the same
property in release builds, where `jassert` is a no-op.

`system.describe` publishes `requiredScope` per operation, so a client can tell
the user what to grant *before* it tries something rather than after being
refused.

## Clients and grants

A client names itself:

- **WebSocket** — a query parameter on the upgrade:
  `ws://127.0.0.1:51734/rpc?client=cursor`
- **MCP** — `clientInfo.name`, in `params._meta` for a modern request or in the
  `initialize` params for a legacy one, where it is recorded on the session and
  reused for every later request.

The name is normalised — lowercased, trimmed, reduced to letters, digits, dots
and hyphens, and capped at 64 characters — so `Cursor`, `cursor `, and `cursor`
are one client rather than three rows to grant separately. A caller that declares
nothing is `unknown`, which is a real entry rather than a rejection: refusing
would break every conforming MCP host that does not send `clientInfo` and every
WebSocket library whose user did not know about `?client=`. Anonymous callers
share one read-only bucket.

Using `clientInfo` at all is worth a note, because the MCP specification tells
servers not to act on it. That rule is about *authorisation*: a server must not
decide who may connect from a field the client wrote. MAGDA does not, and here it
cannot — admission was the bearer token, before any of this ran. The name decides
which of the user's own grants applies to a caller that is already inside, which
is the only way a user can say "my editor may edit, my monitoring script may not"
about two processes presenting the same token.

**A client MAGDA has not seen starts read-only.** It is registered the moment it
first asks for anything, so it appears in the settings list — read-only —
straight away rather than only once the user goes looking.

**Grants persist**, in `config.json` under `remoteApi.clients`:

```json
"remoteApi": {
  "enabled": true,
  "clients": [
    {"name": "cursor", "scopes": ["read", "edit"], "firstSeenMs": 1754000000000}
  ]
}
```

No credential is in there — the token is per-run and lives elsewhere — so this
file being copied between machines or committed by accident grants nothing that
is not already the user's decision. Unknown scope names are dropped on load
rather than rejected, so a config from a newer MAGDA downgrades to the scopes
this build understands.

### Revocation is immediate

The grant is looked up **per request**, not cached on the connection. That is the
entire mechanism: there is no per-connection copy to go stale, so changing a
grant in the settings dialog applies to the client's very next request, on the
socket it is already holding, with nothing to restart.

The permission check also runs *before* the idempotency cache. A cached response
is a previous success, and replaying one to a client whose grant has since been
withdrawn would be the single path where revocation did not take effect.

## Ordering, and why

Inside `RemoteApiService::dispatch`:

1. Shutdown check.
2. Operation lookup — unknown names fail here.
3. Transport-scoped check — a subscription method that reached the dispatcher.
4. **Permission.**
5. Input validation.
6. Idempotency cache.
7. Deadline, then the hop to the message thread.

Permission is before validation because a client that may not call something has
no business learning the shape of its input: a refusal that varied with whether
the payload was well formed would be an oracle for the schema.

A refusal is `permission_denied` and says which scope would have allowed it — not
`unknown_operation`. Hiding an operation from a client that may not call it would
be obscurity over a socket that already required a bearer token to reach, and it
would make the failure indistinguishable from a typo.

On the wire:

| Transport | Shape |
| --- | --- |
| WebSocket | JSON-RPC error, code `-32006`, `error.data.code` = `"permission_denied"` |
| MCP `tools/call` | `isError: true` with the MAGDA envelope in `content[0].text` — a model can act on it |
| MCP `resources/read` | JSON-RPC error, code `-32023` |

`test_remote_permission_conformance.cpp` drives one table of vectors through both
transports and asserts they agree, so the two cannot drift.

## The audit log

Bounded, in memory, 512 entries. Each carries: time, client, connection id,
transport, operation, request id, outcome, and a short reason.

Outcomes are `ok`, `denied`, `failed`, `connected`, `disconnected`, `rejected`.
Connection lifecycle uses the pseudo-operations `connection.open`,
`connection.close`, and `connection.rejected` so it shares one table with
requests rather than needing two views interleaved by timestamp.

**In memory and nowhere else.** A file would outlive the session that produced
it, would need rotation and a retention policy, and would put a description of
the user's editing session on disk permanently. For a loopback control socket
that is a liability, not a feature.

The dispatcher records what it ran; the transports record what it never saw —
connections opening, closing, and being refused before they became requests.

**Not exposed over the remote API.** A client that could read it would learn what
every other client is doing.

### What is never recorded

- Request input, or response results. A request's input is whatever a client
  chose to send and its result is a projection of the user's project; neither
  belongs in a buffer the settings UI displays and a user may screenshot into a
  bug report.
- Error *messages*. A validation failure quotes the offending value, so the log
  keeps the error **code** instead.
- Tokens. `registerRemoteSecret` masks the live token wherever it appears, and a
  `Bearer <credential>` is masked by shape as well, for a value MAGDA never held
  and therefore never registered.
- File paths. The startup log names the discovery file rather than its path —
  the directory it lives in is the user's home directory.

The redaction is applied once, inside `RemoteAuditLog::record`, rather than at
each call site where each new one is a chance to forget.

The `Bearer` shape rule masks the following word only when it *looks* like a
credential — at least 16 characters from RFC 7235's `token68` set. Without that,
"invalid or missing bearer token" would redact to "…bearer \*\*\*", which is how
a heuristic meant to protect the log ends up destroying it. It cannot be exact
and does not need to be: the real guarantee is that MAGDA never builds a message
out of a credential, and registered secrets cover the values it does hold.

## Limits

Per transport, enforced before a request reaches the dispatcher.

| Limit | WebSocket | MCP |
| --- | --- | --- |
| Payload | 256 KB per frame | 256 KB per body |
| Concurrency | 8 in flight per connection | 8 concurrent requests, server-wide |
| Rate | 50 req/s per connection, bursting to the in-flight cap | 50 req/s per remote address |
| Connections | 8 | 4 streams, 8 legacy sessions |
| Queue | 32 replies + 64 events per connection | 64-entry outbox per stream |
| Deadline | 10 s, lowerable per request | 10 s, lowerable per request |

Rate limiting is applied to *every* inbound frame, including one too malformed to
have a method — parsing costs work and every reply costs memory, so a client that
floods garbage runs out of allowance exactly like one that floods valid requests.

Queue overflow drops rather than buffers. A client that sends without reading
fills its socket buffer and parks the writer; without a cap the queue would grow
for as long as it kept talking. A dropped subscription event is not a lost
update: the hub marks the subscriber behind and answers with a fresh snapshot
rather than the deltas it missed.

None of these are permissions and none are per-client. They bound what one
connection can cost everyone else; scopes decide what it is allowed to ask for.

## Token rotation

Settings ▸ Connections ▸ **MCP** or **WebSocket** ▸ **Rotate token**. Per
transport: a new credential is generated for that one, its entry in the discovery
record is rewritten, and every client connected over it is disconnected — because
a token that still admitted the sessions it replaced would not have been revoked.
The other transport is not touched, which is the point of the tokens being
separate: re-credentialling a misbehaving script does not drop an AI host
mid-conversation.

It is implemented as a restart of that one listener rather than a swap in place.
Both servers read `bearerToken` from an immutable options copy taken at
construction; making it mutable would mean comparing against a value changing
under the comparing thread, for the one operation where being half-applied is
worst. Rebuilding guarantees every existing connection on that transport is gone:
they were admitted by a token that no longer exists.

A well-behaved client re-reads the discovery record and reconnects on its own.
The `magda-mcp` bridge does this on every request, so an MCP host survives
rotation without noticing.

## The settings UI

Settings ▸ Connections, four tabs — **MCP**, **WebSocket**, **OSC**, **Clients**.
The transports are separate tabs because they are separate listeners, and grants
are a fourth because turning an endpoint on and saying what a client may do are
separate decisions made at different times.

**MCP** and **WebSocket** — each carries its own enable toggle (applied
immediately, not on OK), that listener's status and port, and its own **Rotate
token**. MCP shows the host configuration snippet composed from the running
install; the WebSocket shows the connect URL and where to read the token, which
is never displayed — it is regenerated every run, so the file path is the durable
thing.

**OSC** is in this dialog but not in this document: no token, no client registry,
no scopes.

**Clients** — a row per client MAGDA has heard from, over either transport, with
a checkbox per scope,
whether it is connected and over which transport, **Disconnect**, and **Forget**.
Underneath, a tail of the audit log, so "why did my assistant say it could not do
that" has an answer on the same screen as the checkbox that fixes it.

`read` is shown ticked and disabled: a row granting nothing would be
indistinguishable from a client that was never granted, while still being one
that can connect. **Forget** drops the stored grant — the client returns to
read-only on its next request but is not disconnected. **Disconnect** closes
every connection that client holds; over WebSocket the socket itself goes within
one read timeout, but no further request from it executes from the moment it is
marked.

The page reads live state on a one-second timer rather than from notifications,
because what it displays changes on transport threads and a UI repainting from
those would need a hop per event for no benefit at that resolution. Rows are
rebuilt only when the set of client names changes — recreating a row under the
pointer would swallow the click about to land on one of its checkboxes.

## Adding a scope

1. Add the enum value and its wire name to `remote_scopes.hpp/.cpp`, and bump
   `SCOPE_COUNT`. The `static_assert` on the naming table catches a value with no
   name.
2. Add the operations that require it to `kWriteScopes` in `remote_api.cpp`.
3. Add a row to the scope table above, and vectors to
   `test_remote_permission_conformance.cpp`.

Existing grants read the new scope as "not granted", which is the correct answer
and needs no migration. The settings UI picks it up from `allScopeValues()`
without changes.

## Testing

| File | Covers |
| --- | --- |
| `test_remote_permissions.cpp` | The vocabulary, the registry, enforcement in the dispatcher, the audit log, and redaction |
| `test_remote_permission_conformance.cpp` | One table of vectors through both transports over real sockets, plus revocation, disconnection, anonymity, and fail-closed behaviour |
| `test_remote_websocket_server.cpp`, `test_remote_mcp_server.cpp` | Transport behaviour, using a permissive registry so permissions are not the variable |

A transport configured without a registry refuses **everything**, reads included.
That is deliberate: a third transport that forgot to consult the registry should
refuse every request loudly on its first test run, not hand out the project
silently and pass.
