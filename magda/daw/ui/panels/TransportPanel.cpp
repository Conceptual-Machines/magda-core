#include "TransportPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "BinaryData.h"
#include "Config.hpp"

namespace magda {

TransportPanel::TransportPanel() {
    setupTransportButtons();
    setupTimeDisplayBoxes();
    setupTempoAndQuantize();
}

TransportPanel::~TransportPanel() = default;

void TransportPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRANSPORT_BACKGROUND));

    // Draw subtle borders between sections
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

    auto bounds = getLocalBounds();
    auto transportArea = getTransportControlsArea();
    auto timeArea = getTimeDisplayArea();

    // Vertical separators
    g.drawVerticalLine(transportArea.getRight(), bounds.getY(), bounds.getBottom());
    g.drawVerticalLine(timeArea.getRight(), bounds.getY(), bounds.getBottom());

    // Bottom border for visual separation from content below
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void TransportPanel::resized() {
    auto transportArea = getTransportControlsArea();
    auto timeArea = getTimeDisplayArea();
    auto tempoArea = getTempoQuantizeArea();

    // Transport controls layout
    auto buttonSize = 44;
    auto buttonY = transportArea.getCentreY() - buttonSize / 2;
    auto buttonSpacing = 4;

    auto x = transportArea.getX() + 8;

    playButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    stopButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    recordButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    pauseButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing + 8;

    loopButton->setBounds(x, buttonY, buttonSize, buttonSize);

    // Time display boxes layout - 3 equal boxes
    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();

    int boxWidth = 160;  // Wider for bars.beats.sub format
    int boxSpacing = 8;
    int primaryHeight = showBothRows ? 18 : 30;
    int secondaryHeight = showBothRows ? 16 : 0;
    int totalBoxHeight = primaryHeight + secondaryHeight;

    int startX = timeArea.getX() + 10;
    int centerY = timeArea.getCentreY();
    int boxY = centerY - totalBoxHeight / 2;

    // Playhead box
    playheadPrimaryLabel->setBounds(startX, boxY, boxWidth, primaryHeight);
    if (showBothRows) {
        playheadSecondaryLabel->setBounds(startX, boxY + primaryHeight, boxWidth, secondaryHeight);
        playheadSecondaryLabel->setVisible(true);
    } else {
        playheadSecondaryLabel->setVisible(false);
    }

    // Selection box
    int selX = startX + boxWidth + boxSpacing;
    selectionPrimaryLabel->setBounds(selX, boxY, boxWidth, primaryHeight);
    if (showBothRows) {
        selectionSecondaryLabel->setBounds(selX, boxY + primaryHeight, boxWidth, secondaryHeight);
        selectionSecondaryLabel->setVisible(true);
    } else {
        selectionSecondaryLabel->setVisible(false);
    }

    // Loop box
    int loopX = selX + boxWidth + boxSpacing;
    loopPrimaryLabel->setBounds(loopX, boxY, boxWidth, primaryHeight);
    if (showBothRows) {
        loopSecondaryLabel->setBounds(loopX, boxY + primaryHeight, boxWidth, secondaryHeight);
        loopSecondaryLabel->setVisible(true);
    } else {
        loopSecondaryLabel->setVisible(false);
    }

    // Tempo and quantize layout
    auto tempoY = tempoArea.getCentreY() - 15;
    auto tempoX = tempoArea.getX() + 10;

    tempoDisplay->setBounds(tempoX, tempoY, 70, 30);

    int stackX = tempoX + 75;
    int stackButtonSize = 14;
    int stackTop = tempoY + 1;
    tempoIncreaseButton->setBounds(stackX, stackTop, stackButtonSize, stackButtonSize);
    tempoDecreaseButton->setBounds(stackX, stackTop + stackButtonSize, stackButtonSize,
                                   stackButtonSize);

    quantizeCombo->setBounds(tempoX + 100, tempoY, 70, 30);
    metronomeButton->setBounds(tempoX + 180, tempoY, 35, 30);
    snapButton->setBounds(tempoX + 220, tempoY, 50, 30);
}

juce::Rectangle<int> TransportPanel::getTransportControlsArea() const {
    return getLocalBounds().removeFromLeft(270);
}

