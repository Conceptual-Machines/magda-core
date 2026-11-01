// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "../../themes/StyledText.hpp"
#include "BaseInspector.hpp"
#include "ModulationSection.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for device/plugin properties
 *
 * A header with the node's type and name, a strip of facts (category, latency,
 * sidechain), the device's or rack's own mods and macros, and, for catalogued devices,
 * an About section with the description.
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
    void deviceModifiersChanged(magda::TrackId trackId) override;
    void modulationNamesChanged(magda::TrackId trackId) override;
    void macroValueChanged(magda::TrackId trackId, magda::ChainScope scope, int ownerId,
                           int macroIndex, float value) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected chain node
     * @param path Chain node path (can be invalid for no selection)
     */
    void setSelectedChainNode(const magda::ChainNodePath& path);

    /// Open a mod or macro (or unfold its section) for a selection of that type.
    void focusModulation(magda::SelectionType type, int index) {
        modulation_.focus(type, index);
    }

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
    ModulationSection modulation_;

    // Painted chrome placed by resized().
    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> factsBounds_;
    juce::Rectangle<int> aboutLabelBounds_;
    int modulationRuleY_ = -1;

    void updateFromSelectedChainNode();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceInspector)
};

}  // namespace magda::daw::ui
