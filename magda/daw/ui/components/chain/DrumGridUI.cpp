#include "DrumGridUI.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include <set>

#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace te = tracktion::engine;

namespace magda::daw::ui {

// =============================================================================
// PadButton
// =============================================================================

DrumGridUI::PadButton::PadButton() {
    playButton_ = std::make_unique<magda::SvgButton>("Play", BinaryData::play_bare_svg,
                                                     BinaryData::play_bare_svgSize);
    playButton_->setSize(16, 16);
    playButton_->setInterceptsMouseClicks(false, false);  // We handle mouse events
    addChildComponent(*playButton_);
}

void DrumGridUI::PadButton::setPadIndex(int index) {
    padIndex_ = index;
}

void DrumGridUI::PadButton::setNoteName(const juce::String& name) {
    if (noteName_ != name) {
        noteName_ = name;
        repaint();
    }
}

void DrumGridUI::PadButton::setSampleName(const juce::String& name) {
    if (sampleName_ != name) {
        sampleName_ = name;
        repaint();
    }
}

void DrumGridUI::PadButton::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void DrumGridUI::PadButton::setHasSample(bool has) {
    if (hasSample_ != has) {
        hasSample_ = has;
        if (playButton_)
            playButton_->setVisible(hasSample_);
        repaint();
    }
}

void DrumGridUI::PadButton::setMuted(bool muted) {
    if (muted_ != muted) {
        muted_ = muted;
        repaint();
    }
}

void DrumGridUI::PadButton::setSoloed(bool soloed) {
    if (soloed_ != soloed) {
        soloed_ = soloed;
        repaint();
    }
}

void DrumGridUI::PadButton::setTriggered(bool triggered) {
    if (triggered_ != triggered) {
        triggered_ = triggered;
        repaint();
    }
}

void DrumGridUI::PadButton::resized() {
    if (playButton_) {
        constexpr int btnSize = 16;
        playButton_->setBounds(getWidth() - btnSize - 3, getHeight() - btnSize - 3, btnSize,
                               btnSize);
    }
}

void DrumGridUI::PadButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(2);

    // Background colour
    juce::Colour bg;
    float borderThickness;
    if (triggered_) {
        bg = juce::Colour(0xFF5A5A2A);
        borderThickness = 1.5f;
    } else if (selected_) {
        bg = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.4f);
        borderThickness = 1.5f;
    } else if (hasSample_) {
        bg = DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.1f);
        borderThickness = 0.75f;
    } else {
        bg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f);
        borderThickness = 0.5f;
    }

    // Dim if muted
    if (muted_)
        bg = bg.withMultipliedAlpha(0.5f);

    g.setColour(bg);
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Border
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, borderThickness);

    // Solo indicator — orange top bar
    if (soloed_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillRoundedRectangle(bounds.removeFromTop(3).toFloat(), 1.0f);
    }

    auto textArea = getLocalBounds().reduced(4);

    if (hasSample_) {
        // --- Filled pad: note name top, plugin/sample name bottom ---
        auto topRow = textArea.removeFromTop(textArea.getHeight() / 3);
        auto bottomRow = textArea;

        // Note name (small, secondary)
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText(noteName_, topRow, juce::Justification::centredBottom, false);

        // Plugin/sample name (primary, truncated)
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.setColour(DarkTheme::getTextColour());
        g.drawText(sampleName_, bottomRow, juce::Justification::centred, true);
    } else {
        // --- Empty pad: note name centred ---
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText(noteName_, textArea, juce::Justification::centred, false);
    }
}

void DrumGridUI::PadButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onRightClicked)
            onRightClicked(padIndex_, e.getScreenPosition());
        return;
    }

    // Check if click is on the play button area
    if (playButton_ && playButton_->isVisible() &&
        playButton_->getBounds().contains(e.getPosition())) {
        playPressed_ = true;
        if (onNotePreview)
            onNotePreview(padIndex_, true);
        return;
    }

    if (onClicked)
        onClicked(padIndex_);
}

void DrumGridUI::PadButton::mouseDrag(const juce::MouseEvent& e) {
    if (playPressed_ || !hasSample_)
        return;

    if (e.getDistanceFromDragStart() < 4)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
        auto* dragInfo = new juce::DynamicObject();
        dragInfo->setProperty("type", "pad");
        dragInfo->setProperty("padIndex", padIndex_);
        container->startDragging(juce::var(dragInfo), this);
    }
}

void DrumGridUI::PadButton::mouseUp(const juce::MouseEvent& /*e*/) {
    if (playPressed_) {
        playPressed_ = false;
        if (onNotePreview)
            onNotePreview(padIndex_, false);
    }
}

