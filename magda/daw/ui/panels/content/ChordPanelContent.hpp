#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "audio/MidiChordEnginePlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Chord analysis side panel for the bottom panel
 *
 * Shows real-time chord detection from the MidiChordEnginePlugin:
 * - Current chord name (large display)
 * - Detected key/mode
 * - Recent chord history
 *
 * Displayed alongside the piano roll in a split layout when the track
 * has a MIDI device (e.g. Chord Engine) loaded.
 */
class ChordPanelContent : public juce::Component, private juce::Timer {
  public:
    ChordPanelContent();
    ~ChordPanelContent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Set the chord engine plugin to read state from. nullptr to disconnect. */
    void setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin);

  private:
    void timerCallback() override;

    magda::daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;

    // Cached display state
    juce::String currentChord_;
    juce::String detectedKey_;
    std::vector<juce::String> recentChords_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPanelContent)
};

}  // namespace magda::daw::ui
