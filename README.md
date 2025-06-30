# 🎹 Magda — Multi-Agent Generic DAW API

**Magda** is an experimental framework and protocol for building AI-driven Digital Audio Workstations (DAWs). It enables multiple intelligent agents to collaboratively compose, arrange, and manipulate music in real time, using a unified API and server-based communication model.

> **M.A.G.D.A.** stands for **Multi-Agent Generic DAW API** — a backend-agnostic system designed to make DAWs programmable, dynamic, and intelligent.

---

## ✨ Vision

Magda serves as the middleware layer between a music production engine and intelligent agents (e.g., LLMs, MIDI generators, mixing assistants). The goal is to create a modular, programmable DAW architecture where agents can:
- Assist with music composition and arrangement
- Automate track editing and mixing tasks
- Control playback and transport
- Respond to user prompts or other agents

---

## 🧠 Key Components

### ✅ MCP Server (Multi-agent Control Protocol)
A WebSocket-based server that agents connect to. Handles:
- Registration and capability discovery
- Routing JSON-based commands
- Sending asynchronous DAW state events

### ✅ Generic API
An abstract control layer that defines what agents can do. Example modules:
- `TransportInterface`: play, stop, locate
- `TrackInterface`: add, mute, delete tracks
- `ClipInterface`: insert MIDI clips
- `MixerInterface`: volume, pan, FX routing
- `PromptInterface`: bridge to language models

### ✅ Host Integration (coming soon)
Adapters for real DAW backends (starting with Tracktion Engine) to bind the Magda API to actual audio and MIDI operations.

---

## 🧪 Status

Magda is in early research and prototyping. It is **not yet ready for production use**, but contributors and feedback are welcome as we design the core protocols and data model.

---

## 📦 Example Command

```json
{
  "command": "addMidiClip",
  "trackId": "track_1",
  "start": 4.0,
  "length": 2.0,
  "notes": [
    { "note": 60, "velocity": 100, "start": 0.0, "duration": 0.5 }
  ]
}
