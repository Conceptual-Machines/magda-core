#include "drum_grid/DrumGridUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/DrumGridPads.hpp"
#include "core/TrackManager.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/chain/layout/DeviceSlotHeaderLayout.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/components/common/MasterSpeakerButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

std::atomic<int> DrumGridUI::nextFaderGesture_{0};

namespace {
constexpr double kMinPadDb = -60.0;
constexpr int kPadGap = 6;
constexpr int kPadPadding = 10;
constexpr int kMinPadsWidth = 268;
constexpr int kMaxPadsWidth = 396;
constexpr int kPanelHeaderHeight = 30;

/// The pads' width for a body this tall: a square grid within the spec's range.
int padsWidthFor(int bodyHeight) {
    const int cell = (bodyHeight - 2 * kPadPadding - 3 * kPadGap) / DrumGridUI::kGridRows;
    return juce::jlimit(kMinPadsWidth, kMaxPadsWidth,
                        DrumGridUI::kGridCols * cell + 3 * kPadGap + 2 * kPadPadding);
}
}  // namespace

// =============================================================================
// PadButton
// =============================================================================

DrumGridUI::PadButton::PadButton() {
    playButton_ = std::make_unique<magda::SvgButton>("Play", BinaryData::play_bare_svg,
                                                     BinaryData::play_bare_svgSize);
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

void DrumGridUI::PadButton::setStripeColour(juce::Colour colour) {
    if (stripe_ != colour) {
        stripe_ = colour;
        repaint();
    }
}

void DrumGridUI::PadButton::resized() {
    constexpr int size = 14;
    playButton_->setBounds(getWidth() - size - 6, 5, size, size);
}

void DrumGridUI::PadButton::mouseEnter(const juce::MouseEvent& /*e*/) {
    hovered_ = true;
    repaint();
}

void DrumGridUI::PadButton::mouseExit(const juce::MouseEvent& /*e*/) {
    hovered_ = false;
    repaint();
}

void DrumGridUI::PadButton::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };

    if (!hasSample_) {
        g.setColour(
            colour(selected_ ? ActiveTheme::DEVICE_ROW_SELECTED : ActiveTheme::DEVICE_PAD_EMPTY));
        g.fillRoundedRectangle(bounds, 5.0f);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, 5.0f);
        const float dashes[] = {4.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(colour(selected_ ? ActiveTheme::DEVICE_BLUE : ActiveTheme::DEVICE_LINE));
        g.fillPath(dashed);
        g.setColour(colour(ActiveTheme::DEVICE_SWITCH_OFF));
        g.setFont(fonts.getMonoFont(9.5f));
        g.drawText(noteName_, getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const auto fill = triggered_  ? ActiveTheme::DEVICE_PAD_HIT
                      : selected_ ? ActiveTheme::DEVICE_ROW_SELECTED
                      : hovered_  ? ActiveTheme::DEVICE_ROW_HOVER
                                  : ActiveTheme::DEVICE_FIELD;
    const auto border = triggered_  ? ActiveTheme::DEVICE_PAD_HIT_BORDER
                        : selected_ ? ActiveTheme::DEVICE_BLUE
                        : hovered_  ? ActiveTheme::DEVICE_LINE
                                    : ActiveTheme::DEVICE_FIELD_BORDER;
    g.setColour(colour(fill));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(colour(border));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    g.setColour(stripe_);
    g.fillRect(juce::Rectangle<float>(bounds.getX() + 1.0f, bounds.getY() + 5.0f, 2.0f,
                                      bounds.getHeight() - 10.0f));

    auto text = getLocalBounds().reduced(10, 7);
    g.setColour(colour(ActiveTheme::DEVICE_ICON_INACTIVE));
    g.setFont(fonts.getMonoFont(9.5f));
    g.drawText(noteName_, text.removeFromTop(14), juce::Justification::topLeft, false);
    if (soloed_ || muted_) {
        g.setColour(colour(soloed_ ? ActiveTheme::DEVICE_AMBER : ActiveTheme::DEVICE_RED));
        g.setFont(fonts.getMonoFont(8.5f).boldened());
        g.drawText(soloed_ ? "S" : "M", text.withHeight(14).translated(0, -14),
                   juce::Justification::topRight, false);
    }

    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(fonts.getUIFontMedium(11.5f));
    g.drawText(sampleName_, text.removeFromBottom(16), juce::Justification::bottomLeft, true);
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
    startTimer(50);  // pad triggers and the pads' mix, at 20 fps

    for (int i = 0; i < kPadsPerPage; ++i) {
        auto& pad = padButtons_[static_cast<size_t>(i)];
        pad.onClicked = [this](int padIndex) { setSelectedPad(padIndex); };
        pad.onNotePreview = [this](int padIndex, bool isNoteOn) {
            if (onNotePreview)
                onNotePreview(padIndex, isNoteOn);
        };
        pad.onRightClicked = [this](int padIndex, juce::Point<int> screenPos) {
            showPadContextMenu(padIndex, screenPos);
        };
        addAndMakeVisible(pad);
    }

    // Rail
    editorToggle_.setTooltip("Pad editor");
    editorToggle_.setClickingTogglesState(true);
    editorToggle_.setToggleState(!detailCollapsed_, juce::dontSendNotification);
    editorToggle_.onClick = [this]() { setDetailCollapsed(!editorToggle_.getToggleState()); };
    addAndMakeVisible(editorToggle_);

    // Pad editor: VOL
    levelControl_.setRange(kMinPadDb, 12.0, 0.0);
    panControl_.setRange(-1.0, 1.0, 0.0);
    for (auto* control : {&levelControl_, &panControl_}) {
        control->setDrawBackground(false);
        control->setDrawBorder(false);
        control->setShowFillIndicator(false);
        control->setShowText(false);
        control->onDragStart = [this]() { faderDragging_ = true; };
        control->onDragEnd = [this](double) {
            faderDragging_ = false;
            endFaderGesture();
        };
        addAndMakeVisible(*control);
    }
    levelControl_.onValueChange = [this]() {
        padInfos_[static_cast<size_t>(selectedPad_)].level =
            static_cast<float>(levelControl_.getValue());
        if (onPadLevelChanged)
            onPadLevelChanged(selectedPad_, static_cast<float>(levelControl_.getValue()));
        // A typed value or a reset has no drag to end, so it closes its own gesture (#2211).
        if (!faderDragging_)
            endFaderGesture();
        repaint(editorArea_);
    };
    panControl_.onValueChange = [this]() {
        padInfos_[static_cast<size_t>(selectedPad_)].pan =
            static_cast<float>(panControl_.getValue());
        if (onPadPanChanged)
            onPadPanChanged(selectedPad_, static_cast<float>(panControl_.getValue()));
        if (!faderDragging_)
            endFaderGesture();
        repaint(editorArea_);
    };
    outputButton_.setTooltip("Pad output");
    outputButton_.onClick = [this]() { showOutputMenu(); };
    addAndMakeVisible(outputButton_);

    // Pad editor: VEL and KEY, on the selected layer's zones
    velocityLowControl_.setRange(1.0, 127.0, 1.0);
    velocityHighControl_.setRange(1.0, 127.0, 127.0);
    for (auto* fade : {&velocityFadeLowControl_, &velocityFadeHighControl_})
        fade->setRange(0.0, 127.0, 0.0);
    for (auto* control : {&velocityLowControl_, &velocityHighControl_, &velocityFadeLowControl_,
                          &velocityFadeHighControl_}) {
        control->setSnapToInteger(true);
        control->setFont(FontManager::getInstance().getMonoFont(11.0f));
        control->onDragStart = [this]() { zoneDragging_ = true; };
        control->onDragEnd = [this](double) {
            zoneDragging_ = false;
            commitZones();
        };
        control->onValueChange = [this]() {
            if (!zoneDragging_)
                commitZones();
            repaint(editorArea_);
        };
        addChildComponent(*control);
    }
    roundRobinButton_.setClickingTogglesState(true);
    roundRobinButton_.setTooltip("Take turns with the pad's other round-robin layers");
    roundRobinButton_.onClick = [this]() {
        roundRobinButton_.setButtonText(roundRobinButton_.getToggleState() ? "On" : "Off");
        commitZones();
    };
    addChildComponent(roundRobinButton_);

    addLayerButton_.setTooltip("Add a layer to this pad");
    addLayerButton_.onClick = [this]() {
        if (onAddLayerRequested)
            onAddLayerRequested(selectedPad_);
    };
    layerListContent_.addAndMakeVisible(addLayerButton_);
    layerList_.setViewedComponent(&layerListContent_, false);
    layerList_.setScrollBarsShown(true, false);
    layerList_.setScrollBarThickness(6);
    addChildComponent(layerList_);

    // The selected layer's gain and pan, live and one undo step per drag.
    layerGainControl_.setRange(kMinPadDb, 6.0, 0.0);
    layerPanControl_.setRange(-1.0, 1.0, 0.0);
    for (auto* control : {&layerGainControl_, &layerPanControl_}) {
        control->setDrawBackground(false);
        control->setDrawBorder(false);
        control->setShowFillIndicator(false);
        control->setShowText(false);
        control->onDragStart = [this]() { faderDragging_ = true; };
        control->onDragEnd = [this](double) {
            faderDragging_ = false;
            endFaderGesture();
        };
        control->onValueChange = [this]() {
            if (const auto* layer = selectedLayer(); layer != nullptr && onLayerMixChanged)
                onLayerMixChanged(layer->id, static_cast<float>(layerGainControl_.getValue()),
                                  static_cast<float>(layerPanControl_.getValue()));
            if (!faderDragging_)
                endFaderGesture();
            repaint(editorArea_);
        };
        addChildComponent(*control);
    }

    // Chain: the rack's chain view on the selected pad
    padChainView_.onLayoutChanged = [this]() {
        if (onLayoutChanged)
            onLayoutChanged();
    };
    addChildComponent(padChainView_);
    emptyAddButton_.setTooltip("Add a device to this pad");
    emptyAddButton_.onClick = [this]() {
        if (onAddDeviceRequested)
            onAddDeviceRequested(selectedPad_);
    };
    addChildComponent(emptyAddButton_);

    for (auto* button : {&chainMuteButton_, &chainSoloButton_})
        addAndMakeVisible(*button);
    chainMuteButton_.setTooltip("Mute pad");
    chainMuteButton_.onClick = [this]() {
        const bool muted = chainMuteButton_.getToggleState();
        syncMuteGlyph(chainMuteButton_, muted);
        padInfos_[static_cast<size_t>(selectedPad_)].mute = muted;
        if (onPadMuteChanged)
            onPadMuteChanged(selectedPad_, muted);
        refreshPadButtons();
    };
    chainSoloButton_.setTooltip("Solo pad");
    chainSoloButton_.onClick = [this]() {
        const bool soloed = chainSoloButton_.getToggleState();
        padInfos_[static_cast<size_t>(selectedPad_)].solo = soloed;
        if (onPadSoloChanged)
            onPadSoloChanged(selectedPad_, soloed);
        refreshPadButtons();
    };

    // Footer
    prevPageButton_ = makeNavArrowButton("Previous page", 0.5f);
    nextPageButton_ = makeNavArrowButton("Next page", 0.0f);
    prevPageButton_->onClick = [this]() { goToPrevPage(); };
    nextPageButton_->onClick = [this]() { goToNextPage(); };
    addAndMakeVisible(*prevPageButton_);
    addAndMakeVisible(*nextPageButton_);

    styleControls();
    refreshPadButtons();
    refreshDetailPanel();
}

DrumGridUI::~DrumGridUI() {
    stopTimer();
    for (auto* button : {&editorToggle_, &outputButton_, &roundRobinButton_})
        button->setLookAndFeel(nullptr);
}

void DrumGridUI::styleControls() {
    using node_header::GlyphToggleLookAndFeel;
    auto& glyph = GlyphToggleLookAndFeel::getInstance();
    editorToggle_.setLookAndFeel(&glyph);
    editorToggle_.setColour(juce::TextButton::textColourOnId,
                            ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
    // The same mute / solo as every other view, in the chipless device style.
    node_header::applyDeviceMuteStyle(chainMuteButton_, 24.0f);
    node_header::applyDeviceSoloStyle(chainSoloButton_, 24.0f);
    outputButton_.setLookAndFeel(&glyph);
    roundRobinButton_.setLookAndFeel(&glyph);
}

void DrumGridUI::restoreDetailCollapsed(bool collapsed) {
    if (detailCollapsed_ == collapsed)
        return;
    detailCollapsed_ = collapsed;
    editorToggle_.setToggleState(!collapsed, juce::dontSendNotification);
    resized();
    repaint();
}

void DrumGridUI::timerCallback() {
    int pageStart = currentPage_ * kPadsPerPage;
    for (int i = 0; i < kTotalPads; ++i) {
        if (consumePadTrigger && consumePadTrigger(i)) {
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

        const auto mix = getPadMix ? getPadMix(i) : std::nullopt;
        if (!mix.has_value())
            continue;

        float chainLevel = mix->level;
        float chainPan = mix->pan;
        bool chainMute = mix->mute;
        bool chainSolo = mix->solo;
        int chainBusOutput = mix->busOutput;

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
        if (info.busOutput != chainBusOutput) {
            info.busOutput = chainBusOutput;
            changed = true;
        }

        if (changed && i == selectedPad_)
            detailNeedsRefresh = true;
    }

    if (detailNeedsRefresh)
        refreshDetailPanel();
}

void DrumGridUI::updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                               float levelDb, float pan, int chainIndex, bool bypassed,
                               int busOutput) {
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
    info.busOutput = busOutput;

    // Update visible pad buttons if this pad is on the current page
    int pageStart = currentPage_ * kPadsPerPage;
    if (padIndex >= pageStart && padIndex < pageStart + kPadsPerPage) {
        int btnIdx = padIndex - pageStart;
        auto& btn = padButtons_[static_cast<size_t>(btnIdx)];
        btn.setSampleName(sampleName);
        btn.setHasSample(sampleName.isNotEmpty());
        btn.setMuted(mute);
        btn.setSoloed(solo);
        btn.setStripeColour(device_shell::chainColour(chainIndex));
    }

    // Update detail panel if this is the selected pad
    if (padIndex == selectedPad_) {
        refreshDetailPanel();
        refreshPadChain();
    }
}

void DrumGridUI::setSelectedPad(int padIndex) {
    if (padIndex < 0 || padIndex >= kTotalPads)
        return;

    if (selectedPad_ != padIndex)
        selectedLayer_ = magda::INVALID_CHAIN_ID;
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
    refreshPadChain();

    resized();
    if (onLayoutChanged)
        onLayoutChanged();
}

// =============================================================================
// FileDragAndDropTarget
// =============================================================================

bool DrumGridUI::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::ranges::any_of(files, isAudioFile);
}

namespace {
int countAudioFilesDG(const juce::StringArray& files) {
    return static_cast<int>(std::ranges::count_if(files, isAudioFile));
}
}  // namespace

void DrumGridUI::fileDragEnter(const juce::StringArray& files, int x, int y) {
    fileDropCount_ = countAudioFilesDG(files);
    int btnIdx = padButtonIndexAtPoint({x, y});
    fileDropStartPad_ = (btnIdx >= 0) ? currentPage_ * kPadsPerPage + btnIdx : -1;
    repaint();
}

void DrumGridUI::fileDragMove(const juce::StringArray& /*files*/, int x, int y) {
    int btnIdx = padButtonIndexAtPoint({x, y});
    int newStart = (btnIdx >= 0) ? currentPage_ * kPadsPerPage + btnIdx : -1;
    if (newStart != fileDropStartPad_) {
        fileDropStartPad_ = newStart;
        repaint();
    }
}

void DrumGridUI::fileDragExit(const juce::StringArray& /*files*/) {
    fileDropStartPad_ = -1;
    fileDropCount_ = 0;
    repaint();
}

void DrumGridUI::filesDropped(const juce::StringArray& files, int x, int y) {
    fileDropStartPad_ = -1;
    fileDropCount_ = 0;
    repaint();

    int btnIdx = padButtonIndexAtPoint({x, y});
    if (btnIdx < 0 || !onSampleDropped)
        return;

    int padIndex = currentPage_ * kPadsPerPage + btnIdx;
    int lastAssigned = -1;

    for (const auto& f : files) {
        if (padIndex >= kTotalPads)
            break;

        juce::File file(f);
        if (!file.existsAsFile())
            continue;

        onSampleDropped(padIndex, file);
        lastAssigned = padIndex;
        ++padIndex;
    }

    if (lastAssigned >= 0)
        setSelectedPad(lastAssigned);
}

// =============================================================================
// Paint
// =============================================================================

void DrumGridUI::paint(juce::Graphics& g) {
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    const auto line = colour(ActiveTheme::DEVICE_LINE);

    g.setColour(colour(ActiveTheme::DEVICE_HEAD2));
    g.fillRect(railArea_);
    g.setColour(line);
    g.fillRect(railArea_.withLeft(railArea_.getRight() - 1));

    if (!editorArea_.isEmpty()) {
        g.setColour(colour(ActiveTheme::DEVICE_HEAD2));
        g.fillRect(editorArea_);
        g.setColour(line);
        g.fillRect(editorArea_.withWidth(1));
        paintEditor(g);
    }

    g.setColour(colour(ActiveTheme::DEVICE_PANEL));
    g.fillRect(chainArea_);
    g.setColour(line);
    g.fillRect(chainArea_.withWidth(1));
    paintChainHeader(g);

    if (!pageTextArea_.isEmpty()) {
        const int first = currentPage_ * kPadsPerPage;
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.setFont(FontManager::getInstance().getMonoFont(11.0f));
        g.drawText("Page " + juce::String(currentPage_ + 1) + "/" + juce::String(kNumPages) +
                       juce::String::fromUTF8(" \xc2\xb7 ") + getNoteName(first) +
                       juce::String::fromUTF8("\xe2\x80\x93") +
                       getNoteName(first + kPadsPerPage - 1),
                   pageTextArea_, juce::Justification::centred, false);
    }
}

void DrumGridUI::paintEditor(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    const auto& info = padInfos_[static_cast<size_t>(selectedPad_)];

    auto header = editorHeaderArea_.reduced(12, 0);
    g.setColour(device_shell::chainColour(info.chainIndex));
    g.fillEllipse(header.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    header.removeFromLeft(8);
    g.setColour(colour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getMonoFont(11.0f));
    g.drawText(getNoteName(selectedPad_), header, juce::Justification::centredRight, false);
    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(fonts.getUIFontMedium(12.0f));
    g.drawText(info.sampleName.isNotEmpty() ? info.sampleName : juce::String("empty"),
               header.withTrimmedRight(40), juce::Justification::centredLeft, true);
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.fillRect(editorHeaderArea_.withTop(editorHeaderArea_.getBottom() - 1));

    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
    for (const auto& [tab, name] :
         {std::pair{EditorTab::Layers, "LAYERS"}, std::pair{EditorTab::Velocity, "VEL"},
          std::pair{EditorTab::Volume, "VOL"}}) {
        const auto area = tabBounds(tab);
        const bool active = tab == editorTab_;
        g.setColour(colour(active ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM2));
        g.drawText(name, area, juce::Justification::centred, false);
        if (active) {
            g.setColour(colour(ActiveTheme::DEVICE_BLUE));
            g.fillRect(area.withTop(area.getBottom() - 2));
        }
    }
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.fillRect(tabsArea_.withTop(tabsArea_.getBottom() - 1));

    if (editorTab_ == EditorTab::Layers)
        return;
    if (editorTab_ == EditorTab::Velocity) {
        paintZones(g);
        return;
    }

    const auto label = [&](juce::Rectangle<int> area, const juce::String& name,
                           const juce::String& value) {
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.drawText(name, area, juce::Justification::centredLeft, false);
        g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT).withAlpha(0.8f));
        g.drawText(value, area, juce::Justification::centredRight, false);
    };
    const double db = levelControl_.getValue();
    const int pan = juce::roundToInt(panControl_.getValue() * 100.0);
    label(levelLabelArea_, "VOLUME",
          db <= kMinPadDb + 0.05 ? juce::String("-inf dB") : juce::String(db, 1) + " dB");
    label(panLabelArea_, "PAN",
          pan == 0 ? juce::String("C") : juce::String(std::abs(pan)) + (pan < 0 ? " L" : " R"));
    label(outputLabelArea_, "OUTPUT", {});
    device_shell::paintGainSlider(g, levelControl_.getBounds(), db, kMinPadDb);
    device_shell::paintPanSlider(g, panControl_.getBounds(), panControl_.getValue());

    const auto field = outputButton_.getBounds().toFloat();
    g.setColour(colour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(field, 4.0f);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(field.reduced(0.5f), 4.0f, 1.0f);
}

void DrumGridUI::paintChainHeader(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    const auto& info = padInfos_[static_cast<size_t>(selectedPad_)];

    auto text = chainHeaderArea_.reduced(14, 0).withTrimmedRight(64);
    g.setFont(fonts.getUIFont(12.0f));
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getUIFont(12.0f));
    g.drawText("Chain", text.removeFromLeft(40), juce::Justification::centredLeft, false);
    if (info.chainIndex >= 0) {
        const auto nameFont = fonts.getUIFont(12.0f);
        const int nameWidth =
            juce::jmin(text.getWidth() / 2,
                       juce::GlyphArrangement::getStringWidthInt(nameFont, info.sampleName) + 8);
        g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
        g.drawText(info.sampleName, text.removeFromLeft(nameWidth),
                   juce::Justification::centredLeft, true);
        int devices = 0;
        if (getPadChainPath)
            if (const auto* chain = magda::TrackManager::getInstance().getChainByPath(
                    getPadChainPath(selectedPad_, selectedLayer_)))
                devices = static_cast<int>(chain->elements.size());
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText(juce::String::fromUTF8("\xc2\xb7 ") + juce::String(devices) +
                       (devices == 1 ? " device" : " devices"),
                   text, juce::Justification::centredLeft, false);
    } else {
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getUIFont(12.0f).italicised());
        g.drawText("empty", text.removeFromLeft(48), juce::Justification::centredLeft, false);
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText(juce::String::fromUTF8("\xc2\xb7 ") + getNoteName(selectedPad_), text,
                   juce::Justification::centredLeft, false);

        auto body = chainArea_.withTrimmedTop(kPanelHeaderHeight);
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.setFont(fonts.getUIFont(12.0f));
        g.drawText("Drop a sample or device here", body.withTrimmedBottom(24),
                   juce::Justification::centred, false);
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText("or press + to build a chain", body.withTrimmedTop(24),
                   juce::Justification::centred, false);
    }
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.fillRect(chainHeaderArea_.withTop(chainHeaderArea_.getBottom() - 1));
}

