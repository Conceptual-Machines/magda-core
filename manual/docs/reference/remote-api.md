# Remote API

MAGDA's Remote API lets a local program inspect and control the project that is
open in MAGDA. The same versioned operation contract is available through two
transports:

- **WebSocket + JSON-RPC 2.0** for scripts, integrations, and control surfaces.
- **MCP** for AI hosts. Every ordinary Remote API operation is exposed as an MCP
  tool, and common reads are also exposed as resources.

This page documents API version **1.0**. Every operation is listed on
[Remote API Operations](remote-api-operations.md). Don't hard-code that list:
call `system.describe` over WebSocket, or use MCP's `tools/list`, to get the
exact schemas of the running build.

## Enable the API

Open **Settings > Connections** and enable the transport you need. Each
transport has its own listener, port, and per-run bearer token. Enabling MCP does
not enable WebSocket, and enabling WebSocket does not enable MCP.

New clients have the `read` permission only. Open the **Clients** tab after the
client first connects and grant any additional permissions it needs:

| Permission | Capability |
|------------|------------|
| `read` | Inspect the project and subscribe to projected state. Always granted. |
| `edit` | Change project content, selection, devices, and automation. |
| `transport` | Play, stop, record-arm, loop, and seek. |
| `session` | Launch, stop, and record Session clips and performances. |
| `hardware-midi` | Send channel messages and SysEx to physical MIDI outputs. |

Permission changes apply to the next request. A reconnect or restart is not
required. Client names are self-declared labels used to remember grants; they
are not credentials.

## Discovery and credentials

While either listener is running, MAGDA writes an owner-only discovery record
named `remote-api-<pid>.json` in its data directory. The WebSocket page in the
Connections dialog displays the exact path. One record describes one running
MAGDA process, so more than one record can legitimately exist.

```json
{
  "port": 51734,
  "url": "ws://127.0.0.1:51734/rpc",
  "token": "...",
  "mcpPort": 51735,
  "mcpUrl": "http://127.0.0.1:51735/mcp",
  "mcpToken": "...",
  "pid": 4021
}
```

Only enabled listeners appear in the record. Use `url` and `token` for
WebSocket, or `mcpUrl` and `mcpToken` for MCP. Ports and tokens may change when
MAGDA restarts. Re-read the record whenever reconnecting instead of persisting
either value.

Tokens authenticate access to a local MAGDA instance. Keep the discovery record
private and never write its contents to logs. **Rotate token** in the Connections
dialog invalidates that transport's token and disconnects its clients.

## WebSocket quick start

Connect to the record's `url`, add a stable client name, and authenticate with
the record's token:

```text
ws://127.0.0.1:51734/rpc?client=my-integration
Authorization: Bearer <token>
```

Native WebSocket clients should use the `Authorization` header. Browser
WebSocket APIs cannot set that header, so an allowed browser origin may instead
use `?client=my-integration&token=<token>`. Browser origins must also be listed
in `config.json` under `remoteApi.allowedOrigins`; an empty list permits no
browser origins. Never put the token in a URL outside this loopback-only use
case.

For a command-line probe, substitute the URL and token from the discovery
record:

```bash
websocat -H='Authorization: Bearer <token>' \
  'ws://127.0.0.1:51734/rpc?client=api-docs'
```

Then send a JSON-RPC request. Every request must have a string or numeric `id`;
client-to-server notifications are rejected because every response carries a
revision needed by later writes.

```json
{"jsonrpc":"2.0","id":1,"method":"project.get","params":{}}
```

A successful response returns the operation result unchanged. Transport
metadata is a sibling of `result`, so array-valued operations stay arrays:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "open": true,
    "name": "My Project",
    "tempo": 120.0
  },
  "meta": {"revision": 42, "apiVersion": "1.0"}
}
```

The abbreviated `result` above is illustrative. The operation's `outputSchema`
is authoritative.

### Discover operations and schemas

`system.describe` returns the API version and every available operation. Each
descriptor contains:

| Field | Meaning |
|-------|---------|
| `name` | The JSON-RPC method and MCP tool name. |
| `summary` | A human-readable description. |
| `access` | `read` or `write`; control operations are reported as writes. |
| `requiredScope` | The permission the named client needs. |
| `inputSchema` | Closed JSON Schema for `params`. |
| `outputSchema` | JSON Schema for `result`. |
| `transportScoped` | `true` for connection-local subscription methods. |

```json
{"jsonrpc":"2.0","id":2,"method":"system.describe","params":{}}
```

Inputs are closed objects: unknown fields are errors. Required fields, numeric
ranges, enums, array limits, and string limits in `inputSchema` are enforced
before an operation runs. Numbers must be finite.

### Write safely

Optional request metadata belongs in top-level `meta`, not in `params`:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "project.setTempo",
  "params": {"tempo": 128.0},
  "meta": {
    "expectedRevision": 42,
    "deadlineMs": 5000,
    "idempotencyKey": "<uuid>"
  }
}
```