// =============================================================================
// DrumGridUI
// =============================================================================

DrumGridUI::DrumGridUI() {
    // Setup pad buttons
    for (int i = 0; i < kPadsPerPage; ++i) {
        padButtons_[static_cast<size_t>(i)].onClicked = [this](int padIndex) {
            if (padIndex == selectedPad_) {
                setDetailCollapsed(!detailCollapsed_);
            } else {
                setDetailCollapsed(false);
                setSelectedPad(padIndex);
            }
        };
        padButtons_[static_cast<size_t>(i)].onNotePreview = [this](int padIndex, bool isNoteOn) {
            if (onNotePreview)
                onNotePreview(padIndex, isNoteOn);
        };
        padButtons_[static_cast<size_t>(i)].onRightClicked = [this](int padIndex,
                                                                    juce::Point<int> screenPos) {
            showPadContextMenu(padIndex, screenPos);
        };
        addAndMakeVisible(padButtons_[static_cast<size_t>(i)]);
    }

    // Pagination
    setupButton(prevPageButton_);
    prevPageButton_.onClick = [this]() { goToPrevPage(); };
    addAndMakeVisible(prevPageButton_);

    setupButton(nextPageButton_);
    nextPageButton_.onClick = [this]() { goToNextPage(); };
    addAndMakeVisible(nextPageButton_);

    pageLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    pageLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    pageLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel_);

    // Detail panel labels
    setupLabel(detailPadNameLabel_, "Pad 0 - C2", 11.0f);
    detailPadNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    setupLabel(detailSampleNameLabel_, "(empty)", 10.0f);
    setupLabel(levelLabel_, "LEVEL", 9.0f);
    setupLabel(panLabel_, "PAN", 9.0f);

    // Level slider (-60 to +12 dB)
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onPadLevelChanged)
            onPadLevelChanged(selectedPad_, static_cast<float>(value));
    };
    addAndMakeVisible(levelSlider_);

    // Pan slider (-1 to +1)
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);
    panSlider_.setValueFormatter([](double v) {
        if (std::abs(v) < 0.01)
            return juce::String("C");
        if (v < 0)
            return juce::String(static_cast<int>(-v * 100)) + "L";
        return juce::String(static_cast<int>(v * 100)) + "R";
    });
    panSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim().toUpperCase();
        if (t == "C" || t == "0")
            return 0.0;
        if (t.endsWithIgnoreCase("L"))
            return -t.dropLastCharacters(1).trim().getDoubleValue() / 100.0;
        if (t.endsWithIgnoreCase("R"))
            return t.dropLastCharacters(1).trim().getDoubleValue() / 100.0;
        return t.getDoubleValue();
    });
    panSlider_.onValueChanged = [this](double value) {
        if (onPadPanChanged)
            onPadPanChanged(selectedPad_, static_cast<float>(value));
    };
    addAndMakeVisible(panSlider_);

    // Mute/Solo buttons
    setupButton(muteButton_);
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        bool muted = muteButton_.getToggleState();
        padInfos_[static_cast<size_t>(selectedPad_)].mute = muted;
        if (onPadMuteChanged)
            onPadMuteChanged(selectedPad_, muted);
        refreshPadButtons();
    };
    addAndMakeVisible(muteButton_);

    setupButton(soloButton_);
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        bool soloed = soloButton_.getToggleState();
        padInfos_[static_cast<size_t>(selectedPad_)].solo = soloed;
        if (onPadSoloChanged)
            onPadSoloChanged(selectedPad_, soloed);
        refreshPadButtons();
    };
    addAndMakeVisible(soloButton_);

    // Load/Clear buttons
    setupButton(loadButton_);
    loadButton_.onClick = [this]() {
        if (onLoadRequested)
            onLoadRequested(selectedPad_);
    };
    addAndMakeVisible(loadButton_);

    setupButton(clearButton_);
    clearButton_.onClick = [this]() {
        if (onClearRequested)
            onClearRequested(selectedPad_);
    };
    addAndMakeVisible(clearButton_);

    // Per-pad FX chain panel
    addAndMakeVisible(padChainPanel_);
    padChainPanel_.onLayoutChanged = [this]() {
        resized();
        repaint();
        if (onLayoutChanged)
            onLayoutChanged();
    };

    // Chains panel
    chainsLabel_.setText("Chains:", juce::dontSendNotification);
    chainsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    chainsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    chainsLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(chainsLabel_);

    chainsViewport_.setScrollBarsShown(true, false);
    chainsViewport_.setInterceptsMouseClicks(false, true);
    chainsContainer_.setInterceptsMouseClicks(false, true);
    chainsViewport_.setViewedComponent(&chainsContainer_, false);
    addAndMakeVisible(chainsViewport_);

    chainsToggleButton_ = std::make_unique<magda::SvgButton>("Chains", BinaryData::menu_svg,
                                                             BinaryData::menu_svgSize);
    chainsToggleButton_->setClickingTogglesState(true);
    chainsToggleButton_->setToggleState(chainsPanelVisible_, juce::dontSendNotification);
    chainsToggleButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    chainsToggleButton_->setActiveColor(juce::Colours::white);
    chainsToggleButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f));
    chainsToggleButton_->setActive(chainsPanelVisible_);
    chainsToggleButton_->onClick = [this]() {
        setChainsPanelVisible(chainsToggleButton_->getToggleState());
        chainsToggleButton_->setActive(chainsToggleButton_->getToggleState());
    };
    addAndMakeVisible(*chainsToggleButton_);

    // Tab buttons for chains panel (Mix / Range)
    auto setupTabButton = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        btn.setColour(juce::TextButton::buttonOnColourId,
                      DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f));
        btn.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1001);
        btn.setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        addAndMakeVisible(btn);
    };
    setupTabButton(mixTabButton_);
    setupTabButton(rangeTabButton_);

    mixTabButton_.setToggleState(true, juce::dontSendNotification);

    mixTabButton_.onClick = [this]() {
        currentChainsTab_ = ChainsTab::Mix;
        resized();
        repaint();
    };
    rangeTabButton_.onClick = [this]() {
        currentChainsTab_ = ChainsTab::Range;
        resized();
        repaint();
    };

    // Initialize
    refreshPadButtons();
    refreshDetailPanel();
}

