#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "MacroPanelComponent.hpp"
#include "NodeComponent.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

class ChainRowComponent;
class ChainPanel;

/**
 * @brief A rack container that holds multiple parallel chains
 *
 * Inherits from NodeComponent for common header/footer layout.
 * Content area shows "Chains:" label and chain rows.
 *
 * Works recursively - can be nested inside ChainPanel at any depth.
 * Uses ChainNodePath to track its location in the hierarchy.
 */
class RackComponent : public NodeComponent {
  public:
    // Constructor for top-level rack (in track)
    RackComponent(magda::TrackId trackId, const magda::RackInfo& rack);

    // Constructor for nested rack (in chain) - with full path context
    RackComponent(const magda::ChainNodePath& rackPath, const magda::RackInfo& rack);
    ~RackComponent() override;

    int getPreferredHeight() const;
    int getPreferredWidth() const override;
    int getMinimumWidth() const;        // Width without chain panel expansion
    void setAvailableWidth(int width);  // Set available width for chain panel
    magda::RackId getRackId() const {
        return rackId_;
    }

    void updateFromRack(const magda::RackInfo& rack);
    void rebuildChainRows();
    void childLayoutChanged();
    void clearChainSelection();
    void clearDeviceSelection();  // Clear device selection in chain panel

    // Chain panel management (shown within rack when chain is selected)
    void showChainPanel(magda::ChainId chainId);
    void hideChainPanel();
    bool isChainPanelVisible() const;
    magda::ChainId getSelectedChainId() const {
        return selectedChainId_;
    }

    // Callback when a chain row is selected (still called, but panel shown internally)
    std::function<void(magda::TrackId, magda::RackId, magda::ChainId)> onChainSelected;
    // Callback when a device in the chain panel is selected (or deselected with INVALID_DEVICE_ID)
    std::function<void(magda::DeviceId)> onDeviceSelected;

    void mouseDown(const juce::MouseEvent& e) override;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
    void resizedCollapsed(juce::Rectangle<int>& area) override;

    // Get the full path to this rack (for nested context)
    const magda::ChainNodePath& getRackPath() const {
        return rackPath_;
    }

    // Check if this is a nested rack (inside a chain)
    bool isNested() const {
        return rackPath_.steps.size() > 1;
    }

  private:
    void initializeCommon(const magda::RackInfo& rack);
    void onChainRowSelected(ChainRowComponent& row);
    void onAddChainClicked();

    magda::ChainNodePath rackPath_;  // Full path to this rack
    magda::TrackId trackId_;
    magda::RackId rackId_;

    // Header extra controls
    std::unique_ptr<magda::SvgButton> modButton_;            // Modulators toggle
    std::unique_ptr<magda::SvgButton> macroButton_;          // Macros toggle
    TextSlider volumeSlider_{TextSlider::Format::Decibels};  // Rack volume (dB)
    juce::TextButton addChainButton_;

    // Content area
    juce::Label chainsLabel_;  // "Chains:" label

    // Viewport for chain rows
    juce::Viewport chainViewport_;
    juce::Component chainRowsContainer_;

    // Chain rows
    std::vector<std::unique_ptr<ChainRowComponent>> chainRows_;

    // Chain panel (shown within rack when chain is selected)
    std::unique_ptr<ChainPanel> chainPanel_;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;
    int availableWidth_ = 0;  // 0 = no limit

    // Macro panel (shown in left side param panel when P button toggled)
    std::unique_ptr<MacroPanelComponent> macroPanel_;
    void updateMacroPanel();  // Update macro panel with current rack data

    // Override param panel for macro display (left side panel)
    int getParamPanelWidth() const override;
    void resizedParamPanel(juce::Rectangle<int> panelArea) override;
    void paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) override;

    static constexpr int CHAINS_LABEL_HEIGHT = 18;
    static constexpr int MIN_CONTENT_HEIGHT = 30;
    static constexpr int BASE_CHAINS_LIST_WIDTH = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackComponent)
};

}  // namespace magda::daw::ui
