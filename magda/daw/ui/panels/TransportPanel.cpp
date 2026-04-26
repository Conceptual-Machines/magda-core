#include "TransportPanel.hpp"

#include "../../audio/QwertyMidiKeyboard.hpp"
#include "../components/common/QwertyKeyboardPopup.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "core/StringTable.hpp"

namespace magda {

TransportPanel::TransportPanel() {
    setupTransportButtons();
    setupTimeDisplayBoxes();
    setupTempoAndQuantize();

    // CPU usage — title label + value label stacked
    cpuTitleLabel = std::make_unique<juce::Label>("cpuTitle", tr("transport.cpu.cpu"));
    cpuTitleLabel->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    cpuTitleLabel->setFont(FontManager::getInstance().getUIFont(8.0f));
    cpuTitleLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuTitleLabel);

    cpuValueLabel = std::make_unique<juce::Label>("cpuValue", "0%");
    cpuValueLabel->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    cpuValueLabel->setFont(FontManager::getInstance().getMonoFont(11.0f));
    cpuValueLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuValueLabel);

    // Automation write indicator label — purple text, visible only when write mode on
    automationWriteLabel = std::make_unique<juce::Label>("automationWrite", "AUTOMATION WRITE");
    automationWriteLabel->setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    automationWriteLabel->setColour(juce::Label::backgroundColourId,
                                    juce::Colours::transparentBlack);
    automationWriteLabel->setFont(FontManager::getInstance().getUIFont(10.0f).boldened());
    automationWriteLabel->setJustificationType(juce::Justification::centredRight);
    addChildComponent(*automationWriteLabel);

    // Overflow menu button — hosts items that don't fit at narrow widths.
    overflowButton =
        std::make_unique<SvgButton>("More", BinaryData::menu_svg, BinaryData::menu_svgSize);
    overflowButton->setNormalColor(DarkTheme::getSecondaryTextColour());
    overflowButton->setActiveColor(juce::Colours::white);
    overflowButton->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.6f));
    overflowButton->onClick = [this]() { showOverflowMenu(); };
    addChildComponent(*overflowButton);
}

TransportPanel::~TransportPanel() {
    autoGridButton->setLookAndFeel(nullptr);
    snapButton->setLookAndFeel(nullptr);
}

