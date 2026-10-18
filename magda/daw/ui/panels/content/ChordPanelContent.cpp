#include "ChordPanelContent.hpp"

#include <array>
#include <utility>

#include "../../../../agents/chord_agent.hpp"
#include "BinaryData.h"
#include "core/ChordProgressionContext.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/LLMClientProvider.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/TrackManager.hpp"
#include "music/ChordEngine.hpp"
#include "music/NotationSettings.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/components/chord/ChordBlockComponent.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

// ============================================================================
// ScaleBlockComponent
// ============================================================================

ScaleBlockComponent::ScaleBlockComponent(magda::music::ScaleWithChords scale)
    : scale_(std::move(scale)) {
    setName("ScaleBlock");
    setRepaintsOnMouseActivity(true);
}

void ScaleBlockComponent::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ActiveTheme::getColour(selected_       ? ActiveTheme::DEVICE_ROW_SELECTED
                                       : isMouseOver() ? ActiveTheme::DEVICE_ROW_HOVER
                                                       : ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(ActiveTheme::getColour(selected_ ? ActiveTheme::DEVICE_ROW_SELECTED_BORDER
                                                 : ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    auto text = getLocalBounds().reduced(12, 0);
    g.setFont(fonts.getMonoFont(10.5f));
    g.setColour(
        ActiveTheme::getColour(selected_ ? ActiveTheme::DEVICE_BLUE : ActiveTheme::DEVICE_DIM));
    // The note count gives way to the name in a narrow column.
    if (getWidth() >= 190) {
        const auto count = juce::String(static_cast<int>(scale_.pitches.size())) + " notes";
        g.drawText(count, text, juce::Justification::centredRight, false);
        text.removeFromRight(60);
    }

    g.setFont(fonts.getUIFont(12.5f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.drawFittedText(
        magda::music::NotationSettings::getInstance().formatRoot(juce::String(scale_.name)), text,
        juce::Justification::centredLeft, 1, 0.75f);
}

void ScaleBlockComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isShiftDown()) {
        // Shift-click toggles selection for filtering
        if (auto* parent = dynamic_cast<ChordPanelContent*>(getParentComponent()))
            parent->toggleScaleSelection(this);
    } else {
        // Click opens diatonic chord popup
        if (auto* parent = dynamic_cast<ChordPanelContent*>(getParentComponent()))
            parent->showScalePopup(scale_, this);
    }
}

void ScaleBlockComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    repaint();
}

void ScaleBlockComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    repaint();
}

// ============================================================================
// ScaleChordsPopup
// ============================================================================

ScaleChordsPopup::ScaleChordsPopup(magda::music::ScaleWithChords scale) : scale_(std::move(scale)) {
    setName("ScaleChordsPopup");

    // Build chord blocks for each diatonic triad
    for (size_t i = 0; i < scale_.chords.size(); ++i) {
        auto block = std::make_unique<ChordBlockComponent>(scale_.chords[i]);

        // Roman numeral degree label
        static const char* romanNumerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        if (i < 7) {
            juce::String degree = romanNumerals[i];
            // Lowercase for minor/diminished
            if (scale_.chords[i].quality == magda::music::ChordQuality::Minor ||
                scale_.chords[i].quality == magda::music::ChordQuality::Diminished) {
                degree = degree.toLowerCase();
            }
            if (scale_.chords[i].quality == magda::music::ChordQuality::Diminished)
                degree += juce::String(juce::CharPointer_UTF8("\xc2\xb0"));  // degree symbol °
            block->setDegreeLabel(degree);
        }

        addAndMakeVisible(block.get());
        chordBlocks_.push_back(std::move(block));
    }
}

void ScaleChordsPopup::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_PANEL));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    auto titleArea = getLocalBounds().reduced(10, 6).removeFromTop(20);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.setFont(FontManager::getInstance().getMonoFont(10.5f).withExtraKerningFactor(0.12f));
    g.drawText(magda::music::NotationSettings::getInstance().formatRoot(juce::String(scale_.name)),
               titleArea, juce::Justification::centredLeft);
}

void ScaleChordsPopup::resized() {
    auto area = getLocalBounds().reduced(10, 6);
    area.removeFromTop(24);  // title

    // Grid of chord blocks — single row if they fit, wrap otherwise
    int blockWidth = 84;
    int blockHeight = 46;
    int gap = 4;

    int x = area.getX();
    int y = area.getY();

    for (auto& block : chordBlocks_) {
        if (x + blockWidth > area.getRight()) {
            x = area.getX();
            y += blockHeight + gap;
        }
        block->setBounds(x, y, blockWidth, blockHeight);
        x += blockWidth + gap;
    }
}

void ScaleChordsPopup::mouseDown(const juce::MouseEvent& e) {
    // Close if clicking outside chord blocks
    bool clickedBlock = false;
    for (auto& block : chordBlocks_) {
        if (block->getBounds().contains(e.getPosition())) {
            clickedBlock = true;
            break;
        }
    }
    if (!clickedBlock)
        dismiss();
}

void ScaleChordsPopup::inputAttemptWhenModal() {
    dismiss();
}

void ScaleChordsPopup::dismiss() {
    exitModalState(0);
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
}

void ScaleChordsPopup::showAt(juce::Component* parent, juce::Rectangle<int> targetArea) {
    // Calculate size based on chord count
    int numChords = static_cast<int>(chordBlocks_.size());
    int blockWidth = 84;
    int blockHeight = 46;
    int gap = 4;
    int cols = std::min(numChords, 7);
    int rows = (numChords + cols - 1) / cols;

    int popupWidth = cols * (blockWidth + gap) - gap + 20;
    int popupHeight = rows * (blockHeight + gap) - gap + 34;

    // Position above the target area
    auto screenTarget = parent->localAreaToGlobal(targetArea);
    int popupX = screenTarget.getX();
    int popupY = screenTarget.getY() - popupHeight - 4;

    // Keep on screen
    auto screenBounds = parent->getTopLevelComponent()->getScreenBounds();
    if (popupY < screenBounds.getY())
        popupY = screenTarget.getBottom() + 4;
    if (popupX + popupWidth > screenBounds.getRight())
        popupX = screenBounds.getRight() - popupWidth;

    auto screenRect = juce::Rectangle<int>(popupX, popupY, popupWidth, popupHeight);
    setBounds(parent->getTopLevelComponent()->getLocalArea(nullptr, screenRect));
    parent->getTopLevelComponent()->addAndMakeVisible(this);
    enterModalState(false, nullptr, false);  // lifetime managed by activePopup_
}

// ============================================================================
// BrowseScaleRowComponent
// ============================================================================

BrowseScaleRowComponent::BrowseScaleRowComponent(magda::music::ScaleWithChords scale)
    : scale_(std::move(scale)) {
    setName("BrowseScaleRow");
    setRepaintsOnMouseActivity(true);
}

void BrowseScaleRowComponent::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ActiveTheme::getColour(isMouseOver() ? ActiveTheme::DEVICE_ROW_HOVER
                                                     : ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(magda::music::NotationSettings::getInstance().formatRoot(juce::String(scale_.name)),
               bounds.reduced(10, 0), juce::Justification::centredLeft);
}

void BrowseScaleRowComponent::resized() {}

void BrowseScaleRowComponent::mouseDown(const juce::MouseEvent&) {
    // Walk up to ChordPanelContent and show the scale chords popup
    for (auto* comp = getParentComponent(); comp != nullptr; comp = comp->getParentComponent()) {
        if (auto* panel = dynamic_cast<ChordPanelContent*>(comp)) {
            panel->showScalePopup(scale_, this);
            return;
        }
    }
}

void BrowseScaleRowComponent::mouseEnter(const juce::MouseEvent&) {
    repaint();
}
void BrowseScaleRowComponent::mouseExit(const juce::MouseEvent&) {
    repaint();
}

int BrowseScaleRowComponent::getRowHeight() {
    return ROW_HEIGHT;
}

// ============================================================================
// AIContainerComponent — paints progression names/descriptions over chord blocks
// ============================================================================

namespace {

// Paints progression names/descriptions; owner pointer + data set externally
struct AIContainerPaintData {
    struct Row {
        juce::String name;
        juce::String description;
        juce::Rectangle<int> nameArea;
        juce::Rectangle<int> descArea;
    };
    std::vector<Row> rows;
};

class AIContainerComponent : public juce::Component {
  public:
    AIContainerPaintData paintData;
    bool greyOut = false;
    juce::String streamingText;
    juce::String promptText;
    bool loading = false;
    int streamingTextBottom = 0;  // y position after streaming text block

    void paint(juce::Graphics& g) override {
        float alpha = greyOut ? 0.3f : 1.0f;

        for (auto& row : paintData.rows) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT).withAlpha(alpha));
            g.setFont(FontManager::getInstance().getUIFontMedium(12.5f));
            g.drawText(row.name, row.nameArea, juce::Justification::centredLeft);

            if (row.description.isNotEmpty() && !row.descArea.isEmpty()) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM).withAlpha(alpha));
                g.setFont(FontManager::getInstance().getMonoFont(10.0f));
                g.drawText(row.description, row.descArea, juce::Justification::centredLeft);
            }
        }

        // Draw prompt + status above streaming text
        int promptBottom = streamingTextBottom;
        if (loading && paintData.rows.empty()) {
            int y = 8;
            if (promptText.isNotEmpty()) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
                g.setFont(FontManager::getInstance().getUIFont(12.0f));
                g.drawText(promptText, 4, y, getWidth() - 8, 20, juce::Justification::centredLeft);
                y += 24;
            }
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("Generating...", 4, y, getWidth() - 8, 20, juce::Justification::centredLeft);
            promptBottom = juce::jmax(promptBottom, y + 28);
        }

        // Draw streaming text below prompt/existing content
        if (streamingText.isNotEmpty()) {
            auto font = FontManager::getInstance().getMonoFont(10.0f);
            g.setFont(font);

            int textY = promptBottom;
            int textWidth = getWidth() - 8;
            auto layout = juce::TextLayout();
            juce::AttributedString attrStr;
            attrStr.append(streamingText, font, ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            attrStr.setWordWrap(juce::AttributedString::WordWrap::byWord);
            layout.createLayout(attrStr, static_cast<float>(textWidth));
            layout.draw(g, juce::Rectangle<float>(4.0f, static_cast<float>(textY),
                                                  static_cast<float>(textWidth),
                                                  static_cast<float>(getHeight() - textY)));
        }
    }
};

}  // anonymous namespace

// ============================================================================
// ToggleStrip
// ============================================================================

namespace {
constexpr float kStripInset = 4.0f;
constexpr float kStripSegmentPadding = 14.0f;

juce::Font stripFont() {
    return FontManager::getInstance().getUIFont(12.5f);
}
}  // namespace

void ToggleStrip::setOptions(const juce::StringArray& options) {
    options_ = options;
    on_.assign(static_cast<size_t>(options.size()), false);
    repaint();
}

void ToggleStrip::setOn(int index, bool on) {
    if (index < 0 || index >= static_cast<int>(on_.size()))
        return;
    on_[static_cast<size_t>(index)] = on;
    repaint();
}

