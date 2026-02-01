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

    // Draw clip boundary lines and dim out-of-bounds area
    if (!relativeMode_ && clipLengthBeats_ > 0) {
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
    double gridResolution = getGridResolutionBeats();

    for (double beat = 0; beat <= lengthBeats; beat += gridResolution) {
        int x = beatToPixel(beat);
        if (x < area.getX() || x > area.getRight()) {
            continue;
        }

        // Determine line style based on beat position
        bool isBar = (static_cast<int>(beat) % 4 == 0) && (beat == std::floor(beat));
        bool isBeat = (beat == std::floor(beat));

        if (isBar) {
            // Bar lines - brightest
            g.setColour(juce::Colour(0xFF707070));
        } else if (isBeat) {
            // Beat lines - medium
            g.setColour(juce::Colour(0xFF585858));
        } else {
            // Sub-beat lines - subtle
            g.setColour(juce::Colour(0xFF454545));
        }

        g.drawVerticalLine(x, static_cast<float>(area.getY()),
                           static_cast<float>(area.getBottom()));
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
    double beat = pixelToBeat(e.x);
    int noteNumber = yToNoteNumber(e.y);

    // In ABS mode, convert absolute beat to clip-relative beat
    if (!relativeMode_) {
        beat = beat - clipStartBeats_;
    }

    // Snap to grid
    beat = snapBeatToGrid(beat);

    // Ensure beat is not negative (before clip start)
    beat = juce::jmax(0.0, beat);

    // Clamp note number
    noteNumber = juce::jlimit(MIN_NOTE, MAX_NOTE, noteNumber);

    if (onNoteAdded && clipId_ != INVALID_CLIP_ID) {
        int defaultVelocity = 100;
        onNoteAdded(clipId_, beat, noteNumber, defaultVelocity);
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

void PianoRollGridComponent::setGridResolution(GridResolution resolution) {
    gridResolution_ = resolution;
    repaint();
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

    // In ABS mode, offset by clip start position for display
    double displayBeat = relativeMode_ ? beat : (clipStartBeats_ + beat);

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
    // Only update if this is our clip
    if (clipId == clipId_) {
        updateNoteComponentBounds();
        repaint();
    }
}

double PianoRollGridComponent::snapBeatToGrid(double beat) const {
    double resolution = getGridResolutionBeats();
    if (resolution <= 0 || gridResolution_ == GridResolution::Off) {
        return beat;
    }
    return std::round(beat / resolution) * resolution;
}

double PianoRollGridComponent::getGridResolutionBeats() const {
    switch (gridResolution_) {
        case GridResolution::Off:
            return 0.0;
        case GridResolution::Bar:
            return 4.0;
        case GridResolution::Beat:
            return 1.0;
        case GridResolution::Eighth:
            return 0.5;
        case GridResolution::Sixteenth:
            return 0.25;
        case GridResolution::ThirtySecond:
            return 0.125;
    }
    return 0.25;  // Default to 1/16
}

void PianoRollGridComponent::createNoteComponents() {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    juce::Colour noteColour = clip->colour;

    for (size_t i = 0; i < clip->midiNotes.size(); i++) {
        auto noteComp = std::make_unique<NoteComponent>(i, this);

        // Set up callbacks
        noteComp->onNoteSelected = [this](size_t index) {
            // Deselect other notes
            for (auto& nc : noteComponents_) {
                if (nc->getNoteIndex() != index) {
                    nc->setSelected(false);
                }
            }
            selectedNoteIndex_ = static_cast<int>(index);

            if (onNoteSelected && clipId_ != INVALID_CLIP_ID) {
                onNoteSelected(clipId_, index);
            }
        };

        noteComp->onNoteMoved = [this](size_t index, double newBeat, int newNoteNumber) {
            if (onNoteMoved && clipId_ != INVALID_CLIP_ID) {
                onNoteMoved(clipId_, index, newBeat, newNoteNumber);
            }
        };

        noteComp->onNoteResized = [this](size_t index, double newLength, bool fromStart) {
            (void)fromStart;  // Length change is already computed
            if (onNoteResized && clipId_ != INVALID_CLIP_ID) {
                onNoteResized(clipId_, index, newLength);
            }
        };

        noteComp->onNoteDeleted = [this](size_t index) {
            if (onNoteDeleted && clipId_ != INVALID_CLIP_ID) {
                onNoteDeleted(clipId_, index);
                selectedNoteIndex_ = -1;
            }
        };

        noteComp->onNoteDragging = [this](size_t index, double previewBeat, bool isDragging) {
            if (onNoteDragging && clipId_ != INVALID_CLIP_ID) {
                onNoteDragging(clipId_, index, previewBeat, isDragging);
            }
        };

        noteComp->snapBeatToGrid = [this](double beat) { return snapBeatToGrid(beat); };

        noteComp->updateFromNote(clip->midiNotes[i], noteColour);
        addAndMakeVisible(noteComp.get());
        noteComponents_.push_back(std::move(noteComp));
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
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        return;
    }

    for (size_t i = 0; i < noteComponents_.size() && i < clip->midiNotes.size(); i++) {
        const auto& note = clip->midiNotes[i];

        // Notes are stored relative to clip start
        // In absolute mode, shift notes by offset so offset point appears at clip start
        double displayBeat =
            relativeMode_ ? note.startBeat : (clipStartBeats_ + note.startBeat - clip->midiOffset);

        int x = beatToPixel(displayBeat);
        int y = noteNumberToY(note.noteNumber);
        int width = juce::jmax(8, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
        int height = noteHeight_ - 2;

        noteComponents_[i]->setBounds(x, y + 1, width, height);

        // Notes before offset are greyed out
        bool isBeforeOffset = !relativeMode_ && (note.startBeat < clip->midiOffset);
        juce::Colour noteColour = isBeforeOffset ? clip->colour.withAlpha(0.3f) : clip->colour;

        noteComponents_[i]->updateFromNote(note, noteColour);
        noteComponents_[i]->setVisible(true);
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
