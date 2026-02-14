#include "PianoRollGridComponent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "core/ClipManager.hpp"

namespace magda {

PianoRollGridComponent::PianoRollGridComponent() {
    setName("PianoRollGrid");
    setWantsKeyboardFocus(true);
    ClipManager::getInstance().addListener(this);
}

PianoRollGridComponent::~PianoRollGridComponent() {
    ClipManager::getInstance().removeListener(this);
    clearNoteComponents();
}

void PianoRollGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    paintGrid(g, bounds);

    // Draw clip boundaries for multi-clip view
    if (clipIds_.size() > 1) {
        auto& clipManager = ClipManager::getInstance();

        // Get tempo for beat conversion
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }

        // Collect selected clip regions to exclude from dimming
        struct ClipRegion {
            int startX, endX;
        };
        std::vector<ClipRegion> selectedRegions;

        for (ClipId clipId : clipIds_) {
            const auto* clip = clipManager.getClip(clipId);
            if (!clip) {
                continue;
            }

            double clipStartBeats = clip->startTime * (tempo / 60.0);
            double clipEndBeats = (clip->startTime + clip->length) * (tempo / 60.0);

            // In relative mode, offset from the earliest clip start
            if (relativeMode_) {
                clipStartBeats -= clipStartBeats_;
                clipEndBeats -= clipStartBeats_;
            }

            int startX = beatToPixel(clipStartBeats);
            int endX = beatToPixel(clipEndBeats);

            if (isClipSelected(clipId)) {
                selectedRegions.push_back({startX, endX});
            }

            // Draw subtle boundary markers
            g.setColour(clip->colour.withAlpha(0.3f));
            g.fillRect(startX, 0, 2, getHeight());
            g.fillRect(endX - 2, 0, 2, getHeight());
        }

