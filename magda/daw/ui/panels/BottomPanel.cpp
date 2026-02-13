#include "BottomPanel.hpp"

#include "../components/common/DraggableValueLabel.hpp"
#include "../components/common/SvgButton.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "../themes/SmallButtonLookAndFeel.hpp"
#include "AudioBridge.hpp"
#include "AudioEngine.hpp"
#include "BinaryData.h"
#include "audio/DrumGridPlugin.hpp"
#include "content/DrumGridClipContent.hpp"
#include "content/MidiEditorContent.hpp"
#include "content/PianoRollContent.hpp"
#include "state/PanelController.hpp"

namespace magda {

namespace {
namespace te = tracktion::engine;

bool trackHasDrumGrid(TrackId trackId) {
    auto* audioEngine = TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return false;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return false;
    auto* teTrack = bridge->getAudioTrack(trackId);
    if (!teTrack)
        return false;

    for (auto* plugin : teTrack->pluginList) {
        if (dynamic_cast<daw::audio::DrumGridPlugin*>(plugin))
            return true;
        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    if (dynamic_cast<daw::audio::DrumGridPlugin*>(innerPlugin))
                        return true;
                }
            }
        }
    }
    return false;
}
}  // namespace

BottomPanel::BottomPanel() : TabbedPanel(daw::ui::PanelLocation::Bottom) {
    setName("Bottom Panel");

    // Create editor tab icon buttons (hidden by default)
    pianoRollTab_ = std::make_unique<SvgButton>("PianoRollTab", BinaryData::piano_roll_svg,
                                                BinaryData::piano_roll_svgSize);
    pianoRollTab_->setTooltip("Piano Roll");
    pianoRollTab_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    pianoRollTab_->onClick = [this]() {
        if (!updatingTabs_)
            onEditorTabChanged(0);
    };
    addChildComponent(pianoRollTab_.get());

    drumGridTab_ = std::make_unique<SvgButton>("DrumGridTab", BinaryData::drum_grid_svg,
                                               BinaryData::drum_grid_svgSize);
    drumGridTab_->setTooltip("Drum Grid");
    drumGridTab_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    drumGridTab_->onClick = [this]() {
        if (!updatingTabs_)
            onEditorTabChanged(1);
    };
    addChildComponent(drumGridTab_.get());

    // Create header controls
    setupHeaderControls();

    // Register as listener for selection changes
    ClipManager::getInstance().addListener(this);
    TrackManager::getInstance().addListener(this);

    // Register as TimelineStateListener for grid sync
    // Note: TimelineController may not exist yet at construction time.
    // Registration is retried lazily in updateContentBasedOnSelection().
    if (auto* controller = TimelineController::getCurrent()) {
        timelineListenerGuard_.reset(controller);
    }

    // Sync initial grid state from timeline
    syncGridStateFromTimeline();

    // Set initial content based on current selection
    updateContentBasedOnSelection();
}

BottomPanel::~BottomPanel() {
    // Clear LookAndFeel references before destruction
    if (timeModeButton_)
        timeModeButton_->setLookAndFeel(nullptr);
    if (autoGridButton_)
        autoGridButton_->setLookAndFeel(nullptr);
    if (snapButton_)
        snapButton_->setLookAndFeel(nullptr);

    ClipManager::getInstance().removeListener(this);
    TrackManager::getInstance().removeListener(this);
    // TimelineController listener removed automatically by timelineListenerGuard_

    // Explicitly destroy before base class teardown to avoid repaint during partial destruction
    pianoRollTab_.reset();
    drumGridTab_.reset();
}

