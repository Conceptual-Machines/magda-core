#include "RackComponent.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <functional>
#include <ranges>

#include "ChainPanel.hpp"
#include "ChainRowComponent.hpp"
#include "audio/DeviceMeters.hpp"
#include "audio/TrackMeters.hpp"
#include "core/Config.hpp"
#include "core/PresetManager.hpp"
#include "core/RangesHelpers.hpp"
#include "core/TrackCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

/// Delete the rack at @p rackPath, undoably and after this component has gone.
///
/// Deferred because removing the rack notifies synchronously, and the rebuild
/// that follows destroys the button whose click is still on the stack; the path
/// is taken by value for the same reason. Both constructors below share this,
/// so the top-level and nested X do the same thing (#2232).
void requestRackDeletion(const magda::ChainNodePath& rackPath) {
    juce::MessageManager::callAsync([rackPath]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::RemoveRackByPathCommand>(rackPath));
    });
}

}  // namespace

// Constructor for top-level rack (in track)
RackComponent::RackComponent(magda::TrackId trackId, const magda::RackInfo& rack)
    : rackPath_(magda::ChainNodePath::rack(trackId, rack.id)), trackId_(trackId), rackId_(rack.id) {
    onDeleteClicked = [this]() { requestRackDeletion(rackPath_); };
    initializeCommon(rack);
}

// Constructor for nested rack (in chain) - with full path context
RackComponent::RackComponent(const magda::ChainNodePath& rackPath, const magda::RackInfo& rack)
    : rackPath_(rackPath), trackId_(rackPath.trackId), rackId_(rack.id) {
    onDeleteClicked = [this]() { requestRackDeletion(rackPath_); };
    initializeCommon(rack);
}

void RackComponent::initializeCommon(const magda::RackInfo& rack) {
    // Set up base class with path for selection
    setNodePath(rackPath_);
    setNodeName(rack.name);
    setBypassed(rack.bypassed);

    // Restore panel visibility from rack state
    modPanelVisible_ = rack.modPanelOpen;
    paramPanelVisible_ = rack.paramPanelOpen;

    onCollapsedChanged = [this](bool collapsed) {
        if (auto* rackInfo = magda::TrackManager::getInstance().getRackByPath(rackPath_))
            rackInfo->expanded = !collapsed;
        styleShellControls();
    };

    onBypassChanged = [this](bool bypassed) {
        magda::TrackManager::getInstance().setRackBypassedByPath(rackPath_, bypassed);
    };

    onModPanelToggled = [this](bool visible) {
        if (auto* rackInfo = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
            rackInfo->modPanelOpen = visible;
        }
        if (modButton_) {
            modButton_->setToggleState(visible, juce::dontSendNotification);
            modButton_->setActive(visible);
        }
        childLayoutChanged();
    };

    onParamPanelToggled = [this](bool visible) {
        if (auto* rackInfo = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
            rackInfo->paramPanelOpen = visible;
        }
        if (macroButton_) {
            macroButton_->setToggleState(visible, juce::dontSendNotification);
            macroButton_->setActive(visible);
        }
        childLayoutChanged();
    };

    // === HEADER EXTRA CONTROLS ===

    modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::iconmodsboldm_svg,
                                                    BinaryData::iconmodsboldm_svgSize);
    modButton_->setTooltip("Modulators");
    modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
    modButton_->setActive(modPanelVisible_);
    modButton_->onClick = [this]() {
        modButton_->setActive(modButton_->getToggleState());
        setModPanelVisible(modButton_->getToggleState());
    };
    addAndMakeVisible(*modButton_);

    macroButton_ =
        std::make_unique<magda::SvgButton>("Macro", BinaryData::knob_svg, BinaryData::knob_svgSize);
    macroButton_->setTooltip("Macros");
    macroButton_->setToggleState(paramPanelVisible_, juce::dontSendNotification);
    macroButton_->setActive(paramPanelVisible_);
    macroButton_->onClick = [this]() {
        macroButton_->setActive(macroButton_->getToggleState());
        setParamPanelVisible(macroButton_->getToggleState());
    };
    addAndMakeVisible(*macroButton_);

    presetButton_ =
        std::make_unique<magda::SvgButton>("Presets", BinaryData::iconpresetsroundboldm_svg,
                                           BinaryData::iconpresetsroundboldm_svgSize);
    presetButton_->setTooltip("MAGDA Rack Presets");
    presetButton_->onClick = [this]() { showPresetMenu(); };
    addAndMakeVisible(*presetButton_);

    // Gain slider — vertical, overlaid on the level meter strip in the
    // content area. Same pattern (and LookAndFeel) as DeviceSlotComponent so
    // the visual reads consistently.
    addAndMakeVisible(levelMeter_);
    gainSlider_ = std::make_unique<node_header::GainSliderWithMeterTooltip>(
        juce::Slider::LinearVertical, juce::Slider::NoTextBox, levelMeter_);
    gainSlider_->setRange(-60.0, 6.0, 0.1);
    gainSlider_->setValue(rack.volume, juce::dontSendNotification);
    gainSlider_->setTooltip("Rack Gain (dB)");
    gainSlider_->setLookAndFeel(&node_header::FlatGainSliderLookAndFeel::getInstance());
    gainSlider_->setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    gainSlider_->setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    // Without this, a click anywhere on the strip jumps the thumb to the
    // cursor before the double-click handler runs, so resetting to 0 dB looks
    // like the thumb darts twice. Disabling it makes the first click a no-op
    // and the double-click goes straight to unity.
    gainSlider_->setSliderSnapsToMousePosition(false);
    gainSlider_->setDoubleClickReturnValue(true, 0.0);
    gainSlider_->onValueChange = [this]() {
        magda::TrackManager::getInstance().setRackVolume(
            rackPath_, static_cast<float>(gainSlider_->getValue()));
    };
    addAndMakeVisible(*gainSlider_);

    bandsToggle_ = std::make_unique<magda::SvgButton>("Bands", BinaryData::mbbands_svg,
                                                      BinaryData::mbbands_svgSize);
    bandsToggle_->setTooltip("Show the bands");
    bandsToggle_->onClick = [this]() { toggleMultibandView(false); };
    addChildComponent(*bandsToggle_);
    faceplateToggle_ = std::make_unique<magda::SvgButton>("Faceplate", BinaryData::mbfaceplate_svg,
                                                          BinaryData::mbfaceplate_svgSize);
    faceplateToggle_->setTooltip("Show the faceplate");
    faceplateToggle_->onClick = [this]() { toggleMultibandView(true); };
    addChildComponent(*faceplateToggle_);

    addChainButton_.setTooltip("Add a chain to this rack");
    addChainButton_.onClick = [this]() { onAddChainClicked(); };
    chainRowsContainer_.addAndMakeVisible(addChainButton_);

    // Viewport for chain rows
    chainViewport_.setViewedComponent(&chainRowsContainer_, false);
    chainViewport_.setScrollBarsShown(true, false);  // Vertical only
    chainViewport_.setScrollBarThickness(6);
    // Allow clicks on empty areas to pass through to parent for selection
    chainViewport_.setInterceptsMouseClicks(false, true);
    chainRowsContainer_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(chainViewport_);

    // Create chain panel (initially hidden)
    chainPanel_ = std::make_unique<ChainPanel>();
    chainPanel_->onClose = [this]() { hideChainPanel(); };
    chainPanel_->onDeviceSelected = [this](magda::DeviceId deviceId) {
        // Forward device selection to parent
        if (onDeviceSelected) {
            onDeviceSelected(deviceId);
        }
    };
    // IMPORTANT: Hook into ChainPanel's layout changes to propagate size changes upward.
    // When nested racks expand, this ensures the size request propagates all the way
    // up to TrackChainContent rather than just calling resized() on this RackComponent.
    chainPanel_->onLayoutChanged = [this]() { childLayoutChanged(); };
    addChildComponent(*chainPanel_);

    // Initialize mods/macros panels from base class
    initializeModsMacrosPanels();
    styleShellControls();

    selector_ = std::make_unique<ChainSelectorControl>(rackPath_);
    selector_->onModulatedValue = [this](std::optional<float> value) {
        for (auto& row : chainRows_)
            row->setModulatedSelector(value);
    };
    selector_->onValueChange = [this]() {
        magda::TrackManager::getInstance().setRackChainSelector(
            rackPath_, static_cast<float>(selector_->getValue()));
    };
    addChildComponent(*selector_);

    // Build chain rows
    updateFromRack(rack);

    // Restore collapsed state AFTER all child components are created, because
    // setCollapsed triggers resized() → resizedCollapsed() which lays out
    // macroButton_ / modButton_. Calling it earlier (when those are still
    // null) crashes inside Component::setBounds with a null `this`. Same
    // pattern DeviceSlotComponent uses. The model field is the single source
    // of truth — TrackChainContent's save/restore-state map skips racks so a
    // freshly-loaded preset's `expanded` value isn't shadowed.
    setCollapsed(!rack.expanded);

    // Start meter polling timer (~30 FPS)
    startTimerHz(30);
}

