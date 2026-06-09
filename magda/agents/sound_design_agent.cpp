#include "sound_design_agent.hpp"

#include <juce_events/juce_events.h>

#include "four_osc_agent.hpp"
#include "four_osc_apply.hpp"
#include "internal_plugins.hpp"

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

}  // namespace

std::unique_ptr<SoundDesignAgent> createSoundDesignAgentFor(const juce::String& pluginId) {
    switch (internalPluginFromId(pluginId)) {
        case InternalPlugin::FourOsc:
            return std::make_unique<FourOscSoundDesignAgent>();
        default:
            return nullptr;
    }
}

bool isSoundDesignSupported(const juce::String& pluginId) {
    return createSoundDesignAgentFor(pluginId) != nullptr;
}

}  // namespace magda
