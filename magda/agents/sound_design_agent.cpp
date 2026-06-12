#include "sound_design_agent.hpp"

#include <juce_events/juce_events.h>

#include "audio/AudioBridge.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "four_osc_agent.hpp"
#include "four_osc_apply.hpp"
#include "internal_plugins.hpp"
#include "poly_step_sequencer_agent.hpp"
#include "poly_step_sequencer_apply.hpp"
#include "step_sequencer_agent.hpp"
#include "step_sequencer_apply.hpp"

namespace magda {

namespace {

// 4OSC-specific implementation. Wraps the existing FourOscAgent + the
// shared applyFourOscPresetToPath helper. New devices add their own
// SoundDesignAgent subclass and a branch in createSoundDesignAgentFor.
class FourOscSoundDesignAgent : public SoundDesignAgent {
  public:
    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
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

        // The apply step writes onto te::Plugin / te::FourOscPlugin state and
        // calls notifyTrackDevicesChanged, both of which TE asserts must run
        // on the message thread (TRACKTION_ASSERT_MESSAGE_THREAD). Hop there
        // and block until it finishes — generation runs on a worker thread
        // and we still want a synchronous status to return.
        auto& mm = *juce::MessageManager::getInstance();
        if (mm.isThisTheMessageThread())
            return applyFourOscPresetToPath(result.preset, path);

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
        mm.callAsync([state, preset, path]() {
            state->status = applyFourOscPresetToPath(preset, path);
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
};

// Build a device-context string for the Poly Step Sequencer agent.
// Reads viewMode from the plugin and, if a DrumGridPlugin is downstream on
// the same track, adds a lane map ("note N = <name>").
// Thread-safe: getPlugin uses a ScopedLock; CachedValue reads are value-copies
// (atomic-equivalent) safe off the message thread; getChains() is read-only
// after construction and stable while the edit is live.
std::string buildPolyStepSequencerContext(const ChainNodePath& path) {
    auto& tm = TrackManager::getInstance();
    auto* engine = tm.getAudioEngine();
    auto* bridge = engine ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return {};

    auto plugin = bridge->getPlugin(path);
    auto* seq = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
    if (seq == nullptr)
        return {};

    std::string ctx = "DEVICE CONTEXT:\n";
    const auto viewMode = seq->viewMode.get().toStdString();
    ctx += "viewMode=" + (viewMode.empty() ? "keys" : viewMode) + "\n";

    // Emit current settings so the model knows not to change them unless asked.
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "currentSettings: numSteps=%d, rate=%d, swing=%.2f, gateLength=%.2f\n",
                      seq->numSteps.get(), seq->rate.get(), static_cast<double>(seq->swing.get()),
                      static_cast<double>(seq->gateLength.get()));
        ctx += buf;
    }

    // Walk the owner track for a downstream DrumGridPlugin (mirrors the
    // findDownstreamDrumGrid logic in PolyStepSequencerUI).
    auto* track = seq->getOwnerTrack();
    if (track != nullptr) {
        namespace te = tracktion::engine;
        bool passedSelf = false;
        daw::audio::DrumGridPlugin* drumGrid = nullptr;
        daw::audio::DrumGridPlugin* fallback = nullptr;
        for (auto* p : track->pluginList) {
            if (p == seq) {
                passedSelf = true;
                continue;
            }
            auto* found = dynamic_cast<daw::audio::DrumGridPlugin*>(p);
            if (found == nullptr) {
                if (auto* rackInstance = dynamic_cast<te::RackInstance*>(p)) {
                    if (rackInstance->type != nullptr) {
                        for (auto* inner : rackInstance->type->getPlugins()) {
                            found = dynamic_cast<daw::audio::DrumGridPlugin*>(inner);
                            if (found != nullptr)
                                break;
                        }
                    }
                }
            }
            if (found != nullptr) {
                if (passedSelf) {
                    drumGrid = found;
                    break;
                }
                if (fallback == nullptr)
                    fallback = found;
            }
        }
        if (drumGrid == nullptr && !passedSelf)
            drumGrid = fallback;

        if (drumGrid != nullptr && !drumGrid->getChains().empty()) {
            ctx += "LANE MAP:\n";
            for (const auto& chain : drumGrid->getChains()) {
                if (chain == nullptr)
                    continue;
                ctx += "note " + std::to_string(chain->lowNote) + " = " +
                       chain->name.toStdString() + "\n";
            }
        }
    }

    return ctx;
}

// Poly Step Sequencer -- generate patterns from a prompt.
class PolyStepSequencerSoundDesignAgent : public SoundDesignAgent {
  public:
    juce::String getUserCaveat() const override {
        return "note: generated pattern is a starting point - edit steps to taste.";
    }

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        juce::ignoreUnused(conversation);  // pattern design is single-shot
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        // Read plugin state (view mode + downstream drum lane map) before
        // generation. buildPolyStepSequencerContext is safe off the message
        // thread: getPlugin uses a ScopedLock and the chain/CachedValue reads
        // are stable value-copies while the edit is live.
        const auto deviceContext = buildPolyStepSequencerContext(path);

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
            return applyPolyStepSequencerPresetToPath(result.preset, path);

        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        const auto preset = result.preset;
        mm.callAsync([state, preset, path]() {
            state->status = applyPolyStepSequencerPresetToPath(preset, path);
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
};

// Build a device-context string for the mono Step Sequencer agent.
// Reads the current numSteps/rate/swing/gateLength from the live plugin.
// Thread-safe: getPlugin uses a ScopedLock; CachedValue reads are value-copies.
std::string buildStepSequencerContext(const ChainNodePath& path) {
    auto& tm = TrackManager::getInstance();
    auto* engine = tm.getAudioEngine();
    auto* bridge = engine ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return {};

    auto plugin = bridge->getPlugin(path);
    auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (seq == nullptr)
        return {};

    char buf[128];
    std::snprintf(
        buf, sizeof(buf),
        "DEVICE CONTEXT:\ncurrentSettings: numSteps=%d, rate=%d, swing=%.2f, gateLength=%.2f\n",
        seq->numSteps.get(), seq->rate.get(), static_cast<double>(seq->swing.get()),
        static_cast<double>(seq->gateLength.get()));
    return buf;
}

// Mono Step Sequencer -- generate 303-style patterns from a prompt.
class StepSequencerSoundDesignAgent : public SoundDesignAgent {
  public:
    juce::String getUserCaveat() const override {
        return "note: generated pattern is a starting point - edit steps to taste.";
    }

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        juce::ignoreUnused(conversation);  // pattern design is single-shot
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        const auto deviceContext = buildStepSequencerContext(path);

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
            return applyStepSequencerPresetToPath(result.preset, path);

        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        const auto preset = result.preset;
        mm.callAsync([state, preset, path]() {
            state->status = applyStepSequencerPresetToPath(preset, path);
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
};

}  // namespace

std::unique_ptr<SoundDesignAgent> createSoundDesignAgentFor(const juce::String& pluginId) {
    switch (internalPluginFromId(pluginId)) {
        case InternalPlugin::FourOsc:
            return std::make_unique<FourOscSoundDesignAgent>();
        case InternalPlugin::PolyStepSequencer:
            return std::make_unique<PolyStepSequencerSoundDesignAgent>();
        case InternalPlugin::StepSequencer:
            return std::make_unique<StepSequencerSoundDesignAgent>();
        default:
            return nullptr;
    }
}

bool isSoundDesignSupported(const juce::String& pluginId) {
    return createSoundDesignAgentFor(pluginId) != nullptr;
}

}  // namespace magda
