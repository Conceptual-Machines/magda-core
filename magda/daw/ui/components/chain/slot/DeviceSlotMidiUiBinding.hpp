#pragma once

namespace magda {
struct ChainNodePath;
}

namespace magda::daw::ui {

class DeviceCustomUIManager;

void bindDeviceSlotMidiCustomUIs(DeviceCustomUIManager& customUI,
                                 const magda::ChainNodePath& nodePath);

}  // namespace magda::daw::ui
