#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

class RackComponent;

/**
 * @brief A single chain row within a rack - simple strip layout
 *
 * Layout: [Name] [Gain] [Pan] [M] [S] [On] [X]
 *
 * Clicking the row will open a chain panel on the right side showing devices.
 * Note: Chain-level mods/macros removed - these are handled at rack level only.
 * Implements SelectionManagerListener for centralized exclusive selection.
 */
class ChainRowComponent : public juce::Component, public magda::SelectionManagerListener {
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

    // Callback for double-click to toggle expand/collapse
    std::function<void(magda::ChainId)> onDoubleClick;

  private:
    void onMuteClicked();
    void onSoloClicked();
    void onBypassClicked();
    void onDeleteClicked();

    RackComponent& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool selected_ = false;
    magda::ChainNodePath nodePath_;  // For centralized selection

    // Single row controls: Name | Gain | Pan | M | S | On | X
    juce::Label nameLabel_;
    magda::DraggableValueLabel gainLabel_;
    magda::DraggableValueLabel panLabel_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    std::unique_ptr<magda::SvgButton> onButton_;  // Bypass/enable toggle (power icon)
    juce::TextButton deleteButton_;               // Delete chain

    static constexpr int ROW_HEIGHT = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainRowComponent)
};

}  // namespace magda::daw::ui
