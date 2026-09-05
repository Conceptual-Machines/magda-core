#include "TransportPanel.hpp"

#include "../../audio/midi/QwertyMidiKeyboard.hpp"
#include "../components/common/GridDivisionMenu.hpp"
#include "../components/common/QwertyKeyboardPopup.hpp"
#include "../layout/LayoutConfig.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "TransportTextWidths.hpp"
#include "core/StringTable.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

namespace transport = daw::ui::transport;

TransportPanel::TransportPanel() {
    MixAnalysisService::getInstance().addListener(this);
    // applyThemedLabelFonts() re-resolves these from FontManager on every
    // look-and-feel change, and the section widths are measured for the result.
    // The font-scale refresh must not multiply them a second time.
    markResolvesOwnFonts(*this);
    setupTransportButtons();
    setupTimeDisplayBoxes();
    setupTempoAndQuantize();

    // CPU usage — title label + value label stacked
    cpuTitleLabel = std::make_unique<juce::Label>("cpuTitle", tr("transport.cpu.cpu"));
    cpuTitleLabel->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    cpuTitleLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuTitleLabel);

    cpuValueLabel = std::make_unique<juce::Label>("cpuValue", "0%");
    cpuValueLabel->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    cpuValueLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuValueLabel);

    // Automation write indicator label — purple text, visible only when write mode on
    automationWriteLabel = std::make_unique<juce::Label>("automationWrite", "AUTOMATION WRITE");
    automationWriteLabel->setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
    automationWriteLabel->setColour(juce::Label::backgroundColourId,
                                    juce::Colours::transparentBlack);
    automationWriteLabel->setJustificationType(juce::Justification::centredRight);
    addChildComponent(*automationWriteLabel);

    // Overflow menu button — hosts items that don't fit at narrow widths.
    overflowButton =
        std::make_unique<SvgButton>("More", BinaryData::menu_svg, BinaryData::menu_svgSize);
    overflowButton->setNormalColor(DarkTheme::getSecondaryTextColour());
    overflowButton->setActiveColor(juce::Colours::white);
    overflowButton->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).darker(0.6f));
    overflowButton->onClick = [this]() { showOverflowMenu(); };
    addChildComponent(*overflowButton);

    applyThemedLabelFonts();
}

TransportPanel::~TransportPanel() {
    MixAnalysisService::getInstance().removeListener(this);
    autoGridButton->setLookAndFeel(nullptr);
    snapButton->setLookAndFeel(nullptr);
}

