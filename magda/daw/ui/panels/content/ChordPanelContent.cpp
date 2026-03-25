#include "ChordPanelContent.hpp"

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

    auto chord = chordPlugin_->getCurrentChordName();
    if (chord != currentChord_) {
        currentChord_ = chord;
        needsRepaint = true;
    }

    auto keyMode = chordPlugin_->getDetectedKeyMode();
    juce::String keyStr;
    if (keyMode.has_value())
        keyStr = keyMode->first + " " + keyMode->second;
    if (keyStr != detectedKey_) {
        detectedKey_ = keyStr;
        needsRepaint = true;
    }

    auto history = chordPlugin_->getRecentChords();
    // Show last 8 chords
    std::vector<juce::String> names;
    int start = std::max(0, static_cast<int>(history.size()) - 8);
    for (int i = start; i < static_cast<int>(history.size()); ++i)
        names.push_back(history[static_cast<size_t>(i)].getDisplayName());
    if (names != recentChords_) {
        recentChords_ = names;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void ChordPanelContent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getBackgroundColour());
    g.fillRect(bounds);

    // Left border
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(bounds.getX(), bounds.getY(), 1, bounds.getHeight());

    auto area = bounds.reduced(12, 8);

    // Section: "Chord" header
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText("CHORD", area.removeFromTop(16), juce::Justification::centredLeft);
    area.removeFromTop(4);

    // Current chord display
    auto chordBox = area.removeFromTop(48);
    g.setColour(DarkTheme::getBackgroundColour().brighter(0.06f));
    g.fillRoundedRectangle(chordBox.toFloat(), 6.0f);
    g.setColour(DarkTheme::getBorderColour());
    g.drawRoundedRectangle(chordBox.toFloat(), 6.0f, 1.0f);

    if (currentChord_.isEmpty()) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.4f));
        g.setFont(FontManager::getInstance().getUIFont(14.0f));
        g.drawText("Play a chord...", chordBox, juce::Justification::centred);
    } else {
        g.setColour(DarkTheme::getAccentColour());
        g.setFont(FontManager::getInstance().getUIFont(24.0f).boldened());
        g.drawText(currentChord_, chordBox, juce::Justification::centred);
    }

    area.removeFromTop(12);

    // Section: Key
    if (detectedKey_.isNotEmpty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("KEY", area.removeFromTop(16), juce::Justification::centredLeft);
        area.removeFromTop(2);

        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFont(14.0f));
        g.drawText(detectedKey_, area.removeFromTop(20), juce::Justification::centredLeft);
        area.removeFromTop(12);
    }

    // Section: Recent chords
    if (!recentChords_.empty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("HISTORY", area.removeFromTop(16), juce::Justification::centredLeft);
        area.removeFromTop(4);

        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        for (const auto& name : recentChords_) {
            if (area.getHeight() < 18)
                break;
            auto row = area.removeFromTop(18);
            bool isCurrent = (name == currentChord_);
            g.setColour(isCurrent ? DarkTheme::getAccentColour()
                                  : DarkTheme::getSecondaryTextColour());
            g.drawText(name, row, juce::Justification::centredLeft);
        }
    }

    // Placeholder when no plugin connected
    if (!chordPlugin_) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("No MIDI device", getLocalBounds(), juce::Justification::centred);
    }
}

void ChordPanelContent::resized() {
    // Pure paint-based layout, nothing to position
}

}  // namespace magda::daw::ui
