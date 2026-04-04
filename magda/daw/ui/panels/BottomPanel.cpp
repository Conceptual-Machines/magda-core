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
#include "audio/MidiChordEnginePlugin.hpp"
#include "content/AudioClipPropertiesContent.hpp"
#include "content/ChordPanelContent.hpp"
#include "content/DrumGridClipContent.hpp"
#include "content/MidiEditorContent.hpp"
#include "content/PianoRollContent.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/UndoManager.hpp"
#include "state/PanelController.hpp"
#include "ui/components/common/TimeBendPopup.hpp"

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
/** Return the first MidiChordEnginePlugin on a track, or nullptr. */
daw::audio::MidiChordEnginePlugin* findChordEngine(TrackId trackId) {
    auto* audioEngine = TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return nullptr;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return nullptr;
    auto* teTrack = bridge->getAudioTrack(trackId);
    if (!teTrack)
        return nullptr;

    for (auto* plugin : teTrack->pluginList) {
        if (auto* ce = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin))
            return ce;
        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    if (auto* ce = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(innerPlugin))
                        return ce;
                }
            }
        }
    }
    return nullptr;
}

}  // namespace

// Resize handle between waveform editor and properties panel
class BottomPanel::PropsResizeHandle : public juce::Component {
  public:
    PropsResizeHandle() {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::RESIZE_HANDLE));
        g.fillAll();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        startDragX_ = event.x;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (onResize)
            onResize(event.x - startDragX_);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (onDoubleClick)
            onDoubleClick();
    }

    std::function<void(int)> onResize;
    std::function<void()> onDoubleClick;

  private:
    int startDragX_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PropsResizeHandle)
};

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

    // Create audio clip properties side panel (hidden by default)
    audioPropsPanel_ = std::make_unique<daw::ui::AudioClipPropertiesContent>();
    addChildComponent(audioPropsPanel_.get());

    // Properties panel collapse button
    propsCollapseButton_ = std::make_unique<SvgButton>("PropsCollapse", BinaryData::right_close_svg,
                                                       BinaryData::right_close_svgSize);
    propsCollapseButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    propsCollapseButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    propsCollapseButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    propsCollapseButton_->setTooltip("Toggle properties panel");
    propsCollapseButton_->onClick = [this]() {
        propsPanelCollapsed_ = !propsPanelCollapsed_;
        propsCollapseButton_->updateSvgData(propsPanelCollapsed_ ? BinaryData::right_open_svg
                                                                 : BinaryData::right_close_svg,
                                            propsPanelCollapsed_ ? BinaryData::right_open_svgSize
                                                                 : BinaryData::right_close_svgSize);
        resized();
    };
    addChildComponent(propsCollapseButton_.get());

    // Resize handle between waveform and properties panel
    propsResizer_ = std::make_unique<PropsResizeHandle>();
    propsResizer_->onResize = [this](int delta) {
        int newWidth = propsPanelWidth_ - delta;
        if (newWidth < PROPS_COLLAPSE_THRESHOLD) {
            propsPanelCollapsed_ = true;
        } else {
            propsPanelCollapsed_ = false;
            propsPanelWidth_ = juce::jlimit(PROPS_MIN_WIDTH, PROPS_MAX_WIDTH, newWidth);
        }
        resized();
    };
    propsResizer_->onDoubleClick = [this]() {
        propsPanelCollapsed_ = !propsPanelCollapsed_;
        resized();
    };
    addChildComponent(propsResizer_.get());

    // Chord panel created lazily in ensureChordPanelCreated() —
    // creating it here during construction triggers AppKit notification
    // crashes because JUCE geometry observers fire before the window is ready.

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
    propsResizer_.reset();
    audioPropsPanel_.reset();
    chordResizer_.reset();
    chordPanel_.reset();
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
    gridDenominatorLabel_->setRange(2.0, 32.0, 4.0);
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
        // Constrain to nearest allowed value (multiples of 2 and 3)
        static constexpr int allowed[] = {2, 3, 4, 6, 8, 12, 16, 24, 32};
        static constexpr int numAllowed = 9;
        int raw = static_cast<int>(std::round(gridDenominatorLabel_->getValue()));
        int best = allowed[0];
        int bestDist = std::abs(raw - best);
        for (int i = 1; i < numAllowed; ++i) {
            int dist = std::abs(raw - allowed[i]);
            if (dist < bestDist) {
                bestDist = dist;
                best = allowed[i];
            }
        }
        gridDenominator_ = best;
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

    // Time bend button (dual icon: off=grey, on=blue when notes selected)
    bendButton_ = std::make_unique<SvgButton>(
        "TimeBend", BinaryData::time_bend_off_svg, BinaryData::time_bend_off_svgSize,
        BinaryData::time_bend_on_svg, BinaryData::time_bend_on_svgSize);
    bendButton_->setTooltip("Time Bend selected notes");
    bendButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    bendButton_->setBorderThickness(1.0f);
    bendButton_->setCornerRadius(3.0f);
    bendButton_->onClick = [this]() {
        const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
        if (!noteSel.isValid() || noteSel.noteIndices.size() < 2)
            return;
        auto clipId = noteSel.clipId;
        auto indices = noteSel.noteIndices;
        auto popup = std::make_unique<daw::ui::TimeBendPopup>(clipId, indices);
        popup->onApply = [clipId, indices](float depth, float skew, int cycles, float quantize,
                                           int quantizeSub, bool hardAngle) {
            auto cmd = std::make_unique<BendNoteTimingCommand>(clipId, indices, depth, skew, cycles,
                                                               quantize, quantizeSub, hardAngle);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        };
        daw::ui::TimeBendPopup::showAbove(std::move(popup), bendButton_.get());
    };
    addChildComponent(bendButton_.get());
}