void TransportPanel::mixAnalysisChanged() {
    // Grey out + disable the transport while an offline render owns the edit:
    // playback is blocked while devices load, so the
    // controls shouldn't look live. setEnabled cascades to the child buttons.
    const bool rendering = MixAnalysisService::getInstance().isBusy();
    if (isEnabled() == !rendering)
        return;
    setEnabled(!rendering);
    repaint();
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
    // automation_on.svg — drawing the letter in ACCENT_MODULATION made it
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
        g.setFont(FontManager::getInstance().getUIFont(transport::kTimecodeCaptionFontSize));
        g.drawText(groupName, wrapperBounds.toNearestInt().reduced(2, 1),
                   juce::Justification::topRight, false);
    };

    if (layout_.selLoopTimesVisible) {
        drawGroupWrapper(selectionStartLabel->getBounds().getUnion(selectionEndLabel->getBounds()),
                         transport::kSelectionCaption,
                         DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY));
        drawGroupWrapper(loopStartLabel->getBounds().getUnion(loopEndLabel->getBounds()),
                         transport::kLoopCaption, DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE));
    }
    drawGroupWrapper(playheadPositionLabel->getBounds().getUnion(editCursorLabel->getBounds()),
                     transport::kCursorCaption, DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION));
    if (layout_.punchVisible) {
        drawGroupWrapper(punchInButton->getBounds()
                             .getUnion(punchStartLabel->getBounds())
                             .getUnion(punchOutButton->getBounds())
                             .getUnion(punchEndLabel->getBounds()),
                         "", DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
    }
    drawGroupWrapper(tempoLabel->getBounds()
                         .getUnion(timeSigNumeratorLabel->getBounds())
                         .getUnion(timeSigDenominatorLabel->getBounds())
                         .getUnion(countInButton->getBounds())
                         .getUnion(metronomeButton->getBounds()),
                     "", DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION));
    if (layout_.gridVisible) {
        drawGroupWrapper(gridDivisionButton->getBounds(), "",
                         DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
        drawGroupWrapper(autoGridButton->getBounds().getUnion(snapButton->getBounds()), "",
                         DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
    }

    // CPU frame — rounded rectangle matching transport group wrapper style.
    // Skipped entirely when the panel is too narrow to host the meter.
    if (layout_.rightClusterVisible) {
        auto frameBounds = layout_.cpuTitle.getUnion(layout_.cpuValue).toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(frameBounds, 3.0f);

        // Separator between header and value, on the boundary the layout drew
        // between the two labels rather than a second guess at it.
        const auto sepY = static_cast<float>(layout_.cpuValue.getY());

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

// The banner only shows while automation write is armed, and only when the
// layout found room for it -- arming on a narrow panel must not put a
// zero-width label on screen.
void TransportPanel::updateAutomationWriteLabelVisibility() {
    if (automationWriteLabel)
        automationWriteLabel->setVisible(isAutomationWriteEnabled &&
                                         layout_.automationWriteLabelFits);
}

void TransportPanel::resized() {
    // The layout itself lives in TransportLayout: each section measures itself
    // through the same code that places it, then sections are dropped into the
    // overflow menu in the declared priority order until the survivors fit.
    // Nothing here decides what fits.
    layout_ = transport::compute(getWidth(), getHeight(), transport::measureTextWidths(),
                                 LayoutConfig::getInstance().densityScale);
    const auto& l = layout_;

    // The readouts keep their digits clear of the caption / punch icons drawn
    // over their right end; the layout measured that zone into their width.
    for (auto* label : {selectionStartLabel.get(), selectionEndLabel.get(), loopStartLabel.get(),
                        loopEndLabel.get(), playheadPositionLabel.get(), editCursorLabel.get(),
                        punchStartLabel.get(), punchEndLabel.get()})
        label->setTrailingInset(l.timeBoxTrailingInset);

    // Visibility first: a dropped section keeps the empty rectangle the layout
    // left it, so no z-order or paint call can read a stale position.
    homeButton->setVisible(l.navVisible);
    prevButton->setVisible(l.navVisible);
    nextButton->setVisible(l.navVisible);
    loopButton->setVisible(l.loopBackVisible);
    backToArrangementButton->setVisible(l.loopBackVisible);
    punchStartLabel->setVisible(l.punchVisible);
    punchEndLabel->setVisible(l.punchVisible);
    punchInButton->setVisible(l.punchVisible);
    punchOutButton->setVisible(l.punchVisible);
    selectionStartLabel->setVisible(l.selLoopTimesVisible);
    selectionEndLabel->setVisible(l.selLoopTimesVisible);
    loopStartLabel->setVisible(l.selLoopTimesVisible);
    loopEndLabel->setVisible(l.selLoopTimesVisible);
    gridDivisionButton->setVisible(l.gridVisible);
    autoGridButton->setVisible(l.gridVisible);
    snapButton->setVisible(l.gridVisible);
    cpuTitleLabel->setVisible(l.rightClusterVisible);
    cpuValueLabel->setVisible(l.rightClusterVisible);
    qwertyKeyboardButton->setVisible(l.rightClusterVisible);
    overflowButton->setVisible(l.overflowVisible);
    updateAutomationWriteLabelVisibility();

    homeButton->setBounds(l.home);
    prevButton->setBounds(l.prev);
    nextButton->setBounds(l.next);

    playButton->setBounds(l.play);
    stopButton->setBounds(l.stop);
    recordButton->setBounds(l.record);
    automationWriteButton->setBounds(l.automationWrite);

    loopButton->setBounds(l.loop);
    backToArrangementButton->setBounds(l.backToArrangement);

    punchStartLabel->setBounds(l.punchStart);
    punchEndLabel->setBounds(l.punchEnd);
    punchInButton->setBounds(l.punchIn);
    punchOutButton->setBounds(l.punchOut);
    // The punch toggles ride on top of the right end of their readout.
    punchInButton->toFront(false);
    punchOutButton->toFront(false);

    // Pause is driven from the menu bar and keyboard only; it stays in the tree
    // for its callback but never takes space.
    pauseButton->setBounds(0, 0, 0, 0);
    pauseButton->setVisible(false);

    tempoLabel->setBounds(l.tempo);
    timeSigNumeratorLabel->setBounds(l.timeSigNumerator);
    timeSigDenominatorLabel->setBounds(l.timeSigDenominator);
    countInButton->setBounds(l.countIn);
    countInButton->toFront(false);
    metronomeButton->setBounds(l.metronome);
    metronomeButton->setAlpha(0.6f);
    metronomeButton->toFront(false);

    selectionStartLabel->setBounds(l.selectionStart);
    selectionEndLabel->setBounds(l.selectionEnd);
    loopStartLabel->setBounds(l.loopStart);
    loopEndLabel->setBounds(l.loopEnd);
    playheadPositionLabel->setBounds(l.playhead);
    editCursorLabel->setBounds(l.editCursor);

    gridDivisionButton->setBounds(l.gridDivision);
    autoGridButton->setBounds(l.autoGrid);
    snapButton->setBounds(l.snap);

    // The grid fraction is edited through the division button's popup; the raw
    // numerator/denominator labels and their slash are legacy and stay hidden.
    gridNumeratorLabel->setVisible(false);
    gridDenominatorLabel->setVisible(false);
    gridSlashLabel->setBounds(0, 0, 0, 0);
    gridSlashLabel->setVisible(false);

    qwertyKeyboardButton->setBounds(l.qwerty);
    overflowButton->setBounds(l.overflow);
    automationWriteLabel->setBounds(l.automationWriteLabel);

    cpuTitleLabel->setBounds(l.cpuTitle);
    cpuValueLabel->setBounds(l.cpuValue);
}

juce::Rectangle<int> TransportPanel::getTransportControlsArea() const {
    return getLocalBounds().withWidth(layout_.transportRight);
}

juce::Rectangle<int> TransportPanel::getMetronomeBpmArea() const {
    return {layout_.transportRight, 0, layout_.metroRight - layout_.transportRight, getHeight()};
}

juce::Rectangle<int> TransportPanel::getTimeDisplayArea() const {
    return {layout_.metroRight, 0, layout_.timeRight - layout_.metroRight, getHeight()};
}

juce::Rectangle<int> TransportPanel::getTempoQuantizeArea() const {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(layout_.timeRight);
    bounds.removeFromRight(layout_.cpu.getWidth());
    return bounds;
}

juce::Rectangle<int> TransportPanel::getCpuArea() const {
    return layout_.cpu;  // empty while the right cluster is collapsed
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

    if (!layout_.loopBackVisible) {
        menu.addSeparator();
        menu.addItem(IdLoop, "Loop", true, isLooping);
        menu.addItem(IdBackToArr, "Back to Arrangement");
    }
    if (!layout_.punchVisible) {
        menu.addSeparator();
        menu.addItem(IdPunchIn, "Punch In", true, isPunchInEnabled);
        menu.addItem(IdPunchOut, "Punch Out", true, isPunchOutEnabled);
    }
    if (!layout_.gridVisible) {
        menu.addSeparator();
        menu.addItem(IdAutoGrid, "Auto Grid", true, isAutoGrid);
        menu.addItem(IdSnap, "Snap", true, isSnapEnabled);
    }
    if (!layout_.navVisible) {
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
        std::make_unique<SvgButton>("Play", BinaryData::play_svg, BinaryData::play_svgSize);
    styleTransportButton(*playButton, DarkTheme::ACCENT_PRIMARY);
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
        std::make_unique<SvgButton>("Stop", BinaryData::stop_svg, BinaryData::stop_svgSize);
    styleTransportButton(*stopButton, DarkTheme::ACCENT_PRIMARY);
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
            updateAutomationWriteLabelVisibility();
            if (onAutomationWriteToggle)
                onAutomationWriteToggle(false);
        }
        if (onStop)
            onStop();
        repaint();
    };
    addAndMakeVisible(*stopButton);

    // Record button
    recordButton =
        std::make_unique<SvgButton>("Record", BinaryData::record_svg, BinaryData::record_svgSize);
    styleTransportButton(*recordButton, DarkTheme::STATUS_ERROR);
    recordButton->onClick = [this]() {
        isRecording = !isRecording;
        recordButton->setActive(isRecording);
        if (onRecord) {
            onRecord();
        }
        repaint();
    };
    addAndMakeVisible(*recordButton);

    // Automation Write button — purple when enabled (write mode),
    // grey when disabled. Matches the purple automation accent used on
    // lane headers and control tints.
    automationWriteButton = std::make_unique<SvgButton>(
        "Automation Write", BinaryData::automation_write_svg, BinaryData::automation_write_svgSize);
    styleTransportButton(*automationWriteButton, DarkTheme::ACCENT_MODULATION);
    automationWriteButton->setActive(false);
    automationWriteButton->onClick = [this]() {
        isAutomationWriteEnabled = !isAutomationWriteEnabled;
        automationWriteButton->setActive(isAutomationWriteEnabled);
        updateAutomationWriteLabelVisibility();
        updateAutomationLabelText();
        if (onAutomationWriteToggle)
            onAutomationWriteToggle(isAutomationWriteEnabled);
        emitCurrentAutomationMode();
        repaint();
    };
    automationWriteButton->addMouseListener(this, false);
    addAndMakeVisible(*automationWriteButton);

    // Pause button
    pauseButton =
        std::make_unique<SvgButton>("Pause", BinaryData::pause_svg, BinaryData::pause_svgSize);
    styleTransportButton(*pauseButton, DarkTheme::ACCENT_PRIMARY);
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
    homeButton =
        std::make_unique<SvgButton>("Home", BinaryData::rewind_svg, BinaryData::rewind_svgSize);
    styleTransportButton(*homeButton, DarkTheme::ACCENT_PRIMARY);
    homeButton->onClick = [this]() {
        if (onGoHome)
            onGoHome();
    };
    addAndMakeVisible(*homeButton);

    // Prev button
    prevButton =
        std::make_unique<SvgButton>("Prev", BinaryData::prev_svg, BinaryData::prev_svgSize);
    styleTransportButton(*prevButton, DarkTheme::ACCENT_PRIMARY);
    prevButton->onClick = [this]() {
        if (onGoToPrev)
            onGoToPrev();
    };
    addAndMakeVisible(*prevButton);

    // Next button
    nextButton =
        std::make_unique<SvgButton>("Next", BinaryData::next_svg, BinaryData::next_svgSize);
    styleTransportButton(*nextButton, DarkTheme::ACCENT_PRIMARY);
    nextButton->onClick = [this]() {
        if (onGoToNext)
            onGoToNext();
    };
    addAndMakeVisible(*nextButton);

    // Loop button
    loopButton =
        std::make_unique<SvgButton>("Loop", BinaryData::loop_svg, BinaryData::loop_svgSize);
    styleTransportButton(*loopButton, DarkTheme::ACCENT_PRIMARY);
    loopButton->onClick = [this]() {
        isLooping = !isLooping;
        loopButton->setActive(isLooping);
        if (onLoop)
            onLoop(isLooping);
    };
    addAndMakeVisible(*loopButton);

    // Back to Arrangement button
    backToArrangementButton = std::make_unique<SvgButton>(
        "BackToArrangement", BinaryData::resume_svg, BinaryData::resume_svgSize);
    styleTransportButton(*backToArrangementButton, DarkTheme::ACCENT_ATTENTION);
    backToArrangementButton->onClick = [this]() {
        if (onBackToArrangement)
            onBackToArrangement();
    };
    addAndMakeVisible(*backToArrangementButton);

    // QWERTY MIDI keyboard toggle
    qwertyKeyboardButton = std::make_unique<SvgButton>(
        "QwertyKeyboard", BinaryData::midi_qwerty_svg, BinaryData::midi_qwerty_svgSize);
    styleTransportButton(*qwertyKeyboardButton, DarkTheme::ACCENT_MODULATION);
    qwertyKeyboardButton->onClick = [this]() {
        bool active = !qwertyKeyboardButton->isActive();
        qwertyKeyboardButton->setActive(active);
        if (onQwertyKeyboardToggled)
            onQwertyKeyboardToggled(active);
    };
    qwertyKeyboardButton->addMouseListener(this, false);
    addAndMakeVisible(*qwertyKeyboardButton);

    // Punch buttons use one geometry; their active purple is injected in code.
    punchInButton = std::make_unique<SvgButton>("PunchIn", BinaryData::punchin_svg,
                                                BinaryData::punchin_svgSize);
    styleTransportButton(*punchInButton, DarkTheme::ACCENT_MODULATION, true);
    punchInButton->onClick = [this]() {
        isPunchInEnabled = !isPunchInEnabled;
        punchInButton->setActive(isPunchInEnabled);
        updatePunchLabelColors();
        if (onPunchInToggle)
            onPunchInToggle(isPunchInEnabled);
    };
    addAndMakeVisible(*punchInButton);

    // Punch Out is an independent toggle using the same code-coloured state.
    punchOutButton = std::make_unique<SvgButton>("PunchOut", BinaryData::punchout_svg,
                                                 BinaryData::punchout_svgSize);
    styleTransportButton(*punchOutButton, DarkTheme::ACCENT_MODULATION, true);
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
        label->setRange(0.0, transport::kTimecodeMaxBeats, 0.0);
        label->setBarsBeatsIsPosition(true);
        label->setDoubleClickResetsValue(false);
        label->setDrawBackground(false);
        label->setOverlayLabel(overlay);
        label->setTextColour(textColour);
        addAndMakeVisible(*label);
    };

    auto accentBlue = DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
    auto accentOrange = DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION);

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
            auto green = DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE);
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
        if (onEditCursorEdit)
            onEditCursorEdit(beats);
    };

    // Punch start/end — stacked box in time display area
    auto accentPurple = DarkTheme::getColour(DarkTheme::ACCENT_MODULATION);

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
    tempoLabel->setRange(MIN_VALID_BPM, MAX_VALID_BPM, DEFAULT_BPM);
    tempoLabel->setValue(currentTempo, juce::dontSendNotification);
    tempoLabel->setSuffix("");
    tempoLabel->setDecimalPlaces(2);
    tempoLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION));
    tempoLabel->setShowFillIndicator(false);
    tempoLabel->setDoubleClickResetsValue(false);
    tempoLabel->setSnapToInteger(true);
    tempoLabel->setDrawBorder(false);
    tempoLabel->setDrawBackground(false);
    tempoLabel->onValueChange = [this]() {
        currentTempo = tempoLabel->getValue();
        if (onTempoChange)
            onTempoChange(currentTempo);
    };
    // Tint the readout purple while a tempo lane is active, matching the volume
    // / pan controls (the label self-subscribes to AutomationManager).
    tempoLabel->setAutomationTarget(ControlTarget::tempo());
    addAndMakeVisible(*tempoLabel);

    // Time signature — numerator / denominator draggable labels
    timeSigNumeratorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    timeSigNumeratorLabel->setRange(MIN_TIME_SIGNATURE_VALUE, MAX_TIME_SIGNATURE_VALUE,
                                    DEFAULT_TIME_SIGNATURE_NUMERATOR);
    timeSigNumeratorLabel->setValue(static_cast<double>(timeSignatureNumerator),
                                    juce::dontSendNotification);
    timeSigNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSigNumeratorLabel->setShowFillIndicator(false);
    timeSigNumeratorLabel->setDoubleClickResetsValue(true);
    timeSigNumeratorLabel->setDrawBorder(false);
    timeSigNumeratorLabel->setDrawBackground(false);
    timeSigNumeratorLabel->setSnapToInteger(true);
    timeSigNumeratorLabel->setSuffix("/");
    timeSigNumeratorLabel->setJustification(juce::Justification::centredRight);
    timeSigNumeratorLabel->onValueChange = [this]() {
        timeSignatureNumerator = clampTimeSignatureValue(
            static_cast<int>(std::round(timeSigNumeratorLabel->getValue())));
        if (onTimeSignatureChange)
            onTimeSignatureChange(timeSignatureNumerator, timeSignatureDenominator);
    };
    addAndMakeVisible(*timeSigNumeratorLabel);

    timeSigDenominatorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    timeSigDenominatorLabel->setRange(MIN_TIME_SIGNATURE_VALUE, MAX_TIME_SIGNATURE_VALUE,
                                      DEFAULT_TIME_SIGNATURE_DENOMINATOR);
    timeSigDenominatorLabel->setValue(static_cast<double>(timeSignatureDenominator),
                                      juce::dontSendNotification);
    timeSigDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSigDenominatorLabel->setShowFillIndicator(false);
    timeSigDenominatorLabel->setDoubleClickResetsValue(true);
    timeSigDenominatorLabel->setDrawBorder(false);
    timeSigDenominatorLabel->setDrawBackground(false);
    timeSigDenominatorLabel->setSnapToInteger(true);
    timeSigDenominatorLabel->setJustification(juce::Justification::centredLeft);
    timeSigDenominatorLabel->onValueChange = [this]() {
        timeSignatureDenominator = clampTimeSignatureValue(
            static_cast<int>(std::round(timeSigDenominatorLabel->getValue())));
        if (onTimeSignatureChange)
            onTimeSignatureChange(timeSignatureNumerator, timeSignatureDenominator);
    };
    addAndMakeVisible(*timeSigDenominatorLabel);

    // Auto grid toggle button (like SNAP button)
    autoGridButton = std::make_unique<juce::TextButton>(transport::kAutoGridCaption);
    autoGridButton->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    autoGridButton->setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_MODULATION).darker(0.3f));
    autoGridButton->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoGridButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
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
        gridDivisionButton->setDivision(gridNumerator, gridDenominator);
        gridDivisionButton->setEnabled(!isAutoGrid);
        gridDivisionButton->setAlpha(isAutoGrid ? 0.8f : 1.0f);
        if (onGridQuantizeChange)
            onGridQuantizeChange(isAutoGrid, gridNumerator, gridDenominator);
    };
    addAndMakeVisible(*autoGridButton);

    // Grid numerator (Integer format, range 1-32)
    gridNumeratorLabel =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    gridNumeratorLabel->setRange(1.0, 128.0, 1.0);
    gridNumeratorLabel->setValue(static_cast<double>(gridNumerator), juce::dontSendNotification);
    gridNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
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
    gridDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
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

    gridDivisionButton = std::make_unique<daw::ui::GridDivisionButton>();
    gridDivisionButton->setTooltip("Grid division");
    gridDivisionButton->setDivision(gridNumerator, gridDenominator);
    gridDivisionButton->onClick = [this]() {
        daw::ui::showGridDivisionMenu(
            *gridDivisionButton, gridNumerator, gridDenominator,
            [this](int numerator, int denominator) {
                const auto [num, den] = magda::grid::normaliseFraction(numerator, denominator);
                gridNumerator = num;
                gridDenominator = den;
                gridNumeratorLabel->setValue(num, juce::dontSendNotification);
                gridDenominatorLabel->setValue(den, juce::dontSendNotification);
                gridDivisionButton->setDivision(num, den);
                if (!isAutoGrid && onGridQuantizeChange)
                    onGridQuantizeChange(false, num, den);
            });
    };
    addAndMakeVisible(*gridDivisionButton);

    // Metronome button
    metronomeButton = std::make_unique<SvgButton>("Metronome", BinaryData::metronome_svg,
                                                  BinaryData::metronome_svgSize);
    styleTransportButton(*metronomeButton, DarkTheme::ACCENT_PRIMARY);
    metronomeButton->setIconPadding(2.0f);
    metronomeButton->setNormalColor(juce::Colour(0xFFBCBCBC));
    metronomeButton->onClick = [this]() {
        bool newState = !metronomeButton->isActive();
        metronomeButton->setActive(newState);
        if (onMetronomeToggle)
            onMetronomeToggle(newState);
    };
    addAndMakeVisible(*metronomeButton);

    // Count-in button. Left-click opens the length menu; there is no toggle,
    // since "off" is one of the menu's own choices.
    countInButton = std::make_unique<SvgButton>("CountIn", BinaryData::record_circle_svg,
                                                BinaryData::record_circle_svgSize);
    styleTransportButton(*countInButton, DarkTheme::ACCENT_PRIMARY, true);
    countInButton->onClick = [this]() { showCountInMenu(); };
    addAndMakeVisible(*countInButton);
    setCountInMode(countInMode_);

    // Snap button (text-based toggle)
    snapButton = std::make_unique<juce::TextButton>(transport::kSnapCaption);
    snapButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    snapButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_MODULATION).darker(0.3f));
    snapButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    snapButton->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
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

