#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/TypeIds.hpp"

namespace magda {

struct ChainNodePath;
class PluginManager;

class SamplerFileLoader {
  public:
    explicit SamplerFileLoader(PluginManager& pluginManager);

    bool loadSample(DeviceId deviceId, const juce::File& file);
    bool loadSample(const ChainNodePath& devicePath, const juce::File& file);

  private:
    PluginManager& pluginManager_;
};

}  // namespace magda