void TransportPanel::paintOverChildren(juce::Graphics& g) {
    if (!automationWriteButton)
        return;

    juce::String letter;
    switch (automationMode_) {
        case AutomationMode::Write:
            letter = "W";
            break;
        case AutomationMode::Touch:
            letter = "T";
            break;
        case AutomationMode::Latch:
            letter = "L";
            break;
        case AutomationMode::Off:
            letter = "W";
            break;  // shouldn't happen — Off is disarmed
    }

    constexpr int kModeLetterStripPercent = 27;
    auto btnBounds = automationWriteButton->getBounds();
    // Bottom strip of the button, nudged upward — the original SVG glyph sat
    // a touch high inside the icon and looked better that way.
    auto labelArea =
        btnBounds.removeFromBottom(btnBounds.getHeight() * kModeLetterStripPercent / 100);
    labelArea.translate(1, -3);

    // When active, the button background fills with the purple from
    // automation_on.svg — drawing the letter in ACCENT_PURPLE made it
    // invisible against that fill. Use white in the active state to match
    // the icon foreground.
    juce::Colour textColour = isAutomationWriteEnabled
                                  ? juce::Colours::white
                                  : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    g.setColour(textColour);
    g.setFont(FontManager::getInstance().getUIFontBold(6.0f));
    g.drawText(letter, labelArea, juce::Justification::centred);
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

    if (selLoopTimesVisible_) {
        drawGroupWrapper(selectionStartLabel->getBounds().getUnion(selectionEndLabel->getBounds()),
                         "SEL", DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        drawGroupWrapper(loopStartLabel->getBounds().getUnion(loopEndLabel->getBounds()), "LOOP",
                         DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    }
    drawGroupWrapper(playheadPositionLabel->getBounds().getUnion(editCursorLabel->getBounds()),
                     "CUR", DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    if (punchVisible_) {
        drawGroupWrapper(punchInButton->getBounds()
                             .getUnion(punchStartLabel->getBounds())
                             .getUnion(punchOutButton->getBounds())
                             .getUnion(punchEndLabel->getBounds()),
                         "", DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    }
    drawGroupWrapper(tempoLabel->getBounds()
                         .getUnion(timeSigNumeratorLabel->getBounds())
                         .getUnion(timeSigDenominatorLabel->getBounds())
                         .getUnion(metronomeButton->getBounds()),
                     "", DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    if (gridVisible_) {
        drawGroupWrapper(
            gridNumeratorLabel->getBounds().getUnion(gridDenominatorLabel->getBounds()), "",
            DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        drawGroupWrapper(autoGridButton->getBounds().getUnion(snapButton->getBounds()), "",
                         DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    }

    // CPU frame — rounded rectangle matching transport group wrapper style.
    // Skipped entirely when the panel is too narrow to host the meter.
    if (cpuVisible_) {
        auto cpuArea = getCpuArea().reduced(4, 3);
        auto frameBounds = cpuArea.toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(frameBounds, 3.0f);

        // Separator line between header and value
        int headerHeight = juce::roundToInt(cpuArea.getHeight() * 0.25f);
        float sepY = cpuArea.getY() + headerHeight;

        // CPU usage fill bar in value area
        if (currentCpuUsage > 0.0f) {
            auto valueArea =
                juce::Rectangle<float>(frameBounds.getX() + 1, sepY + 1, frameBounds.getWidth() - 2,
                                       frameBounds.getBottom() - sepY - 2);
            float fillHeight = valueArea.getHeight() * currentCpuUsage;
            auto fillArea = valueArea.withTop(valueArea.getBottom() - fillHeight);

            juce::Colour fillColour;
            if (currentCpuUsage < 0.5f)
                fillColour = juce::Colour(0xFF55AA55).withAlpha(0.3f);
            else if (currentCpuUsage < 0.8f)
                fillColour = juce::Colour(0xFFAAAA55).withAlpha(0.3f);
            else
                fillColour = juce::Colour(0xFFAA5555).withAlpha(0.3f);

            g.setColour(fillColour);
            g.fillRect(fillArea);
        }

        // Frame border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(frameBounds.reduced(0.5f), 3.0f, 1.0f);

        // Separator line
        g.drawHorizontalLine(static_cast<int>(sepY), frameBounds.getX() + 1,
                             frameBounds.getRight() - 1);
    }

    // Bottom border for visual separation from content below
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void TransportPanel::resized() {
    // Flow layout: sections lay out left-to-right, right cluster from the
    // right edge inward. When total required width exceeds panel width,
    // sections collapse into the overflow popup in priority order.
    //
    // Priority (first-collapsed → last): right cluster (CPU + QWERTY),
    // grid quantize, punch box, loop/back-to-arr, navigation buttons
    // (home/prev/next), selection+loop time groups. Play/stop/record/
    // auto-write, tempo/BPM, and the playhead/edit cursor group are always
    // visible. Time displays are the most informative part of the panel so
    // they collapse last.

    const int W = getWidth();
    const int H = getHeight();

    const int buttonMargin = 3;
    const int buttonSize = juce::jmax(24, H - buttonMargin * 2);
    const int buttonY = buttonMargin;
    const int buttonSpacing = 1;
    const int rowHeight = (buttonSize - 4) / 2;
    const int rowY1 = buttonY + 1;
    const int rowY2 = rowY1 + rowHeight + 2;
    const int minGap = 8;

    auto lerpi = [](int lo, int hi, int inLo, int inHi, int x) {
        if (x <= inLo)
            return lo;
        if (x >= inHi)
            return hi;
        return lo + ((hi - lo) * (x - inLo)) / (inHi - inLo);
    };

    // Elastic time-box width + group spacing.
    const int boxWidth = lerpi(100, 130, 1100, 1280, W);
    const int groupSpacing = lerpi(4, 8, 1100, 1280, W);

    // Section width estimates (left-to-right flow).
    const int navGroupW =
        6 + 3 * buttonSize + 2 * buttonSpacing + buttonSpacing;            // home/prev/next + 6 pad
    const int coreGroupW = 4 * buttonSize + 3 * buttonSpacing;             // play/stop/rec/auto
    const int loopBackW = 2 * buttonSize + buttonSpacing + buttonSpacing;  // loop + backToArr
    const int punchSectionW = 3 + boxWidth + 3;                            // 3 pre/post padding
    const int metroSectionW = 90;
    const int timeGroupW = boxWidth + groupSpacing;
    const int cursorGroupW = 10 + boxWidth + 24;
    const int gridSectionW = 6 + 30 + 4 + 44;
    const int cpuW = 70;
    const int qwertyW = buttonSize;
    const int overflowW = buttonSize;
    // Right cluster when both visible: CPU + gap + QWERTY + 4 trailing.
    const int rightClusterW = cpuW + minGap + qwertyW + 4;
    // When collapsed the overflow button sits in place of the cluster.
    const int overflowSlotW = overflowW + 4;

    // Decide which sections fit. Total width needed = sum of every visible
    // section. Starts with full layout, then drops sections one tier at a
    // time until it fits.
    int required = navGroupW + coreGroupW + loopBackW + punchSectionW + metroSectionW +
                   2 * timeGroupW + cursorGroupW + gridSectionW + rightClusterW;

    bool rightClusterFits = (W >= required);
    if (!rightClusterFits) {
        required = required - rightClusterW + overflowSlotW;
    }
    gridVisible_ = (W >= required);
    if (!gridVisible_)
        required -= gridSectionW;
    punchVisible_ = (W >= required);
    if (!punchVisible_)
        required -= punchSectionW;
    loopBackVisible_ = (W >= required);
    if (!loopBackVisible_)
        required -= loopBackW;
    navButtonsVisible_ = (W >= required);
    if (!navButtonsVisible_)
        required -= navGroupW;
    selLoopTimesVisible_ = (W >= required);
    if (!selLoopTimesVisible_)
        required -= 2 * timeGroupW;

    cpuVisible_ = rightClusterFits;
    qwertyVisible_ = rightClusterFits;
    overflowVisible_ = !rightClusterFits || !gridVisible_ || !punchVisible_ ||
                       !selLoopTimesVisible_ || !loopBackVisible_ || !navButtonsVisible_;

    // Sync component visibility (bounds are skipped for hidden items so
    // toFront / z-order calls below don't touch stale rectangles).
    cpuTitleLabel->setVisible(cpuVisible_);
    cpuValueLabel->setVisible(cpuVisible_);
    qwertyKeyboardButton->setVisible(qwertyVisible_);
    overflowButton->setVisible(overflowVisible_);
    homeButton->setVisible(navButtonsVisible_);
    prevButton->setVisible(navButtonsVisible_);
    nextButton->setVisible(navButtonsVisible_);
    loopButton->setVisible(loopBackVisible_);
    backToArrangementButton->setVisible(loopBackVisible_);
    punchStartLabel->setVisible(punchVisible_);
    punchEndLabel->setVisible(punchVisible_);
    punchInButton->setVisible(punchVisible_);
    punchOutButton->setVisible(punchVisible_);
    selectionStartLabel->setVisible(selLoopTimesVisible_);
    selectionEndLabel->setVisible(selLoopTimesVisible_);
    loopStartLabel->setVisible(selLoopTimesVisible_);
    loopEndLabel->setVisible(selLoopTimesVisible_);
    gridNumeratorLabel->setVisible(gridVisible_);
    gridDenominatorLabel->setVisible(gridVisible_);
    autoGridButton->setVisible(gridVisible_);
    snapButton->setVisible(gridVisible_);

    // ---- Left-to-right flow ----
    int x = 0;

    // Navigation group
    if (navButtonsVisible_) {
        x += 6;
        homeButton->setBounds(x, buttonY, buttonSize, buttonSize);
        x += buttonSize + buttonSpacing;
        prevButton->setBounds(x, buttonY, buttonSize, buttonSize);
        x += buttonSize + buttonSpacing;
        nextButton->setBounds(x, buttonY, buttonSize, buttonSize);
        x += buttonSize + buttonSpacing;
    } else {
        x += 6;
    }

    // Core transport group — always visible
    playButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;
    stopButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;
    recordButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;
    automationWriteButton->setBounds(x, buttonY, buttonSize, buttonSize);
    x += buttonSize + buttonSpacing;

    // Loop + BackToArrangement
    if (loopBackVisible_) {
        loopButton->setBounds(x, buttonY, buttonSize, buttonSize);
        x += buttonSize + buttonSpacing;
        backToArrangementButton->setBounds(x, buttonY, buttonSize, buttonSize);
        x += buttonSize + buttonSpacing + 3;
    } else {
        x += 3;
    }

    // Punch box
    if (punchVisible_) {
        const int punchIconSize = rowHeight / 2 + 2;
        punchStartLabel->setBounds(x, rowY1, boxWidth, rowHeight);
        punchEndLabel->setBounds(x, rowY2, boxWidth, rowHeight);
        const int btnX = x + boxWidth - punchIconSize - 4;
        punchInButton->setBounds(btnX, rowY1 + (rowHeight - punchIconSize) / 2, punchIconSize,
                                 punchIconSize);
        punchOutButton->setBounds(btnX, rowY2 + (rowHeight - punchIconSize) / 2, punchIconSize,
                                  punchIconSize);
        punchInButton->toFront(false);
        punchOutButton->toFront(false);
        x += boxWidth + 3;
    }

    // Pause button — hidden but kept in tree for callback access
    pauseButton->setBounds(0, 0, 0, 0);
    pauseButton->setVisible(false);

    transportRight_ = x;

    // Metronome + BPM section (always visible — tempo is essential)
    {
        const int metroBoxWidth = 70;
        const int metroX = x + (metroSectionW - metroBoxWidth) / 2;
        const int metroIconSize = rowHeight;
        tempoLabel->setBounds(metroX, rowY1, metroBoxWidth, rowHeight);

        const int tsNumWidth = 24;
        const int tsDenWidth = 18;
        const int tsOverlap = 4;
        const int tsTotal = tsNumWidth + tsDenWidth - tsOverlap;
        const int tsX = metroX + (metroBoxWidth - tsTotal) / 2;
        timeSigNumeratorLabel->setBounds(tsX, rowY2, tsNumWidth, rowHeight);
        timeSigDenominatorLabel->setBounds(tsX + tsNumWidth - tsOverlap, rowY2, tsDenWidth,
                                           rowHeight);

        const int metroBtnX = metroX + metroBoxWidth - metroIconSize;
        metronomeButton->setBounds(metroBtnX, rowY2 + (rowHeight - metroIconSize) / 2,
                                   metroIconSize, metroIconSize);
        metronomeButton->setAlpha(0.6f);
        metronomeButton->toFront(false);
        x += metroSectionW;
    }
    metroRight_ = x;

    // Time display groups
    x += 10;  // start pad
    if (selLoopTimesVisible_) {
        selectionStartLabel->setBounds(x, rowY1, boxWidth, rowHeight);
        selectionEndLabel->setBounds(x, rowY2, boxWidth, rowHeight);
        x += boxWidth + groupSpacing;

        loopStartLabel->setBounds(x, rowY1, boxWidth, rowHeight);
        loopEndLabel->setBounds(x, rowY2, boxWidth, rowHeight);
        x += boxWidth + groupSpacing;
    }
    // Cursor group — always visible (playhead is essential)
    playheadPositionLabel->setBounds(x, rowY1, boxWidth, rowHeight);
    editCursorLabel->setBounds(x, rowY2, boxWidth, rowHeight);
    x += boxWidth + 24;  // trailing pad
    timeRight_ = x;

    // Grid quantize cluster
    int gridRight = x;
    if (gridVisible_) {
        const int numDenWidth = 30;
        const int gridGap = 4;
        const int btnWidth = 44;
        int gridX = x + 6;
        gridNumeratorLabel->setBounds(gridX, rowY1, numDenWidth, rowHeight);
        gridDenominatorLabel->setBounds(gridX, rowY2, numDenWidth, rowHeight);
        const int gridBtnX = gridX + numDenWidth + gridGap;
        autoGridButton->setBounds(gridBtnX, rowY1, btnWidth, rowHeight);
        snapButton->setBounds(gridBtnX, rowY2, btnWidth, rowHeight);
        gridRight = gridBtnX + btnWidth;
    }

    gridSlashLabel->setBounds(0, 0, 0, 0);
    gridSlashLabel->setVisible(false);

    // ---- Right-to-left cluster ----
    int rightCursor = W;
    if (overflowVisible_) {
        rightCursor -= overflowW + 4;
        overflowButton->setBounds(rightCursor, buttonY, overflowW, buttonSize);
    }

    auto cpuBounds = getCpuArea();  // empty when !cpuVisible_
    if (cpuVisible_) {
        rightCursor = cpuBounds.getX();
    }

    if (qwertyVisible_) {
        rightCursor -= qwertyW + 4;
        qwertyKeyboardButton->setBounds(rightCursor, buttonY, qwertyW, buttonSize);
    }

    // Automation write indicator fills the gap between grid cluster (or
    // playhead if grid hidden) and the leftmost right-cluster item.
    const int autoWriteLeft = gridRight + minGap;
    const int autoWriteRight = rightCursor - 4;
    autoWriteFits_ = autoWriteRight > autoWriteLeft;
    automationWriteLabel->setVisible(isAutomationWriteEnabled && autoWriteFits_);
    if (autoWriteFits_) {
        automationWriteLabel->setBounds(autoWriteLeft, 0, autoWriteRight - autoWriteLeft,
                                        getHeight());
    }

    // CPU frame interior
    if (cpuVisible_) {
        auto cpuArea = cpuBounds.reduced(4, 3);
        int headerHeight = juce::roundToInt(cpuArea.getHeight() * 0.20f);
        cpuTitleLabel->setBounds(cpuArea.removeFromTop(headerHeight));
        cpuValueLabel->setBounds(cpuArea);
    }
}

juce::Rectangle<int> TransportPanel::getTransportControlsArea() const {
    return getLocalBounds().withWidth(transportRight_);
}

juce::Rectangle<int> TransportPanel::getMetronomeBpmArea() const {
    return juce::Rectangle<int>(transportRight_, 0, metroRight_ - transportRight_, getHeight());
}

juce::Rectangle<int> TransportPanel::getTimeDisplayArea() const {
    return juce::Rectangle<int>(metroRight_, 0, timeRight_ - metroRight_, getHeight());
}

juce::Rectangle<int> TransportPanel::getTempoQuantizeArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(timeRight_);
    bounds.removeFromRight(getCpuArea().getWidth());
    return bounds;
}

juce::Rectangle<int> TransportPanel::getCpuArea() const {
    if (!cpuVisible_)
        return {};
    // When the overflow button is visible it sits to the right of the CPU
    // meter; push the CPU slot inward to leave room for it.
    auto bounds = getLocalBounds();
    if (overflowVisible_) {
        const int overflowWidth = juce::jmax(24, getHeight() - 6);
        bounds.removeFromRight(overflowWidth + 4);
    }
    return bounds.removeFromRight(70);
}

void TransportPanel::showOverflowMenu() {
    juce::PopupMenu menu;

    enum MenuId {
        IdQwerty = 1,
        IdLoop,
        IdBackToArr,
        IdPunchIn,
        IdPunchOut,
        IdAutoGrid,
        IdSnap,
        IdHome,
        IdPrev,
        IdNext,
    };

    // Always offered while overflow button is visible — right cluster always
    // collapses first so QWERTY and CPU are in the menu whenever it exists.
    menu.addItem(IdQwerty, "Virtual MIDI Keyboard", true, qwertyKeyboardButton->isActive());
    const juce::String cpuText =
        "CPU " + juce::String(juce::roundToInt(currentCpuUsage * 100.0f)) + "%";
    menu.addItem(99, cpuText, false, false);

    if (!loopBackVisible_) {
        menu.addSeparator();
        menu.addItem(IdLoop, "Loop", true, isLooping);
        menu.addItem(IdBackToArr, "Back to Arrangement");
    }
    if (!punchVisible_) {
        menu.addSeparator();
        menu.addItem(IdPunchIn, "Punch In", true, isPunchInEnabled);
        menu.addItem(IdPunchOut, "Punch Out", true, isPunchOutEnabled);
    }
    if (!gridVisible_) {
        menu.addSeparator();
        menu.addItem(IdAutoGrid, "Auto Grid", true, isAutoGrid);
        menu.addItem(IdSnap, "Snap", true, isSnapEnabled);
    }
    if (!navButtonsVisible_) {
        menu.addSeparator();
        menu.addItem(IdHome, "Go Home");
        menu.addItem(IdPrev, "Previous");
        menu.addItem(IdNext, "Next");
    }

    auto fire = [](SvgButton* b) {
        if (b && b->onClick)
            b->onClick();
    };
    auto fireTb = [](juce::TextButton* b) {
        if (b && b->onClick)
            b->onClick();
    };

    overflowButton->setActive(true);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(overflowButton.get()),
                       [this, fire, fireTb](int result) {
                           overflowButton->setActive(false);
                           switch (result) {
                               case IdQwerty:
                                   fire(qwertyKeyboardButton.get());
                                   break;
                               case IdLoop:
                                   fire(loopButton.get());
                                   break;
                               case IdBackToArr:
                                   fire(backToArrangementButton.get());
                                   break;
                               case IdPunchIn:
                                   fire(punchInButton.get());
                                   break;
                               case IdPunchOut:
                                   fire(punchOutButton.get());
                                   break;
                               case IdAutoGrid:
                                   fireTb(autoGridButton.get());
                                   break;
                               case IdSnap:
                                   fireTb(snapButton.get());
                                   break;
                               case IdHome:
                                   fire(homeButton.get());
                                   break;
                               case IdPrev:
                                   fire(prevButton.get());
                                   break;
                               case IdNext:
                                   fire(nextButton.get());
                                   break;
                               default:
                                   break;
                           }
                       });
}

void TransportPanel::setupTransportButtons() {
    // Play button
    playButton =
        std::make_unique<SvgButton>("Play", BinaryData::play_off_svg, BinaryData::play_off_svgSize,
                                    BinaryData::play_on_svg, BinaryData::play_on_svgSize);
    playButton->onClick = [this]() {
        DBG("[TransportPanel] playButton->onClick: isPlaying was "
            << (int)isPlaying << ", toggling to " << (int)!isPlaying);
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
        auto mousePos = juce::Desktop::getMousePosition();
        auto localPos = stopButton->getScreenBounds();
        bool mouseIsOver = stopButton->isMouseOver();
        DBG("[TransportPanel] stopButton->onClick mouseOver="
            << (int)mouseIsOver << " mouseScreen=(" << mousePos.x << "," << mousePos.y << ")"
            << " btnScreen=(" << localPos.getX() << "," << localPos.getY() << ","
            << localPos.getWidth() << "x" << localPos.getHeight() << ")");
        isPlaying = false;
        isPaused = false;
        isRecording = false;
        playButton->setActive(false);
        recordButton->setActive(false);
        // Pressing stop also disarms automation write mode, matching the
        // transport-centric mental model (stop = end of pass).
        if (isAutomationWriteEnabled) {
            isAutomationWriteEnabled = false;
            automationWriteButton->setActive(false);
            if (automationWriteLabel)
                automationWriteLabel->setVisible(false);
            if (onAutomationWriteToggle)
                onAutomationWriteToggle(false);
        }
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

    // Automation Write button — purple when enabled (write mode),
    // grey when disabled. Matches the purple automation accent used on
    // lane headers and control tints.
    automationWriteButton = std::make_unique<SvgButton>(
        "Automation Write", BinaryData::automation_off_svg, BinaryData::automation_off_svgSize,
        BinaryData::automation_on_svg, BinaryData::automation_on_svgSize);
    styleTransportButton(*automationWriteButton, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    automationWriteButton->setActive(false);
    automationWriteButton->onClick = [this]() {
        isAutomationWriteEnabled = !isAutomationWriteEnabled;
        automationWriteButton->setActive(isAutomationWriteEnabled);
        if (automationWriteLabel)
            automationWriteLabel->setVisible(isAutomationWriteEnabled);
        updateAutomationLabelText();
        if (onAutomationWriteToggle)
            onAutomationWriteToggle(isAutomationWriteEnabled);
        emitCurrentAutomationMode();
        repaint();
    };
    automationWriteButton->addMouseListener(this, false);
    addAndMakeVisible(*automationWriteButton);

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

    // Back to Arrangement button
    backToArrangementButton = std::make_unique<SvgButton>(
        "BackToArrangement", BinaryData::resume_svg, BinaryData::resume_svgSize,
        BinaryData::resume_on_svg, BinaryData::resume_on_svgSize);
    backToArrangementButton->onClick = [this]() {
        if (onBackToArrangement)
            onBackToArrangement();
    };
    addAndMakeVisible(*backToArrangementButton);

    // QWERTY MIDI keyboard toggle
    qwertyKeyboardButton = std::make_unique<SvgButton>(
        "QwertyKeyboard", BinaryData::midi_qwerty_off_svg, BinaryData::midi_qwerty_off_svgSize,
        BinaryData::midi_qwerty_on_svg, BinaryData::midi_qwerty_on_svgSize);
    qwertyKeyboardButton->onClick = [this]() {
        bool active = !qwertyKeyboardButton->isActive();
        qwertyKeyboardButton->setActive(active);
        if (onQwertyKeyboardToggled)
            onQwertyKeyboardToggled(active);
    };
    qwertyKeyboardButton->addMouseListener(this, false);
    addAndMakeVisible(*qwertyKeyboardButton);

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

    // Loop start/end — always enabled so interaction auto-enables looping
    auto enableLoopIfNeeded = [this]() {
        if (!isLooping) {
            isLooping = true;
            loopButton->setActive(true);
            auto green = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
            loopStartLabel->setTextColour(green);
            loopEndLabel->setTextColour(green);
            if (onLoop)
                onLoop(true);
        }
    };

    auto dimColour = DarkTheme::getColour(DarkTheme::TEXT_DIM);
    setupBBTLabel(loopStartLabel, "S", dimColour);
    loopStartLabel->onValueChange = [this, enableLoopIfNeeded]() {
        enableLoopIfNeeded();
        double startBeats = loopStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onLoopRegionEdit)
            onLoopRegionEdit(startSeconds, cachedLoopEnd);
    };

    setupBBTLabel(loopEndLabel, "E", dimColour);
    loopEndLabel->onValueChange = [this, enableLoopIfNeeded]() {
        enableLoopIfNeeded();
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

    // Time signature — numerator / denominator draggable labels
    timeSigNumeratorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    timeSigNumeratorLabel->setRange(1.0, 32.0, 4.0);
    timeSigNumeratorLabel->setValue(static_cast<double>(timeSignatureNumerator),
                                    juce::dontSendNotification);
    timeSigNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSigNumeratorLabel->setShowFillIndicator(false);
    timeSigNumeratorLabel->setFontSize(14.0f);
    timeSigNumeratorLabel->setDoubleClickResetsValue(true);
    timeSigNumeratorLabel->setDrawBorder(false);
    timeSigNumeratorLabel->setDrawBackground(false);
    timeSigNumeratorLabel->setSnapToInteger(true);
    timeSigNumeratorLabel->setSuffix("/");
    timeSigNumeratorLabel->setJustification(juce::Justification::centredRight);
    timeSigNumeratorLabel->onValueChange = [this]() {
        timeSignatureNumerator = static_cast<int>(std::round(timeSigNumeratorLabel->getValue()));
        if (onTimeSignatureChange)
            onTimeSignatureChange(timeSignatureNumerator, timeSignatureDenominator);
    };
    addAndMakeVisible(*timeSigNumeratorLabel);

    timeSigDenominatorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    timeSigDenominatorLabel->setRange(1.0, 32.0, 4.0);
    timeSigDenominatorLabel->setValue(static_cast<double>(timeSignatureDenominator),
                                      juce::dontSendNotification);
    timeSigDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSigDenominatorLabel->setShowFillIndicator(false);
    timeSigDenominatorLabel->setFontSize(14.0f);
    timeSigDenominatorLabel->setDoubleClickResetsValue(true);
    timeSigDenominatorLabel->setDrawBorder(false);
    timeSigDenominatorLabel->setDrawBackground(false);
    timeSigDenominatorLabel->setSnapToInteger(true);
    timeSigDenominatorLabel->setJustification(juce::Justification::centredLeft);
    timeSigDenominatorLabel->onValueChange = [this]() {
        timeSignatureDenominator =
            static_cast<int>(std::round(timeSigDenominatorLabel->getValue()));
        if (onTimeSignatureChange)
            onTimeSignatureChange(timeSignatureNumerator, timeSignatureDenominator);
    };
    addAndMakeVisible(*timeSigDenominatorLabel);

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

        // When switching to manual, seed from last auto value if it was a valid note fraction
        if (!isAutoGrid) {
            if (!lastAutoWasBars && lastAutoDenominator > 0) {
                gridNumerator = lastAutoNumerator;
                gridDenominator = lastAutoDenominator;
            } else {
                gridNumerator = 1;
                gridDenominator = 4;
            }
            gridNumeratorLabel->setValue(static_cast<double>(gridNumerator),
                                         juce::dontSendNotification);
            gridDenominatorLabel->clearTextOverride();
            gridDenominatorLabel->setValue(static_cast<double>(gridDenominator),
                                           juce::dontSendNotification);
        }

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
    gridNumeratorLabel->setSnapToInteger(true);
    gridNumeratorLabel->setEnabled(!isAutoGrid);
    gridNumeratorLabel->setAlpha(isAutoGrid ? 0.4f : 1.0f);
    gridNumeratorLabel->onValueChange = [this]() {
        gridNumerator = static_cast<int>(std::round(gridNumeratorLabel->getValue()));
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
    metronomeButton->addMouseListener(this, false);
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
    backToArrangementButton->setEnabled(enabled);
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
    backToArrangementButton->setAlpha(alpha);
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

    // Grey out when no valid loop region, green when active
    bool hasValidLoop = loopEnabled && hasLoop;
    auto colour = hasValidLoop ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN)
                               : DarkTheme::getColour(DarkTheme::TEXT_DIM);
    loopStartLabel->setTextColour(colour);
    loopEndLabel->setTextColour(colour);
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
    timeSigNumeratorLabel->setValue(static_cast<double>(numerator), juce::dontSendNotification);
    timeSigDenominatorLabel->setValue(static_cast<double>(denominator), juce::dontSendNotification);

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

void TransportPanel::setAutomationWriteEnabled(bool enabled) {
    if (isAutomationWriteEnabled != enabled) {
        isAutomationWriteEnabled = enabled;
        automationWriteButton->setActive(isAutomationWriteEnabled);
        if (automationWriteLabel)
            automationWriteLabel->setVisible(isAutomationWriteEnabled);
    }
}

void TransportPanel::setQwertyKeyboardEnabled(bool enabled) {
    if (qwertyKeyboardButton)
        qwertyKeyboardButton->setActive(enabled);
}

void TransportPanel::setPlaybackState(bool playing) {
    if (isPlaying != playing) {
        DBG("[TransportPanel] setPlaybackState: " << (int)isPlaying << " -> " << (int)playing);
        isPlaying = playing;
        playButton->setActive(isPlaying);
    }
}

void TransportPanel::setGridQuantize(bool autoGrid, int numerator, int denominator, bool isBars) {
    isAutoGrid = autoGrid;
    gridNumerator = numerator;
    gridDenominator = denominator;

    if (autoGrid) {
        lastAutoNumerator = numerator;
        lastAutoDenominator = denominator;
        lastAutoWasBars = isBars;
    }

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

void TransportPanel::setAnyTrackInSessionMode(bool anyInSession) {
    backToArrangementButton->setActive(anyInSession);
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

void TransportPanel::setCpuUsage(float usage) {
    float clamped = juce::jlimit(0.0f, 1.0f, usage);
    // Exponential moving average for stable display
    currentCpuUsage = currentCpuUsage * 0.7f + clamped * 0.3f;

    // Track peak with slow decay (resets after ~5 seconds of lower values)
    if (currentCpuUsage >= peakCpuUsage) {
        peakCpuUsage = currentCpuUsage;
        peakDecayCounter_ = 0;
    } else if (++peakDecayCounter_ > 10) {
        // ~5s at 500ms update interval
        peakCpuUsage = currentCpuUsage;
        peakDecayCounter_ = 0;
    }

    if (cpuValueLabel) {
        int avgPct = juce::roundToInt(currentCpuUsage * 100.0f);
        int peakPct = juce::roundToInt(peakCpuUsage * 100.0f);
        if (peakPct > avgPct + 2)
            cpuValueLabel->setText(juce::String(avgPct) + "/" + juce::String(peakPct) + "%",
                                   juce::dontSendNotification);
        else
            cpuValueLabel->setText(juce::String(avgPct) + "%", juce::dontSendNotification);
    }
    updateCpuTooltip();
    repaint(getCpuArea());
}

void TransportPanel::setXrunCount(int count) {
    currentXrunCount_ = count;
    updateCpuTooltip();
}

void TransportPanel::setAudioDeviceInfo(const juce::String& deviceName, double sampleRate,
                                        int bufferSize) {
    audioDeviceName_ = deviceName;
    audioSampleRate_ = sampleRate;
    audioBufferSize_ = bufferSize;
    updateCpuTooltip();
}

void TransportPanel::updateCpuTooltip() {
    juce::String tip;
    if (audioDeviceName_.isNotEmpty())
        tip << tr("transport.cpu.device") << ": " << audioDeviceName_ << "\n";
    if (audioSampleRate_ > 0)
        tip << tr("transport.cpu.sample_rate") << ": " << juce::String(audioSampleRate_ / 1000.0, 1)
            << " kHz\n";
    if (audioBufferSize_ > 0) {
        double latencyMs =
            (audioSampleRate_ > 0) ? (audioBufferSize_ / audioSampleRate_) * 1000.0 : 0.0;
        tip << tr("transport.cpu.buffer") << ": " << audioBufferSize_ << " samples";
        if (latencyMs > 0)
            tip << " (" << juce::String(latencyMs, 1) << " ms)";
        tip << "\n";
    }
    tip << tr("transport.cpu.cpu") << ": "
        << juce::String(juce::roundToInt(currentCpuUsage * 100.0f)) << "%";
    if (peakCpuUsage > currentCpuUsage + 0.02f)
        tip << " (" << tr("transport.cpu.peak") << " "
            << juce::String(juce::roundToInt(peakCpuUsage * 100.0f)) << "%)";
    if (currentXrunCount_ > 0)
        tip << "\n" << tr("transport.cpu.xruns") << ": " << currentXrunCount_;
    tip = tip.trimEnd();

    if (tip == lastTooltip_)
        return;
    lastTooltip_ = tip;

    if (cpuTitleLabel)
        cpuTitleLabel->setTooltip(tip);
    if (cpuValueLabel)
        cpuValueLabel->setTooltip(tip);
}

void TransportPanel::mouseDown(const juce::MouseEvent& e) {
    if (e.originalComponent == metronomeButton.get() && e.mods.isRightButtonDown()) {
        showCountInMenu();
    } else if (e.originalComponent == automationWriteButton.get() && e.mods.isRightButtonDown()) {
        showAutomationModeMenu();
    } else if (e.originalComponent == qwertyKeyboardButton.get() && e.mods.isRightButtonDown() &&
               qwertyKeyboard_ != nullptr) {
        // TODO: CallOutBox steals keyboard focus, which silences the
        // QwertyMidiKeyboard key listener while the popup is visible. The
        // popup's own setWantsKeyboardFocus(false) + addKeyListener(keyboard_)
        // don't restore routing — a proper fix probably needs a non-modal
        // floating window instead of a CallOutBox. For now the popup is
        // effectively a static layout reference; live key highlighting won't
        // update while it's open.
        auto popup = std::make_unique<QwertyKeyboardPopup>(*qwertyKeyboard_);
        auto area = qwertyKeyboardButton->getScreenBounds();
        juce::CallOutBox::launchAsynchronously(std::move(popup), area, nullptr);
    }
}

void TransportPanel::showCountInMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, tr("transport.count_in.off"), true, countInMode_ == 0);
    menu.addItem(5, tr("transport.count_in.1_beat"), true, countInMode_ == 4);
    menu.addItem(4, tr("transport.count_in.2_beats"), true, countInMode_ == 3);
    menu.addItem(2, tr("transport.count_in.1_bar"), true, countInMode_ == 1);
    menu.addItem(3, tr("transport.count_in.2_bars"), true, countInMode_ == 2);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(metronomeButton.get()),
                       [this](int result) {
                           if (result <= 0)
                               return;
                           // Map menu item IDs to CountIn enum values
                           static constexpr int idToMode[] = {0, 0, 1, 2, 3, 4};
                           int mode = idToMode[result];
                           countInMode_ = mode;
                           if (onCountInModeChange)
                               onCountInModeChange(mode);
                       });
}

void TransportPanel::setCountInMode(int mode) {
    countInMode_ = mode;
}

void TransportPanel::showAutomationModeMenu() {
    juce::PopupMenu menu;
    auto addModeItem = [&](int id, const juce::String& label, AutomationMode m) {
        menu.addItem(id, label, true, automationMode_ == m);
    };
    addModeItem(1, "Write", AutomationMode::Write);
    addModeItem(2, "Touch", AutomationMode::Touch);
    addModeItem(3, "Latch", AutomationMode::Latch);

    juce::Component::SafePointer<TransportPanel> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(automationWriteButton.get()),
                       [safeThis](int result) {
                           // The async callback can fire after the panel is destroyed (e.g. on
                           // window close); SafePointer guards against the dangling-this race.
                           auto* self = safeThis.getComponent();
                           if (self == nullptr || result <= 0)
                               return;
                           AutomationMode picked = AutomationMode::Write;
                           switch (result) {
                               case 1:
                                   picked = AutomationMode::Write;
                                   break;
                               case 2:
                                   picked = AutomationMode::Touch;
                                   break;
                               case 3:
                                   picked = AutomationMode::Latch;
                                   break;
                               default:
                                   return;
                           }
                           if (picked == self->automationMode_)
                               return;
                           self->automationMode_ = picked;
                           self->updateAutomationLabelText();
                           // Live-update the engine if currently armed; otherwise the choice
                           // becomes effective the next time the user arms.
                           if (self->isAutomationWriteEnabled)
                               self->emitCurrentAutomationMode();
                           self->repaint();
                       });
}

void TransportPanel::setAutomationMode(AutomationMode mode) {
    if (automationMode_ == mode)
        return;
    automationMode_ = mode;
    updateAutomationLabelText();
    repaint();
}

void TransportPanel::emitCurrentAutomationMode() {
    if (onAutomationModeChanged)
        onAutomationModeChanged(isAutomationWriteEnabled ? automationMode_ : AutomationMode::Off);
}

void TransportPanel::updateAutomationLabelText() {
    if (!automationWriteLabel)
        return;
    juce::String suffix;
    switch (automationMode_) {
        case AutomationMode::Write:
            suffix = "WRITE";
            break;
        case AutomationMode::Touch:
            suffix = "TOUCH";
            break;
        case AutomationMode::Latch:
            suffix = "LATCH";
            break;
        case AutomationMode::Off:
            suffix = "WRITE";
            break;  // shouldn't happen — Off implies disarmed
    }
    automationWriteLabel->setText("AUTOMATION " + suffix, juce::dontSendNotification);
}

}  // namespace magda
