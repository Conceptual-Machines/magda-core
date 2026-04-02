#include "ChordPanelContent.hpp"

#include "../../../../agents/llm_client_factory.hpp"
#include "BinaryData.h"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/TrackManager.hpp"
#include "music/ChordEngine.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/chord/ChordBlockComponent.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

// ============================================================================
// ScaleBlockComponent
// ============================================================================

ScaleBlockComponent::ScaleBlockComponent(const magda::music::ScaleWithChords& scale)
    : scale_(scale) {
    setName("ScaleBlock");
    setRepaintsOnMouseActivity(true);
}

void ScaleBlockComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Green-tinted background — brighter when selected
    float baseAlpha = selected_ ? 0.35f : 0.15f;
    auto colour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(baseAlpha);
    if (isMouseOver())
        colour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(baseAlpha + 0.15f);

    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border when selected or hovered
    if (selected_ || isMouseOver()) {
        float borderAlpha = selected_ ? 0.7f : 0.5f;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(borderAlpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }

    // Scale name
    g.setColour(selected_ ? DarkTheme::getTextColour()
                          : DarkTheme::getTextColour().withAlpha(0.8f));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(juce::String(scale_.name), getLocalBounds().reduced(4, 0),
               juce::Justification::centred);
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

ScaleChordsPopup::ScaleChordsPopup(const magda::music::ScaleWithChords& scale) : scale_(scale) {
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

    // Dark background with border
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(DarkTheme::getBorderColour().brighter(0.2f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Title
    auto titleArea = getLocalBounds().reduced(10, 6).removeFromTop(20);
    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
    g.drawText(juce::String(scale_.name), titleArea, juce::Justification::centredLeft);
}

void ScaleChordsPopup::resized() {
    auto area = getLocalBounds().reduced(10, 6);
    area.removeFromTop(24);  // title

    // Grid of chord blocks — single row if they fit, wrap otherwise
    int blockWidth = 70;
    int blockHeight = 36;
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
    int blockWidth = 70;
    int blockHeight = 36;
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

BrowseScaleRowComponent::BrowseScaleRowComponent(const magda::music::ScaleWithChords& scale)
    : scale_(scale) {
    setName("BrowseScaleRow");
    setRepaintsOnMouseActivity(true);
}

void BrowseScaleRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    float baseAlpha = 0.08f;
    if (isMouseOver())
        baseAlpha = 0.2f;
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(baseAlpha));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(DarkTheme::getTextColour().withAlpha(0.8f));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(juce::String(scale_.name), bounds.reduced(6, 0), juce::Justification::centredLeft);
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

int BrowseScaleRowComponent::getRowHeight() const {
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
            g.setColour(DarkTheme::getAccentColour().withAlpha(alpha));
            g.setFont(FontManager::getInstance().getUIFont(11.0f).boldened());
            g.drawText(row.name, row.nameArea, juce::Justification::centredLeft);

            if (row.description.isNotEmpty() && !row.descArea.isEmpty()) {
                g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(alpha));
                g.setFont(FontManager::getInstance().getUIFont(9.5f));
                g.drawText(row.description, row.descArea, juce::Justification::centredLeft);
            }
        }

        // Draw prompt + status above streaming text
        int promptBottom = streamingTextBottom;
        if (loading && paintData.rows.empty()) {
            int y = 8;
            if (promptText.isNotEmpty()) {
                g.setColour(DarkTheme::getTextColour());
                g.setFont(FontManager::getInstance().getUIFont(11.0f));
                g.drawText(promptText, 4, y, getWidth() - 8, 20, juce::Justification::centredLeft);
                y += 24;
            }
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText("Generating...", 4, y, getWidth() - 8, 20, juce::Justification::centredLeft);
            promptBottom = juce::jmax(promptBottom, y + 28);
        }

        // Draw streaming text below prompt/existing content
        if (streamingText.isNotEmpty()) {
            auto font = FontManager::getInstance().getUIFont(9.5f);
            g.setFont(font);
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.7f));

            int textY = promptBottom;
            int textWidth = getWidth() - 8;
            auto layout = juce::TextLayout();
            juce::AttributedString attrStr;
            attrStr.append(streamingText, font,
                           DarkTheme::getSecondaryTextColour().withAlpha(0.7f));
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
    }

    repaint();
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
            auto cfg = Config::getInstance().getAgentLLMConfig(magda::role::MUSIC);
            auto model = cfg.model.empty() ? cfg.provider : cfg.model;
            safeThis->aiModelLabel_.setText(juce::String(model), juce::dontSendNotification);
        }
    });
}

