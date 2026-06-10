#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>
#include <vector>

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
 * @brief A single chain row within a rack - simple strip layout
 *
 * Layout: [Name] [Gain] [Pan] [M] [S] [On] [X]
 *
 * Clicking the row will open a chain panel on the right side showing devices.
 * Note: Chain-level mods/macros removed - these are handled at rack level only.
 * Implements SelectionManagerListener for centralized exclusive selection.
 */
class ChainRowComponent : public juce::Component,
                          public magda::SelectionManagerListener,
                          public magda::TrackManagerListener {
  public:
    ChainRowComponent(RackComponent& owner, magda::TrackId trackId, magda::RackId rackId,
                      const magda::ChainInfo& chain);
    ~ChainRowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    int getPreferredHeight() const;
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

    RackComponent& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool selected_ = false;
    magda::ChainNodePath nodePath_;  // For centralized selection

    // Single row controls: Name | Gain | Pan | M | S | On | X
    ChainNameLabel nameLabel_;
    magda::DraggableValueLabel gainLabel_;
    magda::DraggableValueLabel panLabel_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    std::unique_ptr<magda::SvgButton> onButton_;  // Bypass/enable toggle (power icon)
    juce::TextButton deleteButton_;               // Delete chain

    // Per-chain base values captured at drag start, so a multi-chain gain/pan
    // drag shifts every selected chain by the same delta from its own value
    // (relative edit, matching the mixer) rather than slamming them all equal.
    std::vector<std::pair<magda::ChainNodePath, float>> dragBaseGains_;
    std::vector<std::pair<magda::ChainNodePath, float>> dragBasePans_;
    double dragStartGainDb_ = 0.0;
    double dragStartPan_ = 0.0;

    static constexpr int ROW_HEIGHT = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainRowComponent)
};

}  // namespace magda::daw::ui