void BottomPanel::setupHeaderControls() {
    auto& smallLF = daw::ui::SmallButtonLookAndFeel::getInstance();

    // ABS/REL toggle
    timeModeButton_ = std::make_unique<juce::TextButton>("ABS");
    timeModeButton_->setTooltip("Toggle between Absolute and Relative time display");
    timeModeButton_->setClickingTogglesState(true);
    timeModeButton_->setToggleState(relativeTimeMode_, juce::dontSendNotification);
    timeModeButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    timeModeButton_->setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.3f));
    timeModeButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    timeModeButton_->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    timeModeButton_->setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    timeModeButton_->setWantsKeyboardFocus(false);
    timeModeButton_->setLookAndFeel(&smallLF);
    timeModeButton_->onClick = [this]() {
        relativeTimeMode_ = timeModeButton_->getToggleState();
        timeModeButton_->setButtonText(relativeTimeMode_ ? "REL" : "ABS");
        applyTimeModeToContent();
    };
    addChildComponent(timeModeButton_.get());

    // Grid numerator
    gridNumeratorLabel_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    gridNumeratorLabel_->setRange(1.0, 128.0, 1.0);
    gridNumeratorLabel_->setValue(static_cast<double>(gridNumerator_), juce::dontSendNotification);
    gridNumeratorLabel_->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    gridNumeratorLabel_->setShowFillIndicator(false);
    gridNumeratorLabel_->setFontSize(12.0f);
    gridNumeratorLabel_->setDoubleClickResetsValue(true);
    gridNumeratorLabel_->setDrawBorder(false);
    gridNumeratorLabel_->setEnabled(!isAutoGrid_);
    gridNumeratorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    gridNumeratorLabel_->onValueChange = [this]() {
        gridNumerator_ = static_cast<int>(std::round(gridNumeratorLabel_->getValue()));
        if (!isAutoGrid_) {
            auto* content = getActiveContent();
            auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
            if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
                midiEditor->setGridSettingsFromUI(isAutoGrid_, gridNumerator_, gridDenominator_);
            } else if (auto* controller = TimelineController::getCurrent()) {
                controller->dispatch(
                    SetGridQuantizeEvent{isAutoGrid_, gridNumerator_, gridDenominator_});
            }
        }
    };
    addChildComponent(gridNumeratorLabel_.get());

    // Slash separator
    gridSlashLabel_ = std::make_unique<juce::Label>();
    gridSlashLabel_->setText("/", juce::dontSendNotification);
    gridSlashLabel_->setFont(FontManager::getInstance().getUIFont(12.0f));
    gridSlashLabel_->setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    gridSlashLabel_->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    gridSlashLabel_->setJustificationType(juce::Justification::centred);
    gridSlashLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    addChildComponent(gridSlashLabel_.get());

    // Grid denominator
    gridDenominatorLabel_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Integer);
    gridDenominatorLabel_->setRange(1.0, 64.0, 4.0);
    gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                    juce::dontSendNotification);
    gridDenominatorLabel_->setTextColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    gridDenominatorLabel_->setShowFillIndicator(false);
    gridDenominatorLabel_->setFontSize(12.0f);
    gridDenominatorLabel_->setDoubleClickResetsValue(true);
    gridDenominatorLabel_->setDrawBorder(false);
    gridDenominatorLabel_->setEnabled(!isAutoGrid_);
    gridDenominatorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    gridDenominatorLabel_->onValueChange = [this]() {
        // Constrain to nearest power of 2
        int raw = static_cast<int>(std::round(gridDenominatorLabel_->getValue()));
        int pow2 = 1;
        while (pow2 * 2 <= raw)
            pow2 *= 2;
        if (raw - pow2 > pow2 * 2 - raw && pow2 * 2 <= 64)
            pow2 *= 2;
        gridDenominator_ = pow2;
        gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                        juce::dontSendNotification);
        if (!isAutoGrid_) {
            auto* content = getActiveContent();
            auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
            if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
                midiEditor->setGridSettingsFromUI(isAutoGrid_, gridNumerator_, gridDenominator_);
            } else if (auto* controller = TimelineController::getCurrent()) {
                controller->dispatch(
                    SetGridQuantizeEvent{isAutoGrid_, gridNumerator_, gridDenominator_});
            }
        }
    };
    addChildComponent(gridDenominatorLabel_.get());

    // AUTO toggle
    autoGridButton_ = std::make_unique<juce::TextButton>("AUTO");
    autoGridButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    autoGridButton_->setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.3f));
    autoGridButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoGridButton_->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    autoGridButton_->setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    autoGridButton_->setWantsKeyboardFocus(false);
    autoGridButton_->setClickingTogglesState(true);
    autoGridButton_->setToggleState(isAutoGrid_, juce::dontSendNotification);
    autoGridButton_->setLookAndFeel(&smallLF);
    autoGridButton_->onClick = [this]() {
        isAutoGrid_ = autoGridButton_->getToggleState();
        gridNumeratorLabel_->setEnabled(!isAutoGrid_);
        gridDenominatorLabel_->setEnabled(!isAutoGrid_);
        gridNumeratorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
        gridDenominatorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
        gridSlashLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
        auto* content = getActiveContent();
        auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
        if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
            midiEditor->setGridSettingsFromUI(isAutoGrid_, gridNumerator_, gridDenominator_);
        } else if (auto* controller = TimelineController::getCurrent()) {
            controller->dispatch(
                SetGridQuantizeEvent{isAutoGrid_, gridNumerator_, gridDenominator_});
        }
    };
    addChildComponent(autoGridButton_.get());

    // SNAP toggle
    snapButton_ = std::make_unique<juce::TextButton>("SNAP");
    snapButton_->setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
    snapButton_->setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.3f));
    snapButton_->setColour(juce::TextButton::textColourOffId,
                           DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    snapButton_->setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    snapButton_->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                   juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    snapButton_->setWantsKeyboardFocus(false);
    snapButton_->setClickingTogglesState(true);
    snapButton_->setToggleState(isSnapEnabled_, juce::dontSendNotification);
    snapButton_->setLookAndFeel(&smallLF);
    snapButton_->onClick = [this]() {
        isSnapEnabled_ = snapButton_->getToggleState();
        auto* content = getActiveContent();
        auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
        if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
            midiEditor->setSnapEnabledFromUI(isSnapEnabled_);
        } else if (auto* controller = TimelineController::getCurrent()) {
            controller->dispatch(SetSnapEnabledEvent{isSnapEnabled_});
        }
    };
    addChildComponent(snapButton_.get());
}

void BottomPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Bottom, collapsed);
}

void BottomPanel::paint(juce::Graphics& g) {
    TabbedPanel::paint(g);
}

void BottomPanel::resized() {
    // Position editor tab icons and header controls at the top of the panel area
    if (showEditorTabs_) {
        auto headerBounds = getLocalBounds().removeFromTop(EDITOR_TAB_HEIGHT);

        // Controls right-aligned, leaving space for collapse button
        auto controlsArea = headerBounds;
        controlsArea.removeFromRight(30);  // Space for collapse button

        // Layout controls from right to left
        int x = controlsArea.getRight();
        int y = controlsArea.getY();
        int h = controlsArea.getHeight();
        int vPad = 4;

        // SNAP
        x -= 36;
        snapButton_->setBounds(x, y + vPad, 36, h - vPad * 2);
        x -= 4;

        // AUTO
        x -= 36;
        autoGridButton_->setBounds(x, y + vPad, 36, h - vPad * 2);
        x -= 4;

        // Denominator
        x -= 24;
        gridDenominatorLabel_->setBounds(x, y + vPad, 24, h - vPad * 2);

        // Slash
        x -= 8;
        gridSlashLabel_->setBounds(x, y, 8, h);

        // Numerator
        x -= 24;
        gridNumeratorLabel_->setBounds(x, y + vPad, 24, h - vPad * 2);
        x -= 4;

        // ABS/REL
        x -= 36;
        timeModeButton_->setBounds(x, y + vPad, 36, h - vPad * 2);

        // Tab icon buttons on the left
        int iconSize = h - 4;
        int tabX = headerBounds.getX() + 4;
        int tabY = y + (h - iconSize) / 2;
        pianoRollTab_->setBounds(tabX, tabY, iconSize, iconSize);
        tabX += iconSize + 4;
        drumGridTab_->setBounds(tabX, tabY, iconSize, iconSize);
    }

    // TabbedPanel::resized() uses getContentBounds() which accounts for the tab bar
    TabbedPanel::resized();
}