DrumGridUI::~DrumGridUI() {
    stopTimer();
    mixTabButton_.setLookAndFeel(nullptr);
    rangeTabButton_.setLookAndFeel(nullptr);
}

void DrumGridUI::setDrumGridPlugin(daw::audio::DrumGridPlugin* plugin) {
    drumGridPlugin_ = plugin;
    if (drumGridPlugin_) {
        startTimer(50);  // 20fps polling
        // Restore detail collapsed state
        detailCollapsed_ = drumGridPlugin_->state.getProperty("uiDetailCollapsed", false);
    }
}

void DrumGridUI::timerCallback() {
    if (!drumGridPlugin_)
        return;

    int pageStart = currentPage_ * kPadsPerPage;
    for (int i = 0; i < kTotalPads; ++i) {
        if (drumGridPlugin_->consumePadTrigger(i)) {
            // Only flash pads on the current page
            int btnIdx = i - pageStart;
            if (btnIdx >= 0 && btnIdx < kPadsPerPage) {
                padButtons_[static_cast<size_t>(btnIdx)].setTriggered(true);

                auto safeThis = juce::Component::SafePointer<DrumGridUI>(this);
                int capturedBtnIdx = btnIdx;
                juce::Timer::callAfterDelay(100, [safeThis, capturedBtnIdx]() {
                    if (safeThis)
                        safeThis->padButtons_[static_cast<size_t>(capturedBtnIdx)].setTriggered(
                            false);
                });
            }
        }
    }

    // Sync chain properties (level/pan/mute/solo) back into padInfos_ and UI
    // so that external changes (e.g. mixer sub-channel faders) are reflected
    bool detailNeedsRefresh = false;
    for (int i = 0; i < kTotalPads; ++i) {
        auto& info = padInfos_[static_cast<size_t>(i)];
        if (info.chainIndex < 0)
            continue;

        const auto* chain = drumGridPlugin_->getChainByIndex(info.chainIndex);
        if (!chain)
            continue;

        float chainLevel = chain->level.get();
        float chainPan = chain->pan.get();
        bool chainMute = chain->mute.get();
        bool chainSolo = chain->solo.get();

        bool changed = false;
        if (std::abs(info.level - chainLevel) > 0.01f) {
            info.level = chainLevel;
            changed = true;
        }
        if (std::abs(info.pan - chainPan) > 0.001f) {
            info.pan = chainPan;
            changed = true;
        }
        if (info.mute != chainMute) {
            info.mute = chainMute;
            changed = true;
        }
        if (info.solo != chainSolo) {
            info.solo = chainSolo;
            changed = true;
        }

        if (changed) {
            // Update chain row if visible
            for (auto& row : chainRows_) {
                if (row->getPadIndex() == i) {
                    juce::String displayName = getNoteName(i) + " " + info.sampleName;
                    row->updateFromPad(displayName, info.level, info.pan, info.mute, info.solo);
                    break;
                }
            }
            if (i == selectedPad_)
                detailNeedsRefresh = true;
        }
    }

    if (detailNeedsRefresh)
        refreshDetailPanel();
}

