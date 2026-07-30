#pragma once

#include "plugins/MagdaDevice.hpp"

namespace magda::pro_stub {

/**
 * Transparent proof device for the optional private-pack build path.
 */
class ProStubDevice final : public daw::audio::MagdaDevice {
  public:
    static constexpr const char* xmlTypeName = "magda-pro-stub";

    daw::audio::DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = "Pro Pack Stub",
            .shortName = "Pro Stub",
        };
    }

    void process(daw::audio::DeviceProcessContext&) override {}
};

}  // namespace magda::pro_stub
