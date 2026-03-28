#include "PianoRollContent.hpp"

#include <limits>

#include "../../core/SelectionManager.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"
#include "audio/MidiChordEnginePlugin.hpp"
#include "core/ChordAnnotationCommands.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "music/ChordEngine.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/pianoroll/CCLaneComponent.hpp"
#include "ui/components/pianoroll/MidiDrawerComponent.hpp"
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
    chordToggle_->setTooltip("Toggle chord row");
    chordToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    chordToggle_->setActive(showChordRow_);
    chordToggle_->onClick = [this]() {
        setChordRowVisible(!showChordRow_);
        chordToggle_->setActive(showChordRow_);
    };
    addAndMakeVisible(chordToggle_.get());

    // Create chord detect button (appears in chord row's keyboard column)
    chordDetectBtn_ = std::make_unique<magda::SvgButton>("ChordDetect", BinaryData::refresh_svg,
                                                         BinaryData::refresh_svgSize);
    chordDetectBtn_->setTooltip("Recalculate chords from notes");
    chordDetectBtn_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    chordDetectBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    chordDetectBtn_->onClick = [this]() { detectChordsFromNotes(); };
    chordDetectBtn_->setVisible(showChordRow_);
    addAndMakeVisible(chordDetectBtn_.get());

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

    // Setup MIDI drawer (tabbed: velocity + CC + pitchbend)
    setupMidiDrawer();

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
    gridComponent_->onNoteAdded = [this](magda::ClipId clipId, double beat, int noteNumber,
                                         int velocity) {
        double defaultLength = gridComponent_->getGridResolutionBeats();
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

    // Handle batch note movement (single undo step)
    gridComponent_->onMultipleNotesMoved =
        [](magda::ClipId clipId, std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves) {
            auto cmd =
                std::make_unique<magda::MoveMultipleMidiNotesCommand>(clipId, std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        };

    // Handle batch note resize (single undo step)
    gridComponent_->onMultipleNotesResized =
        [](magda::ClipId clipId, std::vector<std::pair<size_t, double>> noteLengths) {
            auto cmd = std::make_unique<magda::ResizeMultipleMidiNotesCommand>(
                clipId, std::move(noteLengths));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        };

    // Handle left-edge resize for multi-selection (compound move+resize as single undo step)
    gridComponent_->onLeftResizeMultipleNotes =
        [](magda::ClipId clipId, std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves,
           std::vector<std::pair<size_t, double>> noteLengths) {
            magda::CompoundOperationScope scope("Resize Notes From Left");
            auto moveCmd =
                std::make_unique<magda::MoveMultipleMidiNotesCommand>(clipId, std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(moveCmd));
            auto resizeCmd = std::make_unique<magda::ResizeMultipleMidiNotesCommand>(
                clipId, std::move(noteLengths));
            magda::UndoManager::getInstance().executeCommand(std::move(resizeCmd));
        };

    // Handle note selection - update SelectionManager
    gridComponent_->onNoteSelected = [](magda::ClipId clipId, size_t noteIndex, bool isAdditive) {
        if (isAdditive) {
            magda::SelectionManager::getInstance().addNoteToSelection(clipId, noteIndex);
        } else {
            magda::SelectionManager::getInstance().selectNote(clipId, noteIndex);
        }
    };

    // Handle batch note selection changes (lasso, deselect-all, Cmd+click toggle)
    gridComponent_->onNoteSelectionChanged = [this](magda::ClipId clipId,
                                                    std::vector<size_t> noteIndices) {
        if (noteIndices.empty()) {
            // Clear note selection — preserve clip selection
            magda::SelectionManager::getInstance().clearNoteSelection();
            if (midiDrawer_ && midiDrawer_->getVelocityLane()) {
                midiDrawer_->getVelocityLane()->setSelectedNoteIndices({});
            } else if (velocityLane_) {
                velocityLane_->setSelectedNoteIndices({});
            }
        } else {
            magda::SelectionManager::getInstance().selectNotes(clipId, noteIndices);
        }
    };

    // Forward note drag preview to velocity lane for position sync
    gridComponent_->onNoteDragging = [this](magda::ClipId /*clipId*/, size_t noteIndex,
                                            double previewBeat, bool isDragging) {
        if (midiDrawer_ && midiDrawer_->getVelocityLane()) {
            midiDrawer_->getVelocityLane()->setNotePreviewPosition(noteIndex, previewBeat,
                                                                   isDragging);
        } else if (velocityLane_) {
            velocityLane_->setNotePreviewPosition(noteIndex, previewBeat, isDragging);
        }
    };

    // Handle quantize from right-click context menu
    gridComponent_->onQuantizeNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices,
                                         magda::QuantizeMode mode, double gridBeats) {
        auto cmd = std::make_unique<magda::QuantizeMidiNotesCommand>(clipId, std::move(noteIndices),
                                                                     gridBeats, mode);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    // Handle copy from context menu
    gridComponent_->onCopyNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices) {
        magda::ClipManager::getInstance().copyNotesToClipboard(clipId, noteIndices);
    };

    // Handle paste from context menu
    gridComponent_->onPasteNotes = [](magda::ClipId clipId) {
        auto& clipManager = magda::ClipManager::getInstance();
        if (!clipManager.hasNotesInClipboard())
            return;

        const auto* clip = clipManager.getClip(clipId);
        if (!clip || clip->type != magda::ClipType::MIDI)
            return;

        double pasteOffset = clipManager.getNoteClipboardMinBeat();
        const auto& clipboard = clipManager.getNoteClipboard();
        std::vector<magda::MidiNote> notesToPaste;
        notesToPaste.reserve(clipboard.size());
        for (const auto& note : clipboard) {
            magda::MidiNote n = note;
            n.startBeat += pasteOffset;
            notesToPaste.push_back(n);
        }

        auto cmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
            clipId, std::move(notesToPaste), "Paste MIDI Notes");
        auto* cmdPtr = cmd.get();
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        const auto& inserted = cmdPtr->getInsertedIndices();
        if (!inserted.empty()) {
            magda::SelectionManager::getInstance().selectNotes(
                clipId, std::vector<size_t>(inserted.begin(), inserted.end()));
        }
    };

    // Handle duplicate from context menu
    gridComponent_->onDuplicateNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices) {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || clip->type != magda::ClipType::MIDI)
            return;

        double minStart = std::numeric_limits<double>::max();
        double maxEnd = 0.0;
        std::vector<magda::MidiNote> notesToDuplicate;
        for (size_t idx : noteIndices) {
            if (idx < clip->midiNotes.size()) {
                const auto& note = clip->midiNotes[idx];
                notesToDuplicate.push_back(note);
                minStart = std::min(minStart, note.startBeat);
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
        }
        if (!notesToDuplicate.empty()) {
            double offset = maxEnd - minStart;
            for (auto& note : notesToDuplicate) {
                note.startBeat += offset;
            }
            auto cmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
                clipId, std::move(notesToDuplicate), "Duplicate MIDI Notes");
            auto* cmdPtr = cmd.get();
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));

            const auto& inserted = cmdPtr->getInsertedIndices();
            if (!inserted.empty()) {
                magda::SelectionManager::getInstance().selectNotes(
                    clipId, std::vector<size_t>(inserted.begin(), inserted.end()));
            }
        }
    };

    // Handle delete from context menu
    gridComponent_->onDeleteNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices) {
        auto cmd =
            std::make_unique<magda::DeleteMultipleMidiNotesCommand>(clipId, std::move(noteIndices));
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        magda::SelectionManager::getInstance().clearNoteSelection();
    };

    // Edit cursor set from grid (Alt+click on grid line) — local to MIDI editor
    gridComponent_->onEditCursorSet = [this](double positionSeconds) {
        setLocalEditCursor(positionSeconds);
    };

    // Handle chord block drops from the chord panel
    gridComponent_->onChordDropped = [](magda::ClipId clipId, double beat, double noteLength,
                                        std::vector<std::pair<int, int>> notes,
                                        juce::String chordName) {
        if (notes.empty())
            return;

        // Allocate chord group ID for linking notes to annotation
        auto* clipData = magda::ClipManager::getInstance().getClip(clipId);
        int groupId = clipData ? clipData->nextChordGroupId++ : 0;

        // Compound: MIDI notes + chord annotation undo as one step
        magda::CompoundOperationScope scope("Add Chord");

        std::vector<magda::MidiNote> midiNotes;
        midiNotes.reserve(notes.size());
        for (const auto& [noteNumber, velocity] : notes) {
            magda::MidiNote mn;
            mn.noteNumber = noteNumber;
            mn.velocity = velocity;
            mn.startBeat = beat;
            mn.lengthBeats = noteLength;
            mn.chordGroup = groupId;
            midiNotes.push_back(mn);
        }

        auto noteCmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
            clipId, std::move(midiNotes), "Add chord notes");
        magda::UndoManager::getInstance().executeCommand(std::move(noteCmd));

        if (chordName.isNotEmpty()) {
            magda::ClipInfo::ChordAnnotation annotation;
            annotation.beatPosition = beat;
            annotation.lengthBeats = noteLength;
            annotation.chordName = chordName;
            annotation.chordGroup = groupId;
            auto chordCmd = std::make_unique<magda::AddChordAnnotationCommand>(clipId, annotation);
            magda::UndoManager::getInstance().executeCommand(std::move(chordCmd));
        }
    };
}

