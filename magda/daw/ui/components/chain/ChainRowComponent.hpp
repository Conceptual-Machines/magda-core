#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class RackComponent;

/**
 * @brief A single chain row within a rack - simple strip layout
 *
 * Layout: [Name] [Gain] [Pan] [M] [S] [On]
 *
 * Clicking the row will open a chain panel on the right side showing devices
 */
class ChainRowComponent : public juce::Component {
  public:
    ChainRowComponent(RackComponent& owner, magda::TrackId trackId, magda::RackId rackId,
                      const magda::ChainInfo& chain);
    ~ChainRowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

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

    // Callback when chain row is clicked
    std::function<void(ChainRowComponent&)> onSelected;

  private:
    void onMuteClicked();
    void onSoloClicked();
    void onBypassClicked();

    RackComponent& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool selected_ = false;

    // Single row controls: Name | Gain | Pan | M | S | On
    juce::Label nameLabel_;
    juce::Slider gainSlider_;
    juce::Slider panSlider_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::TextButton onButton_;  // Bypass/enable toggle

    static constexpr int ROW_HEIGHT = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainRowComponent)
};

}  // namespace magda::daw::ui