bool ToggleStrip::isOn(int index) const {
    return index >= 0 && index < static_cast<int>(on_.size()) && on_[static_cast<size_t>(index)];
}

int ToggleStrip::getPreferredWidth() const {
    float width = 2.0f * kStripInset;
    for (const auto& option : options_)
        width += juce::GlyphArrangement::getStringWidth(stripFont(), option) +
                 2.0f * kStripSegmentPadding;
    return juce::roundToInt(width);
}

juce::Rectangle<float> ToggleStrip::segmentBounds(int index) const {
    // Widths follow the labels, scaled to whatever room the strip was given.
    const auto inner = getLocalBounds().toFloat().reduced(kStripInset);
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& option : options_) {
        widths.push_back(juce::GlyphArrangement::getStringWidth(stripFont(), option) +
                         2.0f * kStripSegmentPadding);
        total += widths.back();
    }
    const float scale = total > 0.0f ? inner.getWidth() / total : 1.0f;
    float x = inner.getX();
    for (int i = 0; i < index; ++i)
        x += widths[static_cast<size_t>(i)] * scale;
    return {x, inner.getY(), widths[static_cast<size_t>(index)] * scale, inner.getHeight()};
}

void ToggleStrip::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    g.setFont(stripFont());
    for (int i = 0; i < options_.size(); ++i) {
        const auto segment = segmentBounds(i);
        if (isOn(i)) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
            g.fillRoundedRectangle(segment, 4.0f);
        }
        g.setColour(
            ActiveTheme::getColour(isOn(i) ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_DIM));
        g.drawText(options_[i], segment, juce::Justification::centred, false);
    }
}

void ToggleStrip::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < options_.size(); ++i) {
        if (!segmentBounds(i).contains(e.position))
            continue;
        setOn(i, !isOn(i));
        if (onToggle)
            onToggle(i, isOn(i));
        return;
    }
}

// ============================================================================
// ChordPanelContent
// ============================================================================

ChordPanelContent::ChordPanelContent() {
    setName("ChordPanel");
    setupFooterControls();
    Config::getInstance().addListener(this);
}

ChordPanelContent::~ChordPanelContent() {
    Config::getInstance().removeListener(this);
    if (aiThread_ && aiThread_->isThreadRunning()) {
        aiCancelFlag_ = true;
        aiThread_->stopThread(2000);
    }
    stopTimer();
    stopPreview();
    if (chordPlugin_)
        chordPlugin_->removeListener(this);
}

void ChordPanelContent::previewChord(const magda::music::Chord& chord) {
    stopPreview();
    if (trackId_ == magda::INVALID_TRACK_ID || chord.notes.empty())
        return;

    // Transpose to a reasonable range if notes are too low/high.
    // Find the average MIDI note and shift so it's near middle C (60).
    int sum = 0;
    for (const auto& note : chord.notes)
        sum += note.noteNumber;
    int avg = sum / static_cast<int>(chord.notes.size());
    int shift = 0;
    if (avg < 48)
        shift = ((60 - avg) / 12) * 12;  // shift up in octaves
    else if (avg > 84)
        shift = -((avg - 60) / 12) * 12;  // shift down in octaves

    if (chordPlugin_)
        chordPlugin_->setDetectionSuppressed(true);

    for (const auto& note : chord.notes) {
        int n = std::clamp(note.noteNumber + shift, 0, 127);
        magda::TrackManager::getInstance().previewNote(trackId_, n, 100, true);
        previewingNotes_.push_back(n);
    }
}

void ChordPanelContent::stopPreview() {
    if (previewingNotes_.empty() || trackId_ == magda::INVALID_TRACK_ID)
        return;
    for (int noteNum : previewingNotes_)
        magda::TrackManager::getInstance().previewNote(trackId_, noteNum, 0, false);
    previewingNotes_.clear();

    if (chordPlugin_)
        chordPlugin_->setDetectionSuppressed(false);
}

void ChordPanelContent::setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin,
                                       magda::TrackId trackId) {
    if (chordPlugin_ == plugin) {
        // Plugin unchanged — just update trackId (may arrive late from setNodePath)
        trackId_ = trackId;
        seedEngineFromProgression();
        return;
    }

    stopPreview();

    if (chordPlugin_)
        chordPlugin_->removeListener(this);

    chordPlugin_ = plugin;
    trackId_ = trackId;
    currentChord_.clear();
    detectedKey_.clear();
    recentChords_.clear();
    suggestions_.clear();
    detectedScales_.clear();
    selectedScaleNames_.clear();
    suggestionBlocks_.clear();
    historyBlocks_.clear();
    scaleBlocks_.clear();

    if (plugin) {
        plugin->addListener(this);
        syncFooterFromParams();
        updateFromPlugin();  // sync existing state immediately

        // Restore AI progression display if plugin has persisted results
        if (!plugin->getAIProgressions().empty())
            rebuildAIProgressionRows();

        seedEngineFromProgression();
    }

    repaint();
}

void ChordPanelContent::setInEditorSidebar(bool inSidebar) {
    inEditorSidebar_ = inSidebar;
    resized();
    repaint();
}

void ChordPanelContent::seedEngineFromProgression() {
    if (chordPlugin_ == nullptr || trackId_ != magda::TrackManager::getInstance().getChordTrackId())
        return;

    auto& engine = magda::music::ChordEngine::getInstance();
    std::vector<magda::music::Chord> chords;
    for (const auto& p : magda::ChordProgressionContext::current()) {
        const auto spec = magda::music::ChordEngine::parseChordName(p.name);
        chords.push_back(engine.buildChordInRootPosition(spec.root, spec.quality));
    }
    chordPlugin_->seedFromChords(chords);
}

void ChordPanelContent::chordChanged(magda::daw::audio::MidiChordEnginePlugin*) {
    // Async because the caller holds stateMutex_ and updateFromPlugin() re-locks it
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->updateFromPlugin();
    });
}

void ChordPanelContent::keyModeChanged(magda::daw::audio::MidiChordEnginePlugin*) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->updateFromPlugin();
    });
}

void ChordPanelContent::suggestionsChanged(magda::daw::audio::MidiChordEnginePlugin*) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->updateFromPlugin();
    });
}

void ChordPanelContent::configChanged() {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis && safeThis->suggestionTab_ == SuggestionTab::AI) {
            auto cfg = Config::getInstance().getAgentLLMConfig(magda::role::CHORD);
            auto model = cfg.model.empty() ? cfg.provider : cfg.model;
            safeThis->aiModelLabel_.setText(juce::String(model), juce::dontSendNotification);
        }
    });
}

void ChordPanelContent::timerCallback() {
    // Delayed clear of chord display after note release
    stopTimer();
    currentChord_.clear();
    currentChordInfo_ = {};
    for (auto& block : suggestionBlocks_)
        block->setHighlighted(false);
    repaint();
}

void ChordPanelContent::updateFromPlugin() {
    if (!chordPlugin_)
        return;

    bool needsRepaint = false;
    bool needsLayout = false;

    // Key/mode
    auto keyMode = chordPlugin_->getDetectedKeyMode();
    juce::String keyStr;
    if (keyMode.has_value())
        keyStr = keyMode->first + " " + keyMode->second;
    if (keyStr != detectedKey_) {
        detectedKey_ = keyStr;
        key_ = keyMode.has_value() ? chord_function::parseKey(keyMode->first, keyMode->second)
                                   : std::nullopt;
        recentChords_.clear();  // rebuilds the history below with degrees in the new key
        needsRepaint = true;
    }

    // Recent chords (last 8)
    auto history = chordPlugin_->getRecentChords();
    std::vector<juce::String> names;
    int start = std::max(0, static_cast<int>(history.size()) - 8);
    for (int i = start; i < static_cast<int>(history.size()); ++i)
        names.push_back(history[static_cast<size_t>(i)].getDisplayName());
    if (names != recentChords_) {
        recentChords_ = names;
        rebuildHistoryBlocks();
        needsLayout = true;
    }

    // Current chord — live from detection (clears on note release)
    auto chord = chordPlugin_->getCurrentChordName();
    if (chord != currentChord_) {
        if (chord.isNotEmpty()) {
            // New chord detected — show it immediately, cancel any pending clear
            stopTimer();
            currentChord_ = chord;
            currentChordInfo_ = chordPlugin_->getCurrentChord();
            for (auto& block : suggestionBlocks_)
                block->setHighlighted(block->getChord().getDisplayName() == currentChord_);
            needsRepaint = true;
        } else if (currentChord_.isNotEmpty()) {
            // Notes released — start 1-second delayed clear
            startTimer(1000);
        }
    }

    // Suggestions
    auto newSuggestions = chordPlugin_->getSuggestions();
    bool suggestionsChanged = newSuggestions.size() != suggestions_.size();
    if (!suggestionsChanged) {
        for (size_t i = 0; i < newSuggestions.size(); ++i) {
            if (newSuggestions[i].chord.getDisplayName() !=
                suggestions_[i].chord.getDisplayName()) {
                suggestionsChanged = true;
                break;
            }
        }
    }
    if (suggestionsChanged) {
        suggestions_ = newSuggestions;
        rebuildSuggestionBlocks();
        needsLayout = true;
    }

    // Detected scales (top 5)
    auto newScales = chordPlugin_->getDetectedScales(5);
    bool scalesChanged = newScales.size() != detectedScales_.size();
    if (!scalesChanged) {
        for (size_t i = 0; i < newScales.size(); ++i) {
            if (newScales[i].name != detectedScales_[i].name) {
                scalesChanged = true;
                break;
            }
        }
    }
    if (scalesChanged) {
        detectedScales_ = std::move(newScales);
        rebuildScaleBlocks();
        needsLayout = true;
    }

    if (needsLayout) {
        resized();
        repaint();
    } else if (needsRepaint) {
        repaint();
    }
}

void ChordPanelContent::rebuildSuggestionBlocks() {
    for (auto& block : suggestionBlocks_)
        removeChildComponent(block.get());
    suggestionBlocks_.clear();

    std::vector<const magda::music::ChordEngine::SuggestionItem*> ordered;
    for (const auto& item : suggestions_)
        if (item.source == "diatonic")
            ordered.push_back(&item);
    diatonicCount_ = ordered.size();
    for (const auto& item : suggestions_)
        if (item.source != "diatonic")
            ordered.push_back(&item);

    for (const auto* item : ordered) {
        auto block = std::make_unique<ChordBlockComponent>(item->chord);
        block->setDegreeLabel(item->degree);
        block->setFunction(chord_function::functionOfDegree(item->degree));
        block->setHighlighted(item->chord.getDisplayName() == currentChord_);
        block->onClicked = [this](const magda::music::Chord& c) { previewChord(c); };
        block->onReleased = [this] { stopPreview(); };
        block->setVisible(suggestionTab_ == SuggestionTab::KS && !browseMode_);
        addChildComponent(block.get());
        suggestionBlocks_.push_back(std::move(block));
    }
}

