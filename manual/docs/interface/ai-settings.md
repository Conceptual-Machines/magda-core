# AI Settings

The **AI Settings** dialog is where you configure the providers and models that power MAGDA's AI features: the [AI Assistant](../panels/ai-assistant.md) chat, the per-device sound-design panels, the [Media Library](../panels/media-library.md)'s Sample Analyzer, and [Stem Separation](../stem-separation.md).

Open it from **Settings > AI Settings**. The dialog has three tabs.

| Tab | Purpose |
|-----|---------|
| **Cloud** | Register cloud API providers (OpenAI, Anthropic, Gemini, DeepSeek, OpenRouter) |
| **Config** | Choose which providers each agent uses, and how they trade off quality, speed, and cost |
| **Models** | Download and manage every optional local model, grouped by category |

## Cloud

The Cloud tab registers API keys for hosted LLM providers.

To add a provider:

1. Pick the **Provider** from the dropdown (OpenAI, Anthropic, Gemini, DeepSeek, or OpenRouter).
2. Enter your **API Key**.
3. Click **Test** to verify the key, then **Add** to register it.

Registered providers appear in the list below. You can register several and switch between them from the [Config](#config) tab. Click **Remove** to delete a provider. A provider already in the list is greyed out in the dropdown so you do not add it twice.

!!! note "Keys stay on your machine"
    API keys are stored locally in your MAGDA configuration and used only to call the provider you entered them for.

## Config

The Config tab decides which providers the AI agents actually use. The **Setup** dropdown at the top picks how much control you want:

- **Simple** — one decision that applies to every agent.
- **Advanced** — a provider and model per agent role.

### Simple setup

| Setting | Description |
|---------|-------------|
| **Mode** | **Local** (every agent uses a local model), **Cloud** (every agent uses a cloud provider), or **Hybrid** (a mix) |
| **Source** | In Local mode: **Embedded** (the built-in llama.cpp engine, configured under [Models > Local LLM](#local-llm)) or **Local server** (an OpenAI-compatible server you run yourself) |
| **Provider** | Which registered cloud provider to use, shown for Cloud and Hybrid modes |
| **Optimize** | Bias agent selection toward **Quality**, **Speed**, or **Cost** |

In **Local** mode the provider and optimize options are hidden, since everything runs locally.

#### Local Server (OpenAI-compatible)

With **Source** set to **Local server**, MAGDA talks to any server that speaks the OpenAI API — **LM Studio**, **Ollama**, **GPUStack**, or a plain `llama.cpp` server. The default address is Ollama's (`http://localhost:11434/v1`); MAGDA normalises whatever form you give it, with or without the `/v1` suffix.

- **Model** — pick the model the server should use. **Refresh** queries the server's model list; you can also type a model id directly.
- An API key/token is only needed for servers that require one (for example GPUStack); the others run without it.

One server and one model serve all agent roles, the same way the embedded model does.

### Advanced setup

Advanced replaces the single mode with a grid, one row per agent role. Each row has its own **provider** and **model**, drawn from whatever you registered on the Cloud tab and whatever local backends are available. Switching from Simple to Advanced seeds the grid from your simple settings, so you can start there and adjust only the rows you care about.

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

Fast Inference works out of the box using a small built-in model. Installing the larger [Command model](#command-model) from the Models tab replaces it and markedly improves how well natural phrasing is understood. Cloud providers never use either one.

### MCP Tools

**Faust DSP** validates AI-generated Faust code before MAGDA loads it. MAGDA starts the MCP server on demand through `npx`, so there is nothing to install by hand, but `npx` must be on your system `PATH` (it ships with Node.js). See [Effects — Faust validation](../devices/effects.md) for details.

## Models

Every optional download lives on the Models tab, behind a **Category** dropdown. Categories appear only when your build supports them — the ONNX-backed ones (Sample analysis, Stem separation, Command model) are absent from the Intel macOS build.

Downloads and removals take effect straight away; they do not wait for **OK**.

### Local LLM

Configures the embedded **llama.cpp** engine for fully offline AI, with no network calls and no API key.

- **Download Model** — downloads the MAGDA model (fine-tuned for DAW operations) from HuggingFace with a progress indicator.
- **Model (.gguf)** — the path to the model file. Use the browse button to point at any GGUF model of your own.
- **GPU Layers** — **Auto (GPU)** offloads everything the platform can take (Metal on macOS, CUDA on supported builds), **CPU Only** runs without the GPU, and **Custom** lets you enter a layer count for cards that run out of memory on Auto.
- **Context** — the context-window size in tokens (default `4096`).
- **Load Model** / **Unload** — load the model into memory or unload it to free resources.
- **Load model on startup** — when enabled, the model loads automatically each time MAGDA launches.

### Sample analysis

The Sample Analyzer is the audio-tagging model behind the [Media Library](../panels/media-library.md)'s semantic search and "find similar sounds". It is an optional download, separate from the LLM models.

- The page shows the current install state and the **download size**.
- **Download Sample Analyzer** — fetches the model. Without it, the Library still supports filename, tag, family, shape, key, and BPM filtering, but not text-based semantic search.
- **Load** / **Unload** — bring the analyzer into memory or release it. It also preloads in the background the first time you enter Library mode.

For how the analyzer is used once installed, see [Media Library > AI Sample Analyzer](../panels/media-library.md#ai-sample-analyzer).

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

- It is **not bundled** with MAGDA — the download is 429 MB.
- Without it, Fast Inference still works using the small built-in model. With it, natural phrasing is understood far more reliably.
- **Models location** defaults to MAGDA's data folder. Use **Browse...** to keep the weights on another drive, or **Reset** to go back to the default.
- The **Download** button turns into **Cancel** while a download is running, and **Remove** once installed. The location cannot be changed mid-download.
- A link to the source HuggingFace repository sits under the model name.
