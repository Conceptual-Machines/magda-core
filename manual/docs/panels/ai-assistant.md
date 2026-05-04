# AI Assistant

MAGDA includes a built-in AI chat assistant that lets you control the DAW using natural language, and a DSL console for direct scripting.

![AI Assistant](../assets/images/panels/ai-assistant.png){ width="400" }

## Overview

The AI Assistant panel is located in the left panel. It has two tabs at the bottom: **AI** for natural-language interaction and **DSL** for direct script execution.

## AI Tab

Type a request in natural language and the assistant translates it into actions:

- "Add a MIDI track with a bass clip"
- "Transpose the selected notes up an octave"
- "Set the tempo to 120 BPM"
- "Mute tracks 3 and 4"

The assistant is **context-aware** — it knows which tracks, clips, and devices exist in your project and what is currently selected.

### Selection Context

At the bottom of the AI panel, a context indicator shows the currently-selected track, clip, or device.

- When the indicator is **on** (orange accent colour), the AI treats your selection as the default target. A request like "add a bassline" with a track selected will target that track instead of creating a new one.
- Click the indicator to **toggle it off**. The selection is no longer sent to the LLM, and requests are interpreted without a default target.

Creating a new track still works either way — just be explicit in the request ("new track", "another track") when the indicator is on.

### How It Works

1. You type a natural-language request in the chat
2. The assistant translates your request into MAGDA's internal DSL (domain-specific language)
3. The DSL commands are executed as actions in the project
4. The assistant confirms what was done

### Setup

The AI Assistant supports multiple LLM providers. Open the AI Settings dialog from **Settings > AI Settings** to configure your providers.

The settings dialog has three tabs:

| Tab | Description |
|-----|-------------|
| **Cloud** | Configure cloud API providers (Anthropic, OpenAI, DeepSeek, Gemini, OpenRouter) |
| **Local** | Configure the embedded llama.cpp engine with a local GGUF model |
| **Config** | Mode selection (Local / Cloud / Hybrid), provider, and optimization strategy |

To add a cloud provider:

1. Select the **Provider** from the dropdown
2. Enter your **API Key**
3. Click **Add** (or **Test** to verify the key first)

Registered providers appear in the list below. You can add multiple providers and switch between them. Click **X** to remove a provider.

#### Local Tab

The Local tab configures the embedded **llama.cpp** inference engine for fully offline AI. MAGDA includes a one-click downloader for the custom **magda-v0.3.0** GGUF model, fine-tuned for DAW operations.

- **Download Model** — Downloads the MAGDA model from HuggingFace with a progress indicator
- **Model Path** — Path to any `.gguf` model file (use the file browser to select a custom model)
- **GPU Layers** — Number of layers to offload to the GPU (-1 = all layers, for Metal/CUDA acceleration)
- **Context Size** — Context window size (default 4096)
- **Load / Unload** — Load the model into memory or unload it to free resources

#### Config Tab

The Config tab selects which providers the AI agents use:

| Setting | Description |
|---------|-------------|
| **Mode** | **Local** (all agents use embedded llama), **Cloud** (all agents use a cloud provider), or **Hybrid** (mix of local and cloud) |
| **Provider** | Which cloud provider to use (from the registered providers in the Cloud tab) |
| **Optimize** | **Cost** (prefer local where possible), **Speed** (prefer fastest), or **Quality** (prefer most capable model) |

### Usage Tips

- Be specific: "Add a reverb to Track 2" works better than "make it sound spacey"
- The assistant can handle multi-step requests: "Create 4 MIDI tracks and name them Kick, Snare, HiHat, Bass"
- Use it for repetitive tasks: "Set all tracks to -6 dB"
- Prefix a message with `/dsl` to execute DSL directly from the AI chat without making an AI call

## DSL Tab

![DSL Console](../assets/images/panels/dsl-repl.png){ width="300" }

The DSL tab provides a code editor with **syntax highlighting** for the MAGDA DSL. It's designed for users who want to script DAW operations directly without going through the AI.

### Editor Features

