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

    // Transport controls layout — order: Home, Prev, Play, Stop, Rec, Loop, Next, PunchIn, PunchOut
    auto buttonMargin = 4;
    auto buttonHeight = transportArea.getHeight() - buttonMargin * 2;
    auto buttonWidth = 30;
    auto buttonY = buttonMargin;
    auto buttonSpacing = 2;

    auto x = transportArea.getX() + 6;

    homeButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    prevButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    playButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    stopButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    recordButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    loopButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    nextButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    punchInButton->setBounds(x, buttonY, buttonWidth, buttonHeight);
    x += buttonWidth + buttonSpacing;

    punchOutButton->setBounds(x, buttonY, buttonWidth, buttonHeight);

    // Pause button — hidden but still functional via callbacks
    pauseButton->setBounds(0, 0, 0, 0);
    pauseButton->setVisible(false);

    // Time display boxes layout
    int boxHeight = 26;
    int boxSpacing = 8;

    int startX = timeArea.getX() + 10;
    int centerY = timeArea.getCentreY();
    int boxY = centerY - boxHeight / 2;

    // Playhead position (editable BarsBeatsTicksLabel)
    int playheadWidth = 110;
    playheadPositionLabel->setBounds(startX, boxY, playheadWidth, boxHeight);

    // Selection (read-only labels)
    int selX = startX + playheadWidth + boxSpacing;
    int selWidth = 150;
    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();
    int primaryHeight = showBothRows ? 16 : boxHeight;
    int secondaryHeight = showBothRows ? 14 : 0;
    int totalBoxHeight = primaryHeight + secondaryHeight;
    int selBoxY = centerY - totalBoxHeight / 2;

    selectionPrimaryLabel->setBounds(selX, selBoxY, selWidth, primaryHeight);
    if (showBothRows) {
        selectionSecondaryLabel->setBounds(selX, selBoxY + primaryHeight, selWidth,
                                           secondaryHeight);
        selectionSecondaryLabel->setVisible(true);
    } else {
        selectionSecondaryLabel->setVisible(false);
    }

    // Loop start + end (editable BarsBeatsTicksLabels)
    int loopX = selX + selWidth + boxSpacing;
    int loopLabelWidth = 95;
    loopStartLabel->setBounds(loopX, boxY, loopLabelWidth, boxHeight);
    loopEndLabel->setBounds(loopX + loopLabelWidth + 4, boxY, loopLabelWidth, boxHeight);

    // Punch start/end — stacked two rows
    int punchX = loopX + loopLabelWidth * 2 + 4 + boxSpacing;
    int punchLabelWidth = 95;
    int punchRowHeight = 22;
    int punchRowGap = 2;
    int punchTotalHeight = punchRowHeight * 2 + punchRowGap;
    int punchTopY = centerY - punchTotalHeight / 2;

    punchStartLabel->setBounds(punchX, punchTopY, punchLabelWidth, punchRowHeight);
    punchEndLabel->setBounds(punchX, punchTopY + punchRowHeight + punchRowGap, punchLabelWidth,
                             punchRowHeight);

    // Tempo and quantize layout
    auto tempoY = tempoArea.getCentreY() - 13;
    auto tempoX = tempoArea.getX() + 10;

    tempoLabel->setBounds(tempoX, tempoY, 80, 26);

    quantizeCombo->setBounds(tempoX + 88, tempoY, 60, 26);
    metronomeButton->setBounds(tempoX + 156, tempoY, 30, 26);
    snapButton->setBounds(tempoX + 192, tempoY, 45, 26);
}

juce::Rectangle<int> TransportPanel::getTransportControlsArea() const {
    // 9 buttons * 30px + 8 * 2px spacing + 12px padding = 298px
    return getLocalBounds().removeFromLeft(300);
}

juce::Rectangle<int> TransportPanel::getTimeDisplayArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(300);
    return bounds.removeFromLeft(700);
}

