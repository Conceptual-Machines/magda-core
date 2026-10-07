#include "TransportPanel.hpp"

#include "../../audio/midi/QwertyMidiKeyboard.hpp"
#include "../components/common/GridDivisionMenu.hpp"
#include "../components/common/QwertyKeyboardPopup.hpp"
#include "../layout/LayoutConfig.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "TransportTextWidths.hpp"
#include "core/StringTable.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

namespace transport = daw::ui::transport;

// Root in the readout font, quality smaller beside it; "--" while unset.
class TransportPanel::KeyReadout : public juce::Component, public juce::SettableTooltipClient {
  public:
    int root = -1;
    int quality = 0;
    std::function<void()> onClick;

    KeyReadout() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Project key");
    }

    void paint(juce::Graphics& g) override {
        auto& fonts = FontManager::getInstance();
        auto area = getLocalBounds().reduced(4, 0);
        const bool hasKey = root >= 0 && root < static_cast<int>(transport::kKeyRootNames.size());
        const juce::String rootText =
            hasKey ? transport::kKeyRootNames[static_cast<size_t>(root)] : transport::kNoKeyText;

        const auto rootFont = fonts.getUIFontBold(transport::kReadoutFontSize);
        g.setFont(rootFont);
        g.setColour(ActiveTheme::getColour(hasKey ? ActiveTheme::TEXT_PRIMARY
                                                  : ActiveTheme::TRANSPORT_TEXT_DIM));
        const int rootWidth = juce::GlyphArrangement::getStringWidthInt(rootFont, rootText);
        g.drawText(rootText, area.removeFromLeft(rootWidth), juce::Justification::centredLeft,
                   false);
        if (!hasKey)
            return;

        area.removeFromLeft(3);
        g.setFont(fonts.getUIFont(transport::kKeyQualityFontSize));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.drawText(transport::kKeyQualityNames[quality == 1 ? 1 : 0], area,
                   juce::Justification::centredLeft, false);
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onClick)
            onClick();
    }
};

TransportPanel::TransportPanel() {
    MixAnalysisService::getInstance().addListener(this);
    // applyThemedLabelFonts() re-resolves these from FontManager on every
    // look-and-feel change, and the section widths are measured for the result.
    // The font-scale refresh must not multiply them a second time.
    markResolvesOwnFonts(*this);
    setupTransportButtons();
    setupTimeDisplayBoxes();
    setupTempoAndQuantize();

    keyReadout = std::make_unique<KeyReadout>();
    keyReadout->onClick = [this]() { showKeyMenu(); };
    addAndMakeVisible(*keyReadout);
    updateKeyReadout();

    selChipButton = std::make_unique<juce::TextButton>(transport::kSelectionCaption);
    selChipButton->onClick = [this]() {
        showLoopRange_ = false;
        updateRangeVisibility();
    };
    loopChipButton = std::make_unique<juce::TextButton>(transport::kLoopCaption);
    loopChipButton->onClick = [this]() {
        showLoopRange_ = true;
        updateRangeVisibility();
    };

    keepButton = std::make_unique<juce::TextButton>(transport::kKeepCaption);
    keepButton->setEnabled(false);
    keepButton->setTooltip("Rolling master buffer (not available yet)");
    for (auto* button : {selChipButton.get(), loopChipButton.get(), keepButton.get()})
        addAndMakeVisible(*button);
    applyToggleColours();

    style_ = transport::styleFromKey(Config::getInstance().getTransportStyle());
    Config::getInstance().addListener(this);
    ProjectManager::getInstance().addListener(this);

    // CPU usage — title label + value label stacked
    cpuTitleLabel = std::make_unique<juce::Label>("cpuTitle", tr("transport.cpu.cpu"));
    cpuTitleLabel->setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    cpuTitleLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuTitleLabel);

    cpuValueLabel = std::make_unique<juce::Label>("cpuValue", "0%");
    cpuValueLabel->setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    cpuValueLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*cpuValueLabel);

    // Automation write indicator label — purple text, visible only when write mode on
    automationWriteLabel = std::make_unique<juce::Label>("automationWrite", "AUTOMATION WRITE");
    automationWriteLabel->setColour(juce::Label::textColourId,
                                    ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));
    automationWriteLabel->setColour(juce::Label::backgroundColourId,
                                    juce::Colours::transparentBlack);
    automationWriteLabel->setJustificationType(juce::Justification::centredRight);
    addChildComponent(*automationWriteLabel);

    // Overflow menu button — hosts items that don't fit at narrow widths.
    overflowButton =
        std::make_unique<SvgButton>("More", BinaryData::menu_svg, BinaryData::menu_svgSize);
    overflowButton->setNormalColor(ActiveTheme::getSecondaryTextColour());
    overflowButton->setActiveColor(juce::Colours::white);
    overflowButton->setActiveBackgroundColor(
        ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).darker(0.6f));
    overflowButton->onClick = [this]() { showOverflowMenu(); };
    addChildComponent(*overflowButton);

    applyThemedLabelFonts();
}