RackComponent::~RackComponent() {
    stopTimer();
}

void RackComponent::timerCallback() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;

    if (midiLed_.update(audioEngine->meters().midiActivity.getActivityCounter(trackId_)))
        repaint(shellRows_.midiLed.expanded(4));

    // The engine's own meters (#2570).
    magda::DeviceMeters::Levels levels;
    if (audioEngine->deviceMeters().rackPeak(rackId_, levels))
        levelMeter_.setLevels(levels.peakL, levels.peakR);
}

void RackComponent::toggleMultibandView(bool faceplate) {
    auto& shown = faceplate ? faceplateShown_ : bandsShown_;
    const auto& other = faceplate ? bandsShown_ : faceplateShown_;
    if (!(shown && !other))  // One of the two always shows.
        shown = !shown;
    bandsToggle_->setToggleState(bandsShown_, juce::dontSendNotification);
    faceplateToggle_->setToggleState(faceplateShown_, juce::dontSendNotification);
    if (auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
        rack->faceplateShown = faceplateShown_;
        rack->bandsShown = bandsShown_;
    }
    childLayoutChanged();
}

void RackComponent::mouseDown(const juce::MouseEvent& e) {
    if (!collapsed_ && chainTabsArea_.contains(e.getPosition())) {
        for (int i = 0; i < kNumChainViews; ++i)
            if (const auto view = static_cast<ChainView>(i);
                chainTabBounds(view).contains(e.getPosition()))
                setChainView(view);
        return;
    }
    // Let the base class handle selection - it will call selectChainNode in mouseUp
    NodeComponent::mouseDown(e);
}

juce::Rectangle<int> RackComponent::chainTabBounds(ChainView view) const {
    const int width = chainTabsArea_.getWidth() / kNumChainViews;
    return chainTabsArea_.withWidth(width).translated(width * static_cast<int>(view), 0);
}

void RackComponent::setChainView(ChainView view) {
    if (view == chainView_)
        return;
    chainView_ = view;
    resized();
    repaint();
}

void RackComponent::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel) {
    NodeComponent::mouseWheelMove(e, wheel);
}

void RackComponent::styleShellControls() {
    using node_header::DeviceIcon;
    const juce::Colour key(0xFFB3B3B3);
    // Collapsed, the strip stacks them at 16px.
    const float height = collapsed_ ? 16.0f : static_cast<float>(getHeaderButtonSize().y);
    node_header::applyDeviceIconStyle(*macroButton_, DeviceIcon::Toggle, key,
                                      ActiveTheme::ACCENT_MODULATION, height);
    node_header::applyDeviceIconStyle(*modButton_, DeviceIcon::Toggle, key,
                                      ActiveTheme::ACCENT_ATTENTION, height);
    for (auto* toggle : {bandsToggle_.get(), faceplateToggle_.get()})
        if (toggle != nullptr)
            node_header::applyDeviceIconStyle(*toggle, DeviceIcon::Toggle, key,
                                              ActiveTheme::DEVICE_BLUE,
                                              static_cast<float>(getHeaderButtonSize().y));
    node_header::applyDeviceIconStyle(*presetButton_, DeviceIcon::Action, key,
                                      ActiveTheme::PRESET_INDIGO,
                                      static_cast<float>(getHeaderButtonSize().y));
    presetButton_->setHoverColor(ActiveTheme::PRESET_INDIGO);
    styleHeaderPowerAndClose();
    chainViewport_.getVerticalScrollBar().setColour(
        juce::ScrollBar::thumbColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));

    auto& title = getNameLabel();
    title.setFont(FontManager::getInstance().getHeadingFont(13.5f));
    title.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::DEVICE_TITLE));
}

