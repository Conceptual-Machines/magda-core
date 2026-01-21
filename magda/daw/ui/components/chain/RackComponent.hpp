#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "NodeComponent.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

class ChainRowComponent;
class ChainPanel;

/**
 * @brief A rack container that holds multiple parallel chains
 *
 * Inherits from NodeComponent for common header/footer layout.
 * Content area shows "Chains:" label and chain rows.
 */
class RackComponent : public NodeComponent {
  public:
    RackComponent(magda::TrackId trackId, const magda::RackInfo& rack);
    ~RackComponent() override;

    int getPreferredHeight() const;
    int getPreferredWidth() const;
    magda::RackId getRackId() const {
        return rackId_;
    }

    void updateFromRack(const magda::RackInfo& rack);
    void rebuildChainRows();
    void childLayoutChanged();
    void clearChainSelection();

    // Chain panel management (shown within rack when chain is selected)
    void showChainPanel(magda::ChainId chainId);
    void hideChainPanel();
    bool isChainPanelVisible() const;

    // Callback when a chain row is selected (still called, but panel shown internally)
    std::function<void(magda::TrackId, magda::RackId, magda::ChainId)> onChainSelected;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;

    // Hide footer - MOD/MACRO buttons are in header instead
    int getFooterHeight() const override {
        return 0;
    }

  private:
    void onChainRowSelected(ChainRowComponent& row);
    void onAddChainClicked();

    magda::TrackId trackId_;
    magda::RackId rackId_;

    // Header extra controls
    std::unique_ptr<magda::SvgButton> modButton_;    // Modulators toggle
    std::unique_ptr<magda::SvgButton> macroButton_;  // Macros toggle
    juce::TextButton addChainButton_;

    // Content area
    juce::Label chainsLabel_;  // "Chains:" label

    // Chain rows
    std::vector<std::unique_ptr<ChainRowComponent>> chainRows_;

    // Chain panel (shown within rack when chain is selected)
    std::unique_ptr<ChainPanel> chainPanel_;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;

    static constexpr int CHAINS_LABEL_HEIGHT = 18;
    static constexpr int MIN_CONTENT_HEIGHT = 30;
    static constexpr int CHAIN_PANEL_WIDTH = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackComponent)
};

}  // namespace magda::daw::ui
