#include "PianoRollContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/pianoroll/PianoRollGridComponent.hpp"
#include "ui/components/pianoroll/PianoRollKeyboard.hpp"
#include "ui/components/pianoroll/VelocityLaneComponent.hpp"
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

    // Create chord toggle button for sidebar
    chordToggle_ = std::make_unique<magda::SvgButton>("ChordToggle", BinaryData::Chords2_svg,
                                                      BinaryData::Chords2_svgSize);
    chordToggle_->setTooltip("Toggle chord detection row");
    chordToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));  // SVG fill color
    chordToggle_->setActive(showChordRow_);
    chordToggle_->onClick = [this]() {
        setChordRowVisible(!showChordRow_);
        chordToggle_->setActive(showChordRow_);
    };
    addAndMakeVisible(chordToggle_.get());

    // Create velocity toggle button for sidebar (using volume icon as placeholder)
    velocityToggle_ = std::make_unique<magda::SvgButton>(
        "VelocityToggle", BinaryData::volume_up_svg, BinaryData::volume_up_svgSize);
    velocityToggle_->setTooltip("Toggle velocity lane");
    velocityToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    velocityToggle_->setActive(velocityDrawerOpen_);
    velocityToggle_->onClick = [this]() {
        setVelocityDrawerVisible(!velocityDrawerOpen_);
        velocityToggle_->setActive(velocityDrawerOpen_);
    };
    addAndMakeVisible(velocityToggle_.get());

    // Create velocity lane component
    velocityLane_ = std::make_unique<magda::VelocityLaneComponent>();
    velocityLane_->setLeftPadding(GRID_LEFT_PADDING);
    velocityLane_->onVelocityChanged = [this](magda::ClipId clipId, size_t noteIndex,
                                              int newVelocity) {
        auto cmd =
            std::make_unique<magda::SetMidiNoteVelocityCommand>(clipId, noteIndex, newVelocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        velocityLane_->refreshNotes();
        gridComponent_->refreshNotes();
    };
    addChildComponent(velocityLane_.get());  // Start hidden

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

    // Set up note preview callback for keyboard click-to-play
    keyboard_->onNotePreview = [this](int noteNumber, int velocity, bool isNoteOn) {
        DBG("PianoRollContent: Note preview callback - Note="
            << noteNumber << ", Velocity=" << velocity << ", On=" << (isNoteOn ? "YES" : "NO"));

        // Get track ID from currently edited clip
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
            if (clip && clip->trackId != magda::INVALID_TRACK_ID) {
                DBG("PianoRollContent: Calling TrackManager::previewNote for track "
                    << clip->trackId);
                // Preview note through track's instruments
                magda::TrackManager::getInstance().previewNote(clip->trackId, noteNumber, velocity,
                                                               isNoteOn);
            } else {
                DBG("PianoRollContent: No valid clip or track ID");
            }
        } else {
            DBG("PianoRollContent: No clip being edited");
        }
    };

    addAndMakeVisible(keyboard_.get());

    // Create viewport for scrolling (custom viewport that notifies on scroll)
    auto scrollViewport = std::make_unique<ScrollNotifyingViewport>();
    scrollViewport->onScrolled = [this](int x, int y) {
        keyboard_->setScrollOffset(y);
        timeRuler_->setScrollOffset(x);
        if (velocityLane_) {
            velocityLane_->setScrollOffset(x);
        }
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

    // Register as TimelineController listener for playhead updates
    if (auto* controller = magda::TimelineController::getCurrent()) {
        controller->addListener(this);
    }

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

    // Unregister from TimelineController
    if (auto* controller = magda::TimelineController::getCurrent()) {
        controller->removeListener(this);
    }
}

