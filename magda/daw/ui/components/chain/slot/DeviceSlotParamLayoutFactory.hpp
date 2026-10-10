#pragma once

#include <memory>
#include <span>

namespace magda::daw::ui {

class DeviceParamLayout;
struct DeviceSlotTraits;

/// @p excludedSlots leave a compiled device's grid for controls it shows elsewhere.
std::unique_ptr<DeviceParamLayout> createDeviceSlotParamLayout(
    const DeviceSlotTraits& traits, std::span<const int> excludedSlots = {});

}  // namespace magda::daw::ui
