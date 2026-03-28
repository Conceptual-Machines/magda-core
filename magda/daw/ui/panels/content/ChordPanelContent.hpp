#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "TypeIds.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "core/Config.hpp"
#include "music/ChordEngine.hpp"
#include "music/Scales.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {
class DraggableValueLabel;
class SvgButton;
}  // namespace magda

namespace magda::daw::ui {

class ChordBlockComponent;

/**
 * @brief Toggleable scale block — click to select/deselect for suggestion filtering
 */
class ScaleBlockComponent : public juce::Component {
  public:
    explicit ScaleBlockComponent(const magda::music::ScaleWithChords& scale);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    const magda::music::ScaleWithChords& getScale() const {
        return scale_;
    }

    void setSelected(bool selected) {
        selected_ = selected;
        repaint();
    }
    bool isSelected() const {
        return selected_;
    }

  private:
    magda::music::ScaleWithChords scale_;
    bool selected_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaleBlockComponent)
};

/**
 * @brief Modal popup showing diatonic chord blocks from a scale
 */
class ScaleChordsPopup : public juce::Component {
  public:
    ScaleChordsPopup(const magda::music::ScaleWithChords& scale);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void showAt(juce::Component* parent, juce::Rectangle<int> targetArea);
    std::string getScaleName() const {
        return scale_.name;
    }

  private:
    magda::music::ScaleWithChords scale_;
    std::vector<std::unique_ptr<ChordBlockComponent>> chordBlocks_;
    void inputAttemptWhenModal() override;
    void dismiss();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaleChordsPopup)
};

using AIProgression = magda::daw::audio::MidiChordEnginePlugin::AIProgression;

/**
 * @brief Expandable scale row for the browse view — shows scale name, expands to chord blocks
 */
class BrowseScaleRowComponent : public juce::Component {
  public:
    explicit BrowseScaleRowComponent(const magda::music::ScaleWithChords& scale);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    int getRowHeight() const;
    const magda::music::ScaleWithChords& getScale() const {
        return scale_;
    }

  private:
    static constexpr int ROW_HEIGHT = 24;
    magda::music::ScaleWithChords scale_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowseScaleRowComponent)
};

/**
 * @brief Chord analysis side panel for the bottom panel
 *
 * Three-column layout with footer toolbar:
 *   Columns: Chord Detection | Suggestions | Key / Scale
 *   Footer:  [collapse] | novelty / 7ths / 9ths / alt | [explorer]
 */
