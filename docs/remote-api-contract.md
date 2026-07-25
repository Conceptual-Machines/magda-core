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
