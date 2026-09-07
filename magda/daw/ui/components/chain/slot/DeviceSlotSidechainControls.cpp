#include "slot/DeviceSlotSidechainControls.hpp"

#include <iterator>
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

/// The trim steps the menu offers. A menu cannot drag a value, and a key's trim
/// is a matching decision rather than a ride, so a handful of steps is what it
/// is for; anything finer belongs to whoever automates it (#2329).
constexpr float kTrimSteps[] = {-12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f};

juce::String trimLabel(float decibels) {
    return (decibels > 0.0f ? "+" : "") + juce::String(decibels, 0) + " dB";
}

}  // namespace

void showDeviceSlotSidechainMenu(const magda::DeviceInfo& device,
                                 const magda::ChainNodePath& nodePath, juce::Button* targetButton,
                                 std::function<void()> onSidechainChanged) {
    juce::PopupMenu menu;

    magda::SidechainConfig currentSidechain;
    bool canAudio = device.sidechainPort.takesAudio();
    bool canMidi = supportsMidiSidechainSource(device);
    if (auto* currentDevice = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath)) {
        currentSidechain = currentDevice->sidechain;
        canAudio = currentDevice->sidechainPort.takesAudio();
        canMidi = supportsMidiSidechainSource(*currentDevice);
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

    // The rest of the source: where on the track the key is taken, what it is
    // trimmed by, and whether the slot monitors it. Only for an audio key --
    // a MIDI source has no fader to sit either side of and nothing to hear.
    if (currentSidechain.isActive() &&
        currentSidechain.type == magda::SidechainConfig::Type::Audio) {
        menu.addSeparator();
        menu.addSectionHeader("Key");

        const bool preFx = currentSidechain.tapPoint == magda::ModTapPoint::PreFx;
        menu.addItem(300, "Tap: Pre-FX", true, preFx);
        menu.addItem(301, "Tap: Post-Fader", true, !preFx);

        juce::PopupMenu trim;
        for (int step = 0; step < static_cast<int>(std::size(kTrimSteps)); ++step)
            trim.addItem(400 + step, trimLabel(kTrimSteps[step]), true,
                         juce::approximatelyEqual(currentSidechain.gainDb, kTrimSteps[step]));
        menu.addSubMenu("Trim", trim);

        menu.addItem(310, "Listen", true, currentSidechain.listen);
    }

    const auto deviceId = device.id;
    const bool listening = currentSidechain.listen;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetButton),
        [deviceId, listening, trackEntries,
         onSidechainChanged = std::move(onSidechainChanged)](int result) {
            if (result == 0)
                return;

            auto& trackManager = magda::TrackManager::getInstance();

            if (result == 1) {
                trackManager.clearSidechain(deviceId);
            } else if (result == 300 || result == 301) {
                trackManager.setSidechainTapPoint(deviceId, result == 300
                                                                ? magda::ModTapPoint::PreFx
                                                                : magda::ModTapPoint::PostFader);
            } else if (result == 310) {
                trackManager.setSidechainListen(deviceId, !listening);
            } else if (result >= 400 && result < 400 + static_cast<int>(std::size(kTrimSteps))) {
                trackManager.setSidechainGainDb(deviceId,
                                                kTrimSteps[static_cast<size_t>(result - 400)]);
            } else if (result >= 100 && result < 200) {
                const int index = result - 100;
                if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                    trackManager.setSidechainSource(deviceId,
                                                    (*trackEntries)[static_cast<size_t>(index)].id,
                                                    magda::SidechainConfig::Type::Audio);
                }
            } else if (result >= 200) {
                const int index = result - 200;
                if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                    trackManager.setSidechainSource(deviceId,
                                                    (*trackEntries)[static_cast<size_t>(index)].id,
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
        // Listening is worth saying: a slot putting out its key instead of its
        // own output looks like a broken device otherwise (#2329).
        const auto source = sidechain.type == magda::SidechainConfig::Type::MIDI
                                ? juce::String("Sidechain: MIDI")
                                : juce::String("Sidechain: audio");
        button->setTooltip(sidechain.listen ? source + " (listening)" : source);
    } else {
        button->setTooltip("Sidechain source");
    }
}

}  // namespace magda::daw::ui