void DrumGridUI::paintOverChildren(juce::Graphics& g) {
    auto highlightPad = [&](int absolutePadIndex, float alphaFill, float alphaStroke) {
        int pageStart = currentPage_ * kPadsPerPage;
        int btnIdx = absolutePadIndex - pageStart;
        if (btnIdx < 0 || btnIdx >= kPadsPerPage)
            return;
        const auto padBounds = padButtons_[static_cast<size_t>(btnIdx)].getBounds().toFloat();
        const auto blue = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
        g.setColour(blue.withAlpha(alphaFill * 0.5f));
        g.fillRoundedRectangle(padBounds, 5.0f);
        g.setColour(blue.withAlpha(alphaStroke));
        g.drawRoundedRectangle(padBounds.reduced(0.75f), 5.0f, 1.5f);
    };

    // Plugin/pad drop highlight — single pad under cursor.
    if (dropHighlightPad_ >= 0)
        highlightPad(dropHighlightPad_, 0.4f, 0.8f);

    // File-drag preview — every pad that will receive a sample on drop.
    if (fileDropStartPad_ >= 0 && fileDropCount_ > 0) {
        for (int i = 0; i < fileDropCount_; ++i) {
            int pad = fileDropStartPad_ + i;
            if (pad >= kTotalPads)
                break;
            // First pad bright, the rest slightly dimmer so the origin reads.
            const float f = (i == 0) ? 0.4f : 0.25f;
            const float s = (i == 0) ? 0.8f : 0.55f;
            highlightPad(pad, f, s);
        }
    }
}

