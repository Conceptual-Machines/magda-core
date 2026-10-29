// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "drum_grid/DrumGridUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/DrumGridPads.hpp"
#include "core/TrackManager.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/components/common/MasterSpeakerButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

std::atomic<int> DrumGridUI::nextFaderGesture_{0};

namespace {
constexpr int kPadGap = 6;
constexpr int kPadPadding = 0;  // none: at 200px every row counts (#3201)
constexpr int kMinPadsWidth = 268;
constexpr int kMaxPadsWidth = 396;

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

    // A pad of a 200px device is about 33px tall: its two lines close up to fit (#3201).
    const bool tight = getHeight() < 44;
    const int noteRow = tight ? 11 : 14;
    auto text = getLocalBounds().reduced(10, tight ? 4 : 7);
    g.setColour(colour(ActiveTheme::DEVICE_ICON_INACTIVE));
    g.setFont(fonts.getMonoFont(9.5f));
    g.drawText(noteName_, text.removeFromTop(noteRow), juce::Justification::topLeft, false);
    if (soloed_ || muted_) {
        g.setColour(colour(soloed_ ? ActiveTheme::DEVICE_AMBER : ActiveTheme::DEVICE_RED));
        g.setFont(fonts.getMonoFont(8.5f).boldened());
        g.drawText(soloed_ ? "S" : "M", text.withHeight(noteRow).translated(0, -noteRow),
                   juce::Justification::topRight, false);
    }

    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(fonts.getUIFontMedium(11.5f));
    g.drawText(sampleName_, text.removeFromBottom(tight ? 14 : 16), juce::Justification::bottomLeft,
               true);
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

    padMixPanel_.onMixChanged = [this](float volume, float pan) {
        if (onPadMixChanged)
            onPadMixChanged(selectedPad_, volume, pan);
    };
    padMixPanel_.onGestureEnd = [this]() { endFaderGesture(); };
    padMixPanel_.onSwitchesChanged = [this](bool mute, bool solo, bool bypassed) {
        auto& info = padInfos_[static_cast<size_t>(selectedPad_)];
        info.mute = mute;
        info.solo = solo;
        info.bypassed = bypassed;
        if (onPadSwitchesChanged)
            onPadSwitchesChanged(selectedPad_, mute, solo, bypassed);
        refreshPadButtons();
    };
    addAndMakeVisible(padMixPanel_);

    refreshPadButtons();
    refreshDetailPanel();
}

DrumGridUI::~DrumGridUI() {
    stopTimer();
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

        if (changed && i == selectedPad_)
            detailNeedsRefresh = true;
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

    if (dropOnChainArea({x, y}, {}, files))
        return;

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

    g.setColour(colour(ActiveTheme::DEVICE_PANEL));
    g.fillRect(chainArea_);
    g.setColour(line);
    g.fillRect(chainArea_.withWidth(1));
    paintEmptyChain(g);
}

/// An empty pad's chain says how to fill it. A pad's chain has no header of its
/// own: like a rack's, it opens straight onto its devices.
void DrumGridUI::paintEmptyChain(juce::Graphics& g) {
    if (padInfos_[static_cast<size_t>(selectedPad_)].chainIndex >= 0)
        return;

    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getUIFont(12.0f));
    g.drawText("Drop a sample or device here", chainArea_.withTrimmedBottom(24),
               juce::Justification::centred, false);
    g.setColour(colour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getMonoFont(11.0f));
    g.drawText("or press + to build a chain", chainArea_.withTrimmedTop(24),
               juce::Justification::centred, false);
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
    // The pads and the panel sit under the device header; the chain runs from the top.
    auto area = getLocalBounds();
    padsArea_ = area.removeFromLeft(padsWidthFor(area.getHeight() - headerInset_))
                    .withTrimmedTop(headerInset_);
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

    padMixPanel_.setVisible(panelShown_);
    if (panelShown_)
        padMixPanel_.setBounds(
            area.removeFromLeft(PadMixPanel::kWidth).withTrimmedTop(headerInset_));
    chainArea_ = chainShown_ ? area : juce::Rectangle<int>{};
    layoutChain(chainArea_);
}

void DrumGridUI::layoutChain(juce::Rectangle<int> area) {
    area.removeFromLeft(1);
    const bool hasChain = padChainView_.isVisible();
    if (hasChain)
        padChainView_.setBounds(area);
    emptyAddButton_.setVisible(chainShown_ && !hasChain);
    if (chainShown_ && !hasChain)
        emptyAddButton_.setBounds(area.reduced(6).removeFromLeft(40));
}

