#include "sound_design_agent.hpp"

#include <juce_events/juce_events.h>

#include <stdexcept>

#include "api/magda_api.hpp"
#include "api/plugin_api.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "four_osc_agent.hpp"
#include "four_osc_apply.hpp"
#include "generic_sound_design_agent.hpp"
#include "internal_plugins.hpp"
#include "poly_step_sequencer_agent.hpp"
#include "poly_step_sequencer_apply.hpp"
#include "step_sequencer_agent.hpp"
#include "step_sequencer_apply.hpp"

namespace magda {

namespace {

// 4OSC-specific implementation. Wraps the existing FourOscAgent + the
// shared applyFourOscPresetToPath helper. New devices add their own
// SoundDesignAgent subclass and declare its handler in the capability catalog.
class FourOscSoundDesignAgent : public SoundDesignAgent {
  public:
    explicit FourOscSoundDesignAgent(PluginApi* plugins) : plugins_(plugins) {}

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        if (plugins_ == nullptr)
            return "(MagdaApi plugin operations are unavailable)";

        juce::ignoreUnused(conversation);  // 4OSC preset design is single-shot
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        FourOscAgent::GenerateResult result;
        if (onToken) {
            // Forward LLM tokens to the caller; bail early when the caller
            // returns false or we've been asked to stop.
            auto fwd = [this, &onToken](const juce::String& token) -> bool {
                if (shouldStop_.load())
                    return false;
                return onToken(token);
            };
            result = agent_.generateStreaming(prompt.toStdString(), fwd);
        } else {
            result = agent_.generate(prompt.toStdString());
        }
        if (shouldStop_.load())
            return "cancelled";

        if (result.hasError)
            return juce::String("error: ") + juce::String(result.error);

        // Override the model's category pick if the caller asked us to.
        if (categoryOverride_.isNotEmpty())
            result.preset.category = categoryOverride_.toStdString();

        // The live API apply step updates device state and notifies the track,
        // operations which must run on the message thread. Hop there and
        // block until it finishes — generation runs on a worker thread and
        // we still want a synchronous status to return.
        auto& mm = *juce::MessageManager::getInstance();
        if (mm.isThisTheMessageThread())
            return applyFourOscPresetToPath(*plugins_, result.preset, path);

        // Heap-allocated shared state so the queued lambda stays safe even
        // if this worker thread gets force-killed (~AIPanelComponent runs
        // stopThread(2000); the lambda may fire after that). The lambda
        // owns a shared_ptr too, so the state outlives whichever side
        // disappears first.
        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        const auto preset = result.preset;
        auto* plugins = plugins_;
        mm.callAsync([state, preset, path, plugins]() {
            // Uncaught here runs on the message thread and would propagate into
            // JUCE's dispatch loop instead of just failing this apply (#2395).
            try {
                state->status = applyFourOscPresetToPath(*plugins, preset, path);
            } catch (const std::exception& e) {
                state->status = juce::String("error: ") + e.what();
            } catch (...) {
                state->status = "error: unknown exception";
            }
            state->done.signal();
        });

        // Poll in short slices so cancel returns promptly (the queued lambda
        // still runs harmlessly; shared_ptr keeps `state` alive).
        constexpr int sliceMs = 50;
        constexpr int maxMs = 5000;
        for (int waited = 0; waited < maxMs; waited += sliceMs) {
            if (state->done.wait(sliceMs))
                return state->status;
            if (shouldStop_.load())
                return "cancelled";
        }
        return "apply timed out";
    }

    void setCategoryOverride(const juce::String& category) override {
        categoryOverride_ = category;
    }

    void requestCancel() override {
        shouldStop_ = true;
        agent_.requestCancel();
    }

  private:
    FourOscAgent agent_;
    juce::String categoryOverride_;
    PluginApi* plugins_ = nullptr;
};

// Build a device-context string for the Poly Step Sequencer agent.
// Reads viewMode from the plugin and, if a DrumGridPlugin is downstream on
// the same track, adds a lane map ("note N = <name>").
// Thread-safe: getPlugin uses a ScopedLock; CachedValue reads are value-copies
// (atomic-equivalent) safe off the message thread; getChains() is read-only
// after construction and stable while the edit is live.
std::string buildPolyStepSequencerContext(PluginApi& plugins, const ChainNodePath& path) {
    const auto context = plugins.getPolySequencerContext(path);
    if (!context)
        return {};

    std::string ctx = "DEVICE CONTEXT:\n";
    const auto viewMode = context->viewMode.toStdString();
    ctx += "viewMode=" + (viewMode.empty() ? "keys" : viewMode) + "\n";

    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "currentSettings: numSteps=%d, rate=%d, swing=%.2f, gateLength=%.2f\n",
                  context->numSteps, context->rate, static_cast<double>(context->swing),
                  static_cast<double>(context->gateLength));
    ctx += buf;

