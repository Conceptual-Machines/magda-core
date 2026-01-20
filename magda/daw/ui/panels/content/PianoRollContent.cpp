#include "PianoRollContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/pianoroll/PianoRollGridComponent.hpp"
#include "ui/components/pianoroll/PianoRollKeyboard.hpp"
#include "ui/components/timeline/TimeRuler.hpp"

namespace magda::daw::ui {

// Custom LookAndFeel for buttons that use Inter font with minimal styling
class ButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    juce::Font getTextButtonFont(juce::TextButton&, int /*buttonHeight*/) override {
        return magda::FontManager::getInstance().getButtonFont(11.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& /*button*/, const juce::Colour&,
                              bool /*isMouseOverButton*/, bool /*isButtonDown*/) override {
        // Only draw top border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawLine(0.0f, 0.0f, static_cast<float>(g.getClipBounds().getWidth()), 0.0f, 1.0f);
    }
};

// Custom viewport that notifies on scroll with real-time tracking
class ScrollNotifyingViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;
    juce::Component* timeRulerToRepaint = nullptr;
    juce::Component* keyboardToUpdate = nullptr;
    juce::Component* parentToRepaint = nullptr;  // For chord row repaint

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        if (onScrolled) {
            onScrolled(getViewPositionX(), getViewPositionY());
        }
        // Force immediate repaint during scroll
        if (timeRulerToRepaint)
            timeRulerToRepaint->repaint();
        if (keyboardToUpdate)
            keyboardToUpdate->repaint();
        if (parentToRepaint)
            parentToRepaint->repaint();
    }

    // Override scrollBarMoved for real-time updates during scrollbar drag
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override {
        juce::Viewport::scrollBarMoved(scrollBar, newRangeStart);
        // Force immediate repaint during scrollbar drag
        if (timeRulerToRepaint)
            timeRulerToRepaint->repaint();
        if (keyboardToUpdate)
            keyboardToUpdate->repaint();
        if (parentToRepaint)
            parentToRepaint->repaint();
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

    // Create custom LookAndFeel for buttons
    buttonLookAndFeel_ = std::make_unique<ButtonLookAndFeel>();

    // Create time mode toggle button
    timeModeButton_ = std::make_unique<juce::TextButton>("REL");
    timeModeButton_->setTooltip("Toggle between Relative (clip) and Absolute (project) time");
    timeModeButton_->setClickingTogglesState(true);
    timeModeButton_->setToggleState(relativeTimeMode_, juce::dontSendNotification);
    timeModeButton_->setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    timeModeButton_->setLookAndFeel(buttonLookAndFeel_.get());
    timeModeButton_->onClick = [this]() { setRelativeTimeMode(timeModeButton_->getToggleState()); };
    addAndMakeVisible(timeModeButton_.get());

    // Create keyboard component
    keyboard_ = std::make_unique<magda::PianoRollKeyboard>();
    keyboard_->setNoteHeight(noteHeight_);
    keyboard_->setNoteRange(MIN_NOTE, MAX_NOTE);

    // Set up vertical zoom callback from keyboard (drag up/down to zoom)
    keyboard_->onZoomChanged = [this](int newHeight, int anchorNote, int anchorScreenY) {
        if (newHeight != noteHeight_) {
            noteHeight_ = newHeight;

            // Update components
            gridComponent_->setNoteHeight(noteHeight_);
            keyboard_->setNoteHeight(noteHeight_);
            updateGridSize();

            // Adjust scroll to keep anchor note under mouse
            int newAnchorY = (MAX_NOTE - anchorNote) * noteHeight_;
            int newScrollY = newAnchorY - anchorScreenY;
            newScrollY = juce::jmax(0, newScrollY);
            viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
        }
    };

    // Set up vertical scroll callback from keyboard (drag left/right to scroll)
    keyboard_->onScrollRequested = [this](int deltaY) {
        int newScrollY = viewport_->getViewPositionY() + deltaY;
        newScrollY = juce::jmax(0, newScrollY);
        viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
    };

    addAndMakeVisible(keyboard_.get());

    // Create viewport for scrolling (custom viewport that notifies on scroll)
    auto scrollViewport = std::make_unique<ScrollNotifyingViewport>();
    scrollViewport->onScrolled = [this](int x, int y) {
        keyboard_->setScrollOffset(y);
        timeRuler_->setScrollOffset(x);
    };
    scrollViewport->timeRulerToRepaint = timeRuler_.get();
    scrollViewport->keyboardToUpdate = keyboard_.get();
    scrollViewport->parentToRepaint = this;  // For chord row repaint
    scrollViewport->setScrollBarsShown(true, true);
    viewport_ = std::move(scrollViewport);
    addAndMakeVisible(viewport_.get());

    // Create the grid component
    gridComponent_ = std::make_unique<magda::PianoRollGridComponent>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setNoteHeight(noteHeight_);
    gridComponent_->setLeftPadding(GRID_LEFT_PADDING);
    viewport_->setViewedComponent(gridComponent_.get(), false);

    // Link TimeRuler to viewport for real-time scroll sync
    timeRuler_->setLinkedViewport(viewport_.get());

    // Set up zoom callback from time ruler (drag up/down to zoom)
    timeRuler_->onZoomChanged = [this](double newZoom, double anchorTime, int anchorScreenX) {
        // Convert pixels-per-second to pixels-per-beat
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double newPixelsPerBeat = newZoom * secondsPerBeat;

        // Clamp to our limits
        newPixelsPerBeat = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newPixelsPerBeat);

        if (newPixelsPerBeat != horizontalZoom_) {
            // Calculate anchor beat position
            double anchorBeat = anchorTime / secondsPerBeat;

            horizontalZoom_ = newPixelsPerBeat;

            // Update components
            gridComponent_->setPixelsPerBeat(horizontalZoom_);
            updateGridSize();
            updateTimeRuler();

            // Adjust scroll to keep anchor position under mouse
            int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
            int newScrollX = newAnchorX - (anchorScreenX - KEYBOARD_WIDTH);
            newScrollX = juce::jmax(0, newScrollX);
            viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
        }
    };

    // Set up horizontal scroll callback from time ruler (drag left/right to scroll)
    timeRuler_->onScrollRequested = [this](int deltaX) {
        int newScrollX = viewport_->getViewPositionX() + deltaX;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
    };

    setupGridCallbacks();

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

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
    timeModeButton_->setLookAndFeel(nullptr);
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

    // Draw chord row at the top (if visible)
    if (showChordRow_) {
        auto chordArea = getLocalBounds().removeFromTop(CHORD_ROW_HEIGHT);
        chordArea.removeFromLeft(KEYBOARD_WIDTH);  // Skip the button area
        drawChordRow(g, chordArea);
    }
}

