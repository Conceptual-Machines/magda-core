#include "PianoRollGridComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "core/ClipManager.hpp"

namespace magda {

PianoRollGridComponent::PianoRollGridComponent() {
    setName("PianoRollGrid");
    setWantsKeyboardFocus(true);
}

PianoRollGridComponent::~PianoRollGridComponent() {
    clearNoteComponents();
}

void PianoRollGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    paintGrid(g, bounds);
}

void PianoRollGridComponent::paintGrid(juce::Graphics& g, juce::Rectangle<int> area) {
    // Background - match the white key color from keyboard
    g.setColour(juce::Colour(0xFF3a3a3a));
    g.fillRect(area);

    // Get clip info for length
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    double lengthBeats = clip ? clip->length * 2.0 : 16.0;  // Approximate beats (assuming 120 BPM)

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

    // Snap to grid
    beat = snapBeatToGrid(beat);

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

    int x = beatToPixel(beat);
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
        int x = beatToPixel(note.startBeat);
        int y = noteNumberToY(note.noteNumber);
        int width = juce::jmax(8, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
        int height = noteHeight_ - 2;

        noteComponents_[i]->setBounds(x, y + 1, width, height);
        noteComponents_[i]->updateFromNote(note, clip->colour);
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

}  // namespace magda
