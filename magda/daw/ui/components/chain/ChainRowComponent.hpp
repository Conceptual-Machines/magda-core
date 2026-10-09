#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "ZoneBar.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

class RackComponent;

// Chain name label: double-click renames (editOnDoubleClick), but a plain
// single click must still select the owning chain. An editable juce::Label
// intercepts its own mouse clicks, so without this the row's click handler
// would never see clicks that land on the name.
class ChainNameLabel : public juce::Label {
  public:
    using juce::Label::Label;
    // Passes the click's modifiers so Cmd/Shift+click on the name drives the
    // same multi-selection as clicking the row body.
    std::function<void(const juce::MouseEvent&)> onSelect;

  protected:
    void mouseUp(const juce::MouseEvent& e) override {
        juce::Label::mouseUp(e);
        if (!isBeingEdited() && onSelect)
            onSelect(e);
    }
};

/**
 * @brief One chain in a rack's chain list: [dot Name] [Gain] [Pan] [M][S][Power][x].
 *
 * Clicking the row selects the chain, which shows its devices in the rack's viewport. The
 * zone views swap the mix controls for the chain's key, velocity or selector zone (#1808).
 */
class ChainRowComponent : public juce::Component,
                          public magda::SelectionManagerListener,
                          public magda::TrackManagerListener {
  public:
    enum class View { Mix, Key, Velocity, Fade };

    ChainRowComponent(RackComponent& owner, magda::TrackId trackId, magda::RackId rackId,
                      const magda::ChainInfo& chain);
    ~ChainRowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    static int getPreferredHeight();

    /** @brief Column bounds inside a row; the rack's column header aligns to these. */
    struct Columns {
        juce::Rectangle<int> name, gain, pan, buttons;
    };
    static Columns columnsFor(juce::Rectangle<int> row);

    /** The chain's position in its rack, which picks its colour dot. */
    void setColourIndex(int index);
    /// What the row shows while the chain is unnamed: "Chain", or a multiband rack's band name.
    void setPlaceholderName(const juce::String& name);
    magda::ChainId getChainId() const {
        return chainId_;
    }
    magda::TrackId getTrackId() const {
        return trackId_;
    }
    magda::RackId getRackId() const {
        return rackId_;
    }

    void updateFromChain(const magda::ChainInfo& chain);

    /// Which controls the row shows; Fade is the chain's range on the rack's chain selector.
    void setView(View view);

    /// Where macros and modifiers hold the rack's selector, drawn in place of its stored value.
    void setModulatedSelector(std::optional<float> value);

    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }

    // Set the full node path for nested chains (includes parent rack/chain context)
    // Also checks current selection state to handle cases where selection happened before row
    // existed
    void setNodePath(const magda::ChainNodePath& path);

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;

    // TrackManagerListener: keep this row's controls live when a chain value
    // changes from elsewhere (notably a multi-chain edit driven by a sibling).
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(int trackId) override;

    // Callback for double-click to toggle expand/collapse
    std::function<void(magda::ChainId)> onDoubleClick;

  private:
    void onMuteClicked();
    void onSoloClicked();
    void onBypassClicked();
    void onDeleteClicked();

    // Apply a click's modifiers to selection: plain = replace, Cmd = toggle,
    // Shift = range from the anchor chain. Shared by the row body and the name
    // label so both honour the unified selection modifiers.
    void applySelectionForClick(const juce::ModifierKeys& mods);
    void rangeSelectFromAnchor();

    // The chains an edit on this row should touch: every selected chain when
    // this row is part of a multi-selection, otherwise just this one.
    std::vector<magda::ChainNodePath> editTargets() const;

    // Re-read this row's chain from the model and refresh its controls.
    void refreshFromModel();

    void styleControls();
    void commitZones(const magda::ChainZones& zones);

    RackComponent& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool selected_ = false;
    bool hovered_ = false;
    juce::String placeholderName_ = "Chain";
    int colourIndex_ = 0;
    magda::ChainNodePath nodePath_;  // For centralized selection

    // Gain and pan keep the label's drag and edit gestures; the row paints their sliders.
    ChainNameLabel nameLabel_;
    magda::DraggableValueLabel gainLabel_;
    magda::DraggableValueLabel panLabel_;
    magda::SvgButton muteButton_{"mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize};
    magda::SvgButton soloButton_{"solo", BinaryData::solo_svg, BinaryData::solo_svgSize};
    std::unique_ptr<magda::SvgButton> onButton_;      // Bypass/enable toggle (power icon)
    std::unique_ptr<magda::SvgButton> deleteButton_;  // Delete chain
    ZoneBar zoneBar_;
    juce::TextButton roundRobinButton_{"RR"};
    magda::ChainZones zones_;
    View view_ = View::Mix;
    std::optional<float> modulatedSelector_;

    // Per-chain base values captured at drag start, so a multi-chain gain/pan
    // drag shifts every selected chain by the same delta from its own value
    // (relative edit, matching the mixer) rather than slamming them all equal.
    std::vector<std::pair<magda::ChainNodePath, float>> dragBaseGains_;
    std::vector<std::pair<magda::ChainNodePath, float>> dragBasePans_;
    double dragStartGainDb_ = 0.0;
    double dragStartPan_ = 0.0;

    void lookAndFeelChanged() override;

    static constexpr int ROW_HEIGHT = 38;
    static constexpr int COLUMN_GAP = 8;
    static constexpr int BUTTON_WIDTH = 28;
    static constexpr float BUTTON_HEIGHT = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainRowComponent)
};

}  // namespace magda::daw::ui