void PianoRollContent::resized() {
    auto bounds = getLocalBounds();

    // Skip chord row space if visible (drawn in paint)
    if (showChordRow_) {
        bounds.removeFromTop(CHORD_ROW_HEIGHT);
    }

    // Ruler row with time mode button
    auto headerArea = bounds.removeFromTop(RULER_HEIGHT);
    auto timeModeArea = headerArea.removeFromLeft(KEYBOARD_WIDTH);
    timeModeButton_->setBounds(timeModeArea.reduced(4, 2));
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

void PianoRollContent::mouseWheelMove(const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& wheel) {
    int headerHeight = getHeaderHeight();

    // Check if mouse is over the chord row area (very top, only when visible)
    if (showChordRow_ && e.y < CHORD_ROW_HEIGHT && e.x >= KEYBOARD_WIDTH) {
        // Forward horizontal scrolling in chord row area
        if (timeRuler_->onScrollRequested) {
            float delta = (wheel.deltaX != 0.0f) ? wheel.deltaX : wheel.deltaY;
            int scrollAmount = static_cast<int>(-delta * 100.0f);
            if (scrollAmount != 0) {
                timeRuler_->onScrollRequested(scrollAmount);
            }
        }
        return;
    }

    // Check if mouse is over the time ruler area
    int rulerTop = showChordRow_ ? CHORD_ROW_HEIGHT : 0;
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= KEYBOARD_WIDTH) {
        // Forward to time ruler for horizontal scrolling
        if (timeRuler_->onScrollRequested) {
            float delta = (wheel.deltaX != 0.0f) ? wheel.deltaX : wheel.deltaY;
            int scrollAmount = static_cast<int>(-delta * 100.0f);
            if (scrollAmount != 0) {
                timeRuler_->onScrollRequested(scrollAmount);
            }
        }
        return;
    }

    // Check if mouse is over the keyboard area (left side, below header)
    if (e.x < KEYBOARD_WIDTH && e.y >= headerHeight) {
        // Forward to keyboard for vertical scrolling
        if (keyboard_->onScrollRequested) {
            int scrollAmount = static_cast<int>(-wheel.deltaY * 100.0f);
            if (scrollAmount != 0) {
                keyboard_->onScrollRequested(scrollAmount);
            }
        }
        return;
    }

    // Cmd/Ctrl + scroll = horizontal zoom
    if (e.mods.isCommandDown()) {
        // Calculate zoom change
        double zoomFactor = 1.0 + (wheel.deltaY * 0.1);

        // Calculate anchor point - where in the content the mouse is pointing
        int mouseXInContent = e.x - KEYBOARD_WIDTH + viewport_->getViewPositionX();
        double anchorBeat =
            static_cast<double>(mouseXInContent - GRID_LEFT_PADDING) / horizontalZoom_;

        // Apply zoom
        double newZoom = horizontalZoom_ * zoomFactor;
        newZoom = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            horizontalZoom_ = newZoom;

            // Update components
            gridComponent_->setPixelsPerBeat(horizontalZoom_);
            updateGridSize();
            updateTimeRuler();

            // Adjust scroll position to keep anchor point under mouse
            int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
            int newScrollX = newAnchorX - (e.x - KEYBOARD_WIDTH);
            newScrollX = juce::jmax(0, newScrollX);
            viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
        }
        return;
    }

    // Alt/Option + scroll = vertical zoom (note height)
    if (e.mods.isAltDown()) {
        // Calculate zoom change
        int heightDelta = wheel.deltaY > 0 ? 2 : -2;

        // Calculate anchor point - which note is under the mouse
        int mouseYInContent = e.y - headerHeight + viewport_->getViewPositionY();
        int anchorNote = MAX_NOTE - (mouseYInContent / noteHeight_);

        // Apply zoom
        int newHeight = noteHeight_ + heightDelta;
        newHeight = juce::jlimit(MIN_NOTE_HEIGHT, MAX_NOTE_HEIGHT, newHeight);

        if (newHeight != noteHeight_) {
            noteHeight_ = newHeight;

            // Update components
            gridComponent_->setNoteHeight(noteHeight_);
            keyboard_->setNoteHeight(noteHeight_);
            updateGridSize();

            // Adjust scroll position to keep anchor note under mouse
            int newAnchorY = (MAX_NOTE - anchorNote) * noteHeight_;
            int newScrollY = newAnchorY - (e.y - headerHeight);
            newScrollY = juce::jmax(0, newScrollY);
            viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
        }
        return;
    }

    // Regular scroll - don't handle, let default JUCE event propagation work
    // (The viewport will receive the event through normal component hierarchy)
}