void TransportPanel::styleTransportButton(SvgButton& button, ColourRole accentRole,
                                          bool activeGlyphUsesAccent) {
    const auto accentColor = DarkTheme::getColour(accentRole);
    button.setActiveColor(accentColor);
    button.setPressedColor(accentColor);
    button.setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    button.setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    button.setIconPadding(0.0f);

    // Transport SVGs are geometry templates. Their stable source keys are
    // replaced at paint time; no active colour is stored in a second asset.
    button.setStateColourReplacement(juce::Colour(0xFF1A1A1A), DarkTheme::PIANO_ROLL_BACKGROUND,
                                     accentRole);
    const auto activeGlyphRole = activeGlyphUsesAccent ? accentRole : DarkTheme::TEXT_BRIGHT;
    button.setStateColourReplacement(juce::Colour(0xFFBCBCBC), DarkTheme::ICON_TRANSPORT,
                                     activeGlyphRole);
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), DarkTheme::ICON_NEUTRAL,
                                     activeGlyphRole);
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
    auto colour = hasValidLoop ? DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE)
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
    timeSignatureNumerator = clampTimeSignatureValue(numerator);
    timeSignatureDenominator = clampTimeSignatureValue(denominator);

    // Update time signature display
    timeSigNumeratorLabel->setValue(static_cast<double>(timeSignatureNumerator),
                                    juce::dontSendNotification);
    timeSigDenominatorLabel->setValue(static_cast<double>(timeSignatureDenominator),
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
    currentTempo = clampBpm(bpm);
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
        updateAutomationWriteLabelVisibility();
    }
}

