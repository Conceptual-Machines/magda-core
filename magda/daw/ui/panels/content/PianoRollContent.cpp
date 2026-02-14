#include "PianoRollContent.hpp"

#include "../../core/SelectionManager.hpp"
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

PianoRollContent::PianoRollContent() {
    setName("PianoRoll");

    // Create chord toggle button
    chordToggle_ = std::make_unique<magda::SvgButton>("ChordToggle", BinaryData::chord_svg,
                                                      BinaryData::chord_svgSize);
    chordToggle_->setTooltip("Toggle chord detection row");
    chordToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));  // SVG fill color
    chordToggle_->setActive(showChordRow_);
    chordToggle_->onClick = [this]() {
        setChordRowVisible(!showChordRow_);
        chordToggle_->setActive(showChordRow_);
    };
    addAndMakeVisible(chordToggle_.get());

    // Create velocity toggle button (bar chart icon for controls drawer)
    velocityToggle_ = std::make_unique<magda::SvgButton>(
        "VelocityToggle", BinaryData::bar_chart_svg, BinaryData::bar_chart_svgSize);
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

    // Add PianoRoll-specific components to viewport repaint list
    viewport_->componentsToRepaint.push_back(keyboard_.get());
    viewport_->componentsToRepaint.push_back(this);  // For chord row repaint

    // Create the grid component
    gridComponent_ = std::make_unique<magda::PianoRollGridComponent>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setNoteHeight(noteHeight_);
    gridComponent_->setLeftPadding(GRID_LEFT_PADDING);
    gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
    gridComponent_->setSnapEnabled(snapEnabled_);
    if (auto* controller = magda::TimelineController::getCurrent()) {
        gridComponent_->setTimeSignatureNumerator(
            controller->getState().tempo.timeSignatureNumerator);
    }
    viewport_->setViewedComponent(gridComponent_.get(), false);

    setupGridCallbacks();

    // Register as SelectionManager listener (PianoRoll-specific)
    magda::SelectionManager::getInstance().addListener(this);

    // If base found a selected clip, set it up on our grid
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        gridComponent_->setClip(editingClipId_);
        updateTimeRuler();
    }
}

