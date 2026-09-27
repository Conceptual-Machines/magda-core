# Remote API

MAGDA's Remote API lets a local program inspect and control the project that is
open in MAGDA. The same versioned operation contract is available through two
transports:

- **WebSocket + JSON-RPC 2.0** for scripts, integrations, and control surfaces.
- **MCP** for AI hosts. Every ordinary Remote API operation is exposed as an MCP
  tool, and common reads are also exposed as resources.

This page documents API version **1.0**. Do not hard-code the catalogue or copy
JSON schemas from this page: call `system.describe` over WebSocket, or use MCP's
`tools/list`, to obtain the exact schemas implemented by the running build.

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
    "idempotencyKey": "9be29bd9-33f3-4d97-9921-75565a74a31a"
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

Project open/save-as, rendering, master capture, and Session recording can
return a `job_...` record instead of completing the work inline. Job states are
`accepted`, `running`, `completed`, `cancelled`, `failed`, or `unsupported`.
Progress is monotonic from `0` to `1`.

Use `jobs.get` or `jobs.list`, or subscribe to `jobs`. Use `jobs.cancel` to
cancel work. Jobs belong to the connection that created them: another
connection receives `not_found`, and disconnecting cancels active jobs and
forgets their records. Completed records are retained for at most ten minutes,
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

The tables below are an index, not a substitute for the runtime JSON schemas.
Unless stated otherwise, list/get/status operations require `read` and mutation
operations require `edit`.

### System, diagnostics, jobs, and rendering

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `system.describe` | `read` | Return the API version and complete operation/schema catalogue. |
| `engine.health` | `read` | Read engine binding, callback load, xruns, and recent problems. |
| `meters.read` | `read` | Read a bounded latest track/master peak snapshot. |
| `jobs.list` | `read` | List asynchronous jobs owned by this connection. |
| `jobs.get` | `read` | Inspect one owned job. |
| `jobs.cancel` | producing operation's scope | Cancel one owned job. |
| `engine.renderRange` | `edit` | Render a beat/second range to an absolute WAV or FLAC path. |
| `engine.masterCapture.start` | `edit` | Start capturing the true master callback output. |
| `engine.masterCapture.stop` | `edit` | Stop and finalize an owned master capture. |
| `engine.masterCapture.status` | `read` | Read capture capability and current activity. |

### Project and chord track

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `project.get` | `read` | Read safe current-project metadata. |
| `project.new` | `edit` | Create an untitled project. |
| `project.close` | `edit` | Close the current project with an explicit dirty policy. |
| `project.open` | `edit` | Open an absolute path as an asynchronous job. |
| `project.save` | `edit` | Save to the existing target. |
| `project.saveAs` | `edit` | Save to an absolute path as an asynchronous job. |
| `project.setTempo` | `edit` | Set tempo from 20 to 400 BPM. |
| `project.setTimeSignature` | `edit` | Set numerator and denominator. |
| `project.setLoopRange` | `edit` | Set the project loop range in beats. |
| `chordTrack.get` | `read` | Read the singleton chord track and ordered progression. |
| `chordTrack.detect` | `read` | Detect a structured progression in a MIDI clip range. |
| `chordTrack.ensure` | `edit` | Create the singleton chord track if absent. |
| `chordTrack.replaceProgression` | `edit` | Replace its structured progression. |
| `chordTrack.extract` | `edit` | Detect and materialize a progression atomically. |
| `chordTrack.sendToTrack` | `edit` | Bake a progression to a normal MIDI track. |

### Tracks, routing, sends, and sidechains

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `trackPresets.list` | `read` | List saved track-chain presets by opaque id. |
| `tracks.list` | `read` | List tracks. |
| `tracks.get` | `read` | Read one track. |
| `tracks.create` | `edit` | Create a track. |
| `tracks.createFromPreset` | `edit` | Create a track from a saved chain preset. |
| `tracks.applyPreset` | `edit` | Apply a saved chain preset to an existing track. |
| `tracks.update` | `edit` | Update mixer, display, arm, monitor, or input state. |
| `tracks.delete` | `edit` | Delete a track after reference-impact policy checks. |
| `tracks.group` | `edit` | Group tracks under a new group track. |
| `tracks.move` | `edit` | Move a track to a one-based list position. |
| `routing.endpoints.list` | `read` | List safe logical audio and MIDI endpoints. |
| `routing.get` | `read` | Read one track's audio and MIDI routes. |
| `routing.set` | `edit` | Atomically set a track's routes. |
| `sends.list` | `read` | List one track's sends. |
| `sends.create` | `edit` | Create a send. |
| `sends.update` | `edit` | Update a send. |
| `sends.remove` | `edit` | Remove a send. |
| `sidechains.list` | `read` | List device/rack sidechains and capabilities. |
| `sidechains.get` | `read` | Inspect one sidechain. |
| `sidechains.set` | `edit` | Configure or clear one sidechain atomically. |