// ============================================================================
// MidiEditorContent virtual implementations
// ============================================================================

void PianoRollContent::setGridPixelsPerBeat(double ppb) {
    if (gridComponent_)
        gridComponent_->setPixelsPerBeat(ppb);
    if (showChordRow_)
        repaint();
}

void PianoRollContent::setGridPlayheadPosition(double position) {
    if (gridComponent_)
        gridComponent_->setPlayheadPosition(position);
}

void PianoRollContent::setGridEditCursorPosition(double pos, bool visible) {
    if (gridComponent_)
        gridComponent_->setEditCursorPosition(pos, visible);
}

void PianoRollContent::onScrollPositionChanged(int scrollX, int scrollY) {
    keyboard_->setScrollOffset(scrollY);
    if (midiDrawer_) {
        midiDrawer_->setScrollOffset(scrollX);
    } else if (velocityLane_) {
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
    if (timeRuler_)
        timeRuler_->setGridResolution(gridResolutionBeats_);
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

        // Horizontal separator at bottom of chord row — full width
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(CHORD_ROW_HEIGHT - 1, static_cast<float>(SIDEBAR_WIDTH),
                             static_cast<float>(getWidth()));
    }

    // Draw velocity drawer header (if open) — only for legacy path without MidiDrawer
    if (velocityDrawerOpen_ && !midiDrawer_) {
        auto drawerHeaderArea = getLocalBounds();
        drawerHeaderArea.removeFromLeft(SIDEBAR_WIDTH);
        drawerHeaderArea = drawerHeaderArea.removeFromBottom(drawerHeight_);
        drawerHeaderArea = drawerHeaderArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
        drawVelocityHeader(g, drawerHeaderArea);
    }
}

