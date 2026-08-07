# MAGDA Remote API contract

The remote API is a transport-neutral contract above `MagdaApi`. WebSocket,
MCP, and future adapters consume `magda::remote::OperationRegistry`; they do
not define their own operation names or schemas and do not serialize MAGDA
core model objects.

Version 1.0 uses stable, domain-qualified operation names such as
`tracks.list`, `transport.seek`, and `session.launchClip`. Inputs are closed
JSON objects: unknown fields are rejected. Numeric values must be finite and
must satisfy the range in the operation's JSON Schema. Responses use one of:

```json
{ "ok": true, "apiVersion": "1.0", "result": {} }
```

```json
{
  "ok": false,
  "apiVersion": "1.0",
  "error": {
    "code": "validation_failed",
    "message": "Operation input validation failed",
    "issues": [
      { "path": "$.trackId", "code": "minimum", "message": "..." }
    ]
  }
}
```

`system.describe` exposes the version, operation catalogue, access mode, and
shared input/output schemas. A transport may add its own correlation or
framing metadata outside these envelopes, but it must not change the contract
payload.

## Subscriptions

Ten topics partition what a client can watch: `project`, `tracks`, `clips`,
`devices`, `selection`, `transport`, `session`, `automation`, and the two
continuous ones, `meters` and `playhead`.

`subscriptions.subscribe`, `.unsubscribe`, `.list`, and `.resync` are declared in
the registry like any other operation and marked `transportScoped: true`. They
are executed by the transport adapter rather than by the dispatcher, because
what a connection watches is state only that connection has. Dispatching one
through a transport that cannot push fails with `invalid_request` rather than
`unknown_operation` — the operation is real, and only the route is wrong.

A pushed change is one envelope, independent of the transport that carries it:

```json
{ "topic": "clips", "type": "delta", "revision": 57, "payload": {} }
```

`type` is one of:

- `snapshot` — complete state for the topic, in the same shape as the topic's
  read operation (`tracks` is `tracks.list`, `project` is `project.get`, and so
  on). Delivered in the reply to `subscribe` and `resync`, so a client is never
  subscribed without knowing what it is watching, and pushed as an event when a
  client has fallen behind.
- `delta` — what changed since the previous event on that topic. For `tracks`,
  `clips`, `automation`, and `session` the payload is
  `{"added": [], "updated": [], "removed": []}`, where `removed` carries
  identities only — an id, or `{trackId, sceneIndex}` for a session slot. Apply
  `added` and `updated` as upserts keyed by id: a client may legitimately be sent
  a change it already has, and doing so must be harmless. For `project`,
  `transport`, `selection`, and `devices` the payload is the topic's full state,
  because there is nothing useful to diff.
- `sample` — a point reading of `meters` or `playhead`. Latest value wins,
  intermediate readings are discarded, and a dropped sample is never resent.

`revision` orders the stream and is the same counter `expected_revision` uses.
Continuous motion — a parameter following an LFO, a drag preview — publishes
events without advancing it, so equal revisions on consecutive events are
expected and mean "nothing was committed".

`subscribe` always delivers snapshots, including on a reconnect, and
`subscriptions.resync` forces the same thing at any time. There is no "resume
from revision N": `revision` counts committed mutations, while events are also
published for motion that commits nothing, so a client that disconnected and
missed one of those has a revision indistinguishable from a client that saw it.
A cursor that cannot tell those apart cannot be used to skip state.

A client that knows it already has state — because it is about to resync itself,
or only cares about what happens from now on — passes `"snapshot": false`. That
is the client asserting it, which is the difference: being wrong about it is then
its own choice rather than the server's guess.

Opening a different project invalidates every discrete topic at once, so a
client watching only `tracks` hears about the swap rather than continuing to
show the outgoing project's contents.

Delivery is bounded rather than buffered. A client that stops reading has its
events dropped and is marked for resync; the snapshot it is owed is retried on
its own until it is taken, so a client that fell behind is not left stale by the
project happening to go quiet. One that never resumes is disconnected.
Subscribing to `meters` or `playhead` costs nothing until asked for: nothing
samples them otherwise.

## Deliberately excluded data

DTO fields are allow-listed. In particular, the remote API does not expose:

- project, audio-source, MIDI-source, plugin, render, or cache file paths;
- physical audio/MIDI device identifiers (logical `track:N`, `master`,
  `default`, and `all` routing tokens are retained);
- native plugin state, preset blobs, plugin filesystem identifiers, or raw
  plugin identity strings;
- pointers, engine objects, manager objects, or host/plugin instances;
- device parameter internals, wrapper parameters, macros, modulators, kit
  internals, or transient loading objects;
- transient AI conversations, AI output, prompts, or model state;
- UI layout and expansion state, zoom/scroll state, active panels, parameter
  pages, editor grids, playhead caches, waveform/transient caches, and
  rail-managed mixer analysis devices;
- opaque project aliases/bindings or persistence timestamps.

Tracks, clips, and devices are projected into compact value DTOs. Nested device
racks are represented as flat `devices`, `racks`, and `chains` arrays joined by
stable IDs. This preserves structure without exposing recursive core objects.

Adding a field is an API change: update the DTO, encoder/decoder, output schema,
projection, exclusion review, and round-trip tests together.
