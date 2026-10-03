#pragma once

#include "devices/tone/ToneGenerator.hpp"
#include "plugins/SdkDeviceAdapter.hpp"

namespace magda::daw::audio {

/// The Test Tone in the app: devices::ToneGenerator, which also runs through the SDK hosts.
class ToneGeneratorPlugin : public SdkDeviceAdapter {
  public:
    ToneGeneratorPlugin() : SdkDeviceAdapter(std::make_unique<devices::ToneGenerator>()) {}

    static constexpr const char* xmlTypeName = devices::ToneGenerator::kDeviceType;

    static const char* getPluginName() {
        return devices::ToneGenerator::kName;
    }
};

}  // namespace magda::daw::audio