// =============================================================================
// Layout
// =============================================================================

void DrumGridUI::resized() {
    auto area = getLocalBounds();
    footerArea_ = area.removeFromBottom(kFooterHeight);
    layoutFooter(footerArea_);

    railArea_ = area.removeFromLeft(kRailWidth);
    auto rail = railArea_.reduced(5, 8);
    editorToggle_.setBounds(rail.removeFromTop(26).withSizeKeepingCentre(28, 26));

    padsArea_ = area.removeFromLeft(padsWidthFor(area.getHeight()));
    const auto grid = padsArea_.reduced(kPadPadding);
    const int cellW = (grid.getWidth() - 3 * kPadGap) / kGridCols;
    const int cellH = (grid.getHeight() - 3 * kPadGap) / kGridRows;
    for (int i = 0; i < kPadsPerPage; ++i) {
        // Lowest pads at the bottom, like a drum machine.
        const int row = (kGridRows - 1) - i / kGridCols;
        const int col = i % kGridCols;
        auto& pad = padButtons_[static_cast<size_t>(i)];
        pad.setBounds(grid.getX() + col * (cellW + kPadGap), grid.getY() + row * (cellH + kPadGap),
                      cellW, cellH);
    }

    editorArea_ = {};
    if (!detailCollapsed_)
        editorArea_ = area.removeFromLeft(kEditorWidth);
    layoutEditor(editorArea_);

    chainArea_ = area;
    layoutChain(chainArea_);
}

