#pragma once

#include "plugins/MagdaDevice.hpp"

namespace magda::extensions::stub {

/** @brief Transparent proof device for the extension build path. */
class StubDevice final : public daw::audio::MagdaDevice {
  public:
    static constexpr const char* xmlTypeName = "magda-extension-stub";

    daw::audio::DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = "Extension Stub",
            .shortName = "Ext Stub",
        };
    }

    void process(daw::audio::DeviceProcessContext&) override {}
};

}  // namespace magda::extensions::stub
