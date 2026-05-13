#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A chain row showing key range fields (low note / high note / root note).
 *
 * Layout: [Name(50px)] [Low: TextSlider] [High: TextSlider] [Root: TextSlider]
 * Same height as PadChainRowComponent for visual consistency.
 */
class PadChainRangeRowComponent : public juce::Component {
  public:
    explicit PadChainRangeRowComponent(int padIndex);
    ~PadChainRangeRowComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

    void updateFromChain(const juce::String& name, int lowNote, int highNote, int rootNote);
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
    std::function<void(int padIndex, int lowNote, int highNote, int rootNote)> onRangeChanged;

  private:
    int padIndex_;
    bool selected_ = false;

    juce::Label nameLabel_;
    TextSlider lowNoteSlider_{TextSlider::Format::Decimal};
    TextSlider highNoteSlider_{TextSlider::Format::Decimal};
    TextSlider rootNoteSlider_{TextSlider::Format::Decimal};

    static juce::String midiNoteToName(int note);
    static int noteNameToMidi(const juce::String& name);

    void fireRangeChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadChainRangeRowComponent)
};

}  // namespace magda::daw::ui