void ChordPanelContent::timerCallback() {
    // Delayed clear of chord display after note release
    stopTimer();
    currentChord_.clear();
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
        detectedScales_ = newScales;
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

    for (const auto& item : suggestions_) {
        auto block = std::make_unique<ChordBlockComponent>(item.chord);
        block->setDegreeLabel(item.degree);
        block->onClicked = [this](const magda::music::Chord& c) { previewChord(c); };
        block->onReleased = [this] { stopPreview(); };
        addAndMakeVisible(block.get());
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
        auto block = std::make_unique<ChordBlockComponent>(history[static_cast<size_t>(i)]);
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
    for (auto it = selectedScaleNames_.begin(); it != selectedScaleNames_.end();) {
        if (validNames.find(*it) == validNames.end())
            it = selectedScaleNames_.erase(it);
        else
            ++it;
    }

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
    noveltyLabel_->setVisible(false);
    add7thsBtn_->setVisible(false);
    add9thsBtn_->setVisible(false);
    add11thsBtn_->setVisible(false);
    add13thsBtn_->setVisible(false);
    addAltBtn_->setVisible(false);
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
                           DarkTheme::getColour(DarkTheme::SURFACE));
            btn->setColour(juce::TextButton::buttonOnColourId, DarkTheme::getAccentColour());
            btn->setColour(juce::TextButton::textColourOffId,
                           DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            btn->setColour(juce::TextButton::textColourOnId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
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
        auto cfg = Config::getInstance().getAgentLLMConfig(magda::role::MUSIC);
        auto model = cfg.model.empty() ? cfg.provider : cfg.model;
        aiModelLabel_.setText(juce::String(model), juce::dontSendNotification);
    }

    // Toggle K&S footer controls
    noveltyLabel_->setVisible(isKS);
    add7thsBtn_->setVisible(isKS);
    add9thsBtn_->setVisible(isKS);
    add11thsBtn_->setVisible(isKS);
    add13thsBtn_->setVisible(isKS);
    addAltBtn_->setVisible(isKS);
    scaleFilterBtn_->setVisible(isKS);

    // Tab button state
    ksTabBtn_->setToggleState(isKS, juce::dontSendNotification);
    aiTabBtn_->setToggleState(!isKS, juce::dontSendNotification);

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
        auto& prog = aiProgsLayout[i];

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

        // If loading with no rows, account for prompt + "Generating..." height
        if (container->loading && paintData.rows.empty()) {
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
            attrStr.append(container->streamingText, font, DarkTheme::getSecondaryTextColour());
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

    juce::DragAndDropContainer::performExternalDragDropOfFiles(
        juce::StringArray{tempFile.getFullPathName()}, false, this);

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
    // Build context from chord history and detected key
    juce::String context;
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
        for (size_t i = 0; i < std::min(owner_.detectedScales_.size(), size_t(3)); ++i) {
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

    // Create LLM client — use Responses API for CFG when on OpenAI direct,
    // otherwise fall back to the configured provider (local, Anthropic, etc.)
    bool cfg = agentConfig.provider == magda::provider::OPENAI && agentConfig.baseUrl.empty();

    std::unique_ptr<llm::LLMClient> client;
    if (cfg) {
        auto pc = magda::toLLMProviderConfig(agentConfig, "music");
        pc.provider = llm::Provider::OpenAIResponses;
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
            auto tokenCopy = token;
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
    // Novelty slider (0-100%)
    noveltyLabel_ = std::make_unique<magda::DraggableValueLabel>(
        magda::DraggableValueLabel::Format::Percentage);
    noveltyLabel_->setRange(0.0, 1.0, 0.3);
    noveltyLabel_->setValue(0.3, juce::dontSendNotification);
    noveltyLabel_->setFontSize(9.0f);
    noveltyLabel_->setDrawBackground(false);
    noveltyLabel_->setDrawBorder(false);
    noveltyLabel_->setShowFillIndicator(false);
    noveltyLabel_->setTextOverride("Nov 30%");
    noveltyLabel_->setTooltip(
        "Balance between safe diatonic (0%) and adventurous non-diatonic (100%) suggestions");
    noveltyLabel_->onValueChange = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().novelty =
                static_cast<float>(noveltyLabel_->getValue());
            int pct = static_cast<int>(noveltyLabel_->getValue() * 100.0);
            noveltyLabel_->setTextOverride("Novelty " + juce::String(pct) + "%");
            chordPlugin_->refreshSuggestions();
        }
    };
    addAndMakeVisible(noveltyLabel_.get());

    // Toggle button helper — uses project-wide SmallButtonLookAndFeel
    auto makeToggle = [this](const juce::String& text) {
        auto btn = std::make_unique<juce::TextButton>(text);
        btn->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        btn->setClickingTogglesState(true);
        btn->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        btn->setColour(juce::TextButton::buttonOnColourId, DarkTheme::getAccentColour());
        btn->setColour(juce::TextButton::textColourOffId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->setColour(juce::TextButton::textColourOnId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND));
        addAndMakeVisible(btn.get());
        return btn;
    };

    add7thsBtn_ = makeToggle("7th");
    add7thsBtn_->setToggleState(true, juce::dontSendNotification);
    add7thsBtn_->setTooltip("Include 7th chords (Maj7, min7, dom7, dim7)");
    add7thsBtn_->onClick = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().add7ths = add7thsBtn_->getToggleState();
            chordPlugin_->refreshSuggestions();
        }
    };

    add9thsBtn_ = makeToggle("9th");
    add9thsBtn_->setTooltip("Include 9th chords (Maj9, min9, dom9)");
    add9thsBtn_->onClick = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().add9ths = add9thsBtn_->getToggleState();
            chordPlugin_->refreshSuggestions();
        }
    };

    add11thsBtn_ = makeToggle("11th");
    add11thsBtn_->setTooltip("Include 11th chords (dom11, min11)");
    add11thsBtn_->onClick = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().add11ths = add11thsBtn_->getToggleState();
            chordPlugin_->refreshSuggestions();
        }
    };

    add13thsBtn_ = makeToggle("13th");
    add13thsBtn_->setTooltip("Include 13th chords (dom13, min13)");
    add13thsBtn_->onClick = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().add13ths = add13thsBtn_->getToggleState();
            chordPlugin_->refreshSuggestions();
        }
    };

    addAltBtn_ = makeToggle("Alt");
    addAltBtn_->setTooltip("Include altered/non-diatonic chords (borrowed chords, tritone subs)");
    addAltBtn_->onClick = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().addAlterations = addAltBtn_->getToggleState();
            chordPlugin_->refreshSuggestions();
        }
    };

    scaleFilterBtn_ = std::make_unique<magda::SvgButton>("ScaleFilter", BinaryData::funnel_svg,
                                                         BinaryData::funnel_svgSize);
    scaleFilterBtn_->setClickingTogglesState(true);
    scaleFilterBtn_->setToggleState(true, juce::dontSendNotification);
    scaleFilterBtn_->setActive(true);
    scaleFilterBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));  // SVG fill color
    scaleFilterBtn_->setActiveColor(DarkTheme::getColour(DarkTheme::BACKGROUND));
    scaleFilterBtn_->setActiveBackgroundColor(DarkTheme::getAccentColour());
    scaleFilterBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    scaleFilterBtn_->setTooltip("Filter suggestions by detected scales");
    scaleFilterBtn_->onClick = [this]() {
        bool on = scaleFilterBtn_->getToggleState();
        scaleFilterBtn_->setActive(on);
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().useScaleFiltering = on;
            chordPlugin_->refreshSuggestions();
        }
    };
    addAndMakeVisible(scaleFilterBtn_.get());

    browseBtn_ = std::make_unique<magda::SvgButton>("Browse", BinaryData::browser_svg,
                                                    BinaryData::browser_svgSize);
    browseBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    browseBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    browseBtn_->setTooltip("Browse scales");
    browseBtn_->onClick = [this]() { enterBrowseMode(); };
    addAndMakeVisible(browseBtn_.get());

    backBtn_ = std::make_unique<magda::SvgButton>("Back", BinaryData::chevron_left_svg,
                                                  BinaryData::chevron_left_svgSize);
    backBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    backBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    backBtn_->setTooltip("Back to suggestions");
    backBtn_->onClick = [this]() { exitBrowseMode(); };
    backBtn_->setVisible(false);
    addAndMakeVisible(backBtn_.get());

    clearHistoryBtn_ = std::make_unique<magda::SvgButton>("ClearHistory", BinaryData::delete_svg,
                                                          BinaryData::delete_svgSize);
    clearHistoryBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clearHistoryBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clearHistoryBtn_->setTooltip("Clear chord history and reset detection");
    clearHistoryBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->clearHistory();
    };
    addAndMakeVisible(clearHistoryBtn_.get());

    // Suggestion column tab buttons
    ksTabBtn_ = std::make_unique<magda::SvgButton>("KSTab", BinaryData::psychology_svg,
                                                   BinaryData::psychology_svgSize);
    ksTabBtn_->setClickingTogglesState(true);
    ksTabBtn_->setRadioGroupId(1001);
    ksTabBtn_->setToggleState(true, juce::dontSendNotification);
    ksTabBtn_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    ksTabBtn_->setActiveColor(juce::Colours::white);
    ksTabBtn_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    ksTabBtn_->setActiveBackgroundColor(DarkTheme::getAccentColour());
    ksTabBtn_->setTooltip("Krumhansl-Schmuckler profile suggestions");
    ksTabBtn_->onClick = [this]() { switchToTab(SuggestionTab::KS); };
    addAndMakeVisible(ksTabBtn_.get());

    aiTabBtn_ =
        std::make_unique<magda::SvgButton>("AITab", BinaryData::ai_svg, BinaryData::ai_svgSize);
    aiTabBtn_->setClickingTogglesState(true);
    aiTabBtn_->setRadioGroupId(1001);
    aiTabBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    aiTabBtn_->setActiveColor(juce::Colours::white);
    aiTabBtn_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    aiTabBtn_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    aiTabBtn_->setTooltip("AI chord progression suggestions");
    aiTabBtn_->onClick = [this]() { switchToTab(SuggestionTab::AI); };
    addAndMakeVisible(aiTabBtn_.get());

    aiModelLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    aiModelLabel_.setColour(juce::Label::textColourId,
                            DarkTheme::getSecondaryTextColour().withAlpha(0.6f));
    aiModelLabel_.setJustificationType(juce::Justification::centredLeft);
    aiModelLabel_.setVisible(false);
    addAndMakeVisible(aiModelLabel_);

    // AI tab viewport for scrollable progression display
    aiViewport_ = std::make_unique<juce::Viewport>();
    aiContainer_ = std::make_unique<AIContainerComponent>();
    aiViewport_->setViewedComponent(aiContainer_.get(), false);
    aiViewport_->setScrollBarsShown(true, false);
    aiViewport_->setVisible(false);
    addAndMakeVisible(aiViewport_.get());

    // AI text input
    aiInputBox_ = std::make_unique<juce::TextEditor>();
    aiInputBox_->setMultiLine(false);
    aiInputBox_->setReturnKeyStartsNewLine(false);
    aiInputBox_->setFont(FontManager::getInstance().getUIFont(11.0f));
    aiInputBox_->setColour(juce::TextEditor::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    aiInputBox_->setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    aiInputBox_->setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    aiInputBox_->setTextToShowWhenEmpty("Describe a chord progression...",
                                        DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
    aiInputBox_->onReturnKey = [this]() { requestAISuggestions(); };
    aiInputBox_->setVisible(false);
    addAndMakeVisible(aiInputBox_.get());

    aiSendBtn_ = std::make_unique<magda::SvgButton>("AISend", BinaryData::send_svg,
                                                    BinaryData::send_svgSize);
    aiSendBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    aiSendBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    aiSendBtn_->onClick = [this]() { requestAISuggestions(); };
    aiSendBtn_->setVisible(false);
    addAndMakeVisible(aiSendBtn_.get());
}

void ChordPanelContent::syncFooterFromParams() {
    if (!chordPlugin_)
        return;

    const auto& params = chordPlugin_->getSuggestionParams();
    noveltyLabel_->setValue(params.novelty, juce::dontSendNotification);
    int pct = static_cast<int>(params.novelty * 100.0f);
    noveltyLabel_->setTextOverride("Novelty " + juce::String(pct) + "%");
    add7thsBtn_->setToggleState(params.add7ths, juce::dontSendNotification);
    add9thsBtn_->setToggleState(params.add9ths, juce::dontSendNotification);
    add11thsBtn_->setToggleState(params.add11ths, juce::dontSendNotification);
    add13thsBtn_->setToggleState(params.add13ths, juce::dontSendNotification);
    addAltBtn_->setToggleState(params.addAlterations, juce::dontSendNotification);
    scaleFilterBtn_->setToggleState(params.useScaleFiltering, juce::dontSendNotification);
    scaleFilterBtn_->setActive(params.useScaleFiltering);
}

void ChordPanelContent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getBackgroundColour());
    g.fillRect(bounds);

    // Left border
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(bounds.getX(), bounds.getY(), 1, bounds.getHeight());

    // Placeholder when no plugin connected
    if (!chordPlugin_) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("No MIDI device", bounds, juce::Justification::centred);
        return;
    }

    auto headerFont = FontManager::getInstance().getUIFont(10.0f);

    // --- Column 1: Detection ---
    if (detectionCol_.getWidth() > 0) {
        auto col = detectionCol_;
        auto area = col.reduced(PADDING, 0);

        // "CHORD" header
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("CHORD", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);

        // Current chord display box
        auto chordBox = area.removeFromTop(44);
        g.setColour(DarkTheme::getBackgroundColour().brighter(0.06f));
        g.fillRoundedRectangle(chordBox.toFloat(), 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(chordBox.toFloat(), 4.0f, 1.0f);

        if (currentChord_.isEmpty()) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.4f));
            g.setFont(FontManager::getInstance().getUIFont(13.0f));
            g.drawText("Play...", chordBox, juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getUIFont(20.0f).boldened());
            g.drawText(currentChord_, chordBox, juce::Justification::centred);
        }

        area.removeFromTop(8);

        // "HISTORY" header with clear button
        {
            auto histHeaderArea = area.removeFromTop(SECTION_HEADER_HEIGHT);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("HISTORY", histHeaderArea, juce::Justification::centredLeft);
        }
    }

    // --- Column 2: Suggestions (browse OR tabbed: K&S / AI) ---
    if (suggestionsCol_.getWidth() > 0) {
        auto col = suggestionsCol_;

        // Column separator
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(col.getX(), col.getY() + 4, 1, col.getHeight() - 8);

        auto area = col.reduced(PADDING, 0);

        if (browseMode_) {
            // Browse mode header
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("BROWSE SCALES", area.removeFromTop(SECTION_HEADER_HEIGHT),
                       juce::Justification::centredLeft);
        } else {
            auto tabArea = area.removeFromTop(SECTION_HEADER_HEIGHT);
            // Horizontal border below tab header
            g.setColour(DarkTheme::getBorderColour());
            g.fillRect(col.getX() + PADDING, tabArea.getBottom(), col.getWidth() - 2 * PADDING, 1);

            if (suggestionTab_ == SuggestionTab::KS) {
                if (suggestionBlocks_.empty() && chordPlugin_) {
                    g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
                    g.setFont(FontManager::getInstance().getUIFont(11.0f));
                    area.removeFromTop(8);
                    g.drawText("Play to get suggestions", area.removeFromTop(20),
                               juce::Justification::centredLeft);
                }
            } else {
                // AI tab content — draw progression names and descriptions
                if (!aiLoading_ && (!chordPlugin_ || chordPlugin_->getAIProgressions().empty())) {
                    g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
                    g.setFont(FontManager::getInstance().getUIFont(11.0f));
                    area.removeFromTop(8);
                    g.drawText("Type a prompt or press Send", area.removeFromTop(20),
                               juce::Justification::centredLeft);
                }
            }
        }
    }

    // --- Column 3: Key / Scale ---
    if (keyScaleCol_.getWidth() > 0) {
        auto col = keyScaleCol_;

        // Column separator
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(col.getX(), col.getY() + 4, 1, col.getHeight() - 8);

        auto area = col.reduced(PADDING, 0);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("KEY", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);

        if (detectedKey_.isNotEmpty()) {
            area.removeFromTop(4);
            g.setColour(DarkTheme::getTextColour());
            g.setFont(FontManager::getInstance().getUIFont(16.0f).boldened());
            g.drawText(detectedKey_, area.removeFromTop(24), juce::Justification::centredLeft);
        } else {
            area.removeFromTop(4);
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText("Detecting...", area.removeFromTop(20), juce::Justification::centredLeft);
        }

        area.removeFromTop(8);

        // "SCALES" header
        if (!scaleBlocks_.empty()) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("SCALES", area.removeFromTop(SECTION_HEADER_HEIGHT),
                       juce::Justification::centredLeft);
            // Scale blocks positioned in resized()
        }
    }

    // --- Border above browse button ---
    if (chordPlugin_ && keyScaleCol_.getWidth() > 0 && browseBtn_ && browseBtn_->isVisible()) {
        g.setColour(DarkTheme::getBorderColour());
        int browseTopY = browseBtn_->getY() - 6;
        g.fillRect(keyScaleCol_.getX() + PADDING, browseTopY, keyScaleCol_.getWidth() - 2 * PADDING,
                   1);
    }

    // --- Footer separator line ---
    if (chordPlugin_) {
        int footerY = getHeight() - FOOTER_HEIGHT;
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(0, footerY, getWidth(), 1);
    }
}