        // Dim everything outside selected clip regions
        if (!selectedRegions.empty()) {
            g.setColour(juce::Colour(0x20000000));
            int prevEnd = bounds.getX();
            // Sort by startX
            std::sort(selectedRegions.begin(), selectedRegions.end(),
                      [](const ClipRegion& a, const ClipRegion& b) { return a.startX < b.startX; });
            for (const auto& region : selectedRegions) {
                if (region.startX > prevEnd) {
                    g.fillRect(prevEnd, 0, region.startX - prevEnd, getHeight());
                }
                prevEnd = juce::jmax(prevEnd, region.endX);
            }
            if (prevEnd < bounds.getRight()) {
                g.fillRect(prevEnd, 0, bounds.getRight() - prevEnd, getHeight());
            }
        }
    } else if (!relativeMode_ && clipLengthBeats_ > 0) {
        // Single clip in absolute mode - original behavior
        // Clip start boundary
        int clipStartX = beatToPixel(clipStartBeats_);
        if (clipStartX >= 0 && clipStartX <= bounds.getRight()) {
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.6f));
            g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());
        }

        // Dim area before clip start
        if (clipStartX > bounds.getX()) {
            g.setColour(juce::Colour(0x60000000));
            g.fillRect(bounds.getX(), bounds.getY(), clipStartX - bounds.getX(),
                       bounds.getHeight());
        }

        // Clip end boundary
        int clipEndX = beatToPixel(clipStartBeats_ + clipLengthBeats_);
        if (clipEndX >= 0 && clipEndX <= bounds.getRight()) {
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
            g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());
        }

        // Dim area after clip end
        if (clipEndX < bounds.getRight()) {
            g.setColour(juce::Colour(0x60000000));
            g.fillRect(clipEndX, bounds.getY(), bounds.getRight() - clipEndX, bounds.getHeight());
        }
    } else if (clipLengthBeats_ > 0) {
        // In relative mode, just show end boundary at clip length
        int clipEndX = beatToPixel(clipLengthBeats_);
        if (clipEndX >= 0 && clipEndX <= bounds.getRight()) {
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
            g.fillRect(clipEndX - 1, 0, 3, bounds.getHeight());
        }

        // Dim area after clip end
        if (clipEndX < bounds.getRight()) {
            g.setColour(juce::Colour(0x60000000));
            g.fillRect(clipEndX, bounds.getY(), bounds.getRight() - clipEndX, bounds.getHeight());
        }
    }

    // Draw loop region markers
    if (loopEnabled_ && loopLengthBeats_ > 0.0) {
        double loopStartBeat =
            relativeMode_ ? loopOffsetBeats_ : (clipStartBeats_ + loopOffsetBeats_);
        double loopEndBeat = loopStartBeat + loopLengthBeats_;

        int loopStartX = beatToPixel(loopStartBeat);
        int loopEndX = beatToPixel(loopEndBeat);

        juce::Colour loopColour = DarkTheme::getColour(DarkTheme::LOOP_MARKER);

        // Green vertical lines at loop boundaries (2px)
        if (loopStartX >= 0 && loopStartX <= bounds.getRight()) {
            g.setColour(loopColour);
            g.fillRect(loopStartX - 1, 0, 2, bounds.getHeight());
        }
        if (loopEndX >= 0 && loopEndX <= bounds.getRight()) {
            g.setColour(loopColour);
            g.fillRect(loopEndX - 1, 0, 2, bounds.getHeight());
        }
    }

    // Draw content offset marker (yellow vertical line)
    if (clipIds_.size() <= 1 && clipId_ != INVALID_CLIP_ID) {
        const auto* offsetClip = ClipManager::getInstance().getClip(clipId_);
        if (offsetClip && offsetClip->midiOffset > 0.0) {
            double offsetBeat =
                relativeMode_ ? offsetClip->midiOffset : (clipStartBeats_ + offsetClip->midiOffset);
            int offsetX = beatToPixel(offsetBeat);
            if (offsetX >= 0 && offsetX <= bounds.getRight()) {
                g.setColour(DarkTheme::getColour(DarkTheme::OFFSET_MARKER));
                g.fillRect(offsetX - 1, 0, 2, bounds.getHeight());
            }
        }
    }

    // Draw ghost loop-repeating notes (paint-only, non-interactive)
    if (loopEnabled_ && loopLengthBeats_ > 0.0 && clipIds_.size() <= 1) {
        auto& clipManager = ClipManager::getInstance();
        const auto* clip = clipManager.getClip(clipId_);
        if (clip && clip->type == ClipType::MIDI && clipLengthBeats_ > 0.0) {
            int numRepetitions = static_cast<int>(std::ceil(clipLengthBeats_ / loopLengthBeats_));

            for (int rep = 1; rep < numRepetitions; ++rep) {
                for (const auto& note : clip->midiNotes) {
                    // Only draw notes within the loop region
                    if (note.startBeat < loopOffsetBeats_ ||
                        note.startBeat >= loopOffsetBeats_ + loopLengthBeats_) {
                        continue;
                    }

                    double relStart = (note.startBeat - loopOffsetBeats_) + rep * loopLengthBeats_;

                    // Clamp note end to repetition boundary and clip length
                    double noteEnd = relStart + note.lengthBeats;
                    double repEnd = (rep + 1) * loopLengthBeats_;
                    noteEnd = juce::jmin(noteEnd, repEnd);
                    noteEnd = juce::jmin(noteEnd, clipLengthBeats_);
                    if (relStart >= clipLengthBeats_) {
                        continue;
                    }
                    double displayLength = noteEnd - relStart;
                    if (displayLength <= 0.0) {
                        continue;
                    }

                    // Convert to display coordinates
                    double displayBeat = relativeMode_ ? relStart : (clipStartBeats_ + relStart);

                    int x = beatToPixel(displayBeat);
                    int y = noteNumberToY(note.noteNumber);
                    int width = juce::jmax(8, static_cast<int>(displayLength * pixelsPerBeat_));
                    int height = noteHeight_ - 2;

                    // Draw as ghost rectangle
                    g.setColour(clip->colour.withAlpha(0.35f));
                    g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                           static_cast<float>(width), static_cast<float>(height),
                                           2.0f);
                }
            }
        }
    }

    // Draw playhead line if playing
    if (playheadPosition_ >= 0.0) {
        // Convert seconds to beats
        // Get tempo from TimelineController
        double tempo = 120.0;  // Default
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double playheadBeats = playheadPosition_ / secondsPerBeat;

        // In absolute mode, playhead is at absolute position
        // In relative mode, need to offset by clip start
        double displayBeat = relativeMode_ ? (playheadBeats - clipStartBeats_) : playheadBeats;

        int playheadX = beatToPixel(displayBeat);
        if (playheadX >= 0 && playheadX <= bounds.getRight()) {
            // Draw playhead line (red)
            g.setColour(juce::Colour(0xFFFF4444));
            g.fillRect(playheadX - 1, 0, 2, bounds.getHeight());
        }
    }
}

