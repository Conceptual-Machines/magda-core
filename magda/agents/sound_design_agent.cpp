#include "sound_design_agent.hpp"

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
    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path) override {
        agent_.resetCancel();
        if (shouldStop_.load())
            return "(cancelled)";

        auto result = agent_.generate(prompt.toStdString());
        if (shouldStop_.load())
            return "(cancelled)";

        if (result.hasError)
            return juce::String("(error: ") + juce::String(result.error) + ")";

        // Override the model's category pick if the caller asked us to.
        if (categoryOverride_.isNotEmpty())
            result.preset.category = categoryOverride_.toStdString();

        return applyFourOscPresetToPath(result.preset, path);
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
