// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "../../themes/StyledText.hpp"
#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for device/plugin properties
 *
 * A header with the node's type and name, a strip of facts (category, latency,
 * sidechain) and, for catalogued devices, an About section with the description.
 */
class DeviceInspector : public BaseInspector, public magda::TrackManagerListener {
  public:
    DeviceInspector();
    ~DeviceInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    // TrackManagerListener: live-refresh the displayed name/props when the
    // selected chain node's owning track changes (e.g. a chain rename).
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(int trackId) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected chain node
     * @param path Chain node path (can be invalid for no selection)
     */
    void setSelectedChainNode(const magda::ChainNodePath& path);

  private:
    struct Fact {
        juce::String label;
        juce::String value;
        bool mono = false;
    };

    magda::ChainNodePath selectedChainNode_;

    juce::String kicker_;
    juce::String name_;
    std::vector<Fact> facts_;
    magda::StyledTextDisplay descriptionValue_;

    // Painted chrome placed by resized().
    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> factsBounds_;
    juce::Rectangle<int> aboutLabelBounds_;

    void updateFromSelectedChainNode();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceInspector)
};

}  // namespace magda::daw::ui
