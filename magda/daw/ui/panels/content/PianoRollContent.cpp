#include "PianoRollContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/pianoroll/PianoRollGridComponent.hpp"
#include "ui/components/pianoroll/PianoRollKeyboard.hpp"
#include "ui/components/timeline/TimeRuler.hpp"

namespace magda::daw::ui {

// Custom viewport that notifies on scroll
class ScrollNotifyingViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        if (onScrolled) {
            onScrolled(getViewPositionX(), getViewPositionY());
        }
    }
};

PianoRollContent::PianoRollContent() {
    setName("PianoRoll");

    // Create time ruler (small left padding for label visibility)
    timeRuler_ = std::make_unique<magda::TimeRuler>();
    timeRuler_->setDisplayMode(magda::TimeRuler::DisplayMode::BarsBeats);
    timeRuler_->setRelativeMode(relativeTimeMode_);
    timeRuler_->setLeftPadding(GRID_LEFT_PADDING);
    addAndMakeVisible(timeRuler_.get());

    // Create time mode toggle button
    timeModeButton_ = std::make_unique<juce::TextButton>("REL");
    timeModeButton_->setTooltip("Toggle between Relative (clip) and Absolute (project) time");
    timeModeButton_->setClickingTogglesState(true);
    timeModeButton_->setToggleState(relativeTimeMode_, juce::dontSendNotification);
    timeModeButton_->onClick = [this]() { setRelativeTimeMode(timeModeButton_->getToggleState()); };
    addAndMakeVisible(timeModeButton_.get());

    // Create keyboard component
    keyboard_ = std::make_unique<magda::PianoRollKeyboard>();
    keyboard_->setNoteHeight(NOTE_HEIGHT);
    keyboard_->setNoteRange(MIN_NOTE, MAX_NOTE);
    addAndMakeVisible(keyboard_.get());

    // Create viewport for scrolling (custom viewport that notifies on scroll)
    auto scrollViewport = std::make_unique<ScrollNotifyingViewport>();
    scrollViewport->onScrolled = [this](int x, int y) {
        keyboard_->setScrollOffset(y);
        timeRuler_->setScrollOffset(x);
    };
    scrollViewport->setScrollBarsShown(true, true);
    viewport_ = std::move(scrollViewport);
    addAndMakeVisible(viewport_.get());

    // Create the grid component
    gridComponent_ = std::make_unique<magda::PianoRollGridComponent>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setNoteHeight(NOTE_HEIGHT);
    gridComponent_->setLeftPadding(GRID_LEFT_PADDING);
    viewport_->setViewedComponent(gridComponent_.get(), false);

    setupGridCallbacks();

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

    // Sync tempo from engine
    syncTempoFromEngine();

    // Check if there's already a selected MIDI clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = selectedClip;
            gridComponent_->setClip(selectedClip);
            updateTimeRuler();
        }
    }
}

PianoRollContent::~PianoRollContent() {
    magda::ClipManager::getInstance().removeListener(this);
}

void PianoRollContent::setupGridCallbacks() {
    // Handle note addition
    gridComponent_->onNoteAdded = [this](magda::ClipId clipId, double beat, int noteNumber,
                                         int velocity) {
        double defaultLength = 1.0;
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(clipId, beat, noteNumber,
                                                               defaultLength, velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        gridComponent_->refreshNotes();
    };

    // Handle note movement
    gridComponent_->onNoteMoved = [this](magda::ClipId clipId, size_t noteIndex, double newBeat,
                                         int newNoteNumber) {
        auto cmd =
            std::make_unique<magda::MoveMidiNoteCommand>(clipId, noteIndex, newBeat, newNoteNumber);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        gridComponent_->refreshNotes();
    };

    // Handle note resizing
    gridComponent_->onNoteResized = [this](magda::ClipId clipId, size_t noteIndex,
                                           double newLength) {
        auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(clipId, noteIndex, newLength);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        gridComponent_->refreshNotes();
    };

    // Handle note deletion
    gridComponent_->onNoteDeleted = [this](magda::ClipId clipId, size_t noteIndex) {
        auto cmd = std::make_unique<magda::DeleteMidiNoteCommand>(clipId, noteIndex);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        gridComponent_->refreshNotes();
    };

    // Handle note selection
    gridComponent_->onNoteSelected = [](magda::ClipId /*clipId*/, size_t /*noteIndex*/) {
        // Currently just updates selection state in the grid component
    };
}

void PianoRollContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void PianoRollContent::resized() {
    auto bounds = getLocalBounds();

    // Header row: toggle button + time ruler
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);

    // Small toggle button in the keyboard area
    auto buttonArea = headerArea.removeFromLeft(KEYBOARD_WIDTH);
    timeModeButton_->setBounds(buttonArea.reduced(4, 2));

    // Time ruler fills the rest of the header
    timeRuler_->setBounds(headerArea);

    // Keyboard on the left
    auto keyboardArea = bounds.removeFromLeft(KEYBOARD_WIDTH);
    keyboard_->setBounds(keyboardArea);

    // Viewport fills the remaining space
    viewport_->setBounds(bounds);

    // Update the grid size
    updateGridSize();
    updateTimeRuler();
}