void DrumGridUI::layoutEditor(juce::Rectangle<int> area) {
    const bool visible = !area.isEmpty();
    for (auto* control : volumeControls())
        control->setVisible(visible && editorTab_ == EditorTab::Volume);
    for (auto* control : zoneControls())
        control->setVisible(false);
    if (!visible) {
        layerList_.setVisible(false);
        return;
    }

    editorHeaderArea_ = area.removeFromTop(kPanelHeaderHeight);
    tabsArea_ = area.removeFromTop(28);
    layoutLayerList(editorTab_ == EditorTab::Layers ? area : juce::Rectangle<int>{});
    auto body = area.reduced(10);

    if (editorTab_ == EditorTab::Velocity) {
        const auto pair = [&body](juce::Component& left, juce::Component& right) {
            auto row = body.removeFromTop(24);
            left.setBounds(row.removeFromLeft((row.getWidth() - 8) / 2));
            row.removeFromLeft(8);
            right.setBounds(row);
            left.setVisible(true);
            right.setVisible(true);
        };
        layerMixLabelArea_ = body.removeFromTop(16);
        body.removeFromTop(4);
        {
            auto row = body.removeFromTop(20);
            layerGainControl_.setBounds(row.removeFromLeft((row.getWidth() - 8) / 2));
            row.removeFromLeft(8);
            layerPanControl_.setBounds(row);
            layerGainControl_.setVisible(true);
            layerPanControl_.setVisible(true);
        }
        body.removeFromTop(10);
        zoneRangeLabelArea_ = body.removeFromTop(16);
        body.removeFromTop(4);
        pair(velocityLowControl_, velocityHighControl_);
        body.removeFromTop(8);
        zoneBarArea_ = body.removeFromTop(10);
        body.removeFromTop(10);
        zoneFadeLabelArea_ = body.removeFromTop(16);
        body.removeFromTop(4);
        pair(velocityFadeLowControl_, velocityFadeHighControl_);
        body.removeFromTop(10);
        auto row = body.removeFromTop(24);
        roundRobinButton_.setBounds(row.removeFromRight(56));
        roundRobinButton_.setVisible(true);
        roundRobinLabelArea_ = row;
        return;
    }

    levelLabelArea_ = body.removeFromTop(16);
    body.removeFromTop(4);
    levelControl_.setBounds(body.removeFromTop(20));
    body.removeFromTop(10);
    panLabelArea_ = body.removeFromTop(16);
    body.removeFromTop(4);
    panControl_.setBounds(body.removeFromTop(20));
    body.removeFromTop(10);
    outputLabelArea_ = body.removeFromTop(16);
    body.removeFromTop(4);
    outputButton_.setBounds(body.removeFromTop(30));
}

