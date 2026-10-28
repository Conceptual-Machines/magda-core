// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace magda::daw::ui {

inline bool shouldShowIndexingStopButton(bool indexingActive, const juce::String& indexingStatus) {
    juce::ignoreUnused(indexingActive, indexingStatus);
    return false;
}

}  // namespace magda::daw::ui