TransportPanel::~TransportPanel() {
    MixAnalysisService::getInstance().removeListener(this);
    Config::getInstance().removeListener(this);
    ProjectManager::getInstance().removeListener(this);
    for (auto* button : {autoGridButton.get(), snapButton.get(), selChipButton.get(),
                         loopChipButton.get(), keepButton.get()})
        button->setLookAndFeel(nullptr);
}

void TransportPanel::configChanged() {
    const auto style = transport::styleFromKey(Config::getInstance().getTransportStyle());
    if (style == style_)
        return;
    style_ = style;
    resized();
}

void TransportPanel::projectOpened(const ProjectInfo&) {
    updateKeyReadout();
}

void TransportPanel::projectClosed() {
    updateKeyReadout();
}

void TransportPanel::projectPropertiesChanged() {
    updateKeyReadout();
}

void TransportPanel::updateKeyReadout() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    keyReadout->root = info.keyRoot;
    keyReadout->quality = info.keyQuality;
    keyReadout->repaint();
}

void TransportPanel::showKeyMenu() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    const int root = info.keyRoot;
    const int quality = info.keyQuality;

    constexpr int kNoKeyId = 1;
    constexpr int kRootIdBase = 100;
    constexpr int kQualityIdBase = 200;

    juce::PopupMenu menu;
    menu.addItem(kNoKeyId, "No Key", true, root < 0);
    menu.addSeparator();
    for (int i = 0; i < static_cast<int>(transport::kKeyRootNames.size()); ++i)
        menu.addItem(kRootIdBase + i, transport::kKeyRootNames[static_cast<size_t>(i)], true,
                     root == i);
    menu.addSeparator();
    menu.addItem(kQualityIdBase, "Major", root >= 0, root >= 0 && quality == 0);
    menu.addItem(kQualityIdBase + 1, "Minor", root >= 0, root >= 0 && quality == 1);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(keyReadout.get()),
                       [root, quality](int result) {
                           auto& pm = ProjectManager::getInstance();
                           if (result == kNoKeyId)
                               pm.setKey(-1, quality);
                           else if (result >= kQualityIdBase)
                               pm.setKey(root, result - kQualityIdBase);
                           else if (result >= kRootIdBase)
                               pm.setKey(result - kRootIdBase, quality);
                       });
}

void TransportPanel::styleToggle(juce::TextButton& button, juce::Colour offFill,
                                 juce::Colour offText, juce::Colour onFill, juce::Colour onText) {
    button.setColour(juce::TextButton::buttonColourId, offFill);
    button.setColour(juce::TextButton::buttonOnColourId, onFill);
    button.setColour(juce::TextButton::textColourOffId, offText);
    button.setColour(juce::TextButton::textColourOnId, onText);
    button.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                             juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    button.setWantsKeyboardFocus(false);
    button.setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
}

