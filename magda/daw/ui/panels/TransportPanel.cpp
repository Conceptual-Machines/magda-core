#include "TransportPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"

namespace magda {

TransportPanel::TransportPanel() {
    setupTransportButtons();
    setupTimeDisplayBoxes();
    setupTempoAndQuantize();
}

TransportPanel::~TransportPanel() {
    autoGridButton->setLookAndFeel(nullptr);
    snapButton->setLookAndFeel(nullptr);
}

void TransportPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRANSPORT_BACKGROUND));

    // Draw subtle borders between sections
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

    auto bounds = getLocalBounds();
    auto transportArea = getTransportControlsArea();
    auto metroBpmArea = getMetronomeBpmArea();
    auto timeArea = getTimeDisplayArea();

    // Vertical separators
    g.drawVerticalLine(transportArea.getRight(), bounds.getY(), bounds.getBottom());
    g.drawVerticalLine(metroBpmArea.getRight(), bounds.getY(), bounds.getBottom());
    g.drawVerticalLine(timeArea.getRight(), bounds.getY(), bounds.getBottom());

    // Draw wrapper borders around each stacked pair in time display area
    auto drawGroupWrapper = [&](juce::Rectangle<int> wrapperArea, const juce::String& groupName,
                                juce::Colour groupColour) {
        auto wrapperBounds = wrapperArea.expanded(2, 0).toFloat();

        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(wrapperBounds, 2.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(wrapperBounds.reduced(0.5f), 2.0f, 1.0f);

        // Group label at top-right
        g.setColour(groupColour.withAlpha(0.5f));
        g.setFont(FontManager::getInstance().getUIFont(7.0f));
        g.drawText(groupName, wrapperBounds.toNearestInt().reduced(2, 1),
                   juce::Justification::topRight, false);
    };

    drawGroupWrapper(selectionStartLabel->getBounds().getUnion(selectionEndLabel->getBounds()),
                     "SEL", DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    drawGroupWrapper(loopStartLabel->getBounds().getUnion(loopEndLabel->getBounds()), "LOOP",
                     DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    drawGroupWrapper(playheadPositionLabel->getBounds().getUnion(editCursorLabel->getBounds()),
                     "CUR", DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    drawGroupWrapper(punchInButton->getBounds()
                         .getUnion(punchStartLabel->getBounds())
                         .getUnion(punchOutButton->getBounds())
                         .getUnion(punchEndLabel->getBounds()),
                     "", DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    drawGroupWrapper(tempoLabel->getBounds()
                         .getUnion(timeSignatureLabel->getBounds())
                         .getUnion(metronomeButton->getBounds()),
                     "", DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    drawGroupWrapper(gridNumeratorLabel->getBounds().getUnion(gridDenominatorLabel->getBounds()),
                     "", DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    drawGroupWrapper(autoGridButton->getBounds().getUnion(snapButton->getBounds()), "",
                     DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));

    // Bottom border for visual separation from content below
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void TransportPanel::resized() {
    auto transportArea = getTransportControlsArea();
    auto timeArea = getTimeDisplayArea();
    auto tempoArea = getTempoQuantizeArea();

    // Transport controls layout — order: Home, Prev, Play, Stop, Rec, Loop, Next
    auto buttonMargin = 3;
    auto buttonSize = transportArea.getHeight() - buttonMargin * 2;
    auto buttonY = buttonMargin;
    auto buttonSpacing = 1;

    auto x = transportArea.getX() + 6;

    homeButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    prevButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    playButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    stopButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    recordButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    loopButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    nextButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing + 3;  // extra gap before punch group

    // Stacked row metrics (shared by punch group and time display groups)
    int boxWidth = 130;
    int rowHeight = (buttonSize - 4) / 2;  // 1px top + 1px bottom padding
    int rowY1 = buttonY + 1;               // 1px top padding
    int rowY2 = rowY1 + rowHeight + 2;     // 2px gap between rows

    // Punch in/out — stacked box with labels + icon buttons overlapping right edge
    int punchIconSize = rowHeight / 2 + 2;

    punchStartLabel->setBounds(x, rowY1, boxWidth, rowHeight);
    punchEndLabel->setBounds(x, rowY2, boxWidth, rowHeight);

    // Place buttons overlapping the right side of labels, then raise z-order
    int btnX = x + boxWidth - punchIconSize - 4;
    punchInButton->setBounds(btnX, rowY1 + (rowHeight - punchIconSize) / 2, punchIconSize,
                             punchIconSize);
    punchOutButton->setBounds(btnX, rowY2 + (rowHeight - punchIconSize) / 2, punchIconSize,
                              punchIconSize);
    punchInButton->toFront(false);
    punchOutButton->toFront(false);

    // Pause button — hidden but still functional via callbacks
    pauseButton->setBounds(0, 0, 0, 0);
    pauseButton->setVisible(false);

    // Time display boxes layout — 3 stacked groups with 1px top/bottom padding
    int groupSpacing = 8;

    int startX = timeArea.getX() + 10;

    // Group 1 — Selection: start (top), end (bottom)
    selectionStartLabel->setBounds(startX, rowY1, boxWidth, rowHeight);
    selectionEndLabel->setBounds(startX, rowY2, boxWidth, rowHeight);

    int loopX = startX + boxWidth + groupSpacing;

    // Group 2 — Loop: start (top), end (bottom)
    loopStartLabel->setBounds(loopX, rowY1, boxWidth, rowHeight);
    loopEndLabel->setBounds(loopX, rowY2, boxWidth, rowHeight);

    int cursorX = loopX + boxWidth + groupSpacing;

    // Group 3 — Cursors: playhead (top), edit cursor (bottom)
    playheadPositionLabel->setBounds(cursorX, rowY1, boxWidth, rowHeight);
    editCursorLabel->setBounds(cursorX, rowY2, boxWidth, rowHeight);

    // Metronome + BPM — stacked box: tempo (top), time sig + metronome icon (bottom)
    auto metroBpmArea = getMetronomeBpmArea();
    int metroBoxWidth = 70;
    int metroX = metroBpmArea.getX() + (metroBpmArea.getWidth() - metroBoxWidth) / 2;
    int metroIconSize = rowHeight;

    tempoLabel->setBounds(metroX, rowY1, metroBoxWidth, rowHeight);
    timeSignatureLabel->setBounds(metroX, rowY2, metroBoxWidth, rowHeight);

    int metroBtnX = metroX + metroBoxWidth - metroIconSize;
    metronomeButton->setBounds(metroBtnX, rowY2 + (rowHeight - metroIconSize) / 2, metroIconSize,
                               metroIconSize);
    metronomeButton->setAlpha(0.6f);
    metronomeButton->toFront(false);

    // Grid quantize layout — two stacked boxes side by side
    int gridX = tempoArea.getX() + 6;
    int numDenWidth = 30;
    int gridGap = 4;
    int btnWidth = 44;

    // Left box: numerator (top) / denominator (bottom)
    gridNumeratorLabel->setBounds(gridX, rowY1, numDenWidth, rowHeight);
    gridDenominatorLabel->setBounds(gridX, rowY2, numDenWidth, rowHeight);

    // Right box: AUTO (top) / SNAP (bottom)
    int gridBtnX = gridX + numDenWidth + gridGap;
    autoGridButton->setBounds(gridBtnX, rowY1, btnWidth, rowHeight);
    snapButton->setBounds(gridBtnX, rowY2, btnWidth, rowHeight);

    // Hide slash label (no longer needed in this layout)
    gridSlashLabel->setBounds(0, 0, 0, 0);
    gridSlashLabel->setVisible(false);
}

juce::Rectangle<int> TransportPanel::getTransportControlsArea() const {
    // 7 square buttons + punch stacked box (boxWidth=130)
    int buttonSize = getHeight() - 6;
    int boxWidth = 130;
    // 6px left pad + 7 buttons + 6*1px spacing + 3px gap + punch box + 6px right pad
    int width = 6 + 7 * buttonSize + 6 + 3 + boxWidth + 6;
    return getLocalBounds().removeFromLeft(width);
}

juce::Rectangle<int> TransportPanel::getMetronomeBpmArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(getTransportControlsArea().getWidth());
    return bounds.removeFromLeft(90);
}

juce::Rectangle<int> TransportPanel::getTimeDisplayArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(getTransportControlsArea().getWidth() + 106);
    return bounds.removeFromLeft(440);
}

juce::Rectangle<int> TransportPanel::getTempoQuantizeArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(getTransportControlsArea().getWidth() + 106 + 440);
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

    // Punch In button (dual-icon: off/on)
    punchInButton =
        std::make_unique<SvgButton>("PunchIn", BinaryData::punchin_svg, BinaryData::punchin_svgSize,
                                    BinaryData::punchin_on_svg, BinaryData::punchin_on_svgSize);
    punchInButton->onClick = [this]() {
        isPunchInEnabled = !isPunchInEnabled;
        punchInButton->setActive(isPunchInEnabled);
        updatePunchLabelColors();
        if (onPunchInToggle)
            onPunchInToggle(isPunchInEnabled);
    };
    addAndMakeVisible(*punchInButton);

    // Punch Out button (dual-icon: off/on, independent toggle)
    punchOutButton = std::make_unique<SvgButton>(
        "PunchOut", BinaryData::punchout_svg, BinaryData::punchout_svgSize,
        BinaryData::punchout_on_svg, BinaryData::punchout_on_svgSize);
    punchOutButton->onClick = [this]() {
        isPunchOutEnabled = !isPunchOutEnabled;
        punchOutButton->setActive(isPunchOutEnabled);
        updatePunchLabelColors();
        if (onPunchOutToggle)
            onPunchOutToggle(isPunchOutEnabled);
    };
    addAndMakeVisible(*punchOutButton);
}

void TransportPanel::setupTimeDisplayBoxes() {
    auto setupBBTLabel = [this](std::unique_ptr<BarsBeatsTicksLabel>& label,
                                const juce::String& overlay, juce::Colour textColour) {
        label = std::make_unique<BarsBeatsTicksLabel>();
        label->setRange(0.0, 100000.0, 0.0);
        label->setBarsBeatsIsPosition(true);
        label->setDoubleClickResetsValue(false);
        label->setDrawBackground(false);
        label->setOverlayLabel(overlay);
        label->setTextColour(textColour);
        addAndMakeVisible(*label);
    };

    auto accentBlue = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    auto accentGreen = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    auto accentOrange = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);

    // Selection start/end
    setupBBTLabel(selectionStartLabel, "S", accentBlue);
    selectionStartLabel->onValueChange = [this]() {
        double startBeats = selectionStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onTimeSelectionEdit)
            onTimeSelectionEdit(startSeconds, cachedSelectionEnd);
    };

    setupBBTLabel(selectionEndLabel, "E", accentBlue);
    selectionEndLabel->onValueChange = [this]() {
        double endBeats = selectionEndLabel->getValue();
        double endSeconds = (endBeats * 60.0) / currentTempo;
        if (onTimeSelectionEdit)
            onTimeSelectionEdit(cachedSelectionStart, endSeconds);
    };

    // Loop start/end
    setupBBTLabel(loopStartLabel, "S", accentGreen);
    loopStartLabel->onValueChange = [this]() {
        double startBeats = loopStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onLoopRegionEdit)
            onLoopRegionEdit(startSeconds, cachedLoopEnd);
    };

    setupBBTLabel(loopEndLabel, "E", accentGreen);
    loopEndLabel->onValueChange = [this]() {
        double endBeats = loopEndLabel->getValue();
        double endSeconds = (endBeats * 60.0) / currentTempo;
        if (onLoopRegionEdit)
            onLoopRegionEdit(cachedLoopStart, endSeconds);
    };

    // Playhead position
    setupBBTLabel(playheadPositionLabel, "P", accentOrange);
    playheadPositionLabel->onValueChange = [this]() {
        double beats = playheadPositionLabel->getValue();
        if (onPlayheadEdit)
            onPlayheadEdit(beats);
    };

    // Edit cursor
    setupBBTLabel(editCursorLabel, "E", accentOrange);
    editCursorLabel->onValueChange = [this]() {
        double beats = editCursorLabel->getValue();
        double seconds = (beats * 60.0) / currentTempo;
        if (onEditCursorEdit)
            onEditCursorEdit(seconds);
    };

    // Punch start/end — stacked box in time display area
    auto accentPurple = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);

    setupBBTLabel(punchStartLabel, "I", accentPurple);
    punchStartLabel->onValueChange = [this]() {
        double startBeats = punchStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onPunchRegionEdit)
            onPunchRegionEdit(startSeconds, cachedPunchEnd);
    };

    setupBBTLabel(punchEndLabel, "O", accentPurple);
    punchEndLabel->onValueChange = [this]() {
        double endBeats = punchEndLabel->getValue();
        double endSeconds = (endBeats * 60.0) / currentTempo;
        if (onPunchRegionEdit)
            onPunchRegionEdit(cachedPunchStart, endSeconds);
    };

    updatePunchLabelColors();

    // Initialize displays
    setPlayheadPosition(0.0);
    setEditCursorPosition(0.0);
}