juce::Rectangle<int> TransportPanel::getTempoQuantizeArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(1000);  // 300 + 700
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

    // Home button
    homeButton = std::make_unique<SvgButton>(
        "Home", BinaryData::rewind_off_svg, BinaryData::rewind_off_svgSize,
        BinaryData::rewind_on_svg, BinaryData::rewind_on_svgSize);
    homeButton->onClick = [this]() {
        if (onGoHome)
            onGoHome();
    };
    addAndMakeVisible(*homeButton);

    // Prev button
    prevButton =
        std::make_unique<SvgButton>("Prev", BinaryData::prev_off_svg, BinaryData::prev_off_svgSize,
                                    BinaryData::prev_on_svg, BinaryData::prev_on_svgSize);
    prevButton->onClick = [this]() {
        if (onGoToPrev)
            onGoToPrev();
    };
    addAndMakeVisible(*prevButton);

    // Next button
    nextButton =
        std::make_unique<SvgButton>("Next", BinaryData::next_off_svg, BinaryData::next_off_svgSize,
                                    BinaryData::next_on_svg, BinaryData::next_on_svgSize);
    nextButton->onClick = [this]() {
        if (onGoToNext)
            onGoToNext();
    };
    addAndMakeVisible(*nextButton);

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

    // Punch In button
    punchInButton = std::make_unique<SvgButton>("PunchIn", BinaryData::punchin_svg,
                                                BinaryData::punchin_svgSize);
    styleTransportButton(*punchInButton, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    punchInButton->setOriginalColor(juce::Colour(0xFFB3B3B3));
    punchInButton->setBorderColor(DarkTheme::getColour(DarkTheme::SEPARATOR));
    punchInButton->setBorderThickness(1.0f);
    punchInButton->onClick = [this]() {
        isPunchEnabled = !isPunchEnabled;
        punchInButton->setActive(isPunchEnabled);
        punchOutButton->setActive(isPunchEnabled);
        if (onPunchToggle)
            onPunchToggle(isPunchEnabled);
    };
    addAndMakeVisible(*punchInButton);

    // Punch Out button (visual pair, toggles same state)
    punchOutButton = std::make_unique<SvgButton>("PunchOut", BinaryData::punchout_svg,
                                                 BinaryData::punchout_svgSize);
    styleTransportButton(*punchOutButton, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    punchOutButton->setOriginalColor(juce::Colour(0xFFB3B3B3));
    punchOutButton->setBorderColor(DarkTheme::getColour(DarkTheme::SEPARATOR));
    punchOutButton->setBorderThickness(1.0f);
    punchOutButton->onClick = [this]() {
        isPunchEnabled = !isPunchEnabled;
        punchInButton->setActive(isPunchEnabled);
        punchOutButton->setActive(isPunchEnabled);
        if (onPunchToggle)
            onPunchToggle(isPunchEnabled);
    };
    addAndMakeVisible(*punchOutButton);
}

void TransportPanel::setupTimeDisplayBoxes() {
    // Playhead position — editable BarsBeatsTicksLabel
    playheadPositionLabel = std::make_unique<BarsBeatsTicksLabel>();
    playheadPositionLabel->setRange(0.0, 100000.0, 0.0);
    playheadPositionLabel->setBarsBeatsIsPosition(true);
    playheadPositionLabel->setDoubleClickResetsValue(false);
    playheadPositionLabel->onValueChange = [this]() {
        double beats = playheadPositionLabel->getValue();
        if (onPlayheadEdit)
            onPlayheadEdit(beats);
    };
    addAndMakeVisible(*playheadPositionLabel);

    // Selection — read-only labels
    auto setupLabel = [this](std::unique_ptr<juce::Label>& label, juce::Colour textColor,
                             float fontSize) {
        label = std::make_unique<juce::Label>();
        label->setText("", juce::dontSendNotification);
        label->setFont(FontManager::getInstance().getUIFont(fontSize));
        label->setColour(juce::Label::textColourId, textColor);
        label->setColour(juce::Label::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        label->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*label);
    };

    setupLabel(selectionPrimaryLabel, DarkTheme::getColour(DarkTheme::ACCENT_BLUE), 14.0f);
    setupLabel(selectionSecondaryLabel, DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f),
               11.0f);

    // Loop start — editable BarsBeatsTicksLabel
    loopStartLabel = std::make_unique<BarsBeatsTicksLabel>();
    loopStartLabel->setRange(0.0, 100000.0, 0.0);
    loopStartLabel->setBarsBeatsIsPosition(true);
    loopStartLabel->setDoubleClickResetsValue(false);
    loopStartLabel->onValueChange = [this]() {
        double startBeats = loopStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onLoopRegionEdit)
            onLoopRegionEdit(startSeconds, cachedLoopEnd);
    };
    addAndMakeVisible(*loopStartLabel);

    // Loop end — editable BarsBeatsTicksLabel
    loopEndLabel = std::make_unique<BarsBeatsTicksLabel>();
    loopEndLabel->setRange(0.0, 100000.0, 0.0);
    loopEndLabel->setBarsBeatsIsPosition(true);
    loopEndLabel->setDoubleClickResetsValue(false);
    loopEndLabel->onValueChange = [this]() {
        double endBeats = loopEndLabel->getValue();
        double endSeconds = (endBeats * 60.0) / currentTempo;
        if (onLoopRegionEdit)
            onLoopRegionEdit(cachedLoopStart, endSeconds);
    };
    addAndMakeVisible(*loopEndLabel);

    // Punch start — editable BarsBeatsTicksLabel
    punchStartLabel = std::make_unique<BarsBeatsTicksLabel>();
    punchStartLabel->setRange(0.0, 100000.0, 0.0);
    punchStartLabel->setBarsBeatsIsPosition(true);
    punchStartLabel->setDoubleClickResetsValue(false);
    punchStartLabel->onValueChange = [this]() {
        double startBeats = punchStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onPunchRegionEdit)
            onPunchRegionEdit(startSeconds, cachedPunchEnd);
    };
    addAndMakeVisible(*punchStartLabel);

    // Punch end — editable BarsBeatsTicksLabel
    punchEndLabel = std::make_unique<BarsBeatsTicksLabel>();
    punchEndLabel->setRange(0.0, 100000.0, 0.0);
    punchEndLabel->setBarsBeatsIsPosition(true);
    punchEndLabel->setDoubleClickResetsValue(false);
    punchEndLabel->onValueChange = [this]() {
        double endBeats = punchEndLabel->getValue();
        double endSeconds = (endBeats * 60.0) / currentTempo;
        if (onPunchRegionEdit)
            onPunchRegionEdit(cachedPunchStart, endSeconds);
    };
    addAndMakeVisible(*punchEndLabel);

    // Initialize displays
    setPlayheadPosition(0.0);
}

