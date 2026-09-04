# RemoteApiHost lifecycle

`RemoteApiHost` (`magda/daw/api/remote_api_host.hpp`) owns the remote API for
the lifetime of a running MAGDA (#1856, #1858): the dispatcher, the
`ModelChangeBridge` that feeds it local edits, and the WebSocket and MCP
transports. One dispatcher and one `SubscriptionHub` are shared by both
transports on purpose, not as an optimization — two dispatchers would be two
revision counters and two undo groupings over one project, and two hubs would
be two projections of the model drifting apart.

Construct, destroy, and call `start()`/`startTransport()`/`rotateToken()` on
the message thread only: `ModelChangeBridge` attaches to the model singletons,
which notify there.

## Discovery record and token

The token is generated per run and never persisted to config — a config file
gets copied between machines, committed by accident, or pasted into a bug
report, and a credential in one leaks eventually. Instead it lives in a file
beside the other app data, owner-only permissions, deleted on shutdown, named
after the process (MAGDA allows more than one running instance, so one shared
file would let the second instance's start hide the first, and the first
instance's stop would delete a record for the instance still running):

```
remote-api-<pid>.json
{"port":51734,"token":"...","url":"ws://127.0.0.1:51734/rpc",
 "mcpPort":51735,"mcpUrl":"http://127.0.0.1:51735/mcp","pid":4021}
```

This is the same shape Jupyter uses for its local server: a client on the
machine needs no configuration, a process elsewhere gets nothing. Both
transports are named in one record because a client picks one (an MCP host
wants `mcpUrl`, a script wanting pushed state wants `url`); the token is
shared because it authenticates the user at the keyboard, not a protocol.

`start()` collects any record left behind by a crashed process, but an
instance only ever deletes its own live record. `publishRecord()` rewrites
the file after every start/stop/rotation, and deletes it once neither
transport is up — the record is a live projection of what's listening, not a
mirror of config, because a port in it that nothing answers on is exactly the
failure mode it exists to prevent.

Disabled means disabled: `start()` returns false and opens no socket when the
feature is off, no token could be generated, or the port is taken. There's no
partial state where MAGDA listens but nobody can authenticate.

## Per-transport control (#2142)

`startTransport`/`stopTransport`/`rotateToken` all take a `Transport` and
touch only that one: separate credentials, separate discovery-record entries,
separate connection lists. Rotating one transport's token disconnects only
its clients and leaves the other's alone — re-credentialling a misbehaving
script no longer drops an AI host mid-conversation. That's granularity, not
isolation: both tokens still live in the same owner-only file and both doors
open onto the same dispatcher and the same grants.

## stop() vs. stopListening()

`stop()` is the destruction path: it also shuts the dispatcher and
subscription hub down permanently (`RemoteApiService::shutdown()` never comes
back), so `start()` must never be called afterward — it would bind listeners
around a dispatcher that answers every request with `Cancelled`.

`stopListening()` is what the settings toggle uses: sockets close and the
token is withdrawn, but the dispatcher, bridge, and hub stay alive, so a later
`start()` produces a working server again.

## GrantWriter

Grant changes come from two places — the settings dialog (message thread) and
a client connecting for the first time (a transport thread) — and both are
persisted through `persistGrants()`/`GrantWriter`, so config writes have to be
serialized and ordered. `mutex` guards `latest`/`posted` and is held only
briefly; `applyMutex` serializes the actual file write and is always taken
before `mutex`, never after, so a transport thread recording a newer grant is
never blocked behind a save — it just leaves `latest` for whoever is inside
the write to pick up. Whoever finds `posted == false` owns the hop to the
message thread; every later caller just replaces `latest`, so the newest
state always wins and a burst of changes costs one write. `GrantWriter` is
held by `shared_ptr` so a queued write can outlive the host.

## Member declaration order

Declaration order is destruction order reversed, and it's load-bearing: a
transport's connections deregister from the hub, the hub listens to the
service's change source, and the bridge writes into the service — each has to
outlive what talks to it. `clients_` (the `RemoteClientRegistry`) comes first
because both transports hold a raw pointer to it. `audit_` is a `shared_ptr`
rather than a `unique_ptr` because declaration order alone isn't enough for
it: a dispatch completion carrying it can still be queued on the message
thread when the host is destroyed, and member ordering only governs what
happens *inside* the destructor — whoever still holds a share keeps it alive.