- `expectedRevision` applies optimistic concurrency. A write fails with
  `conflict` if another committed mutation advanced the project first.
- `deadlineMs` may shorten the server deadline, never extend it. It must be a
  positive whole number.
- `idempotencyKey` makes a completed write safe to retry on the same connection.
  Use a unique value such as a UUID. JSON-RPC `id` only correlates a response and
  is not an idempotency key.

Successful mutations normally advance the revision by one. Reads, continuous
samples, control operations, and writes that make no change do not. Use the
`meta.revision` returned by every response as the basis for the next guarded
write.

### Errors

Failures use JSON-RPC's `error` member. `error.data.code` is the stable MAGDA
error name, `revision` is the current project revision, and validation failures
include field-level `issues`.

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "error": {
    "code": -32602,
    "message": "Operation input validation failed",
    "data": {
      "code": "validation_failed",
      "message": "Operation input validation failed",
      "issues": [
        {"path": "$.tempo", "code": "minimum", "message": "..."}
      ],
      "revision": 43
    }
  }
}
```

| JSON-RPC code | MAGDA code | Meaning |
|---------------|------------|---------|
| `-32700` | — | Malformed JSON. |
| `-32600` | `invalid_request` | Invalid JSON-RPC framing or unsupported route. |
| `-32601` | `unknown_operation` | Unknown method. |
| `-32602` | `validation_failed` | `params` did not satisfy the input schema. |
| `-32603` | `internal_error` | Unexpected server failure. |
| `-32001` | `not_found` | A referenced project entity does not exist. |
| `-32002` | `conflict` | State or revision conflicts with the request. |
| `-32003` | `timeout` | The request missed its deadline. |
| `-32004` | `cancelled` | Work was cancelled. |
| `-32005` | — | Connection request rate or in-flight limit exceeded. |
| `-32006` | `permission_denied` | Grant the named client the reported scope. |

Do not branch only on the numeric code. Prefer `error.data.code` when present;
it is shared by every transport.

## Subscriptions

WebSocket clients can subscribe to these topics:

`project`, `tracks`, `clips`, `devices`, `selection`, `transport`, `session`,
`automation`, `jobs`, `meters`, and `playhead`.

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "subscriptions.subscribe",
  "params": {"topics": ["tracks", "clips", "transport"], "snapshot": true}
}
```

The response includes the accepted topics, current revision, and initial
snapshots unless `snapshot` is `false`. Later changes arrive as JSON-RPC
server-to-client notifications:

```json
{
  "jsonrpc": "2.0",
  "method": "subscriptions.event",
  "params": {
    "topic": "tracks",
    "type": "delta",
    "revision": 57,
    "payload": {"added": [], "updated": [], "removed": []}
  }
}
```

Event `type` is:

- `snapshot` — complete topic state. Treat it as replacement state.
- `delta` — an upsert/remove set for tracks, clips, automation, and Session
  slots, or the complete current value for the other discrete topics.
- `sample` — a latest-value reading for `meters` or `playhead`.

Apply `added` and `updated` as idempotent upserts. A client that falls behind is
sent a fresh snapshot instead of an incomplete delta history. There is no
resume-from-revision protocol: call `subscriptions.resync` after any uncertainty.

`subscriptions.unsubscribe` accepts an optional `topics` array; omitting it
unsubscribes from everything. `subscriptions.list` reports the current set.
All four methods require `read` and are connection-local.

Keep reading the socket even when not sending requests. The server uses ping and
pong traffic to detect dead clients, and subscription delivery is deliberately
bounded rather than buffered without limit.

MCP subscriptions use resource notifications rather than these WebSocket
methods. MCP does not expose `meters` or `playhead` resources; use WebSocket for
continuous samples.

## Asynchronous jobs

Project open/save-as, rendering, track freezing, master capture, and Session
recording can return a `job_...` record instead of completing the work inline.
Job states are `accepted`, `running`, `completed`, `cancelled`, `failed`, or
`unsupported`. Progress is monotonic from `0` to `1`.

Use `jobs.get` or `jobs.list`, or subscribe to `jobs`. Use `jobs.cancel` to
cancel work; it also needs the scope of the operation that started the job.
Jobs belong to the connection that created them: another connection receives
`not_found`, and disconnecting cancels active jobs and forgets their records. Completed records are retained for at most ten minutes,
up to the newest 128 records.

