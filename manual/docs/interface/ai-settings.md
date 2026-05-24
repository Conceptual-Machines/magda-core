# AI Settings

The **AI Settings** dialog is where you configure the providers and models that power MAGDA's AI features: the [AI Assistant](../panels/ai-assistant.md) chat, the per-device sound-design panels, and the [Media Library](../panels/media-library.md)'s Sample Analyzer.

Open it from **Settings > AI Settings**. The dialog has four tabs.

| Tab | Purpose |
|-----|---------|
| **Cloud** | Register cloud API providers (OpenAI, Anthropic, Gemini, DeepSeek, OpenRouter) |
| **Local** | Configure the embedded llama.cpp engine and a local GGUF model for offline use |
| **Config** | Choose which providers the AI agents use and how they trade off quality, speed, and cost |
| **Sample Analyzer** | Download and load the audio-tagging model used by the Media Library |

## Cloud

The Cloud tab registers API keys for hosted LLM providers.

To add a provider:

1. Pick the **Provider** from the dropdown (OpenAI, Anthropic, Gemini, DeepSeek, or OpenRouter).
2. Enter your **API Key**.
3. Click **Test** to verify the key, then **Add** to register it.

Registered providers appear in the list below. You can register several and switch between them from the [Config](#config) tab. Click **Remove** to delete a provider. A provider already in the list is greyed out in the dropdown so you do not add it twice.

!!! note "Keys stay on your machine"
    API keys are stored locally in your MAGDA configuration and used only to call the provider you entered them for.

## Local

The Local tab configures the embedded **llama.cpp** engine for fully offline AI, with no network calls and no API key.

- **Download Model** - downloads the MAGDA model (`magda-v0.3.0-q4_k_m.gguf`, fine-tuned for DAW operations) from HuggingFace with a progress indicator.
- **Model (.gguf)** - the path to the model file. Use **Browse** to point at any GGUF model of your own.
- **GPU Layers** - how many model layers to offload to the GPU. `-1` offloads all layers (Metal on macOS, CUDA on supported builds); `0` runs on CPU only.
- **Context** - the context-window size in tokens (default `4096`).
- **Load Model** / **Unload** - load the model into memory or unload it to free resources.
- **Load model on startup** - when enabled, the model loads automatically each time MAGDA launches.

## Config

The Config tab decides which providers the AI agents actually use.

| Setting | Description |
|---------|-------------|
| **Mode** | **Local** (every agent uses the embedded llama model), **Cloud** (every agent uses a cloud provider), or **Hybrid** (a mix) |
| **Provider** | Which registered cloud provider to use, shown for Cloud and Hybrid modes |
| **Optimize** | Bias agent selection toward **Quality**, **Speed**, or **Cost** |

In **Local** mode the provider and optimize options are hidden, since everything runs on the embedded model.

## Sample Analyzer

The Sample Analyzer is the audio-tagging model behind the [Media Library](../panels/media-library.md)'s semantic search and "find similar sounds". It is an optional download, separate from the LLM models on the other tabs.

- The tab shows the current install state and the **download size**.
- **Download Sample Analyzer** - fetches the model. Without it, the Library still supports filename, tag, family, shape, key, and BPM filtering, but not text-based semantic search.
- **Load** / **Unload** - bring the analyzer into memory or release it. It also preloads in the background the first time you enter Library mode.

For how the analyzer is used once installed, see [Media Library > AI Sample Analyzer](../panels/media-library.md#ai-sample-analyzer).
