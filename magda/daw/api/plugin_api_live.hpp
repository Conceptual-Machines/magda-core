#pragma once

#include "plugin_api.hpp"

namespace magda {

class PluginApiLive final : public PluginApi {
  public:
    std::vector<DeviceInfo> getExternalPlugins() const override;
    std::vector<DeviceInfo> getAllExternalPlugins() const override;
    std::optional<SequencerRuntimeContext> getStepSequencerContext(
        const ChainNodePath& path) const override;
    std::optional<SequencerRuntimeContext> getPolySequencerContext(
        const ChainNodePath& path) const override;
    juce::String applyStepSequencerPattern(const ChainNodePath& path,
                                           const StepSequencerPattern& pattern) override;
    juce::String applyPolySequencerPattern(const ChainNodePath& path,
                                           const PolySequencerPattern& pattern) override;
    juce::String applyFourOscUpdate(const ChainNodePath& path,
                                    const FourOscUpdate& update) override;
    juce::String applyFaustSource(const ChainNodePath& path, const juce::String& displayName,
                                  const juce::String& source, bool verified) override;
};

}  // namespace magda
