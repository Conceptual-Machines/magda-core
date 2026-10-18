#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "TypeIds.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "core/Config.hpp"
#include "music/ChordEngine.hpp"
#include "music/Scales.hpp"
#include "ui/components/chord/ChordFunction.hpp"
#include "ui/themes/ActiveTheme.hpp"
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
    explicit ScaleBlockComponent(magda::music::ScaleWithChords scale);

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
    ScaleChordsPopup(magda::music::ScaleWithChords scale);

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

/**
 * @brief Adjoining toggles in one field, each lit on its own: the suggestion extensions.
 */
class ToggleStrip : public juce::Component, public juce::SettableTooltipClient {
  public:
    void setOptions(const juce::StringArray& options);
    void setOn(int index, bool on);
    bool isOn(int index) const;
    int getPreferredWidth() const;

    std::function<void(int index, bool on)> onToggle;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    juce::Rectangle<float> segmentBounds(int index) const;

    juce::StringArray options_;
    std::vector<bool> on_;
};

using AIProgression = magda::daw::audio::MidiChordEnginePlugin::AIProgression;

/**
 * @brief Expandable scale row for the browse view — shows scale name, expands to chord blocks
 */
class BrowseScaleRowComponent : public juce::Component {
  public:
    explicit BrowseScaleRowComponent(magda::music::ScaleWithChords scale);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    static int getRowHeight();
    const magda::music::ScaleWithChords& getScale() const {
        return scale_;
    }

  private:
    static constexpr int ROW_HEIGHT = 30;
    magda::music::ScaleWithChords scale_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowseScaleRowComponent)
};

/**
 * @brief The Chord Engine's face, in its device slot and beside the chord editor.
 *
 * Three columns over a footer: the sounding chord and its history | suggestions (K&S or AI) |
 * key and scales. The footer holds novelty and the extensions, or the AI prompt.
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
    void mouseDrag(const juce::MouseEvent& e) override;

    void setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin,
                        magda::TrackId trackId = magda::INVALID_TRACK_ID);
    /** Docked beside the chord editor: no keyboard, and the footer clears the collapse button. */
    void setInEditorSidebar(bool inSidebar);
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
    /** The roman numeral of @p chord in the detected key, or empty without one. */
    juce::String degreeInKey(const magda::music::Chord& chord) const;
    void paintCurrentChord(juce::Graphics& g, juce::Rectangle<int> box) const;
    static void paintSectionLabel(juce::Graphics& g, juce::Rectangle<int> area,
                                  const juce::String& label, const juce::String& note = {});

    magda::daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;
    // Prime the bound engine from the chord-track progression (no-op unless the
    // bound track is the chord track) so suggestions/key reflect the song.
    void seedEngineFromProgression();

    magda::TrackId trackId_ = magda::INVALID_TRACK_ID;
    bool inEditorSidebar_ = false;

    void previewChord(const magda::music::Chord& chord);
    void stopPreview();

    // Cached display state
    juce::String currentChord_;
    magda::music::Chord currentChordInfo_;
    juce::String detectedKey_;
    std::optional<chord_function::Key> key_;
    std::vector<juce::String> recentChords_;
    std::vector<magda::music::ChordEngine::SuggestionItem> suggestions_;
    std::vector<magda::music::ScaleWithChords> detectedScales_;
    std::set<std::string> selectedScaleNames_;  // persists across rebuilds

    // Child components — chord blocks and scale blocks
    /// The diatonic suggestions first, then the rest: two groups under their own headers.
    std::vector<std::unique_ptr<ChordBlockComponent>> suggestionBlocks_;
    size_t diatonicCount_ = 0;
    std::vector<std::unique_ptr<ChordBlockComponent>> historyBlocks_;
    std::vector<std::unique_ptr<ScaleBlockComponent>> scaleBlocks_;

    // Active popup (scale chords)
    std::unique_ptr<ScaleChordsPopup> activePopup_;

    // Footer controls
    juce::Slider noveltySlider_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    /// 7th, 9th, 11th, 13th and Alt, in that order.
    ToggleStrip extensions_;
    std::unique_ptr<juce::TextButton> notationBtn_;  // cycles C / Do / both notation
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
        std::unique_ptr<magda::SvgButton> exportButton;
        int progressionIndex = 0;
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
    void startProgressionDrag(int progressionIndex);

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
    static std::vector<AIProgression> parseAIResponse(const juce::String& dsl);

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

    // Areas resized() lays out and paint() draws into.
    juce::Rectangle<int> detectionCol_;
    juce::Rectangle<int> suggestionsCol_;
    juce::Rectangle<int> keyScaleCol_;
    juce::Rectangle<int> footer_;
    juce::Rectangle<int> chordHeader_;
    juce::Rectangle<int> chordBox_;
    juce::Rectangle<int> statusLine_;
    juce::Rectangle<int> historyHeader_;
    juce::Rectangle<int> tabNote_;
    juce::Rectangle<int> legend_;
    juce::Rectangle<int> diatonicHeader_;
    juce::Rectangle<int> beyondHeader_;
    juce::Rectangle<int> keyHeader_;
    juce::Rectangle<int> keyBox_;
    juce::Rectangle<int> browseBox_;
    juce::Rectangle<int> scalesHeader_;
    juce::Rectangle<int> noveltyCaption_;
    juce::Rectangle<int> noveltySafe_;
    juce::Rectangle<int> noveltyWild_;
    juce::Rectangle<int> noveltyValue_;
    juce::Rectangle<int> extensionsCaption_;
    juce::Rectangle<int> footerDivider_;
    std::vector<juce::Point<int>> historyArrows_;

    static constexpr int SECTION_HEADER_HEIGHT = 24;
    static constexpr int HISTORY_HEIGHT = 34;
    static constexpr int TILE_HEIGHT = 54;
    static constexpr int SCALE_ROW_HEIGHT = 36;
    static constexpr int GAP = 8;
    static constexpr int PADDING = 14;
    static constexpr int FOOTER_HEIGHT = 46;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordPanelContent)
};

}  // namespace magda::daw::ui
