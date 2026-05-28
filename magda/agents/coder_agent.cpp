#include "coder_agent.hpp"

#include <juce_events/juce_events.h>

#include <memory>

#include "../daw/audio/AudioBridge.hpp"
#include "../daw/audio/plugins/FaustPlugin.hpp"
#include "../daw/core/TrackManager.hpp"
#include "../daw/engine/AudioEngine.hpp"
#include "faust_agent.hpp"
#include "internal_plugins.hpp"
#include "sound_design_agent.hpp"

namespace magda {

namespace {

// Faust-specific implementation. Streams a .dsp source from the LLM and
// applies it via FaustPlugin::loadDspSource on the message thread.
class FaustCoderAgent : public CoderAgent {
  public:
    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  TokenCallback onToken = {}) override {
        agent_.resetCancel();
        if (shouldStop_.load())
            return "cancelled";

        FaustAgent::Result result;
        if (onToken) {
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

        auto& mm = *juce::MessageManager::getInstance();
        auto applyOnce = [name = result.name, source = result.source, path]() -> juce::String {
            auto& tm = TrackManager::getInstance();
            auto* device = tm.getDeviceInChainByPath(path);
            if (device == nullptr ||
                internalPluginFromId(device->pluginId) != InternalPlugin::Faust)
                return "(target device is not a Faust plugin)";
            auto* engine = tm.getAudioEngine();
            auto* bridge = engine ? engine->getAudioBridge() : nullptr;
            auto plugin = bridge ? bridge->getPlugin(path) : nullptr;
            auto* faust = dynamic_cast<daw::audio::FaustPlugin*>(plugin.get());
            if (faust == nullptr)
                return "(could not resolve live Faust plugin)";
            juce::String err;
            const auto displayName = name.empty() ? juce::String("AI DSP") : juce::String(name);
            if (!faust->loadDspSource(displayName, juce::String(source), err))
                return juce::String("compile error: ") + err;
            return juce::String("applied \"") + displayName + "\"";
        };

        if (mm.isThisTheMessageThread())
            return applyOnce();

        struct ApplyState {
            juce::String status;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<ApplyState>();
        mm.callAsync([state, applyOnce]() {
            state->status = applyOnce();
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
    FaustAgent agent_;
};

}  // namespace

std::unique_ptr<CoderAgent> createCoderAgentFor(const juce::String& pluginId) {
    switch (internalPluginFromId(pluginId)) {
        case InternalPlugin::Faust:
            return std::make_unique<FaustCoderAgent>();
        default:
            return nullptr;
    }
}

bool isCoderSupported(const juce::String& pluginId) {
    return createCoderAgentFor(pluginId) != nullptr;
}

std::unique_ptr<DeviceAIAgent> createDeviceAIAgentFor(const juce::String& pluginId) {
    if (auto sd = createSoundDesignAgentFor(pluginId))
        return sd;
    if (auto cd = createCoderAgentFor(pluginId))
        return cd;
    return nullptr;
}

bool isDeviceAISupported(const juce::String& pluginId) {
    return isSoundDesignSupported(pluginId) || isCoderSupported(pluginId);
}

}  // namespace magda