void RackComponent::lookAndFeelChanged() {
    NodeComponent::lookAndFeelChanged();
    styleShellControls();
    repaint();
}

void RackComponent::paintNodeFrame(juce::Graphics& g, juce::Rectangle<int> bounds,
                                   int headerHeight) {
    device_shell::paintFrame(g, bounds, headerHeight, shellRows_, {}, midiLed_.isLit());
}

void RackComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> /*contentArea*/) {
    if (collapsed_)
        return;

    auto& fonts = FontManager::getInstance();
    if (!chainTabsArea_.isEmpty()) {
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        for (const auto& [view, name] :
             {std::pair{ChainView::Mix, "MIX"}, std::pair{ChainView::Key, "KEY"},
              std::pair{ChainView::Velocity, "VEL"}, std::pair{ChainView::Fade, "FADE"}}) {
            const auto area = chainTabBounds(view);
            const bool active = view == chainView_;
            g.setColour(ActiveTheme::getColour(active ? ActiveTheme::DEVICE_VALUE_TEXT
                                                      : ActiveTheme::DEVICE_DIM2));
            g.drawText(name, area, juce::Justification::centred, false);
            if (active) {
                g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
                g.fillRect(area.withTop(area.getBottom() - 2));
            }
        }
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.fillRect(chainTabsArea_.withTop(chainTabsArea_.getBottom() - 1));
    }

    if (!columnHeaderArea_.isEmpty()) {
        const auto columns = ChainRowComponent::columnsFor(columnHeaderArea_);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        g.drawText(multiband_ ? "BAND" : "CHAIN", columns.name, juce::Justification::centredLeft,
                   false);
        if (chainView_ == ChainView::Mix) {
            g.drawText("GAIN", columns.gain, juce::Justification::centredLeft, false);
            g.drawText("PAN", columns.pan, juce::Justification::centredLeft, false);
        }
    }

    if (!selectorCaption_.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        g.drawText("CHAIN SELECT", selectorCaption_, juce::Justification::centredLeft, false);
    }

    if (!viewportArea_.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
        g.fillRect(viewportArea_);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.fillRect(viewportArea_.withWidth(1));
    }
}

void RackComponent::resizedContent(juce::Rectangle<int> contentArea) {
    shellRows_.sideStrip = {};
    columnHeaderArea_ = viewportArea_ = chainTabsArea_ = {};

    // When collapsed, hide content controls only (buttons handled by resizedCollapsed)
    // NOTE: Side panels (macro/mods) visibility is managed by base class
    if (collapsed_) {
        shellRows_.footer = shellRows_.midiLed = selectorCaption_ = {};
        bandsToggle_->setVisible(false);
        faceplateToggle_->setVisible(false);
        if (crossoverDisplay_)
            crossoverDisplay_->setVisible(false);
        if (selector_)
            selector_->setVisible(false);
        addChainButton_.setVisible(false);
        chainViewport_.setVisible(false);
        chainViewport_.setBounds(0, 0, 0, 0);  // Clear stale bounds
        if (chainPanel_)
            chainPanel_->setVisible(false);
        if (gainSlider_)
            gainSlider_->setVisible(false);
        if (presetButton_)
            presetButton_->setVisible(false);
        // levelMeter_ is placed by resizedCollapsed
        return;
    }

    const bool bandsShown = !multiband_ || bandsShown_;
    addChainButton_.setVisible(bandsShown);
    chainViewport_.setVisible(bandsShown);
    modButton_->setVisible(true);
    macroButton_->setVisible(true);
    if (presetButton_)
        presetButton_->setVisible(true);
    levelMeter_.setVisible(true);

    shellRows_.sideStrip = contentArea.removeFromRight(SIDE_STRIP_WIDTH);
    layoutSideStrip(shellRows_.sideStrip);
    if (bandsShown)
        layoutChainList(contentArea.removeFromLeft(CHAIN_LIST_WIDTH));
    // A multiband rack's faceplate stands beside its bands, where a rack's chain list ends.
    faceplateArea_ = {};
    if (crossoverDisplay_)
        crossoverDisplay_->setVisible(multiband_ && faceplateShown_);
    if (multiband_ && faceplateShown_) {
        faceplateArea_ = contentArea.removeFromLeft(FACEPLATE_WIDTH)
                             .withTrimmedTop(8)
                             .withTrimmedLeft(bandsShown ? 0 : 12)
                             .withTrimmedRight(12)
                             .withTrimmedBottom(12);
        if (crossoverDisplay_) {
            crossoverDisplay_->setBounds(faceplateArea_);
            crossoverDisplay_->setVisible(true);
        }
    }

    viewportArea_ = contentArea;
    if (chainPanel_ && chainPanel_->isVisible())
        chainPanel_->setBounds(viewportArea_);
}