void TransportPanel::setupTempoAndQuantize() {
    // Tempo — DraggableValueLabel (Raw format with suffix)
    tempoLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    tempoLabel->setRange(20.0, 999.0, 120.0);
    tempoLabel->setValue(currentTempo, juce::dontSendNotification);
    tempoLabel->setSuffix(" bpm");
    tempoLabel->setDecimalPlaces(1);
    tempoLabel->setDoubleClickResetsValue(false);
    tempoLabel->setSnapToInteger(true);
    tempoLabel->onValueChange = [this]() {
        currentTempo = tempoLabel->getValue();
        if (onTempoChange)
            onTempoChange(currentTempo);
    };
    addAndMakeVisible(*tempoLabel);

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
    homeButton->setEnabled(enabled);
    prevButton->setEnabled(enabled);
    nextButton->setEnabled(enabled);
    punchInButton->setEnabled(enabled);
    punchOutButton->setEnabled(enabled);

    // Visual feedback - dim buttons when disabled
    float alpha = enabled ? 1.0f : 0.4f;
    playButton->setAlpha(alpha);
    stopButton->setAlpha(alpha);
    recordButton->setAlpha(alpha);
    pauseButton->setAlpha(alpha);
    homeButton->setAlpha(alpha);
    prevButton->setAlpha(alpha);
    nextButton->setAlpha(alpha);
    punchInButton->setAlpha(alpha);
    punchOutButton->setAlpha(alpha);
}

void TransportPanel::styleTransportButton(SvgButton& button, juce::Colour accentColor) {
    button.setActiveColor(accentColor);
    button.setPressedColor(accentColor);
    button.setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    button.setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
}

void TransportPanel::setPlayheadPosition(double positionInSeconds) {
    cachedPlayheadPosition = positionInSeconds;

    // Convert seconds to beats
    double beats = (positionInSeconds * currentTempo) / 60.0;
    playheadPositionLabel->setValue(beats, juce::dontSendNotification);
}

