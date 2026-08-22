# Connections

The Connections dialog is where you say what is allowed to drive MAGDA from outside it. Open it from **Settings > Connections**. It has four tabs.

| Tab | Purpose |
|-----|---------|
| **MCP** | The endpoint an AI host connects to, and the config to paste into it |
| **WebSocket** | The socket a script or control surface connects to |
| **OSC** | Listen for Open Sound Control, on which interface and port |
| **Clients** | Grant each remote client what it may do, and see what it has done |

None of this is AI-specific. An AI assistant is one kind of client, but the same permissions cover a Lua script, a bespoke controller, or anything else holding a token. Model and provider configuration lives in [AI Settings](ai-settings.md); MIDI profiles and Lua scripts live in [Controllers](controllers.md).

## MCP and WebSocket are two separate listeners

This is the thing most worth understanding before you touch either switch.

MAGDA's **remote API** — the thing that lets an outside program inspect and control your session — is served over **two transports**:

- **MCP**, which is what an AI host such as Claude speaks.
- **WebSocket**, which is what a script, a custom tool, or a control surface speaks, and which also pushes changes back to you as they happen.

They are genuinely separate. Each has its own switch, its own port, and its own token. Turning one on does not turn on the other, and neither needs the other to work — if you only ever use an AI host, leave the WebSocket off; if you drive MAGDA from a Python script and use no AI host, leave MCP off.

What they *share* is everything past the socket: the same set of operations, and the same per-client permissions on the **Clients** tab. A client granted `edit` gets `edit` whichever transport it arrives over.

**OSC is not one of them.** It is a different protocol on a different stack, with no token, no client list, and no permissions — an OSC surface can do what OSC surfaces can do, and it will never appear on the Clients tab. It is in this dialog because it is another way into MAGDA from outside, not because it shares any of the above.

## MCP

- **Enable the MCP endpoint** — Opens and closes the listener immediately, not at the next launch.
- **Rotate token** — Throws this endpoint's credential away and mints a new one, disconnecting every MCP client. WebSocket clients are unaffected. Clients that read the token file find the new one by themselves.

Below the toggle, MAGDA composes the exact JSON to paste into an MCP host's config, using this install's own paths, with a **Copy** button. It stays correct across restarts: the helper it names finds MAGDA's current port and token each time, so neither has to be written down.

On Linux inside an AppImage there is no stable path to publish, so the page shows a placeholder and says so — download `magda-mcp` from the release, put it somewhere permanent, and use that path.

## WebSocket

- **Enable the WebSocket API** — Same immediate effect, on its own listener.
- **Rotate token** — The mirror image of the MCP one: drops WebSocket clients only.

The page shows the URL to connect to, including the `?client=` query string. **Name your client there** — that name is what gets its own row and its own permissions on the Clients tab. A client that sends no name is lumped in with every other anonymous caller and stays read-only.

The token itself is never shown. It is regenerated every run and written to an owner-only file whose path the page gives you; read it from there rather than copying it, because a token pasted somewhere permanent stops being valid the moment MAGDA restarts.

## Clients

A client that connects for the first time can **read only**. If your assistant reports that it cannot create a track or start playback, that is not a bug — nothing has been granted yet.

Each client MAGDA has heard from over **either** transport gets a row, listed by the name it reported and tagged with the transport it came in over. Tick what you want it to be able to do:

| Permission | Lets the client |
|------------|-----------------|
| `read` | Inspect the project. Always on. |
| `edit` | Change tempo, tracks, clips, notes, devices, and automation |
| `transport` | Play, stop, record-arm, loop, and seek |
| `session` | Launch and stop session clips and scenes |
| `hardware-midi` | Reach physical MIDI ports. Nothing uses it yet. |

Changes apply to the client's next request. There is nothing to restart and no need to reconnect.

Each row also has **Disconnect**, which drops the client's current connections, and **Forget**, which discards its permissions. Forget does not disconnect: the client reverts to read-only and stays connected.

Grants are remembered between launches, keyed by the reported name. Two clients reporting the same name are one entry. A client that reports nothing is listed as `unknown` and shares one read-only entry with every other anonymous caller.

### Recent activity

Underneath the client list is a tail of what has actually been asked for, and how it was answered — so `tracks.create denied (edit)` appears next to the checkbox that would allow it.

## OSC

MAGDA can be driven by an Open Sound Control surface — a tablet layout, a lighting desk, anything that sends OSC.

- **Listen for OSC** — Whether the socket is open at all.
- **Listen on** — **All interfaces (0.0.0.0)** or **This computer only (127.0.0.1)**. OSC has no authentication, so which interfaces answer is the whole of its access control; loopback is the safe default if the surface runs on this machine.
- **Receive port** — Where MAGDA listens. Committed when the field loses focus, so typing part of a port number does not rebind the socket.
- **Feedback port** — Where MAGDA answers.

There is no "send feedback to" address. MAGDA reads the datagrams it receives, so it answers whoever is talking to it.

The status area below reports whether the socket is open, how many messages have arrived, and one line per surface MAGDA has heard from with its message counts and when it was last heard. A surface listed with nothing arriving and a surface never listed at all are different problems, which is why the list is there rather than just a counter.

If OSC is enabled but not listening, the port was taken by another application.