void RackComponent::layoutChainList(juce::Rectangle<int> list) {
    auto area = list.withTrimmedTop(4).reduced(12, 0).withTrimmedBottom(12);
    if (!multiband_)
        chainTabsArea_ = area.removeFromTop(CHAIN_TABS_HEIGHT);
    area.removeFromTop(ROW_GAP);
    columnHeaderArea_ = area.removeFromTop(COLUMN_HEADER_HEIGHT);

    const auto rowView = static_cast<ChainRowComponent::View>(static_cast<int>(chainView_));
    for (auto& row : chainRows_)
        row->setView(rowView);
    area.removeFromTop(ROW_GAP);
    chainViewport_.setBounds(area);

    const int totalHeight = stackedChainRowsHeight();
    const bool scrolls = totalHeight > area.getHeight();
    const int width = area.getWidth() - (scrolls ? chainViewport_.getScrollBarThickness() + 2 : 0);
    chainRowsContainer_.setSize(width, juce::jmax(totalHeight, area.getHeight()));
    int y = 0;
    if (multiband_) {
        // Bands read high to low; each divider is the crossover
        // between the bands either side of it.
        for (int band = static_cast<int>(chainRows_.size()) - 1; band >= 0; --band) {
            chainRows_[static_cast<std::size_t>(band)]->setBounds(
                0, y, width, ChainRowComponent::getPreferredHeight());
            y += ChainRowComponent::getPreferredHeight();
            if (band > 0 && band - 1 < static_cast<int>(crossoverDividers_.size())) {
                crossoverDividers_[static_cast<std::size_t>(band - 1)]->setBounds(
                    0, y, width, multiband::CrossoverDivider::kHeight);
                y += multiband::CrossoverDivider::kHeight;
            }
        }
        y += ROW_GAP;
    } else {
        for (auto& row : chainRows_) {
            row->setBounds(0, y, width, ChainRowComponent::getPreferredHeight());
            y += ChainRowComponent::getPreferredHeight() + ROW_GAP;
        }
    }
    addChainButton_.setBounds(0, y, width, ADD_CHAIN_HEIGHT);
}

// The 12px fader over the meter. Racks have no dry/wet (#2328) and no delta solo.
void RackComponent::layoutSideStrip(juce::Rectangle<int> strip) {
    auto area = strip.reduced(5, 6);
    const auto fader = area.withSizeKeepingCentre(12, area.getHeight());
    levelMeter_.setBounds(fader);
    if (gainSlider_) {
        gainSlider_->setBounds(fader);
        gainSlider_->setVisible(true);
        gainSlider_->toFront(false);
    }
}

void RackComponent::resizedShellFooter(juce::Rectangle<int> footer) {
    shellRows_.footer = footer;
    layoutFooter(footer);
}

// The chain selector under the chain list, and the MIDI LED on the right.
void RackComponent::layoutFooter(juce::Rectangle<int> footer) {
    auto area = footer.reduced(12, 0);
    shellRows_.midiLed = area.removeFromRight(7).withSizeKeepingCentre(7, 7);
    selectorCaption_ = {};
    bandsToggle_->setVisible(multiband_);
    faceplateToggle_->setVisible(multiband_);
    auto selector = area.withWidth(juce::jmin(area.getWidth(), CHAIN_LIST_WIDTH - 24));
    // Bands are chosen by frequency, never by the selector; the footer toggles the views instead.
    if (multiband_) {
        if (selector_)
            selector_->setVisible(false);
        const auto size = getHeaderButtonSize();
        for (auto* toggle : {bandsToggle_.get(), faceplateToggle_.get()}) {
            toggle->setBounds(area.removeFromLeft(size.x).withSizeKeepingCentre(size.x, size.y));
            area.removeFromLeft(getHeaderButtonGap());
        }
        return;
    }
    selectorCaption_ = selector.removeFromLeft(88);
    if (selector_) {
        selector_->setBounds(selector.withSizeKeepingCentre(selector.getWidth(), 16));
        selector_->setVisible(true);
    }
}

void RackComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    const auto placeLeft = [this, &headerArea](juce::Component& button) {
        const auto size = getHeaderButtonSize();
        button.setBounds(headerArea.removeFromLeft(size.x).withSizeKeepingCentre(size.x, size.y));
        headerArea.removeFromLeft(getHeaderButtonGap());
    };
    placeLeft(*macroButton_);
    placeLeft(*modButton_);
    // The separator carries its own 6px margins in place of the last gap.
    headerArea.setLeft(headerArea.getX() - getHeaderButtonGap());
    shellRows_.headerSeparatorLeft = headerArea.removeFromLeft(13).withSizeKeepingCentre(1, 14);
}

juce::String RackComponent::getCollapsedName() const {
    auto name = getNodeName();
    // Strip "Instrument Wrapper: " prefix for cleaner collapsed display
    if (name.startsWith("Instrument Wrapper: "))
        return name.substring(20);
    return name;
}

void RackComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    // Meter is positioned by base class via getCollapsedMeterWidth() -> collapsedMeterArea_
    levelMeter_.setBounds(collapsedMeterArea_);
    levelMeter_.setVisible(true);

    // Add macro and mod buttons vertically when collapsed
    int buttonSize = juce::jmin(16, area.getWidth() - 4);

    macroButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    macroButton_->setVisible(true);
    area.removeFromTop(4);
    modButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    modButton_->setVisible(true);
}

int RackComponent::stackedChainRowsHeight() const {
    const auto rows = static_cast<int>(chainRows_.size());
    if (multiband_)
        return rows * ChainRowComponent::getPreferredHeight() +
               juce::jmax(0, rows - 1) * multiband::CrossoverDivider::kHeight + ROW_GAP +
               ADD_CHAIN_HEIGHT;
    return rows * (ChainRowComponent::getPreferredHeight() + ROW_GAP) + ADD_CHAIN_HEIGHT;
}

int RackComponent::getPreferredHeight() const {
    // Content padding: NodeComponent insets the content 1px top and bottom.
    const int top = multiband_ ? 0 : CHAIN_TABS_HEIGHT;
    const int list = 4 + top + ROW_GAP + COLUMN_HEADER_HEIGHT + ROW_GAP + stackedChainRowsHeight();
    const int faceplate = 8 + FACEPLATE_MIN_HEIGHT;
    const int body = !multiband_       ? list
                     : !bandsShown_    ? faceplate
                     : faceplateShown_ ? juce::jmax(list, faceplate)
                                       : list;
    return HEADER_BAR_HEIGHT + body + 12 + FOOTER_BAR_HEIGHT + 2;
}