void DrumGridUI::layoutLayerList(juce::Rectangle<int> area) {
    // Every layer, then Add layer, scrolling when they outgrow the panel.
    const bool show = !area.isEmpty() && selectedPadHasChain() && !layers_.empty();
    layerList_.setVisible(show);
    if (!show)
        return;

    constexpr int kGap = 4;
    constexpr int kAddHeight = 22;
    constexpr int kPadding = 8;
    const int rows = static_cast<int>(layerRows_.size());
    const int content = rows * (PadLayerRow::kHeight + kGap) + kAddHeight + 2 * kPadding;
    layerList_.setBounds(area);

    const int width = area.getWidth() - (content > area.getHeight() ? 8 : 0);
    layerListContent_.setSize(width, content);
    auto list = layerListContent_.getLocalBounds().reduced(kPadding);
    for (auto& row : layerRows_) {
        row->setBounds(list.removeFromTop(PadLayerRow::kHeight));
        row->setVisible(true);
        list.removeFromTop(kGap);
    }
    addLayerButton_.setBounds(list.removeFromTop(kAddHeight));
}

void DrumGridUI::layoutChain(juce::Rectangle<int> area) {
    chainHeaderArea_ = area.removeFromTop(kPanelHeaderHeight);
    auto buttons = chainHeaderArea_.reduced(10, 0).removeFromRight(56);
    chainSoloButton_.setBounds(buttons.removeFromRight(28).withSizeKeepingCentre(28, 24));
    chainMuteButton_.setBounds(buttons.removeFromRight(28).withSizeKeepingCentre(28, 24));

    const bool hasChain = padChainView_.isVisible();
    if (hasChain)
        padChainView_.setBounds(area.withTrimmedLeft(1));
    emptyAddButton_.setVisible(!hasChain);
    if (!hasChain)
        emptyAddButton_.setBounds(area.reduced(6).removeFromLeft(40));
}