juce::Rectangle<int> TransportPanel::getTimeDisplayArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(270);
    return bounds.removeFromLeft(520);  // 3 boxes @ 160 + spacing
}

juce::Rectangle<int> TransportPanel::getTempoQuantizeArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(790);  // 270 + 520
    return bounds;
}

void TransportPanel::setupTransportButtons() {
    // Play button
    playButton =
        std::make_unique<SvgButton>("Play", BinaryData::play_off_svg, BinaryData::play_off_svgSize,
                                    BinaryData::play_on_svg, BinaryData::play_on_svgSize);
    playButton->onClick = [this]() {
        isPlaying = !isPlaying;
        if (isPlaying) {
            isPaused = false;
            if (onPlay)
                onPlay();
        } else {
            if (onStop)
                onStop();
        }
        playButton->setActive(isPlaying);
        repaint();
    };
    addAndMakeVisible(*playButton);

    // Stop button
    stopButton =
        std::make_unique<SvgButton>("Stop", BinaryData::stop_off_svg, BinaryData::stop_off_svgSize,
                                    BinaryData::stop_on_svg, BinaryData::stop_on_svgSize);
    stopButton->onClick = [this]() {
        isPlaying = false;
        isPaused = false;
        isRecording = false;
        playButton->setActive(false);
        recordButton->setActive(false);
        if (onStop)
            onStop();
        repaint();
    };
    addAndMakeVisible(*stopButton);

    // Record button
    recordButton = std::make_unique<SvgButton>(
        "Record", BinaryData::record_off_svg, BinaryData::record_off_svgSize,
        BinaryData::record_on_svg, BinaryData::record_on_svgSize);
    recordButton->onClick = [this]() {
        isRecording = !isRecording;
        recordButton->setActive(isRecording);
        if (isRecording && onRecord) {
            onRecord();
        }
        repaint();
    };
    addAndMakeVisible(*recordButton);

    // Pause button
    pauseButton = std::make_unique<SvgButton>(
        "Pause", BinaryData::pause_off_svg, BinaryData::pause_off_svgSize, BinaryData::pause_on_svg,
        BinaryData::pause_on_svgSize);
    pauseButton->onClick = [this]() {
        if (isPlaying) {
            isPaused = !isPaused;
            pauseButton->setActive(isPaused);
            if (onPause)
                onPause();
        }
        repaint();
    };
    addAndMakeVisible(*pauseButton);

    // Loop button
    loopButton =
        std::make_unique<SvgButton>("Loop", BinaryData::loop_off_svg, BinaryData::loop_off_svgSize,
                                    BinaryData::loop_on_svg, BinaryData::loop_on_svgSize);
    loopButton->onClick = [this]() {
        isLooping = !isLooping;
        loopButton->setActive(isLooping);
        if (onLoop)
            onLoop(isLooping);
    };
    addAndMakeVisible(*loopButton);
}

void TransportPanel::setupTimeDisplayBoxes() {
    auto setupLabel = [this](std::unique_ptr<juce::Label>& label, juce::Colour textColor,
                             float fontSize, bool isPrimary) {
        label = std::make_unique<juce::Label>();
        label->setText("", juce::dontSendNotification);
        label->setFont(FontManager::getInstance().getUIFont(fontSize));
        label->setColour(juce::Label::textColourId, textColor);
        label->setColour(juce::Label::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        label->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*label);
    };

    // Playhead - white/primary color
    setupLabel(playheadPrimaryLabel, DarkTheme::getTextColour(), 14.0f, true);
    setupLabel(playheadSecondaryLabel, DarkTheme::getSecondaryTextColour(), 11.0f, false);

    // Selection - blue accent
    setupLabel(selectionPrimaryLabel, DarkTheme::getColour(DarkTheme::ACCENT_BLUE), 14.0f, true);
    setupLabel(selectionSecondaryLabel, DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f),
               11.0f, false);

    // Loop - green/loop marker color
    setupLabel(loopPrimaryLabel, DarkTheme::getColour(DarkTheme::LOOP_MARKER), 14.0f, true);
    setupLabel(loopSecondaryLabel, DarkTheme::getColour(DarkTheme::LOOP_MARKER).darker(0.3f), 11.0f,
               false);

    // Initialize displays
    setPlayheadPosition(0.0);
}