int RackComponent::fixedWidth() const {
    // Content padding: NodeComponent insets the content 2px each side.
    const bool bands = !multiband_ || bandsShown_;
    const bool faceplate = multiband_ && faceplateShown_;
    return (bands ? CHAIN_LIST_WIDTH : 0) + (faceplate ? FACEPLATE_WIDTH : 0) + SIDE_STRIP_WIDTH +
           4 + getLeftPanelsWidth() + getRightPanelsWidth();
}

int RackComponent::getPreferredWidth() const {
    // When collapsed, return collapsed strip width + meter + any visible side panels
    if (collapsed_) {
        return getLeftPanelsWidth() + NodeComponent::COLLAPSED_WIDTH + METER_STRIP_WIDTH + 2 +
               getRightPanelsWidth();
    }

    int viewportWidth = MIN_VIEWPORT_WIDTH;
    if (chainPanel_ && chainPanel_->isVisible())
        viewportWidth = juce::jmax(viewportWidth, chainPanel_->getContentWidth());
    if (availableWidth_ > 0)
        viewportWidth = juce::jlimit(MIN_VIEWPORT_WIDTH,
                                     juce::jmax(MIN_VIEWPORT_WIDTH, availableWidth_ - fixedWidth()),
                                     viewportWidth);
    return fixedWidth() + viewportWidth;
}

int RackComponent::getMinimumWidth() const {
    return fixedWidth() + MIN_VIEWPORT_WIDTH;
}

void RackComponent::setAvailableWidth(int width) {
    availableWidth_ = width;

    // Pass remaining width to chain panel after accounting for base rack width
    if (chainPanel_ && chainPanel_->isVisible())
        chainPanel_->setMaxWidth(juce::jmax(MIN_VIEWPORT_WIDTH, width - fixedWidth()));
}

void RackComponent::updateFromRack(const magda::RackInfo& rack) {
    setNodeName(rack.name);
    setBypassed(rack.bypassed);
    if (gainSlider_)
        gainSlider_->setValue(rack.volume, juce::dontSendNotification);
    if (selector_ && !selector_->getModulatedValue())
        selector_->setValue(rack.chainSelector, juce::dontSendNotification);
    rebuildChainRows();

    // Refresh both panels if either is visible.
    refreshPanels();

    // Also refresh the chain panel if it's showing a chain
    if (chainPanel_ && chainPanel_->isVisible() && selectedChainId_ != magda::INVALID_CHAIN_ID) {
        // Check if the selected chain still exists in this rack
        bool chainExists = false;
        for (const auto& chain : rack.chains) {
            if (chain.id == selectedChainId_) {
                chainExists = true;
                break;
            }
        }

        if (chainExists) {
            chainPanel_->refresh();
        } else {
            // Chain was deleted, hide the panel
            hideChainPanel();
        }
    }

    // Auto-expand: if no chain panel is showing and the rack has exactly one chain
    // with at least one device, show it automatically so the user sees the chain content.
    // Defer via MessageManager to avoid recursion during initialization (resized not yet valid).
    if (selectedChainId_ == magda::INVALID_CHAIN_ID && rack.chains.size() == 1) {
        if (!rack.chains[0].elements.empty()) {
            auto chainId = rack.chains[0].id;
            auto safeThis = juce::Component::SafePointer<RackComponent>(this);
            juce::MessageManager::callAsync([safeThis, chainId]() {
                if (safeThis != nullptr && safeThis->selectedChainId_ == magda::INVALID_CHAIN_ID)
                    safeThis->showChainPanel(chainId);
            });
        }
    }
}

void RackComponent::rebuildChainRows() {
    // Use path-based lookup to support nested racks at any depth
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (!rack) {
        unfocusAllComponents();
        chainRows_.clear();
        resized();
        repaint();
        return;
    }

    // Smart rebuild: preserve existing rows, only add/remove as needed
    std::vector<std::unique_ptr<ChainRowComponent>> newRows;

    for (const auto& chain : rack->chains) {
        // Check if we already have a row for this chain
        std::unique_ptr<ChainRowComponent> existingRow;
        for (auto it = chainRows_.begin(); it != chainRows_.end(); ++it) {
            if ((*it)->getChainId() == chain.id) {
                // Found existing row - preserve it and update its data
                existingRow = std::move(*it);
                chainRows_.erase(it);
                existingRow->updateFromChain(chain);
                break;
            }
        }

        if (existingRow) {
            // Update the path in case hierarchy changed
            existingRow->setNodePath(rackPath_.withChain(chain.id));
            newRows.push_back(std::move(existingRow));
        } else {
            // Create new row for new chain
            auto row = std::make_unique<ChainRowComponent>(*this, trackId_, rackId_, chain);
            // Set the full nested path (includes parent rack/chain context)
            row->setNodePath(rackPath_.withChain(chain.id));
            if (selector_)
                row->setModulatedSelector(selector_->getModulatedValue());
            // Wire up double-click to toggle expand/collapse
            row->onDoubleClick = [this](magda::ChainId chainId) {
                if (selectedChainId_ == chainId) {
                    // Already showing this chain - collapse it
                    hideChainPanel();
                } else {
                    // Show this chain
                    showChainPanel(chainId);
                }
            };
            chainRowsContainer_.addAndMakeVisible(*row);
            newRows.push_back(std::move(row));
        }
    }

    // Unfocus before destroying remaining old rows (chains that were removed)
    if (!chainRows_.empty()) {
        unfocusAllComponents();
    }

    // Move new rows to member variable (old rows are destroyed here)
    chainRows_ = std::move(newRows);
    for (size_t i = 0; i < chainRows_.size(); ++i)
        chainRows_[i]->setColourIndex(static_cast<int>(i));
    syncMultiband(*rack);
    const int chains = static_cast<int>(chainRows_.size());
    if (multiband_)
        setHeaderSubtitle(juce::String::fromUTF8("Multiband Rack \xc2\xb7 ") +
                          juce::String(chains) + " bands");
    else
        setHeaderSubtitle(juce::String::fromUTF8("Rack \xc2\xb7 ") + juce::String(chains) +
                          (chains == 1 ? " chain" : " chains"));

    resized();
    repaint();
}

