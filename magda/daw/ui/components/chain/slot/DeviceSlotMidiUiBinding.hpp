#pragma once

#include "core/TypeIds.hpp"

namespace magda {
struct ChainNodePath;
}

namespace magda::daw::ui {

class DeviceCustomUIManager;

void bindDeviceSlotMidiCustomUIs(DeviceCustomUIManager& customUI, magda::DeviceId deviceId,
                                 const magda::ChainNodePath& nodePath);

}  // namespace magda::daw::ui