void DrumGridUI::layoutFooter(juce::Rectangle<int> area) {
    auto inner = area.reduced(10, 0);
    auto nav = inner.removeFromLeft(220);
    prevPageButton_->setBounds(nav.removeFromLeft(16).withSizeKeepingCentre(12, 12));
    nextPageButton_->setBounds(nav.removeFromRight(16).withSizeKeepingCentre(12, 12));
    pageTextArea_ = nav;
}

// =============================================================================
// DragAndDropTarget (plugin drops, pad-to-pad moves, and samples dragged from
// MAGDA's own browser, which arrive as an internal {type:"files"} payload)
// =============================================================================

bool DrumGridUI::isInterestedInDragSource(const SourceDetails& details) {
    if (magda::dnd::acceptsFilesDrag(*this, details))
        return true;

    if (auto* obj = details.description.getDynamicObject()) {
        auto type = obj->getProperty("type").toString();
        bool interested = type == "plugin" || type == "pad";
        DBG("DrumGridUI::isInterestedInDragSource: " << (interested ? "YES" : "NO"));
        return interested;
    }
    return false;
}

void DrumGridUI::itemDragEnter(const SourceDetails& details) {
    if (magda::dnd::forwardFilesDragEnter(*this, details))
        return;

    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    if (btnIdx >= 0)
        dropHighlightPad_ = currentPage_ * kPadsPerPage + btnIdx;
    else
        dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDragMove(const SourceDetails& details) {
    if (magda::dnd::forwardFilesDragMove(*this, details))
        return;

    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    int newHighlight = btnIdx >= 0 ? currentPage_ * kPadsPerPage + btnIdx : -1;
    if (newHighlight != dropHighlightPad_) {
        dropHighlightPad_ = newHighlight;
        repaint();
    }
}

void DrumGridUI::itemDragExit(const SourceDetails& details) {
    if (magda::dnd::forwardFilesDragExit(*this, details))
        return;

    dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDropped(const SourceDetails& details) {
    if (magda::dnd::forwardFilesDrop(*this, details))
        return;

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

void DrumGridUI::endFaderGesture() {
    faderGesture_ = nextFaderGesture_.fetch_add(1);
}

void DrumGridUI::showPadContextMenu(int padIndex, juce::Point<int> screenPos) {
    setSelectedPad(padIndex);

    auto& info = padInfos_[static_cast<size_t>(padIndex)];
    if (info.sampleName.isEmpty())
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Analyze pad role");
    menu.addItem(3, "Add Layer");
    menu.addSeparator();
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, padIndex](int result) {
                           if (result == 1 && onAnalyzePadRoleRequested)
                               onAnalyzePadRoleRequested(padIndex);
                           else if (result == 2 && onClearRequested)
                               onClearRequested(padIndex);
                           else if (result == 3 && onAddLayerRequested)
                               onAddLayerRequested(padIndex);
                       });
}

int DrumGridUI::getPreferredContentWidth() const {
    int width = kRailWidth + padsWidthFor(getHeight() - kFooterHeight);
    if (!detailCollapsed_)
        width += kEditorWidth;
    const int chainWidth = selectedPadHasChain() ? padChainView_.getContentWidth() : 0;
    return width + juce::jmax(kMinChainWidth, chainWidth);
}

void DrumGridUI::setDetailCollapsed(bool collapsed) {
    if (detailCollapsed_ == collapsed)
        return;
    detailCollapsed_ = collapsed;
    editorToggle_.setToggleState(!collapsed, juce::dontSendNotification);
    if (onDetailCollapsedChanged)
        onDetailCollapsedChanged(collapsed);
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

bool DrumGridUI::selectedPadHasChain() const {
    return padInfos_[static_cast<size_t>(selectedPad_)].chainIndex >= 0;
}

void DrumGridUI::refreshPadChain() {
    refreshLayers();
    const auto path =
        getPadChainPath ? getPadChainPath(selectedPad_, selectedLayer_) : magda::ChainNodePath{};
    if (path.isValid()) {
        if (padChainView_.getChainPath() == path)
            padChainView_.refresh();
        else
            padChainView_.showChain(path);
        padChainView_.setAlpha(padInfos_[static_cast<size_t>(selectedPad_)].bypassed ? 0.35f
                                                                                     : 1.0f);
    } else {
        padChainView_.clear();
    }
    resized();
    repaint();
}

void DrumGridUI::showOutputMenu() {
    const int current = padInfos_[static_cast<size_t>(selectedPad_)].busOutput;
    juce::PopupMenu menu;
    menu.addItem(1, "Main", true, current == 0);
    for (int bus = 1; bus < magda::kPadBusCount; ++bus)
        menu.addItem(bus + 1, "Bus " + juce::String(bus), true, current == bus);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&outputButton_),
                       [safeThis = juce::Component::SafePointer(this)](int result) {
                           if (result <= 0 || safeThis == nullptr)
                               return;
                           const int pad = safeThis->selectedPad_;
                           safeThis->padInfos_[static_cast<size_t>(pad)].busOutput = result - 1;
                           if (safeThis->onPadOutputChanged)
                               safeThis->onPadOutputChanged(pad, result - 1);
                           safeThis->refreshDetailPanel();
                       });
}