PianoRollContent::~PianoRollContent() {
    magda::SelectionManager::getInstance().removeListener(this);
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
        // Get source clip and note
        auto* sourceClip = magda::ClipManager::getInstance().getClip(clipId);
        if (!sourceClip || noteIndex >= sourceClip->midiNotes.size())
            return;

        double oldBeat = sourceClip->midiNotes[noteIndex].startBeat;

        DBG("=== NOTE MOVE ===");
        DBG("  Clip " << clipId);
        DBG("  FROM content-beat " << oldBeat << " TO content-beat " << newBeat);
        DBG("  Note index: " << noteIndex);

        // Normal movement within same clip (only executed if no cross-clip transfer occurred)
        auto cmd =
            std::make_unique<magda::MoveMidiNoteCommand>(clipId, noteIndex, newBeat, newNoteNumber);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // After moving, check if note is still visible in this clip (considering offset)
        auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->type == magda::ClipType::MIDI && noteIndex < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[noteIndex];
            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double beatsPerSecond = tempo / 60.0;
            double clipLengthBeats = clip->length * beatsPerSecond;

            // Check if note is outside visible range [offset, offset + length]
            double effectiveOffset = (clip->view == magda::ClipView::Session || clip->loopEnabled)
                                         ? clip->midiOffset
                                         : 0.0;
            if (note.startBeat < effectiveOffset ||
                note.startBeat >= effectiveOffset + clipLengthBeats) {
                DBG("Note is no longer visible in clip "
                    << clipId << " (offset=" << clip->midiOffset << ", note at " << note.startBeat
                    << ")");

                // Find which clip would show this note
                // Note: startBeat is in content coordinates, so subtract offset to get timeline
                // position
                double clipStartBeats = clip->startTime * beatsPerSecond;
                double absoluteBeat = clipStartBeats + note.startBeat - effectiveOffset;
                double absoluteSeconds = absoluteBeat / beatsPerSecond;

                magda::ClipId destClipId = magda::ClipManager::getInstance().getClipAtPosition(
                    clip->trackId, absoluteSeconds);

                if (destClipId != magda::INVALID_CLIP_ID && destClipId != clipId) {
                    DBG("  -> Would be visible in clip " << destClipId << ", moving it there");
                    auto* destClip = magda::ClipManager::getInstance().getClip(destClipId);
                    if (destClip && destClip->type == magda::ClipType::MIDI) {
                        // Calculate position in destination clip's content coordinates
                        // absoluteBeat is timeline position, convert to content position
                        double destClipStartBeats = destClip->startTime * beatsPerSecond;
                        double destOffset =
                            (destClip->view == magda::ClipView::Session || destClip->loopEnabled)
                                ? destClip->midiOffset
                                : 0.0;
                        double relativeNewBeat = absoluteBeat - destClipStartBeats + destOffset;

                        DBG("  -> Transfer: absoluteBeat="
                            << absoluteBeat << ", destClipStart=" << destClipStartBeats
                            << ", destOffset=" << destClip->midiOffset
                            << ", contentBeat=" << relativeNewBeat);

                        // Move to destination clip
                        auto moveCmd = std::make_unique<magda::MoveMidiNoteBetweenClipsCommand>(
                            clipId, noteIndex, destClipId, relativeNewBeat, note.noteNumber);
                        magda::UndoManager::getInstance().executeCommand(std::move(moveCmd));
                    }
                }
            }
        }
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note copy (shift+drag)
    gridComponent_->onNoteCopied = [this](magda::ClipId clipId, size_t noteIndex, double destBeat,
                                          int destNoteNumber) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (!clip || noteIndex >= clip->midiNotes.size())
            return;

        const auto& sourceNote = clip->midiNotes[noteIndex];
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(
            clipId, destBeat, destNoteNumber, sourceNote.lengthBeats, sourceNote.velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // Select the newly copied note (appended at end) after the async refresh
        const auto* updatedClip = magda::ClipManager::getInstance().getClip(clipId);
        if (updatedClip && !updatedClip->midiNotes.empty()) {
            int newNoteIndex = static_cast<int>(updatedClip->midiNotes.size()) - 1;
            gridComponent_->selectNoteAfterRefresh(clipId, newNoteIndex);
        }
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
    gridComponent_->onNoteSelected = [](magda::ClipId clipId, size_t noteIndex,
                                        bool /*isAdditive*/) {
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

// ============================================================================
// MidiEditorContent virtual implementations
// ============================================================================

void PianoRollContent::setGridPixelsPerBeat(double ppb) {
    if (gridComponent_)
        gridComponent_->setPixelsPerBeat(ppb);
}

void PianoRollContent::setGridPlayheadPosition(double position) {
    if (gridComponent_)
        gridComponent_->setPlayheadPosition(position);
}

void PianoRollContent::onScrollPositionChanged(int scrollX, int scrollY) {
    keyboard_->setScrollOffset(scrollY);
    if (velocityLane_) {
        velocityLane_->setScrollOffset(scrollX);
    }
}

void PianoRollContent::onGridResolutionChanged() {
    if (gridComponent_) {
        gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
        gridComponent_->setSnapEnabled(snapEnabled_);

        // Sync time signature
        if (auto* controller = magda::TimelineController::getCurrent()) {
            gridComponent_->setTimeSignatureNumerator(
                controller->getState().tempo.timeSignatureNumerator);
        }
    }
}

// ============================================================================
// Paint / Layout
// ============================================================================

void PianoRollContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Draw sidebar on the left
    auto sidebarArea = getLocalBounds().removeFromLeft(SIDEBAR_WIDTH);
    drawSidebar(g, sidebarArea);

    // Draw chord row at the top (if visible)
    if (showChordRow_) {
        auto chordArea = getLocalBounds();
        chordArea.removeFromLeft(SIDEBAR_WIDTH);
        chordArea = chordArea.removeFromTop(CHORD_ROW_HEIGHT);
        chordArea.removeFromLeft(KEYBOARD_WIDTH);
        drawChordRow(g, chordArea);
    }

    // Draw velocity drawer header (if open)
    if (velocityDrawerOpen_) {
        auto drawerHeaderArea = getLocalBounds();
        drawerHeaderArea.removeFromLeft(SIDEBAR_WIDTH);
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

    // Position sidebar icons: chord at top, velocity at bottom
    int iconSize = 22;
    int padding = (SIDEBAR_WIDTH - iconSize) / 2;
    chordToggle_->setBounds(padding, padding, iconSize, iconSize);
    velocityToggle_->setBounds(padding, getHeight() - iconSize - padding, iconSize, iconSize);

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

    // Ruler row
    auto headerArea = bounds.removeFromTop(RULER_HEIGHT);
    headerArea.removeFromLeft(KEYBOARD_WIDTH);
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

// ============================================================================
// Mouse
// ============================================================================

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

    // Cmd/Ctrl + scroll = horizontal zoom (uses shared base method)
    if (e.mods.isCommandDown()) {
        double zoomFactor = 1.0 + (wheel.deltaY * 0.1);
        int mouseXInViewport = e.x - leftPanelWidth;
        performWheelZoom(zoomFactor, mouseXInViewport);
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

// ============================================================================
// Grid sizing (PianoRoll-specific)
// ============================================================================

void PianoRollContent::updateGridSize() {
    auto& clipManager = magda::ClipManager::getInstance();
    const auto* clip =
        editingClipId_ != magda::INVALID_CLIP_ID ? clipManager.getClip(editingClipId_) : nullptr;

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

    // When multiple clips are selected, compute the combined range
    const auto& selectedClipIds = gridComponent_->getSelectedClipIds();
    if (selectedClipIds.size() > 1) {
        double earliestStart = std::numeric_limits<double>::max();
        double latestEnd = 0.0;
        for (magda::ClipId id : selectedClipIds) {
            const auto* c = clipManager.getClip(id);
            if (!c)
                continue;
            earliestStart = juce::jmin(earliestStart, c->startTime);
            latestEnd = juce::jmax(latestEnd, c->startTime + c->length);
        }
        clipStartBeats = earliestStart / secondsPerBeat;
        clipLengthBeats = (latestEnd - earliestStart) / secondsPerBeat;
    } else if (clip) {
        if (clip->view == magda::ClipView::Session) {
            clipStartBeats = 0.0;
            clipLengthBeats = clip->length / secondsPerBeat;
        } else {
            clipStartBeats = clip->startTime / secondsPerBeat;
            clipLengthBeats = clip->length / secondsPerBeat;
        }
    }

    int gridWidth = juce::jmax(viewport_->getWidth(),
                               static_cast<int>(displayLengthBeats * horizontalZoom_) + 100);
    int gridHeight = (MAX_NOTE - MIN_NOTE + 1) * noteHeight_;

    gridComponent_->setSize(gridWidth, gridHeight);

    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
    gridComponent_->setTimelineLengthBeats(displayLengthBeats);

    // Pass loop region data to grid
    // Note: Grid expects beats, so convert from seconds
    if (clip && selectedClipIds.size() <= 1) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double beatsPerSecond = tempo / 60.0;
        double loopPhaseBeats = (clip->offset - clip->loopStart) * beatsPerSecond;
        double sourceLengthBeats = clip->loopLength * beatsPerSecond;
        gridComponent_->setLoopRegion(loopPhaseBeats, sourceLengthBeats, clip->loopEnabled);
    } else {
        gridComponent_->setLoopRegion(0.0, 0.0, false);
    }
}

// ============================================================================
// TimeRuler (extends base to add loop region)
// ============================================================================

void PianoRollContent::updateTimeRuler() {
    MidiEditorContent::updateTimeRuler();

    // Add loop region data (PianoRoll-specific)
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;
    if (clip) {
        timeRuler_->setLoopRegion(clip->offset - clip->loopStart, clip->loopLength,
                                  clip->loopEnabled);
    } else {
        timeRuler_->setLoopRegion(0.0, 0.0, false);
    }
}

// ============================================================================
// Relative time mode (PianoRoll-specific multi-clip handling)
// ============================================================================

void PianoRollContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;

        // Reload clips based on new mode
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            auto& clipManager = magda::ClipManager::getInstance();
            auto& selectionManager = magda::SelectionManager::getInstance();
            const auto* clip = clipManager.getClip(editingClipId_);
            if (clip && clip->type == magda::ClipType::MIDI) {
                magda::TrackId trackId = clip->trackId;

                // Get all selected clips
                const auto& selectedClipsSet = selectionManager.getSelectedClips();
                std::vector<magda::ClipId> selectedMidiClips;

                // Filter selected clips to only MIDI clips on this track
                for (magda::ClipId id : selectedClipsSet) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->type == magda::ClipType::MIDI && c->trackId == trackId) {
                        selectedMidiClips.push_back(id);
                    }
                }

                // If no selected clips or selected clips are on different track, use just the
                // primary
                if (selectedMidiClips.empty()) {
                    selectedMidiClips.push_back(editingClipId_);
                }

                if (relative) {
                    // Relative mode: show only selected clips
                    gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
                } else {
                    // Absolute mode: show ALL MIDI clips on this track
                    auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId);

                    // Filter to MIDI clips only
                    std::vector<magda::ClipId> allMidiClips;
                    for (magda::ClipId id : allClipsOnTrack) {
                        auto* c = clipManager.getClip(id);
                        if (c && c->type == magda::ClipType::MIDI) {
                            allMidiClips.push_back(id);
                        }
                    }

                    gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
                }
            }
        }

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