void PianoRollContent::updateGridSize() {
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    // Get tempo to convert between seconds and beats
    double tempo = 120.0;
    double timelineLength = 300.0;  // Default 5 minutes
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timelineLength = state.timelineLength;
    }
    double secondsPerBeat = 60.0 / tempo;

    // Always use the full arrangement length for the grid
    double displayLengthBeats = timelineLength / secondsPerBeat;

    // Calculate clip position and length in beats
    double clipStartBeats = 0.0;
    double clipLengthBeats = 0.0;
    if (clip) {
        clipStartBeats = clip->startTime / secondsPerBeat;
        clipLengthBeats = clip->length / secondsPerBeat;
    }

    int gridWidth = juce::jmax(viewport_->getWidth(),
                               static_cast<int>(displayLengthBeats * horizontalZoom_) + 100);
    int gridHeight = (MAX_NOTE - MIN_NOTE + 1) * noteHeight_;

    gridComponent_->setSize(gridWidth, gridHeight);

    // Update grid's display mode and clip boundaries
    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
    gridComponent_->setTimelineLengthBeats(displayLengthBeats);
}

void PianoRollContent::updateTimeRuler() {
    if (!timeRuler_)
        return;

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    // Get tempo from TimelineController
    double tempo = 120.0;  // Default fallback
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);
    }
    timeRuler_->setTempo(tempo);

    // Calculate timing values
    double secondsPerBeat = 60.0 / tempo;

    // Get timeline length from controller
    double timelineLength = 300.0;  // Default 5 minutes
    if (auto* controller = magda::TimelineController::getCurrent()) {
        timelineLength = controller->getState().timelineLength;
    }

    // Set timeline length to full arrangement
    timeRuler_->setTimelineLength(timelineLength);

    // Set zoom (convert pixels per beat to pixels per second)
    double pixelsPerSecond = horizontalZoom_ / secondsPerBeat;
    timeRuler_->setZoom(pixelsPerSecond);

    // Set clip info for boundary drawing
    // timeOffset is always the clip's start time (used for boundary markers)
    // relativeMode controls whether bar numbers are offset
    if (clip) {
        timeRuler_->setTimeOffset(clip->startTime);
        timeRuler_->setClipLength(clip->length);
    } else {
        timeRuler_->setTimeOffset(0.0);
        timeRuler_->setClipLength(0.0);
    }

    // Update relative mode
    timeRuler_->setRelativeMode(relativeTimeMode_);
}

void PianoRollContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;
        timeModeButton_->setButtonText(relative ? "REL" : "ABS");
        timeModeButton_->setToggleState(relative, juce::dontSendNotification);
        updateGridSize();  // Grid size changes between modes
        updateTimeRuler();

        // In ABS mode, scroll to show bar 1 at the left
        // In REL mode, reset scroll to show the start of the clip
        viewport_->setViewPosition(0, viewport_->getViewPositionY());
    }
}

void PianoRollContent::setChordRowVisible(bool visible) {
    if (showChordRow_ != visible) {
        showChordRow_ = visible;
        resized();
        repaint();
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

            // Reset scroll to bar 1 when selecting a new clip
            viewport_->setViewPosition(0, viewport_->getViewPositionY());

            repaint();
        }
    }
}

void PianoRollContent::clipDragPreview(magda::ClipId clipId, double previewStartTime,
                                       double previewLength) {
    // Only update if this is the clip we're editing
    if (clipId != editingClipId_) {
        return;
    }

    // Update TimeRuler with preview position in real-time
    timeRuler_->setTimeOffset(previewStartTime);
    timeRuler_->setClipLength(previewLength);

    // Also update the grid with preview clip boundaries
    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        tempo = controller->getState().tempo.bpm;
    }
    double secondsPerBeat = 60.0 / tempo;
    double clipStartBeats = previewStartTime / secondsPerBeat;
    double clipLengthBeats = previewLength / secondsPerBeat;

    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
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

        // Reset scroll to bar 1 when setting a new clip
        viewport_->setViewPosition(0, viewport_->getViewPositionY());

        repaint();
    }
}

void PianoRollContent::drawChordRow(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw chord row background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw bottom border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(static_cast<float>(area.getX()), static_cast<float>(area.getBottom() - 1),
               static_cast<float>(area.getRight()), static_cast<float>(area.getBottom() - 1), 1.0f);

    // Get time signature for beat timing
    int timeSignatureNumerator = 4;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        timeSignatureNumerator = controller->getState().tempo.timeSignatureNumerator;
    }

    // Get scroll offset from viewport
    int scrollX = viewport_ ? viewport_->getViewPositionX() : 0;

    // Mock chords - one chord per 2 bars for demonstration
    const char* mockChords[] = {"C", "Am", "F", "G", "Dm", "Em", "Bdim", "C"};
    int numMockChords = 8;

    // Calculate beats per bar and pixels per beat
    double beatsPerBar = timeSignatureNumerator;
    double beatsPerChord = beatsPerBar * 2;  // 2 bars per chord

    g.setFont(11.0f);

    for (int i = 0; i < numMockChords; ++i) {
        double startBeat = i * beatsPerChord;
        double endBeat = (i + 1) * beatsPerChord;

        int startX = static_cast<int>(startBeat * horizontalZoom_) + GRID_LEFT_PADDING - scrollX;
        int endX = static_cast<int>(endBeat * horizontalZoom_) + GRID_LEFT_PADDING - scrollX;

        // Skip if out of view
        if (endX < 0 || startX > area.getWidth()) {
            continue;
        }

        // Clip to visible area
        int drawStartX = juce::jmax(0, startX) + area.getX();
        int drawEndX = juce::jmin(area.getWidth(), endX) + area.getX();

        // Draw chord block
        auto blockBounds = juce::Rectangle<int>(drawStartX + 1, area.getY() + 2,
                                                drawEndX - drawStartX - 2, area.getHeight() - 4);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
        g.fillRoundedRectangle(blockBounds.toFloat(), 3.0f);

        // Draw chord name (only if block is mostly visible)
        if (startX >= -20 && endX <= area.getWidth() + 20) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            g.drawText(mockChords[i], blockBounds, juce::Justification::centred, true);
        }
    }
}

}  // namespace magda::daw::ui