void DrumGridUI::setHeaderInset(int inset) {
    if (inset == headerInset_)
        return;
    headerInset_ = inset;
    resized();
    repaint();
}

bool DrumGridUI::hitTest(int x, int y) {
    return y >= headerInset_ || x >= getChainX();
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
    if (btnIdx < 0) {
        dropOnChainArea(details.localPosition, details.description, {});
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

bool DrumGridUI::dropOnChainArea(juce::Point<int> point, const juce::var& description,
                                 const juce::StringArray& files) {
    if (!chainArea_.contains(point))
        return false;

    std::optional<magda::DeviceInfo> device;
    const auto sample = std::ranges::find_if(files, isAudioFile);
    if (sample != files.end()) {
        device = magda::padSamplerDevice(*sample, magda::padNoteFor(selectedPad_));
    } else if (auto* obj = description.getDynamicObject();
               obj != nullptr && obj->getProperty("type").toString() == "plugin") {
        // A pad with no chain yet is made by the drop, as on the pad itself.
        if (!selectedPadHasChain()) {
            if (onPluginDropped)
                onPluginDropped(selectedPad_, *obj);
            return true;
        }
        device = magda::TrackManager::deviceInfoFromPluginObject(*obj);
    }
    if (!device)
        return false;

    if (!selectedPadHasChain()) {
        if (onSampleDropped)
            onSampleDropped(selectedPad_, juce::File(*sample));
    } else if (onDeviceDroppedOnPad) {
        onDeviceDroppedOnPad(selectedPad_, *device);
    }
    return true;
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
    menu.addSeparator();
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, padIndex](int result) {
                           if (result == 1 && onAnalyzePadRoleRequested)
                               onAnalyzePadRoleRequested(padIndex);
                           else if (result == 2 && onClearRequested)
                               onClearRequested(padIndex);
                       });
}

int DrumGridUI::fixedContentWidth() const {
    return padsWidthFor(getHeight() - headerInset_) + (panelShown_ ? PadMixPanel::kWidth : 0);
}

void DrumGridUI::setMaxContentWidth(int maxWidth) {
    maxContentWidth_ = maxWidth;
    padChainView_.setMaxWidth(
        maxWidth > 0 ? juce::jmax(minimumChainWidth(), maxWidth - fixedContentWidth()) : 0);
}

int DrumGridUI::minimumChainWidth() const {
    // Short of the widest device in the pad's chain, the grid widens and the track chain scrolls.
    return selectedPadHasChain()
               ? juce::jmax(kMinChainWidth, padChainView_.getMinimumContentWidth())
               : kMinChainWidth;
}

int DrumGridUI::getPreferredContentWidth() const {
    const int width = fixedContentWidth();
    if (!chainShown_)
        return width;
    if (!selectedPadHasChain())
        return width + kMinChainWidth;
    const int minimum = minimumChainWidth();
    int chainWidth = juce::jmax(minimum, padChainView_.getContentWidth());
    if (maxContentWidth_ > 0)
        chainWidth =
            juce::jlimit(minimum, juce::jmax(minimum, maxContentWidth_ - width), chainWidth);
    return width + chainWidth;
}

bool DrumGridUI::selectedPadHasChain() const {
    return padInfos_[static_cast<size_t>(selectedPad_)].chainIndex >= 0;
}

void DrumGridUI::setViewsShown(bool panelShown, bool chainShown) {
    if (panelShown_ == panelShown && chainShown_ == chainShown)
        return;
    panelShown_ = panelShown;
    chainShown_ = chainShown;
    refreshPadChain();
    if (onLayoutChanged)
        onLayoutChanged();
}

void DrumGridUI::refreshPadChain() {
    const auto path = getPadChainPath ? getPadChainPath(selectedPad_) : magda::ChainNodePath{};
    if (path.isValid() && chainShown_) {
        if (padChainView_.isVisible() && padChainView_.getChainPath() == path)
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
    if (onPageChanged)
        onPageChanged();
}

void DrumGridUI::refreshDetailPanel() {
    refreshPadRow();
    repaint();
}

void DrumGridUI::refreshPadRow() {
    const auto& info = padInfos_[static_cast<size_t>(selectedPad_)];
    padMixPanel_.setPad({.name = info.sampleName,
                         .note = getNoteName(selectedPad_),
                         .colourIndex = juce::jmax(0, info.chainIndex),
                         .volume = info.level,
                         .pan = info.pan,
                         .mute = info.mute,
                         .solo = info.solo,
                         .bypassed = info.bypassed});
    padMixPanel_.setMixable(info.chainIndex >= 0);
}

void DrumGridUI::lookAndFeelChanged() {
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