void BottomPanel::setCollapsed(bool collapsed) {
    daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Bottom, collapsed);
}

void BottomPanel::paint(juce::Graphics& g) {
    TabbedPanel::paint(g);

    // Draw tint overlay for plugin drag-and-drop
    if (showPluginDropOverlay_) {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(getLocalBounds());
    }

    bool hasHeader = showEditorTabs_ || showPropsPanel_ || showChordPanel_;

    // Draw bottom border below the header and column dividers
    if (hasHeader) {
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(0, EDITOR_TAB_HEIGHT - 1, getWidth(), 1);

        // Sidebar column divider (for MIDI editor tab icons)
        if (showEditorTabs_) {
            g.fillRect(SIDEBAR_WIDTH, 0, 1, EDITOR_TAB_HEIGHT - 1);

            // Update bend button active state based on note selection
            const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
            bool hasNotes = noteSel.isValid() && noteSel.noteIndices.size() >= 2;
            bendButton_->setActive(hasNotes);
        }
    }

    // Vertical border on the left of the collapsed side panel strip
    auto drawCollapsedBorder = [&](bool show, bool collapsed) {
        if (show && collapsed) {
            g.setColour(DarkTheme::getBorderColour());
            int stripX = getWidth() - 28;
            int top = hasHeader ? EDITOR_TAB_HEIGHT : 0;
            g.fillRect(stripX, top, 1, getHeight() - top);
        }
    };
    drawCollapsedBorder(showPropsPanel_, propsPanelCollapsed_);
    drawCollapsedBorder(showChordPanel_, chordPanelCollapsed_);
}