### Clips and MIDI events

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `clips.list` | `read` | List clips, optionally filtered by track and view. |
| `clips.get` | `read` | Read one clip. |
| `clips.createMidi` | `edit` | Create a MIDI clip. |
| `clips.loadSample` | `edit` | Load a host-local audio file as a clip. |
| `clips.updateAudio` | `edit` | Set playback mode, source tempo, stretch, reverse, and fades. |
| `clips.addMidiNote` | `edit` | Add one note to a MIDI clip. |
| `clips.listMidiEvents` | `read` | List notes, CC, pitch bend, channel pressure, and poly aftertouch. |
| `clips.addMidiEvents` | `edit` | Atomically add expressive MIDI events. |
| `clips.updateMidiEvents` | `edit` | Atomically update events by id. |
| `clips.replaceMidiEvents` | `edit` | Atomically replace every event. |
| `clips.deleteMidiEvents` | `edit` | Atomically delete events by id. |
| `clips.delete` | `edit` | Delete a clip. |
| `clips.move` | `edit` | Move a clip to an Arrangement position or Session slot. |
| `clips.resize` | `edit` | Resize from the start or end edge. |
| `clips.duplicate` | `edit` | Duplicate to an Arrangement position or Session slot. |
| `clips.update` | `edit` | Change name, enabled state, or groove assignment. |
| `clips.transpose` | `edit` | Transpose MIDI notes by semitones. |
| `clips.quantize` | `edit` | Quantize selected or all MIDI notes. |
| `clips.sliceNotes` | `edit` | Slice selected or all MIDI notes into subdivisions. |

### Devices, pads, racks, and modulation

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `devices.list` | `read` | List the safe flattened device/rack/chain graph. |
| `devices.catalog` | `read` | List addable devices by catalogue id. |
| `devicePresets.list` | `read` | List presets for an addressed device. |
| `devices.add` | `edit` | Add a catalogue device to a track or rack chain. |
| `devices.applyPreset` | `edit` | Apply an opaque preset to an existing device. |
| `devices.replace` | `edit` | Replace a device while preserving its chain slot. |
| `devices.remove` | `edit` | Remove a device. |
| `devices.move` | `edit` | Move a device within its chain. |
| `devices.setBypassed` | `edit` | Set bypass state. |
| `devices.listParameters` | `read` | List real-unit parameter values and customization flags. |
| `devices.setParameter` | `edit` | Set one AI-enabled parameter in real units. |
| `devices.setParameterConfig` | `edit` | Configure visible, mini-mixer, and AI-agent parameters. |
| `devices.openEditor` | `edit` | Open a plugin editor window. |
| `pads.list` | `read` | List all 64 slots of an addressed Drum Grid. |
| `pads.create` | `edit` | Create an empty pad chain. |
| `pads.setDevice` | `edit` | Replace a pad voice with a catalogue device. |
| `pads.setSample` | `edit` | Replace a pad voice with a host-local sample. |
| `pads.clear` | `edit` | Clear a pad. |
| `pads.swap` | `edit` | Swap two single-note pads. |
| `pads.update` | `edit` | Update note range, level, pan, switches, and output bus. |
| `racks.create` | `edit` | Create a rack on a track or in a nested chain. |
| `racks.remove` | `edit` | Remove a rack at any nesting depth. |
| `racks.setBypassed` | `edit` | Set rack bypass. |
| `racks.update` | `edit` | Update a nested rack. |
| `chains.create` | `edit` | Create a chain inside a rack. |
| `chains.remove` | `edit` | Remove a nested rack chain. |
| `chains.update` | `edit` | Update chain name, routing, mixer, or switch state. |
| `mods.list` | `read` | List modulators on a device. |
| `mods.create` | `edit` | Create an LFO, envelope, random, or follower modulator. |
| `mods.update` | `edit` | Update a device modulator. |
| `mods.remove` | `edit` | Remove a device modulator. |
| `mods.link` | `edit` | Link a modulator to an AI-enabled parameter. |
| `mods.unlink` | `edit` | Remove a modulator link. |
| `macros.list` | `read` | List device macros and links. |
| `macros.setValue` | `edit` | Set a normalized macro value. |
| `macros.link` | `edit` | Link a macro to an AI-enabled parameter. |
| `macros.unlink` | `edit` | Remove a macro link. |

