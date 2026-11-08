# AI

The **AI** section of [Settings](preferences.md) configures the providers and models behind MAGDA's AI features: the [AI Assistant](../panels/ai-assistant.md) chat, the per-device sound-design panels, the [Media Library](../panels/media-library.md)'s Sample Analyzer, and [Stem Separation](../stem-separation.md). It has three tabs.

| Tab | Purpose |
|-----|---------|
| **Assistant** | Choose which providers the AI agents use, and how they trade off quality, speed, and cost |
| **Providers** | Add API keys for cloud providers (OpenAI, Anthropic, Gemini, DeepSeek, OpenRouter) |
| **Local models** | Set up the embedded local model, and download every optional model |

Changes wait for **Apply**, except downloads, removals and model folders on the **Local models** tab, which take effect straight away.

## Assistant

The Assistant tab decides which providers the AI agents actually use. A status in the tab row sums up the current choice. **Setup** picks how much control you want:

- **Simple** — one decision that applies to every agent.
- **Advanced** — a provider and model per agent role.

### Simple setup

| Setting | Description |
|---------|-------------|
| **Run on** | **Local** (every agent uses a local model), **Cloud** (every agent uses a cloud provider), or **Hybrid** (a mix) |
| **Source** | In Local mode: **Embedded** (the built-in llama.cpp engine, set up under [Local models > Local LLM](#local-llm)) or **Local server** (an OpenAI-compatible server you run yourself) |
| **Provider** | Which cloud provider to use, from the keys on the [Providers](#providers) tab. Shown for Cloud and Hybrid |
| **Optimize for** | **Quality** or **Cost** in Cloud mode; **Speed** or **Cost** in Hybrid mode |

In **Local** mode the provider and optimize options are hidden, since everything runs locally.

#### Local Server (OpenAI-compatible)

With **Source** set to **Local server**, MAGDA talks to a server that speaks the OpenAI API, such as **Ollama**, **LM Studio**, **GPUStack**, or a plain `llama.cpp` server.

- **Model** — pick the model the server should use. **Refresh** asks the server for its model list; you can also type a model id directly.

MAGDA looks for the server at `http://localhost:11434/v1`, Ollama's default address. To use another address, or a server that needs an API key, quit MAGDA and set `localServerUrl` and `localServerApiKey` in the `ai` block of [`config.json`](../reference/troubleshooting.md#config-file-location).

One server and one model serve all agent roles, the same way the embedded model does.

### Advanced setup

Advanced replaces the single choice with a grid, one row per agent role. Each row has its own **provider** and **model**, drawn from the keys on the Providers tab and the local backends available. Switching from Simple to Advanced seeds the grid from your simple settings, so you can start there and adjust only the rows you care about.

| Agent | Handles |
|-------|---------|
| **Command** | Turning chat requests into [DSL](../reference/dsl.md) operations on tracks, clips, and devices |
| **Music** | Generating musical content — notes, patterns, parts |
| **Faust** | Writing [Faust DSP](../devices/effects.md) code for custom effects |
| **Chord** | Chord progressions and harmony |
| **Controller** | [Controller](controllers.md) profiles and scripts |
| **Theme** | Colour themes |

Choosing a local provider gives a model dropdown of **Embedded** or **Server**. The **Command** row has a third option, **Fast Inference (Command)** — see below.

### Fast Inference (Command)

**Fast Inference** runs Command requests entirely on your machine with a purpose-built model rather than a general-purpose LLM. It is not a chat model: it reads one command and emits the DSL directly, with no streaming and effectively no wait.

Because it is a single-command tagger, it deliberately ignores conversation history — each request is read on its own. For follow-ups that depend on what you said earlier ("now make it louder"), a general model is the better fit.

Fast Inference works out of the box using a small built-in model. Installing the larger [Command model](#command-model) from the Local models tab replaces it and markedly improves how well natural phrasing is understood. Cloud providers never use either one.

### Tools

Switch on **Faust DSP** to have AI-generated Faust code checked before MAGDA loads it. It needs Node.js: MAGDA starts the checker on demand through `npx`, so there is nothing else to install, but `npx` must be on your system `PATH`. The row says whether `npx` was found, and **Get Node.js** opens the Node.js download page. See [Effects — Faust validation](../devices/effects.md) for details.

## Providers

The Providers tab holds API keys for hosted LLM providers. The tab row shows how many keys you have added.

To add a key, pick the provider in the add row (OpenAI, Anthropic, Gemini, DeepSeek, or OpenRouter), paste the key, then click **Test** to check it and **Add** to keep it. **Test** sends one small request to the provider.

Added keys are listed under **API keys**, each with its own **Test** and **Remove**. A provider that already has a key is greyed out in the dropdown. Choose between providers on the [Assistant](#assistant) tab.

!!! note "Keys stay on your machine"
    API keys are saved in MAGDA's settings file and used only to call the provider you entered them for.

## Local models

Every optional download lives on the Local models tab, behind the **Type** dropdown: **Local LLM**, **Sample analysis**, **Tempo detection**, **Stem separation** and **Command model**.

!!! note "Intel Macs"
    Sample analysis, Tempo detection, Stem separation and the Command model run on the ONNX runtime, which the Intel Mac build of MAGDA doesn't include. On an Intel Mac only **Local LLM** is listed. **Transcribe to MIDI** is unavailable there too, and **Split into Stems** offers HPSS only.

### Local LLM

Sets up the embedded **llama.cpp** engine for fully offline AI, with no network calls and no API key.

- **Download a model…** — downloads MAGDA's own model, fine-tuned for DAW operations, to a place you choose.
- **File (.gguf)** — the model file. **Choose…** points at any GGUF model of your own.
- **GPU layers** — **Auto (GPU)** offloads everything the GPU can take: Metal on macOS, and Vulkan on Windows and Linux when a Vulkan driver is installed (without one, the model runs on the CPU). **CPU Only** runs without the GPU, and **Custom** lets you enter a layer count for cards that run out of memory on Auto.
- **Context length** — the context-window size in tokens (default `4096`).
- **Load model** / **Unload** — load the model into memory or unload it to free resources. The tab row shows which model is loaded.
- **Load model on startup** — when enabled, the model loads automatically each time MAGDA launches.

### Sample analysis

The Sample Analyzer is the audio-tagging model behind the [Media Library](../panels/media-library.md)'s semantic search and "find similar sounds". It is an optional download, separate from the LLM models.

- The page shows the current install state and the **download size**.
- **Download Sample Analyzer** — fetches the model. Without it, the Library still supports filename, tag, family, shape, key, and BPM filtering, but not text-based semantic search.
- **Models location** — where the model is kept. **Browse...** moves it to another folder, **Reset** goes back to the default.
- **Load** / **Unload** — bring the analyzer into memory or release it. It also preloads in the background the first time you enter Library mode.
- **Load on startup** — load the analyzer each time MAGDA launches.

For how the analyzer is used once installed, see [Media Library > AI Sample Analyzer](../panels/media-library.md#ai-sample-analyzer).

### Tempo detection

The beat tracker measures the tempo of samples whose file name and header don't carry one, when the Library indexes them, and lets a clip go into beat mode without you typing a BPM. Without it MAGDA falls back to an estimate that is right far less often. **Download Beat Tracker** fetches it, and **Remove** frees the disk space again.

### Stem separation

Weights for the machine-learning engines behind [Split into Stems](../stem-separation.md). The HPSS engine needs nothing here — it is pure signal processing and always available.

| Model | Size | Splits into |
|-------|------|-------------|
| **Demucs (4-stem)** | 301 MB | Drums, Bass, Other, Vocals |
| **Spleeter (2-stem)** | 74 MB | Vocals, Accompaniment |

Each row shows its install state, a progress bar while downloading, and a **Download** / **Remove** button, alongside a link to the HuggingFace repository the weights come from. **Models location** shows where they are stored.

Choosing an uninstalled engine from a clip's **Split into Stems** menu opens this page directly.

### Command model

The larger model behind [Fast Inference](#fast-inference-command): a DeBERTa-v3 encoder fine-tuned to read a command and tag its intent and parameters.

- It is **not bundled** with MAGDA — the download is 430 MB.
- Without it, Fast Inference still works using the small built-in model. With it, natural phrasing is understood far more reliably.
- **Models location** defaults to MAGDA's data folder. Use **Browse...** to keep the weights on another drive, or **Reset** to go back to the default.
- The **Download** button turns into **Cancel** while a download is running, and **Remove** once installed. The location cannot be changed mid-download.
- A link to the source HuggingFace repository sits under the model name.