void PianoRollGridComponent::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Background - match the white key color from keyboard
    g.setColour(juce::Colour(0xFF3a3a3a));
    g.fillRect(area);

    // Use the full timeline length for drawing grid lines
    double lengthBeats = timelineLengthBeats_;

    // The grid area starts after left padding
    auto gridArea = area.withTrimmedLeft(leftPadding_);

    // Draw row backgrounds - alternate for black/white keys (only in grid area)
    for (int note = MIN_NOTE; note <= MAX_NOTE; note++) {
        int y = noteNumberToY(note);

        if (y + noteHeight_ < area.getY() || y > area.getBottom()) {
            continue;
        }

        // Black key rows are darker
        if (isBlackKey(note)) {
            g.setColour(juce::Colour(0xFF2a2a2a));
            g.fillRect(gridArea.getX(), y, gridArea.getWidth(), noteHeight_);
        }
    }

    // Fill left padding area with solid panel background (covers the alternating rows)
    if (leftPadding_ > 0) {
        g.setColour(DarkTheme::getPanelBackgroundColour());
        g.fillRect(area.getX(), area.getY(), leftPadding_, area.getHeight());
    }

    // Draw horizontal grid lines at each note boundary (at bottom of each row, -1 to match
    // keyboard)
    g.setColour(juce::Colour(0xFF505050));
    for (int note = MIN_NOTE; note <= MAX_NOTE; note++) {
        int y = noteNumberToY(note) + noteHeight_ - 1;
        if (y >= area.getY() && y <= area.getBottom()) {
            g.drawHorizontalLine(y, static_cast<float>(gridArea.getX()),
                                 static_cast<float>(area.getRight()));
        }
    }

    // Vertical beat lines
    paintBeatLines(g, gridArea, lengthBeats);
}

void PianoRollGridComponent::paintBeatLines(juce::Graphics& g, juce::Rectangle<int> area,
                                            double lengthBeats) {
    double gridRes = gridResolutionBeats_;
    if (gridRes <= 0.0)
        return;

    const float top = static_cast<float>(area.getY());
    const float bottom = static_cast<float>(area.getBottom());
    const int left = area.getX();
    const int right = area.getRight();
    const int tsNum = timeSignatureNumerator_;

    // Pass 1: Subdivision lines at grid resolution (finest, drawn first)
    // Use integer counter to avoid floating-point drift (important for triplets etc.)
    {
        g.setColour(juce::Colour(0xFF505050));
        int numLines = static_cast<int>(std::ceil(lengthBeats / gridRes));
        for (int i = 0; i <= numLines; i++) {
            double beat = i * gridRes;
            if (beat > lengthBeats)
                break;
            // Skip positions on whole beats (drawn in pass 2/3)
            double nearest = std::round(beat);
            if (std::abs(beat - nearest) < 0.001)
                continue;
            int x = beatToPixel(beat);
            if (x >= left && x <= right)
                g.drawVerticalLine(x, top, bottom);
        }
    }

    // Pass 2: Beat lines (always visible)
    g.setColour(juce::Colour(0xFF585858));
    for (int b = 1; b <= static_cast<int>(lengthBeats); b++) {
        // Skip bar boundaries (drawn in pass 3)
        if (b % tsNum == 0)
            continue;
        int x = beatToPixel(static_cast<double>(b));
        if (x >= left && x <= right)
            g.drawVerticalLine(x, top, bottom);
    }

    // Pass 3: Bar lines (brightest, always visible, drawn last)
    g.setColour(juce::Colour(0xFF707070));
    for (int bar = 0; bar * tsNum <= static_cast<int>(lengthBeats); bar++) {
        int x = beatToPixel(static_cast<double>(bar * tsNum));
        if (x >= left && x <= right)
            g.drawVerticalLine(x, top, bottom);
    }
}

