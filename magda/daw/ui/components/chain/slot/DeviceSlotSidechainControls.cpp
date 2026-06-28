#include "slot/DeviceSlotSidechainControls.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "core/PluginCapabilities.hpp"
#include "core/TrackManager.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

struct TrackEntry {
    magda::TrackId id;
    juce::String name;
};

}  // namespace

void showDeviceSlotSidechainMenu(const magda::DeviceInfo& device,
                                 const magda::ChainNodePath& nodePath, juce::Button* targetButton,
                                 std::function<void()> onSidechainChanged) {
    juce::PopupMenu menu;

    magda::SidechainConfig currentSidechain;
    bool canAudio = device.canSidechain;
    bool canMidi = supportsMidiInputRouting(device);
    if (auto* currentDevice = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath)) {
        currentSidechain = currentDevice->sidechain;
        canAudio = currentDevice->canSidechain;
        canMidi = supportsMidiInputRouting(*currentDevice);
    }

    const bool isNone = !currentSidechain.isActive();
    menu.addItem(1, "None", true, isNone);
    menu.addSeparator();

    auto trackEntries = std::make_shared<std::vector<TrackEntry>>();
    const auto& tracks = magda::TrackManager::getInstance().getTracks();
    for (const auto& track : tracks) {
        if (track.id == nodePath.trackId)
            continue;
        trackEntries->push_back({track.id, track.name});
    }

    if (canAudio) {
        menu.addSectionHeader("Audio Sidechain");
        int itemId = 100;
        for (const auto& entry : *trackEntries) {
            const bool isSelected = currentSidechain.isActive() &&
                                    currentSidechain.type == magda::SidechainConfig::Type::Audio &&
                                    currentSidechain.sourceTrackId == entry.id;
            menu.addItem(itemId, entry.name, true, isSelected);
            ++itemId;
        }
    }

    if (canMidi) {
        menu.addSectionHeader("MIDI Source");
        int itemId = 200;
        for (const auto& entry : *trackEntries) {
            const bool isSelected = currentSidechain.isActive() &&
                                    currentSidechain.type == magda::SidechainConfig::Type::MIDI &&
                                    currentSidechain.sourceTrackId == entry.id;
            menu.addItem(itemId, entry.name, true, isSelected);
            ++itemId;
        }
    }

    const auto deviceId = device.id;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetButton),
        [deviceId, trackEntries, onSidechainChanged = std::move(onSidechainChanged)](int result) {
            if (result == 0)
                return;

            if (result == 1) {
                magda::TrackManager::getInstance().clearSidechain(deviceId);
            } else if (result >= 100 && result < 200) {
                const int index = result - 100;
                if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                    magda::TrackManager::getInstance().setSidechainSource(
                        deviceId, (*trackEntries)[static_cast<size_t>(index)].id,
                        magda::SidechainConfig::Type::Audio);
                }
            } else if (result >= 200) {
                const int index = result - 200;
                if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                    magda::TrackManager::getInstance().setSidechainSource(
                        deviceId, (*trackEntries)[static_cast<size_t>(index)].id,
                        magda::SidechainConfig::Type::MIDI);
                }
            }

            if (onSidechainChanged)
                onSidechainChanged();
        });
}

void updateDeviceSlotSidechainButtonState(magda::SvgButton* button,
                                          const magda::SidechainConfig& sidechain) {
    if (button == nullptr)
        return;

    // The icon highlights (orange, via its active state) when a sidechain is
    // routed; the tooltip distinguishes the MIDI vs audio source.
    const bool active = sidechain.isActive();
    button->setActive(active);
    if (active) {
        button->setTooltip(sidechain.type == magda::SidechainConfig::Type::MIDI
                               ? "Sidechain: MIDI"
                               : "Sidechain: audio");
    } else {
        button->setTooltip("Sidechain source");
    }
}

}  // namespace magda::daw::ui