void RackComponent::syncMultiband(const magda::RackInfo& rack) {
    multiband_ = rack.isMultiband();
    faceplateShown_ = rack.faceplateShown;
    bandsShown_ = rack.bandsShown || !rack.faceplateShown;
    bandsToggle_->setToggleState(bandsShown_, juce::dontSendNotification);
    faceplateToggle_->setToggleState(faceplateShown_, juce::dontSendNotification);
    if (!multiband_) {
        crossoverDisplay_.reset();
        crossoverDividers_.clear();
        return;
    }
    chainView_ = ChainView::Mix;
    if (crossoverDisplay_ == nullptr) {
        crossoverDisplay_ = std::make_unique<multiband::CrossoverDisplay>(rackPath_);
        addAndMakeVisible(*crossoverDisplay_);
    }
    crossoverDisplay_->repaint();
    const auto crossovers = rack.crossovers.size();
    while (crossoverDividers_.size() > crossovers)
        crossoverDividers_.pop_back();
    while (crossoverDividers_.size() < crossovers) {
        auto divider = std::make_unique<multiband::CrossoverDivider>(
            rackPath_, static_cast<int>(crossoverDividers_.size()));
        chainRowsContainer_.addAndMakeVisible(*divider);
        crossoverDividers_.push_back(std::move(divider));
    }
    for (auto& divider : crossoverDividers_)
        divider->repaint();
    const int bands = static_cast<int>(chainRows_.size());
    for (int band = 0; band < bands; ++band)
        chainRows_[static_cast<std::size_t>(band)]->setPlaceholderName(
            multiband::bandName(band, bands));
    addChainButton_.setButtonText("Split band");
    addChainButton_.setTooltip("Split the selected band at its centre");
    addChainButton_.setEnabled(bands <= magda::kMaxCrossovers);
}

// The selected band, or the top one when none is, splits at its centre.
void RackComponent::splitSelectedBand() {
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (rack == nullptr || rack->chains.empty())
        return;
    auto band = static_cast<int>(rack->chains.size()) - 1;
    const auto selected = magda::SelectionManager::getInstance().getSelectedChainNode();
    for (std::size_t i = 0; i < rack->chains.size(); ++i)
        if (selected == rackPath_.withChain(rack->chains[i].id))
            band = static_cast<int>(i);
    if (!magda::canSplitBand(rack->crossovers, static_cast<std::size_t>(band)))
        return;
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SplitRackBandCommand>(rackPath_, band));
}

void RackComponent::childLayoutChanged() {
    resized();
    repaint();
    if (onLayoutChanged) {
        onLayoutChanged();
    }
}

void RackComponent::clearChainSelection() {
    for (auto& row : chainRows_) {
        row->setSelected(false);
    }
}

void RackComponent::clearDeviceSelection() {
    if (chainPanel_) {
        chainPanel_->clearDeviceSelection();
    }
}

void RackComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    // First let base class handle visual selection state
    NodeComponent::chainNodeSelectionChanged(path);
    if (crossoverDisplay_)
        crossoverDisplay_->repaint();

    // Check if the selected path is one of our chains
    if (path.trackId != trackId_) {
        return;  // Not our track
    }

    // Check if the path is a chain within this rack
    if (path.steps.empty() || path.steps.size() != rackPath_.steps.size() + 1) {
        return;  // Not a direct child chain or invalid path
    }

    // Verify all parent steps match
    for (size_t i = 0; i < rackPath_.steps.size(); ++i) {
        if (i >= path.steps.size() || path.steps[i].type != rackPath_.steps[i].type ||
            path.steps[i].id != rackPath_.steps[i].id) {
            return;  // Not in this rack or invalid path
        }
    }

    // Verify the last step is a chain
    if (path.steps.back().type != magda::ChainStepType::Chain) {
        return;  // Not a chain
    }

    magda::ChainId chainId = path.steps.back().id;

    // Show chain panel within this rack
    showChainPanel(chainId);

    // Notify parent (for clearing selections in other racks)
    if (onChainSelected) {
        onChainSelected(trackId_, rackId_, chainId);
    }
}

void RackComponent::onAddChainClicked() {
    if (multiband_) {
        splitSelectedBand();
        return;
    }
    auto newChainId = magda::TrackManager::getInstance().addChainToRack(rackPath_);

    // Auto-select the newly created chain
    if (newChainId != magda::INVALID_CHAIN_ID) {
        auto newChainPath = rackPath_.withChain(newChainId);
        magda::SelectionManager::getInstance().selectChainNode(newChainPath);
    }
}

void RackComponent::showChainPanel(magda::ChainId chainId) {
    selectedChainId_ = chainId;
    if (chainPanel_) {
        auto chainPath = rackPath_.withChain(chainId);
        chainPanel_->showChain(chainPath);
        childLayoutChanged();
    }
}

void RackComponent::hideChainPanel() {
    selectedChainId_ = magda::INVALID_CHAIN_ID;
    // Don't call clearChainSelection() - let SelectionManager control visual selection
    // This allows collapsing the chain panel while keeping the chain selected
    if (chainPanel_) {
        chainPanel_->clear();
        childLayoutChanged();
    }
}

bool RackComponent::isChainPanelVisible() const {
    return chainPanel_ && chainPanel_->isVisible();
}

// === Virtual data provider overrides ===

const magda::ModArray* RackComponent::getModsData() const {
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    return rack ? &rack->mods : nullptr;
}

const magda::MacroArray* RackComponent::getMacrosData() const {
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    return rack ? &rack->macros : nullptr;
}