void BottomPanel::clipsChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::clipSelectionChanged(ClipId /*clipId*/) {
    updateContentBasedOnSelection();
    syncGridControlsFromContent();
}

void BottomPanel::tracksChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::trackSelectionChanged(TrackId /*trackId*/) {
    updateContentBasedOnSelection();
}

void BottomPanel::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    if (hasFlag(changes, ChangeFlags::Display)) {
        // If a MIDI editor is active, the controls reflect clip state -- skip arrangement sync
        auto* content = getActiveContent();
        auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
        if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
            return;
        }

        const auto& gq = state.display.gridQuantize;
        // Sync grid controls from timeline state (e.g. changed from TransportPanel)
        isAutoGrid_ = gq.autoGrid;
        gridNumerator_ = gq.numerator;
        gridDenominator_ = gq.denominator;
        isSnapEnabled_ = state.display.snapEnabled;

        autoGridButton_->setToggleState(isAutoGrid_, juce::dontSendNotification);
        // Don't overwrite labels while the user is actively dragging them
        // (our own dispatch triggers this callback synchronously)
        if (!gridNumeratorLabel_->isDragging())
            gridNumeratorLabel_->setValue(static_cast<double>(gridNumerator_),
                                          juce::dontSendNotification);
        if (!gridDenominatorLabel_->isDragging())
            gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                            juce::dontSendNotification);
        snapButton_->setToggleState(isSnapEnabled_, juce::dontSendNotification);

        gridNumeratorLabel_->setEnabled(!isAutoGrid_);
        gridDenominatorLabel_->setEnabled(!isAutoGrid_);
        gridNumeratorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
        gridDenominatorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
        gridSlashLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    }
}


void BottomPanel::updateContentBasedOnSelection() {
    // Lazy registration: BottomPanel may be constructed before TimelineController
    if (!timelineListenerGuard_.get()) {
        if (auto* controller = TimelineController::getCurrent()) {
            timelineListenerGuard_.reset(controller);
            syncGridStateFromTimeline();
        }
    }

    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    ClipId selectedClip = clipManager.getSelectedClip();
    TrackId selectedTrack = trackManager.getSelectedTrack();

    daw::ui::PanelContentType targetContent = daw::ui::PanelContentType::Empty;
    bool needsTabs = false;

    if (selectedClip != INVALID_CLIP_ID) {
        const auto* clip = clipManager.getClip(selectedClip);
        if (clip) {
            if (clip->type == ClipType::MIDI) {
                needsTabs = true;

                // Auto-default to Drum Grid for DrumGrid tracks (on first selection)
                if (selectedClip != lastEditorClipId_) {
                    lastEditorClipId_ = selectedClip;
                    if (trackHasDrumGrid(clip->trackId))
                        lastEditorTabChoice_ = 1;  // Drum Grid
                    else
                        lastEditorTabChoice_ = 0;  // Piano Roll
                }

                targetContent = (lastEditorTabChoice_ == 1)
                                    ? daw::ui::PanelContentType::DrumGridClipView
                                    : daw::ui::PanelContentType::PianoRoll;
            } else if (clip->type == ClipType::Audio) {
                targetContent = daw::ui::PanelContentType::WaveformEditor;
            }
        }
    } else if (selectedTrack != INVALID_TRACK_ID) {
        targetContent = daw::ui::PanelContentType::TrackChain;
    }

    // Update tab icons and controls visibility
    showEditorTabs_ = needsTabs;

    // Update tab icon active states
    if (showEditorTabs_) {
        updatingTabs_ = true;
        pianoRollTab_->setActive(lastEditorTabChoice_ == 0);
        drumGridTab_->setActive(lastEditorTabChoice_ == 1);
        updatingTabs_ = false;
    }

    // Show/hide tab icons
    pianoRollTab_->setVisible(showEditorTabs_);
    drumGridTab_->setVisible(showEditorTabs_);

    // Show/hide header controls
    timeModeButton_->setVisible(showEditorTabs_);
    gridNumeratorLabel_->setVisible(showEditorTabs_);
    gridSlashLabel_->setVisible(showEditorTabs_);
    gridDenominatorLabel_->setVisible(showEditorTabs_);
    autoGridButton_->setVisible(showEditorTabs_);
    snapButton_->setVisible(showEditorTabs_);

    resized();

    // Switch to the appropriate content via PanelController
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetContent);

    // Apply time mode to new content and sync grid controls
    if (showEditorTabs_) {
        applyTimeModeToContent();
    }
    syncGridControlsFromContent();
}

