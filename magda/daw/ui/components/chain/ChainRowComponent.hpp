#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

class RackComponent;

/**
 * @brief A single chain row within a rack - simple strip layout
 *
 * Layout: [Name] [Gain] [Pan] [M] [S] [On] [X]
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
    void onDeleteClicked();

    RackComponent& owner_;
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool selected_ = false;

    // Single row controls: Name | Gain | Pan | MOD | MACRO | M | S | On | X
    juce::Label nameLabel_;
    TextSlider gainSlider_{TextSlider::Format::Decibels};
    TextSlider panSlider_{TextSlider::Format::Pan};
    std::unique_ptr<magda::SvgButton> modButton_;    // Modulators toggle
    std::unique_ptr<magda::SvgButton> macroButton_;  // Macros toggle
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::TextButton onButton_;      // Bypass/enable toggle
    juce::TextButton deleteButton_;  // Delete chain

    static constexpr int ROW_HEIGHT = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainRowComponent)
};

}  // namespace magda::daw::ui