void DrumGridUI::updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                               float levelDb, float pan, int chainIndex, bool bypassed) {
    if (padIndex < 0 || padIndex >= kTotalPads)
        return;

    auto& info = padInfos_[static_cast<size_t>(padIndex)];
    info.sampleName = sampleName;
    info.mute = mute;
    info.solo = solo;
    info.bypassed = bypassed;
    info.level = levelDb;
    info.pan = pan;
    info.chainIndex = chainIndex;

    // Update visible pad buttons if this pad is on the current page
    int pageStart = currentPage_ * kPadsPerPage;
    if (padIndex >= pageStart && padIndex < pageStart + kPadsPerPage) {
        int btnIdx = padIndex - pageStart;
        auto& btn = padButtons_[static_cast<size_t>(btnIdx)];
        btn.setSampleName(sampleName);
        btn.setHasSample(sampleName.isNotEmpty());
        btn.setMuted(mute);
        btn.setSoloed(solo);
    }

    // Update detail panel if this is the selected pad
    if (padIndex == selectedPad_) {
        refreshDetailPanel();
        padChainPanel_.refresh();
    }

    // Rebuild chain rows to reflect updated pad state
    rebuildChainRows();
}

void DrumGridUI::setSelectedPad(int padIndex) {
    if (padIndex < 0 || padIndex >= kTotalPads)
        return;

    selectedPad_ = padIndex;

    // Switch page if needed
    int targetPage = padIndex / kPadsPerPage;
    if (targetPage != currentPage_) {
        currentPage_ = targetPage;
        refreshPadButtons();
    } else {
        // Just update selection highlight
        int pageStart = currentPage_ * kPadsPerPage;
        for (int i = 0; i < kPadsPerPage; ++i) {
            padButtons_[static_cast<size_t>(i)].setSelected(pageStart + i == selectedPad_);
        }
    }

    refreshDetailPanel();
    padChainPanel_.showPadChain(padIndex);

    // Update chain row selection highlights — select the row whose chain covers the selected pad
    int selectedChainIdx = padInfos_[static_cast<size_t>(selectedPad_)].chainIndex;
    for (auto& row : chainRows_) {
        int rowPad = row->getPadIndex();
        int rowChainIdx = padInfos_[static_cast<size_t>(rowPad)].chainIndex;
        row->setSelected(rowChainIdx >= 0 && rowChainIdx == selectedChainIdx);
    }
    for (auto& row : rangeRows_) {
        int rowPad = row->getPadIndex();
        int rowChainIdx = padInfos_[static_cast<size_t>(rowPad)].chainIndex;
        row->setSelected(rowChainIdx >= 0 && rowChainIdx == selectedChainIdx);
    }

    // Scroll chains viewport to show the selected row
    if (currentChainsTab_ == ChainsTab::Mix) {
        for (auto& row : chainRows_) {
            if (row->isSelected()) {
                chainsViewport_.setViewPosition(0, row->getY());
                break;
            }
        }
    } else {
        for (auto& row : rangeRows_) {
            if (row->isSelected()) {
                chainsViewport_.setViewPosition(0, row->getY());
                break;
            }
        }
    }

    resized();
    if (onLayoutChanged)
        onLayoutChanged();
}

// =============================================================================
// FileDragAndDropTarget
// =============================================================================

bool DrumGridUI::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") ||
            f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac") ||
            f.endsWithIgnoreCase(".ogg") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void DrumGridUI::filesDropped(const juce::StringArray& files, int x, int y) {
    // Find which pad the file was dropped on
    int btnIdx = padButtonIndexAtPoint({x, y});
    if (btnIdx < 0)
        return;

    int padIndex = currentPage_ * kPadsPerPage + btnIdx;

    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile() && onSampleDropped) {
            setSelectedPad(padIndex);
            onSampleDropped(padIndex, file);
            break;
        }
    }
}

// =============================================================================
// Paint
// =============================================================================

void DrumGridUI::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));

    // Dividers — positioned to match right-to-left layout
    bool selectedPadHasContent =
        padInfos_[static_cast<size_t>(selectedPad_)].sampleName.isNotEmpty();
    bool showDetailPanel = selectedPadHasContent;

    auto divArea = getLocalBounds().reduced(6);
    float top = static_cast<float>(divArea.getY());
    float bottom = static_cast<float>(divArea.getBottom());
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));

    // Draw dividers between panels
    if (showDetailPanel) {
        int detailLeft = padChainPanel_.getX() - kGap / 2;
        g.drawVerticalLine(detailLeft, top, bottom);
    }
    if (chainsPanelVisible_) {
        int chainsLeft = chainsViewport_.getX() - kGap / 2;
        g.drawVerticalLine(chainsLeft, top, bottom);
    }
}