// ============================================================================
// Activation
// ============================================================================

void PianoRollContent::onActivated() {
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = selectedClip;
            gridComponent_->setClip(selectedClip);

            // Session clips and looping arrangement clips are locked to relative mode
            bool forceRelative = (clip->view == magda::ClipView::Session) || clip->loopEnabled;
            if (forceRelative) {
                setRelativeTimeMode(true);
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
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(editingClipId_);
        if (!clip) {
            gridComponent_->setClip(magda::INVALID_CLIP_ID);
            velocityLane_->setClip(magda::INVALID_CLIP_ID);
        } else {
            // Re-fetch all clips on this track (a split/delete may have changed the list)
            magda::TrackId trackId = clip->trackId;
            auto& selectionManager = magda::SelectionManager::getInstance();
            const auto& selectedClipsSet = selectionManager.getSelectedClips();

            std::vector<magda::ClipId> selectedMidiClips;
            for (magda::ClipId id : selectedClipsSet) {
                auto* c = clipManager.getClip(id);
                if (c && c->type == magda::ClipType::MIDI && c->trackId == trackId) {
                    selectedMidiClips.push_back(id);
                }
            }
            if (selectedMidiClips.empty()) {
                selectedMidiClips.push_back(editingClipId_);
            }

            if (relativeTimeMode_) {
                gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
            } else {
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId);
                std::vector<magda::ClipId> allMidiClips;
                for (magda::ClipId id : allClipsOnTrack) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->type == magda::ClipType::MIDI) {
                        allMidiClips.push_back(id);
                    }
                }
                gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
            }
        }
    }
    MidiEditorContent::clipsChanged();
    updateVelocityLane();
}

