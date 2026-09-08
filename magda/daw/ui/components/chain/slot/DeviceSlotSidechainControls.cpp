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

/// One thing the menu can do, looked up by item id rather than decoded out of
/// an id range. Two unbounded lists of tracks and a handful of fixed actions
/// cannot share one id space: with enough tracks the source ids run into the
/// action ids, and picking a track changes the tap point instead.
struct MenuAction {
    enum class Kind { Clear, SetSource, SetTapPoint, SetTrim, ToggleListen };

    Kind kind = Kind::Clear;
    magda::TrackId trackId = magda::INVALID_TRACK_ID;
    magda::SidechainConfig::Type type = magda::SidechainConfig::Type::None;
    magda::ModTapPoint tapPoint = magda::ModTapPoint::PostFader;
    float gainDb = 0.0f;
    bool listen = false;
};

using MenuActions = std::vector<MenuAction>;

/// Adds @p action and returns the item id that runs it. Ids are 1-based: a
/// PopupMenu reports 0 for "nothing was chosen".
int addAction(MenuActions& actions, MenuAction action) {
    actions.push_back(action);
    return static_cast<int>(actions.size());
}

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

    auto actions = std::make_shared<MenuActions>();

    const bool isNone = !currentSidechain.isActive();
    menu.addItem(addAction(*actions, {.kind = MenuAction::Kind::Clear}), "None", true, isNone);
    menu.addSeparator();

    std::vector<TrackEntry> trackEntries;
    for (const auto& track : magda::TrackManager::getInstance().getTracks()) {
        if (track.id == nodePath.trackId)
            continue;
        trackEntries.push_back({track.id, track.name});
    }

    const auto addSources = [&](magda::SidechainConfig::Type type, const char* header) {
        menu.addSectionHeader(header);
        for (const auto& entry : trackEntries) {
            const bool isSelected = currentSidechain.isActive() && currentSidechain.type == type &&
                                    currentSidechain.sourceTrackId == entry.id;
            const auto item = addAction(
                *actions, {.kind = MenuAction::Kind::SetSource, .trackId = entry.id, .type = type});
            menu.addItem(item, entry.name, true, isSelected);
        }
    };

    if (canAudio)
        addSources(magda::SidechainConfig::Type::Audio, "Audio Sidechain");
    if (canMidi)
        addSources(magda::SidechainConfig::Type::MIDI, "MIDI Source");

    // The rest of the source: where on the track the key is taken, what it is
    // trimmed by, and whether the slot monitors it. Only for an audio key -- a
    // MIDI source has no fader to sit either side of and nothing to hear.
    if (currentSidechain.isActive() &&
        currentSidechain.type == magda::SidechainConfig::Type::Audio) {
        menu.addSeparator();
        menu.addSectionHeader("Key");

        const auto addTapPoint = [&](magda::ModTapPoint point, const char* label) {
            const auto item =
                addAction(*actions, {.kind = MenuAction::Kind::SetTapPoint, .tapPoint = point});
            menu.addItem(item, label, true, currentSidechain.tapPoint == point);
        };
        addTapPoint(magda::ModTapPoint::PreFx, "Tap: Pre-FX");
        addTapPoint(magda::ModTapPoint::PostFader, "Tap: Post-Fader");

        juce::PopupMenu trim;
        for (const float step : kTrimSteps) {
            const auto item =
                addAction(*actions, {.kind = MenuAction::Kind::SetTrim, .gainDb = step});
            trim.addItem(item, trimLabel(step), true,
                         juce::approximatelyEqual(currentSidechain.gainDb, step));
        }
        menu.addSubMenu("Trim", trim);

        const auto listenItem = addAction(
            *actions, {.kind = MenuAction::Kind::ToggleListen, .listen = !currentSidechain.listen});
        menu.addItem(listenItem, "Listen", true, currentSidechain.listen);
    }

    const auto deviceId = device.id;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetButton),
        [deviceId, actions, onSidechainChanged = std::move(onSidechainChanged)](int result) {
            if (result <= 0 || result > static_cast<int>(actions->size()))
                return;

            auto& trackManager = magda::TrackManager::getInstance();
            const auto& action = (*actions)[static_cast<size_t>(result - 1)];

            switch (action.kind) {
                case MenuAction::Kind::Clear:
                    trackManager.clearSidechain(deviceId);
                    break;
                case MenuAction::Kind::SetSource:
                    trackManager.setSidechainSource(deviceId, action.trackId, action.type);
                    break;
                case MenuAction::Kind::SetTapPoint:
                    trackManager.setSidechainTapPoint(deviceId, action.tapPoint);
                    break;
                case MenuAction::Kind::SetTrim:
                    trackManager.setSidechainGainDb(deviceId, action.gainDb);
                    break;
                case MenuAction::Kind::ToggleListen:
                    trackManager.setSidechainListen(deviceId, action.listen);
                    break;
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