// SEL / LOOP read as text until chosen; AUTO / SNAP and KEEP sit on chips.
void TransportPanel::applyToggleColours() {
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    const auto none = juce::Colours::transparentBlack;
    const auto chip = colour(ActiveTheme::TRANSPORT_CHIP);
    const auto dim = colour(ActiveTheme::TRANSPORT_TEXT_DIM);

    styleToggle(*selChipButton, none, dim, chip, colour(ActiveTheme::ACCENT_PRIMARY));
    styleToggle(*loopChipButton, none, dim, chip, colour(ActiveTheme::ACCENT_POSITIVE));
    styleToggle(*keepButton, chip, colour(ActiveTheme::TEXT_PRIMARY), chip,
                colour(ActiveTheme::TEXT_PRIMARY));
    keepButton->setColour(juce::ComboBox::outlineColourId,
                          colour(ActiveTheme::TRANSPORT_WELL_BORDER));
    for (auto* button : {autoGridButton.get(), snapButton.get()})
        styleToggle(*button, chip, colour(ActiveTheme::TEXT_SECONDARY),
                    colour(ActiveTheme::TRANSPORT_TOGGLE_ON), juce::Colours::white);
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
                                  : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);

    g.setColour(textColour);
    g.setFont(FontManager::getInstance().getUIFontBold(6.0f));
    g.drawText(letter, labelArea, juce::Justification::centred);
}

void TransportPanel::paintFrame(juce::Graphics& g, juce::Rectangle<int> area) const {
    if (area.isEmpty())
        return;
    const auto bounds = area.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL_BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

// The master buffer is not built yet: the frame, dot, meter floor and length
// are drawn idle so the slot reads as what it will become.
void TransportPanel::paintMemory(juce::Graphics& g) const {
    const auto& l = layout_;
    if (!l.rightClusterVisible)
        return;
    const auto dim = ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_MEMORY));
    g.fillEllipse(l.memoryDot.toFloat());

    if (!l.memoryMeterVisible)
        return;
    auto& fonts = FontManager::getInstance();
    g.setFont(fonts.getUIFont(transport::kMemoryCaptionFontSize));
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.drawText(transport::kMemoryCaption, l.memoryCaption, juce::Justification::centredLeft, false);

    constexpr int kBarPitch = 3;
    const auto meter = l.memoryMeter.toFloat();
    const float floorY = meter.getCentreY() + 4.0f;
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_METER_FLOOR));
    for (float x = meter.getX(); x + 1.0f <= meter.getRight(); x += kBarPitch)
        g.fillRect(x, floorY, 1.5f, 1.5f);

    // Held length bright, capacity dim.
    const auto timeFont = fonts.getUIFont(transport::kMemoryTimeFontSize);
    const juce::String held = "--:--";
    auto timeArea = l.memoryTime;
    g.setFont(timeFont);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    g.drawText(held,
               timeArea.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(timeFont, held)),
               juce::Justification::centredLeft, false);
    g.setColour(dim);
    g.drawText(" / --:--", timeArea, juce::Justification::centredLeft, false);
}

void TransportPanel::paintStackCaptions(juce::Graphics& g) const {
    const auto& l = layout_;
    if (l.stackFrame.isEmpty())
        return;
    g.setFont(FontManager::getInstance().getUIFont(transport::kTimecodeCaptionFontSize));
    const auto caption = [&](juce::Rectangle<int> area, const char* text, ColourRole role) {
        g.setColour(ActiveTheme::getColour(role).withAlpha(0.7f));
        g.drawText(text, area, juce::Justification::centredLeft, false);
    };
    caption(l.selCaption, transport::kSelectionCaption, ActiveTheme::ACCENT_PRIMARY);
    caption(l.loopCaption, transport::kLoopCaption, ActiveTheme::ACCENT_POSITIVE);
    caption(l.cursorCaption, transport::kCursorCaption, ActiveTheme::ACCENT_ATTENTION);

    // A short rule between each start and end.
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM));
    for (const auto& [start, end] :
         {std::pair{l.selectionStart, l.selectionEnd}, std::pair{l.loopStart, l.loopEnd},
          std::pair{l.playhead, l.editCursor}}) {
        const float x0 = static_cast<float>(start.getRight()) + 3.0f;
        const float x1 = static_cast<float>(end.getX()) - 3.0f;
        g.drawHorizontalLine(start.getCentreY(), x0, x1);
    }
}

