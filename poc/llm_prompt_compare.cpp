// Quick comparison: compact instructions vs DSL grammar for local LLM
// Build: make debug, then run manually
// Usage: cmake-build-debug/poc/llm_prompt_compare /path/to/model.gguf

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "../magda/agents/llama_model_manager.hpp"

// ── Compact instruction prompt (current command_agent) ──────────────
static const char* COMPACT_PROMPT = R"(You are MAGDA, an AI assistant for a DAW.
Respond ONLY with compact instructions. No prose. No markdown. One instruction per line.

INSTRUCTIONS:
  TRACK <name>                              - Create new track
  DEL <id>                                  - Delete track by index
  MUTE <name>                               - Mute tracks by name
  SOLO <name>                               - Solo tracks by name
  SET [id] key=val key=val ...              - Set track props (vol, pan, mute, solo, name)
  CLIP [id] <bar> <length_bars> [name]      - Create clip, optional name
  FX <fx_alias>                             - Add effect to current track

EXAMPLES:
"create a bass track" -> TRACK Bass
"add a 4 bar clip at bar 1 on track 2" -> CLIP 2 1 4
"set volume of track 3 to -6 dB and pan left" -> SET 3 vol=-6 pan=-1
"create 3 clips" ->
CLIP 1 4
CLIP 5 4
CLIP 9 4
)";

// ── DSL prompt (functional method-chaining style) ──────────────────
static const char* DSL_PROMPT = R"(You are MAGDA, an AI assistant for a DAW.
Respond ONLY with DSL code. No prose. No markdown. One statement per line.

DSL SYNTAX:
- track(name="Bass") - Create or reference track
- track(name="Bass", new=true) - Always create new track
- track(id=1) - Reference track by index
- .clip.new(bar=1, length_bars=4) - Create MIDI clip
- .clip.new(length_bars=4) - Create clip auto-placed after last
- .track.set(name="X", volume_db=-3, pan=0.5, mute=true)
- .fx.add(name="reverb") - Add effect
- .delete() - Delete track
- .notes.add(pitch=C4, beat=0, length=1, velocity=100)
- .notes.add_chord(root=C4, quality=major, beat=0, length=1)

EXAMPLES:
"create a bass track" -> track(name="Bass", new=true)
"add a 4 bar clip at bar 1 on track 2" -> track(id=2).clip.new(bar=1, length_bars=4)
"set volume of track 3 to -6 dB and pan left" -> track(id=3).track.set(volume_db=-6, pan=-1)
"create 3 clips on track 1" ->
track(id=1).clip.new(bar=1, length_bars=4)
track(id=1).clip.new(bar=5, length_bars=4)
track(id=1).clip.new(bar=9, length_bars=4)
"create a track, add reverb and a clip with a C major chord" ->
track(name="Pad", new=true).fx.add(name="reverb").clip.new(bar=1, length_bars=4).notes.add_chord(root=C4, quality=major, beat=0, length=4)
)";

// ── Test prompts ────────────────────────────────────────────────────
static const std::vector<std::string> TEST_PROMPTS = {
    "create a bass track",
    "rename this track to Synth Lead",
    "add a 2 bar clip at bar 5",
    "create 5 clips of different lengths",
    "add reverb and delay to the vocals track",
    "set volume to -6 dB and mute track 2",
    "add a clip with a C major chord at bar 1",
    "create a drum track with 4 clips, each 2 bars long",
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <model.gguf>" << std::endl;
        return 1;
    }

    // Load model
    magda::LlamaModelManager::Config cfg;
    cfg.modelPath = argv[1];
    cfg.gpuLayers = -1;
    cfg.contextSize = 4096;

    std::cout << "Loading model: " << cfg.modelPath << std::endl;
    if (!magda::LlamaModelManager::getInstance().loadModel(cfg)) {
        std::cerr << "Failed to load model" << std::endl;
        return 1;
    }
    std::cout << "Model loaded.\n" << std::endl;

    auto& mgr = magda::LlamaModelManager::getInstance();

    for (const auto& prompt : TEST_PROMPTS) {
        std::cout << "================================================================"
                  << std::endl;
        std::cout << "PROMPT: " << prompt << std::endl;
        std::cout << "================================================================"
                  << std::endl;

        // Run with compact prompt
        {
            magda::LlamaModelManager::InferenceRequest req;
            req.systemPrompt = COMPACT_PROMPT;
            req.userMessage = prompt;
            req.temperature = 0.1f;
            req.maxTokens = 256;

            auto t0 = std::chrono::steady_clock::now();
            auto result = mgr.infer(req);
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::cout << "\n--- COMPACT (" << (int)ms << "ms) ---" << std::endl;
            if (result.success)
                std::cout << result.text << std::endl;
            else
                std::cout << "ERROR: " << result.error << std::endl;
        }

        // Run with DSL prompt
        {
            magda::LlamaModelManager::InferenceRequest req;
            req.systemPrompt = DSL_PROMPT;
            req.userMessage = prompt;
            req.temperature = 0.1f;
            req.maxTokens = 256;

            auto t0 = std::chrono::steady_clock::now();
            auto result = mgr.infer(req);
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::cout << "\n--- DSL (" << (int)ms << "ms) ---" << std::endl;
            if (result.success)
                std::cout << result.text << std::endl;
            else
                std::cout << "ERROR: " << result.error << std::endl;
        }

        std::cout << std::endl;
    }

    mgr.unloadModel();
    return 0;
}