void DrumGridUI::paintOverChildren(juce::Graphics& g) {
    // Drop highlight on pad (painted over children so it's visible on top of PadButton)
    if (dropHighlightPad_ >= 0) {
        int pageStart = currentPage_ * kPadsPerPage;
        int btnIdx = dropHighlightPad_ - pageStart;
        if (btnIdx >= 0 && btnIdx < kPadsPerPage) {
            auto padBounds = padButtons_[static_cast<size_t>(btnIdx)].getBounds();
            g.setColour(juce::Colours::yellow.withAlpha(0.4f));
            g.fillRoundedRectangle(padBounds.toFloat(), 3.0f);
            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.drawRoundedRectangle(padBounds.toFloat().reduced(1.0f), 3.0f, 1.5f);
        }
    }
}

// =============================================================================
// Layout
// =============================================================================

void DrumGridUI::resized() {
    auto area = getLocalBounds().reduced(4);

    bool selectedPadHasContent =
        padInfos_[static_cast<size_t>(selectedPad_)].sampleName.isNotEmpty();
    bool showDetailPanel = selectedPadHasContent && !detailCollapsed_;

    // --- Layout (right-to-left): [Pads] | [Chains] | [Detail] ---
    // Allocate from the right: detail → chains → toggle+pads get remainder

    // Left column: toggle button (always present)
    auto toggleCol = area.removeFromLeft(kToggleColWidth);
    chainsToggleButton_->setBounds(toggleCol.removeFromTop(20).withSizeKeepingCentre(20, 20));

    // Right side allocation
    auto rightBounds = area;

    // 1. DETAIL — from the right (reserve space for grid + chains)
    juce::Rectangle<int> detailArea;
    if (showDetailPanel) {
        int reservedWidth = kPadGridWidth + kGap;
        if (chainsPanelVisible_)
            reservedWidth += kChainsPanelWidth + kGap;
        int detailWidth = rightBounds.getWidth() - reservedWidth;
        detailArea = rightBounds.removeFromRight(juce::jmax(detailWidth, 0));
        rightBounds.removeFromRight(kGap);
    }

    // 2. CHAINS — fixed width from the right
    juce::Rectangle<int> chainsArea;
    if (chainsPanelVisible_) {
        chainsArea =
            rightBounds.removeFromRight(juce::jmin(kChainsPanelWidth, rightBounds.getWidth()));
        rightBounds.removeFromRight(kGap);
    }

    // 3. PADS — fixed width, left-aligned in remaining space
    auto gridArea = rightBounds.removeFromLeft(juce::jmin(kPadGridWidth, rightBounds.getWidth()));

    // --- Chains panel layout (FlexBox column) ---
    if (chainsPanelVisible_) {
        chainsLabel_.setVisible(true);
        chainsViewport_.setBounds(chainsArea);
        chainsViewport_.setVisible(true);
        mixTabButton_.setVisible(true);
        rangeTabButton_.setVisible(true);

        chainsLabel_.setVisible(false);

        // Reserve tab row space, but position tabs after we know the viewport content width
        auto tabRow = chainsArea.removeFromTop(20);
        chainsArea.removeFromTop(2);  // small gap below tabs

        chainsViewport_.setBounds(chainsArea);

        int scrollbarWidth = chainsViewport_.getScrollBarThickness();
        int containerWidth = chainsViewport_.getWidth() - scrollbarWidth;

        // Tab buttons — match the container width (excluding scrollbar)
        auto tabArea = tabRow.withWidth(containerWidth);
        int tabW = tabArea.getWidth() / 2;
        mixTabButton_.setBounds(tabArea.removeFromLeft(tabW));
        rangeTabButton_.setBounds(tabArea.withWidth(tabArea.getWidth()));

        // Add only the visible rows to the container
        chainsContainer_.removeAllChildren();

        int y = 0;
        if (currentChainsTab_ == ChainsTab::Mix) {
            for (auto& row : chainRows_) {
                row->setBounds(0, y, containerWidth, PadChainRowComponent::ROW_HEIGHT);
                chainsContainer_.addAndMakeVisible(*row);
                y += PadChainRowComponent::ROW_HEIGHT + 2;
            }
        } else {
            for (auto& row : rangeRows_) {
                row->setBounds(0, y, containerWidth, PadChainRangeRowComponent::ROW_HEIGHT);
                chainsContainer_.addAndMakeVisible(*row);
                y += PadChainRangeRowComponent::ROW_HEIGHT + 2;
            }
        }
        chainsContainer_.setSize(containerWidth, juce::jmax(y, chainsArea.getHeight()));
    } else {
        chainsLabel_.setVisible(false);
        chainsViewport_.setVisible(false);
        mixTabButton_.setVisible(false);
        rangeTabButton_.setVisible(false);
    }

    // --- Pad Grid layout ---
    auto paginationRow = gridArea.removeFromBottom(18);
    gridArea.removeFromBottom(2);

    constexpr int padGap = 3;
    constexpr int minPadSize = 40;
    constexpr int maxPadSize = 65;
    // Use only width to determine pad size — the grid width is fixed (kPadGridWidth),
    // so pad size should not fluctuate with container height changes (e.g., scrollbar toggling).
    int padSize = (gridArea.getWidth() - padGap * (kGridCols - 1)) / kGridCols;
    padSize = juce::jlimit(minPadSize, maxPadSize, padSize);

    for (int i = 0; i < kPadsPerPage; ++i) {
        int row = i / kGridCols;
        int col = i % kGridCols;
        // Flip rows so lowest pads (row 3) are at the bottom, like a drum machine
        int flippedRow = (kGridRows - 1) - row;
        int x = gridArea.getX() + col * (padSize + padGap);
        int y = gridArea.getY() + flippedRow * (padSize + padGap);
        padButtons_[static_cast<size_t>(i)].setBounds(x, y, padSize, padSize);
    }

    int btnW = 22;
    prevPageButton_.setBounds(paginationRow.removeFromLeft(btnW));
    nextPageButton_.setBounds(paginationRow.removeFromRight(btnW));
    pageLabel_.setBounds(paginationRow);

    // --- Detail Panel ---
    detailSampleNameLabel_.setVisible(false);
    levelSlider_.setVisible(false);
    panSlider_.setVisible(false);
    muteButton_.setVisible(false);
    soloButton_.setVisible(false);
    loadButton_.setVisible(false);
    clearButton_.setVisible(false);
    levelLabel_.setVisible(false);
    panLabel_.setVisible(false);
    detailPadNameLabel_.setVisible(false);

    if (showDetailPanel) {
        padChainPanel_.setBounds(detailArea);
        padChainPanel_.setVisible(true);
        // Dim pad chain panel when the selected pad is bypassed
        bool selectedBypassed = padInfos_[static_cast<size_t>(selectedPad_)].bypassed;
        padChainPanel_.setAlpha(selectedBypassed ? 0.35f : 1.0f);
    } else {
        padChainPanel_.setVisible(false);
    }
}

