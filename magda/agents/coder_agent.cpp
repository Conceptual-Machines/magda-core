#include "coder_agent.hpp"

#include <juce_events/juce_events.h>

#include <memory>

#include "../daw/api/magda_api.hpp"
#include "../daw/api/plugin_api.hpp"
#include "faust_agent.hpp"
#include "internal_plugins.hpp"
#include "mcp/MCPServerManager.hpp"
#include "sound_design_agent.hpp"

namespace magda {

namespace {

// Faust-specific implementation. Streams a .dsp source from the LLM and
// applies it via FaustPlugin::loadDspSource on the message thread.
class FaustCoderAgent : public CoderAgent {
  public:
    FaustCoderAgent(FaustAgent::Target target, PluginApi* plugins)
        : agent_(target), plugins_(plugins) {}

    juce::String getUserCaveat() const override {
        return "note: generated code is a starting point - review and refine in the editor.";
    }

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override {
        if (plugins_ == nullptr)
            return "(MagdaApi plugin operations are unavailable)";

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
            result = agent_.generateStreaming(prompt.toStdString(), conversation, fwd);
        } else {
            result = agent_.generate(prompt.toStdString(), conversation);
        }
        if (shouldStop_.load())
            return "cancelled";
        if (result.hasError)
            return juce::String("error: ") + juce::String(result.error);

        auto& mm = *juce::MessageManager::getInstance();
        auto applyOnce = [plugins = plugins_, name = result.name, source = result.source, path,
                          verified = result.mcpVerified]() -> juce::String {
            const auto displayName = name.empty() ? juce::String("AI DSP") : juce::String(name);
            return plugins->applyFaustSource(path, displayName, juce::String(source), verified);
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
    PluginApi* plugins_ = nullptr;
};

}  // namespace

std::unique_ptr<CoderAgent> createCoderAgentFor(const juce::String& pluginId, MagdaApi* api) {
    switch (getInternalPluginCapabilities(pluginId).coderAgent) {
        case CoderAgentKind::Faust:
            return std::make_unique<FaustCoderAgent>(pluginId.equalsIgnoreCase("faustinstrument")
                                                         ? FaustAgent::Target::Instrument
                                                         : FaustAgent::Target::Effect,
                                                     api != nullptr ? &api->plugins() : nullptr);
        default:
            return nullptr;
    }
}

bool isCoderSupported(const juce::String& pluginId) {
    return getInternalPluginCapabilities(pluginId).supportsCoder();
}

std::unique_ptr<DeviceAIAgent> createDeviceAIAgentFor(const juce::String& pluginId, MagdaApi* api) {
    if (auto sd = createSoundDesignAgentFor(pluginId, api))
        return sd;
    if (auto cd = createCoderAgentFor(pluginId, api))
        return cd;
    return nullptr;
}

std::unique_ptr<DeviceAIAgent> createDeviceAIAgentFor(const DeviceInfo& device, MagdaApi* api) {
    if (auto sd = createSoundDesignAgentFor(device, api))
        return sd;
    if (auto cd = createCoderAgentFor(device.pluginId, api))
        return cd;
    return nullptr;
}

bool isDeviceAISupported(const juce::String& pluginId) {
    return getInternalPluginCapabilities(pluginId).supportsDeviceAI();
}

bool isDeviceAISupported(const DeviceInfo& device) {
    return isSoundDesignSupported(device) || isCoderSupported(device.pluginId);
}

}  // namespace magda