class ChordPanelContent : public juce::Component,
                          private magda::daw::audio::MidiChordEnginePlugin::Listener,
                          private magda::ConfigListener,
                          private juce::Timer {
  public:
    ChordPanelContent();
    ~ChordPanelContent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin,
                        magda::TrackId trackId = magda::INVALID_TRACK_ID);
    void toggleScaleSelection(ScaleBlockComponent* block);
    void showScalePopup(const magda::music::ScaleWithChords& scale, juce::Component* source);

  private:
    // MidiChordEnginePlugin::Listener
    void chordChanged(magda::daw::audio::MidiChordEnginePlugin*) override;
    void keyModeChanged(magda::daw::audio::MidiChordEnginePlugin*) override;
    void suggestionsChanged(magda::daw::audio::MidiChordEnginePlugin*) override;

    // ConfigListener
    void configChanged() override;

    void updateFromPlugin();
    void timerCallback() override;  // delayed chord clear after note release
    void rebuildSuggestionBlocks();
    void rebuildHistoryBlocks();
    void rebuildScaleBlocks();
    void setupFooterControls();
    void syncFooterFromParams();
    void updateScaleFilterPitchClasses();

    magda::daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;
    magda::TrackId trackId_ = magda::INVALID_TRACK_ID;

    void previewChord(const magda::music::Chord& chord);
    void stopPreview();

    // Cached display state
    juce::String currentChord_;
    juce::String detectedKey_;
    std::vector<juce::String> recentChords_;
    std::vector<magda::music::ChordEngine::SuggestionItem> suggestions_;
    std::vector<magda::music::ScaleWithChords> detectedScales_;
    std::set<std::string> selectedScaleNames_;  // persists across rebuilds

    // Child components — chord blocks and scale blocks
    std::vector<std::unique_ptr<ChordBlockComponent>> suggestionBlocks_;
    std::vector<std::unique_ptr<ChordBlockComponent>> historyBlocks_;
    std::vector<std::unique_ptr<ScaleBlockComponent>> scaleBlocks_;

    // Active popup (scale chords)
    std::unique_ptr<ScaleChordsPopup> activePopup_;

    // Footer controls
    std::unique_ptr<magda::DraggableValueLabel> noveltyLabel_;
    std::unique_ptr<juce::TextButton> add7thsBtn_;
    std::unique_ptr<juce::TextButton> add9thsBtn_;
    std::unique_ptr<juce::TextButton> addAltBtn_;
    std::unique_ptr<juce::TextButton> add11thsBtn_;
    std::unique_ptr<juce::TextButton> add13thsBtn_;
    std::unique_ptr<magda::SvgButton> scaleFilterBtn_;
    std::unique_ptr<magda::SvgButton> browseBtn_;
    std::unique_ptr<magda::SvgButton> backBtn_;
    std::unique_ptr<magda::SvgButton> clearHistoryBtn_;

    // Preview state
    std::vector<int> previewingNotes_;  // MIDI note numbers currently sounding

    // Suggestion tabs: K&S (Krumhansl-Schmuckler) vs AI
    enum class SuggestionTab { KS, AI };
    SuggestionTab suggestionTab_ = SuggestionTab::KS;
    std::unique_ptr<magda::SvgButton> ksTabBtn_;
    std::unique_ptr<magda::SvgButton> aiTabBtn_;
    juce::Label aiModelLabel_;

    // AI tab — inline progression display + text input
    std::unique_ptr<juce::Viewport> aiViewport_;
    std::unique_ptr<juce::Component> aiContainer_;
    struct AIProgressionRow {
        juce::Rectangle<int> nameArea;
        juce::Rectangle<int> descArea;
        std::vector<std::unique_ptr<ChordBlockComponent>> blocks;
    };
    std::vector<std::unique_ptr<AIProgressionRow>> aiRows_;
    std::unique_ptr<juce::TextEditor> aiInputBox_;
    std::unique_ptr<magda::SvgButton> aiSendBtn_;
    bool aiLoading_ = false;
    bool aiGreyOut_ = false;        // dim old results while generating
    juce::String aiStreamingText_;  // accumulated tokens during streaming
    juce::String aiPromptText_;     // user prompt shown during generation

    void switchToTab(SuggestionTab tab);
    void rebuildAIProgressionRows();
    void layoutAIProgressionRows();

    // Browse mode state
    bool browseMode_ = false;
    int browseKeyFilter_ = -1;  // -1 = all keys, 0-11 = specific root
    std::vector<std::unique_ptr<juce::TextButton>> browseKeyButtons_;
    std::unique_ptr<juce::Viewport> browseViewport_;
    std::unique_ptr<juce::Component> browseContainer_;
    std::vector<std::unique_ptr<BrowseScaleRowComponent>> browseRows_;

    void enterBrowseMode();
    void exitBrowseMode();
    void rebuildBrowseRows();
    void layoutBrowseRows();

    // AI chord suggestion
    void requestAISuggestions();
    std::vector<AIProgression> parseAIResponse(const juce::String& json);

    class AIRequestThread : public juce::Thread {
      public:
        AIRequestThread(ChordPanelContent& owner, juce::String userPrompt)
            : juce::Thread("AIChordSuggest"), owner_(owner), userPrompt_(std::move(userPrompt)) {}
        void run() override;

      private:
        ChordPanelContent& owner_;
        juce::String userPrompt_;
    };
    std::unique_ptr<AIRequestThread> aiThread_;
    std::atomic<bool> aiCancelFlag_{false};

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
    static constexpr int FOOTER_HEIGHT = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPanelContent)
};

}  // namespace magda::daw::ui
