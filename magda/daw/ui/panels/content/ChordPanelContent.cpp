#include "ChordPanelContent.hpp"

#include "ui/components/chord/ChordBlockComponent.hpp"

namespace magda::daw::ui {

ChordPanelContent::ChordPanelContent() {
    setName("ChordPanel");
}

ChordPanelContent::~ChordPanelContent() {
    stopTimer();
}

void ChordPanelContent::setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin) {
    if (chordPlugin_ == plugin)
        return;

    chordPlugin_ = plugin;
    currentChord_.clear();
    detectedKey_.clear();
    recentChords_.clear();
    suggestions_.clear();
    suggestionBlocks_.clear();
    historyBlocks_.clear();

    if (plugin)
        startTimerHz(15);
    else
        stopTimer();

    repaint();
}

void ChordPanelContent::timerCallback() {
    if (!chordPlugin_)
        return;

    bool needsRepaint = false;
    bool needsLayout = false;

    // Current chord
    auto chord = chordPlugin_->getCurrentChordName();
    if (chord != currentChord_) {
        currentChord_ = chord;
        needsRepaint = true;
    }

    // Key/mode
    auto keyMode = chordPlugin_->getDetectedKeyMode();
    juce::String keyStr;
    if (keyMode.has_value())
        keyStr = keyMode->first + " " + keyMode->second;
    if (keyStr != detectedKey_) {
        detectedKey_ = keyStr;
        needsRepaint = true;
    }

    // Recent chords (last 5)
    auto history = chordPlugin_->getRecentChords();
    std::vector<juce::String> names;
    int start = std::max(0, static_cast<int>(history.size()) - 5);
    for (int i = start; i < static_cast<int>(history.size()); ++i)
        names.push_back(history[static_cast<size_t>(i)].getDisplayName());
    if (names != recentChords_) {
        recentChords_ = names;
        rebuildHistoryBlocks();
        needsLayout = true;
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

    if (needsLayout)
        resized();
    else if (needsRepaint)
        repaint();
}

void ChordPanelContent::rebuildSuggestionBlocks() {
    for (auto& block : suggestionBlocks_)
        removeChildComponent(block.get());
    suggestionBlocks_.clear();

    for (const auto& item : suggestions_) {
        auto block = std::make_unique<ChordBlockComponent>(item.chord);
        block->setDegreeLabel(item.degree);
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
    int start = std::max(0, static_cast<int>(history.size()) - 5);
    for (int i = start; i < static_cast<int>(history.size()); ++i) {
        auto block = std::make_unique<ChordBlockComponent>(history[static_cast<size_t>(i)]);
        addAndMakeVisible(block.get());
        historyBlocks_.push_back(std::move(block));
    }
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

        // "HISTORY" header
        if (!historyBlocks_.empty()) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("HISTORY", area.removeFromTop(SECTION_HEADER_HEIGHT),
                       juce::Justification::centredLeft);
            // History blocks are positioned in resized()
        }
    }

    // --- Column 2: Suggestions header ---
    if (suggestionsCol_.getWidth() > 0) {
        auto col = suggestionsCol_;

        // Column separator
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(col.getX(), col.getY() + 4, 1, col.getHeight() - 8);

        auto area = col.reduced(PADDING, 0);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("SUGGESTIONS", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);
        // Suggestion blocks are positioned in resized()

        if (suggestionBlocks_.empty() && chordPlugin_) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            auto emptyArea = area;
            emptyArea.removeFromTop(8);
            g.drawText("Play to get suggestions", emptyArea.removeFromTop(20),
                       juce::Justification::centredLeft);
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
    }
}

void ChordPanelContent::resized() {
    auto bounds = getLocalBounds();

    // 3-column split: detection (30%) | suggestions (45%) | key/scale (25%)
    auto totalWidth = bounds.getWidth();
    int detectionWidth = static_cast<int>(totalWidth * 0.28f);
    int keyScaleWidth = static_cast<int>(totalWidth * 0.22f);
    int suggestionsWidth = totalWidth - detectionWidth - keyScaleWidth - COLUMN_GAP * 2;

    detectionCol_ = bounds.removeFromLeft(detectionWidth);
    bounds.removeFromLeft(COLUMN_GAP);
    keyScaleCol_ = bounds.removeFromRight(keyScaleWidth);
    bounds.removeFromRight(COLUMN_GAP);
    suggestionsCol_ = bounds;

    // Position history blocks in detection column
    {
        auto area = detectionCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "CHORD" header
        area.removeFromTop(44);                     // chord display box
        area.removeFromTop(8);                      // gap

        if (!historyBlocks_.empty()) {
            area.removeFromTop(SECTION_HEADER_HEIGHT);  // "HISTORY" header
            area.removeFromTop(2);

            // Flow blocks horizontally, wrapping
            int x = area.getX();
            int y = area.getY();
            int blockWidth = std::max(50, (area.getWidth() - BLOCK_GAP) / 2);

            for (auto& block : historyBlocks_) {
                if (x + blockWidth > area.getRight()) {
                    x = area.getX();
                    y += BLOCK_HEIGHT + BLOCK_GAP;
                }
                if (y + BLOCK_HEIGHT > area.getBottom())
                    break;
                block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
                x += blockWidth + BLOCK_GAP;
            }
        }
    }

    // Position suggestion blocks in suggestions column (grid flow)
    {
        auto area = suggestionsCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "SUGGESTIONS" header
        area.removeFromTop(2);

        // 2 columns of blocks (or 3 if wide enough)
        int numCols = area.getWidth() > 280 ? 3 : 2;
        int blockWidth = (area.getWidth() - BLOCK_GAP * (numCols - 1)) / numCols;
        int x = area.getX();
        int y = area.getY();

        for (auto& block : suggestionBlocks_) {
            if (x + blockWidth > area.getRight() + 1) {
                x = area.getX();
                y += BLOCK_HEIGHT + BLOCK_GAP;
            }
            if (y + BLOCK_HEIGHT > area.getBottom())
                break;
            block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
            x += blockWidth + BLOCK_GAP;
        }
    }
}

}  // namespace magda::daw::ui