void PianoRollGridComponent::resized() {
    updateNoteComponentBounds();
}

void PianoRollGridComponent::mouseDown(const juce::MouseEvent& e) {
    // Click on empty space - deselect all notes
    if (!e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        for (auto& noteComp : noteComponents_) {
            noteComp->setSelected(false);
        }
        selectedNoteIndex_ = -1;
    }
}

void PianoRollGridComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    // Double-click to add a new note
    if (selectedClipIds_.empty()) {
        return;
    }

    double beat = pixelToBeat(e.x);
    int noteNumber = yToNoteNumber(e.y);

    ClipId targetClipId = INVALID_CLIP_ID;

    if (relativeMode_ && selectedClipIds_.size() <= 1) {
        // Single clip relative mode: add to the primary selected clip
        targetClipId = clipId_;
    } else if (relativeMode_ && selectedClipIds_.size() > 1) {
        // Multi-clip relative mode: find which clip region the click falls in
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }

        auto& clipManager = ClipManager::getInstance();

        for (ClipId selectedClipId : selectedClipIds_) {
            const auto* clip = clipManager.getClip(selectedClipId);
            if (!clip)
                continue;

            double clipOffsetBeats = clip->startTime * (tempo / 60.0) - clipStartBeats_;
            double clipEndRelBeats = clipOffsetBeats + clip->length * (tempo / 60.0);

            if (beat >= clipOffsetBeats && beat < clipEndRelBeats) {
                targetClipId = selectedClipId;
                // Convert to clip-relative beat
                beat = beat - clipOffsetBeats;
                break;
            }
        }

        if (targetClipId == INVALID_CLIP_ID) {
            targetClipId = clipId_;
        }
    } else {
        // Absolute mode: find which selected clip contains this beat
        // Get tempo for beat conversion
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }

        double timeSeconds = beat / (tempo / 60.0);
        auto& clipManager = ClipManager::getInstance();

        // Find selected clip at this position
        for (ClipId selectedClipId : selectedClipIds_) {
            const auto* clip = clipManager.getClip(selectedClipId);
            if (!clip) {
                continue;
            }

            if (timeSeconds >= clip->startTime && timeSeconds < (clip->startTime + clip->length)) {
                targetClipId = selectedClipId;
                break;
            }
        }

        // If no selected clip at this position, use the primary selected clip
        if (targetClipId == INVALID_CLIP_ID) {
            targetClipId = clipId_;
        }

        // Convert absolute beat to clip-relative beat
        const auto* clip = clipManager.getClip(targetClipId);
        if (!clip) {
            return;
        }

        double clipStartBeats = clip->startTime * (tempo / 60.0);
        beat = beat - clipStartBeats;
    }

    // Snap to grid
    beat = snapBeatToGrid(beat);

    // Ensure beat is not negative (before clip start)
    beat = juce::jmax(0.0, beat);

    // Clamp note number
    noteNumber = juce::jlimit(MIN_NOTE, MAX_NOTE, noteNumber);

    if (onNoteAdded && targetClipId != INVALID_CLIP_ID) {
        int defaultVelocity = 100;
        onNoteAdded(targetClipId, beat, noteNumber, defaultVelocity);
    }
}

bool PianoRollGridComponent::keyPressed(const juce::KeyPress& key) {
    // Delete key removes selected note
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (selectedNoteIndex_ >= 0 && onNoteDeleted && clipId_ != INVALID_CLIP_ID) {
            onNoteDeleted(clipId_, static_cast<size_t>(selectedNoteIndex_));
            selectedNoteIndex_ = -1;
            return true;
        }
    }

    return false;
}