void TransportPanel::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::TRANSPORT_BACKGROUND));
    const auto& l = layout_;

    for (const auto& frame : {l.punchFrame, l.tempoFrame, l.cursorFrame, l.rangeFrame, l.stackFrame,
                              l.gridFrame, l.memoryFrame})
        paintFrame(g, frame);

    if (!l.tempoFrame.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL_BORDER));
        g.drawVerticalLine(l.keyDividerX, static_cast<float>(l.tempoFrame.getY() + 8),
                           static_cast<float>(l.tempoFrame.getBottom() - 8));
    }

    if (!l.clock.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getMonoFont(transport::kClockFontSize));
        // Starts under the bar number's first glyph.
        const int glyphX = playheadPositionLabel->getX() + playheadPositionLabel->barsGlyphX();
        g.drawText(clockText_, l.clock.withLeft(glyphX), juce::Justification::centredLeft, false);
    }

    // The headline playhead carries its caption at the top-right.
    if (!l.cursorFrame.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION).withAlpha(0.5f));
        g.setFont(FontManager::getInstance().getUIFont(transport::kTimecodeCaptionFontSize));
        g.drawText(transport::kCursorCaption, l.cursorFrame.reduced(4, 2),
                   juce::Justification::topRight, false);
    }

    paintStackCaptions(g);
    paintMemory(g);

    // CPU frame, with a fill rising behind the value as the load does.
    if (l.rightClusterVisible) {
        auto frameBounds = l.cpu.toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL));
        g.fillRoundedRectangle(frameBounds, 4.0f);

        const auto sepY = static_cast<float>(l.cpuValue.getY());
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

        g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL_BORDER));
        g.drawRoundedRectangle(frameBounds.reduced(0.5f), 4.0f, 1.0f);
    }

    // Bottom border for visual separation from content below
    g.setColour(ActiveTheme::getBorderColour());
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
    // The layout itself lives in TransportLayout: each group measures itself
    // through the same code that places it, then sections are dropped into the
    // overflow menu in the declared priority order until the survivors fit.
    // Nothing here decides what fits.
    layout_ = transport::compute(getWidth(), getHeight(), transport::measureTextWidths(),
                                 LayoutConfig::getInstance().densityScale, style_);
    const auto& l = layout_;

    // The headline playhead stands alone in its box; the Justified stack draws
    // every readout small, captioned on the left rather than lettered.
    const bool headline = !l.cursorFrame.isEmpty();
    const bool stacked = !l.stackFrame.isEmpty();
    playheadPositionLabel->setFontSize(headline ? transport::kHeadlineTimecodeFontSize
                                                : transport::kStackTimecodeFontSize);
    playheadPositionLabel->setOverlayLabel("");
    playheadPositionLabel->setTrailingInset(headline ? l.cursorTrailingInset : 0);
    editCursorLabel->setFontSize(transport::kStackTimecodeFontSize);
    editCursorLabel->setOverlayLabel("");
    const float rangeFont =
        stacked ? transport::kStackTimecodeFontSize : transport::kRowTimecodeFontSize;
    for (auto* label : {selectionStartLabel.get(), loopStartLabel.get()}) {
        label->setFontSize(rangeFont);
        label->setOverlayLabel(stacked ? "" : "S");
    }
    for (auto* label : {selectionEndLabel.get(), loopEndLabel.get()}) {
        label->setFontSize(rangeFont);
        label->setOverlayLabel(stacked ? "" : "E");
    }
    punchStartLabel->setTrailingInset(l.punchTrailingInset);
    punchEndLabel->setTrailingInset(l.punchTrailingInset);

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
    editCursorLabel->setVisible(!l.editCursor.isEmpty());
    gridDivisionButton->setVisible(l.gridVisible);
    autoGridButton->setVisible(l.gridVisible);
    snapButton->setVisible(l.gridVisible);
    cpuTitleLabel->setVisible(l.rightClusterVisible);
    cpuValueLabel->setVisible(l.rightClusterVisible);
    qwertyKeyboardButton->setVisible(l.rightClusterVisible);
    keepButton->setVisible(l.rightClusterVisible);
    overflowButton->setVisible(l.overflowVisible);
    updateRangeVisibility();
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
    keyReadout->setBounds(l.key);

    selChipButton->setBounds(l.selChip);
    loopChipButton->setBounds(l.loopChip);
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

    keepButton->setBounds(l.keep);
    qwertyKeyboardButton->setBounds(l.qwerty);
    overflowButton->setBounds(l.overflow);
    automationWriteLabel->setBounds(l.automationWriteLabel);

    cpuTitleLabel->setBounds(l.cpuTitle);
    cpuValueLabel->setBounds(l.cpuValue);
    repaint();
}

