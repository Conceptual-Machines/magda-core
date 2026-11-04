// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TrackPropertyCommands.hpp"

#include "StringTable.hpp"
#include "TrackAvailability.hpp"

namespace magda {

juce::String switchTrackEnabled(TrackId trackId, bool enabled) {
    auto& tracks = TrackManager::getInstance();
    if (enabled)
        if (const auto missing = tracks.getUnavailableDeviceNames(trackId); !missing.isEmpty())
            return tr("tracks.enable_refused").replace("{0}", unavailableDevicesText(missing));

    UndoManager::getInstance().executeCommand(
        std::make_unique<SetTrackEnabledCommand>(trackId, enabled));
    return {};
}

}  // namespace magda