void PianoRollContent::setupGridCallbacks() {
    // Handle note addition
    gridComponent_->onNoteAdded = [](magda::ClipId clipId, double beat, int noteNumber,
                                     int velocity) {
        double defaultLength = 1.0;
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(clipId, beat, noteNumber,
                                                               defaultLength, velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note movement
    gridComponent_->onNoteMoved = [](magda::ClipId clipId, size_t noteIndex, double newBeat,
                                     int newNoteNumber) {
        auto cmd =
            std::make_unique<magda::MoveMidiNoteCommand>(clipId, noteIndex, newBeat, newNoteNumber);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note resizing
    gridComponent_->onNoteResized = [](magda::ClipId clipId, size_t noteIndex, double newLength) {
        auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(clipId, noteIndex, newLength);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note deletion
    gridComponent_->onNoteDeleted = [](magda::ClipId clipId, size_t noteIndex) {
        auto cmd = std::make_unique<magda::DeleteMidiNoteCommand>(clipId, noteIndex);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note selection - update SelectionManager
    gridComponent_->onNoteSelected = [](magda::ClipId clipId, size_t noteIndex) {
        magda::SelectionManager::getInstance().selectNote(clipId, noteIndex);
    };

    // Forward note drag preview to velocity lane for position sync
    gridComponent_->onNoteDragging = [this](magda::ClipId /*clipId*/, size_t noteIndex,
                                            double previewBeat, bool isDragging) {
        if (velocityLane_) {
            velocityLane_->setNotePreviewPosition(noteIndex, previewBeat, isDragging);
        }
    };
}

void PianoRollContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw sidebar on the left
    auto sidebarArea = getLocalBounds().removeFromLeft(SIDEBAR_WIDTH);
    drawSidebar(g, sidebarArea);

    // Draw chord row at the top (if visible)
    if (showChordRow_) {
        auto chordArea = getLocalBounds();
        chordArea.removeFromLeft(SIDEBAR_WIDTH);  // Skip sidebar
        chordArea = chordArea.removeFromTop(CHORD_ROW_HEIGHT);
        chordArea.removeFromLeft(KEYBOARD_WIDTH);  // Skip the button area
        drawChordRow(g, chordArea);
    }

    // Draw velocity drawer header (if open)
    if (velocityDrawerOpen_) {
        auto drawerHeaderArea = getLocalBounds();
        drawerHeaderArea.removeFromLeft(SIDEBAR_WIDTH);  // Skip sidebar
        drawerHeaderArea =
            drawerHeaderArea.removeFromBottom(VELOCITY_LANE_HEIGHT + VELOCITY_HEADER_HEIGHT);
        drawerHeaderArea = drawerHeaderArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
        drawVelocityHeader(g, drawerHeaderArea);
    }
}

void PianoRollContent::resized() {
    auto bounds = getLocalBounds();

    // Skip sidebar (painted in paint())
    bounds.removeFromLeft(SIDEBAR_WIDTH);

    // Position sidebar icons at the top of the sidebar
    int iconSize = 24;
    int padding = (SIDEBAR_WIDTH - iconSize) / 2;
    chordToggle_->setBounds(padding, padding, iconSize, iconSize);
    velocityToggle_->setBounds(padding, padding + iconSize + 4, iconSize, iconSize);

    // Skip chord row space if visible (drawn in paint)
    if (showChordRow_) {
        bounds.removeFromTop(CHORD_ROW_HEIGHT);
    }

    // Velocity drawer at bottom (if open)
    if (velocityDrawerOpen_) {
        auto drawerArea = bounds.removeFromBottom(VELOCITY_LANE_HEIGHT + VELOCITY_HEADER_HEIGHT);
        // Header area (drawn in paint)
        drawerArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
        // Skip keyboard width for alignment
        drawerArea.removeFromLeft(KEYBOARD_WIDTH);
        velocityLane_->setBounds(drawerArea);
        velocityLane_->setVisible(true);
    } else {
        velocityLane_->setVisible(false);
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
    updateVelocityLane();

    // Center on middle C on first layout
    if (needsInitialCentering_ && viewport_->getHeight() > 0) {
        centerOnMiddleC();
        needsInitialCentering_ = false;
    }
}

void PianoRollContent::mouseWheelMove(const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& wheel) {
    int headerHeight = getHeaderHeight();
    int leftPanelWidth = SIDEBAR_WIDTH + KEYBOARD_WIDTH;

    // Check if mouse is over the chord row area (very top, only when visible)
    if (showChordRow_ && e.y < CHORD_ROW_HEIGHT && e.x >= leftPanelWidth) {
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
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= leftPanelWidth) {
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
    if (e.x >= SIDEBAR_WIDTH && e.x < leftPanelWidth && e.y >= headerHeight) {
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
        int mouseXInContent = e.x - leftPanelWidth + viewport_->getViewPositionX();
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
            int newScrollX = newAnchorX - (e.x - leftPanelWidth);
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
        if (clip->view == magda::ClipView::Session) {
            // Session clips: startTime is always 0, length is derived from loop length (beats)
            clipStartBeats = 0.0;
            clipLengthBeats = clip->internalLoopLength;
        } else {
            clipStartBeats = clip->startTime / secondsPerBeat;
            clipLengthBeats = clip->length / secondsPerBeat;
        }
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
        if (clip->view == magda::ClipView::Session) {
            // Session clips: no timeline offset, length from loop length in seconds
            timeRuler_->setTimeOffset(0.0);
            timeRuler_->setClipLength(clip->internalLoopLength * secondsPerBeat);
        } else {
            timeRuler_->setTimeOffset(clip->startTime);
            timeRuler_->setClipLength(clip->length);
        }
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
        updateVelocityLane();

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

void PianoRollContent::setVelocityDrawerVisible(bool visible) {
    if (velocityDrawerOpen_ != visible) {
        velocityDrawerOpen_ = visible;
        updateVelocityLane();
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

            // Session clips are locked to relative mode
            bool isSessionClip = (clip->view == magda::ClipView::Session);
            if (isSessionClip) {
                setRelativeTimeMode(true);
                timeModeButton_->setEnabled(false);
                timeModeButton_->setTooltip("Session clips always use relative time");
            } else {
                timeModeButton_->setEnabled(true);
                timeModeButton_->setTooltip(
                    "Toggle between Relative (clip) and Absolute (project) time");
            }

            updateGridSize();
            updateTimeRuler();
            updateVelocityLane();
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
            velocityLane_->setClip(magda::INVALID_CLIP_ID);
        }
    }
    gridComponent_->refreshNotes();
    updateTimeRuler();
    updateVelocityLane();
    repaint();
}

void PianoRollContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        // Defer UI refresh asynchronously to prevent deleting components during event handling
        // This is the core of the Observer pattern - data changes notify listeners,
        // and listeners schedule their own UI updates safely
        // Use SafePointer to avoid use-after-free if component is destroyed before callback runs
        juce::Component::SafePointer<PianoRollContent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, clipId]() {
            if (auto* self = safeThis.getComponent()) {
                // Verify clip is still being edited (could have changed during async delay)
                if (clipId == self->editingClipId_) {
                    self->gridComponent_->refreshNotes();
                    self->updateGridSize();
                    self->updateTimeRuler();
                    self->updateVelocityLane();
                    self->repaint();
                }
            }
        });
    }
}

void PianoRollContent::clipSelectionChanged(magda::ClipId clipId) {
    if (clipId != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = clipId;
            gridComponent_->setClip(clipId);

            // Session clips are locked to relative mode
            bool isSessionClip = (clip->view == magda::ClipView::Session);
            if (isSessionClip) {
                setRelativeTimeMode(true);
                timeModeButton_->setEnabled(false);
                timeModeButton_->setTooltip("Session clips always use relative time");
            } else {
                timeModeButton_->setEnabled(true);
                timeModeButton_->setTooltip(
                    "Toggle between Relative (clip) and Absolute (project) time");
            }

            updateGridSize();
            updateTimeRuler();
            updateVelocityLane();

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
// TimelineStateListener
// ============================================================================

void PianoRollContent::timelineStateChanged(const magda::TimelineState& state) {
    // General state changes (tempo, time signature, etc.)
    updateTimeRuler();
    updateGridSize();
    repaint();
}

void PianoRollContent::playheadStateChanged(const magda::TimelineState& state) {
    // Update grid component with current playback position
    if (gridComponent_) {
        gridComponent_->setPlayheadPosition(state.playhead.playbackPosition);
    }
    if (timeRuler_) {
        timeRuler_->setPlayheadPosition(state.playhead.playbackPosition);
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
        updateVelocityLane();

        // Reset scroll to bar 1 when setting a new clip
        viewport_->setViewPosition(0, viewport_->getViewPositionY());

        repaint();
    }
}

void PianoRollContent::drawSidebar(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw sidebar background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw right separator line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(area.getRight() - 1, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));
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

void PianoRollContent::drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw header background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));

    // Draw "Velocity" label in keyboard area
    auto labelArea = area.removeFromLeft(KEYBOARD_WIDTH);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    g.drawText("Velocity", labelArea.reduced(4, 0), juce::Justification::centredLeft, true);
}

void PianoRollContent::updateVelocityLane() {
    if (!velocityLane_)
        return;

    // Update clip reference
    velocityLane_->setClip(editingClipId_);

    // Update zoom and mode settings
    velocityLane_->setPixelsPerBeat(horizontalZoom_);
    velocityLane_->setRelativeMode(relativeTimeMode_);

    // Get clip start beats for absolute mode
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    if (clip) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double clipStartBeats = clip->startTime / secondsPerBeat;
        velocityLane_->setClipStartBeats(clipStartBeats);
    } else {
        velocityLane_->setClipStartBeats(0.0);
    }

    // Sync scroll offset
    if (viewport_) {
        velocityLane_->setScrollOffset(viewport_->getViewPositionX());
    }

    velocityLane_->refreshNotes();
}

void PianoRollContent::centerOnMiddleC() {
    if (!viewport_) {
        return;
    }

    // C4 (middle C) is MIDI note 60
    constexpr int MIDDLE_C = 60;

    // Calculate Y position of middle C
    int middleCY = (MAX_NOTE - MIDDLE_C) * noteHeight_;

    // Center it in the viewport
    int viewportHeight = viewport_->getHeight();
    int scrollY = middleCY - (viewportHeight / 2) + (noteHeight_ / 2);

    // Clamp to valid range
    scrollY = juce::jmax(0, scrollY);

    viewport_->setViewPosition(viewport_->getViewPositionX(), scrollY);

    // Update keyboard scroll to match
    keyboard_->setScrollOffset(scrollY);
}

}  // namespace magda::daw::ui
