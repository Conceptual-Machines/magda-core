#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom UI for the native convolution device (Convolution)
 *
 * The loaded IR's waveform on top, which takes a click or a drop to load one; below it the
 * filter's response over Low Cut, High Cut and Q, and the Gain and Mix tiles.
 */
class ImpulseResponseUI : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget {
  public:
    ImpulseResponseUI();
    ~ImpulseResponseUI() override = default;

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    void setIRName(const juce::String& name);
    /// The encoded IR the device holds (empty for none); decoded only when it changes.
    void setImpulseResponse(const juce::MemoryBlock& encoded);

    std::function<void(int paramIndex, float value)> onParameterChanged;
    std::function<void()> onLoadIRRequested;
    std::function<void(const juce::File&)> onFileDropped;

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    // FileDragAndDropTarget — IRs dropped from the OS file manager.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    // DragAndDropTarget — IRs dragged from MAGDA's own browser arrive as an
    // internal {type:"files"} payload; these forward it to the handlers above.
    // See InternalFileDrag.hpp.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

  private:
    struct SliderWithLabel {
        juce::Label label;
        LinkableTextSlider slider;
        juce::Rectangle<int> tile;  ///< The painted cell behind the caption and value.

        explicit SliderWithLabel(TextSlider::Format fmt = TextSlider::Format::Decimal)
            : slider(fmt) {}
    };

    std::unique_ptr<magda::SvgButton> loadButton_;
    juce::String irName_;
    juce::MemoryBlock encodedIR_;
    std::vector<float> peaks_;  ///< Per-column peak of the IR, normalised to its loudest.
    double irSeconds_ = 0.0;
    int irChannels_ = 0;
    bool dragOver_ = false;

    juce::Rectangle<int> irPanel_, filterPanel_, outputPanel_;
    juce::Rectangle<int> irTitle_, waveArea_, filterTitle_, curveArea_, outputTitle_;

    SliderWithLabel gain_{TextSlider::Format::Decibels};
    SliderWithLabel lowCut_;
    SliderWithLabel highCut_;
    SliderWithLabel mix_;
    SliderWithLabel filterQ_;

    void setupSlider(SliderWithLabel& s, const juce::String& labelText);
    void paintWaveform(juce::Graphics& g) const;
    void paintResponse(juce::Graphics& g) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImpulseResponseUI)
};

}  // namespace magda::daw::ui