// Selection and loop share the rows behind the chips in the Anchored and
// MemoryFill styles; the chips pick which pair is on.
void TransportPanel::updateRangeVisibility() {
    const auto& l = layout_;
    const bool shared = l.selectionAndLoopShareRows();
    const bool showSelection = l.selLoopTimesVisible && (!shared || !showLoopRange_);
    const bool showLoop = l.selLoopTimesVisible && (!shared || showLoopRange_);
    selectionStartLabel->setVisible(showSelection);
    selectionEndLabel->setVisible(showSelection);
    loopStartLabel->setVisible(showLoop);
    loopEndLabel->setVisible(showLoop);

    const bool chips = l.selLoopTimesVisible && shared;
    selChipButton->setVisible(chips);
    loopChipButton->setVisible(chips);
    selChipButton->setToggleState(!showLoopRange_, juce::dontSendNotification);
    loopChipButton->setToggleState(showLoopRange_, juce::dontSendNotification);
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
    styleTransportButton(*playButton, ActiveTheme::ACCENT_PRIMARY);
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
    styleTransportButton(*stopButton, ActiveTheme::ACCENT_PRIMARY);
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
    styleTransportButton(*recordButton, ActiveTheme::STATUS_ERROR);
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
    styleTransportButton(*automationWriteButton, ActiveTheme::ACCENT_MODULATION);
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
    styleTransportButton(*pauseButton, ActiveTheme::ACCENT_PRIMARY);
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
    styleTransportButton(*homeButton, ActiveTheme::ACCENT_PRIMARY);
    homeButton->onClick = [this]() {
        if (onGoHome)
            onGoHome();
    };
    addAndMakeVisible(*homeButton);

    // Prev button
    prevButton =
        std::make_unique<SvgButton>("Prev", BinaryData::prev_svg, BinaryData::prev_svgSize);
    styleTransportButton(*prevButton, ActiveTheme::ACCENT_PRIMARY);
    prevButton->onClick = [this]() {
        if (onGoToPrev)
            onGoToPrev();
    };
    addAndMakeVisible(*prevButton);

    // Next button
    nextButton =
        std::make_unique<SvgButton>("Next", BinaryData::next_svg, BinaryData::next_svgSize);
    styleTransportButton(*nextButton, ActiveTheme::ACCENT_PRIMARY);
    nextButton->onClick = [this]() {
        if (onGoToNext)
            onGoToNext();
    };
    addAndMakeVisible(*nextButton);

    // Loop button
    loopButton =
        std::make_unique<SvgButton>("Loop", BinaryData::loop_svg, BinaryData::loop_svgSize);
    styleTransportButton(*loopButton, ActiveTheme::ACCENT_PRIMARY);
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
    styleTransportButton(*backToArrangementButton, ActiveTheme::ACCENT_ATTENTION);
    backToArrangementButton->onClick = [this]() {
        if (onBackToArrangement)
            onBackToArrangement();
    };
    addAndMakeVisible(*backToArrangementButton);

    // QWERTY MIDI keyboard toggle
    qwertyKeyboardButton = std::make_unique<SvgButton>(
        "QwertyKeyboard", BinaryData::midi_qwerty_svg, BinaryData::midi_qwerty_svgSize);
    styleTransportButton(*qwertyKeyboardButton, ActiveTheme::ACCENT_MODULATION);
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
    styleTransportButton(*punchInButton, ActiveTheme::ACCENT_MODULATION, true);
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
    styleTransportButton(*punchOutButton, ActiveTheme::ACCENT_MODULATION, true);
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
        label->setOverlayColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM));
        label->setPacked(true);
        label->setFontSize(transport::kRowTimecodeFontSize);
        label->setTextColour(textColour);
        addAndMakeVisible(*label);
    };

    auto accentBlue = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    auto accentOrange = ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);

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
            auto green = ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE);
            loopStartLabel->setTextColour(green);
            loopEndLabel->setTextColour(green);
            if (onLoop)
                onLoop(true);
        }
    };

    auto dimColour = ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);
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
    auto accentPurple = ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION);

    setupBBTLabel(punchStartLabel, "", accentPurple);
    punchStartLabel->onValueChange = [this]() {
        double startBeats = punchStartLabel->getValue();
        double startSeconds = (startBeats * 60.0) / currentTempo;
        if (onPunchRegionEdit)
            onPunchRegionEdit(startSeconds, cachedPunchEnd);
    };

    setupBBTLabel(punchEndLabel, "", accentPurple);
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
    tempoLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION));
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
    timeSigNumeratorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
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
    timeSigDenominatorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
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
                              ActiveTheme::getColour(ActiveTheme::SURFACE).darker(0.2f));
    autoGridButton->setColour(juce::TextButton::buttonOnColourId,
                              ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION).darker(0.3f));
    autoGridButton->setColour(juce::TextButton::textColourOffId,
                              ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
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
    gridNumeratorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));
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
                              ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
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
    gridDenominatorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));
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
    styleTransportButton(*metronomeButton, ActiveTheme::ACCENT_PRIMARY);
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
    styleTransportButton(*countInButton, ActiveTheme::ACCENT_PRIMARY, true);
    countInButton->onClick = [this]() { showCountInMenu(); };
    addAndMakeVisible(*countInButton);
    setCountInMode(countInMode_);

    // Snap button (text-based toggle)
    snapButton = std::make_unique<juce::TextButton>(transport::kSnapCaption);
    snapButton->setColour(juce::TextButton::buttonColourId,
                          ActiveTheme::getColour(ActiveTheme::SURFACE).darker(0.2f));
    snapButton->setColour(juce::TextButton::buttonOnColourId,
                          ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION).darker(0.3f));
    snapButton->setColour(juce::TextButton::textColourOffId,
                          ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
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
    const auto accentColor = ActiveTheme::getColour(accentRole);
    button.setActiveColor(accentColor);
    button.setPressedColor(accentColor);
    button.setHoverColor(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    button.setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    button.setIconPadding(0.0f);

    // Transport SVGs are geometry templates. Their stable source keys are
    // replaced at paint time; no active colour is stored in a second asset.
    button.setStateColourReplacement(juce::Colour(0xFF1A1A1A), ActiveTheme::TRANSPORT_TILE,
                                     accentRole);
    button.setStateColourReplacement(juce::Colour(0xFF444444), ActiveTheme::TRANSPORT_WELL_BORDER,
                                     accentRole);
    const auto activeGlyphRole = activeGlyphUsesAccent ? accentRole : ActiveTheme::TEXT_BRIGHT;
    button.setStateColourReplacement(juce::Colour(0xFFBCBCBC), ActiveTheme::TRANSPORT_GLYPH,
                                     activeGlyphRole);
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), ActiveTheme::ICON_NEUTRAL,
                                     activeGlyphRole);
}