void TransportPanel::setupTempoAndQuantize() {
    // Tempo — DraggableValueLabel (Raw format with suffix)
    tempoLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    tempoLabel->setRange(20.0, 999.0, 120.0);
    tempoLabel->setValue(currentTempo, juce::dontSendNotification);
    tempoLabel->setSuffix("");
    tempoLabel->setDecimalPlaces(2);
    tempoLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    tempoLabel->setShowFillIndicator(false);
    tempoLabel->setFontSize(14.0f);
    tempoLabel->setDoubleClickResetsValue(false);
    tempoLabel->setSnapToInteger(true);
    tempoLabel->setDrawBorder(false);
    tempoLabel->onValueChange = [this]() {
        currentTempo = tempoLabel->getValue();
        if (onTempoChange)
            onTempoChange(currentTempo);
    };
    addAndMakeVisible(*tempoLabel);

    // Time signature label
    timeSignatureLabel = std::make_unique<juce::Label>();
    timeSignatureLabel->setText("4/4", juce::dontSendNotification);
    timeSignatureLabel->setFont(FontManager::getInstance().getUIFont(14.0f));
    timeSignatureLabel->setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSignatureLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    timeSignatureLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*timeSignatureLabel);

    // Auto grid toggle button (like SNAP button)
    autoGridButton = std::make_unique<juce::TextButton>("AUTO");
    autoGridButton->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    autoGridButton->setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.3f));
    autoGridButton->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoGridButton->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    autoGridButton->setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    autoGridButton->setWantsKeyboardFocus(false);
    autoGridButton->setClickingTogglesState(true);
    autoGridButton->setToggleState(isAutoGrid, juce::dontSendNotification);
    autoGridButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    autoGridButton->onClick = [this]() {
        isAutoGrid = autoGridButton->getToggleState();
        // Enable/disable numerator/denominator labels
        gridNumeratorLabel->setEnabled(!isAutoGrid);
        gridDenominatorLabel->setEnabled(!isAutoGrid);
        gridNumeratorLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
        gridDenominatorLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
        if (onGridQuantizeChange)
            onGridQuantizeChange(isAutoGrid, gridNumerator, gridDenominator);
    };
    addAndMakeVisible(*autoGridButton);

    // Grid numerator (Integer format, range 1-32)
    gridNumeratorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    gridNumeratorLabel->setRange(1.0, 128.0, 1.0);
    gridNumeratorLabel->setValue(static_cast<double>(gridNumerator), juce::dontSendNotification);
    gridNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    gridNumeratorLabel->setShowFillIndicator(false);
    gridNumeratorLabel->setFontSize(12.0f);
    gridNumeratorLabel->setDoubleClickResetsValue(true);
    gridNumeratorLabel->setDrawBorder(false);
    gridNumeratorLabel->setEnabled(!isAutoGrid);
    gridNumeratorLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
    gridNumeratorLabel->onValueChange = [this]() {
        gridNumerator = static_cast<int>(gridNumeratorLabel->getValue());
        if (!isAutoGrid && onGridQuantizeChange)
            onGridQuantizeChange(isAutoGrid, gridNumerator, gridDenominator);
    };
    addAndMakeVisible(*gridNumeratorLabel);

    // Grid slash label
    gridSlashLabel = std::make_unique<juce::Label>();
    gridSlashLabel->setText("/", juce::dontSendNotification);
    gridSlashLabel->setFont(FontManager::getInstance().getUIFont(12.0f));
    gridSlashLabel->setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    gridSlashLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    gridSlashLabel->setJustificationType(juce::Justification::centred);
    gridSlashLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
    addAndMakeVisible(*gridSlashLabel);

    // Grid denominator (Integer format, constrained to powers of 2)
    gridDenominatorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    gridDenominatorLabel->setRange(2.0, 32.0, 4.0);
    gridDenominatorLabel->setValue(static_cast<double>(gridDenominator),
                                   juce::dontSendNotification);
    gridDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    gridDenominatorLabel->setShowFillIndicator(false);
    gridDenominatorLabel->setFontSize(12.0f);
    gridDenominatorLabel->setDoubleClickResetsValue(true);
    gridDenominatorLabel->setDrawBorder(false);
    gridDenominatorLabel->setEnabled(!isAutoGrid);
    gridDenominatorLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
    gridDenominatorLabel->onValueChange = [this]() {
        // Constrain to nearest allowed value (multiples of 2 and 3)
        static constexpr int allowed[] = {2, 3, 4, 6, 8, 12, 16, 24, 32};
        static constexpr int numAllowed = 9;
        int raw = static_cast<int>(std::round(gridDenominatorLabel->getValue()));
        int best = allowed[0];
        int bestDist = std::abs(raw - best);
        for (int i = 1; i < numAllowed; ++i) {
            int dist = std::abs(raw - allowed[i]);
            if (dist < bestDist) {
                bestDist = dist;
                best = allowed[i];
            }
        }
        gridDenominator = best;
        gridDenominatorLabel->setValue(static_cast<double>(gridDenominator),
                                       juce::dontSendNotification);
        if (!isAutoGrid && onGridQuantizeChange)
            onGridQuantizeChange(isAutoGrid, gridNumerator, gridDenominator);
    };
    addAndMakeVisible(*gridDenominatorLabel);

    // Metronome button
    metronomeButton = std::make_unique<SvgButton>("Metronome", BinaryData::metronome_svg,
                                                  BinaryData::metronome_svgSize);
    styleTransportButton(*metronomeButton, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    metronomeButton->setNormalColor(juce::Colour(0xFFBCBCBC));
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
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.3f));
    snapButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    snapButton->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    snapButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    snapButton->setWantsKeyboardFocus(false);
    snapButton->setClickingTogglesState(true);
    snapButton->setToggleState(isSnapEnabled, juce::dontSendNotification);
    snapButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
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