Artifacts are opaque `artifact_...` handles with a kind and media type. The API
does not expose internal filesystem or engine objects through job results.

## Data conventions

- Timeline positions and durations are in **beats** unless a field or schema
  explicitly says seconds.
- Track, clip, scene, lane, device, rack, chain, modulator, and job identifiers
  are opaque. Obtain them from list/get operations; do not invent or persist
  them across projects.
- The master track uses track id `-2` where an operation accepts it.
- `tracks.move` uses a one-based destination position; most collection indices
  and Session scene indices are zero-based. Follow each operation's schema.
- Colours are unsigned 32-bit ARGB values.
- Device parameter writes use real units, such as Hz or dB. Read
  `devices.listParameters` first. Automation values remain normalized `0..1`.
- External plugin parameter writes and modulation links are restricted to
  parameters the user enabled for AI/agent control.
- File-taking operations use absolute paths on the computer running MAGDA.
  Reads avoid exposing media paths, plugin state, or native engine objects.
- A device id is not globally unique. Address devices and nested racks with the
  complete `devicePath` returned by `devices.list`.

A device path has this stable wire shape:

```json
{
  "trackId": 3,
  "section": "fx",
  "trackLevel": false,
  "topLevelDeviceId": 5,
  "steps": [{"type": "device", "id": 5}]
}
```

Nested paths add `rack`, `chain`, `pad_rack`, `pad_chain`, and `device` steps.
Always round-trip the path returned by MAGDA rather than reconstructing it from
individual ids.

## MCP clients

For normal use, enable MCP and copy the configuration shown in **Settings >
Connections > MCP**. It launches the stable `magda-mcp` helper, which locates a
running MAGDA instance and re-reads its current URL and token. Do not paste the
ephemeral `mcpUrl` or `mcpToken` into a permanent host configuration.

MCP tool names are identical to the operation names below. Inputs use the same
schemas. Successful results are returned as structured content; because MCP
requires structured tool content to be an object, an array-valued tool result is
wrapped as `{"items": [...]}`. WebSocket and MCP resources return that array
bare.

Common MCP resources include:

| URI | Backing operation |
|-----|-------------------|
| `magda://project/current` | `project.get` |
| `magda://tracks` | `tracks.list` |
| `magda://tracks/{track_id}` | `tracks.get` |
| `magda://tracks/{track_id}/clips` | `clips.list` |
| `magda://tracks/{track_id}/devices` | `devices.list` |
| `magda://clips/{clip_id}` | `clips.get` |
| `magda://devices` | `devices.list` |
| `magda://devices/catalog` | `devices.catalog` |
| `magda://selection` | `selection.get` |
| `magda://transport` | `transport.get` |
| `magda://session` | `session.get` |
| `magda://jobs` | `jobs.list` |

## Operation catalogue

[Remote API Operations](remote-api-operations.md) lists every operation with its
scope and parameters. It is generated from the same registry as
`system.describe`, and the OpenRPC document behind it,
[`magda-openrpc.json`](magda-openrpc.json), is there for client generators and
API tools.

## Limits and security model

The Remote API binds to loopback by default and uses cleartext `ws://` and
`http://` because it is a local control surface, not a network service. Do not
expose either listener to another machine or tunnel it without adding transport
security appropriate to that environment.

The bearer token prevents accidental or cross-user access to the listener. The
per-client permission checkboxes prevent a well-behaved named client from doing
more than the user intended. They are not a sandbox against hostile software
running as the same OS user: such software can read the discovery record and can
claim another client's name.

Connections, frame/body size, in-flight requests, queued replies/events,
request rate, MCP streams, and job history are bounded. Handle `503`, `-32005`,
timeouts, disconnects, and snapshots after backpressure as normal recoverable
conditions.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Connection refused | Enable the correct listener and re-read the current discovery record. |
| HTTP `401` during upgrade/POST | The token is absent, stale, or belongs to the other transport. |
| HTTP `403` from a browser | Add the exact browser origin in Connections; native clients should omit `Origin`. |
| `permission_denied` | Grant the reported scope to the exact normalized client name on the Clients tab. |
| `validation_failed` | Read `error.data.issues` and the operation's current `inputSchema`. |
| `conflict` | Refresh state, use the returned revision, and decide whether the write is still valid. |
| Missing events | Keep reading continuously and call `subscriptions.resync`; accept snapshots as replacement state. |
| MCP config stops working after restart | Configure the `magda-mcp` helper, not an ephemeral URL/token. |
| Operation absent | Check `apiVersion` and `system.describe`; do not assume all MAGDA builds expose the same catalogue. |