void TransportPanel::setLiveTempoDisplay(double bpm) {
    const double clamped = clampBpm(bpm);
    if (std::abs(tempoLabel->getValue() - clamped) < 0.01)
        return;
    tempoLabel->setValue(clamped, juce::dontSendNotification);
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

void TransportPanel::setRecordingState(bool recording) {
    if (isRecording != recording) {
        DBG("[TransportPanel] setRecordingState: " << (int)isRecording << " -> " << (int)recording);
        isRecording = recording;
        recordButton->setActive(isRecording);
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
    gridDivisionButton->setDivision(numerator, denominator);
    gridDivisionButton->setEnabled(!autoGrid);
    gridDivisionButton->setAlpha(autoGrid ? 0.8f : 1.0f);
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
    auto activeColor = DarkTheme::getColour(DarkTheme::ACCENT_MODULATION);
    auto inactiveColor = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    // Punch start label color matches punch in button state
    punchStartLabel->setTextColour(isPunchInEnabled ? activeColor : inactiveColor);
    punchStartLabel->setAlpha(isPunchInEnabled ? 1.0f : 0.5f);

    // Punch end label color matches punch out button state
    punchEndLabel->setTextColour(isPunchOutEnabled ? activeColor : inactiveColor);
    punchEndLabel->setAlpha(isPunchOutEnabled ? 1.0f : 0.5f);
}

void TransportPanel::lookAndFeelChanged() {
    applyThemedLabelColours();
    // The children that cache a resolved font have to be handed the new one
    // before the bar is measured for it, or the layout would be sized for a
    // font those children are not drawing with. Then relayout: a look-and-feel
    // broadcast repaints but does not resize, and the section widths come from
    // the fonts now.
    applyThemedLabelFonts();
    if (overflowButton != nullptr)
        resized();
}

// The children that resolve a font at paint time (the timecode readouts, the
// grid division button, the AUTO/SNAP toggles) follow a font change on their
// own. These cache one, so they are handed it again here, at the sizes
// TransportTextWidths measures them at.
void TransportPanel::applyThemedLabelFonts() {
    if (overflowButton == nullptr)
        return;

    auto& fonts = FontManager::getInstance();
    cpuTitleLabel->setFont(fonts.getUIFont(transport::kCpuTitleFontSize));
    cpuValueLabel->setFont(fonts.getMonoFont(transport::kCpuValueFontSize));
    automationWriteLabel->setFont(fonts.getUIFont(transport::kBannerFontSize).boldened());

    // DraggableValueLabel resolves and caches on setFontSize, so re-setting the
    // same size is what re-fetches it from FontManager.
    tempoLabel->setFontSize(transport::kReadoutFontSize);
    timeSigNumeratorLabel->setFontSize(transport::kReadoutFontSize);
    timeSigDenominatorLabel->setFontSize(transport::kReadoutFontSize);
}

void TransportPanel::applyThemedLabelColours() {
    // overflowButton is created last in the constructor, so its presence means
    // every child below exists. Guards against a look-and-feel change arriving
    // before construction finishes.
    if (overflowButton == nullptr)
        return;

    const auto accentBlue = DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
    const auto accentOrange = DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION);
    const auto secondary = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);

    // BPM readout.
    tempoLabel->setTextColour(accentOrange);

    // Bars/beats "measures" readouts.
    selectionStartLabel->setTextColour(accentBlue);
    selectionEndLabel->setTextColour(accentBlue);
    playheadPositionLabel->setTextColour(accentOrange);
    editCursorLabel->setTextColour(accentOrange);

    // Time-signature digits and the grid numerator/denominator/slash readouts.
    timeSigNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    timeSigDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    gridNumeratorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
    gridDenominatorLabel->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));
    gridSlashLabel->setColour(juce::Label::textColourId, secondary);

    // Loop labels: green when a valid loop is active, dim otherwise (mirrors
    // setLoopRegion), recomputed from cached loop state.
    const bool hasValidLoop =
        cachedLoopEnabled && cachedLoopEnd > cachedLoopStart && cachedLoopStart >= 0.0;
    const auto loopColour = hasValidLoop ? DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE)
                                         : DarkTheme::getColour(DarkTheme::TEXT_DIM);
    loopStartLabel->setTextColour(loopColour);
    loopEndLabel->setTextColour(loopColour);

    // Punch labels track their arm state and the active palette.
    updatePunchLabelColors();

    cpuTitleLabel->setColour(juce::Label::textColourId, secondary);
    cpuValueLabel->setColour(juce::Label::textColourId, secondary);
    automationWriteLabel->setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_MODULATION));

    overflowButton->setNormalColor(DarkTheme::getSecondaryTextColour());
    overflowButton->setActiveBackgroundColor(accentBlue.darker(0.6f));

    // AUTO/SNAP capture concrete colours at construction; re-apply them so a
    // live theme switch restyles the toggles instead of leaving the old
    // palette behind.
    for (auto* button : {autoGridButton.get(), snapButton.get()}) {
        if (button == nullptr)
            continue;
        button->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
        button->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_MODULATION).darker(0.3f));
        button->setColour(juce::TextButton::textColourOffId, secondary);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    repaint();
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
        cpuValueLabel->setText(transport::cpuReadoutText(juce::roundToInt(currentCpuUsage * 100.0f),
                                                         juce::roundToInt(peakCpuUsage * 100.0f)),
                               juce::dontSendNotification);
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
    if (e.originalComponent == automationWriteButton.get() && e.mods.isRightButtonDown()) {
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
    // Without the header the list reads as a bare Off/1/2/1 Bar/2 Bars, which
    // is exactly what a metronome click-interval setting would offer.
    menu.addSectionHeader(tr("transport.count_in.header"));
    menu.addItem(1, tr("transport.count_in.off"), true, countInMode_ == 0);
    menu.addItem(5, tr("transport.count_in.1_beat"), true, countInMode_ == 4);
    menu.addItem(4, tr("transport.count_in.2_beats"), true, countInMode_ == 3);
    menu.addItem(2, tr("transport.count_in.1_bar"), true, countInMode_ == 1);
    menu.addItem(3, tr("transport.count_in.2_bars"), true, countInMode_ == 2);

    juce::Component::SafePointer<TransportPanel> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(countInButton.get()),
                       [safeThis](int result) {
                           if (safeThis == nullptr || result <= 0)
                               return;
                           // Map menu item IDs to CountIn enum values
                           static constexpr int idToMode[] = {0, 0, 1, 2, 3, 4};
                           int mode = idToMode[result];
                           safeThis->setCountInMode(mode);
                           if (safeThis->onCountInModeChange)
                               safeThis->onCountInModeChange(mode);
                       });
}

void TransportPanel::setCountInMode(int mode) {
    countInMode_ = mode;
    if (countInButton) {
        countInButton->setActive(mode != 0);
        // Matches the metronome's resting dimness when off, full strength when
        // armed, so the two gutter icons sit at the same weight until one of
        // them has something to say.
        countInButton->setAlpha(mode != 0 ? 1.0f : 0.6f);
        countInButton->setTooltip(tr("transport.count_in.header") + ": " + countInModeLabel(mode));
    }
}

juce::String TransportPanel::countInModeLabel(int mode) {
    switch (mode) {
        case 1:
            return tr("transport.count_in.1_bar");
        case 2:
            return tr("transport.count_in.2_bars");
        case 3:
            return tr("transport.count_in.2_beats");
        case 4:
            return tr("transport.count_in.1_beat");
        default:
            return tr("transport.count_in.off");
    }
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