void ChordPanelContent::resized() {
    auto bounds = getLocalBounds();

    // Reserve footer area
    auto footerArea = bounds.removeFromBottom(FOOTER_HEIGHT);

    // 3-column split: detection (28%) | suggestions (50%) | key/scale (22%)
    auto totalWidth = bounds.getWidth();
    int detectionWidth = static_cast<int>(totalWidth * 0.28f);
    int keyScaleWidth = static_cast<int>(totalWidth * 0.22f);

    detectionCol_ = bounds.removeFromLeft(detectionWidth);
    bounds.removeFromLeft(COLUMN_GAP);
    keyScaleCol_ = bounds.removeFromRight(keyScaleWidth);
    bounds.removeFromRight(COLUMN_GAP);
    suggestionsCol_ = bounds;

    // Layout footer — depends on active tab (hidden in browse mode)
    if (!browseMode_) {
        auto footer = footerArea;
        footer.removeFromTop(1);  // separator line
        footer.removeFromLeft(detectionWidth + COLUMN_GAP);

        if (suggestionTab_ == SuggestionTab::KS) {
            // K&S footer: novelty / 7th / 9th / 11th / 13th / alt / funnel
            auto mid = footer.reduced(PADDING, 0);
            scaleFilterBtn_->setBounds(mid.removeFromRight(22).reduced(0, 2));
            mid.removeFromRight(4);
            noveltyLabel_->setBounds(mid.removeFromLeft(80).reduced(0, 2));
            mid.removeFromLeft(4);
            add7thsBtn_->setBounds(mid.removeFromLeft(32).reduced(0, 2));
            mid.removeFromLeft(2);
            add9thsBtn_->setBounds(mid.removeFromLeft(32).reduced(0, 2));
            mid.removeFromLeft(2);
            add11thsBtn_->setBounds(mid.removeFromLeft(34).reduced(0, 2));
            mid.removeFromLeft(2);
            add13thsBtn_->setBounds(mid.removeFromLeft(34).reduced(0, 2));
            mid.removeFromLeft(2);
            addAltBtn_->setBounds(mid.removeFromLeft(32).reduced(0, 2));
        } else {
            // AI footer: [text input] [Send]
            auto mid = footer.reduced(PADDING, 0);
            aiSendBtn_->setBounds(mid.removeFromRight(22).reduced(0, 2));
            mid.removeFromRight(4);
            aiInputBox_->setBounds(mid.reduced(0, 1));
        }
    }

    // Position history blocks in detection column
    {
        auto area = detectionCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "CHORD" header
        area.removeFromTop(44);                     // chord display box
        area.removeFromTop(8);                      // gap

        auto histHeader = area.removeFromTop(SECTION_HEADER_HEIGHT);
        clearHistoryBtn_->setBounds(histHeader.removeFromRight(22).reduced(0, 2));
        area.removeFromTop(2);

        if (!historyBlocks_.empty()) {
            int x = area.getX();
            int y = area.getY();
            int blockWidth = std::max(50, (area.getWidth() - BLOCK_GAP) / 2);
            bool overflow = false;

            for (auto& block : historyBlocks_) {
                if (overflow) {
                    block->setVisible(false);
                    continue;
                }
                if (x + blockWidth > area.getRight()) {
                    x = area.getX();
                    y += BLOCK_HEIGHT + BLOCK_GAP;
                }
                if (y + BLOCK_HEIGHT > area.getBottom()) {
                    overflow = true;
                    block->setVisible(false);
                    continue;
                }
                block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
                block->setVisible(true);
                x += blockWidth + BLOCK_GAP;
            }
        }
    }

    // Suggestions column — browse mode OR tab buttons + content
    {
        auto area = suggestionsCol_.reduced(PADDING, 0);

        if (browseMode_) {
            // Browse mode takes over the entire suggestions column (no tabs)
            area.removeFromTop(SECTION_HEADER_HEIGHT);  // "BROWSE SCALES" header painted in paint()
            area.removeFromTop(2);

            // Key filter buttons — two rows of 6
            int keyBtnWidth = (area.getWidth() - 5 * 2) / 6;
            for (int row = 0; row < 2; ++row) {
                int x = area.getX();
                for (int col = 0; col < 6; ++col) {
                    int idx = row * 6 + col;
                    if (idx < static_cast<int>(browseKeyButtons_.size())) {
                        browseKeyButtons_[static_cast<size_t>(idx)]->setBounds(x, area.getY(),
                                                                               keyBtnWidth, 18);
                        x += keyBtnWidth + 2;
                    }
                }
                area.removeFromTop(20);
            }
            area.removeFromTop(4);

            if (browseViewport_) {
                browseViewport_->setBounds(area);
                layoutBrowseRows();
            }
        } else {
            // Tab buttons in header row
            auto tabRow = area.removeFromTop(SECTION_HEADER_HEIGHT);
            ksTabBtn_->setBounds(tabRow.removeFromLeft(28));
            tabRow.removeFromLeft(2);
            aiTabBtn_->setBounds(tabRow.removeFromLeft(28));
            tabRow.removeFromLeft(6);
            aiModelLabel_.setBounds(tabRow);

            area.removeFromTop(2);

            if (suggestionTab_ == SuggestionTab::KS) {
                // K&S tab content
                int numCols = area.getWidth() > 280 ? 3 : 2;
                int blockWidth = (area.getWidth() - BLOCK_GAP * (numCols - 1)) / numCols;
                int x = area.getX();
                int y = area.getY();
                bool overflow = false;

                for (auto& block : suggestionBlocks_) {
                    if (overflow) {
                        block->setVisible(false);
                        continue;
                    }
                    if (x + blockWidth > area.getRight() + 1) {
                        x = area.getX();
                        y += BLOCK_HEIGHT + BLOCK_GAP;
                    }
                    if (y + BLOCK_HEIGHT > area.getBottom()) {
                        overflow = true;
                        block->setVisible(false);
                        continue;
                    }
                    block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
                    block->setVisible(true);
                    x += blockWidth + BLOCK_GAP;
                }
            } else {
                // AI tab content — scrollable viewport
                if (aiViewport_) {
                    aiViewport_->setBounds(area);
                    layoutAIProgressionRows();
                }
            }
        }
    }

    // Position scale blocks in key/scale column
    {
        auto area = keyScaleCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "KEY" header

        if (detectedKey_.isNotEmpty()) {
            area.removeFromTop(4);   // gap
            area.removeFromTop(24);  // key text
        } else {
            area.removeFromTop(4);
            area.removeFromTop(20);
        }

        area.removeFromTop(8);  // gap before SCALES

        if (!scaleBlocks_.empty()) {
            area.removeFromTop(SECTION_HEADER_HEIGHT);  // "SCALES" header
            area.removeFromTop(2);

            int scaleBlockHeight = 26;
            bool scaleOverflow = false;
            for (auto& block : scaleBlocks_) {
                if (scaleOverflow) {
                    block->setVisible(false);
                    continue;
                }
                if (area.getHeight() < scaleBlockHeight) {
                    scaleOverflow = true;
                    block->setVisible(false);
                    continue;
                }
                block->setBounds(area.removeFromTop(scaleBlockHeight));
                block->setVisible(true);
                area.removeFromTop(BLOCK_GAP);
            }
        }

        // Browse button pinned to bottom of key/scale column (border drawn in paint)
        auto browseArea = keyScaleCol_.reduced(PADDING, 0);
        browseArea.removeFromBottom(6);  // bottom padding
        auto btnArea = browseArea.removeFromBottom(22).reduced(0, 1);
        browseBtn_->setBounds(btnArea);
        backBtn_->setBounds(btnArea);
    }
}

}  // namespace magda::daw::ui