void TransportPanel::setTimeSelection(double startTime, double endTime, bool hasSelection) {
    cachedSelectionStart = startTime;
    cachedSelectionEnd = endTime;
    cachedSelectionActive = hasSelection;

    auto& config = Config::getInstance();
    bool showBothRows = config.getTransportShowBothFormats();

    if (hasSelection) {
        // Format selection as bars.beats range
        double secondsPerBeat = 60.0 / currentTempo;

        double startBeats = startTime / secondsPerBeat;
        int startBars = static_cast<int>(startBeats / timeSignatureNumerator) + 1;
        int startBeat =
            static_cast<int>(std::fmod(startBeats, static_cast<double>(timeSignatureNumerator))) +
            1;
        int startSub = static_cast<int>(std::fmod(startBeats, 1.0) * 4) + 1;

        double endBeats = endTime / secondsPerBeat;
        int endBars = static_cast<int>(endBeats / timeSignatureNumerator) + 1;
        int endBeat =
            static_cast<int>(std::fmod(endBeats, static_cast<double>(timeSignatureNumerator))) + 1;
        int endSub = static_cast<int>(std::fmod(endBeats, 1.0) * 4) + 1;

        juce::String barsText = juce::String::formatted("%d.%d.%d - %d.%d.%d", startBars, startBeat,
                                                        startSub, endBars, endBeat, endSub);

        int startMin = static_cast<int>(startTime) / 60;
        double startSec = std::fmod(startTime, 60.0);
        int endMin = static_cast<int>(endTime) / 60;
        double endSec = std::fmod(endTime, 60.0);
        juce::String secsText =
            juce::String::formatted("%d:%04.1f - %d:%04.1f", startMin, startSec, endMin, endSec);

        bool defaultBarsBeats = config.getTransportDefaultBarsBeats();
        if (showBothRows) {
            if (defaultBarsBeats) {
                selectionPrimaryLabel->setText(barsText, juce::dontSendNotification);
                selectionSecondaryLabel->setText(secsText, juce::dontSendNotification);
            } else {
                selectionPrimaryLabel->setText(secsText, juce::dontSendNotification);
                selectionSecondaryLabel->setText(barsText, juce::dontSendNotification);
            }
        } else {
            selectionPrimaryLabel->setText(defaultBarsBeats ? barsText : secsText,
                                           juce::dontSendNotification);
        }
    } else {
        selectionPrimaryLabel->setText("-", juce::dontSendNotification);
        selectionSecondaryLabel->setText("-", juce::dontSendNotification);
    }
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

    bool hasLoop = startTime >= 0 && endTime > startTime;
    if (hasLoop) {
        double startBeats = (startTime * currentTempo) / 60.0;
        double endBeats = (endTime * currentTempo) / 60.0;
        loopStartLabel->setValue(startBeats, juce::dontSendNotification);
        loopEndLabel->setValue(endBeats, juce::dontSendNotification);
    } else {
        loopStartLabel->setValue(0.0, juce::dontSendNotification);
        loopEndLabel->setValue(0.0, juce::dontSendNotification);
    }

    // Update enabled appearance
    loopStartLabel->setEnabled(loopEnabled);
    loopEndLabel->setEnabled(loopEnabled);
    float alpha = loopEnabled ? 1.0f : 0.5f;
    loopStartLabel->setAlpha(alpha);
    loopEndLabel->setAlpha(alpha);
}

void TransportPanel::setPunchRegion(double startTime, double endTime, bool punchEnabled) {
    cachedPunchStart = startTime;
    cachedPunchEnd = endTime;
    cachedPunchEnabled = punchEnabled;

    // Sync punch button state
    if (isPunchEnabled != punchEnabled) {
        isPunchEnabled = punchEnabled;
        punchInButton->setActive(isPunchEnabled);
        punchOutButton->setActive(isPunchEnabled);
    }

    bool hasPunch = startTime >= 0 && endTime > startTime;
    if (hasPunch) {
        double startBeats = (startTime * currentTempo) / 60.0;
        double endBeats = (endTime * currentTempo) / 60.0;
        punchStartLabel->setValue(startBeats, juce::dontSendNotification);
        punchEndLabel->setValue(endBeats, juce::dontSendNotification);
    } else {
        punchStartLabel->setValue(0.0, juce::dontSendNotification);
        punchEndLabel->setValue(0.0, juce::dontSendNotification);
    }

    // Update enabled appearance
    punchStartLabel->setEnabled(punchEnabled);
    punchEndLabel->setEnabled(punchEnabled);
    float alpha = punchEnabled ? 1.0f : 0.5f;
    punchStartLabel->setAlpha(alpha);
    punchEndLabel->setAlpha(alpha);
}

void TransportPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = numerator;
    timeSignatureDenominator = denominator;

    // Update beats per bar on BarsBeatsTicksLabels
    playheadPositionLabel->setBeatsPerBar(numerator);
    loopStartLabel->setBeatsPerBar(numerator);
    loopEndLabel->setBeatsPerBar(numerator);
    punchStartLabel->setBeatsPerBar(numerator);
    punchEndLabel->setBeatsPerBar(numerator);

    // Refresh all displays with new time signature
    setPlayheadPosition(cachedPlayheadPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
    setPunchRegion(cachedPunchStart, cachedPunchEnd, cachedPunchEnabled);
}

void TransportPanel::setTempo(double bpm) {
    currentTempo = juce::jlimit(20.0, 999.0, bpm);
    tempoLabel->setValue(currentTempo, juce::dontSendNotification);

    // Refresh all displays with new tempo
    setPlayheadPosition(cachedPlayheadPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
    setPunchRegion(cachedPunchStart, cachedPunchEnd, cachedPunchEnabled);
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