void PianoRollContent::paintOverChildren(juce::Graphics& g) {
    // Extend the ruler's tick-area border line through the sidebar/keyboard corner
    int rulerTop = showChordRow_ ? CHORD_ROW_HEIGHT : 0;
    int tickLineY = rulerTop + RULER_HEIGHT - LayoutConfig::getInstance().rulerMajorTickHeight;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(SIDEBAR_WIDTH, tickLineY, KEYBOARD_WIDTH, 1);
}

void PianoRollContent::resized() {
    auto bounds = getLocalBounds();

    // Skip sidebar (painted in paint())
    bounds.removeFromLeft(SIDEBAR_WIDTH);

    // Position sidebar icons
    int iconSize = 22;
    int padding = (SIDEBAR_WIDTH - iconSize) / 2;
    // Chord toggle at top of sidebar — vertically centered in chord row height
    int chordToggleY = showChordRow_ ? (CHORD_ROW_HEIGHT - iconSize) / 2 : padding;
    chordToggle_->setBounds(padding, chordToggleY, iconSize, iconSize);
    // Velocity toggle at bottom
    velocityToggle_->setBounds(padding, getHeight() - iconSize - padding, iconSize, iconSize);

    // Skip chord row space if visible (drawn in paint)
    if (showChordRow_) {
        bounds.removeFromTop(CHORD_ROW_HEIGHT);
        // Position detect button in the keyboard column of the chord row
        int detectSize = 18;
        int detectX = SIDEBAR_WIDTH + (KEYBOARD_WIDTH - detectSize) / 2;
        int detectY = (CHORD_ROW_HEIGHT - detectSize) / 2;
        chordDetectBtn_->setBounds(detectX, detectY, detectSize, detectSize);
        chordDetectBtn_->setVisible(true);
    } else {
        chordDetectBtn_->setVisible(false);
    }

    // MIDI drawer at bottom (if open)
    if (velocityDrawerOpen_) {
        auto drawerArea = bounds.removeFromBottom(drawerHeight_);
        if (midiDrawer_) {
            // MidiDrawerComponent gets the full width including the left column,
            // so it can place controls (e.g. PB range) in the left margin area.
            midiDrawer_->setLeftMargin(KEYBOARD_WIDTH);
            midiDrawer_->setBounds(drawerArea);
            midiDrawer_->setVisible(true);
        } else if (velocityLane_) {
            // Legacy path: separate header drawn in paint(), lane below
            drawerArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
            drawerArea.removeFromLeft(KEYBOARD_WIDTH);
            velocityLane_->setBounds(drawerArea);
            velocityLane_->setVisible(true);
        }
    } else {
        if (midiDrawer_)
            midiDrawer_->setVisible(false);
        if (velocityLane_)
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

    // Center on notes (or C4) on first layout
    if (needsInitialCentering_ && viewport_->getHeight() > 0) {
        centerOnNotes();
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
        if (clip->loopEnabled || clip->view == magda::ClipView::Session) {
            // Looped clips and session clips: show content from bar 1
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
    if (clip && selectedClipIds.size() <= 1) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double beatsPerSecond = tempo / 60.0;
        double loopOffsetBeats = clip->loopStart * beatsPerSecond;
        // MIDI clips use loopLengthBeats directly; audio clips derive from loopLength (seconds)
        double sourceLengthBeats =
            clip->loopLengthBeats > 0.0 ? clip->loopLengthBeats : clip->loopLength * beatsPerSecond;
        gridComponent_->setLoopRegion(loopOffsetBeats, sourceLengthBeats, clip->loopEnabled);
    } else {
        gridComponent_->setLoopRegion(0.0, 0.0, false);
    }
}

// Loop region is now handled by MidiEditorContent::updateTimeRuler()

void PianoRollContent::updateGridLoopRegion() {
    if (draggingLoopRegion_) {
        gridComponent_->setLoopRegion(previewLoopStartBeats_, previewLoopLengthBeats_, true);
    }
}

void PianoRollContent::setGridPhasePreview(double beats, bool active) {
    gridComponent_->setPhasePreview(beats, active);
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
                    // Absolute mode: show MIDI clips on this track matching the editing clip's view
                    auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);

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

// setVelocityDrawerVisible is now in the base class MidiEditorContent

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

            // Auto-show chord row if track has a chord engine
            if (!showChordRow_ && clip->trackId != magda::INVALID_TRACK_ID) {
                auto* trackInfo = magda::TrackManager::getInstance().getTrack(clip->trackId);
                if (trackInfo) {
                    for (const auto& elem : trackInfo->chainElements) {
                        if (magda::isDevice(elem)) {
                            const auto& dev = magda::getDevice(elem);
                            if (dev.pluginId.containsIgnoreCase(
                                    magda::daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
                                setChordRowVisible(true);
                                chordToggle_->setActive(true);
                                break;
                            }
                        }
                    }
                }
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
            if (midiDrawer_)
                midiDrawer_->setClip(magda::INVALID_CLIP_ID);
            else if (velocityLane_)
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
                // Only show clips matching the editing clip's view (arrangement or session)
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);
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

                // Sync chord annotations with their linked notes
                self->syncChordAnnotations(clipId);

                // Auto-clear chord annotations if notes were all deleted
                if (clip && clip->midiNotes.empty() && !clip->chordAnnotations.empty()) {
                    magda::ClipManager::getInstance().clearChordAnnotations(clipId);
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
                // Absolute mode: show MIDI clips on this track matching the editing clip's view
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);

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

void PianoRollContent::noteSelectionChanged(const magda::NoteSelection& selection) {
    setVelocityLaneSelectedNotes(selection.noteIndices);
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

        // Center vertically on existing notes (or C4 if empty)
        centerOnNotes();

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

    // Get chord annotations from the editing clip
    const auto* clip = (editingClipId_ != magda::INVALID_CLIP_ID)
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;
    if (!clip || clip->chordAnnotations.empty()) {
        // Empty state hint
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("No chords detected", area.reduced(4, 0), juce::Justification::centredLeft);
        return;
    }

    int scrollX = viewport_ ? viewport_->getViewPositionX() : 0;
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    for (const auto& annotation : clip->chordAnnotations) {
        int startX = static_cast<int>(annotation.beatPosition * horizontalZoom_) +
                     GRID_LEFT_PADDING - scrollX;
        int endX =
            static_cast<int>((annotation.beatPosition + annotation.lengthBeats) * horizontalZoom_) +
            GRID_LEFT_PADDING - scrollX;

        // Skip if out of view
        if (endX < 0 || startX > area.getWidth())
            continue;

        // Clip to visible area
        int drawStartX = juce::jmax(0, startX) + area.getX();
        int drawEndX = juce::jmin(area.getWidth(), endX) + area.getX();

        // Draw chord block
        auto blockBounds = juce::Rectangle<int>(drawStartX + 1, area.getY() + 2,
                                                drawEndX - drawStartX - 2, area.getHeight() - 4);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
        g.fillRoundedRectangle(blockBounds.toFloat(), 3.0f);

        // Draw chord name
        if (blockBounds.getWidth() > 10) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            g.drawText(annotation.chordName, blockBounds.reduced(2, 0),
                       juce::Justification::centredLeft, true);
        }
    }
}

void PianoRollContent::syncChordAnnotations(magda::ClipId clipId) {
    if (isSyncingChords_)
        return;
    isSyncingChords_ = true;

    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->chordAnnotations.empty()) {
        isSyncingChords_ = false;
        return;
    }

    auto& engine = magda::music::ChordEngine::getInstance();

    for (auto it = clip->chordAnnotations.begin(); it != clip->chordAnnotations.end();) {
        if (it->chordGroup == 0) {
            ++it;
            continue;  // Skip unlinked annotations
        }

        // Find all notes in this chord group
        std::vector<magda::music::ChordNote> chordNotes;
        double minBeat = std::numeric_limits<double>::max();
        double maxEnd = 0.0;

        for (const auto& note : clip->midiNotes) {
            if (note.chordGroup == it->chordGroup) {
                chordNotes.push_back({note.noteNumber, note.velocity});
                minBeat = std::min(minBeat, note.startBeat);
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
        }

        if (chordNotes.empty()) {
            // All notes in group deleted — remove annotation
            it = clip->chordAnnotations.erase(it);
            continue;
        }

        // Update position and length from note extents
        it->beatPosition = minBeat;
        it->lengthBeats = maxEnd - minBeat;

        // Re-detect chord name if pitches changed
        if (chordNotes.size() >= 2) {
            auto chord = engine.detect(chordNotes);
            if (chord.name != "none" && !chord.name.isEmpty())
                it->chordName = chord.getDisplayName();
        }
        ++it;
    }

    isSyncingChords_ = false;
}

void PianoRollContent::detectChordsFromNotes() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->midiNotes.empty())
        return;

    int beatsPerBar = 4;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;

    // Find the end of the last note (not the full clip length)
    double lastNoteEnd = 0.0;
    for (const auto& note : clip->midiNotes)
        lastNoteEnd = juce::jmax(lastNoteEnd, note.startBeat + note.lengthBeats);

    double scanLength = lastNoteEnd;
    double step = beatsPerBar;

    // Collect detected chords with their contributing note indices
    struct DetectedChord {
        double beat;
        juce::String name;
        std::vector<size_t> noteIndices;
    };
    std::vector<DetectedChord> detected;

    auto& engine = magda::music::ChordEngine::getInstance();
    for (double beat = 0.0; beat < scanLength; beat += step) {
        // Collect notes sounding at this beat
        std::vector<magda::music::ChordNote> chordNotes;
        std::vector<size_t> indices;
        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];
            if (note.startBeat <= beat && (note.startBeat + note.lengthBeats) > beat) {
                chordNotes.push_back({note.noteNumber, note.velocity});
                indices.push_back(i);
            }
        }

        if (chordNotes.size() < 2)
            continue;

        auto chord = engine.detect(chordNotes);
        if (chord.name == "none" || chord.name == "unknown" || chord.name.isEmpty())
            continue;

        detected.push_back({beat, chord.getDisplayName(), std::move(indices)});
    }

    if (detected.empty())
        return;

    // Clear existing + add new, all as one undo step
    magda::CompoundOperationScope scope("Detect Chords");

    auto clearCmd = std::make_unique<magda::ClearChordAnnotationsCommand>(editingClipId_);
    magda::UndoManager::getInstance().executeCommand(std::move(clearCmd));

    // Assign chordGroup IDs to annotations and their notes
    std::vector<std::pair<size_t, int>> noteGroupAssignments;

    for (size_t i = 0; i < detected.size(); ++i) {
        int groupId = clip->nextChordGroupId++;

        magda::ClipInfo::ChordAnnotation annotation;
        annotation.beatPosition = detected[i].beat;
        // Extend to next chord or end of last note
        annotation.lengthBeats = (i + 1 < detected.size())
                                     ? (detected[i + 1].beat - detected[i].beat)
                                     : (lastNoteEnd - detected[i].beat);
        annotation.chordName = detected[i].name;
        annotation.chordGroup = groupId;

        auto cmd = std::make_unique<magda::AddChordAnnotationCommand>(editingClipId_, annotation);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // Collect note-to-group assignments
        for (size_t noteIdx : detected[i].noteIndices)
            noteGroupAssignments.emplace_back(noteIdx, groupId);
    }

    // Tag notes with their chord group IDs (undoable)
    if (!noteGroupAssignments.empty()) {
        auto groupCmd = std::make_unique<magda::SetNoteChordGroupsCommand>(
            editingClipId_, std::move(noteGroupAssignments));
        magda::UndoManager::getInstance().executeCommand(std::move(groupCmd));
    }

    repaint();
}