    if (!context->laneNames.empty()) {
        ctx += "LANE MAP:\n";
        for (const auto& [note, name] : context->laneNames)
            ctx += "note " + std::to_string(note) + " = " + name.toStdString() + "\n";
    }

    return ctx;
}

// Poly Step Sequencer -- generate patterns from a prompt.
class PolyStepSequencerSoundDesignAgent : public SoundDesignAgent {
  public:
    explicit PolyStepSequencerSoundDesignAgent(PluginApi* plugins) : plugins_(plugins) {}

    juce::String getUserCaveat() const override {
        return "note: generated pattern is a starting point - edit steps to taste.";
    }

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        if (plugins_ == nullptr)
            return "(MagdaApi plugin operations are unavailable)";

        juce::ignoreUnused(conversation);  // pattern design is single-shot
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        // Read plugin state (view mode + downstream drum lane map) before
        // generation. buildPolyStepSequencerContext is safe off the message
        // thread: getPlugin uses a ScopedLock and the chain/CachedValue reads
        // are stable value-copies while the edit is live.
        const auto deviceContext = buildPolyStepSequencerContext(*plugins_, path);

        PolyStepSequencerAgent::GenerateResult result;
        if (onToken) {
            auto fwd = [this, &onToken](const juce::String& token) -> bool {
                if (shouldStop_.load())
                    return false;
                return onToken(token);
            };
            result = agent_.generateStreaming(prompt.toStdString(), fwd, deviceContext);
        } else {
            result = agent_.generate(prompt.toStdString(), deviceContext);
        }
        if (shouldStop_.load())
            return "cancelled";

        if (result.hasError)
            return juce::String("error: ") + juce::String(result.error);

        // Apply must run on the message thread (TE ValueTree asserts it).
        auto& mm = *juce::MessageManager::getInstance();
        if (mm.isThisTheMessageThread())
            return applyPolyStepSequencerPresetToPath(*plugins_, result.preset, path);

        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        const auto preset = result.preset;
        auto* plugins = plugins_;
        mm.callAsync([state, preset, path, plugins]() {
            // Uncaught here runs on the message thread and would propagate into
            // JUCE's dispatch loop instead of just failing this apply (#2395).
            try {
                state->status = applyPolyStepSequencerPresetToPath(*plugins, preset, path);
            } catch (const std::exception& e) {
                state->status = juce::String("error: ") + e.what();
            } catch (...) {
                state->status = "error: unknown exception";
            }
            state->done.signal();
        });

        constexpr int sliceMs = 50;
        constexpr int maxMs = 5000;
        for (int waited = 0; waited < maxMs; waited += sliceMs) {
            if (state->done.wait(sliceMs))
                return state->status;
            if (shouldStop_.load())
                return "cancelled";
        }
        return "apply timed out";
    }

    void requestCancel() override {
        shouldStop_ = true;
        agent_.requestCancel();
    }

  private:
    PolyStepSequencerAgent agent_;
    PluginApi* plugins_ = nullptr;
};

// Build a device-context string for the mono Step Sequencer agent.
// Reads the current numSteps/rate/swing/gateLength from the live plugin.
// Thread-safe: getPlugin uses a ScopedLock; CachedValue reads are value-copies.
std::string buildStepSequencerContext(PluginApi& plugins, const ChainNodePath& path) {
    const auto context = plugins.getStepSequencerContext(path);
    if (!context)
        return {};

    char buf[128];
    std::snprintf(
        buf, sizeof(buf),
        "DEVICE CONTEXT:\ncurrentSettings: numSteps=%d, rate=%d, swing=%.2f, gateLength=%.2f\n",
        context->numSteps, context->rate, static_cast<double>(context->swing),
        static_cast<double>(context->gateLength));
    return buf;
}

// Mono Step Sequencer -- generate 303-style patterns from a prompt.
class StepSequencerSoundDesignAgent : public SoundDesignAgent {
  public:
    explicit StepSequencerSoundDesignAgent(PluginApi* plugins) : plugins_(plugins) {}

    juce::String getUserCaveat() const override {
        return "note: generated pattern is a starting point - edit steps to taste.";
    }

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        if (plugins_ == nullptr)
            return "(MagdaApi plugin operations are unavailable)";

        juce::ignoreUnused(conversation);  // pattern design is single-shot
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        const auto deviceContext = buildStepSequencerContext(*plugins_, path);