void BottomPanel::resized() {
    // Hide all header controls when collapsed
    if (isCollapsed()) {
        pianoRollTab_->setVisible(false);
        drumGridTab_->setVisible(false);
        audioPropsPanel_->setVisible(false);
        propsResizer_->setVisible(false);
        propsCollapseButton_->setVisible(false);
        if (chordPanel_)
            chordPanel_->setVisible(false);
        if (chordResizer_)
            chordResizer_->setVisible(false);
        if (chordCollapseButton_)
            chordCollapseButton_->setVisible(false);
        timeModeButton_->setVisible(false);
        gridNumeratorLabel_->setVisible(false);
        gridSlashLabel_->setVisible(false);
        gridDenominatorLabel_->setVisible(false);
        autoGridButton_->setVisible(false);
        snapButton_->setVisible(false);
        TabbedPanel::resized();
        return;
    }

    bool hasHeader = showEditorTabs_ || showPropsPanel_ || showChordPanel_;

    if (hasHeader) {
        auto headerBounds = getLocalBounds().removeFromTop(EDITOR_TAB_HEIGHT);

        // Ensure header controls are visible after un-collapse
        timeModeButton_->setVisible(true);
        gridNumeratorLabel_->setVisible(true);
        gridSlashLabel_->setVisible(true);
        gridDenominatorLabel_->setVisible(true);
        autoGridButton_->setVisible(true);
        snapButton_->setVisible(true);

        // Grid controls on the right (shared by MIDI and audio editors)
        auto controlsArea = headerBounds;
        controlsArea.removeFromRight(30);

        int x = controlsArea.getRight();
        int y = controlsArea.getY();
        int h = controlsArea.getHeight();
        int vPad = 4;

        x -= 36;
        snapButton_->setBounds(x, y + vPad, 36, h - vPad * 2);
        x -= 4;
        x -= 36;
        autoGridButton_->setBounds(x, y + vPad, 36, h - vPad * 2);
        x -= 4;
        x -= 24;
        gridDenominatorLabel_->setBounds(x, y + vPad, 24, h - vPad * 2);
        x -= 8;
        gridSlashLabel_->setBounds(x, y, 8, h);
        x -= 24;
        gridNumeratorLabel_->setBounds(x, y + vPad, 24, h - vPad * 2);
        x -= 4;
        x -= 36;
        timeModeButton_->setBounds(x, y + vPad, 36, h - vPad * 2);

        if (showEditorTabs_) {
            // MIDI editor: tab icons on left
            int iconSize = h - 8;
            int tabY = y + (h - iconSize) / 2;
            int tabX = headerBounds.getX() + SIDEBAR_WIDTH + 4;
            pianoRollTab_->setBounds(tabX, tabY, iconSize, iconSize);
            tabX += iconSize + 4;
            drumGridTab_->setBounds(tabX, tabY, iconSize, iconSize);

            // Time bend button centered horizontally in header
            bendButton_->setBounds((headerBounds.getCentreX() - iconSize / 2), tabY, iconSize,
                                   iconSize);
            bendButton_->setVisible(true);
        } else {
            bendButton_->setVisible(false);
        }
    }

    // TabbedPanel::resized() uses getContentBounds() which accounts for the header and side panel
    TabbedPanel::resized();

    // Position resize handle and properties side panel
    if (showPropsPanel_ && !propsPanelCollapsed_) {
        auto fullContent = getLocalBounds();
        if (hasHeader)
            fullContent.removeFromTop(EDITOR_TAB_HEIGHT);

        // Resize handle
        int resizerX = fullContent.getRight() - propsPanelWidth_ - RESIZE_HANDLE_SIZE;
        propsResizer_->setBounds(resizerX, fullContent.getY(), RESIZE_HANDLE_SIZE,
                                 fullContent.getHeight());
        propsResizer_->setVisible(true);

        // Properties panel
        auto propsArea = fullContent.removeFromRight(propsPanelWidth_);
        audioPropsPanel_->setBounds(propsArea);
        audioPropsPanel_->setVisible(true);

        // Collapse button at bottom-left of properties panel
        constexpr int collapseBtnSize = 20;
        constexpr int collapsePad = 4;
        propsCollapseButton_->setBounds(propsArea.getX() + collapsePad,
                                        propsArea.getBottom() - collapseBtnSize - collapsePad,
                                        collapseBtnSize, collapseBtnSize);
        propsCollapseButton_->setVisible(true);
        propsCollapseButton_->toFront(false);
    } else if (showPropsPanel_ && propsPanelCollapsed_) {
        // Collapsed: hide panel and resizer, but show button in a small strip
        audioPropsPanel_->setVisible(false);
        propsResizer_->setVisible(false);

        constexpr int collapseBtnSize = 20;
        constexpr int stripWidth = 28;
        auto fullContent = getLocalBounds();
        if (hasHeader)
            fullContent.removeFromTop(EDITOR_TAB_HEIGHT);
        propsCollapseButton_->setBounds(fullContent.getRight() - stripWidth + 4,
                                        fullContent.getBottom() - collapseBtnSize - 4,
                                        collapseBtnSize, collapseBtnSize);
        propsCollapseButton_->setVisible(true);
    } else {
        audioPropsPanel_->setVisible(false);
        propsResizer_->setVisible(false);
        propsCollapseButton_->setVisible(false);
    }

    // Position chord analysis side panel (same pattern as props panel)
    if (chordPanel_) {
        if (showChordPanel_ && !chordPanelCollapsed_) {
            auto fullContent = getLocalBounds();
            if (hasHeader)
                fullContent.removeFromTop(EDITOR_TAB_HEIGHT);

            int resizerX = fullContent.getRight() - chordPanelWidth_ - RESIZE_HANDLE_SIZE;
            chordResizer_->setBounds(resizerX, fullContent.getY(), RESIZE_HANDLE_SIZE,
                                     fullContent.getHeight());
            chordResizer_->setVisible(true);

            auto chordArea = fullContent.removeFromRight(chordPanelWidth_);
            chordPanel_->setBounds(chordArea);
            chordPanel_->setVisible(true);

            constexpr int collapseBtnSize = 20;
            constexpr int collapsePad = 4;
            chordCollapseButton_->setBounds(chordArea.getX() + collapsePad,
                                            chordArea.getBottom() - collapseBtnSize - collapsePad,
                                            collapseBtnSize, collapseBtnSize);
            chordCollapseButton_->setVisible(true);
            chordCollapseButton_->toFront(false);
        } else if (showChordPanel_ && chordPanelCollapsed_) {
            chordPanel_->setVisible(false);
            chordResizer_->setVisible(false);

            constexpr int collapseBtnSize = 20;
            constexpr int stripWidth = 28;
            auto fullContent = getLocalBounds();
            if (hasHeader)
                fullContent.removeFromTop(EDITOR_TAB_HEIGHT);
            chordCollapseButton_->setBounds(fullContent.getRight() - stripWidth + 4,
                                            fullContent.getBottom() - collapseBtnSize - 4,
                                            collapseBtnSize, collapseBtnSize);
            chordCollapseButton_->setVisible(true);
        } else {
            chordPanel_->setVisible(false);
            chordResizer_->setVisible(false);
            chordCollapseButton_->setVisible(false);
        }
    }
}