void TransportPanel::setPlayheadPosition(double positionInSeconds) {
    cachedPlayheadPosition = positionInSeconds;
    if (auto text = transport::clockText(positionInSeconds); text != clockText_) {
        clockText_ = text;
        repaint(layout_.clock);
    }

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
    auto colour = hasValidLoop ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                               : ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);
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

    for (auto* label : {playheadPositionLabel.get(), editCursorLabel.get(),
                        selectionStartLabel.get(), selectionEndLabel.get(), loopStartLabel.get(),
                        loopEndLabel.get(), punchStartLabel.get(), punchEndLabel.get()})
        label->setTimeSignature(timeSignatureNumerator, timeSignatureDenominator);

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
    auto activeColor = ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION);
    auto inactiveColor = ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);

    // Punch start label color matches punch in button state
    punchStartLabel->setTextColour(isPunchInEnabled ? activeColor : inactiveColor);

    // Punch end label color matches punch out button state
    punchEndLabel->setTextColour(isPunchOutEnabled ? activeColor : inactiveColor);
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
    tempoLabel->setFontSize(transport::kHeadlineFontSize);
    timeSigNumeratorLabel->setFontSize(transport::kReadoutFontSize);
    timeSigDenominatorLabel->setFontSize(transport::kReadoutFontSize);
}