void TransportPanel::setupTempoAndQuantize() {
    // Tempo decrease button
    tempoDecreaseButton =
        std::make_unique<SvgButton>("Decrease", BinaryData::remove_svg, BinaryData::remove_svgSize);
    styleTransportButton(*tempoDecreaseButton, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    tempoDecreaseButton->onClick = [this]() { adjustTempo(-1.0); };
    addAndMakeVisible(*tempoDecreaseButton);

    // Tempo display
    tempoDisplay = std::make_unique<juce::Label>();
    tempoDisplay->setFont(FontManager::getInstance().getTimeFont(18.0f));
    tempoDisplay->setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    tempoDisplay->setColour(juce::Label::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    tempoDisplay->setColour(juce::Label::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    tempoDisplay->setJustificationType(juce::Justification::centred);
    tempoDisplay->setEditable(true);
    tempoDisplay->onTextChange = [this]() {
        double newTempo = tempoDisplay->getText().getDoubleValue();
        if (newTempo >= 20.0 && newTempo <= 999.0) {
            currentTempo = newTempo;
            if (onTempoChange)
                onTempoChange(currentTempo);
        }
        updateTempoDisplay();
    };
    addAndMakeVisible(*tempoDisplay);
    updateTempoDisplay();

    // Tempo increase button
    tempoIncreaseButton =
        std::make_unique<SvgButton>("Increase", BinaryData::add_svg, BinaryData::add_svgSize);
    styleTransportButton(*tempoIncreaseButton, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    tempoIncreaseButton->onClick = [this]() { adjustTempo(1.0); };
    addAndMakeVisible(*tempoIncreaseButton);

    // Quantize combo
    quantizeCombo = std::make_unique<juce::ComboBox>();
    quantizeCombo->addItem("Off", 1);
    quantizeCombo->addItem("1/4", 2);
    quantizeCombo->addItem("1/8", 3);
    quantizeCombo->addItem("1/16", 4);
    quantizeCombo->addItem("1/32", 5);
    quantizeCombo->setSelectedId(2);
    addAndMakeVisible(*quantizeCombo);

    // Metronome button
    metronomeButton = std::make_unique<SvgButton>("Metronome", BinaryData::metronome_svg,
                                                  BinaryData::metronome_svgSize);
    styleTransportButton(*metronomeButton, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    metronomeButton->onClick = [this]() {
        bool newState = !metronomeButton->isActive();
        metronomeButton->setActive(newState);
        if (onMetronomeToggle)
            onMetronomeToggle(newState);
    };
    addAndMakeVisible(*metronomeButton);

    // Snap button (text-based toggle)
    snapButton = std::make_unique<juce::TextButton>("SNAP");
    snapButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    snapButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    snapButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    snapButton->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    snapButton->setClickingTogglesState(true);
    snapButton->setToggleState(isSnapEnabled, juce::dontSendNotification);
    snapButton->onClick = [this]() {
        isSnapEnabled = snapButton->getToggleState();
        if (onSnapToggle)
            onSnapToggle(isSnapEnabled);
    };
    addAndMakeVisible(*snapButton);
}

void TransportPanel::setTransportEnabled(bool enabled) {
    playButton->setEnabled(enabled);
    stopButton->setEnabled(enabled);
    recordButton->setEnabled(enabled);
    pauseButton->setEnabled(enabled);

    // Visual feedback - dim buttons when disabled
    float alpha = enabled ? 1.0f : 0.4f;
    playButton->setAlpha(alpha);
    stopButton->setAlpha(alpha);
    recordButton->setAlpha(alpha);
    pauseButton->setAlpha(alpha);
}

void TransportPanel::updateTempoDisplay() {
    tempoDisplay->setText(juce::String(currentTempo, 1), juce::dontSendNotification);
}

void TransportPanel::adjustTempo(double delta) {
    currentTempo = juce::jlimit(20.0, 999.0, currentTempo + delta);
    updateTempoDisplay();
    if (onTempoChange)
        onTempoChange(currentTempo);
}

void TransportPanel::styleTransportButton(SvgButton& button, juce::Colour accentColor) {
    button.setActiveColor(accentColor);
    button.setPressedColor(accentColor);
    button.setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    button.setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
}

// Formatting helpers
juce::String TransportPanel::formatPositionBarsBeats(double seconds) const {
    if (seconds < 0)
        return "-";

    double secondsPerBeat = 60.0 / currentTempo;
    double totalBeats = seconds / secondsPerBeat;

    int bars = static_cast<int>(totalBeats / timeSignatureNumerator) + 1;
    double beatInBar = std::fmod(totalBeats, static_cast<double>(timeSignatureNumerator));
    int beat = static_cast<int>(beatInBar) + 1;

    // Subdivision (16ths within beat) and ticks
    double beatFraction = std::fmod(beatInBar, 1.0);
    int subdivision = static_cast<int>(beatFraction * 4) + 1;              // 1-4 (16th notes)
    int ticks = static_cast<int>(std::fmod(beatFraction * 4, 1.0) * 100);  // 00-99

    return juce::String::formatted("%d.%d.%d.%02d", bars, beat, subdivision, ticks);
}

juce::String TransportPanel::formatPositionSeconds(double seconds) const {
    if (seconds < 0)
        return "-";

    int minutes = static_cast<int>(seconds) / 60;
    double secs = std::fmod(seconds, 60.0);
    return juce::String::formatted("%d:%05.2f", minutes, secs);
}

juce::String TransportPanel::formatRangeBarsBeats(double startTime, double endTime) const {
    if (startTime < 0 || endTime <= startTime)
        return "-";

    double secondsPerBeat = 60.0 / currentTempo;

    // Start position (bars.beats.subdivision)
    double startBeats = startTime / secondsPerBeat;
    int startBars = static_cast<int>(startBeats / timeSignatureNumerator) + 1;
    double startBeatInBar = std::fmod(startBeats, static_cast<double>(timeSignatureNumerator));
    int startBeat = static_cast<int>(startBeatInBar) + 1;
    int startSub = static_cast<int>(std::fmod(startBeatInBar, 1.0) * 4) + 1;

    // End position (bars.beats.subdivision)
    double endBeats = endTime / secondsPerBeat;
    int endBars = static_cast<int>(endBeats / timeSignatureNumerator) + 1;
    double endBeatInBar = std::fmod(endBeats, static_cast<double>(timeSignatureNumerator));
    int endBeat = static_cast<int>(endBeatInBar) + 1;
    int endSub = static_cast<int>(std::fmod(endBeatInBar, 1.0) * 4) + 1;

    // Length in bars.beats.subdivision
    double lengthBeats = (endTime - startTime) / secondsPerBeat;
    int lenBars = static_cast<int>(lengthBeats / timeSignatureNumerator);
    double lenBeatFrac = std::fmod(lengthBeats, static_cast<double>(timeSignatureNumerator));
    int lenBeat = static_cast<int>(lenBeatFrac);
    int lenSub = static_cast<int>(std::fmod(lenBeatFrac, 1.0) * 4);

    juce::String lenStr;
    if (lenBars > 0) {
        lenStr = juce::String::formatted("%d.%d.%d", lenBars, lenBeat, lenSub);
    } else if (lenBeat > 0) {
        lenStr = juce::String::formatted("%d.%d", lenBeat, lenSub);
    } else {
        lenStr = juce::String::formatted(".%d", lenSub);
    }

    return juce::String::formatted("%d.%d.%d - %d.%d.%d - %s", startBars, startBeat, startSub,
                                   endBars, endBeat, endSub, lenStr.toRawUTF8());
}

juce::String TransportPanel::formatRangeSeconds(double startTime, double endTime) const {
    if (startTime < 0 || endTime <= startTime)
        return "-";

    double length = endTime - startTime;

    // Format start
    int startMin = static_cast<int>(startTime) / 60;
    double startSec = std::fmod(startTime, 60.0);

    // Format end
    int endMin = static_cast<int>(endTime) / 60;
    double endSec = std::fmod(endTime, 60.0);

    // Format length
    juce::String lenStr;
    if (length >= 60.0) {
        int lenMin = static_cast<int>(length) / 60;
        double lenSec = std::fmod(length, 60.0);
        lenStr = juce::String::formatted("%d:%04.1f", lenMin, lenSec);
    } else {
        lenStr = juce::String::formatted("%.1fs", length);
    }

    return juce::String::formatted("%d:%04.1f-%d:%04.1f-%s", startMin, startSec, endMin, endSec,
                                   lenStr.toRawUTF8());
}

void TransportPanel::updateDisplayBox(juce::Label* primary, juce::Label* secondary,
                                      const juce::String& barsText, const juce::String& secsText,
                                      bool showBothRows) {
    auto& config = Config::getInstance();
    bool defaultBarsBeats = config.getTransportDefaultBarsBeats();

    if (showBothRows) {
        // Primary = default format, Secondary = alternate
        if (defaultBarsBeats) {
            primary->setText(barsText, juce::dontSendNotification);
            secondary->setText(secsText, juce::dontSendNotification);
        } else {
            primary->setText(secsText, juce::dontSendNotification);
            secondary->setText(barsText, juce::dontSendNotification);
        }
    } else {
        // Single row - use default format
        if (defaultBarsBeats) {
            primary->setText(barsText, juce::dontSendNotification);
        } else {
            primary->setText(secsText, juce::dontSendNotification);
        }
    }
}

void TransportPanel::setPlayheadPosition(double positionInSeconds) {
    cachedPlayheadPosition = positionInSeconds;

    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();

    juce::String barsText = formatPositionBarsBeats(positionInSeconds);
    juce::String secsText = formatPositionSeconds(positionInSeconds);

    updateDisplayBox(playheadPrimaryLabel.get(), playheadSecondaryLabel.get(), barsText, secsText,
                     showBothRows);
}

void TransportPanel::setTimeSelection(double startTime, double endTime, bool hasSelection) {
    cachedSelectionStart = startTime;
    cachedSelectionEnd = endTime;
    cachedSelectionActive = hasSelection;

    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();

    juce::String barsText = hasSelection ? formatRangeBarsBeats(startTime, endTime) : "-";
    juce::String secsText = hasSelection ? formatRangeSeconds(startTime, endTime) : "-";

    updateDisplayBox(selectionPrimaryLabel.get(), selectionSecondaryLabel.get(), barsText, secsText,
                     showBothRows);
}

void TransportPanel::setLoopRegion(double startTime, double endTime, bool loopEnabled) {
    cachedLoopStart = startTime;
    cachedLoopEnd = endTime;
    cachedLoopEnabled = loopEnabled;

    // Sync loop button state
    if (isLooping != loopEnabled) {
        isLooping = loopEnabled;
        loopButton->setActive(isLooping);
    }

    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();

    bool hasLoop = startTime >= 0 && endTime > startTime;
    juce::String barsText = hasLoop ? formatRangeBarsBeats(startTime, endTime) : "-";
    juce::String secsText = hasLoop ? formatRangeSeconds(startTime, endTime) : "-";

    updateDisplayBox(loopPrimaryLabel.get(), loopSecondaryLabel.get(), barsText, secsText,
                     showBothRows);

    // Update color based on enabled state
    juce::Colour loopColor = loopEnabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER)
                                         : DarkTheme::getColour(DarkTheme::TEXT_DIM);
    loopPrimaryLabel->setColour(juce::Label::textColourId, loopColor);
    loopSecondaryLabel->setColour(juce::Label::textColourId, loopColor.darker(0.3f));
}

void TransportPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = numerator;
    timeSignatureDenominator = denominator;

    // Refresh all displays with new time signature
    setPlayheadPosition(cachedPlayheadPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
}

void TransportPanel::setTempo(double bpm) {
    currentTempo = juce::jlimit(20.0, 999.0, bpm);
    updateTempoDisplay();

    // Refresh all displays with new tempo
    setPlayheadPosition(cachedPlayheadPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
}

void TransportPanel::setPlaybackState(bool playing) {
    if (isPlaying != playing) {
        isPlaying = playing;
        playButton->setActive(isPlaying);
    }
}

void TransportPanel::setSnapEnabled(bool enabled) {
    if (isSnapEnabled != enabled) {
        isSnapEnabled = enabled;
        snapButton->setToggleState(enabled, juce::dontSendNotification);
    }
}

}  // namespace magda
