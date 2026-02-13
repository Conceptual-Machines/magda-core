#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A single chain row representing a non-empty drum pad.
 *
 * Layout: [Name(50px)] [Level] [Pan] [M] [S] [Power] [Delete]
 * Matches ChainRowComponent styling but wires callbacks to DrumGridUI
 * instead of SelectionManager/TrackManager.
 */
class PadChainRowComponent : public juce::Component {
  public:
    PadChainRowComponent(int padIndex);
    ~PadChainRowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

    void updateFromPad(const juce::String& name, float level, float pan, bool mute, bool solo);
    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }
    int getPadIndex() const {
        return padIndex_;
    }

    static constexpr int ROW_HEIGHT = 22;

    // Callbacks
    std::function<void(int padIndex)> onClicked;
    std::function<void(int padIndex, float)> onLevelChanged;
    std::function<void(int padIndex, float)> onPanChanged;
    std::function<void(int padIndex, bool)> onMuteChanged;
    std::function<void(int padIndex, bool)> onSoloChanged;
    std::function<void(int padIndex)> onDeleteClicked;

  private:
    int padIndex_;
    bool selected_ = false;

    juce::Label nameLabel_;
    TextSlider levelSlider_{TextSlider::Format::Decibels};
    TextSlider panSlider_{TextSlider::Format::Pan};
    juce::TextButton muteButton_{"M"};
    juce::TextButton soloButton_{"S"};
    std::unique_ptr<magda::SvgButton> onButton_;
    juce::TextButton deleteButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadChainRowComponent)
};

}  // namespace magda::daw::ui