void PianoRollGridComponent::setClip(ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        selectedClipIds_ = {clipId};
        clipIds_ = {clipId};

        // Get track ID from clip
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        trackId_ = clip ? clip->trackId : INVALID_TRACK_ID;

        refreshNotes();
    }
}

void PianoRollGridComponent::setClips(TrackId trackId, const std::vector<ClipId>& selectedClipIds,
                                      const std::vector<ClipId>& allClipIds) {
    bool needsRefresh =
        (trackId_ != trackId || selectedClipIds_ != selectedClipIds || clipIds_ != allClipIds);

    trackId_ = trackId;
    selectedClipIds_ = selectedClipIds;  // Clips selected for editing
    clipId_ = selectedClipIds.empty() ? INVALID_CLIP_ID : selectedClipIds[0];  // Primary selection
    clipIds_ = allClipIds;  // All clips to display

    DBG("PianoRollGrid::setClips - Selected: " << selectedClipIds.size()
                                               << ", All: " << allClipIds.size());

    if (needsRefresh) {
        refreshNotes();
    }
}

void PianoRollGridComponent::setPixelsPerBeat(double ppb) {
    if (pixelsPerBeat_ != ppb) {
        pixelsPerBeat_ = ppb;
        updateNoteComponentBounds();
        repaint();
    }
}

void PianoRollGridComponent::setNoteHeight(int height) {
    if (noteHeight_ != height) {
        noteHeight_ = height;
        updateNoteComponentBounds();
        repaint();
    }
}

void PianoRollGridComponent::setGridResolutionBeats(double beats) {
    if (gridResolutionBeats_ != beats) {
        gridResolutionBeats_ = beats;
        repaint();
    }
}

void PianoRollGridComponent::setSnapEnabled(bool enabled) {
    snapEnabled_ = enabled;
}

void PianoRollGridComponent::setTimeSignatureNumerator(int numerator) {
    if (timeSignatureNumerator_ != numerator) {
        timeSignatureNumerator_ = numerator;
        repaint();
    }
}

int PianoRollGridComponent::beatToPixel(double beat) const {
    return static_cast<int>(beat * pixelsPerBeat_) + leftPadding_;
}

double PianoRollGridComponent::pixelToBeat(int x) const {
    return (x - leftPadding_) / pixelsPerBeat_;
}

void PianoRollGridComponent::setLeftPadding(int padding) {
    if (leftPadding_ != padding) {
        leftPadding_ = padding;
        updateNoteComponentBounds();
        repaint();
    }
}

void PianoRollGridComponent::setClipStartBeats(double startBeats) {
    if (clipStartBeats_ != startBeats) {
        clipStartBeats_ = startBeats;
        updateNoteComponentBounds();
        repaint();
    }
}

void PianoRollGridComponent::setClipLengthBeats(double lengthBeats) {
    if (clipLengthBeats_ != lengthBeats) {
        clipLengthBeats_ = lengthBeats;
        repaint();
    }
}

void PianoRollGridComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        updateNoteComponentBounds();
        repaint();
    }
}

void PianoRollGridComponent::setTimelineLengthBeats(double lengthBeats) {
    if (timelineLengthBeats_ != lengthBeats) {
        timelineLengthBeats_ = lengthBeats;
        repaint();
    }
}

int PianoRollGridComponent::noteNumberToY(int noteNumber) const {
    return (MAX_NOTE - noteNumber) * noteHeight_;
}

int PianoRollGridComponent::yToNoteNumber(int y) const {
    int note = MAX_NOTE - (y / noteHeight_);
    return juce::jlimit(MIN_NOTE, MAX_NOTE, note);
}