// =============================================================================
// PadChainPanel detail (wired from DeviceSlotComponent)
// =============================================================================

// =============================================================================
// DragAndDropTarget (plugin drops)
// =============================================================================

bool DrumGridUI::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        auto type = obj->getProperty("type").toString();
        bool interested = type == "plugin" || type == "pad";
        DBG("DrumGridUI::isInterestedInDragSource: " << (interested ? "YES" : "NO"));
        return interested;
    }
    return false;
}

void DrumGridUI::itemDragEnter(const SourceDetails& details) {
    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    if (btnIdx >= 0)
        dropHighlightPad_ = currentPage_ * kPadsPerPage + btnIdx;
    else
        dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDragMove(const SourceDetails& details) {
    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    int newHighlight = btnIdx >= 0 ? currentPage_ * kPadsPerPage + btnIdx : -1;
    if (newHighlight != dropHighlightPad_) {
        dropHighlightPad_ = newHighlight;
        repaint();
    }
}

void DrumGridUI::itemDragExit(const SourceDetails&) {
    dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDropped(const SourceDetails& details) {
    dropHighlightPad_ = -1;

    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    DBG("DrumGridUI::itemDropped at " << details.localPosition.toString() << " btnIdx=" << btnIdx);
    if (btnIdx < 0) {
        repaint();
        return;
    }

    int padIndex = currentPage_ * kPadsPerPage + btnIdx;

    if (auto* obj = details.description.getDynamicObject()) {
        auto type = obj->getProperty("type").toString();

        if (type == "pad") {
            int sourcePad = static_cast<int>(obj->getProperty("padIndex"));
            if (sourcePad != padIndex && onPadsSwapped)
                onPadsSwapped(sourcePad, padIndex);
        } else {
            setSelectedPad(padIndex);
            if (onPluginDropped)
                onPluginDropped(padIndex, *obj);
        }
    }

    repaint();
}

// =============================================================================
// Chains panel
// =============================================================================

void DrumGridUI::rebuildChainRows() {
    chainRows_.clear();
    rangeRows_.clear();
    chainsContainer_.removeAllChildren();

    // Build rows from padInfos — one row per pad that has a chain
    // (A chain may cover multiple pads; we show the row for the lowest pad in the range)
    std::set<int> seenChains;
    for (int i = 0; i < kTotalPads; ++i) {
        auto& info = padInfos_[static_cast<size_t>(i)];
        if (info.sampleName.isEmpty() || info.chainIndex < 0)
            continue;

        // Skip if we already created a row for this chain
        if (!seenChains.insert(info.chainIndex).second)
            continue;

        // --- Mix row ---
        auto row = std::make_unique<PadChainRowComponent>(i);
        juce::String displayName = getNoteName(i) + " " + info.sampleName;
        row->updateFromPad(displayName, info.level, info.pan, info.mute, info.solo, info.bypassed);

        row->onClicked = [this](int padIndex) {
            bool wasSelected = (padIndex == selectedPad_) ||
                               (padInfos_[static_cast<size_t>(padIndex)].chainIndex >= 0 &&
                                padInfos_[static_cast<size_t>(padIndex)].chainIndex ==
                                    padInfos_[static_cast<size_t>(selectedPad_)].chainIndex);
            if (wasSelected) {
                setDetailCollapsed(!detailCollapsed_);
            } else {
                setDetailCollapsed(false);
                setSelectedPad(padIndex);
            }
        };
        row->onLevelChanged = [this](int padIndex, float val) {
            if (onPadLevelChanged)
                onPadLevelChanged(padIndex, val);
        };
        row->onPanChanged = [this](int padIndex, float val) {
            if (onPadPanChanged)
                onPadPanChanged(padIndex, val);
        };
        row->onMuteChanged = [this](int padIndex, bool val) {
            padInfos_[static_cast<size_t>(padIndex)].mute = val;
            if (onPadMuteChanged)
                onPadMuteChanged(padIndex, val);
            refreshPadButtons();
        };
        row->onSoloChanged = [this](int padIndex, bool val) {
            padInfos_[static_cast<size_t>(padIndex)].solo = val;
            if (onPadSoloChanged)
                onPadSoloChanged(padIndex, val);
            refreshPadButtons();
        };
        row->onBypassChanged = [this](int padIndex, bool val) {
            padInfos_[static_cast<size_t>(padIndex)].bypassed = val;
            if (onPadBypassChanged)
                onPadBypassChanged(padIndex, val);
            // Update detail panel dimming if this is the selected pad
            if (padIndex == selectedPad_)
                padChainPanel_.setAlpha(val ? 0.35f : 1.0f);
        };
        row->onDeleteClicked = [this](int padIndex) {
            if (onPadDeleteRequested)
                onPadDeleteRequested(padIndex);
            else if (onClearRequested)
                onClearRequested(padIndex);
        };
        row->onRightClicked = [this](int padIndex, juce::Point<int> screenPos) {
            showChainContextMenu(padIndex, screenPos);
        };

        row->setSelected(i == selectedPad_);
        chainRows_.push_back(std::move(row));

        // --- Range row ---
        auto rangeRow = std::make_unique<PadChainRangeRowComponent>(i);
        juce::String rangeName = getNoteName(i) + " " + info.sampleName;

        // Query note range from DrumGridPlugin via callback
        int lowNote = i;
        int highNote = i;
        int rootNote = i;
        if (getNoteRange) {
            auto [lo, hi, rt] = getNoteRange(i);
            lowNote = lo;
            highNote = hi;
            rootNote = rt;
        }
        rangeRow->updateFromChain(rangeName, lowNote, highNote, rootNote);
        rangeRow->onClicked = [this](int padIndex) { setSelectedPad(padIndex); };
        rangeRow->onRangeChanged = [this](int padIndex, int lo, int hi, int rt) {
            if (onPadRangeChanged)
                onPadRangeChanged(padIndex, lo, hi, rt);
        };
        rangeRow->setSelected(i == selectedPad_);
        rangeRows_.push_back(std::move(rangeRow));
    }

    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void DrumGridUI::showPadContextMenu(int padIndex, juce::Point<int> screenPos) {
    setSelectedPad(padIndex);

    auto& info = padInfos_[static_cast<size_t>(padIndex)];
    if (info.sampleName.isEmpty())
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, padIndex](int result) {
                           if (result == 1 && onClearRequested)
                               onClearRequested(padIndex);
                       });
}

void DrumGridUI::showChainContextMenu(int padIndex, juce::Point<int> screenPos) {
    setSelectedPad(padIndex);

    juce::PopupMenu menu;
    menu.addItem(1, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, padIndex](int result) {
                           if (result == 1 && onClearRequested)
                               onClearRequested(padIndex);
                       });
}