void TransportPanel::applyThemedLabelColours() {
    // overflowButton is created last in the constructor, so its presence means
    // every child below exists. Guards against a look-and-feel change arriving
    // before construction finishes.
    if (overflowButton == nullptr)
        return;

    const auto accentBlue = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    const auto accentOrange = ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);
    const auto secondary = ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);

    // BPM readout.
    tempoLabel->setTextColour(accentOrange);

    // Bars/beats "measures" readouts.
    selectionStartLabel->setTextColour(accentBlue);
    selectionEndLabel->setTextColour(accentBlue);
    playheadPositionLabel->setTextColour(accentOrange);
    editCursorLabel->setTextColour(accentOrange);

    // Time-signature digits and the grid numerator/denominator/slash readouts.
    timeSigNumeratorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    timeSigDenominatorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    gridNumeratorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));
    gridDenominatorLabel->setTextColour(ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));
    gridSlashLabel->setColour(juce::Label::textColourId, secondary);

    // Loop labels: green when a valid loop is active, dim otherwise (mirrors
    // setLoopRegion), recomputed from cached loop state.
    const bool hasValidLoop =
        cachedLoopEnabled && cachedLoopEnd > cachedLoopStart && cachedLoopStart >= 0.0;
    const auto loopColour = hasValidLoop ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                                         : ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);
    loopStartLabel->setTextColour(loopColour);
    loopEndLabel->setTextColour(loopColour);

    // Punch labels track their arm state and the active palette.
    updatePunchLabelColors();

    cpuTitleLabel->setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM));
    cpuValueLabel->setColour(juce::Label::textColourId, secondary);
    automationWriteLabel->setColour(juce::Label::textColourId,
                                    ActiveTheme::getColour(ActiveTheme::ACCENT_MODULATION));

    overflowButton->setNormalColor(ActiveTheme::getSecondaryTextColour());
    overflowButton->setActiveBackgroundColor(accentBlue.darker(0.6f));

    // AUTO/SNAP capture concrete colours at construction; re-apply them so a
    // live theme switch restyles the toggles instead of leaving the old
    // palette behind.
    applyToggleColours();
    for (auto* label : {selectionStartLabel.get(), selectionEndLabel.get(), loopStartLabel.get(),
                        loopEndLabel.get(), playheadPositionLabel.get(), editCursorLabel.get(),
                        punchStartLabel.get(), punchEndLabel.get()})
        label->setOverlayColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM));

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
    repaint(layout_.cpu);
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
