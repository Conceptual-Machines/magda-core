#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/ArpeggiatorPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Two-column UI for the ArpeggiatorPlugin.
 *
 * Left column:  Pattern (combo), Rate (combo), Octaves (combo), Latch (toggle)
 * Right column: Gate (slider), Swing (slider), Velocity Mode (combo), Fixed Vel (slider)
 *
 * Reads/writes CachedValues on the plugin's ValueTree directly.
 */
class ArpeggiatorUI : public juce::Component, private juce::ValueTree::Listener {
  public:
    ArpeggiatorUI();
    ~ArpeggiatorUI() override;

    void setArpeggiator(daw::audio::ArpeggiatorPlugin* plugin);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    daw::audio::ArpeggiatorPlugin* plugin_ = nullptr;
    juce::ValueTree watchedState_;

    // Left column
    juce::Label patternLabel_;
    juce::ComboBox patternCombo_;
    juce::Label rateLabel_;
    juce::Slider rateSlider_;
    juce::Label octavesLabel_;
    juce::Slider octavesSlider_;
    juce::Label latchLabel_;
    juce::TextButton latchButton_{"OFF"};

    // Right column
    juce::Label gateLabel_;
    juce::Slider gateSlider_;
    juce::Label swingLabel_;
    juce::Slider swingSlider_;
    juce::Label velModeLabel_;
    juce::ComboBox velModeCombo_;
    juce::Label fixedVelLabel_;
    juce::Slider fixedVelSlider_;

    void syncFromPlugin();
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupCombo(juce::ComboBox& combo);
    void setupSlider(juce::Slider& slider, double min, double max, double step);

    // ValueTree::Listener — sync UI when plugin state changes externally
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorUI)
};

}  // namespace magda::daw::ui
