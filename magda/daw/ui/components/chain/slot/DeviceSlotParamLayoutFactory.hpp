#pragma once

#include <memory>

namespace magda::daw::ui {

class DeviceParamLayout;
struct DeviceSlotTraits;

std::unique_ptr<DeviceParamLayout> createDeviceSlotParamLayout(const DeviceSlotTraits& traits);

}  // namespace magda::daw::ui