void DrumGridUI::refreshPadButtons() {
    const int pageStart = currentPage_ * kPadsPerPage;
    for (int i = 0; i < kPadsPerPage; ++i) {
        const int padIdx = pageStart + i;
        auto& btn = padButtons_[static_cast<size_t>(i)];
        const auto& info = padInfos_[static_cast<size_t>(padIdx)];
        btn.setPadIndex(padIdx);
        btn.setNoteName(getNoteName(padIdx));
        btn.setSampleName(info.sampleName);
        btn.setHasSample(info.sampleName.isNotEmpty());
        btn.setSelected(padIdx == selectedPad_);
        btn.setMuted(info.mute);
        btn.setSoloed(info.solo);
        btn.setStripeColour(device_shell::chainColour(info.chainIndex));
    }
    prevPageButton_->setEnabled(currentPage_ > 0);
    nextPageButton_->setEnabled(currentPage_ < kNumPages - 1);
    repaint(footerArea_);
}

void DrumGridUI::refreshDetailPanel() {
    const auto& info = padInfos_[static_cast<size_t>(selectedPad_)];
    levelControl_.setValue(info.level, juce::dontSendNotification);
    panControl_.setValue(info.pan, juce::dontSendNotification);
    outputButton_.setButtonText(info.busOutput == 0 ? juce::String("Main")
                                                    : "Bus " + juce::String(info.busOutput));
    const bool hasChain = info.chainIndex >= 0;
    for (auto* button : {&chainMuteButton_, &chainSoloButton_})
        button->setEnabled(hasChain);
    syncMuteGlyph(chainMuteButton_, info.mute);
    chainSoloButton_.setToggleState(info.solo, juce::dontSendNotification);
    for (auto* control : volumeControls())
        control->setEnabled(hasChain);
    refreshZoneControls();
    repaint();
}

std::vector<juce::Component*> DrumGridUI::volumeControls() {
    return {&levelControl_, &panControl_, &outputButton_};
}

std::vector<juce::Component*> DrumGridUI::zoneControls() {
    return {&velocityLowControl_,      &velocityHighControl_, &velocityFadeLowControl_,
            &velocityFadeHighControl_, &roundRobinButton_,    &layerGainControl_,
            &layerPanControl_};
}

juce::Rectangle<int> DrumGridUI::tabBounds(EditorTab tab) const {
    const int width = tabsArea_.getWidth() / 3;
    return tabsArea_.withWidth(width).translated(width * static_cast<int>(tab), 0);
}

void DrumGridUI::mouseDown(const juce::MouseEvent& event) {
    if (editorArea_.isEmpty() || !tabsArea_.contains(event.getPosition()))
        return;
    for (const auto tab : {EditorTab::Layers, EditorTab::Velocity, EditorTab::Volume})
        if (tabBounds(tab).contains(event.getPosition()) && tab != editorTab_) {
            editorTab_ = tab;
            layoutEditor(editorArea_);
            repaint();
        }
}

const PadLayerView* DrumGridUI::selectedLayer() const {
    const auto found = std::ranges::find(layers_, selectedLayer_, &PadLayerView::id);
    return found == layers_.end() ? nullptr : &*found;
}

void DrumGridUI::refreshLayers() {
    layers_ = getPadLayers ? getPadLayers(selectedPad_) : std::vector<PadLayerView>{};
    if (selectedLayer() == nullptr)
        selectedLayer_ = layers_.empty() ? magda::INVALID_CHAIN_ID : layers_.front().id;

    // Rows are kept while the count holds, so a fader drag survives a refresh.
    if (layerRows_.size() != layers_.size()) {
        layerRows_.clear();
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            auto row = std::make_unique<PadLayerRow>();
            row->onSelect = [this](magda::ChainId id) {
                selectedLayer_ = id;
                refreshPadChain();
                refreshZoneControls();
            };
            row->onSwitchesChanged = [this](magda::ChainId id, bool mute, bool solo,
                                            bool bypassed) {
                if (onLayerSwitchesChanged)
                    onLayerSwitchesChanged(selectedPad_, id, mute, solo, bypassed);
            };
            row->onRemove = [this](magda::ChainId id) {
                if (onRemoveLayerRequested)
                    onRemoveLayerRequested(selectedPad_, id);
            };
            layerListContent_.addAndMakeVisible(*row);
            layerRows_.push_back(std::move(row));
        }
    }
    for (std::size_t i = 0; i < layers_.size(); ++i)
        layerRows_[i]->setLayer(layers_[i], static_cast<int>(i), layers_[i].id == selectedLayer_,
                                layers_.size() > 1);
    refreshZoneControls();
    layoutEditor(editorArea_);
}