void PianoRollContent::drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw header background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));

    // Draw active lane label in keyboard area
    auto labelArea = area.removeFromLeft(KEYBOARD_WIDTH);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    juce::String label = midiDrawer_ ? midiDrawer_->getActiveTabName() : "Velocity";
    g.drawText(label, labelArea.reduced(4, 0), juce::Justification::centredLeft, true);
}

void PianoRollContent::updateVelocityLane() {
    // Update the MIDI drawer if available
    if (midiDrawer_) {
        MidiEditorContent::updateMidiDrawer();

        // Pass multi-clip IDs for multi-clip velocity display
        if (gridComponent_) {
            midiDrawer_->setClipIds(gridComponent_->getSelectedClipIds());
        }

        // Override clip start beats for multi-clip mode
        const auto& selectedClipIds =
            gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
        if (selectedClipIds.size() > 1) {
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
                midiDrawer_->setClipStartBeats(earliestStart * (tempo / 60.0));
            }
        }

        // Sync loop region and clip length
        if (gridComponent_) {
            midiDrawer_->setClipLengthBeats(gridComponent_->getClipLengthBeats());
            midiDrawer_->setLoopRegion(gridComponent_->getLoopOffsetBeats(),
                                       gridComponent_->getLoopLengthBeats(),
                                       gridComponent_->isLoopEnabled());
        }

        midiDrawer_->refreshAll();
        return;
    }

    // Fallback: legacy velocity-only path
    if (!velocityLane_)
        return;

    MidiEditorContent::updateVelocityLane();

    if (gridComponent_) {
        velocityLane_->setClipIds(gridComponent_->getSelectedClipIds());
    }

    const auto& selectedClipIds =
        gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
    if (selectedClipIds.size() > 1) {
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
        }
    }

    if (gridComponent_) {
        velocityLane_->setClipLengthBeats(gridComponent_->getClipLengthBeats());
        velocityLane_->setLoopRegion(gridComponent_->getLoopOffsetBeats(),
                                     gridComponent_->getLoopLengthBeats(),
                                     gridComponent_->isLoopEnabled());
    }

    velocityLane_->refreshNotes();
}