void PianoRollGridComponent::updateNotePosition(NoteComponent* note, double beat, int noteNumber,
                                                double length) {
    if (!note)
        return;

    double displayBeat;
    if (relativeMode_) {
        // For multi-clip, offset by the clip's distance from the earliest clip
        if (clipIds_.size() > 1) {
            ClipId clipId = note->getSourceClipId();
            const auto* clip = ClipManager::getInstance().getClip(clipId);
            if (clip) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                double clipOffsetBeats = clip->startTime * (tempo / 60.0) - clipStartBeats_;
                displayBeat = clipOffsetBeats + beat;
            } else {
                displayBeat = beat;
            }
        } else {
            displayBeat = beat;
        }
    } else {
        // In ABS mode, use the note's own clip start position (not the grid-wide clipStartBeats_)
        ClipId clipId = note->getSourceClipId();
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double clipStartBeats = clip->startTime * (tempo / 60.0);
            double offset =
                (clip->view == ClipView::Session || clip->loopEnabled) ? clip->midiOffset : 0.0;
            displayBeat = clipStartBeats + beat - offset;
        } else {
            displayBeat = clipStartBeats_ + beat;
        }
    }

    int x = beatToPixel(displayBeat);
    int y = noteNumberToY(noteNumber);
    int width = juce::jmax(8, static_cast<int>(length * pixelsPerBeat_));
    int height = noteHeight_ - 2;  // Small gap between notes

    note->setBounds(x, y + 1, width, height);
}

void PianoRollGridComponent::refreshNotes() {
    clearNoteComponents();

    if (clipId_ == INVALID_CLIP_ID) {
        repaint();
        return;
    }

    createNoteComponents();
    updateNoteComponentBounds();
    repaint();
}

void PianoRollGridComponent::clipPropertyChanged(ClipId clipId) {
    // Update if this is one of our clips
    bool isOurClip = false;
    for (ClipId id : clipIds_) {
        if (id == clipId) {
            isOurClip = true;
            break;
        }
    }

    if (isOurClip) {
        // Defer refresh asynchronously to avoid destroying NoteComponents
        // while their mouse handlers are still executing (use-after-free crash)
        juce::Component::SafePointer<PianoRollGridComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent()) {
                self->refreshNotes();
            }
        });
    }
}

double PianoRollGridComponent::snapBeatToGrid(double beat) const {
    if (!snapEnabled_ || gridResolutionBeats_ <= 0.0) {
        return beat;
    }
    return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
}

void PianoRollGridComponent::createNoteComponents() {
    auto& clipManager = ClipManager::getInstance();

    DBG("createNoteComponents: clipIds_.size()=" << clipIds_.size() << ", selectedClipIds_.size()="
                                                 << selectedClipIds_.size());

    // Iterate through all clips
    for (ClipId clipId : clipIds_) {
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || clip->type != ClipType::MIDI) {
            DBG("  Skipping clip " << clipId << " (null or not MIDI)");
            continue;
        }

        juce::Colour noteColour = getColourForClip(clipId);

        DBG("  Creating notes for clip " << clipId << ": " << clip->midiNotes.size() << " notes");

        // Create note component for each note in this clip
        for (size_t i = 0; i < clip->midiNotes.size(); i++) {
            auto noteComp = std::make_unique<NoteComponent>(i, this, clipId);

            noteComp->onNoteSelected = [this, clipId](size_t index) {
                // Deselect other notes
                for (auto& nc : noteComponents_) {
                    if (nc->getSourceClipId() != clipId || nc->getNoteIndex() != index) {
                        nc->setSelected(false);
                    }
                }
                selectedNoteIndex_ = static_cast<int>(index);

                if (onNoteSelected) {
                    onNoteSelected(clipId, index);
                }
            };

            noteComp->onNoteMoved = [this, clipId](size_t index, double newBeat,
                                                   int newNoteNumber) {
                if (onNoteMoved) {
                    onNoteMoved(clipId, index, newBeat, newNoteNumber);
                }
            };

            noteComp->onNoteResized = [this, clipId](size_t index, double newLength,
                                                     bool fromStart) {
                (void)fromStart;  // Length change is already computed
                if (onNoteResized) {
                    onNoteResized(clipId, index, newLength);
                }
            };

            noteComp->onNoteDeleted = [this, clipId](size_t index) {
                if (onNoteDeleted) {
                    onNoteDeleted(clipId, index);
                    selectedNoteIndex_ = -1;
                }
            };

            noteComp->onNoteDragging = [this, clipId](size_t index, double previewBeat,
                                                      bool isDragging) {
                if (onNoteDragging) {
                    onNoteDragging(clipId, index, previewBeat, isDragging);
                }
            };

            noteComp->snapBeatToGrid = [this](double beat) { return snapBeatToGrid(beat); };

            noteComp->setGhost(!isClipSelected(clipId));
            noteComp->updateFromNote(clip->midiNotes[i], noteColour);
            addAndMakeVisible(noteComp.get());
            noteComponents_.push_back(std::move(noteComp));
        }
    }
}