void DrumGridUI::refreshZoneControls() {
    const auto* layer = selectedLayer();
    for (auto* control : zoneControls())
        control->setEnabled(layer != nullptr && selectedPadHasChain());
    if (layer == nullptr || zoneDragging_)
        return;

    if (!faderDragging_) {
        layerGainControl_.setValue(layer->volume, juce::dontSendNotification);
        layerPanControl_.setValue(layer->pan, juce::dontSendNotification);
    }
    const auto& zones = layer->zones;
    velocityLowControl_.setValue(zones.velocityLow, juce::dontSendNotification);
    velocityHighControl_.setValue(zones.velocityHigh, juce::dontSendNotification);
    velocityFadeLowControl_.setValue(zones.velocityFadeLow, juce::dontSendNotification);
    velocityFadeHighControl_.setValue(zones.velocityFadeHigh, juce::dontSendNotification);
    roundRobinButton_.setToggleState(zones.roundRobin, juce::dontSendNotification);
    roundRobinButton_.setButtonText(zones.roundRobin ? "On" : "Off");
}

void DrumGridUI::commitZones() {
    const auto* layer = selectedLayer();
    if (layer == nullptr || !onLayerZonesChanged)
        return;

    // A pad plays one note, so its layers' key zones are not edited here.
    auto zones = layer->zones;
    zones.velocityLow = juce::roundToInt(velocityLowControl_.getValue());
    zones.velocityHigh = juce::roundToInt(velocityHighControl_.getValue());
    zones.velocityFadeLow = juce::roundToInt(velocityFadeLowControl_.getValue());
    zones.velocityFadeHigh = juce::roundToInt(velocityFadeHighControl_.getValue());
    zones.roundRobin = roundRobinButton_.getToggleState();
    if (zones != layer->zones)
        onLayerZonesChanged(layer->id, zones);
}

void DrumGridUI::paintZones(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    const auto label = [&](juce::Rectangle<int> area, const juce::String& name,
                           const juce::String& value) {
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        g.setColour(colour(ActiveTheme::DEVICE_DIM2));
        g.drawText(name, area, juce::Justification::centredLeft, false);
        g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT).withAlpha(0.8f));
        g.drawText(value, area, juce::Justification::centredRight, false);
    };

    const int low = juce::roundToInt(velocityLowControl_.getValue());
    const int high = juce::roundToInt(velocityHighControl_.getValue());
    const int fadeLow = juce::roundToInt(velocityFadeLowControl_.getValue());
    const int fadeHigh = juce::roundToInt(velocityFadeHighControl_.getValue());
    label(zoneRangeLabelArea_, "VELOCITY",
          juce::String(low) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(high));
    label(zoneFadeLabelArea_, "CROSSFADE", juce::String(fadeLow) + " / " + juce::String(fadeHigh));
    label(roundRobinLabelArea_, "ROUND ROBIN", {});

    const double db = layerGainControl_.getValue();
    const int pan = juce::roundToInt(layerPanControl_.getValue() * 100.0);
    label(
        layerMixLabelArea_, "LAYER",
        (db <= kMinPadDb + 0.05 ? juce::String("-inf dB") : juce::String(db, 1) + " dB") + "  " +
            (pan == 0 ? juce::String("C") : juce::String(std::abs(pan)) + (pan < 0 ? " L" : " R")));
    device_shell::paintGainSlider(g, layerGainControl_.getBounds(), db, kMinPadDb);
    device_shell::paintPanSlider(g, layerPanControl_.getBounds(), layerPanControl_.getValue());

    // The range over its whole span, its fades ramping in from each edge.
    const auto bar = zoneBarArea_.toFloat();
    g.setColour(colour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(bar, 2.0f);
    constexpr float floor = 1.0f;
    const auto xFor = [&bar](int value) {
        return bar.getX() + (static_cast<float>(value) - floor) / (128.0f - floor) * bar.getWidth();
    };
    const float left = xFor(low);
    const float right = xFor(high + 1);
    if (right > left) {
        juce::Path shape;
        shape.startNewSubPath(left, bar.getBottom());
        shape.lineTo(xFor(low + fadeLow), bar.getY());
        shape.lineTo(xFor(high + 1 - fadeHigh), bar.getY());
        shape.lineTo(right, bar.getBottom());
        shape.closeSubPath();
        g.setColour(colour(ActiveTheme::DEVICE_BLUE).withAlpha(0.35f));
        g.fillPath(shape);
        g.setColour(colour(ActiveTheme::DEVICE_BLUE));
        g.fillRect(juce::Rectangle<float>(left, bar.getY(), 2.0f, bar.getHeight()));
        g.fillRect(juce::Rectangle<float>(right - 2.0f, bar.getY(), 2.0f, bar.getHeight()));
    }
}

void DrumGridUI::lookAndFeelChanged() {
    styleControls();
    refreshDetailPanel();
    repaint();
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
    int midiNote = magda::padNoteFor(padIndex);
    return juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3);
}

int DrumGridUI::padButtonIndexAtPoint(juce::Point<int> point) const {
    for (int i = 0; i < kPadsPerPage; ++i) {
        if (padButtons_[static_cast<size_t>(i)].getBounds().contains(point))
            return i;
    }
    return -1;
}

}  // namespace magda::daw::ui