void BottomPanel::clipsChanged() {
    updateContentBasedOnSelection();
}

void BottomPanel::clipSelectionChanged(ClipId /*clipId*/) {
    updateContentBasedOnSelection();
    syncGridControlsFromContent();
}

void BottomPanel::clipPropertyChanged(ClipId clipId) {
    // Re-evaluate ABS/REL button state when clip properties change
    // (e.g. loop toggled on/off)
    auto* content = getActiveContent();
    auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
    if (midiEditor && midiEditor->getEditingClipId() == clipId) {
        applyTimeModeToContent();
    }
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

void BottomPanel::ensureChordPanelCreated() {
    if (chordPanel_)
        return;

    chordPanel_ = std::make_unique<daw::ui::ChordPanelContent>();
    addChildComponent(chordPanel_.get());

    chordCollapseButton_ = std::make_unique<SvgButton>("ChordCollapse", BinaryData::right_close_svg,
                                                       BinaryData::right_close_svgSize);
    chordCollapseButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    chordCollapseButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    chordCollapseButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    chordCollapseButton_->setTooltip("Toggle chord panel");
    chordCollapseButton_->onClick = [this]() {
        chordPanelCollapsed_ = !chordPanelCollapsed_;
        chordCollapseButton_->updateSvgData(chordPanelCollapsed_ ? BinaryData::right_open_svg
                                                                 : BinaryData::right_close_svg,
                                            chordPanelCollapsed_ ? BinaryData::right_open_svgSize
                                                                 : BinaryData::right_close_svgSize);
        resized();
    };
    addChildComponent(chordCollapseButton_.get());

    chordResizer_ = std::make_unique<PropsResizeHandle>();
    chordResizer_->onResize = [this](int delta) {
        int newWidth = chordPanelWidth_ - delta;
        if (newWidth < PROPS_COLLAPSE_THRESHOLD) {
            chordPanelCollapsed_ = true;
        } else {
            chordPanelCollapsed_ = false;
            chordPanelWidth_ = juce::jlimit(CHORD_MIN_WIDTH, CHORD_MAX_WIDTH, newWidth);
        }
        resized();
    };
    chordResizer_->onDoubleClick = [this]() {
        chordPanelCollapsed_ = !chordPanelCollapsed_;
        resized();
    };
    addChildComponent(chordResizer_.get());
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
        if (!clip) {
            // Clip data temporarily unavailable (e.g. during track rebuild).
            // Preserve current editor state to avoid transient tab flicker.
            return;
        }
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
    showPropsPanel_ = (targetContent == daw::ui::PanelContentType::WaveformEditor);

    // Show chord panel when piano roll is active and track has a chord engine
    {
        TrackId midiTrackId = INVALID_TRACK_ID;
        if (targetContent == daw::ui::PanelContentType::PianoRoll &&
            selectedClip != INVALID_CLIP_ID) {
            const auto* clip = clipManager.getClip(selectedClip);
            if (clip)
                midiTrackId = clip->trackId;
        }
        auto* ce = (midiTrackId != INVALID_TRACK_ID) ? findChordEngine(midiTrackId) : nullptr;

        showChordPanel_ = (ce != nullptr);
        if (ce) {
            ensureChordPanelCreated();
            chordPanel_->setChordEngine(ce, midiTrackId);
        } else if (chordPanel_) {
            chordPanel_->setChordEngine(nullptr);
        }
    }

    // Update MIDI tab icon active states
    if (showEditorTabs_) {
        updatingTabs_ = true;
        pianoRollTab_->setActive(lastEditorTabChoice_ == 0);
        drumGridTab_->setActive(lastEditorTabChoice_ == 1);
        updatingTabs_ = false;
    }

    // Show/hide tab icons
    pianoRollTab_->setVisible(showEditorTabs_);
    drumGridTab_->setVisible(showEditorTabs_);

    // Show/hide header controls (for MIDI and audio editors)
    bool showHeaderControls = showEditorTabs_ || showPropsPanel_ || showChordPanel_;
    timeModeButton_->setVisible(showHeaderControls);
    gridNumeratorLabel_->setVisible(showHeaderControls);
    gridSlashLabel_->setVisible(showHeaderControls);
    gridDenominatorLabel_->setVisible(showHeaderControls);
    autoGridButton_->setVisible(showHeaderControls);
    snapButton_->setVisible(showHeaderControls);

    // Activate/deactivate audio clip properties side panel
    if (showPropsPanel_) {
        audioPropsPanel_->onActivated();
    } else {
        audioPropsPanel_->onDeactivated();
    }

    resized();

    // Switch to the appropriate content via PanelController
    daw::ui::PanelController::getInstance().setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                               targetContent);

    // Apply time mode to new content and sync grid controls
    if (showEditorTabs_) {
        applyTimeModeToContent();
    }
    syncGridControlsFromContent();

    // Connect auto-grid display callback so num/den labels update during zoom
    auto* content = getActiveContent();
    if (auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content)) {
        midiEditor->onAutoGridDisplayChanged = [this](int numerator, int denominator) {
            gridNumerator_ = numerator;
            gridDenominator_ = denominator;
            if (!gridNumeratorLabel_->isDragging())
                gridNumeratorLabel_->setValue(static_cast<double>(numerator),
                                              juce::dontSendNotification);
            if (!gridDenominatorLabel_->isDragging())
                gridDenominatorLabel_->setValue(static_cast<double>(denominator),
                                                juce::dontSendNotification);
        };
    }
}

