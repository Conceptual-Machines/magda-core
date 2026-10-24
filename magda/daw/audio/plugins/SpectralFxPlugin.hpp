#pragma once

#include "devices/spectral/SpectralFx.hpp"
#include "plugins/SdkDeviceAdapter.hpp"

namespace magda::daw::audio {

/// Spectral FX in the app: devices::spectral::SpectralFx, which also runs through the SDK hosts.
class SpectralFxPlugin : public SdkDeviceAdapter {
  public:
    SpectralFxPlugin() : SdkDeviceAdapter(std::make_unique<devices::spectral::SpectralFx>()) {}

    static constexpr const char* xmlTypeName = devices::spectral::SpectralFx::xmlTypeName;

    static const char* getPluginName() {
        return devices::spectral::SpectralFx::kName;
    }
};

}  // namespace magda::daw::audio