juce::Rectangle<int> BottomPanel::getCollapseButtonBounds() {
    if (isCollapsed()) {
        return juce::Rectangle<int>(getWidth() / 2 - 10, 2, 20, 20);
    } else {
        // Collapse button on the right side of the header
        return juce::Rectangle<int>(getWidth() - 28, 4, 20, 20);
    }
}

juce::Rectangle<int> BottomPanel::getTabBarBounds() {
    // No tab bar for bottom panel - content is auto-switched based on selection
    return juce::Rectangle<int>();
}

juce::Rectangle<int> BottomPanel::getContentBounds() {
    auto bounds = getLocalBounds();
    if (showEditorTabs_) {
        bounds.removeFromTop(EDITOR_TAB_HEIGHT);
    }
    return bounds;
}

void BottomPanel::onEditorTabChanged(int tabIndex) {
    if (updatingTabs_)
        return;

    lastEditorTabChoice_ = tabIndex;

    // Update icon active states
    pianoRollTab_->setActive(tabIndex == 0);
    drumGridTab_->setActive(tabIndex == 1);

    auto targetType = (tabIndex == 1) ? daw::ui::PanelContentType::DrumGridClipView
                                      : daw::ui::PanelContentType::PianoRoll;
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetType);

    // Apply time mode to the newly active content and sync grid controls
    applyTimeModeToContent();
    syncGridControlsFromContent();
}

void BottomPanel::applyTimeModeToContent() {
    auto* content = getActiveContent();
    if (!content)
        return;

    if (auto* pianoRoll = dynamic_cast<daw::ui::PianoRollContent*>(content)) {
        pianoRoll->setRelativeTimeMode(relativeTimeMode_);
    } else if (auto* drumGrid = dynamic_cast<daw::ui::DrumGridClipContent*>(content)) {
        drumGrid->setRelativeTimeMode(relativeTimeMode_);
    }
}

void BottomPanel::syncGridControlsFromContent() {
    auto* content = getActiveContent();
    auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
    if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
        // Read grid state from the clip
        const auto* clip = ClipManager::getInstance().getClip(midiEditor->getEditingClipId());
        if (clip) {
            isAutoGrid_ = clip->gridAutoGrid;
            gridNumerator_ = clip->gridNumerator;
            gridDenominator_ = clip->gridDenominator;
            isSnapEnabled_ = clip->gridSnapEnabled;
        }
    } else {
        // Read from arrangement (timeline state)
        syncGridStateFromTimeline();
    }

    // Update UI controls
    autoGridButton_->setToggleState(isAutoGrid_, juce::dontSendNotification);
    if (!gridNumeratorLabel_->isDragging())
        gridNumeratorLabel_->setValue(static_cast<double>(gridNumerator_),
                                      juce::dontSendNotification);
    if (!gridDenominatorLabel_->isDragging())
        gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                        juce::dontSendNotification);
    snapButton_->setToggleState(isSnapEnabled_, juce::dontSendNotification);

    gridNumeratorLabel_->setEnabled(!isAutoGrid_);
    gridDenominatorLabel_->setEnabled(!isAutoGrid_);
    gridNumeratorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    gridDenominatorLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
    gridSlashLabel_->setAlpha(isAutoGrid_ ? 0.6f : 1.0f);
}

void BottomPanel::syncGridStateFromTimeline() {
    if (auto* controller = TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        const auto& gq = state.display.gridQuantize;
        isAutoGrid_ = gq.autoGrid;
        gridNumerator_ = gq.numerator;
        gridDenominator_ = gq.denominator;
        isSnapEnabled_ = state.display.snapEnabled;
    }
}

}  // namespace magda