### Selection, transport, and Session

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `selection.get` | `read` | Read the current track, clip, and note selection. |
| `selection.set` | `edit` | Replace the current selection. |
| `transport.get` | `read` | Read transport state. |
| `transport.play` | `transport` | Start playback. |
| `transport.stop` | `transport` | Stop playback. |
| `transport.setRecording` | `transport` | Set recording state. |
| `transport.setLoopEnabled` | `transport` | Enable or disable the transport loop. |
| `transport.seek` | `transport` | Seek to an absolute beat. |
| `transport.seekRelative` | `transport` | Move by beats or bars, clamped at zero. |
| `session.get` | `read` | Read scenes, tracks, and every Session slot state. |
| `session.createScene` | `edit` | Create a durable scene. |
| `session.updateScene` | `edit` | Update scene metadata. |
| `session.moveScene` | `edit` | Move a scene by stable id. |
| `session.duplicateScene` | `edit` | Duplicate a scene with explicit clip-copy behavior. |
| `session.deleteScene` | `edit` | Delete a scene using an explicit populated-slot policy. |
| `session.updateClipSettings` | `edit` | Update clip launch mode, quantization, and follow action. |
| `session.launchClip` | `session` | Launch a Session clip. |
| `session.stopClip` | `session` | Stop a Session clip. |
| `session.stopTrack` | `session` | Stop the active Session clip on one track. |
| `session.stopAll` | `session` | Stop every Session clip. |
| `session.launchScene` | `session` | Launch a scene. |
| `session.returnToArrangement` | `session` | Return one or all tracks to Arrangement playback. |
| `session.recordingCapabilities` | `read` | Read Session recording support and stop policy. |
| `session.armSlotRecording` | `session` | Arm or unarm an empty addressed slot. |
| `session.beginSlotRecording` | `session` | Begin a slot take as an asynchronous job. |
| `session.stopSlotRecording` | `session` | Stop a slot take and complete its job. |
| `session.beginPerformanceCapture` | `session` | Capture a performance into Arrangement as a job. |
| `session.stopPerformanceCapture` | `session` | Stop and commit performance capture. |

### Automation and grooves

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `automation.listLanes` | `read` | List every automation lane. |
| `automation.getLane` | `read` | Read one lane. |
| `automation.createLane` | `edit` | Create an absolute or clip-based lane. |
| `automation.addPoint` | `edit` | Add a normalized point. |
| `automation.setPoints` | `edit` | Replace every point on an absolute lane. |
| `automation.clearLane` | `edit` | Remove all points from a lane. |
| `automation.deleteLane` | `edit` | Delete a lane and its clips. |
| `automation.listClips` | `read` | List automation clips, optionally by lane. |
| `automation.getClip` | `read` | Read one automation clip. |
| `automation.createClip` | `edit` | Create a clip on a clip-based lane. |
| `automation.deleteClip` | `edit` | Delete an automation clip. |
| `automation.moveClip` | `edit` | Move an automation clip. |
| `automation.resizeClip` | `edit` | Resize an automation clip from either edge. |
| `automation.duplicateClip` | `edit` | Duplicate an automation clip after its source. |
| `automation.updateClip` | `edit` | Update metadata, looping, or points. |
| `grooves.list` | `read` | List groove template names. |
| `grooves.upsert` | `edit` | Create or replace a groove template. |

### Focused device and hardware MIDI

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `focused.get` | `read` | Read the focused device/rack and its macro page. |
| `focused.setMacro` | `edit` | Set a normalized macro on the focused device. |
| `focused.cycleDevice` | `edit` | Move focus to the previous or next top-level node. |
| `midi.listOutputPorts` | `read` | List physical MIDI output names. |
| `midi.send` | `hardware-midi` | Send one status-first channel message. |
| `midi.sendSysEx` | `hardware-midi` | Send a SysEx payload; MAGDA adds `F0`/`F7`. |

### WebSocket subscription methods

| Operation | Scope | Purpose |
|-----------|-------|---------|
| `subscriptions.subscribe` | `read` | Subscribe to topics and optionally receive snapshots. |
| `subscriptions.unsubscribe` | `read` | Stop selected subscriptions, or all when omitted. |
| `subscriptions.list` | `read` | List this connection's subscribed topics. |
| `subscriptions.resync` | `read` | Request complete snapshots for subscribed topics. |

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