std::vector<std::pair<magda::DeviceId, juce::String>> RackComponent::getAvailableDevices() const {
    // Emit a chain-header sentinel (deviceId == INVALID_DEVICE_ID) before each
    // chain's devices. Knob menus that build a chain-grouped picker (the
    // macro "Link to Parameter…" submenu) detect these to start a new
    // submenu; consumers that just iterate device IDs (unlink-name lookup,
    // mod knobs) skip sentinels naturally because their deviceId compare
    // can't match an invalid id.
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices;
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (rack) {
        for (const auto& chain : rack->chains) {
            availableDevices.emplace_back(magda::INVALID_DEVICE_ID, chain.displayName());
            for (const auto& element : chain.elements) {
                if (magda::isDevice(element)) {
                    const auto& device = magda::getDevice(element);
                    availableDevices.emplace_back(device.id, device.name);
                }
            }
        }
    }
    return availableDevices;
}

std::map<magda::DeviceId, std::vector<juce::String>> RackComponent::getDeviceParamNames() const {
    std::map<magda::DeviceId, std::vector<juce::String>> result;
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (rack == nullptr)
        return result;
    // One level, the same devices getAvailableDevices() offers: a nested rack's
    // devices are picked from that rack's own component.
    for (const auto& chain : rack->chains) {
        for (const auto& element : chain.elements) {
            if (!magda::isDevice(element))
                continue;
            const auto& device = magda::getDevice(element);
            result[device.id] = device.paramNamesByIndex();
        }
    }
    return result;
}

// === Virtual callback overrides for mod/macro persistence ===

void RackComponent::onModTargetChangedInternal(int modIndex, magda::ControlTarget target) {
    magda::TrackManager::getInstance().setModTarget(rackPath_, modIndex, target);
}

void RackComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetModNameCommand>(rackPath_, modIndex, name));
}

void RackComponent::onModTypeChangedInternal(int modIndex, magda::ModType type) {
    magda::TrackManager::getInstance().setModType(rackPath_, modIndex, type);
}

void RackComponent::onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().setModWaveform(rackPath_, modIndex, waveform);
}

void RackComponent::onModRateChangedInternal(int modIndex, float rate) {
    magda::TrackManager::getInstance().setModRate(rackPath_, modIndex, rate);
}

void RackComponent::onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) {
    magda::TrackManager::getInstance().setModPhaseOffset(rackPath_, modIndex, phaseOffset);
}

void RackComponent::onModTempoSyncChangedInternal(int modIndex, bool tempoSync) {
    magda::TrackManager::getInstance().setModTempoSync(rackPath_, modIndex, tempoSync);
}

void RackComponent::onModSyncDivisionChangedInternal(int modIndex, magda::SyncDivision division) {
    magda::TrackManager::getInstance().setModSyncDivision(rackPath_, modIndex, division);
}

void RackComponent::onModTriggerModeChangedInternal(int modIndex, magda::LFOTriggerMode mode) {
    magda::TrackManager::getInstance().setModTriggerMode(rackPath_, modIndex, mode);
}

void RackComponent::onModAudioAttackChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioAttack(rackPath_, modIndex, ms);
}

void RackComponent::onModAudioReleaseChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioRelease(rackPath_, modIndex, ms);
}

void RackComponent::onModEnvelopeChangedInternal(int modIndex, const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModEnvelope(rackPath_, modIndex, mod);
}

void RackComponent::onModRandomChangedInternal(int modIndex, const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModRandom(rackPath_, modIndex, mod);
}

void RackComponent::onModFollowerChangedInternal(int modIndex, const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModFollower(rackPath_, modIndex, mod);
}

void RackComponent::onModCurveChangedInternal(int /*modIndex*/) {
    DBG("[HardCorner] RackComponent notifyModCurveChanged path=" << rackPath_.toString());
    // Curve points are already written directly to ModInfo by LFOCurveEditor.
    // Just notify the audio thread to pick up the new data.
    magda::TrackManager::getInstance().notifyModCurveChanged(rackPath_);
}

void RackComponent::onMacroValueChangedInternal(int macroIndex, float value) {
    magda::TrackManager::getInstance().setMacroValue(rackPath_, macroIndex, value);

    // Refresh chain panel to update parameter movement indicators
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanel_->updateParamIndicators();
    }
}

void RackComponent::onMacroTargetChangedInternal(int macroIndex, magda::ControlTarget target) {
    magda::TrackManager::getInstance().setMacroTarget(rackPath_, macroIndex, target);
}

void RackComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetMacroNameCommand>(rackPath_, macroIndex, name));
}

void RackComponent::onModClickedInternal(int modIndex) {
    // Select this mod in the SelectionManager for inspector display
    magda::SelectionManager::getInstance().selectMod(rackPath_, modIndex);
}

void RackComponent::onMacroClickedInternal(int macroIndex) {
    // Select this macro in the SelectionManager for inspector display
    magda::SelectionManager::getInstance().selectMacro(rackPath_, macroIndex);
    DBG("Macro clicked: " << macroIndex << " on path: " << rackPath_.toString());
}

// === Virtual callbacks for page management ===

void RackComponent::onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                              magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().addMod(rackPath_, slotIndex, type, waveform);
    // Refresh both panels so the macro-link menu picks up the new mod as a
    // possible target — the f4f556f regression was a missing macro refresh
    // here. refreshPanels() guards each by its own visibility flag.
    refreshPanels();
}

void RackComponent::onModRemoveRequestedInternal(int modIndex) {
    magda::TrackManager::getInstance().removeMod(rackPath_, modIndex);
    refreshPanels();
}

void RackComponent::onModEnableToggledInternal(int modIndex, bool enabled) {
    magda::TrackManager::getInstance().setModEnabled(rackPath_, modIndex, enabled);
}

void RackComponent::onModPageAddRequested(int /*itemsToAdd*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void RackComponent::onModPageRemoveRequested(int /*itemsToRemove*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void RackComponent::onMacroPageAddRequested(int /*itemsToAdd*/) {
    magda::TrackManager::getInstance().addMacroPage(rackPath_);
}

void RackComponent::onMacroPageRemoveRequested(int /*itemsToRemove*/) {
    magda::TrackManager::getInstance().removeMacroPage(rackPath_);
}