void PianoRollContent::clipPropertyChanged(magda::ClipId clipId) {
    // Check if this clip is one of the displayed clips
    const auto& displayedClips = gridComponent_->getClipIds();
    bool isDisplayed = false;
    for (magda::ClipId id : displayedClips) {
        if (id == clipId) {
            isDisplayed = true;
            break;
        }
    }

    if (isDisplayed) {
        // Defer UI refresh asynchronously to prevent deleting components during event handling
        juce::Component::SafePointer<PianoRollContent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, clipId]() {
            if (auto* self = safeThis.getComponent()) {
                // Re-evaluate force-relative mode (loop may have been toggled)
                const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
                if (clip && clip->type == magda::ClipType::MIDI) {
                    bool forceRelative =
                        (clip->view == magda::ClipView::Session) || clip->loopEnabled;
                    if (forceRelative) {
                        self->setRelativeTimeMode(true);
                    }
                }

                self->applyClipGridSettings();
                self->updateGridSize();
                self->updateTimeRuler();
                self->updateVelocityLane();
                self->repaint();
            }
        });
    }
}

void PianoRollContent::clipSelectionChanged(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID) {
        // Selection cleared - clear the piano roll
        editingClipId_ = magda::INVALID_CLIP_ID;
        gridComponent_->setClip(magda::INVALID_CLIP_ID);
        updateGridSize();
        updateTimeRuler();
        updateVelocityLane();
        repaint();
        return;
    }

    if (clipId != magda::INVALID_CLIP_ID) {
        auto& clipManager = magda::ClipManager::getInstance();
        auto& selectionManager = magda::SelectionManager::getInstance();
        const auto* clip = clipManager.getClip(clipId);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = clipId;

            magda::TrackId trackId = clip->trackId;

            // Get all selected clips
            const auto& selectedClipsSet = selectionManager.getSelectedClips();
            DBG("PianoRoll: Total selected clips: " << selectedClipsSet.size());

            std::vector<magda::ClipId> selectedMidiClips;

            // Filter selected clips to only MIDI clips on this track
            for (magda::ClipId id : selectedClipsSet) {
                auto* c = clipManager.getClip(id);
                if (c && c->type == magda::ClipType::MIDI && c->trackId == trackId) {
                    selectedMidiClips.push_back(id);
                    DBG("  - Selected MIDI clip on track: " << id);
                }
            }

            // If no selected clips or selected clips are on different track, use just the primary
            if (selectedMidiClips.empty()) {
                selectedMidiClips.push_back(clipId);
                DBG("  - No multi-selection, using primary clip: " << clipId);
            }

            DBG("PianoRoll: Selected MIDI clips count: " << selectedMidiClips.size());

            if (relativeTimeMode_) {
                // Relative mode: show only selected clips
                gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
            } else {
                // Absolute mode: show ALL MIDI clips on this track
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId);

                // Filter to MIDI clips only
                std::vector<magda::ClipId> allMidiClips;
                for (magda::ClipId id : allClipsOnTrack) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->type == magda::ClipType::MIDI) {
                        allMidiClips.push_back(id);
                    }
                }

                DBG("PianoRoll: Total MIDI clips on track: " << allMidiClips.size());
                gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
            }

            // Session clips are locked to relative mode
            bool forceRelative = (clip->view == magda::ClipView::Session);
            if (forceRelative) {
                setRelativeTimeMode(true);
            }

            updateGridSize();
            updateTimeRuler();
            updateVelocityLane();

            // Scroll to clip start position
            int scrollX = 0;
            if (!relativeTimeMode_ && clip->view != magda::ClipView::Session) {
                double tempo = 120.0;
                if (auto* controller = magda::TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                double clipStartBeats = clip->startTime * (tempo / 60.0);
                scrollX = static_cast<int>(clipStartBeats * horizontalZoom_);
            }
            viewport_->setViewPosition(scrollX, viewport_->getViewPositionY());

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
// SelectionManagerListener
// ============================================================================

void PianoRollContent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // Selection type changed - refresh the view
    repaint();
}

