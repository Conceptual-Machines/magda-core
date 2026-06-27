#include "slot/DeviceSlotMultiOutControls.hpp"

#include <utility>

#include "core/TrackManager.hpp"

namespace magda::daw::ui {

void showDeviceSlotMultiOutMenu(const magda::ChainNodePath& nodePath, magda::DeviceId deviceId,
                                juce::Button* targetButton, std::function<void()> onPairToggled) {
    constexpr int ACTIVATE_ALL_ID = 9001;
    constexpr int DEACTIVATE_ALL_ID = 9002;

    juce::PopupMenu menu;
    menu.addSectionHeader("Multi-Output Routing");

    auto& tm = magda::TrackManager::getInstance();
    const auto trackId = nodePath.trackId;

    auto* freshDevice = tm.getDevice(trackId, deviceId);
    if (freshDevice == nullptr || !freshDevice->multiOut.isMultiOut)
        return;

    bool anyInactive = false;
    bool anyActive = false;
    for (size_t i = 0; i < freshDevice->multiOut.outputPairs.size(); ++i) {
        const auto& pair = freshDevice->multiOut.outputPairs[i];
        if (pair.outputIndex == 0)
            continue;

        menu.addItem(static_cast<int>(i + 1), pair.name, true, pair.active);
        (pair.active ? anyActive : anyInactive) = true;
    }

    menu.addSeparator();
    menu.addItem(ACTIVATE_ALL_ID, "Activate All", anyInactive);
    menu.addItem(DEACTIVATE_ALL_ID, "Deactivate All", anyActive);

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetButton),
        [trackId, deviceId, onPairToggled = std::move(onPairToggled)](int result) {
            if (result == 0)
                return;

            auto& tm = magda::TrackManager::getInstance();
            auto* device = tm.getDevice(trackId, deviceId);
            if (device == nullptr || !device->multiOut.isMultiOut)
                return;

            if (result == ACTIVATE_ALL_ID) {
                for (size_t i = 0;; ++i) {
                    auto* dev = tm.getDevice(trackId, deviceId);
                    if (dev == nullptr || i >= dev->multiOut.outputPairs.size())
                        break;

                    const auto& pair = dev->multiOut.outputPairs[i];
                    if (pair.outputIndex == 0 || pair.active)
                        continue;
                    tm.activateMultiOutPair(trackId, deviceId, static_cast<int>(i));
                }
                return;
            }

            if (result == DEACTIVATE_ALL_ID) {
                tm.deactivateAllMultiOutPairs(trackId, deviceId);
                return;
            }

            const int pairIndex = result - 1;
            if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
                return;

            const auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];
            if (pair.active) {
                tm.deactivateMultiOutPair(trackId, deviceId, pairIndex);
            } else {
                tm.activateMultiOutPair(trackId, deviceId, pairIndex);
            }

            if (onPairToggled)
                onPairToggled();
        });
}

}  // namespace magda::daw::ui