void ChordPanelContent::rebuildHistoryBlocks() {
    for (auto& block : historyBlocks_)
        removeChildComponent(block.get());
    historyBlocks_.clear();

    if (!chordPlugin_)
        return;

    auto history = chordPlugin_->getRecentChords();
    int start = std::max(0, static_cast<int>(history.size()) - 8);
    for (int i = start; i < static_cast<int>(history.size()); ++i) {
        const auto& chord = history[static_cast<size_t>(i)];
        auto block = std::make_unique<ChordBlockComponent>(chord);
        block->setDegreeLabel(degreeInKey(chord));
        block->onClicked = [this](const magda::music::Chord& c) { previewChord(c); };
        block->onReleased = [this] { stopPreview(); };
        addAndMakeVisible(block.get());
        historyBlocks_.push_back(std::move(block));
    }
}

void ChordPanelContent::rebuildScaleBlocks() {
    for (auto& block : scaleBlocks_)
        removeChildComponent(block.get());
    scaleBlocks_.clear();

    // Prune stale selections (scales that are no longer detected)
    std::set<std::string> validNames;
    for (const auto& scale : detectedScales_)
        validNames.insert(scale.name);
    std::erase_if(selectedScaleNames_, [&](const std::string& name) {
        return validNames.find(name) == validNames.end();
    });

    for (const auto& scale : detectedScales_) {
        auto block = std::make_unique<ScaleBlockComponent>(scale);
        // Restore selection state
        if (selectedScaleNames_.count(scale.name))
            block->setSelected(true);
        addAndMakeVisible(block.get());
        scaleBlocks_.push_back(std::move(block));
    }

    updateScaleFilterPitchClasses();
}

void ChordPanelContent::toggleScaleSelection(ScaleBlockComponent* block) {
    const auto& name = block->getScale().name;
    bool nowSelected = !block->isSelected();
    block->setSelected(nowSelected);

    if (nowSelected)
        selectedScaleNames_.insert(name);
    else
        selectedScaleNames_.erase(name);

    updateScaleFilterPitchClasses();
}

void ChordPanelContent::updateScaleFilterPitchClasses() {
    if (!chordPlugin_)
        return;

    auto& params = chordPlugin_->getSuggestionParams();
    params.explicitScalePitchClasses.clear();

    // Merge pitch classes from all selected scales
    for (const auto& scale : detectedScales_) {
        if (selectedScaleNames_.count(scale.name)) {
            for (int pitch : scale.pitches)
                params.explicitScalePitchClasses.insert(pitch % 12);
        }
    }

    chordPlugin_->refreshSuggestions();
}

void ChordPanelContent::showScalePopup(const magda::music::ScaleWithChords& scale,
                                       juce::Component* source) {
    // If popup is already showing for this scale, toggle it off
    if (activePopup_ && activePopup_->getScaleName() == scale.name) {
        activePopup_.reset();
        return;
    }

    // Dismiss any existing popup before creating a new one
    activePopup_.reset();

    auto popup = std::make_unique<ScaleChordsPopup>(scale);
    auto* popupPtr = popup.get();
    activePopup_ = std::move(popup);
    popupPtr->showAt(this, source->getBounds());
}

// ============================================================================
// Browse Mode
// ============================================================================