void TransportPanel::setEditCursorPosition(double positionInSeconds) {
    cachedEditCursorPosition = positionInSeconds;

    double beats = (positionInSeconds * currentTempo) / 60.0;
    editCursorLabel->setValue(beats, juce::dontSendNotification);
}

void TransportPanel::setTimeSelection(double startTime, double endTime, bool hasSelection) {
    cachedSelectionStart = startTime;
    cachedSelectionEnd = endTime;
    cachedSelectionActive = hasSelection;

    if (hasSelection) {
        double startBeats = (startTime * currentTempo) / 60.0;
        double endBeats = (endTime * currentTempo) / 60.0;
        selectionStartLabel->setValue(startBeats, juce::dontSendNotification);
        selectionEndLabel->setValue(endBeats, juce::dontSendNotification);
    } else {
        selectionStartLabel->setValue(0.0, juce::dontSendNotification);
        selectionEndLabel->setValue(0.0, juce::dontSendNotification);
    }

    selectionStartLabel->setEnabled(hasSelection);
    selectionEndLabel->setEnabled(hasSelection);
    float alpha = hasSelection ? 1.0f : 0.5f;
    selectionStartLabel->setAlpha(alpha);
    selectionEndLabel->setAlpha(alpha);
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

void TransportPanel::setPunchRegion(double startTime, double endTime, bool punchInEnabled,
                                    bool punchOutEnabled) {
    cachedPunchStart = startTime;
    cachedPunchEnd = endTime;
    cachedPunchInEnabled = punchInEnabled;
    cachedPunchOutEnabled = punchOutEnabled;

    // Sync punch button states independently
    if (isPunchInEnabled != punchInEnabled) {
        isPunchInEnabled = punchInEnabled;
        punchInButton->setActive(isPunchInEnabled);
    }
    if (isPunchOutEnabled != punchOutEnabled) {
        isPunchOutEnabled = punchOutEnabled;
        punchOutButton->setActive(isPunchOutEnabled);
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

    updatePunchLabelColors();
}

void TransportPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = numerator;
    timeSignatureDenominator = denominator;

    // Update time signature display
    timeSignatureLabel->setText(juce::String(numerator) + "/" + juce::String(denominator),
                                juce::dontSendNotification);

    // Update beats per bar on BarsBeatsTicksLabels
    playheadPositionLabel->setBeatsPerBar(numerator);
    editCursorLabel->setBeatsPerBar(numerator);
    selectionStartLabel->setBeatsPerBar(numerator);
    selectionEndLabel->setBeatsPerBar(numerator);
    loopStartLabel->setBeatsPerBar(numerator);
    loopEndLabel->setBeatsPerBar(numerator);
    punchStartLabel->setBeatsPerBar(numerator);
    punchEndLabel->setBeatsPerBar(numerator);

    // Refresh all displays with new time signature
    setPlayheadPosition(cachedPlayheadPosition);
    setEditCursorPosition(cachedEditCursorPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
    setPunchRegion(cachedPunchStart, cachedPunchEnd, cachedPunchInEnabled, cachedPunchOutEnabled);
}

void TransportPanel::setTempo(double bpm) {
    currentTempo = juce::jlimit(20.0, 999.0, bpm);
    tempoLabel->setValue(currentTempo, juce::dontSendNotification);

    // Refresh all displays with new tempo
    setPlayheadPosition(cachedPlayheadPosition);
    setEditCursorPosition(cachedEditCursorPosition);
    setTimeSelection(cachedSelectionStart, cachedSelectionEnd, cachedSelectionActive);
    setLoopRegion(cachedLoopStart, cachedLoopEnd, cachedLoopEnabled);
    setPunchRegion(cachedPunchStart, cachedPunchEnd, cachedPunchInEnabled, cachedPunchOutEnabled);
}

void TransportPanel::setPlaybackState(bool playing) {
    if (isPlaying != playing) {
        isPlaying = playing;
        playButton->setActive(isPlaying);
    }
}

void TransportPanel::setGridQuantize(bool autoGrid, int numerator, int denominator, bool isBars) {
    isAutoGrid = autoGrid;
    gridNumerator = numerator;
    gridDenominator = denominator;

    autoGridButton->setToggleState(autoGrid, juce::dontSendNotification);
    gridNumeratorLabel->setValue(static_cast<double>(numerator), juce::dontSendNotification);

    if (isBars) {
        gridDenominatorLabel->setTextOverride("B");
    } else {
        gridDenominatorLabel->clearTextOverride();
        gridDenominatorLabel->setValue(static_cast<double>(denominator),
                                       juce::dontSendNotification);
    }

    // Enable/disable labels based on autoGrid state
    gridNumeratorLabel->setEnabled(!autoGrid);
    gridDenominatorLabel->setEnabled(!autoGrid);
    gridNumeratorLabel->setAlpha(autoGrid ? 0.4f : 1.0f);
    gridDenominatorLabel->setAlpha(autoGrid ? 0.4f : 1.0f);
}

void TransportPanel::setSnapEnabled(bool enabled) {
    if (isSnapEnabled != enabled) {
        isSnapEnabled = enabled;
        snapButton->setToggleState(enabled, juce::dontSendNotification);
    }
}

void TransportPanel::updatePunchLabelColors() {
    auto activeColor = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    auto inactiveColor = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    // Punch start label color matches punch in button state
    punchStartLabel->setTextColour(isPunchInEnabled ? activeColor : inactiveColor);
    punchStartLabel->setAlpha(isPunchInEnabled ? 1.0f : 0.5f);

    // Punch end label color matches punch out button state
    punchEndLabel->setTextColour(isPunchOutEnabled ? activeColor : inactiveColor);
    punchEndLabel->setAlpha(isPunchOutEnabled ? 1.0f : 0.5f);
}

}  // namespace magda