void PianoRollContent::multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) {
    // Multi-clip selection changed - update piano roll to show selected clips
    if (clipIds.empty()) {
        return;
    }

    auto& clipManager = magda::ClipManager::getInstance();

    // Get the first clip to determine the track
    magda::ClipId firstClipId = *clipIds.begin();
    const auto* firstClip = clipManager.getClip(firstClipId);
    if (!firstClip || firstClip->type != magda::ClipType::MIDI) {
        return;
    }

    magda::TrackId trackId = firstClip->trackId;

    // Filter selected clips to only MIDI clips on this track
    std::vector<magda::ClipId> selectedMidiClips;
    for (magda::ClipId id : clipIds) {
        auto* c = clipManager.getClip(id);
        if (c && c->type == magda::ClipType::MIDI && c->trackId == trackId) {
            selectedMidiClips.push_back(id);
        }
    }

    if (selectedMidiClips.empty()) {
        return;
    }

    // Update editing clip ID to the first selected clip
    editingClipId_ = selectedMidiClips[0];

    // Session clips are locked to relative mode
    bool forceRelative = (firstClip->view == magda::ClipView::Session);
    if (forceRelative) {
        setRelativeTimeMode(true);
    }

    gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);

    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();
    repaint();
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

        // Scroll to clip start position
        int scrollX = 0;
        if (!relativeTimeMode_) {
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
            if (clip && clip->view != magda::ClipView::Session) {
                double tempo = 120.0;
                if (auto* controller = magda::TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                double clipStartBeats = clip->startTime * (tempo / 60.0);
                scrollX = static_cast<int>(clipStartBeats * horizontalZoom_);
            }
        }
        viewport_->setViewPosition(scrollX, viewport_->getViewPositionY());

        repaint();
    }
}

// ============================================================================
// Drawing helpers
// ============================================================================

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

    // Pass multi-clip IDs for multi-clip velocity display
    if (gridComponent_) {
        velocityLane_->setClipIds(gridComponent_->getSelectedClipIds());
    }

    // Update zoom and mode settings
    velocityLane_->setPixelsPerBeat(horizontalZoom_);
    velocityLane_->setRelativeMode(relativeTimeMode_);

    // Get clip start beats
    const auto& selectedClipIds =
        gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
    if (selectedClipIds.size() > 1) {
        // Multi-clip: use earliest clip start (same as grid)
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double earliestStart = std::numeric_limits<double>::max();
        auto& clipManager = magda::ClipManager::getInstance();
        for (magda::ClipId id : selectedClipIds) {
            const auto* c = clipManager.getClip(id);
            if (c) {
                earliestStart = juce::jmin(earliestStart, c->startTime);
            }
        }
        if (earliestStart < std::numeric_limits<double>::max()) {
            velocityLane_->setClipStartBeats(earliestStart * (tempo / 60.0));
        } else {
            velocityLane_->setClipStartBeats(0.0);
        }
    } else {
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
    }

    // Sync loop region and clip length
    if (gridComponent_) {
        velocityLane_->setClipLengthBeats(gridComponent_->getClipLengthBeats());
        velocityLane_->setLoopRegion(gridComponent_->getLoopOffsetBeats(),
                                     gridComponent_->getLoopLengthBeats(),
                                     gridComponent_->isLoopEnabled());
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