- **Syntax highlighting** — keywords (blue), methods (yellow), parameters (light blue), strings (orange), numbers (green), note names (teal), comments (green)
- **Direct execution** — commands run immediately against the DAW with no network calls
- **Command history** — results appear in the output area above the editor
- **Keyboard shortcuts**:

| Shortcut | Action |
|----------|--------|
| ++cmd+enter++ (Mac) / ++ctrl+enter++ (Win) | Execute code |
| ++cmd+l++ (Mac) / ++ctrl+l++ (Win) | Clear output |

### Quick Start

Switch to the DSL tab, type a command, and press ++cmd+enter++:

```
track(name="Bass", new=true).clip.new(bar=1, length_bars=4)
```

Type `help` and execute to see available commands.

## DSL Quick Reference

A few common commands to get started. For the full language reference, see **[DSL Reference](../reference/dsl.md)**.

```
track(name="Bass", new=true).clip.new(bar=1, length_bars=4)
  .notes.add_chord(root=C4, quality=major, beat=0, length=4)
  .notes.add(pitch=E2, beat=0, length=1, velocity=100)
track(name="Bass").fx.add(name="compressor")
track(name="Bass").track.set(volume_db=-6)
groove.set(template="Basic 8th Swing", strength=0.5)
```

### Slash Commands

Prefix your message with a slash command to constrain the AI to a specific domain:

| Command | Description |
|---------|-------------|
| `/groove <request>` | Create or apply swing/groove timing templates |
| `/design <description>` | Generate a 4OSC preset from a natural-language description and apply it to the focused 4OSC device |

Typing `/` shows an autocomplete popup with available commands.

#### `/design` — AI Sound Design

Select a 4OSC device, then type `/design <description>`. The assistant produces a preset (waves, filter type, voice mode, FX gates, ADSR, levels) and applies it directly to the focused device.

The chat shows a categorised summary of what changed, plus a one-line apply status. The preset name and category the AI chose become the default values when you save the preset from the device header — just click save and hit Enter.

Built-in safeguards:

- A **master-level safety cap** estimates worst-case peak gain from the active oscillator count, distortion drive, and filter resonance, then clamps the master `level` to keep peaks in a sensible range. The AI's choices are only overridden when they would clip.
- The result is a **starting point**, not a final preset. Tweak by ear before saving.

For example prompts and recipes, see the [4OSC Synth — AI Sound Design](../devices/4osc.md#ai-sound-design-design) section.

## Per-Device AI Panel

Sound-design generation is also available without leaving the device chain. Every device slot exposes an **AI** icon in its header — click it to open a docked panel attached to that device.

![4OSC with the AI panel docked on its left, showing a prompt echo, the model's preset description, an apply status, and a yellow "starting point only" disclaimer.](../assets/images/panels/4osc-ai-panel.png)

The panel has three rows:

- **Output area** — streams the model's response token-by-token, then appends a one-line apply status (`→ applied N params, M waves, …`). The history persists across slot rebuilds (preset loads, plugin reloads, sidechain edits) — your last result stays put until you clear it.
- **Prompt input** — type a description and press ++enter++ to submit. Submitting cancels any in-flight generation on the same device.
- **Footer** — shows the active model id on the left and a delete button on the right. The delete button clears the chat for that device only.

Generations are scoped to the device the panel is mounted on. Devices without a sound-design agent (everything except 4OSC at the moment) show **AI design not supported for this device** in place of the prompt placeholder.

This is the same engine as the chat-based `/design` command — same agent, same safeguards, same preset name/category propagation to the save dialog. The panel is just a more direct path: focus the synth, prompt it, hear it.

## Param Aliases (`@`)

The AI chat understands **`@aliases`** as a shorthand for paths. Type `@` in the chat input to open an autocomplete list of available aliases — focused track, focused device, named macros, named modulators. Picking one expands to the full path the AI agent resolves.

Useful for tying a request to a specific scope without spelling the path out:

```
@focused.macro = 0.7
modulate @focused.cutoff with a slow LFO
```

Aliases are resolved server-side to the same `ChainNodePath` the DSL uses, so any command that takes a path takes an alias.