void PianoRollGridComponent::clearNoteComponents() {
    for (auto& noteComp : noteComponents_) {
        removeChildComponent(noteComp.get());
    }
    noteComponents_.clear();
    selectedNoteIndex_ = -1;
}

void PianoRollGridComponent::updateNoteComponentBounds() {
    auto& clipManager = ClipManager::getInstance();

    for (auto& noteComp : noteComponents_) {
        ClipId clipId = noteComp->getSourceClipId();
        size_t noteIndex = noteComp->getNoteIndex();

        const auto* clip = clipManager.getClip(clipId);
        if (!clip || noteIndex >= clip->midiNotes.size()) {
            continue;
        }

        const auto& note = clip->midiNotes[noteIndex];

        // Calculate display position
        double displayBeat;
        if (relativeMode_) {
            // Relative: note at its clip-relative position
            // For multi-clip, offset by the clip's distance from the earliest clip
            if (clipIds_.size() > 1) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                double clipOffsetBeats = clip->startTime * (tempo / 60.0) - clipStartBeats_;
                displayBeat = clipOffsetBeats + note.startBeat;
            } else {
                displayBeat = note.startBeat;
            }
        } else {
            // Absolute: convert to timeline position
            // Get tempo from TimelineController
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double clipStartBeats = clip->startTime * (tempo / 60.0);
            double offset =
                (clip->view == ClipView::Session || clip->loopEnabled) ? clip->midiOffset : 0.0;
            displayBeat = clipStartBeats + note.startBeat - offset;
        }

        int x = beatToPixel(displayBeat);
        int y = noteNumberToY(note.noteNumber);
        int width = juce::jmax(8, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
        int height = noteHeight_ - 2;

        noteComp->setBounds(x, y + 1, width, height);

        // Determine note colour based on editability and state
        juce::Colour noteColour = getColourForClip(clipId);

        noteComp->setGhost(!isClipSelected(clipId));
        noteComp->updateFromNote(note, noteColour);
        noteComp->setVisible(true);
    }
}

bool PianoRollGridComponent::isBlackKey(int noteNumber) const {
    int note = noteNumber % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

juce::Colour PianoRollGridComponent::getClipColour() const {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? clip->colour : juce::Colour(0xFF6688CC);
}

juce::Colour PianoRollGridComponent::getColourForClip(ClipId clipId) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        return juce::Colours::grey;
    }

    // Use clip's color, but slightly desaturated for multi-clip view
    if (clipIds_.size() == 1) {
        return clip->colour;
    } else {
        return clip->colour.withSaturation(0.7f);
    }
}

bool PianoRollGridComponent::isClipSelected(ClipId clipId) const {
    return std::find(selectedClipIds_.begin(), selectedClipIds_.end(), clipId) !=
           selectedClipIds_.end();
}

void PianoRollGridComponent::setLoopRegion(double offsetBeats, double lengthBeats, bool enabled) {
    loopOffsetBeats_ = offsetBeats;
    loopLengthBeats_ = lengthBeats;
    loopEnabled_ = enabled;
    repaint();
}

void PianoRollGridComponent::setPlayheadPosition(double positionSeconds) {
    if (playheadPosition_ != positionSeconds) {
        playheadPosition_ = positionSeconds;
        repaint();
    }
}

}  // namespace magda