void PianoRollContent::onVelocityEdited() {
    // Refresh velocity lane (via base or drawer)
    MidiEditorContent::onVelocityEdited();
    if (midiDrawer_)
        midiDrawer_->refreshAll();
    // Also refresh grid component (PianoRoll-specific)
    if (gridComponent_) {
        gridComponent_->refreshNotes();
    }
}

void PianoRollContent::centerOnNote(int noteNumber) {
    if (!viewport_)
        return;

    int noteY = (MAX_NOTE - noteNumber) * noteHeight_;
    int viewportHeight = viewport_->getHeight();
    int scrollY = juce::jmax(0, noteY - (viewportHeight / 2) + (noteHeight_ / 2));

    viewport_->setViewPosition(viewport_->getViewPositionX(), scrollY);
    keyboard_->setScrollOffset(scrollY);
}

void PianoRollContent::centerOnNotes() {
    if (!viewport_)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->midiNotes.empty()) {
        // No notes — default to C4 (MIDI note 72 in C-2 convention)
        centerOnNote(72);
        return;
    }

    // Find note range
    int minNote = 127;
    int maxNote = 0;
    for (const auto& note : clip->midiNotes) {
        minNote = juce::jmin(minNote, note.noteNumber);
        maxNote = juce::jmax(maxNote, note.noteNumber);
    }

    // Center on the midpoint of the note range
    int midNote = (minNote + maxNote) / 2;
    centerOnNote(midNote);
}

}  // namespace magda::daw::ui
