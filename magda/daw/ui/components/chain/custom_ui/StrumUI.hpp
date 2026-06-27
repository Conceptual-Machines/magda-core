#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "audio/plugins/MidiStrumPlugin.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Inline UI for the Strum MIDI effect.
 *
 * Sibling of ArpeggiatorUI. Exposes the strum controls (trigger / order / shape
 * preset / cycles / length / sync interval) plus a read-only onset-distribution
 * strip: vertical ticks marking where each strummed note lands in time, mirroring
 * the tick strip at the bottom of the Arpeggiator's Time Bend curve. The strum's
 * timing is a fixed shape preset, so there is no draggable curve editor - the
 * strip is purely a visualisation of the selected preset + cycles.
 */
class StrumUI : public juce::Component, private juce::ValueTree::Listener {
  public:
    StrumUI();
    ~StrumUI() override;

    void setPlugin(daw::audio::MidiStrumPlugin* plugin);

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Read-only viz: vertical ticks at the strum onset positions.
    class OnsetStrip : public juce::Component {
      public:
        void setOnsets(std::vector<float> onsets);
        void paint(juce::Graphics& g) override;

      private:
        std::vector<float> onsets_;
    };

    daw::audio::MidiStrumPlugin* plugin_ = nullptr;
    juce::ValueTree watchedState_;

    juce::Label triggerLabel_;
    juce::ComboBox triggerCombo_;
    juce::Label orderLabel_;
    juce::ComboBox orderCombo_;
    juce::Label shapeLabel_;
    juce::ComboBox shapeCombo_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cyclesSlider_;
    juce::Label lengthLabel_;
    LinkableTextSlider lengthSlider_;
    juce::Label loopModeLabel_;
    juce::ComboBox loopModeCombo_;
    juce::Label loopLabel_;
    LinkableTextSlider syncSlider_;  // Loop interval in Time (ms) mode.
    juce::ComboBox loopRateCombo_;   // Loop division in Beat-sync mode.
    juce::Label vizLabel_;
    OnsetStrip onsetStrip_;

    void syncFromPlugin();
    void refreshOnsets();
    void updateLoopControls();
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& combo);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    // ValueTree::Listener — resync UI when plugin state changes externally.
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrumUI)
};

}  // namespace magda::daw::ui