juce::Rectangle<int> BottomPanel::getTabBarBounds() {
    // No tab bar for bottom panel - content is auto-switched based on selection
    return juce::Rectangle<int>();
}

juce::Rectangle<int> BottomPanel::getContentBounds() {
    auto bounds = getLocalBounds();
    // Reserve header space
    if (showEditorTabs_ || showPropsPanel_ || showChordPanel_) {
        bounds.removeFromTop(EDITOR_TAB_HEIGHT);
    }
    // Reserve space for properties side panel + resize handle when expanded,
    // or a small strip for the collapse button when collapsed
    if (showPropsPanel_ && !propsPanelCollapsed_) {
        bounds.removeFromRight(propsPanelWidth_ + RESIZE_HANDLE_SIZE);
    } else if (showPropsPanel_ && propsPanelCollapsed_) {
        bounds.removeFromRight(28);
    }
    // Reserve space for chord analysis side panel
    if (showChordPanel_ && !chordPanelCollapsed_) {
        bounds.removeFromRight(chordPanelWidth_ + RESIZE_HANDLE_SIZE);
    } else if (showChordPanel_ && chordPanelCollapsed_) {
        bounds.removeFromRight(28);
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

    // Disable ABS/REL toggle when clip is in loop mode or session view
    // (these always force relative mode)
    auto* midiEditor = dynamic_cast<daw::ui::MidiEditorContent*>(content);
    if (midiEditor && midiEditor->getEditingClipId() != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(midiEditor->getEditingClipId());
        bool forceRelative = clip && (clip->view == ClipView::Session || clip->loopEnabled);
        timeModeButton_->setEnabled(!forceRelative);
        timeModeButton_->setAlpha(forceRelative ? 0.4f : 1.0f);
    } else {
        timeModeButton_->setEnabled(true);
        timeModeButton_->setAlpha(1.0f);
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

// =============================================================================
// Plugin Drag-and-Drop Implementation (DragAndDropTarget)
// =============================================================================

bool BottomPanel::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        return obj->getProperty("type").toString() == "plugin";
    }
    return false;
}

void BottomPanel::itemDragEnter(const SourceDetails& /*details*/) {
    showPluginDropOverlay_ = true;
    repaint();
}

void BottomPanel::itemDragExit(const SourceDetails& /*details*/) {
    showPluginDropOverlay_ = false;
    repaint();
}

void BottomPanel::itemDropped(const SourceDetails& details) {
    showPluginDropOverlay_ = false;
    repaint();

    if (auto* obj = details.description.getDynamicObject()) {
        auto device = TrackManager::deviceInfoFromPluginObject(*obj);
        TrackType trackType = TrackType::Audio;
        juce::String pluginName = obj->getProperty("name").toString();
        auto cmd = std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
        UndoManager::getInstance().executeCommand(std::move(cmd));
        // trackSelectionChanged listener will trigger updateContentBasedOnSelection()
        // which shows TrackChainContent with the new plugin
    }
}

}  // namespace magda