void PianoRollContent::updateGridSize() {
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    double lengthBeats = clip ? clip->length * 2.0 : 16.0;
    int gridWidth =
        juce::jmax(viewport_->getWidth(), static_cast<int>(lengthBeats * horizontalZoom_) + 100);
    int gridHeight = (MAX_NOTE - MIN_NOTE + 1) * NOTE_HEIGHT;

    gridComponent_->setSize(gridWidth, gridHeight);
}

void PianoRollContent::updateTimeRuler() {
    if (!timeRuler_)
        return;

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    // Get tempo from engine
    syncTempoFromEngine();

    // Calculate clip length in seconds
    double tempo =
        timeRuler_->getTimeOffset() > 0 ? 120.0 : 120.0;  // Will be set by syncTempoFromEngine
    double secondsPerBeat = 60.0 / tempo;
    double lengthBeats = clip ? clip->length * 2.0 : 16.0;
    double lengthSeconds = lengthBeats * secondsPerBeat;

    // Set timeline length
    timeRuler_->setTimelineLength(lengthSeconds);

    // Set zoom (convert pixels per beat to pixels per second)
    double pixelsPerSecond = horizontalZoom_ / secondsPerBeat;
    timeRuler_->setZoom(pixelsPerSecond);

    // Set time offset for absolute mode (where the clip starts in the project)
    if (clip) {
        timeRuler_->setTimeOffset(clip->startTime);
    } else {
        timeRuler_->setTimeOffset(0.0);
    }

    // Update relative mode
    timeRuler_->setRelativeMode(relativeTimeMode_);
}

void PianoRollContent::syncTempoFromEngine() {
    if (!timeRuler_)
        return;

    // Get tempo from TimelineController
    double tempo = 120.0;  // Default fallback
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);
    }
    timeRuler_->setTempo(tempo);
}

void PianoRollContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;
        timeModeButton_->setButtonText(relative ? "REL" : "ABS");
        timeModeButton_->setToggleState(relative, juce::dontSendNotification);
        updateTimeRuler();
    }
}

void PianoRollContent::onActivated() {
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = selectedClip;
            gridComponent_->setClip(selectedClip);
            updateGridSize();
            updateTimeRuler();
        }
    }
    syncTempoFromEngine();
    repaint();
}

void PianoRollContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void PianoRollContent::clipsChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            editingClipId_ = magda::INVALID_CLIP_ID;
            gridComponent_->setClip(magda::INVALID_CLIP_ID);
        }
    }
    gridComponent_->refreshNotes();
    updateTimeRuler();
    repaint();
}

void PianoRollContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        gridComponent_->refreshNotes();
        updateGridSize();
        updateTimeRuler();
        repaint();
    }
}

void PianoRollContent::clipSelectionChanged(magda::ClipId clipId) {
    if (clipId != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = clipId;
            gridComponent_->setClip(clipId);
            updateGridSize();
            updateTimeRuler();
            repaint();
        }
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void PianoRollContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        gridComponent_->setClip(clipId);
        updateGridSize();
        updateTimeRuler();
        repaint();
    }
}

}  // namespace magda::daw::ui