int DrumGridUI::getPreferredContentWidth() const {
    bool selectedPadHasContent =
        padInfos_[static_cast<size_t>(selectedPad_)].sampleName.isNotEmpty();
    bool showDetailPanel = selectedPadHasContent && !detailCollapsed_;

    // Account for layout overhead from parent components:
    //   NodeComponent::resized() reduced(2,1) = 4px
    //   DeviceSlotComponent contentArea.reduced(4,2) = 8px
    //   DrumGridUI::resized() reduced(4) = 8px
    // Total: 20px horizontal consumed before content layout
    int width = 20 + kToggleColWidth + kPadGridWidth;
    if (chainsPanelVisible_)
        width += kGap + kChainsPanelWidth;
    if (showDetailPanel) {
        // Cap chain panel width so it doesn't expand indefinitely
        static constexpr int kMaxChainPanelWidth = 800;
        static constexpr int kMinChainPanelWidth = 80;
        int chainWidth = juce::jlimit(kMinChainPanelWidth, kMaxChainPanelWidth,
                                      padChainPanel_.getContentWidth());
        width += kGap + chainWidth;
    }

    return width;
}

void DrumGridUI::setChainsPanelVisible(bool visible) {
    if (chainsPanelVisible_ == visible)
        return;
    chainsPanelVisible_ = visible;
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

// =============================================================================
// Internal helpers
// =============================================================================

void DrumGridUI::setDetailCollapsed(bool collapsed) {
    if (detailCollapsed_ == collapsed)
        return;
    detailCollapsed_ = collapsed;
    if (drumGridPlugin_)
        drumGridPlugin_->state.setProperty("uiDetailCollapsed", collapsed, nullptr);
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void DrumGridUI::refreshPadButtons() {
    int pageStart = currentPage_ * kPadsPerPage;

    for (int i = 0; i < kPadsPerPage; ++i) {
        int padIdx = pageStart + i;
        auto& btn = padButtons_[static_cast<size_t>(i)];
        auto& info = padInfos_[static_cast<size_t>(padIdx)];

        btn.setPadIndex(padIdx);
        btn.setNoteName(getNoteName(padIdx));
        btn.setSampleName(info.sampleName);
        btn.setHasSample(info.sampleName.isNotEmpty());
        btn.setSelected(padIdx == selectedPad_);
        btn.setMuted(info.mute);
        btn.setSoloed(info.solo);
    }

    // Update page label
    pageLabel_.setText("Page " + juce::String(currentPage_ + 1) + "/" + juce::String(kNumPages),
                       juce::dontSendNotification);
    prevPageButton_.setEnabled(currentPage_ > 0);
    nextPageButton_.setEnabled(currentPage_ < kNumPages - 1);
}

void DrumGridUI::refreshDetailPanel() {
    auto& info = padInfos_[static_cast<size_t>(selectedPad_)];

    detailPadNameLabel_.setText("Pad " + juce::String(selectedPad_) + " - " +
                                    getNoteName(selectedPad_),
                                juce::dontSendNotification);

    if (info.sampleName.isNotEmpty()) {
        detailSampleNameLabel_.setText(info.sampleName, juce::dontSendNotification);
        detailSampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    } else {
        detailSampleNameLabel_.setText("(empty)", juce::dontSendNotification);
        detailSampleNameLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
    }

    levelSlider_.setValue(info.level, juce::dontSendNotification);
    panSlider_.setValue(info.pan, juce::dontSendNotification);

    muteButton_.setToggleState(info.mute, juce::dontSendNotification);
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_RED));
    soloButton_.setToggleState(info.solo, juce::dontSendNotification);
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
}

void DrumGridUI::goToPrevPage() {
    if (currentPage_ > 0) {
        --currentPage_;
        refreshPadButtons();
    }
}

void DrumGridUI::goToNextPage() {
    if (currentPage_ < kNumPages - 1) {
        ++currentPage_;
        refreshPadButtons();
    }
}

juce::String DrumGridUI::getNoteName(int padIndex) {
    int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
    return juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3);
}

int DrumGridUI::padButtonIndexAtPoint(juce::Point<int> point) const {
    for (int i = 0; i < kPadsPerPage; ++i) {
        if (padButtons_[static_cast<size_t>(i)].getBounds().contains(point))
            return i;
    }
    return -1;
}

void DrumGridUI::setupLabel(juce::Label& label, const juce::String& text, float fontSize) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(fontSize));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void DrumGridUI::setupButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    button.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
}

}  // namespace magda::daw::ui
