// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

/**
 * @brief Dialog for managing track visibility per view mode
 */
class TrackManagerDialog : public juce::DialogWindow, public TrackManagerListener {
  public:
    TrackManagerDialog();
    ~TrackManagerDialog() override;

    void closeButtonPressed() override;

    // TrackManagerListener
    void tracksChanged() override;

    // Show the dialog (creates and shows modal)
    static void show();

  private:
    class ContentComponent;
    std::unique_ptr<ContentComponent> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackManagerDialog)
};

}  // namespace magda
