// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/FloatingHostWindow.hpp"

namespace magda {
class AudioEngine;
}

namespace magda::daw::ui {

/**
 * @brief The CPU readout expanded (#3357): load over the last 30 seconds on each render thread,
 *        and each track's share, opened by clicking the readout.
 *
 * Closing hides it; the owner reopens the same instance.
 */
class CpuLoadWindow : public FloatingHostWindow {
  public:
    explicit CpuLoadWindow(AudioEngine& engine);
    ~CpuLoadWindow() override;

    void closeButtonPressed() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CpuLoadWindow)
};

}  // namespace magda::daw::ui
