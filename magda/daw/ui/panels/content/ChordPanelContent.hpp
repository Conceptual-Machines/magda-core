#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "audio/MidiChordEnginePlugin.hpp"
#include "music/ChordEngine.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

class ChordBlockComponent;

/**
 * @brief Chord analysis side panel for the bottom panel
 *
 * Three-column layout:
 *   1. Chord Detection — current chord (large), recent history (last 5)
 *   2. Suggestions — draggable chord blocks from the engine
 *   3. Key / Scale — detected key/mode display
 *
 * Chord blocks are draggable onto the piano roll to create MIDI notes.
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
    void rebuildSuggestionBlocks();
    void rebuildHistoryBlocks();

    magda::daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;

    // Cached display state
    juce::String currentChord_;
    juce::String detectedKey_;
    std::vector<juce::String> recentChords_;
    std::vector<magda::music::ChordEngine::SuggestionItem> suggestions_;

    // Child components — chord blocks
    std::vector<std::unique_ptr<ChordBlockComponent>> suggestionBlocks_;
    std::vector<std::unique_ptr<ChordBlockComponent>> historyBlocks_;

    // Column areas (computed in resized, used in paint for headers)
    juce::Rectangle<int> detectionCol_;
    juce::Rectangle<int> suggestionsCol_;
    juce::Rectangle<int> keyScaleCol_;

    // Layout constants
    static constexpr int COLUMN_GAP = 1;
    static constexpr int SECTION_HEADER_HEIGHT = 20;
    static constexpr int BLOCK_HEIGHT = 32;
    static constexpr int BLOCK_GAP = 4;
    static constexpr int PADDING = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPanelContent)
};

}  // namespace magda::daw::ui