// === Panel width overrides ===

int RackComponent::getParamPanelWidth() const {
    return DEFAULT_PANEL_WIDTH;
}

int RackComponent::getModPanelWidth() const {
    // Width for 2 columns of mod knobs (2x4 grid)
    return DEFAULT_PANEL_WIDTH;
}

// =============================================================================
// MAGDA Rack Presets — UI wiring for PresetManager::save/loadRackPreset
// =============================================================================

namespace {
void showRackPresetErrorAsync(const juce::String& title, const juce::String& message) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

// Recursively walk the rack-presets directory and append items / submenus to
// `menu`. `outIndex` collects the relative path of every preset file in click
// order so the chosen menu id can be resolved back to a path. Mirrors the
// equivalent helper in DeviceSlotComponent.
void buildRackPresetSubmenu(juce::PopupMenu& menu, const juce::File& dir,
                            const juce::String& prefix, int idBase,
                            const juce::String& currentLoaded, juce::StringArray& outIndex) {
    if (!dir.isDirectory())
        return;
    auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.mps");
    subdirs.sort();
    files.sort();

    for (const auto& sub : subdirs) {
        juce::PopupMenu submenu;
        buildRackPresetSubmenu(submenu, sub, prefix + sub.getFileName() + "/", idBase,
                               currentLoaded, outIndex);
        menu.addSubMenu(sub.getFileName(), submenu);
    }
    for (const auto& f : files) {
        const auto displayName = f.getFileNameWithoutExtension();
        const auto relPath = prefix + displayName;
        outIndex.add(relPath);
        const bool ticked = (relPath == currentLoaded);
        menu.addItem(idBase + outIndex.size() - 1, displayName, /*isActive*/ true, ticked);
    }
}
}  // namespace

void RackComponent::showPresetMenu() {
    auto& pm = magda::PresetManager::getInstance();

    constexpr int kSaveOverwrite = 1;
    constexpr int kSaveAs = 2;
    constexpr int kRevealInFinder = 3;
    constexpr int kPresetIdBase = 1000;

    juce::PopupMenu menu;
    menu.addSectionHeader("MAGDA Rack Presets");

    juce::StringArray index;  // relative paths, indexed by chosen-id - kPresetIdBase
    buildRackPresetSubmenu(menu, pm.getRacksDirectory(), "", kPresetIdBase, currentPresetName_,
                           index);

    if (index.isEmpty())
        menu.addItem(kPresetIdBase, "(no presets yet)", /*isActive*/ false);

    menu.addSeparator();
    if (currentPresetName_.isNotEmpty())
        menu.addItem(kSaveOverwrite, "Save \"" + currentPresetName_ + "\"");
    menu.addItem(kSaveAs, "Save as MAGDA Rack Preset...");
    menu.addItem(kRevealInFinder, "Reveal in Finder");

    const auto indexCopy = index;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(presetButton_.get()),
        [this, indexCopy](int chosen) {
            if (chosen == 0)
                return;
            if (chosen == kSaveAs) {
                showSaveRackPresetDialog();
            } else if (chosen == kSaveOverwrite) {
                saveCurrentRackPreset();
            } else if (chosen == kRevealInFinder) {
                magda::PresetManager::getInstance().getRacksDirectory().revealToUser();
            } else if (chosen >= kPresetIdBase) {
                const int idx = chosen - kPresetIdBase;
                if (idx >= 0 && idx < indexCopy.size())
                    loadRackPresetByName(indexCopy[idx]);
            }
        });
}

void RackComponent::showSaveRackPresetDialog() {
    const auto* live = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    const juce::String defaultName =
        currentPresetName_.isNotEmpty() ? currentPresetName_ : (live ? live->name : "Rack");

    auto* aw = new juce::AlertWindow(
        "Save MAGDA Rack Preset",
        R"(Enter a name for this rack preset (use "/" to nest, e.g. "Drums/808 Stack"):)",
        juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", defaultName, "Name:");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<RackComponent> self(this);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([aw, self](int result) {
            if (result != 1) {
                delete aw;
                return;
            }
            auto name = aw->getTextEditorContents("name").trim();
            delete aw;
            if (name.isEmpty() || self == nullptr)
                return;

            auto doSave = [name, self]() {
                if (self == nullptr)
                    return;
                const auto* fresh =
                    magda::TrackManager::getInstance().getRackByPath(self->rackPath_);
                if (fresh == nullptr) {
                    showRackPresetErrorAsync("Save Rack Preset Failed", "Rack no longer exists.");
                    return;
                }
                auto& mgr = magda::PresetManager::getInstance();
                if (!mgr.saveRackPreset(*fresh, name)) {
                    showRackPresetErrorAsync("Save Rack Preset Failed", mgr.getLastError());
                    return;
                }
                self->currentPresetName_ = name;
            };

            if (magda::PresetManager::getInstance().getRackPresets().contains(name)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Rack Preset?")
                        .withMessage("\"" + name + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int r) {
                        if (r == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void RackComponent::saveCurrentRackPreset() {
    if (currentPresetName_.isEmpty())
        return;
    const auto* live = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (live == nullptr)
        return;
    auto& pm = magda::PresetManager::getInstance();
    if (!pm.saveRackPreset(*live, currentPresetName_))
        showRackPresetErrorAsync("Save Rack Preset Failed", pm.getLastError());
}

void RackComponent::loadRackPresetByName(const juce::String& presetName) {
    auto& pm = magda::PresetManager::getInstance();
    magda::RackInfo preset;
    if (!pm.loadRackPreset(presetName, preset)) {
        showRackPresetErrorAsync("Load Rack Preset Failed", pm.getLastError());
        return;
    }
    if (!magda::TrackManager::getInstance().applyRackPreset(rackPath_, preset)) {
        showRackPresetErrorAsync("Load Rack Preset Failed", "Failed to apply preset to live rack.");
        return;
    }
    currentPresetName_ = presetName;
}

}  // namespace magda::daw::ui