        StepSequencerAgent::GenerateResult result;
        if (onToken) {
            auto fwd = [this, &onToken](const juce::String& token) -> bool {
                if (shouldStop_.load())
                    return false;
                return onToken(token);
            };
            result = agent_.generateStreaming(prompt.toStdString(), fwd, deviceContext);
        } else {
            result = agent_.generate(prompt.toStdString(), deviceContext);
        }
        if (shouldStop_.load())
            return "cancelled";

        if (result.hasError)
            return juce::String("error: ") + juce::String(result.error);

        // Apply must run on the message thread (TE ValueTree asserts it).
        auto& mm = *juce::MessageManager::getInstance();
        if (mm.isThisTheMessageThread())
            return applyStepSequencerPresetToPath(*plugins_, result.preset, path);

        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        const auto preset = result.preset;
        auto* plugins = plugins_;
        mm.callAsync([state, preset, path, plugins]() {
            // Uncaught here runs on the message thread and would propagate into
            // JUCE's dispatch loop instead of just failing this apply (#2395).
            try {
                state->status = applyStepSequencerPresetToPath(*plugins, preset, path);
            } catch (const std::exception& e) {
                state->status = juce::String("error: ") + e.what();
            } catch (...) {
                state->status = "error: unknown exception";
            }
            state->done.signal();
        });

        constexpr int sliceMs = 50;
        constexpr int maxMs = 5000;
        for (int waited = 0; waited < maxMs; waited += sliceMs) {
            if (state->done.wait(sliceMs))
                return state->status;
            if (shouldStop_.load())
                return "cancelled";
        }
        return "apply timed out";
    }

    void requestCancel() override {
        shouldStop_ = true;
        agent_.requestCancel();
    }

  private:
    StepSequencerAgent agent_;
    PluginApi* plugins_ = nullptr;
};

}  // namespace

std::unique_ptr<SoundDesignAgent> createSoundDesignAgentFor(const juce::String& pluginId,
                                                            MagdaApi* api) {
    auto* plugins = api != nullptr ? &api->plugins() : nullptr;
    switch (getInternalPluginCapabilities(pluginId).soundDesignAgent) {
        // 4OSC keeps its bespoke agent for now — its wave/filter/voice/FX
        // controls are ValueTree properties, not automatable parameters, so it
        // can't yet go through the generic path. TODO: make those controls
        // automatable and retire FourOscSoundDesignAgent / FourOscAgent.
        case SoundDesignAgentKind::FourOsc:
            return std::make_unique<FourOscSoundDesignAgent>(plugins);
        // The sequencers are MIDI generators with pattern-shaped output, not
        // parameter presets — they stay on their own bespoke agents.
        case SoundDesignAgentKind::PolyStepSequencer:
            return std::make_unique<PolyStepSequencerSoundDesignAgent>(plugins);
        case SoundDesignAgentKind::StepSequencer:
            return std::make_unique<StepSequencerSoundDesignAgent>(plugins);
        // Every other MAGDA sound generator (compiled Faust synths + native
        // Mutable ports). The only per-device inputs are the display name +
        // description used to prime the LLM.
        case SoundDesignAgentKind::Generic: {
            juce::String displayName = pluginId;
            juce::String description;
            if (const auto* spec = daw::audio::compiled::findCompiledPluginSpec(pluginId)) {
                displayName = spec->displayName;
                description = spec->description;
            } else if (const auto* spec = daw::audio::findInternalPluginSpec(pluginId)) {
                displayName = spec->displayName;
                description = spec->description;
            }
            return std::make_unique<GenericSoundDesignAgent>(pluginId, displayName, description);
        }
        case SoundDesignAgentKind::None:
            break;
    }

    return nullptr;
}

bool isSoundDesignSupported(const juce::String& pluginId) {
    return getInternalPluginCapabilities(pluginId).supportsSoundDesign();
}

std::unique_ptr<SoundDesignAgent> createSoundDesignAgentFor(const DeviceInfo& device,
                                                            MagdaApi* api) {
    if (auto internal = createSoundDesignAgentFor(device.pluginId, api))
        return internal;

    if (device.format == PluginFormat::Internal || device.aiSoundDesignerParameters.empty())
        return nullptr;

    juce::String description;
    if (device.manufacturer.isNotEmpty())
        description = "Made by " + device.manufacturer + ".";
    return std::make_unique<GenericSoundDesignAgent>(
        device.pluginId, device.name.isNotEmpty() ? device.name : device.pluginId, description,
        device.aiSoundDesignerParameters, device.aiSoundDesignerPrompt);
}

bool isSoundDesignSupported(const DeviceInfo& device) {
    return isSoundDesignSupported(device.pluginId) ||
           (device.format != PluginFormat::Internal && !device.aiSoundDesignerParameters.empty());
}

}  // namespace magda