void ChordPanelContent::enterBrowseMode() {
    browseMode_ = true;

    // Hide tabs and all tab content — browse replaces the entire suggestions column
    ksTabBtn_->setVisible(false);
    aiTabBtn_->setVisible(false);
    for (auto& block : suggestionBlocks_)
        block->setVisible(false);
    if (aiViewport_)
        aiViewport_->setVisible(false);
    if (aiInputBox_)
        aiInputBox_->setVisible(false);
    if (aiSendBtn_)
        aiSendBtn_->setVisible(false);

    // Hide tab-specific footer controls
    noveltySlider_.setVisible(false);
    extensions_.setVisible(false);
    scaleFilterBtn_->setVisible(false);

    // Create key filter buttons (C, C#, D, ... B)
    if (browseKeyButtons_.empty()) {
        static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
        for (int i = 0; i < 12; ++i) {
            auto btn = std::make_unique<juce::TextButton>(noteNames[i]);
            btn->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
            btn->setClickingTogglesState(true);
            btn->setColour(juce::TextButton::buttonColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
            btn->setColour(juce::TextButton::buttonOnColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
            btn->setColour(juce::TextButton::textColourOffId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            btn->setColour(juce::TextButton::textColourOnId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
            btn->onClick = [this, i]() {
                bool on = browseKeyButtons_[static_cast<size_t>(i)]->getToggleState();
                // Exclusive toggle: deselect others
                if (on) {
                    for (int j = 0; j < 12; ++j) {
                        if (j != i)
                            browseKeyButtons_[static_cast<size_t>(j)]->setToggleState(
                                false, juce::dontSendNotification);
                    }
                    browseKeyFilter_ = i;
                } else {
                    browseKeyFilter_ = -1;
                }
                rebuildBrowseRows();
            };
            addAndMakeVisible(btn.get());
            browseKeyButtons_.push_back(std::move(btn));
        }
    } else {
        for (auto& btn : browseKeyButtons_)
            btn->setVisible(true);
    }

    // Create viewport for scrollable scale list
    if (!browseViewport_) {
        browseViewport_ = std::make_unique<juce::Viewport>();
        browseContainer_ = std::make_unique<juce::Component>();
        browseContainer_->setInterceptsMouseClicks(false, true);
        browseViewport_->setViewedComponent(browseContainer_.get(), false);
        browseViewport_->setScrollBarsShown(true, false);
        addAndMakeVisible(browseViewport_.get());
    } else {
        browseViewport_->setVisible(true);
    }

    browseBtn_->setVisible(false);
    backBtn_->setVisible(true);
    rebuildBrowseRows();
    resized();
    repaint();
}

void ChordPanelContent::exitBrowseMode() {
    browseMode_ = false;

    // Hide browse UI
    for (auto& btn : browseKeyButtons_)
        btn->setVisible(false);
    if (browseViewport_)
        browseViewport_->setVisible(false);
    for (auto& row : browseRows_)
        row->setVisible(false);

    // Restore tabs and current tab's content
    ksTabBtn_->setVisible(true);
    aiTabBtn_->setVisible(true);
    switchToTab(suggestionTab_);  // restores correct tab content + footer

    browseBtn_->setVisible(true);
    backBtn_->setVisible(false);
    resized();
    repaint();
}

void ChordPanelContent::rebuildBrowseRows() {
    // Remove old rows from container
    for (auto& row : browseRows_)
        browseContainer_->removeChildComponent(row.get());
    browseRows_.clear();

    const auto& allScales = magda::music::getAllScalesWithChordsCached();

    for (const auto& scale : allScales) {
        // Apply key filter
        if (browseKeyFilter_ >= 0 && scale.rootNote != browseKeyFilter_)
            continue;

        auto row = std::make_unique<BrowseScaleRowComponent>(scale);
        browseContainer_->addAndMakeVisible(row.get());
        browseRows_.push_back(std::move(row));
    }

    layoutBrowseRows();
}

void ChordPanelContent::layoutBrowseRows() {
    if (!browseContainer_)
        return;

    int containerWidth =
        browseViewport_ ? browseViewport_->getWidth() - browseViewport_->getScrollBarThickness()
                        : 200;
    int y = 0;

    for (auto& row : browseRows_) {
        row->setBounds(0, y, containerWidth, row->getRowHeight());
        y += row->getRowHeight() + 2;
    }

    browseContainer_->setSize(containerWidth, y);
}

// ============================================================================
// AI Chord Suggestions
// ============================================================================

void ChordPanelContent::requestAISuggestions() {
    // Build user prompt from text input, falling back to auto-context
    juce::String userPrompt;
    if (aiInputBox_ && aiInputBox_->getText().trim().isNotEmpty()) {
        userPrompt = aiInputBox_->getText().trim();
        aiInputBox_->clear();
    }

    if (userPrompt.isEmpty() && recentChords_.empty() && detectedKey_.isEmpty())
        return;

    // Cancel any running request
    if (aiThread_ && aiThread_->isThreadRunning()) {
        aiCancelFlag_ = true;
        aiThread_->stopThread(2000);
    }

    aiCancelFlag_ = false;
    aiLoading_ = true;
    aiStreamingText_ = {};
    aiPromptText_ = userPrompt;

    // Grey out existing results while generating
    if (auto* container = dynamic_cast<AIContainerComponent*>(aiContainer_.get())) {
        container->loading = true;
        container->promptText = userPrompt;
        container->streamingText = {};
        if (!aiRows_.empty()) {
            container->greyOut = true;
        }
    }
    if (!aiRows_.empty()) {
        aiGreyOut_ = true;
        for (auto& row : aiRows_)
            for (auto& block : row->blocks)
                block->setAlpha(0.3f);
    }

    if (aiSendBtn_)
        aiSendBtn_->setEnabled(false);
    repaint();

    aiThread_ = std::make_unique<AIRequestThread>(*this, userPrompt);
    aiThread_->startThread();
}

void ChordPanelContent::switchToTab(SuggestionTab tab) {
    suggestionTab_ = tab;

    bool isKS = (tab == SuggestionTab::KS);

    // Toggle K&S content visibility
    for (auto& block : suggestionBlocks_)
        block->setVisible(isKS);

    // Toggle AI content visibility
    if (aiViewport_)
        aiViewport_->setVisible(!isKS);
    if (aiInputBox_)
        aiInputBox_->setVisible(!isKS);
    if (aiSendBtn_)
        aiSendBtn_->setVisible(!isKS);
    aiModelLabel_.setVisible(!isKS);

    if (!isKS) {
        auto cfg = Config::getInstance().getAgentLLMConfig(magda::role::CHORD);
        auto model = cfg.model.empty() ? cfg.provider : cfg.model;
        aiModelLabel_.setText(juce::String(model), juce::dontSendNotification);
    }

    // Toggle K&S footer controls
    noveltySlider_.setVisible(isKS);
    extensions_.setVisible(isKS);
    scaleFilterBtn_->setVisible(isKS);

    ksTabBtn_->setToggleState(isKS, juce::dontSendNotification);
    aiTabBtn_->setToggleState(!isKS, juce::dontSendNotification);
    ksTabBtn_->setActive(isKS);
    aiTabBtn_->setActive(!isKS);

    resized();
    repaint();
}

void ChordPanelContent::rebuildAIProgressionRows() {
    // Clear old rows from container
    for (auto& row : aiRows_) {
        for (auto& block : row->blocks)
            aiContainer_->removeChildComponent(block.get());
    }
    aiRows_.clear();

    const auto& aiProgs =
        chordPlugin_ ? chordPlugin_->getAIProgressions() : std::vector<AIProgression>{};
    for (int progIdx = 0; progIdx < static_cast<int>(aiProgs.size()); ++progIdx) {
        const auto& prog = aiProgs[static_cast<size_t>(progIdx)];
        auto row = std::make_unique<AIProgressionRow>();
        row->progressionIndex = progIdx;

        for (const auto& chord : prog.chords) {
            auto block = std::make_unique<ChordBlockComponent>(chord);
            block->onClicked = [this](const magda::music::Chord& c) { previewChord(c); };
            block->onReleased = [this] { stopPreview(); };
            aiContainer_->addAndMakeVisible(block.get());
            row->blocks.push_back(std::move(block));
        }

        // Export as clip button
        row->exportButton = std::make_unique<magda::SvgButton>("ExportProg", BinaryData::copy_svg,
                                                               BinaryData::copy_svgSize);
        row->exportButton->setTooltip("Click to copy chords, drag to timeline");
        row->exportButton->addMouseListener(this, false);
        row->exportButton->onClick = [this, progIdx = row->progressionIndex]() {
            if (!chordPlugin_)
                return;
            const auto& progs = chordPlugin_->getAIProgressions();
            if (progIdx < 0 || progIdx >= static_cast<int>(progs.size()))
                return;
            const auto& prog = progs[static_cast<size_t>(progIdx)];
            if (prog.chords.empty())
                return;

            constexpr double beatsPerChord = 4.0;
            std::vector<magda::MidiNote> notes;
            for (size_t ci = 0; ci < prog.chords.size(); ++ci) {
                double startBeat = static_cast<double>(ci) * beatsPerChord;
                for (const auto& cn : prog.chords[ci].notes) {
                    magda::MidiNote note;
                    note.noteNumber = std::clamp(cn.noteNumber, 0, 127);
                    note.velocity = std::clamp(cn.velocity, 1, 127);
                    note.startBeat = startBeat;
                    note.lengthBeats = beatsPerChord;
                    notes.push_back(note);
                }
            }

            if (!notes.empty())
                ClipManager::getInstance().setNoteClipboard(std::move(notes));
        };
        aiContainer_->addAndMakeVisible(row->exportButton.get());

        aiRows_.push_back(std::move(row));
    }

    layoutAIProgressionRows();
}

void ChordPanelContent::layoutAIProgressionRows() {
    if (!aiContainer_ || !aiViewport_)
        return;

    int containerWidth = aiViewport_->getWidth() - aiViewport_->getScrollBarThickness();
    int blockWidth = 70;
    int blockHeight = 32;
    int gap = 4;
    int y = 4;

    auto* container = dynamic_cast<AIContainerComponent*>(aiContainer_.get());
    AIContainerPaintData paintData;

    const auto& aiProgsLayout =
        chordPlugin_ ? chordPlugin_->getAIProgressions() : std::vector<AIProgression>{};
    for (size_t i = 0; i < aiRows_.size() && i < aiProgsLayout.size(); ++i) {
        auto& row = aiRows_[i];
        const auto& prog = aiProgsLayout[i];

        AIContainerPaintData::Row paintRow;
        paintRow.name = prog.name;
        paintRow.description = prog.description;

        // Name area with export button
        int exportBtnSize = 16;
        row->nameArea = juce::Rectangle<int>(4, y, containerWidth - 8 - exportBtnSize - 4, 16);
        paintRow.nameArea = row->nameArea;
        if (row->exportButton)
            row->exportButton->setBounds(containerWidth - 4 - exportBtnSize, y, exportBtnSize,
                                         exportBtnSize);
        y += 16;

        // Description area
        if (prog.description.isNotEmpty()) {
            row->descArea = juce::Rectangle<int>(4, y, containerWidth - 8, 14);
            paintRow.descArea = row->descArea;
            y += 16;
        }

        paintData.rows.push_back(std::move(paintRow));

        // Chord blocks
        y += 2;
        int x = 4;
        for (auto& block : row->blocks) {
            if (x + blockWidth > containerWidth - 4) {
                x = 4;
                y += blockHeight + gap;
            }
            block->setBounds(x, y, blockWidth, blockHeight);
            x += blockWidth + gap;
        }
        y += blockHeight + 8;

        // Separator
        y += 4;
    }

    if (container) {
        container->paintData = std::move(paintData);

        // If loading with no rows, account for prompt + "Generating..." height.
        // Read through the container: `paintData` has just been moved from.
        if (container->loading && container->paintData.rows.empty()) {
            int promptHeight = 8;  // top padding
            if (container->promptText.isNotEmpty())
                promptHeight += 24;  // prompt text line
            promptHeight += 28;      // "Generating..." line
            y = juce::jmax(y, promptHeight);
        }
        container->streamingTextBottom = y;

        // If streaming, expand container to fit streaming text
        if (container->streamingText.isNotEmpty()) {
            auto font = FontManager::getInstance().getUIFont(9.5f);
            juce::AttributedString attrStr;
            attrStr.append(container->streamingText, font, ActiveTheme::getSecondaryTextColour());
            attrStr.setWordWrap(juce::AttributedString::WordWrap::byWord);
            juce::TextLayout layout;
            layout.createLayout(attrStr, static_cast<float>(containerWidth - 8));
            y += static_cast<int>(layout.getHeight()) + 8;
        }
    }

    aiContainer_->setSize(containerWidth, y);
    aiContainer_->repaint();
}

void ChordPanelContent::mouseDrag(const juce::MouseEvent& e) {
    if (e.getDistanceFromDragStart() < 5)
        return;

    // Check if drag originates from an AI progression export button
    for (auto& row : aiRows_) {
        if (row->exportButton && e.originalComponent == row->exportButton.get()) {
            startProgressionDrag(row->progressionIndex);
            return;
        }
    }
}

void ChordPanelContent::startProgressionDrag(int progressionIndex) {
    if (!chordPlugin_)
        return;

    const auto& progs = chordPlugin_->getAIProgressions();
    if (progressionIndex < 0 || progressionIndex >= static_cast<int>(progs.size()))
        return;

    const auto& prog = progs[static_cast<size_t>(progressionIndex)];
    if (prog.chords.empty())
        return;

    // Convert chords to MidiNotes — 4 beats per chord (1 bar in 4/4)
    constexpr double beatsPerChord = 4.0;
    std::vector<magda::MidiNote> notes;

    for (size_t chordIdx = 0; chordIdx < prog.chords.size(); ++chordIdx) {
        const auto& chord = prog.chords[chordIdx];
        double startBeat = static_cast<double>(chordIdx) * beatsPerChord;

        for (const auto& cn : chord.notes) {
            magda::MidiNote note;
            note.noteNumber = std::clamp(cn.noteNumber, 0, 127);
            note.velocity = std::clamp(cn.velocity, 1, 127);
            note.startBeat = startBeat;
            note.lengthBeats = beatsPerChord;
            notes.push_back(note);
        }
    }

    if (notes.empty())
        return;

    // Build chord markers for pre-populated annotations
    std::vector<daw::ChordMarker> markers;
    for (size_t chordIdx = 0; chordIdx < prog.chords.size(); ++chordIdx) {
        double startBeat = static_cast<double>(chordIdx) * beatsPerChord;
        markers.push_back({startBeat, beatsPerChord, prog.chords[chordIdx].getDisplayName()});
    }

    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo <= 0.0)
        tempo = 120.0;

    auto clipName = prog.name.isNotEmpty() ? prog.name : prog.description;
    if (clipName.isEmpty())
        clipName = "chord-progression";
    auto tempFile = daw::MidiFileWriter::writeToTempFile(notes, tempo, clipName, markers);
    if (!tempFile.existsAsFile())
        return;

    // Ghost the chord blocks during drag
    std::vector<ChordBlockComponent*> draggedBlocks;
    for (auto& row : aiRows_) {
        if (row->progressionIndex == progressionIndex) {
            for (auto& block : row->blocks) {
                block->setAlpha(0.4f);
                draggedBlocks.push_back(block.get());
            }
            if (row->exportButton)
                row->exportButton->setAlpha(0.4f);
            break;
        }
    }

    magda::dnd::startFilesDrag(this, juce::StringArray{tempFile.getFullPathName()});

    for (auto* block : draggedBlocks)
        block->setAlpha(1.0f);
    for (auto& row : aiRows_) {
        if (row->progressionIndex == progressionIndex && row->exportButton) {
            row->exportButton->setAlpha(1.0f);
            break;
        }
    }
}

void ChordPanelContent::AIRequestThread::run() {
    {
        // ChordAgent owns the request/prompt/provider/parsing domain logic. This
        // thread remains a thin UI adapter: snapshot state, relay stream tokens,
        // then paint the structured result on the message thread.
        magda::ChordAgent::Input input;
        input.userPrompt = userPrompt_;
        input.chordTrackProgression = magda::ChordProgressionContext::summary();
        input.detectedKey = owner_.detectedKey_;
        input.recentChords = owner_.recentChords_;
        for (const auto& scale : owner_.detectedScales_)
            input.detectedScales.emplace_back(scale.name);

        auto safeThis = juce::Component::SafePointer<ChordPanelContent>(&owner_);
        const auto finish = [safeThis](magda::ChordAgent::Result result) {
            juce::MessageManager::callAsync([safeThis, result = std::move(result)]() mutable {
                if (!safeThis)
                    return;

                safeThis->aiLoading_ = false;
                safeThis->aiGreyOut_ = false;
                safeThis->aiStreamingText_ =
                    result.hasError ? "Error: " + result.error : juce::String();
                if (auto* container =
                        dynamic_cast<AIContainerComponent*>(safeThis->aiContainer_.get())) {
                    container->greyOut = false;
                    container->loading = false;
                    container->promptText = {};
                    container->streamingText = safeThis->aiStreamingText_;
                }
                for (auto& row : safeThis->aiRows_)
                    for (auto& block : row->blocks)
                        block->setAlpha(1.0f);
                if (safeThis->aiSendBtn_)
                    safeThis->aiSendBtn_->setEnabled(true);

                if (!result.hasError && !result.progressions.empty() && safeThis->chordPlugin_) {
                    safeThis->chordPlugin_->getAIProgressions() = std::move(result.progressions);
                    safeThis->rebuildAIProgressionRows();
                }
                safeThis->resized();
                safeThis->repaint();
            });
        };

        magda::ChordAgent agent;
        auto result = agent.generate(
            input,
            [safeThis](const juce::String& token) {
                juce::MessageManager::callAsync([safeThis, token]() {
                    if (!safeThis)
                        return;
                    safeThis->aiStreamingText_ += token;
                    if (auto* container =
                            dynamic_cast<AIContainerComponent*>(safeThis->aiContainer_.get())) {
                        container->streamingText = safeThis->aiStreamingText_;
                        safeThis->layoutAIProgressionRows();
                        if (safeThis->aiViewport_)
                            safeThis->aiViewport_->setViewPosition(
                                0, std::max(0, container->getHeight() -
                                                   safeThis->aiViewport_->getHeight()));
                    }
                });
                return true;
            },
            [this] { return threadShouldExit() || owner_.aiCancelFlag_; });
        finish(std::move(result));
    }
    return;

    // Build context from chord history and detected key
    juce::String context;

    // The chord-track progression is the song's authored harmony - always part
    // of the engine's context.
    if (const auto progression = magda::ChordProgressionContext::summary();
        progression.isNotEmpty())
        context += "Chord-track progression: " + progression + "\n";

    if (owner_.detectedKey_.isNotEmpty())
        context += "Detected key: " + owner_.detectedKey_ + "\n";

    if (!owner_.recentChords_.empty()) {
        context += "Recent chord history: ";
        for (size_t i = 0; i < owner_.recentChords_.size(); ++i) {
            if (i > 0)
                context += ", ";
            context += owner_.recentChords_[i];
        }
        context += "\n";
    }

    if (!owner_.detectedScales_.empty()) {
        context += "Detected scales: ";
        for (size_t i = 0; i < std::min(owner_.detectedScales_.size(), static_cast<size_t>(3));
             ++i) {
            if (i > 0)
                context += ", ";
            context += juce::String(owner_.detectedScales_[i].name);
        }
        context += "\n";
    }

    juce::String prompt;
    if (userPrompt_.isNotEmpty()) {
        prompt = userPrompt_ + "\n\n";
        if (context.isNotEmpty())
            prompt += "Musical context:\n" + context + "\n";
    } else {
        prompt = "Based on this musical context, suggest 3-4 chord progressions as "
                 "continuations or variations.\n\n" +
                 context + "\n";
    }
    // Create LLM client early so we know which provider we're using
    auto agentConfig = magda::Config::getInstance().getAgentLLMConfig(magda::role::MUSIC);
    bool isLocal = agentConfig.provider == magda::provider::LLAMA_LOCAL;

    if (isLocal) {
        // Local model: skip name/description to maximize chord output
        prompt += "Generate chord progressions. Each progression() block should have 4-8 chords "
                  "via .add_chord() calls.\n"
                  "Use beat values starting at 0, incrementing by the chord length.\n"
                  "Use appropriate octaves (root around C3-C4).\n"
                  "Quality names: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, "
                  "min9, sus2, sus4, add9, madd9, 6, min6, power.";
    } else {
        prompt += "Generate DSL using the chord_progressions tool. Each progression() block should "
                  "have a name, a short description (under 60 chars), and 4-8 chords via "
                  ".add_chord() calls.\n"
                  "Use beat values starting at 0, incrementing by the chord length.\n"
                  "Use appropriate octaves (root around C3-C4). Use inversions (inversion=0/1/2) "
                  "to match the voicings in the chord history and create smooth voice leading.\n"
                  "Quality names: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, "
                  "min9, sus2, sus4, add9, madd9, 6, min6, power.";
    }

    // Chord progression grammar (Lark format) for grammar-constrained output
    static const char* chordProgressionGrammar = R"GRAMMAR(
start: progression+

progression: "progression(" params ")" chord_chain

chord_chain: chord+

chord: ".add_chord(" params ")"

params: param ("," param)*

param: IDENTIFIER "=" value

value: STRING
     | NUMBER
     | IDENTIFIER

STRING: "\"" /[^"]*/ "\""
NUMBER: /-?[0-9]+(\.[0-9]+)?/
IDENTIFIER: /[a-zA-Z_#][a-zA-Z0-9_#]*/

%import common.WS
%ignore WS
)GRAMMAR";

    static const char* chordToolDescription =
        "Generates chord progression suggestions using MAGDA DSL.\n\n"
        "Format: progression(name=\"Name\", description=\"Why it works\") followed by "
        ".add_chord(root=<note>, quality=<quality>, beat=<beat>, length=<beats>, "
        "inversion=<0|1|2>) chains.\n\n"
        "Example:\n"
        "progression(name=\"Classic Pop\", description=\"Timeless I-V-vi-IV cadence\")"
        ".add_chord(root=C4, quality=major, beat=0, length=1)"
        ".add_chord(root=G3, quality=major, beat=1, length=1, inversion=1)"
        ".add_chord(root=A3, quality=minor, beat=2, length=1)"
        ".add_chord(root=F3, quality=major, beat=3, length=1, inversion=2)\n"
        "progression(name=\"Jazz ii-V-I\", description=\"Smooth jazz resolution\")"
        ".add_chord(root=D3, quality=min7, beat=0, length=1)"
        ".add_chord(root=G3, quality=dom7, beat=1, length=1, inversion=1)"
        ".add_chord(root=C4, quality=maj7, beat=2, length=2)\n\n"
        "Qualities: major, minor, dim, aug, dom7, maj7, min7, dim7, dom9, maj9, min9, "
        "sus2, sus4, add9, madd9, 6, min6, power\n"
        "Inversion: 0=root position (default), 1=first inversion, 2=second inversion. "
        "Use inversions to create smooth voice leading matching the played voicings.\n"
        "Notes: C3-B5 range (e.g. C4, F#3, Bb4)";

    static const char* chordToolDescriptionLocal =
        "Generate chord progressions using MAGDA DSL. No prose.\n\n"
        "Format: progression() followed by .add_chord() chains. 4-8 chords per progression.\n\n"
        "Example:\n"
        "progression()"
        ".add_chord(root=C4, quality=major, beat=0, length=1)"
        ".add_chord(root=G3, quality=major, beat=1, length=1)"
        ".add_chord(root=A3, quality=minor, beat=2, length=1)"
        ".add_chord(root=F3, quality=major, beat=3, length=1)\n"
        "progression()"
        ".add_chord(root=D3, quality=min7, beat=0, length=1)"
        ".add_chord(root=G3, quality=dom7, beat=1, length=1)"
        ".add_chord(root=C4, quality=maj7, beat=2, length=2)";

    // CFG grammar support is currently available only on the GPT-5 Responses path.
    bool cfg = magda::supportsOpenAICFG(agentConfig);

    std::unique_ptr<llm::LLMClient> client;
    if (cfg) {
        auto pc = magda::toLLMProviderConfig(agentConfig, "music");
        client = llm::LLMClientFactory::create(pc);
    } else {
        client = magda::createLLMClient(agentConfig, "music");
    }

    const char* systemPrompt = isLocal ? chordToolDescriptionLocal : chordToolDescription;

    llm::Request request;
    request.systemPrompt = juce::String(systemPrompt);
    request.userMessage = prompt;
    request.temperature = 0.1f;
    if (cfg) {
        request.grammar = juce::String(chordProgressionGrammar);
        request.grammarToolName = "chord_progression";
        request.grammarToolDescription = juce::String(chordToolDescription);
    }

    // Stream tokens to UI — post each token to the message thread for live display
    auto safeThis = juce::Component::SafePointer<ChordPanelContent>(&owner_);

    // CFG (OpenAI Responses API) doesn't support streaming — use sync
    bool isCfg = cfg;
    llm::Response response;

    if (isCfg) {
        response = client->sendRequest(request);
    } else {
        response = client->sendStreamingRequest(request, [&](const juce::String& token) {
            if (threadShouldExit() || owner_.aiCancelFlag_)
                return false;

            // Post token to UI thread for live display
            const auto& tokenCopy = token;
            juce::MessageManager::callAsync([safeThis, tokenCopy]() {
                if (!safeThis)
                    return;
                safeThis->aiStreamingText_ += tokenCopy;
                if (auto* container =
                        dynamic_cast<AIContainerComponent*>(safeThis->aiContainer_.get())) {
                    container->streamingText = safeThis->aiStreamingText_;
                    safeThis->layoutAIProgressionRows();
                    // Scroll to bottom to follow streaming text
                    if (safeThis->aiViewport_)
                        safeThis->aiViewport_->setViewPosition(
                            0, std::max(0, container->getHeight() -
                                               safeThis->aiViewport_->getHeight()));
                }
            });
            return true;
        });
    }

    if (threadShouldExit() || owner_.aiCancelFlag_)
        return;

    auto dsl = response.text.trim();

    if (!response.success || dsl.isEmpty()) {
        DBG("AI Suggest: No DSL generated - " + response.error);
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis)
                return;
            safeThis->aiLoading_ = false;
            safeThis->aiGreyOut_ = false;
            safeThis->aiStreamingText_ = {};
            if (auto* container =
                    dynamic_cast<AIContainerComponent*>(safeThis->aiContainer_.get())) {
                container->greyOut = false;
                container->streamingText = {};
                container->loading = false;
                container->promptText = {};
            }
            // Restore block alpha
            for (auto& row : safeThis->aiRows_)
                for (auto& block : row->blocks)
                    block->setAlpha(1.0f);
            if (safeThis->aiSendBtn_)
                safeThis->aiSendBtn_->setEnabled(true);
            safeThis->repaint();
        });
        return;
    }

    DBG("AI Suggest DSL: " + dsl.substring(0, 500));

    auto progressions = owner_.parseAIResponse(dsl);

    // Post result back to UI thread — store and display inline
    juce::MessageManager::callAsync([safeThis, progressions = std::move(progressions)]() mutable {
        if (!safeThis)
            return;

        safeThis->aiLoading_ = false;
        safeThis->aiGreyOut_ = false;
        safeThis->aiStreamingText_ = {};

        if (auto* container = dynamic_cast<AIContainerComponent*>(safeThis->aiContainer_.get())) {
            container->greyOut = false;
            container->streamingText = {};
            container->loading = false;
            container->promptText = {};
        }

        if (safeThis->aiSendBtn_)
            safeThis->aiSendBtn_->setEnabled(true);

        if (!progressions.empty() && safeThis->chordPlugin_) {
            safeThis->chordPlugin_->getAIProgressions() = std::move(progressions);
            safeThis->rebuildAIProgressionRows();
        }

        safeThis->resized();
        safeThis->repaint();
    });
}

std::vector<AIProgression> ChordPanelContent::parseAIResponse(const juce::String& dsl) {
    std::vector<AIProgression> result;
    if (dsl.isEmpty())
        return result;

    auto& engine = magda::music::ChordEngine::getInstance();

    // Split DSL into progression blocks
    // Format: progression(name="...", description="...").add_chord(root=C4, quality=major, ...)...
    int pos = 0;
    while (pos < dsl.length()) {
        int progStart = dsl.indexOf(pos, "progression(");
        if (progStart < 0)
            break;

        // Find the end of this progression (next "progression(" or end of string)
        int nextProg = dsl.indexOf(progStart + 12, "progression(");
        juce::String progBlock =
            (nextProg >= 0) ? dsl.substring(progStart, nextProg) : dsl.substring(progStart);

        AIProgression prog;

        // Extract name="..." from progression params
        int nameStart = progBlock.indexOf("name=\"");
        if (nameStart >= 0) {
            nameStart += 6;
            int nameEnd = progBlock.indexOf(nameStart, "\"");
            if (nameEnd >= 0)
                prog.name = progBlock.substring(nameStart, nameEnd);
        }

        // Extract description="..." from progression params
        int descStart = progBlock.indexOf("description=\"");
        if (descStart >= 0) {
            descStart += 13;
            int descEnd = progBlock.indexOf(descStart, "\"");
            if (descEnd >= 0)
                prog.description = progBlock.substring(descStart, descEnd);
        }

        // Extract all .add_chord(...) calls
        int chordPos = 0;
        while (chordPos < progBlock.length()) {
            int chordStart = progBlock.indexOf(chordPos, ".add_chord(");
            if (chordStart < 0)
                break;

            int paramsStart = chordStart + 11;
            int paramsEnd = progBlock.indexOf(paramsStart, ")");
            if (paramsEnd < 0)
                break;

            auto paramsStr = progBlock.substring(paramsStart, paramsEnd);

            // Parse root=, quality=, inversion= from params
            juce::String rootStr, qualityStr;
            int octave = 4;
            int inversion = 0;

            int rootIdx = paramsStr.indexOf("root=");
            if (rootIdx >= 0) {
                rootIdx += 5;
                int rootEnd = paramsStr.indexOf(rootIdx, ",");
                rootStr = (rootEnd >= 0) ? paramsStr.substring(rootIdx, rootEnd).trim()
                                         : paramsStr.substring(rootIdx).trim();

                // Extract octave from note name (e.g. "C4" -> octave=4)
                if (rootStr.isNotEmpty()) {
                    auto lastChar = rootStr[rootStr.length() - 1];
                    if (lastChar >= '0' && lastChar <= '9')
                        octave = lastChar - '0';
                }
            }

            int qualIdx = paramsStr.indexOf("quality=");
            if (qualIdx >= 0) {
                qualIdx += 8;
                int qualEnd = paramsStr.indexOf(qualIdx, ",");
                qualityStr = (qualEnd >= 0) ? paramsStr.substring(qualIdx, qualEnd).trim()
                                            : paramsStr.substring(qualIdx).trim();
            }

            int invIdx = paramsStr.indexOf("inversion=");
            if (invIdx >= 0) {
                invIdx += 10;
                int invEnd = paramsStr.indexOf(invIdx, ",");
                auto invStr = (invEnd >= 0) ? paramsStr.substring(invIdx, invEnd).trim()
                                            : paramsStr.substring(invIdx).trim();
                inversion = invStr.getIntValue();
            }

            // Build chord from parsed params
            if (rootStr.isNotEmpty() && qualityStr.isNotEmpty()) {
                // Strip octave digit from root for chord name
                juce::String rootName = rootStr;
                if (rootName.isNotEmpty()) {
                    auto last = rootName[rootName.length() - 1];
                    if (last >= '0' && last <= '9')
                        rootName = rootName.dropLastCharacters(1);
                }

                auto chordName = rootName + " " + qualityStr;
                auto spec = engine.parseChordName(chordName);
                auto chord =
                    (inversion > 0)
                        ? engine.buildChordInversion(spec.root, spec.quality, inversion, octave)
                        : engine.buildChordInRootPosition(spec.root, spec.quality, octave);
                magda::music::ChordEngine::finalizeChord(chord);
                if (!chord.notes.empty())
                    prog.chords.push_back(chord);
            }

            chordPos = paramsEnd + 1;
        }

        if (!prog.chords.empty())
            result.push_back(std::move(prog));

        pos = (nextProg >= 0) ? nextProg : dsl.length();
    }

    return result;
}

void ChordPanelContent::setupFooterControls() {
    using node_header::applyDeviceIconStyle;
    using node_header::DeviceIcon;
    const juce::Colour glyphKey(0xFFB3B3B3);

    noveltySlider_.setRange(0.0, 1.0, 0.01);
    noveltySlider_.setValue(0.3, juce::dontSendNotification);
    noveltySlider_.setDoubleClickReturnValue(true, 0.3);
    noveltySlider_.setColour(juce::Slider::backgroundColourId,
                             ActiveTheme::getColour(ActiveTheme::DEVICE_SLIDER_TRACK));
    noveltySlider_.setColour(juce::Slider::trackColourId,
                             ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    noveltySlider_.setColour(juce::Slider::thumbColourId,
                             ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    noveltySlider_.setTooltip(
        "Balance between safe diatonic (0%) and adventurous non-diatonic (100%) suggestions");
    noveltySlider_.onValueChange = [this]() {
        repaint(noveltyValue_);
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().novelty =
                static_cast<float>(noveltySlider_.getValue());
            chordPlugin_->refreshSuggestions();
        }
    };
    addAndMakeVisible(noveltySlider_);

    extensions_.setOptions({"7th", "9th", "11th", "13th", "Alt"});
    extensions_.setOn(0, true);
    extensions_.setTooltip("Extensions the suggestions may use; Alt adds borrowed chords and "
                           "tritone substitutes");
    extensions_.onToggle = [this](int index, bool on) {
        if (!chordPlugin_)
            return;
        auto& params = chordPlugin_->getSuggestionParams();
        bool* flags[] = {&params.add7ths, &params.add9ths, &params.add11ths, &params.add13ths,
                         &params.addAlterations};
        *flags[index] = on;
        chordPlugin_->refreshSuggestions();
    };
    addAndMakeVisible(extensions_);

    // Drives the shared NotationSettings, so it flips the chord-track lane's names too.
    notationBtn_ =
        std::make_unique<juce::TextButton>(magda::music::NotationSettings::getInstance().label());
    notationBtn_->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    notationBtn_->setColour(juce::TextButton::buttonColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    notationBtn_->setColour(juce::TextButton::textColourOffId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    notationBtn_->setTooltip("Note notation: C / Do (solfege) / both");
    notationBtn_->onClick = [this]() {
        magda::music::NotationSettings::getInstance().cycle();
        notationBtn_->setButtonText(magda::music::NotationSettings::getInstance().label());
        resized();
        repaint();
    };
    addAndMakeVisible(notationBtn_.get());

    scaleFilterBtn_ = std::make_unique<magda::SvgButton>("ScaleFilter", BinaryData::funnel_svg,
                                                         BinaryData::funnel_svgSize);
    applyDeviceIconStyle(*scaleFilterBtn_, DeviceIcon::Toggle, glyphKey);
    scaleFilterBtn_->setToggleState(true, juce::dontSendNotification);
    scaleFilterBtn_->setActive(true);
    scaleFilterBtn_->setTooltip("Filter suggestions by detected scales");
    scaleFilterBtn_->onClick = [this]() {
        const bool on = scaleFilterBtn_->getToggleState();
        scaleFilterBtn_->setActive(on);
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().useScaleFiltering = on;
            chordPlugin_->refreshSuggestions();
        }
    };
    addAndMakeVisible(scaleFilterBtn_.get());

    browseBtn_ = std::make_unique<magda::SvgButton>("Browse", BinaryData::browser_svg,
                                                    BinaryData::browser_svgSize);
    applyDeviceIconStyle(*browseBtn_, DeviceIcon::Action, glyphKey);
    browseBtn_->setTooltip("Browse scales");
    browseBtn_->onClick = [this]() { enterBrowseMode(); };
    addAndMakeVisible(browseBtn_.get());

    backBtn_ = std::make_unique<magda::SvgButton>("Back", BinaryData::chevron_left_svg,
                                                  BinaryData::chevron_left_svgSize);
    applyDeviceIconStyle(*backBtn_, DeviceIcon::Action, glyphKey);
    backBtn_->setTooltip("Back to suggestions");
    backBtn_->onClick = [this]() { exitBrowseMode(); };
    backBtn_->setVisible(false);
    addAndMakeVisible(backBtn_.get());

    clearHistoryBtn_ = std::make_unique<magda::SvgButton>("ClearHistory", BinaryData::delete_svg,
                                                          BinaryData::delete_svgSize);
    applyDeviceIconStyle(*clearHistoryBtn_, DeviceIcon::Action, glyphKey, ActiveTheme::DEVICE_BLUE,
                         22.0f);
    clearHistoryBtn_->setTooltip("Clear chord history and reset detection");
    clearHistoryBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->clearHistory();
    };
    addAndMakeVisible(clearHistoryBtn_.get());

    ksTabBtn_ = std::make_unique<magda::SvgButton>("KSTab", BinaryData::psychology_svg,
                                                   BinaryData::psychology_svgSize);
    applyDeviceIconStyle(*ksTabBtn_, DeviceIcon::Toggle, juce::Colour(0xFFE3E3E3));
    ksTabBtn_->setRadioGroupId(1001);
    ksTabBtn_->setToggleState(true, juce::dontSendNotification);
    ksTabBtn_->setActive(true);
    ksTabBtn_->setTooltip("Krumhansl-Schmuckler profile suggestions");
    ksTabBtn_->onClick = [this]() { switchToTab(SuggestionTab::KS); };
    addAndMakeVisible(ksTabBtn_.get());

    aiTabBtn_ =
        std::make_unique<magda::SvgButton>("AITab", BinaryData::ai_svg, BinaryData::ai_svgSize);
    applyDeviceIconStyle(*aiTabBtn_, DeviceIcon::Toggle, glyphKey, ActiveTheme::MIDI_KEY_ON);
    aiTabBtn_->setRadioGroupId(1001);
    aiTabBtn_->setTooltip("AI chord progression suggestions");
    aiTabBtn_->onClick = [this]() { switchToTab(SuggestionTab::AI); };
    addAndMakeVisible(aiTabBtn_.get());

    aiModelLabel_.setFont(FontManager::getInstance().getMonoFont(10.5f));
    aiModelLabel_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    aiModelLabel_.setJustificationType(juce::Justification::centredLeft);
    aiModelLabel_.setVisible(false);
    addAndMakeVisible(aiModelLabel_);

    aiViewport_ = std::make_unique<juce::Viewport>();
    aiContainer_ = std::make_unique<AIContainerComponent>();
    aiViewport_->setViewedComponent(aiContainer_.get(), false);
    aiViewport_->setScrollBarsShown(true, false);
    aiViewport_->setVisible(false);
    addAndMakeVisible(aiViewport_.get());

    aiInputBox_ = std::make_unique<juce::TextEditor>();
    aiInputBox_->setMultiLine(false);
    aiInputBox_->setReturnKeyStartsNewLine(false);
    aiInputBox_->setFont(FontManager::getInstance().getUIFont(12.0f));
    aiInputBox_->setIndents(10, 7);
    aiInputBox_->setColour(juce::TextEditor::backgroundColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    aiInputBox_->setColour(juce::TextEditor::textColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    aiInputBox_->setColour(juce::TextEditor::outlineColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    aiInputBox_->setColour(juce::TextEditor::focusedOutlineColourId,
                           ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    aiInputBox_->setTextToShowWhenEmpty("Describe a chord progression...",
                                        ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    aiInputBox_->onReturnKey = [this]() { requestAISuggestions(); };
    aiInputBox_->setVisible(false);
    addAndMakeVisible(aiInputBox_.get());

    aiSendBtn_ = std::make_unique<magda::SvgButton>("AISend", BinaryData::send_svg,
                                                    BinaryData::send_svgSize);
    applyDeviceIconStyle(*aiSendBtn_, DeviceIcon::Action, glyphKey);
    aiSendBtn_->onClick = [this]() { requestAISuggestions(); };
    aiSendBtn_->setVisible(false);
    addAndMakeVisible(aiSendBtn_.get());
}

void ChordPanelContent::syncFooterFromParams() {
    if (!chordPlugin_)
        return;

    const auto& params = chordPlugin_->getSuggestionParams();
    noveltySlider_.setValue(params.novelty, juce::dontSendNotification);
    const bool flags[] = {params.add7ths, params.add9ths, params.add11ths, params.add13ths,
                          params.addAlterations};
    for (int i = 0; i < 5; ++i)
        extensions_.setOn(i, flags[i]);
    scaleFilterBtn_->setToggleState(params.useScaleFiltering, juce::dontSendNotification);
    scaleFilterBtn_->setActive(params.useScaleFiltering);
    repaint();
}

juce::String ChordPanelContent::degreeInKey(const magda::music::Chord& chord) const {
    return key_.has_value() ? chord_function::romanNumeral(chord.root, chord.quality, *key_)
                            : juce::String();
}

void ChordPanelContent::paintSectionLabel(juce::Graphics& g, juce::Rectangle<int> area,
                                          const juce::String& label, const juce::String& note) {
    if (area.isEmpty())
        return;
    const auto font = FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.12f);
    g.setFont(font);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    const auto text = label.toUpperCase();
    g.drawText(text, area, juce::Justification::centredLeft, false);
    if (note.isEmpty())
        return;
    const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, text));
    g.setFont(FontManager::getInstance().getMonoFont(10.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.drawText(note, area.withTrimmedLeft(width + 10), juce::Justification::centredLeft, true);
}

void ChordPanelContent::paintCurrentChord(juce::Graphics& g, juce::Rectangle<int> box) const {
    static constexpr std::array<const char*, 12> kNoteNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                            "F#", "G",  "G#", "A",  "A#", "B"};
    auto& fonts = FontManager::getInstance();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(box.toFloat(), 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 6.0f, 1.0f);

    auto inner = box.reduced(16, 10);
    auto title = inEditorSidebar_ ? inner : inner.removeFromTop(34);
    const auto& notation = magda::music::NotationSettings::getInstance();
    if (currentChord_.isEmpty()) {
        g.setFont(fonts.getUIFont(15.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.drawFittedText("Play a chord...", title, juce::Justification::centredLeft, 2, 0.8f);
    } else {
        const auto nameFont = fonts.getUIFontMedium(26.0f);
        const auto name = notation.format(currentChord_);
        // The voicing's note names sit at the title's right when the name leaves them room.
        juce::StringArray names;
        for (const auto& note : currentChordInfo_.notes)
            names.add(notation.formatRoot(kNoteNames[static_cast<size_t>(note.noteNumber % 12)]));
        const auto notesFont = fonts.getMonoFont(11.0f).withExtraKerningFactor(0.1f);
        const auto notes = names.joinIntoString(" ");
        const float needed = juce::GlyphArrangement::getStringWidth(nameFont, name) +
                             juce::GlyphArrangement::getStringWidth(notesFont, notes) + 64.0f;
        if (needed <= static_cast<float>(title.getWidth())) {
            g.setFont(notesFont);
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
            g.drawText(notes, title, juce::Justification::centredRight, false);
        }

        g.setFont(nameFont);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
        g.drawText(name, title, juce::Justification::centredLeft, true);

        if (const auto degree = degreeInKey(currentChordInfo_); degree.isNotEmpty()) {
            const auto badgeFont = fonts.getMonoFont(12.0f);
            const int nameWidth =
                juce::roundToInt(juce::GlyphArrangement::getStringWidth(nameFont, name));
            const int badgeWidth =
                juce::roundToInt(juce::GlyphArrangement::getStringWidth(badgeFont, degree)) + 14;
            const auto badge = juce::Rectangle<int>(title.getX() + nameWidth + 12,
                                                    title.getCentreY() - 9, badgeWidth, 24);
            if (badge.getRight() <= title.getRight()) {
                const auto blue = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
                g.setColour(blue.withAlpha(0.16f));
                g.fillRoundedRectangle(badge.toFloat(), 4.0f);
                g.setColour(blue);
                g.setFont(badgeFont);
                g.drawText(degree, badge, juce::Justification::centred, false);
            }
        }
    }

    if (inEditorSidebar_)
        return;

    // Two octaves from the C under the lowest note; the voicing lights its keys.
    inner.removeFromTop(6);
    const auto keys = inner.withHeight(juce::jmin(inner.getHeight(), 70)).toFloat();
    if (keys.getHeight() < 16.0f)
        return;

    std::set<int> lit;
    int lowest = 60;
    if (currentChord_.isNotEmpty() && !currentChordInfo_.notes.empty()) {
        lowest = 127;
        for (const auto& note : currentChordInfo_.notes)
            lowest = juce::jmin(lowest, note.noteNumber);
    }
    const int start = (lowest / 12) * 12;
    if (currentChord_.isNotEmpty())
        for (const auto& note : currentChordInfo_.notes)
            lit.insert(note.noteNumber >= start + 24 ? start + 12 + note.noteNumber % 12
                                                     : note.noteNumber);

    static constexpr int kWhite[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kBlack[] = {1, 3, -1, 6, 8, 10, -1};
    const float whiteWidth = keys.getWidth() / 14.0f;
    const auto blue = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    for (int i = 0; i < 14; ++i) {
        const int note = start + (i / 7) * 12 + kWhite[i % 7];
        const auto key = juce::Rectangle<float>(keys.getX() + static_cast<float>(i) * whiteWidth,
                                                keys.getY(), whiteWidth - 1.0f, keys.getHeight());
        g.setColour(lit.contains(note) ? blue : ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
        g.fillRoundedRectangle(key, 2.0f);
    }
    for (int i = 0; i < 14; ++i) {
        if (kBlack[i % 7] < 0)
            continue;
        const int note = start + (i / 7) * 12 + kBlack[i % 7];
        const auto key =
            juce::Rectangle<float>(keys.getX() + (static_cast<float>(i) + 0.68f) * whiteWidth,
                                   keys.getY(), whiteWidth * 0.62f, keys.getHeight() * 0.6f);
        g.setColour(lit.contains(note) ? blue.darker(0.3f)
                                       : ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
        g.fillRoundedRectangle(key, 1.5f);
    }
}

void ChordPanelContent::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    g.fillAll(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM);
    const auto dim2 = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);

    if (!chordPlugin_) {
        g.setColour(dim2);
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText("No chord engine", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    for (const auto* column : {&suggestionsCol_, &keyScaleCol_})
        if (column->getWidth() > 0)
            g.fillRect(column->getX(), column->getY(), 1, column->getHeight());
    g.fillRect(0, footer_.getY(), getWidth(), 1);
    if (!footerDivider_.isEmpty())
        g.fillRect(footerDivider_);

    // Sounding chord and history.
    paintSectionLabel(g, chordHeader_, "Chord");
    if (!chordBox_.isEmpty())
        paintCurrentChord(g, chordBox_);
    if (!statusLine_.isEmpty()) {
        const bool live = currentChord_.isNotEmpty();
        g.setColour(
            ActiveTheme::getColour(live ? ActiveTheme::DEVICE_GREEN : ActiveTheme::DEVICE_DIM2));
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f)
                          .withCentre({static_cast<float>(statusLine_.getX()) + 4.0f,
                                       statusLine_.toFloat().getCentreY()}));
        g.setColour(dim);
        g.setFont(fonts.getMonoFont(10.5f));
        g.drawText(
            juce::String(juce::CharPointer_UTF8("MIDI in \xc2\xb7 play a chord or pick one")),
            statusLine_.withTrimmedLeft(16), juce::Justification::centredLeft, true);
    }
    paintSectionLabel(g, historyHeader_, "History");
    g.setColour(dim2);
    g.setFont(fonts.getMonoFont(11.0f));
    for (const auto& arrow : historyArrows_)
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92")),
                   juce::Rectangle<int>(18, HISTORY_HEIGHT).withPosition(arrow),
                   juce::Justification::centred, false);

    // Suggestions.
    if (!tabNote_.isEmpty() && !recentChords_.empty()) {
        g.setColour(dim2);
        g.setFont(fonts.getMonoFont(10.5f));
        g.drawText("after " +
                       magda::music::NotationSettings::getInstance().format(recentChords_.back()),
                   tabNote_, juce::Justification::centredLeft, true);
    }
    if (!legend_.isEmpty()) {
        using chord_function::HarmonicFunction;
        auto row = legend_;
        g.setFont(fonts.getMonoFont(10.5f));
        for (const auto& [function, label] :
             {std::pair{HarmonicFunction::Dominant, "dominant"},
              std::pair{HarmonicFunction::PreDominant, "pre-dominant"},
              std::pair{HarmonicFunction::Tonic, "tonic"}}) {
            const int width =
                juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), label));
            auto item = row.removeFromRight(width + 14);
            row.removeFromRight(12);
            g.setColour(chord_function::functionColour(function));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f)
                              .withCentre({static_cast<float>(item.getX()) + 3.0f,
                                           item.toFloat().getCentreY()}));
            g.setColour(dim);
            g.drawText(label, item.withTrimmedLeft(12), juce::Justification::centredLeft, false);
        }
    }
    if (browseMode_) {
        paintSectionLabel(g, diatonicHeader_, "Browse scales");
    } else if (suggestionTab_ == SuggestionTab::KS) {
        paintSectionLabel(g, diatonicHeader_, "Diatonic", "in key");
        paintSectionLabel(g, beyondHeader_, "Beyond the key", "secondary, borrowed & substitutes");
        if (suggestionBlocks_.empty()) {
            g.setColour(dim2);
            g.setFont(fonts.getMonoFont(11.0f));
            g.drawText("Play a chord to get suggestions",
                       suggestionsCol_.reduced(PADDING).withTrimmedTop(44).withHeight(20),
                       juce::Justification::centredLeft, true);
        }
    } else if (!aiLoading_ && chordPlugin_->getAIProgressions().empty()) {
        g.setColour(dim2);
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText("Describe a progression below and press send",
                   suggestionsCol_.reduced(PADDING).withTrimmedTop(44).withHeight(20),
                   juce::Justification::centredLeft, true);
    }

    // Key and scales.
    paintSectionLabel(g, keyHeader_, "Key");
    if (!keyBox_.isEmpty()) {
        for (const auto& box : {keyBox_, browseBox_}) {
            if (box.isEmpty())
                continue;
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
            g.fillRoundedRectangle(box.toFloat(), 6.0f);
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
            g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 6.0f, 1.0f);
        }
        const bool known = detectedKey_.isNotEmpty();
        g.setFont(known ? fonts.getUIFontMedium(18.0f) : fonts.getUIFont(14.0f));
        g.setColour(
            ActiveTheme::getColour(known ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_DIM2));
        g.drawFittedText(
            known ? magda::music::NotationSettings::getInstance().formatRoot(detectedKey_)
                  : juce::String("Detecting..."),
            keyBox_.reduced(12, 0), juce::Justification::centredLeft, 1, 0.8f);
    }
    paintSectionLabel(g, scalesHeader_, "Scales");
    if (scalesHeader_.getWidth() >= 210) {
        g.setColour(dim2);
        g.setFont(fonts.getMonoFont(10.0f));
        g.drawText("shift-click filters", scalesHeader_, juce::Justification::centredRight, true);
    }

    // Footer.
    paintSectionLabel(g, noveltyCaption_, "Novelty");
    paintSectionLabel(g, extensionsCaption_, "Extensions");
    g.setFont(fonts.getMonoFont(10.5f));
    g.setColour(dim2);
    g.drawText("safe", noveltySafe_, juce::Justification::centredRight, false);
    g.drawText("wild", noveltyWild_, juce::Justification::centredLeft, false);
    if (!noveltyValue_.isEmpty()) {
        g.setFont(fonts.getMonoFont(11.5f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
        g.drawText(juce::String(juce::roundToInt(noveltySlider_.getValue() * 100.0)) + "%",
                   noveltyValue_, juce::Justification::centredLeft, false);
    }
}

void ChordPanelContent::resized() {
    auto bounds = getLocalBounds();
    footer_ = bounds.removeFromBottom(FOOTER_HEIGHT);
    const int total = bounds.getWidth();
    detectionCol_ = bounds.removeFromLeft(juce::roundToInt(static_cast<float>(total) * 0.28f));
    keyScaleCol_ = bounds.removeFromRight(juce::roundToInt(static_cast<float>(total) * 0.22f));
    suggestionsCol_ = bounds;

    const auto& fonts = FontManager::getInstance();

    // Sounding chord, then history as a chain of chips.
    {
        auto area = detectionCol_.reduced(PADDING);
        chordHeader_ = area.removeFromTop(SECTION_HEADER_HEIGHT);
        notationBtn_->setBounds(
            chordHeader_.withLeft(chordHeader_.getRight() - 46).withSizeKeepingCentre(46, 20));
        area.removeFromTop(4);
        // History keeps one row of chips; the status line only shows when there is room for both.
        constexpr int kHistoryNeeds = GAP * 2 + SECTION_HEADER_HEIGHT + 6 + HISTORY_HEIGHT;
        constexpr int kStatusNeeds = GAP + 2 + 16;
        const bool status = area.getHeight() >= 168 + kHistoryNeeds + kStatusNeeds;
        chordBox_ = area.removeFromTop(
            inEditorSidebar_
                ? 58
                : juce::jlimit(64, 168,
                               area.getHeight() - kHistoryNeeds - (status ? kStatusNeeds : 0)));
        statusLine_ = {};
        if (status) {
            area.removeFromTop(GAP + 2);
            statusLine_ = area.removeFromTop(16);
        }
        area.removeFromTop(GAP * 2);
        historyHeader_ = area.removeFromTop(SECTION_HEADER_HEIGHT);
        clearHistoryBtn_->setBounds(
            historyHeader_.withLeft(historyHeader_.getRight() - 26).withSizeKeepingCentre(26, 22));
        area.removeFromTop(6);

        // Oldest chips drop first, so the chain always ends on the chord just played.
        historyArrows_.clear();
        constexpr int kArrow = 18;
        const int chipHeight = juce::jmin(HISTORY_HEIGHT, area.getHeight());
        std::vector<int> widths;
        for (const auto& block : historyBlocks_) {
            const auto name = magda::music::NotationSettings::getInstance().format(
                block->getChord().getDisplayName());
            widths.push_back(juce::jmax(48, juce::roundToInt(juce::GlyphArrangement::getStringWidth(
                                                fonts.getUIFontMedium(12.5f), name)) +
                                                22));
        }
        const auto layout = [&](size_t first, bool place) {
            int x = area.getX();
            int y = area.getY();
            for (size_t i = first; i < historyBlocks_.size(); ++i) {
                const int arrow = i == first ? 0 : kArrow;
                if (x + arrow + widths[i] > area.getRight() && x > area.getX()) {
                    x = area.getX();
                    y += chipHeight + GAP;
                }
                if (y + chipHeight > area.getBottom())
                    return false;
                if (place) {
                    if (arrow > 0)
                        historyArrows_.push_back({x, y + (chipHeight - HISTORY_HEIGHT) / 2});
                    historyBlocks_[i]->setBounds(x + arrow, y,
                                                 juce::jmin(widths[i], area.getRight() - x - arrow),
                                                 chipHeight);
                }
                x += arrow + widths[i];
            }
            return true;
        };
        size_t first = 0;
        while (first < historyBlocks_.size() && (chipHeight < 24 || !layout(first, false)))
            ++first;
        for (size_t i = 0; i < historyBlocks_.size(); ++i)
            historyBlocks_[i]->setVisible(i >= first);
        layout(first, true);
    }

    // Suggestions: the tab row, then the two groups (or browse, or the AI tab).
    tabNote_ = legend_ = diatonicHeader_ = beyondHeader_ = {};
    {
        auto area = suggestionsCol_.reduced(PADDING);
        auto tabRow = area.removeFromTop(28);
        ksTabBtn_->setBounds(tabRow.removeFromLeft(30).withSizeKeepingCentre(30, 26));
        tabRow.removeFromLeft(4);
        aiTabBtn_->setBounds(tabRow.removeFromLeft(30).withSizeKeepingCentre(30, 26));
        tabRow.removeFromLeft(12);
        aiModelLabel_.setBounds(tabRow);
        if (!browseMode_ && suggestionTab_ == SuggestionTab::KS) {
            tabNote_ = tabRow.removeFromLeft(juce::jmin(110, tabRow.getWidth()));
            if (tabRow.getWidth() >= 260)
                legend_ = tabRow;
        }
        area.removeFromTop(GAP + 2);

        if (browseMode_) {
            diatonicHeader_ = area.removeFromTop(SECTION_HEADER_HEIGHT);
            area.removeFromTop(4);
            const int keyWidth = (area.getWidth() - 5 * 4) / 6;
            for (int row = 0; row < 2; ++row) {
                auto keys = area.removeFromTop(24);
                for (int col = 0; col < 6; ++col) {
                    const auto index = static_cast<size_t>(row * 6 + col);
                    if (index < browseKeyButtons_.size())
                        browseKeyButtons_[index]->setBounds(keys.removeFromLeft(keyWidth));
                    keys.removeFromLeft(4);
                }
                area.removeFromTop(4);
            }
            area.removeFromTop(GAP);
            if (browseViewport_) {
                browseViewport_->setBounds(area);
                layoutBrowseRows();
            }
        } else if (suggestionTab_ == SuggestionTab::KS) {
            const int width = area.getWidth();
            const int columns = width >= 470 ? 4 : width >= 330 ? 3 : 2;
            const int tileWidth = (width - GAP * (columns - 1)) / columns;
            bool overflow = false;
            const auto placeGroup = [&](size_t begin, size_t end, juce::Rectangle<int>& header) {
                if (begin >= end)
                    return;
                if (area.getHeight() < SECTION_HEADER_HEIGHT + TILE_HEIGHT)
                    overflow = true;
                if (!overflow) {
                    header = area.removeFromTop(SECTION_HEADER_HEIGHT);
                    area.removeFromTop(4);
                }
                juce::Rectangle<int> row;
                for (size_t i = begin; i < end; ++i) {
                    auto& block = suggestionBlocks_[i];
                    const int column = static_cast<int>(i - begin) % columns;
                    if (column == 0 && !overflow) {
                        if (i > begin)
                            area.removeFromTop(GAP);
                        overflow = area.getHeight() < TILE_HEIGHT;
                        if (!overflow)
                            row = area.removeFromTop(TILE_HEIGHT);
                    }
                    block->setVisible(!overflow);
                    if (!overflow)
                        block->setBounds(row.getX() + column * (tileWidth + GAP), row.getY(),
                                         tileWidth, TILE_HEIGHT);
                }
                area.removeFromTop(GAP * 2);
            };
            placeGroup(0, diatonicCount_, diatonicHeader_);
            placeGroup(diatonicCount_, suggestionBlocks_.size(), beyondHeader_);
        } else if (aiViewport_) {
            aiViewport_->setBounds(area);
            layoutAIProgressionRows();
        }
    }

    // Key, with browse beside it, then the detected scales.
    keyHeader_ = keyBox_ = browseBox_ = scalesHeader_ = {};
    {
        auto area = keyScaleCol_.reduced(PADDING);
        keyHeader_ = area.removeFromTop(SECTION_HEADER_HEIGHT);
        area.removeFromTop(4);
        auto keyRow = area.removeFromTop(44);
        // Beside the key when there is room for both, else at the right of its header.
        juce::Rectangle<int> browse;
        if (keyRow.getWidth() >= 170) {
            browseBox_ = keyRow.removeFromRight(44);
            keyRow.removeFromRight(GAP);
            browse = browseBox_.withSizeKeepingCentre(30, 26);
        } else {
            browse = keyHeader_.withLeft(keyHeader_.getRight() - 30).withSizeKeepingCentre(30, 24);
        }
        browseBtn_->setBounds(browse);
        backBtn_->setBounds(browse);
        keyBox_ = keyRow;
        area.removeFromTop(GAP * 2);

        if (!scaleBlocks_.empty()) {
            scalesHeader_ = area.removeFromTop(SECTION_HEADER_HEIGHT);
            area.removeFromTop(4);
        }
        for (auto& block : scaleBlocks_) {
            const bool fits = area.getHeight() >= SCALE_ROW_HEIGHT;
            block->setVisible(fits);
            if (!fits)
                continue;
            block->setBounds(area.removeFromTop(SCALE_ROW_HEIGHT));
            area.removeFromTop(6);
        }
    }

    // Footer: novelty and the extensions for K&S, the prompt for AI.
    noveltyCaption_ = noveltySafe_ = noveltyWild_ = noveltyValue_ = extensionsCaption_ =
        footerDivider_ = {};
    {
        auto footer = footer_.withTrimmedTop(1).reduced(PADDING, 0);
        if (inEditorSidebar_)
            footer.removeFromLeft(26);
        if (browseMode_)
            return;
        if (suggestionTab_ == SuggestionTab::AI) {
            aiSendBtn_->setBounds(footer.removeFromRight(30).withSizeKeepingCentre(30, 26));
            footer.removeFromRight(GAP);
            aiInputBox_->setBounds(footer.withSizeKeepingCentre(footer.getWidth(), 30));
            return;
        }

        scaleFilterBtn_->setBounds(footer.removeFromRight(30).withSizeKeepingCentre(30, 26));
        footer.removeFromRight(GAP * 2);
        const bool wide = footer.getWidth() >= 680;
        if (wide) {
            noveltyCaption_ = footer.removeFromLeft(72);
            noveltySafe_ = footer.removeFromLeft(30);
            footer.removeFromLeft(10);
        }
        const int sliderWidth = wide ? 140 : 110;
        noveltySlider_.setBounds(
            footer.removeFromLeft(sliderWidth).withSizeKeepingCentre(sliderWidth, 20));
        if (wide) {
            footer.removeFromLeft(10);
            noveltyWild_ = footer.removeFromLeft(30);
        }
        footer.removeFromLeft(8);
        noveltyValue_ = footer.removeFromLeft(40);
        footer.removeFromLeft(GAP);
        footerDivider_ = footer.removeFromLeft(1).reduced(0, 12);
        footer.removeFromLeft(GAP * 2);
        if (wide)
            extensionsCaption_ = footer.removeFromLeft(90);
        const int stripWidth = juce::jmin(extensions_.getPreferredWidth(), footer.getWidth());
        extensions_.setBounds(
            footer.removeFromLeft(stripWidth).withSizeKeepingCentre(stripWidth, 32));
    }
}

}  // namespace magda::daw::ui
