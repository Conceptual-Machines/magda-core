#include "PianoRollGridComponent.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../state/TimelineController.hpp"
#include "../../state/TimelineEvents.hpp"
#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../utils/SelectionPolicy.hpp"
#include "../../windows/CommandIDs.hpp"
#include "PhaseMarker.hpp"
#include "PitchFoldMap.hpp"
#include "core/ChordAnnotationCommands.hpp"
#include "core/ClipManager.hpp"
#include "core/GestureRouter.hpp"
#include "core/MidiChordMarkers.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {
// Clip start/end boundary marker (the vertical edge + soft fade where the
// editable clip region begins/ends). A muted slate reads as a frame edge that
// sits in the dark grid instead of glaring like the old near-white grey.
constexpr juce::uint32 kClipBoundaryColour = 0xFF6A7280;

double timelineStartBeats(const ClipInfo& clip, double bpm) {
    return clip.getStartBeats(bpm);
}

double timelineLengthBeats(const ClipInfo& clip, double bpm) {
    return clip.getLengthInBeats(bpm);
}

double timelineEndBeats(const ClipInfo& clip, double bpm) {
    return clip.getEndBeats(bpm);
}

double effectiveLoopStartBeats(const ClipInfo& clip, double bpm) {
    if (clip.loopStartBeats > 0.0)
        return clip.loopStartBeats;

    if (clip.loopStart > 0.0 && bpm > 0.0)
        return clip.loopStart * bpm / 60.0;

    return 0.0;
}

double effectiveLoopLengthBeats(const ClipInfo& clip, double bpm) {
    if (clip.loopLengthBeats > 0.0)
        return clip.loopLengthBeats;

    if (clip.loopLength > 0.0 && bpm > 0.0)
        return clip.loopLength * bpm / 60.0;

    return timelineLengthBeats(clip, bpm);
}

// Grid clicks use the same relative-loop contract as the ruler: display beat is
// loop phase, and the global target stays in the current playhead cycle.
double globalBeatForRelativeLoopClick(double displayBeat, double currentGlobalBeat,
                                      const ClipInfo& clip, double bpm) {
    const double clipStart = timelineStartBeats(clip, bpm);
    const double loopStart = effectiveLoopStartBeats(clip, bpm);
    const double loopLength = effectiveLoopLengthBeats(clip, bpm);
    const double phase = wrapPhase(displayBeat - loopStart, loopLength);
    const double currentElapsed = currentGlobalBeat - clipStart;
    const double cycle =
        currentElapsed >= loopStart ? std::floor((currentElapsed - loopStart) / loopLength) : 0.0;

    double target = clipStart + loopStart + cycle * loopLength + phase;

    const double clipEnd = timelineEndBeats(clip, bpm);
    while (target < clipStart)
        target += loopLength;
    while (target > clipEnd)
        target -= loopLength;

    return juce::jlimit(clipStart, clipEnd, target);
}
}  // namespace

PianoRollGridComponent::PianoRollGridComponent() {
    setName("PianoRollGrid");
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    ClipManager::getInstance().addListener(this);
}

PianoRollGridComponent::~PianoRollGridComponent() {
    ClipManager::getInstance().removeListener(this);
    clearNoteComponents();
}

void PianoRollGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    paintGrid(g, bounds);

    // Ghost notes from overlay tracks, under everything else (#1281)
    paintOverlayNotes(g);

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

            double clipStartBeats = timelineStartBeats(*clip, tempo);
            double clipEndBeats = timelineEndBeats(*clip, tempo);

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
            g.setColour(juce::Colour(kClipBoundaryColour));
            g.fillRect(clipStartX - 1, 0, 2, bounds.getHeight());
        }

        // Dim area before clip start
        if (clipStartX > bounds.getX()) {
            g.setColour(juce::Colour(0x60000000));
            g.fillRect(bounds.getX(), bounds.getY(), clipStartX - bounds.getX(),
                       bounds.getHeight());
        }

        // Clip end boundary and dimming (only for non-looped clips)
        if (!loopEnabled_) {
            int clipEndX = beatToPixel(clipStartBeats_ + clipLengthBeats_);
            if (clipEndX >= 0 && clipEndX <= bounds.getRight()) {
                g.setColour(juce::Colour(kClipBoundaryColour));
                g.fillRect(clipEndX - 1, 0, 2, bounds.getHeight());
            }

            if (clipEndX < bounds.getRight()) {
                g.setColour(juce::Colour(0x60000000));
                g.fillRect(clipEndX, bounds.getY(), bounds.getRight() - clipEndX,
                           bounds.getHeight());
            }
        }
    } else if (clipLengthBeats_ > 0) {
        // In relative mode, just show end boundary at clip length (non-looped only)
        if (!loopEnabled_) {
            int clipEndX = beatToPixel(clipLengthBeats_);
            if (clipEndX >= 0 && clipEndX <= bounds.getRight()) {
                g.setColour(juce::Colour(kClipBoundaryColour));
                g.fillRect(clipEndX - 1, 0, 2, bounds.getHeight());
            }

            if (clipEndX < bounds.getRight()) {
                g.setColour(juce::Colour(0x60000000));
                g.fillRect(clipEndX, bounds.getY(), bounds.getRight() - clipEndX,
                           bounds.getHeight());
            }
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

    // Draw loop phase marker (yellow vertical line)
    if (clipIds_.size() <= 1 && clipId_ != INVALID_CLIP_ID) {
        const auto* phaseClip = ClipManager::getInstance().getClip(clipId_);
        if (phaseClip && phaseClip->loopEnabled) {
            double offset = phasePreviewActive_ ? phasePreviewBeats_ : phaseClip->midiOffset;
            double phaseBeat = relativeMode_ ? offset : (clipStartBeats_ + offset);
            int phaseX = beatToPixel(phaseBeat);
            if (phaseX >= 0 && phaseX <= bounds.getRight()) {
                paintPhaseMarker(g, phaseClip, phaseX, bounds.getHeight(), nearPhaseMarker_,
                                 phasePreviewActive_);
            }
        }
    }

    // Draw copy drag ghost preview
    for (const auto& ghost : copyDragGhosts_) {
        int gx = beatToPixel(ghost.beat);
        int gy = noteNumberToY(ghost.noteNumber);
        int gw = juce::jmax(8, static_cast<int>(ghost.length * pixelsPerBeat_));
        int gh = noteHeight_ - 2;

        g.setColour(ghost.colour.withAlpha(0.35f));
        g.fillRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                               static_cast<float>(gw), static_cast<float>(gh), 2.0f);
        g.setColour(ghost.colour.withAlpha(0.6f));
        g.drawRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                               static_cast<float>(gw), static_cast<float>(gh), 2.0f, 1.0f);
    }

    // Draw chord drop preview (vertical line during DnD drag)
    if (chordDropActive_) {
        int lineX = beatToPixel(chordDropBeat_);
        g.setColour(juce::Colour(0xFF5599FF).withAlpha(0.8f));
        g.drawLine(float(lineX), 0.f, float(lineX), float(bounds.getHeight()), 2.0f);
    }

    // Draw pending chord placement preview (after drop, awaiting length confirmation)
    if (pendingChord_.active) {
        int startX = beatToPixel(pendingChord_.startBeat);
        int endX = beatToPixel(pendingChord_.previewEndBeat);

        // Draw the span region
        if (endX > startX) {
            g.setColour(juce::Colour(0xFF5599FF).withAlpha(0.12f));
            g.fillRect(startX, 0, endX - startX, bounds.getHeight());
        }

        // Draw blinking start line
        float alpha = pendingChord_.blinkOn ? 0.9f : 0.3f;
        g.setColour(juce::Colour(0xFF5599FF).withAlpha(alpha));
        g.drawLine(float(startX), 0.f, float(startX), float(bounds.getHeight()), 2.0f);

        // Draw end line at mouse position
        if (endX > startX + 2) {
            g.setColour(juce::Colour(0xFF5599FF).withAlpha(0.5f));
            g.drawLine(float(endX), 0.f, float(endX), float(bounds.getHeight()), 1.0f);
        }
    }

    // Draw Shift-drag note creation preview
    if (isDrawingNote_ && drawingNoteClipId_ != INVALID_CLIP_ID) {
        const double startBeat = std::min(drawingNoteStartBeat_, drawingNoteEndBeat_);
        const double endBeat = std::max(drawingNoteStartBeat_, drawingNoteEndBeat_);
        const double length = juce::jmax(getDefaultNoteLengthBeats(), endBeat - startBeat);

        MidiNote previewNote;
        previewNote.startBeat = startBeat;
        previewNote.noteNumber = drawingNoteNumber_;
        previewNote.lengthBeats = length;
        if (const auto* clip = ClipManager::getInstance().getClip(drawingNoteClipId_);
            clip != nullptr &&
            ClipOperations::constrainMidiNoteToVisibleRange(*clip, previewNote)) {
            const int x =
                beatToPixel(displayBeatForClipBeat(drawingNoteClipId_, previewNote.startBeat));
            const int y = noteNumberToY(previewNote.noteNumber);
            const int w = juce::jmax(8, static_cast<int>(previewNote.lengthBeats * pixelsPerBeat_));
            const int h = noteHeight_ - 2;
            const auto colour = getColourForClip(drawingNoteClipId_);

            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                   static_cast<float>(w), static_cast<float>(h), 2.0f);
            g.setColour(colour.withAlpha(0.8f));
            g.drawRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                   static_cast<float>(w), static_cast<float>(h), 2.0f, 1.0f);
        }
    }

    // Draw edit cursor line (blinking white)
    if (editCursorPosition_ >= 0.0 && editCursorVisible_) {
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double cursorBeats = editCursorPosition_ / secondsPerBeat;
        double displayBeat = relativeMode_ ? (cursorBeats - clipStartBeats_) : cursorBeats;
        int cursorX = beatToPixel(displayBeat);
        if (cursorX >= 0 && cursorX <= bounds.getRight()) {
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawLine(float(cursorX - 1), 0.f, float(cursorX - 1), float(bounds.getHeight()), 1.f);
            g.drawLine(float(cursorX + 1), 0.f, float(cursorX + 1), float(bounds.getHeight()), 1.f);
            g.setColour(juce::Colours::white);
            g.drawLine(float(cursorX), 0.f, float(cursorX), float(bounds.getHeight()), 2.f);
        }
    }

    // Draw playhead line if playing
    {
        int playheadX = 0;
        if (getPlayheadDisplayX(playheadX) && playheadX >= 0 && playheadX <= bounds.getRight()) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            g.fillRect(playheadX - 1, 0, 2, bounds.getHeight());
        }
    }

    // Draw rubber band selection rectangle
    if (isDragSelecting_) {
        auto selectionRect = juce::Rectangle<int>(dragSelectStart_, dragSelectEnd_).toFloat();
        g.setColour(juce::Colour(0x306688CC));
        g.fillRect(selectionRect);
        g.setColour(juce::Colour(0xAA6688CC));
        g.drawRect(selectionRect, 1.0f);
    }
}

void PianoRollGridComponent::setOverlayTracks(std::vector<TrackId> trackIds) {
    overlayTrackIds_ = std::move(trackIds);
    repaint();
}

void PianoRollGridComponent::setOverlayNotes(std::vector<MidiNote> notes, juce::Colour colour) {
    overlayNotes_ = std::move(notes);
    overlayNotesColour_ = colour;
    repaint();
}

void PianoRollGridComponent::clearOverlayNotes() {
    if (overlayNotes_.empty())
        return;
    overlayNotes_.clear();
    repaint();
}

void PianoRollGridComponent::paintOverlayNoteSet(juce::Graphics& g) {
    if (overlayNotes_.empty())
        return;
    const auto visibleArea = g.getClipBounds();
    for (const auto& note : overlayNotes_) {
        // Comp-take notes are relative to the clip; place them like the active
        // notes (content-relative beats, shifted by clipStartBeats_ in absolute
        // mode).
        const double displayBeat =
            relativeMode_ ? note.startBeat : (clipStartBeats_ + note.startBeat);
        const int x = beatToPixel(displayBeat);
        const int w = juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
        if (x + w < visibleArea.getX() || x > visibleArea.getRight())
            continue;
        if (foldMap_ && foldMap_->isActive() &&
            foldMap_->noteForRow(foldMap_->rowForNote(note.noteNumber)) != note.noteNumber)
            continue;
        const int y = noteNumberToY(note.noteNumber);
        if (y + noteHeight_ < visibleArea.getY() || y > visibleArea.getBottom())
            continue;
        const auto rect =
            juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y + 1),
                                   static_cast<float>(w), static_cast<float>(noteHeight_ - 2));
        g.setColour(overlayNotesColour_.withAlpha(0.22f));
        g.fillRoundedRectangle(rect, 2.0f);
        g.setColour(overlayNotesColour_.withAlpha(0.5f));
        g.drawRoundedRectangle(rect, 2.0f, 1.0f);
    }
}

void PianoRollGridComponent::paintOverlayNotes(juce::Graphics& g) {
    paintOverlayNoteSet(g);

    if (overlayTrackIds_.empty())
        return;

    auto& clipManager = ClipManager::getInstance();

    double tempo = 120.0;
    if (auto* controller = TimelineController::getCurrent())
        tempo = controller->getState().tempo.bpm;

    const auto visibleArea = g.getClipBounds();

    for (TrackId overlayTrackId : overlayTrackIds_) {
        // The edited track's own clips are already rendered as note components
        if (overlayTrackId == trackId_)
            continue;

        for (ClipId overlayClipId : clipManager.getClipsOnTrack(overlayTrackId)) {
            const auto* clip = clipManager.getClip(overlayClipId);
            if (!clip || !clip->isMidi())
                continue;

            // Ghost notes take the source clip's colour
            const auto colour = clip->colour;

            // Same display math as updateNoteComponentBounds()' multi-clip
            // path: position notes on the shared timeline, shifted by the
            // edited clip's start in relative mode.
            const double visibleStart = ClipOperations::getMidiVisibleRange(*clip).startBeat;
            double clipOffsetBeats = timelineStartBeats(*clip, tempo);
            if (relativeMode_)
                clipOffsetBeats -= clipStartBeats_;

            for (auto note : clip->midiNotes) {
                if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, note))
                    continue;

                // When folded the axis only has rows for the edited clip's own
                // pitches; ghost notes on other pitches have no row, so hide
                // them rather than snapping them onto an unrelated row.
                if (foldMap_ && foldMap_->isActive() &&
                    foldMap_->noteForRow(foldMap_->rowForNote(note.noteNumber)) != note.noteNumber)
                    continue;

                const double displayBeat = clipOffsetBeats + note.startBeat - visibleStart;
                const int x = beatToPixel(displayBeat);
                const int w = juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
                if (x + w < visibleArea.getX() || x > visibleArea.getRight())
                    continue;

                const int y = noteNumberToY(note.noteNumber);
                if (y + noteHeight_ < visibleArea.getY() || y > visibleArea.getBottom())
                    continue;

                const auto rect = juce::Rectangle<float>(
                    static_cast<float>(x), static_cast<float>(y + 1), static_cast<float>(w),
                    static_cast<float>(noteHeight_ - 2));
                g.setColour(colour.withAlpha(0.22f));
                g.fillRoundedRectangle(rect, 2.0f);
                g.setColour(colour.withAlpha(0.45f));
                g.drawRoundedRectangle(rect, 2.0f, 1.0f);
            }
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

    // Draw row backgrounds - alternate for black/white keys (only in grid area).
    // Iterate rows (not pitches) so folded mode draws exactly the visible rows.
    const int rows = foldRowCount();
    for (int row = 0; row < rows; row++) {
        int y = row * noteHeight_;

        if (y + noteHeight_ < area.getY() || y > area.getBottom()) {
            continue;
        }

        // Black key rows are darker
        if (isBlackKey(noteForRow(row))) {
            g.setColour(juce::Colour(0xFF2a2a2a));
            g.fillRect(gridArea.getX(), y, gridArea.getWidth(), noteHeight_);
        }
    }

    if (!selectedPitchRows_.empty()) {
        g.setColour(juce::Colour(0x556688CC));
        for (int note : selectedPitchRows_) {
            int y = noteNumberToY(note);
            if (y + noteHeight_ < area.getY() || y > area.getBottom())
                continue;
            g.fillRect(gridArea.getX(), y, gridArea.getWidth(), noteHeight_);
        }
    }

    // Fill left padding area with solid panel background (covers the alternating rows)
    if (leftPadding_ > 0) {
        g.setColour(DarkTheme::getPanelBackgroundColour());
        g.fillRect(area.getX(), area.getY(), leftPadding_, area.getHeight());
    }

    // Draw horizontal grid lines at each row boundary (at bottom of each row, -1 to match
    // keyboard)
    g.setColour(juce::Colour(0xFF505050));
    for (int row = 0; row < rows; row++) {
        int y = row * noteHeight_ + noteHeight_ - 1;
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

void PianoRollGridComponent::paintOverChildren(juce::Graphics& g) {
    paintPitchExpression(g);
}

void PianoRollGridComponent::resized() {
    updateNoteComponentBounds();
}

void PianoRollGridComponent::mouseDown(const juce::MouseEvent& e) {
    // Pitch glide overlay editing takes priority when active
    if (pitchExpressionMode_ && handleExpressionMouseDown(e))
        return;

    // If pending chord is active, click confirms the length
    if (pendingChord_.active) {
        if (e.mods.isPopupMenu()) {
            cancelPendingChord();
            return;
        }
        double beat = pixelToBeat(e.x);
        if (snapEnabled_)
            beat = snapBeatToGrid(beat);
        // Only accept clicks to the right of the start position
        if (beat > pendingChord_.startBeat)
            confirmPendingChord(beat);
        else
            cancelPendingChord();
        return;
    }

    isEditCursorClick_ = false;
    isPendingPlayheadClick_ = false;

    // Right-click context menu
    if (e.mods.isPopupMenu()) {
        // Collect selected note indices
        std::vector<size_t> selectedIndices;
        for (const auto& nc : noteComponents_) {
            if (nc->isSelected()) {
                selectedIndices.push_back(nc->getNoteIndex());
            }
        }

        if (clipId_ != INVALID_CLIP_ID) {
            juce::PopupMenu menu;
            bool hasSelection = !selectedIndices.empty();

            // Edit operations
            menu.addItem(10, "Copy", hasSelection);
            menu.addItem(11, "Paste", ClipManager::getInstance().hasNotesInClipboard());
            menu.addItem(12, "Duplicate", hasSelection);
            menu.addItem(13, "Delete", hasSelection);
            menu.addSeparator();
            addDefaultNoteMenuItems(menu);
            menu.addSeparator();

            // Quantize submenu
            {
                juce::PopupMenu quantizeMenu;

                // Current Grid (IDs 1-3)
                {
                    juce::PopupMenu modeMenu;
                    modeMenu.addItem(1, "Start");
                    modeMenu.addItem(2, "Length");
                    modeMenu.addItem(3, "Start & Length");
                    quantizeMenu.addSubMenu("Current Grid", modeMenu,
                                            hasSelection && gridResolutionBeats_ > 0.0);
                }
                quantizeMenu.addSeparator();

                // Fixed grid values
                struct GridOption {
                    const char* name;
                    double beats;
                };
                // clang-format off
                const GridOption grids[] = {
                    {"1/1",   4.0},    {"1/2",   2.0},    {"1/4",   1.0},
                    {"1/8",   0.5},    {"1/16",  0.25},   {"1/32",  0.125},
                    {"1/2.",  3.0},    {"1/4.",  1.5},
                    {"1/8.",  0.75},   {"1/16.", 0.375},
                    {"1/2T",  4.0/3},  {"1/4T",  2.0/3},
                    {"1/8T",  1.0/3},  {"1/16T", 1.0/6},
                };
                // clang-format on

                int itemId = 20;  // IDs 20-61 (14 grids x 3 modes)
                for (const auto& grid : grids) {
                    juce::PopupMenu modeMenu;
                    modeMenu.addItem(itemId++, "Start");
                    modeMenu.addItem(itemId++, "Length");
                    modeMenu.addItem(itemId++, "Start & Length");
                    quantizeMenu.addSubMenu(grid.name, modeMenu, hasSelection);
                }
                menu.addSubMenu("Quantize", quantizeMenu, hasSelection);
            }

            menu.showMenuAsync(juce::PopupMenu::Options(), [this,
                                                            indices = std::move(selectedIndices),
                                                            gridRes =
                                                                gridResolutionBeats_](int result) {
                if (result == 0)
                    return;
                if (result == 10 && onCopyNotes)
                    onCopyNotes(clipId_, indices);
                else if (result == 11 && onPasteNotes)
                    onPasteNotes(clipId_);
                else if (result == 12 && onDuplicateNotes)
                    onDuplicateNotes(clipId_, indices);
                else if (result == 13 && onDeleteNotes)
                    onDeleteNotes(clipId_, indices);
                else if (handleDefaultNoteMenuResult(result))
                    return;
                else if (result >= 1 && result <= 3 && onQuantizeNotes) {
                    // Current Grid
                    const QuantizeMode modes[] = {QuantizeMode::StartOnly, QuantizeMode::LengthOnly,
                                                  QuantizeMode::StartAndLength};
                    onQuantizeNotes(clipId_, indices, modes[result - 1], gridRes);
                } else if (result >= 20 && result <= 61 && onQuantizeNotes) {
                    // clang-format off
                        const double gridBeats[] = {
                            4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                            3.0, 1.5, 0.75, 0.375,
                            4.0/3, 2.0/3, 1.0/3, 1.0/6,
                        };
                    // clang-format on
                    const QuantizeMode modes[] = {QuantizeMode::StartOnly, QuantizeMode::LengthOnly,
                                                  QuantizeMode::StartAndLength};
                    int offset = result - 20;
                    onQuantizeNotes(clipId_, indices, modes[offset % 3], gridBeats[offset / 3]);
                }
            });
        }
        return;
    }

    // Alt + click on a grid line -> set edit cursor
    if (e.mods.isAltDown() && isNearGridLine(e.x)) {
        isEditCursorClick_ = true;
        return;
    }

    if (e.mods.isShiftDown() && onNoteAdded) {
        auto insertPos = getNoteInsertPosition(e.getPosition());
        if (insertPos.has_value()) {
            const auto* clip = ClipManager::getInstance().getClip(insertPos->clipId);
            if (clip) {
                auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
                if (trackInfo && trackInfo->frozen)
                    return;
            }

            isDrawingNote_ = true;
            drawingNoteClipId_ = insertPos->clipId;
            drawingNoteStartBeat_ = insertPos->beat;
            drawingNoteEndBeat_ = insertPos->beat + getDefaultNoteLengthBeats();
            drawingNoteNumber_ = insertPos->noteNumber;
            setMouseCursor(CursorManager::getInstance().getNoteDrawCursor());
            repaint();
            return;
        }
    }

    // Store drag start point for potential rubber band selection
    dragSelectStart_ = e.getPosition();
    dragSelectEnd_ = e.getPosition();
    isDragSelecting_ = false;
    isPendingPlayheadClick_ =
        !e.mods.isShiftDown() && !e.mods.isCommandDown() && !e.mods.isAltDown();
    playheadClickStart_ = e.getPosition();
}

void PianoRollGridComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isExpressionDragging_) {
        handleExpressionMouseDrag(e);
        return;
    }

    if (isEditCursorClick_)
        return;

    if (isDrawingNote_) {
        drawingNoteEndBeat_ = clipBeatForDisplayX(drawingNoteClipId_, e.x);
        repaint();
        return;
    }

    const int deltaX = std::abs(e.x - playheadClickStart_.x);
    const int deltaY = std::abs(e.y - playheadClickStart_.y);
    if (juce::jmax(deltaX, deltaY) <= PLAYHEAD_CLICK_DRAG_THRESHOLD)
        return;

    isPendingPlayheadClick_ = false;
    isDragSelecting_ = true;
    dragSelectEnd_ = e.getPosition();
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    repaint();
}

void PianoRollGridComponent::mouseUp(const juce::MouseEvent& e) {
    if (isExpressionDragging_) {
        handleExpressionMouseUp(e);
        return;
    }

    if (isDrawingNote_) {
        const ClipId clipId = drawingNoteClipId_;
        MidiNote note;
        note.startBeat = std::min(drawingNoteStartBeat_, drawingNoteEndBeat_);
        note.noteNumber = drawingNoteNumber_;
        note.lengthBeats = juce::jmax(getDefaultNoteLengthBeats(),
                                      std::abs(drawingNoteEndBeat_ - drawingNoteStartBeat_));

        isDrawingNote_ = false;
        drawingNoteClipId_ = INVALID_CLIP_ID;

        if (onNoteAdded && clipId != INVALID_CLIP_ID) {
            const auto* clip = ClipManager::getInstance().getClip(clipId);
            if (clip && ClipOperations::clipMidiNoteToVisibleRange(*clip, note)) {
                rememberAddedNoteLength(note.lengthBeats);
                onNoteAdded(clipId, note.startBeat, note.noteNumber, note.lengthBeats,
                            defaultNoteVelocity_);
            }
        }

        updateEmptyGridCursor(e.mods, e.x);
        repaint();
        return;
    }

    // Don't deselect on right-click release (context menu was shown)
    if (e.mods.isPopupMenu()) {
        return;
    }

    // Grid line click -> set edit cursor position
    if (isEditCursorClick_) {
        isEditCursorClick_ = false;
        double gridBeat = getNearestGridLineBeat(e.x);

        // In relative mode, convert from relative beat to absolute beat
        double absoluteBeat = relativeMode_ ? (gridBeat + clipStartBeats_) : gridBeat;

        // Convert beats to seconds
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double positionSeconds = absoluteBeat * (60.0 / tempo);

        if (onEditCursorSet) {
            onEditCursorSet(positionSeconds);
        }
        return;
    }

    if (isPendingPlayheadClick_ && e.getNumberOfClicks() == 1) {
        const int deltaX = std::abs(e.x - playheadClickStart_.x);
        const int deltaY = std::abs(e.y - playheadClickStart_.y);
        if (juce::jmax(deltaX, deltaY) <= PLAYHEAD_CLICK_DRAG_THRESHOLD) {
            if (onPlayheadPositionBeatsChanged)
                onPlayheadPositionBeatsChanged(
                    absolutePlayheadBeatForDisplayX(playheadClickStart_.x));
        }
    }
    isPendingPlayheadClick_ = false;

    if (isDragSelecting_) {
        // Build normalized selection rectangle
        auto selectionRect = juce::Rectangle<int>(dragSelectStart_, dragSelectEnd_);

        bool isAdditive = magda::isAdditiveMarqueeDrag(e.mods);

        // If not additive, deselect all first
        if (!isAdditive) {
            for (auto& nc : noteComponents_) {
                nc->setSelected(false);
            }
            selectedNoteIndex_ = -1;
        }

        // Select notes whose bounds intersect the selection rectangle
        for (auto& nc : noteComponents_) {
            if (nc->getBounds().intersects(selectionRect)) {
                nc->setSelected(true);
            }
        }
        rebuildSelectedPitchRows();

        isDragSelecting_ = false;

        // Notify with all selected note indices
        if (onNoteSelectionChanged) {
            std::vector<size_t> selectedIndices;
            for (auto& nc : noteComponents_) {
                if (nc->isSelected()) {
                    selectedIndices.push_back(nc->getNoteIndex());
                }
            }
            onNoteSelectionChanged(clipId_, selectedIndices);
        }

        repaint();
        updateEmptyGridCursor(e.mods, e.x);
    } else {
        // Plain click on empty space — deselect all notes
        if (!e.mods.isCommandDown() && !e.mods.isShiftDown()) {
            for (auto& noteComp : noteComponents_) {
                noteComp->setSelected(false);
            }
            selectedNoteIndex_ = -1;
            rebuildSelectedPitchRows();

            if (onNoteSelectionChanged) {
                onNoteSelectionChanged(clipId_, {});
            }
        }
    }
}

void PianoRollGridComponent::mouseMove(const juce::MouseEvent& e) {
    // Track the glide point under the mouse for the pitch value label
    if (pitchExpressionMode_) {
        auto hit = hitTestExpressionPoint(e.getPosition());
        if (hit != hoveredExpressionPoint_) {
            hoveredExpressionPoint_ = hit;
            repaint();
        }
    } else if (hoveredExpressionPoint_) {
        hoveredExpressionPoint_.reset();
        repaint();
    }

    // Update pending chord preview end position
    if (pendingChord_.active) {
        auto localPos = e.getEventRelativeTo(this).getPosition();
        double beat = pixelToBeat(localPos.x);
        if (snapEnabled_)
            beat = snapBeatToGrid(beat);
        if (beat > pendingChord_.startBeat)
            pendingChord_.previewEndBeat = beat;
        else
            pendingChord_.previewEndBeat = pendingChord_.startBeat + gridResolutionBeats_;
        repaint();
        return;
    }

    // Get mouse position relative to this component (important for child-forwarded events)
    auto localPos = e.getEventRelativeTo(this).getPosition();

    updateEmptyGridCursor(e.mods, localPos.x);

    // Check proximity to phase marker for hover display
    bool wasNear = nearPhaseMarker_;
    nearPhaseMarker_ = false;

    if (clipIds_.size() <= 1 && clipId_ != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId_);
        double phaseBeat = relativeMode_ ? 0.0 : clipStartBeats_;
        int phaseX = beatToPixel(phaseBeat);
        nearPhaseMarker_ = isNearPhaseMarker(localPos.x, phaseX, clip);
    }

    if (nearPhaseMarker_ != wasNear) {
        repaint();
    }
}

void PianoRollGridComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    isPendingPlayheadClick_ = false;
    if (nearPhaseMarker_) {
        nearPhaseMarker_ = false;
        repaint();
    }
    if (hoveredExpressionPoint_) {
        hoveredExpressionPoint_.reset();
        repaint();
    }
}

void PianoRollGridComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (pitchExpressionMode_) {
        handleExpressionDoubleClick(e);
        return;  // never create notes while editing glides
    }

    // Block note creation on frozen tracks
    if (clipId_ != INVALID_CLIP_ID) {
        auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (clip) {
            auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
            if (trackInfo && trackInfo->frozen)
                return;
        }
    }

    auto insertPos = getNoteInsertPosition(e.getPosition());
    if (onNoteAdded && insertPos.has_value()) {
        MidiNote previewNote;
        previewNote.startBeat = insertPos->beat;
        previewNote.noteNumber = insertPos->noteNumber;
        previewNote.lengthBeats = getDefaultNoteLengthBeats();

        const auto* targetClip = ClipManager::getInstance().getClip(insertPos->clipId);
        if (targetClip != nullptr &&
            !ClipOperations::clipMidiNoteToVisibleRange(*targetClip, previewNote)) {
            return;
        }

        rememberAddedNoteLength(previewNote.lengthBeats);
        onNoteAdded(insertPos->clipId, previewNote.startBeat, previewNote.noteNumber,
                    previewNote.lengthBeats, defaultNoteVelocity_);
    }
}

void PianoRollGridComponent::mouseWheelMove(const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& wheel) {
    // Alt+wheel = vertical (lane height) zoom, resolved via GestureRouter so the
    // binding is configurable (#1350). The view's callback keeps its own zoom
    // magnitude math, so the gesture only selects the action. A plain wheel
    // falls through to the enclosing viewport for content scroll.
    const auto gesture = GestureRouter::getInstance().resolve(GestureContext::PianoRoll, wheel,
                                                              e.mods, e.getPosition());
    if (gesture.type == GestureActionType::ZoomVertical && onVerticalZoomRequested) {
        onVerticalZoomRequested(e.y, wheel);
        return;
    }

    juce::Component::mouseWheelMove(e, wheel);
}

juce::ApplicationCommandTarget* PianoRollGridComponent::getNextCommandTarget() {
    // Fall through to the global target (MainComponent) for everything this
    // view does not claim.
    return findParentComponentOfClass<juce::ApplicationCommandTarget>();
}

void PianoRollGridComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    // Context commands this view claims when focused. selectAll means "select
    // all notes in the edited clip" here, vs "select all clips" globally (#25).
    commands.add(CommandIDs::selectAll);
}

void PianoRollGridComponent::getCommandInfo(juce::CommandID commandID,
                                            juce::ApplicationCommandInfo& result) {
    if (commandID == CommandIDs::selectAll) {
        result.setInfo("Select All Notes", "Select every note in the edited clip", "Piano Roll", 0);
        result.setActive(clipId_ != INVALID_CLIP_ID);
    }
}

bool PianoRollGridComponent::perform(const InvocationInfo& info) {
    if (info.commandID != CommandIDs::selectAll)
        return false;

    if (clipId_ == INVALID_CLIP_ID)
        return false;

    std::vector<size_t> allIndices;
    for (auto& nc : noteComponents_) {
        if (nc->getSourceClipId() == clipId_) {
            nc->setSelected(true);
            allIndices.push_back(nc->getNoteIndex());
        }
    }
    rebuildSelectedPitchRows();

    if (onNoteSelectionChanged) {
        onNoteSelectionChanged(clipId_, allIndices);
    }
    repaint();
    return true;
}

bool PianoRollGridComponent::keyPressed(const juce::KeyPress& key) {
    // Handle pending chord confirmation/cancellation
    if (pendingChord_.active) {
        if (key.getKeyCode() == juce::KeyPress::returnKey) {
            confirmPendingChord(pendingChord_.previewEndBeat);
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) {
            cancelPendingChord();
            return true;
        }
    }

    // Cmd+A (select all notes) is now the selectAll command, handled in
    // perform() so it resolves to this view only when focused (#25).

    // Cmd+D: Duplicate selected notes (consume key to prevent clip duplication)
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D') {
        if (clipId_ == INVALID_CLIP_ID)
            return true;

        std::vector<size_t> selectedIndices;
        for (const auto& nc : noteComponents_) {
            if (nc->isSelected() && nc->getSourceClipId() == clipId_) {
                selectedIndices.push_back(nc->getNoteIndex());
            }
        }

        if (!selectedIndices.empty() && onDuplicateNotes) {
            onDuplicateNotes(clipId_, selectedIndices);
        }
        return true;
    }

    // M2: Delete/Backspace — Delete all selected notes
    if (key.getKeyCode() == juce::KeyPress::deleteKey ||
        key.getKeyCode() == juce::KeyPress::backspaceKey) {
        if (clipId_ == INVALID_CLIP_ID)
            return false;

        std::vector<size_t> selectedIndices;
        for (const auto& nc : noteComponents_) {
            if (nc->isSelected() && nc->getSourceClipId() == clipId_) {
                selectedIndices.push_back(nc->getNoteIndex());
            }
        }

        if (!selectedIndices.empty() && onDeleteNotes) {
            onDeleteNotes(clipId_, selectedIndices);
        }
        return !selectedIndices.empty();
    }

    // Arrow up/down: move selected notes by semitone (or octave with Shift)
    // Alt+arrows reserved for viewport scrolling
    if (!key.getModifiers().isAltDown() && (key.getKeyCode() == juce::KeyPress::upKey ||
                                            key.getKeyCode() == juce::KeyPress::downKey)) {
        const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
        if (!noteSel.isValid())
            return false;

        int delta = (key.getKeyCode() == juce::KeyPress::upKey) ? 1 : -1;
        if (key.getModifiers().isShiftDown())
            delta *= 12;

        const auto* clip = ClipManager::getInstance().getClip(noteSel.clipId);
        if (!clip || !clip->isMidi())
            return false;

        // Check all notes stay within valid range
        for (size_t idx : noteSel.noteIndices) {
            if (idx >= clip->midiNotes.size())
                return false;
            int newNote = clip->midiNotes[idx].noteNumber + delta;
            if (newNote < MIN_NOTE || newNote > MAX_NOTE)
                return true;  // Consume but don't move
        }

        // B3 fix: batch all pitch moves into a single command
        std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
        for (size_t idx : noteSel.noteIndices) {
            const auto& note = clip->midiNotes[idx];
            moves.push_back({idx, note.startBeat, note.noteNumber + delta});
        }

        if (moves.size() > 1 && onMultipleNotesMoved) {
            onMultipleNotesMoved(noteSel.clipId, std::move(moves));
        } else if (moves.size() == 1 && onNoteMoved) {
            onNoteMoved(noteSel.clipId, moves[0].noteIndex, moves[0].newStartBeat,
                        moves[0].newNoteNumber);
        }
        return true;
    }

    // M6: Left/Right arrow — nudge selected notes by one grid step
    if (!key.getModifiers().isAltDown() && (key.getKeyCode() == juce::KeyPress::leftKey ||
                                            key.getKeyCode() == juce::KeyPress::rightKey)) {
        const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
        if (!noteSel.isValid())
            return false;

        const auto* clip = ClipManager::getInstance().getClip(noteSel.clipId);
        if (!clip || !clip->isMidi())
            return false;

        double nudge = gridResolutionBeats_;
        if (key.getKeyCode() == juce::KeyPress::leftKey)
            nudge = -nudge;

        // Check all notes stay at >= 0
        for (size_t idx : noteSel.noteIndices) {
            if (idx >= clip->midiNotes.size())
                return false;
            if (clip->midiNotes[idx].startBeat + nudge < 0.0)
                return true;  // Consume but don't move
        }

        std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
        for (size_t idx : noteSel.noteIndices) {
            const auto& note = clip->midiNotes[idx];
            moves.push_back({idx, note.startBeat + nudge, note.noteNumber});
        }

        if (moves.size() > 1 && onMultipleNotesMoved) {
            onMultipleNotesMoved(noteSel.clipId, std::move(moves));
        } else if (moves.size() == 1 && onNoteMoved) {
            onNoteMoved(noteSel.clipId, moves[0].noteIndex, moves[0].newStartBeat,
                        moves[0].newNoteNumber);
        }
        return true;
    }

    // Let other key presses bubble up to the command manager
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

double PianoRollGridComponent::getDefaultNoteLengthBeats() const {
    if (rememberLastNoteLength_ && lastAddedNoteLengthBeats_ > 0.0)
        return lastAddedNoteLengthBeats_;
    if (defaultNoteLengthBeats_ > 0.0)
        return defaultNoteLengthBeats_;
    return juce::jmax(1.0 / 16.0, gridResolutionBeats_);
}

void PianoRollGridComponent::rememberAddedNoteLength(double lengthBeats) {
    if (lengthBeats > 0.0)
        lastAddedNoteLengthBeats_ = lengthBeats;
}

void PianoRollGridComponent::addDefaultNoteMenuItems(juce::PopupMenu& menu) const {
    juce::PopupMenu lengthMenu;
    lengthMenu.addItem(100, "Current Grid", true,
                       !rememberLastNoteLength_ && defaultNoteLengthBeats_ <= 0.0);
    lengthMenu.addItem(108, "Remember Note Lengths", true, rememberLastNoteLength_);
    lengthMenu.addSeparator();

    struct LengthOption {
        int id;
        const char* name;
        double beats;
    };
    const LengthOption lengths[] = {
        {101, "1/1", 4.0},   {102, "1/2", 2.0},    {103, "1/4", 1.0},       {104, "1/8", 0.5},
        {105, "1/16", 0.25}, {106, "1/32", 0.125}, {107, "1/8T", 1.0 / 3.0}};
    for (const auto& option : lengths)
        lengthMenu.addItem(option.id, option.name, true,
                           !rememberLastNoteLength_ &&
                               std::abs(defaultNoteLengthBeats_ - option.beats) < 0.000001);
    menu.addSubMenu("Default Length", lengthMenu);

    juce::PopupMenu velocityMenu;
    const int velocities[] = {127, 120, 100, 96, 80, 64, 48, 32, 16};
    for (int velocity : velocities)
        velocityMenu.addItem(200 + velocity, juce::String(velocity), true,
                             defaultNoteVelocity_ == velocity);
    menu.addSubMenu("Default Velocity", velocityMenu);
}

bool PianoRollGridComponent::handleDefaultNoteMenuResult(int result) {
    if (result == 100) {
        defaultNoteLengthBeats_ = 0.0;
        rememberLastNoteLength_ = false;
        return true;
    }

    if (result == 108) {
        rememberLastNoteLength_ = !rememberLastNoteLength_;
        return true;
    }

    struct LengthOption {
        int id;
        double beats;
    };
    const LengthOption lengths[] = {{101, 4.0},  {102, 2.0},   {103, 1.0},      {104, 0.5},
                                    {105, 0.25}, {106, 0.125}, {107, 1.0 / 3.0}};
    for (const auto& option : lengths) {
        if (result == option.id) {
            defaultNoteLengthBeats_ = option.beats;
            rememberLastNoteLength_ = false;
            return true;
        }
    }

    if (result >= 201 && result <= 327) {
        defaultNoteVelocity_ = juce::jlimit(1, 127, result - 200);
        return true;
    }

    return false;
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
    // std::round to match TimeRuler::timeToPixel — truncation here causes
    // sub-pixel drift between the loop vertical line drawn by the grid and
    // the loop flag drawn by the ruler above it.
    return static_cast<int>(std::round(beat * pixelsPerBeat_)) + leftPadding_;
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
        // In absolute mode, note positions depend on clipStartBeats_
        // so we need to update their bounds (e.g. during clip drag preview)
        if (!relativeMode_) {
            updateNoteComponentBounds();
        }
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

void PianoRollGridComponent::setFoldMap(const PitchFoldMap* map) {
    if (foldMap_ == map)
        return;
    foldMap_ = map;
    updateNoteComponentBounds();
    repaint();
}

int PianoRollGridComponent::foldRowCount() const {
    return foldMap_ ? foldMap_->rowCount() : NOTE_COUNT;
}

int PianoRollGridComponent::noteForRow(int row) const {
    return foldMap_ ? foldMap_->noteForRow(row) : (MAX_NOTE - row);
}

int PianoRollGridComponent::noteNumberToY(int noteNumber) const {
    const int row = foldMap_ ? foldMap_->rowForNote(noteNumber) : (MAX_NOTE - noteNumber);
    return row * noteHeight_;
}

int PianoRollGridComponent::yToNoteNumber(int y) const {
    int row = juce::jlimit(0, foldRowCount() - 1, y / noteHeight_);
    return juce::jlimit(MIN_NOTE, MAX_NOTE, noteForRow(row));
}

int PianoRollGridComponent::noteNumberByRowDelta(int startNote, int rowsUp) const {
    // Rows increase downward, pitch increases upward — moving `rowsUp` rows up
    // means decreasing the row index by that many.
    if (!foldMap_)
        return juce::jlimit(MIN_NOTE, MAX_NOTE, startNote + rowsUp);
    const int targetRow = foldMap_->rowForNote(startNote) - rowsUp;
    return juce::jlimit(MIN_NOTE, MAX_NOTE, noteForRow(targetRow));
}

void PianoRollGridComponent::updateNotePosition(NoteComponent* note, double beat, int noteNumber,
                                                double length) {
    if (!note)
        return;

    // Relative mode: notes at content-relative beats.
    // Absolute mode: midiTrimOffset compensates for startTime changes from
    // left-resize so notes stay at their timeline positions.
    ClipId clipId = note->getSourceClipId();
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        MidiNote previewNote;
        previewNote.startBeat = beat;
        previewNote.noteNumber = noteNumber;
        previewNote.lengthBeats = length;
        if (!ClipOperations::constrainMidiNoteToVisibleRange(*clip, previewNote)) {
            note->setVisible(false);
            rebuildSelectedPitchRows();
            return;
        }
        beat = previewNote.startBeat;
        noteNumber = previewNote.noteNumber;
        length = previewNote.lengthBeats;
        note->setVisible(true);
    }

    double displayBeat;
    const double visibleStart = clip ? ClipOperations::getMidiVisibleRange(*clip).startBeat : 0.0;
    if (relativeMode_) {
        if (clipIds_.size() > 1 && clip) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double clipOffsetBeats = timelineStartBeats(*clip, tempo) - clipStartBeats_;
            displayBeat = clipOffsetBeats + beat - visibleStart;
        } else {
            displayBeat = beat - visibleStart;
        }
    } else {
        if (clip) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double clipStartBeats = timelineStartBeats(*clip, tempo);
            displayBeat = clipStartBeats + beat - visibleStart;
        } else {
            displayBeat = clipStartBeats_ + beat;
        }
    }

    int x = beatToPixel(displayBeat);
    int y = noteNumberToY(noteNumber);
    int width = juce::jmax(8, static_cast<int>(length * pixelsPerBeat_));
    int height = noteHeight_ - 2;  // Small gap between notes

    note->setBounds(x, y + 1, width, height);
    note->updatePreviewPitch(noteNumber);
    rebuildSelectedPitchRows();
}

void PianoRollGridComponent::setCopyDragPreview(double beat, int noteNumber, double length,
                                                juce::Colour colour, bool active,
                                                size_t sourceNoteIndex) {
    copyDragGhosts_.clear();
    if (!active) {
        repaint();
        return;
    }

    // Find the source note to compute the delta
    const auto* srcClip = ClipManager::getInstance().getClip(clipId_);
    if (!srcClip || sourceNoteIndex >= srcClip->midiNotes.size()) {
        copyDragGhosts_.push_back({beat, noteNumber, length, colour});
        repaint();
        return;
    }

    const auto& sourceNote = srcClip->midiNotes[sourceNoteIndex];
    double beatDelta = beat - sourceNote.startBeat;
    int noteDelta = noteNumber - sourceNote.noteNumber;

    auto toDisplayBeat = [this, srcClip](double clipBeat) {
        const double visibleStart = ClipOperations::getMidiVisibleRange(*srcClip).startBeat;
        if (relativeMode_) {
            if (clipIds_.size() > 1) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent())
                    tempo = controller->getState().tempo.bpm;
                return srcClip->startTime * (tempo / 60.0) - clipStartBeats_ + clipBeat -
                       visibleStart;
            }
            return clipBeat - visibleStart;
        }

        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent())
            tempo = controller->getState().tempo.bpm;
        return srcClip->startTime * (tempo / 60.0) + clipBeat - visibleStart;
    };

    auto addGhost = [&](double clipBeat, int ghostNote, double ghostLength) {
        MidiNote previewNote;
        previewNote.startBeat = clipBeat;
        previewNote.noteNumber = ghostNote;
        previewNote.lengthBeats = ghostLength;
        if (!ClipOperations::constrainMidiNoteToVisibleRange(*srcClip, previewNote))
            return;

        copyDragGhosts_.push_back({toDisplayBeat(previewNote.startBeat), previewNote.noteNumber,
                                   previewNote.lengthBeats, colour});
    };

    // Ghost for the dragged note
    addGhost(beat, noteNumber, length);

    // Ghosts for other selected notes
    for (auto& nc : noteComponents_) {
        if (nc->getNoteIndex() == sourceNoteIndex)
            continue;
        if (!nc->isSelected())
            continue;

        size_t idx = nc->getNoteIndex();
        if (idx >= srcClip->midiNotes.size())
            continue;

        const auto& otherNote = srcClip->midiNotes[idx];
        double ghostBeat = juce::jmax(0.0, otherNote.startBeat + beatDelta);
        int ghostNote = juce::jlimit(MIN_NOTE, MAX_NOTE, otherNote.noteNumber + noteDelta);
        addGhost(ghostBeat, ghostNote, otherNote.lengthBeats);
    }

    repaint();
}

void PianoRollGridComponent::updateSelectedNotePositions(NoteComponent* draggedNote,
                                                         double beatDelta, int noteDelta) {
    if (!draggedNote)
        return;

    ClipId dragClipId = draggedNote->getSourceClipId();
    const auto* clip = ClipManager::getInstance().getClip(dragClipId);
    if (!clip)
        return;

    for (auto& nc : noteComponents_) {
        if (nc.get() == draggedNote)
            continue;
        if (nc->getSourceClipId() != dragClipId)
            continue;
        if (!nc->isSelected())
            continue;

        size_t idx = nc->getNoteIndex();
        if (idx >= clip->midiNotes.size())
            continue;

        const auto& note = clip->midiNotes[idx];
        double newBeat = juce::jmax(0.0, note.startBeat + beatDelta);
        int newNote = juce::jlimit(0, 127, note.noteNumber + noteDelta);
        updateNotePosition(nc.get(), newBeat, newNote, note.lengthBeats);
    }
    rebuildSelectedPitchRows();
}

void PianoRollGridComponent::updateSelectedNoteLengths(NoteComponent* draggedNote,
                                                       double lengthDelta) {
    if (!draggedNote)
        return;

    ClipId dragClipId = draggedNote->getSourceClipId();
    const auto* clip = ClipManager::getInstance().getClip(dragClipId);
    if (!clip)
        return;

    constexpr double MIN_LENGTH = 1.0 / 16.0;

    for (auto& nc : noteComponents_) {
        if (nc.get() == draggedNote)
            continue;
        if (nc->getSourceClipId() != dragClipId)
            continue;
        if (!nc->isSelected())
            continue;

        size_t idx = nc->getNoteIndex();
        if (idx >= clip->midiNotes.size())
            continue;

        const auto& note = clip->midiNotes[idx];
        double newLength = juce::jmax(MIN_LENGTH, note.lengthBeats + lengthDelta);
        updateNotePosition(nc.get(), note.startBeat, note.noteNumber, newLength);
    }
    rebuildSelectedPitchRows();
}

void PianoRollGridComponent::updateSelectedNoteLeftResize(NoteComponent* draggedNote,
                                                          double lengthDelta) {
    if (!draggedNote)
        return;

    ClipId dragClipId = draggedNote->getSourceClipId();
    const auto* clip = ClipManager::getInstance().getClip(dragClipId);
    if (!clip)
        return;

    constexpr double MIN_LENGTH = 1.0 / 16.0;
    double beatDelta = -lengthDelta;  // Start shifts opposite to length change

    for (auto& nc : noteComponents_) {
        if (nc.get() == draggedNote)
            continue;
        if (nc->getSourceClipId() != dragClipId)
            continue;
        if (!nc->isSelected())
            continue;

        size_t idx = nc->getNoteIndex();
        if (idx >= clip->midiNotes.size())
            continue;

        const auto& note = clip->midiNotes[idx];
        double newLength = juce::jmax(MIN_LENGTH, note.lengthBeats + lengthDelta);
        double newStart = juce::jmax(0.0, note.startBeat + beatDelta);
        updateNotePosition(nc.get(), newStart, note.noteNumber, newLength);
    }
    rebuildSelectedPitchRows();
}

void PianoRollGridComponent::selectNoteAfterRefresh(ClipId clipId, int noteIndex) {
    pendingSelectClipId_ = clipId;
    pendingSelectNoteIndex_ = noteIndex;
}

void PianoRollGridComponent::syncSelectionFromManager() {
    const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
    for (auto& nc : noteComponents_) {
        bool shouldSelect = false;
        if (noteSel.isValid() && nc->getSourceClipId() == noteSel.clipId) {
            size_t idx = nc->getNoteIndex();
            for (size_t selIdx : noteSel.noteIndices) {
                if (idx == selIdx) {
                    shouldSelect = true;
                    break;
                }
            }
        }
        nc->setSelected(shouldSelect);
    }
    rebuildSelectedPitchRows();
    repaint();
}

void PianoRollGridComponent::refreshNotes() {
    // Pending single-note selection (e.g. after add)
    int selectNoteIndex = pendingSelectNoteIndex_ >= 0 ? pendingSelectNoteIndex_ : -1;
    ClipId selectClipId = pendingSelectClipId_ != INVALID_CLIP_ID ? pendingSelectClipId_ : clipId_;

    // Clear pending single-note
    pendingSelectClipId_ = INVALID_CLIP_ID;
    pendingSelectNoteIndex_ = -1;

    // Take pending copy positions
    auto pendingPositions = std::move(pendingSelectPositions_);
    pendingSelectPositions_.clear();

    // Preserve multi-selection by index (when no pending overrides)
    // Prefer SelectionManager as source of truth (handles duplicate, paste, etc.)
    struct SavedSel {
        ClipId clipId;
        size_t index;
    };
    std::vector<SavedSel> savedSelection;
    if (selectNoteIndex < 0 && pendingPositions.empty()) {
        const auto& noteSel = SelectionManager::getInstance().getNoteSelection();
        if (noteSel.isValid() && !noteSel.noteIndices.empty()) {
            for (size_t idx : noteSel.noteIndices)
                savedSelection.push_back({noteSel.clipId, idx});
        } else {
            for (const auto& nc : noteComponents_) {
                if (nc->isSelected())
                    savedSelection.push_back({nc->getSourceClipId(), nc->getNoteIndex()});
            }
        }
    }

    clearNoteComponents();

    if (clipId_ == INVALID_CLIP_ID) {
        repaint();
        return;
    }

    createNoteComponents();
    updateNoteComponentBounds();

    // Apply selection
    if (!pendingPositions.empty()) {
        // Select notes matching copy destinations by position
        auto& clipManager = ClipManager::getInstance();
        for (auto& noteComp : noteComponents_) {
            ClipId ncClipId = noteComp->getSourceClipId();
            size_t idx = noteComp->getNoteIndex();
            const auto* clip = clipManager.getClip(ncClipId);
            if (!clip || idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            for (const auto& pos : pendingPositions) {
                if (pos.clipId == ncClipId && std::abs(note.startBeat - pos.beat) < 0.001 &&
                    note.noteNumber == pos.noteNumber) {
                    noteComp->setSelected(true);
                    selectedNoteIndex_ = static_cast<int>(idx);
                    break;
                }
            }
        }
    } else if (selectNoteIndex >= 0) {
        // Restore single pending selection
        for (auto& noteComp : noteComponents_) {
            if (noteComp->getSourceClipId() == selectClipId &&
                noteComp->getNoteIndex() == static_cast<size_t>(selectNoteIndex)) {
                noteComp->setSelected(true);
                selectedNoteIndex_ = selectNoteIndex;
                break;
            }
        }
    } else if (!savedSelection.empty()) {
        // Restore previous multi-selection
        for (auto& noteComp : noteComponents_) {
            for (const auto& sel : savedSelection) {
                if (noteComp->getSourceClipId() == sel.clipId &&
                    noteComp->getNoteIndex() == sel.index) {
                    noteComp->setSelected(true);
                    break;
                }
            }
        }
    }

    rebuildSelectedPitchRows();
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
        return;
    }

    // Ghost overlay is paint-only, so a repaint is enough when an overlay
    // track's clip changes
    if (!overlayTrackIds_.empty()) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip && std::find(overlayTrackIds_.begin(), overlayTrackIds_.end(), clip->trackId) !=
                        overlayTrackIds_.end()) {
            repaint();
        }
    }
}

double PianoRollGridComponent::snapBeatToGrid(double beat) const {
    if (!snapEnabled_ || gridResolutionBeats_ <= 0.0) {
        return beat;
    }
    return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
}

bool PianoRollGridComponent::isNearGridLine(int mouseX) const {
    if (gridResolutionBeats_ <= 0.0)
        return false;
    double beat = pixelToBeat(mouseX);
    double nearestBeat = std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
    int gridX = beatToPixel(nearestBeat);
    return std::abs(mouseX - gridX) <= GRID_LINE_HIT_TOLERANCE;
}

double PianoRollGridComponent::getNearestGridLineBeat(int mouseX) const {
    double beat = pixelToBeat(mouseX);
    if (gridResolutionBeats_ <= 0.0)
        return beat;
    return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
}

std::optional<PianoRollGridComponent::NoteInsertPosition>
PianoRollGridComponent::getNoteInsertPosition(juce::Point<int> localPos) const {
    if (selectedClipIds_.empty()) {
        return std::nullopt;
    }

    const double displayBeat = pixelToBeat(localPos.x);
    ClipId targetClipId = INVALID_CLIP_ID;

    if (relativeMode_ && selectedClipIds_.size() <= 1) {
        targetClipId = clipId_;
    } else if (relativeMode_ && selectedClipIds_.size() > 1) {
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }

        auto& clipManager = ClipManager::getInstance();
        for (ClipId selectedClipId : selectedClipIds_) {
            const auto* clip = clipManager.getClip(selectedClipId);
            if (!clip)
                continue;

            const double clipOffsetBeats = timelineStartBeats(*clip, tempo) - clipStartBeats_;
            const double clipEndRelBeats = clipOffsetBeats + timelineLengthBeats(*clip, tempo);
            if (displayBeat >= clipOffsetBeats && displayBeat < clipEndRelBeats) {
                targetClipId = selectedClipId;
                break;
            }
        }

        if (targetClipId == INVALID_CLIP_ID) {
            targetClipId = clipId_;
        }
    } else {
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }

        auto& clipManager = ClipManager::getInstance();
        for (ClipId selectedClipId : selectedClipIds_) {
            const auto* clip = clipManager.getClip(selectedClipId);
            if (!clip)
                continue;

            if (displayBeat >= timelineStartBeats(*clip, tempo) &&
                displayBeat < timelineEndBeats(*clip, tempo)) {
                targetClipId = selectedClipId;
                break;
            }
        }

        if (targetClipId == INVALID_CLIP_ID) {
            targetClipId = clipId_;
        }
    }

    const auto* targetClip = ClipManager::getInstance().getClip(targetClipId);
    if (!targetClip) {
        return std::nullopt;
    }

    NoteInsertPosition insertPos;
    insertPos.clipId = targetClipId;
    insertPos.beat = clipBeatForDisplayX(targetClipId, localPos.x);
    insertPos.noteNumber = yToNoteNumber(localPos.y);
    return insertPos;
}

double PianoRollGridComponent::displayBeatForClipBeat(ClipId clipId, double clipBeat) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    const double visibleStart = clip ? ClipOperations::getMidiVisibleRange(*clip).startBeat : 0.0;

    if (relativeMode_) {
        if (clipIds_.size() > 1 && clip) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            return timelineStartBeats(*clip, tempo) - clipStartBeats_ + clipBeat - visibleStart;
        }
        return clipBeat - visibleStart;
    }

    if (clip) {
        double clipOffsetBeats = clipStartBeats_;
        if (clipIds_.size() > 1) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            clipOffsetBeats = timelineStartBeats(*clip, tempo);
        }
        return clipOffsetBeats + clipBeat - visibleStart;
    }

    return clipStartBeats_ + clipBeat;
}

double PianoRollGridComponent::clipBeatForDisplayX(ClipId clipId, int mouseX) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    double clipBeat = pixelToBeat(mouseX);

    if (relativeMode_) {
        if (clipIds_.size() > 1 && clip) {
            double tempo = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            clipBeat -= timelineStartBeats(*clip, tempo) - clipStartBeats_;
        }
    } else {
        if (clip) {
            double clipOffsetBeats = clipStartBeats_;
            if (clipIds_.size() > 1) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                clipOffsetBeats = timelineStartBeats(*clip, tempo);
            }
            clipBeat -= clipOffsetBeats;
        }
    }

    clipBeat = snapBeatToGrid(clipBeat);
    clipBeat = juce::jmax(0.0, clipBeat);
    if (clip) {
        clipBeat += ClipOperations::getMidiVisibleRange(*clip).startBeat;
    }
    return clipBeat;
}

double PianoRollGridComponent::absolutePlayheadBeatForDisplayX(int mouseX) const {
    double beat = pixelToBeat(mouseX);
    if (snapEnabled_)
        beat = snapBeatToGrid(beat);

    if (relativeMode_) {
        const auto* clip =
            clipId_ != INVALID_CLIP_ID ? ClipManager::getInstance().getClip(clipId_) : nullptr;

        if (clip && clip->loopEnabled) {
            double bpm = 120.0;
            double currentGlobalBeat = 0.0;
            if (auto* controller = TimelineController::getCurrent()) {
                const auto& state = controller->getState();
                bpm = state.tempo.bpm > 0.0 ? state.tempo.bpm : bpm;
                currentGlobalBeat = state.playhead.getCurrentPositionBeats();
            }

            const double loopLength = effectiveLoopLengthBeats(*clip, bpm);
            if (loopLength > 0.0)
                return globalBeatForRelativeLoopClick(beat, currentGlobalBeat, *clip, bpm);
        }

        return juce::jmax(0.0, beat + clipStartBeats_);
    }

    return juce::jlimit(0.0, timelineLengthBeats_, beat);
}

void PianoRollGridComponent::updateEmptyGridCursor(const juce::ModifierKeys& mods, int mouseX) {
    if (mods.isAltDown() && isNearGridLine(mouseX)) {
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    } else if (mods.isShiftDown()) {
        setMouseCursor(CursorManager::getInstance().getNoteDrawCursor());
    } else {
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    }
}

void PianoRollGridComponent::createNoteComponents() {
    auto& clipManager = ClipManager::getInstance();

    // Iterate through all clips
    for (ClipId clipId : clipIds_) {
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi()) {
            continue;
        }

        juce::Colour noteColour = getColourForClip(clipId);

        // Create note component for each note in this clip
        for (size_t i = 0; i < clip->midiNotes.size(); i++) {
            auto visibleNote = clip->midiNotes[i];
            if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, visibleNote))
                continue;

            auto noteComp = std::make_unique<NoteComponent>(i, this, clipId);

            noteComp->onNoteSelected = [this, clipId](size_t index, bool isAdditive) {
                if (!isAdditive) {
                    // Deselect other notes (exclusive selection)
                    for (auto& nc : noteComponents_) {
                        if (nc->getSourceClipId() != clipId || nc->getNoteIndex() != index) {
                            nc->setSelected(false);
                        }
                    }
                }
                selectedNoteIndex_ = static_cast<int>(index);
                rebuildSelectedPitchRows();

                if (onNoteSelected) {
                    onNoteSelected(clipId, index, isAdditive);
                }
            };

            noteComp->onNoteRangeSelected = [this, clipId](size_t index) {
                if (onNoteRangeSelected) {
                    onNoteRangeSelected(clipId, index);
                }
                syncSelectionFromManager();
            };

            noteComp->onNoteDeselected = [this, clipId](size_t /*index*/) {
                // Cmd+click toggled this note OFF — remove from SelectionManager
                std::vector<size_t> selectedIndices;
                for (auto& nc : noteComponents_) {
                    if (nc->isSelected()) {
                        selectedIndices.push_back(nc->getNoteIndex());
                    }
                }
                selectedNoteIndex_ =
                    selectedIndices.empty() ? -1 : static_cast<int>(selectedIndices.back());
                if (onNoteSelectionChanged) {
                    onNoteSelectionChanged(clipId, selectedIndices);
                }
                rebuildSelectedPitchRows();
            };

            noteComp->onNoteMoved = [this, clipId](size_t index, double newBeat,
                                                   int newNoteNumber) {
                const auto* srcClip = ClipManager::getInstance().getClip(clipId);
                if (!srcClip || index >= srcClip->midiNotes.size()) {
                    if (onNoteMoved)
                        onNoteMoved(clipId, index, newBeat, newNoteNumber);
                    return;
                }

                const auto& sourceNote = srcClip->midiNotes[index];
                double beatDelta = newBeat - sourceNote.startBeat;
                int noteDelta = newNoteNumber - sourceNote.noteNumber;

                // Collect all selected notes into a single batch move
                std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
                moves.push_back({index, newBeat, newNoteNumber});

                for (auto& nc : noteComponents_) {
                    if (nc->getSourceClipId() != clipId)
                        continue;
                    if (nc->getNoteIndex() == index)
                        continue;
                    if (!nc->isSelected())
                        continue;

                    size_t otherIndex = nc->getNoteIndex();
                    if (otherIndex >= srcClip->midiNotes.size())
                        continue;

                    const auto& otherNote = srcClip->midiNotes[otherIndex];
                    double otherNewBeat = juce::jmax(0.0, otherNote.startBeat + beatDelta);
                    int otherNewNote =
                        juce::jlimit(MIN_NOTE, MAX_NOTE, otherNote.noteNumber + noteDelta);

                    moves.push_back({otherIndex, otherNewBeat, otherNewNote});
                }

                if (moves.size() > 1 && onMultipleNotesMoved) {
                    onMultipleNotesMoved(clipId, std::move(moves));
                } else if (onNoteMoved) {
                    onNoteMoved(clipId, index, newBeat, newNoteNumber);
                }
            };

            noteComp->onNoteCopied = [this, clipId](size_t index, double destBeat,
                                                    int destNoteNumber) {
                if (!onNoteCopied)
                    return;

                // Get the source note data to compute deltas
                const auto* srcClip = ClipManager::getInstance().getClip(clipId);
                if (!srcClip || index >= srcClip->midiNotes.size()) {
                    onNoteCopied(clipId, index, destBeat, destNoteNumber);
                    pendingSelectPositions_.push_back({clipId, destBeat, destNoteNumber});
                    return;
                }

                const auto& sourceNote = srcClip->midiNotes[index];
                double beatDelta = destBeat - sourceNote.startBeat;
                int noteDelta = destNoteNumber - sourceNote.noteNumber;

                // Copy the dragged note
                onNoteCopied(clipId, index, destBeat, destNoteNumber);
                pendingSelectPositions_.push_back({clipId, destBeat, destNoteNumber});

                // Copy other selected notes with the same delta
                for (auto& nc : noteComponents_) {
                    if (nc->getSourceClipId() != clipId)
                        continue;
                    if (nc->getNoteIndex() == index)
                        continue;
                    if (!nc->isSelected())
                        continue;

                    size_t otherIndex = nc->getNoteIndex();
                    if (otherIndex >= srcClip->midiNotes.size())
                        continue;

                    const auto& otherNote = srcClip->midiNotes[otherIndex];
                    double otherDestBeat = juce::jmax(0.0, otherNote.startBeat + beatDelta);
                    int otherDestNote =
                        juce::jlimit(MIN_NOTE, MAX_NOTE, otherNote.noteNumber + noteDelta);

                    onNoteCopied(clipId, otherIndex, otherDestBeat, otherDestNote);
                    pendingSelectPositions_.push_back({clipId, otherDestBeat, otherDestNote});
                }
            };

            noteComp->onNoteResized = [this, clipId](size_t index, double newLength,
                                                     bool fromStart) {
                const auto* srcClip = ClipManager::getInstance().getClip(clipId);
                if (!srcClip || index >= srcClip->midiNotes.size()) {
                    if (onNoteResized)
                        onNoteResized(clipId, index, newLength);
                    return;
                }

                // Compute length delta from the dragged note
                double lengthDelta = newLength - srcClip->midiNotes[index].lengthBeats;
                constexpr double MIN_LENGTH = 1.0 / 16.0;

                // Collect all selected notes into a batch resize
                std::vector<std::pair<size_t, double>> resizes;
                resizes.emplace_back(index, newLength);

                // For left-resize, also collect start position moves
                // (start shifts by -lengthDelta to keep the right edge fixed)
                std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
                if (fromStart) {
                    double beatDelta = -lengthDelta;
                    const auto& draggedNote = srcClip->midiNotes[index];
                    moves.push_back({index, juce::jmax(0.0, draggedNote.startBeat + beatDelta),
                                     draggedNote.noteNumber});
                }

                for (auto& nc : noteComponents_) {
                    if (nc->getSourceClipId() != clipId)
                        continue;
                    if (nc->getNoteIndex() == index)
                        continue;
                    if (!nc->isSelected())
                        continue;

                    size_t otherIndex = nc->getNoteIndex();
                    if (otherIndex >= srcClip->midiNotes.size())
                        continue;

                    double otherNewLength = juce::jmax(
                        MIN_LENGTH, srcClip->midiNotes[otherIndex].lengthBeats + lengthDelta);
                    resizes.emplace_back(otherIndex, otherNewLength);

                    if (fromStart) {
                        double beatDelta = -lengthDelta;
                        const auto& otherNote = srcClip->midiNotes[otherIndex];
                        moves.push_back({otherIndex,
                                         juce::jmax(0.0, otherNote.startBeat + beatDelta),
                                         otherNote.noteNumber});
                    }
                }

                if (fromStart && !moves.empty() && resizes.size() > 1) {
                    // Left-resize with multi-selection: compound move+resize as one undo step
                    if (onLeftResizeMultipleNotes)
                        onLeftResizeMultipleNotes(clipId, std::move(moves), std::move(resizes));
                } else if (fromStart && resizes.size() == 1) {
                    // Left-resize single note: compound move+resize
                    if (!moves.empty() && onNoteMoved)
                        onNoteMoved(clipId, moves[0].noteIndex, moves[0].newStartBeat,
                                    moves[0].newNoteNumber);
                    if (onNoteResized)
                        onNoteResized(clipId, index, newLength);
                } else if (resizes.size() > 1 && onMultipleNotesResized) {
                    onMultipleNotesResized(clipId, std::move(resizes));
                } else if (onNoteResized) {
                    onNoteResized(clipId, index, newLength);
                }
            };

            noteComp->onNoteDeleted = [this, clipId](size_t index) {
                // If the double-clicked note is part of a multi-selection,
                // delete all selected notes instead of just this one
                std::vector<size_t> selectedIndices;
                bool indexIsSelected = false;
                for (const auto& nc : noteComponents_) {
                    if (nc->isSelected()) {
                        selectedIndices.push_back(nc->getNoteIndex());
                        if (nc->getNoteIndex() == index) {
                            indexIsSelected = true;
                        }
                    }
                }

                if (indexIsSelected && selectedIndices.size() > 1 && onDeleteNotes) {
                    onDeleteNotes(clipId, selectedIndices);
                } else if (onNoteDeleted) {
                    onNoteDeleted(clipId, index);
                }
                selectedNoteIndex_ = -1;
                rebuildSelectedPitchRows();
            };

            noteComp->onNoteDragging = [this, clipId](size_t index, double previewBeat,
                                                      bool isDragging) {
                if (onNoteDragging) {
                    onNoteDragging(clipId, index, previewBeat, isDragging);
                }
            };

            noteComp->snapBeatToGrid = [this](double beat) { return snapBeatToGrid(beat); };

            noteComp->onRightClick = [this, clipId](size_t /*index*/,
                                                    const juce::MouseEvent& /*event*/) {
                // Collect all selected note indices
                std::vector<size_t> selectedIndices;
                for (const auto& nc : noteComponents_) {
                    if (nc->isSelected()) {
                        selectedIndices.push_back(nc->getNoteIndex());
                    }
                }

                juce::PopupMenu menu;
                bool hasSelection = !selectedIndices.empty();

                menu.addItem(10, "Copy", hasSelection);
                menu.addItem(11, "Paste", ClipManager::getInstance().hasNotesInClipboard());
                menu.addItem(12, "Duplicate", hasSelection);
                menu.addItem(13, "Delete", hasSelection);
                menu.addSeparator();
                addDefaultNoteMenuItems(menu);
                menu.addSeparator();

                // Quantize submenu
                {
                    juce::PopupMenu quantizeMenu;
                    {
                        juce::PopupMenu modeMenu;
                        modeMenu.addItem(1, "Start");
                        modeMenu.addItem(2, "Length");
                        modeMenu.addItem(3, "Start & Length");
                        quantizeMenu.addSubMenu("Current Grid", modeMenu,
                                                hasSelection && gridResolutionBeats_ > 0.0);
                    }
                    quantizeMenu.addSeparator();

                    struct GridOption {
                        const char* name;
                        double beats;
                    };
                    // clang-format off
                    const GridOption grids[] = {
                        {"1/1",   4.0},    {"1/2",   2.0},    {"1/4",   1.0},
                        {"1/8",   0.5},    {"1/16",  0.25},   {"1/32",  0.125},
                        {"1/2.",  3.0},    {"1/4.",  1.5},
                        {"1/8.",  0.75},   {"1/16.", 0.375},
                        {"1/2T",  4.0/3},  {"1/4T",  2.0/3},
                        {"1/8T",  1.0/3},  {"1/16T", 1.0/6},
                    };
                    // clang-format on
                    int itemId = 20;
                    for (const auto& grid : grids) {
                        juce::PopupMenu modeMenu;
                        modeMenu.addItem(itemId++, "Start");
                        modeMenu.addItem(itemId++, "Length");
                        modeMenu.addItem(itemId++, "Start & Length");
                        quantizeMenu.addSubMenu(grid.name, modeMenu, hasSelection);
                    }
                    menu.addSubMenu("Quantize", quantizeMenu, hasSelection);
                }

                menu.showMenuAsync(
                    juce::PopupMenu::Options(), [this, clipId, indices = std::move(selectedIndices),
                                                 gridRes = gridResolutionBeats_](int result) {
                        if (result == 0)
                            return;
                        if (result == 10 && onCopyNotes)
                            onCopyNotes(clipId, indices);
                        else if (result == 11 && onPasteNotes)
                            onPasteNotes(clipId);
                        else if (result == 12 && onDuplicateNotes)
                            onDuplicateNotes(clipId, indices);
                        else if (result == 13 && onDeleteNotes)
                            onDeleteNotes(clipId, indices);
                        else if (handleDefaultNoteMenuResult(result))
                            return;
                        else if (result >= 1 && result <= 3 && onQuantizeNotes) {
                            const QuantizeMode modes[] = {QuantizeMode::StartOnly,
                                                          QuantizeMode::LengthOnly,
                                                          QuantizeMode::StartAndLength};
                            onQuantizeNotes(clipId, indices, modes[result - 1], gridRes);
                        } else if (result >= 20 && result <= 61 && onQuantizeNotes) {
                            // clang-format off
                            const double gridBeats[] = {
                                4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                3.0, 1.5, 0.75, 0.375,
                                4.0/3, 2.0/3, 1.0/3, 1.0/6,
                            };
                            // clang-format on
                            const QuantizeMode modes[] = {QuantizeMode::StartOnly,
                                                          QuantizeMode::LengthOnly,
                                                          QuantizeMode::StartAndLength};
                            int offset = result - 20;
                            onQuantizeNotes(clipId, indices, modes[offset % 3],
                                            gridBeats[offset / 3]);
                        }
                    });
            };

            noteComp->setGhost(!isClipSelected(clipId));
            noteComp->updateFromNote(visibleNote, noteColour);
            noteComp->setInterceptsMouseClicks(!pitchExpressionMode_, !pitchExpressionMode_);
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
    if (!selectedPitchRows_.empty()) {
        selectedPitchRows_.clear();
        if (onSelectedPitchRowsChanged)
            onSelectedPitchRowsChanged(selectedPitchRows_);
    }
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

        auto note = clip->midiNotes[noteIndex];
        if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, note)) {
            noteComp->setVisible(false);
            continue;
        }

        // Relative mode: notes at content-relative beats.
        // Absolute mode: midiTrimOffset compensates for left-resize.
        double displayBeat;

        const double visibleStart = ClipOperations::getMidiVisibleRange(*clip).startBeat;
        if (relativeMode_) {
            if (clipIds_.size() > 1) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                double clipOffsetBeats = timelineStartBeats(*clip, tempo) - clipStartBeats_;
                displayBeat = clipOffsetBeats + note.startBeat - visibleStart;
            } else {
                displayBeat = note.startBeat - visibleStart;
            }
        } else {
            // Absolute mode: use clipStartBeats_ which reflects the drag
            // preview position during clip moves, falling back to the clip's
            // actual timeline position otherwise.
            double clipOffsetBeats;
            if (clipIds_.size() > 1) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                clipOffsetBeats = timelineStartBeats(*clip, tempo);
            } else {
                clipOffsetBeats = clipStartBeats_;
            }
            displayBeat = clipOffsetBeats + note.startBeat - visibleStart;
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
    rebuildSelectedPitchRows();
}

void PianoRollGridComponent::rebuildSelectedPitchRows() {
    std::set<int> pitches;
    for (const auto& noteComp : noteComponents_) {
        if (noteComp->isSelected() && noteComp->isVisible()) {
            pitches.insert(yToNoteNumber(noteComp->getY()));
        }
    }

    if (selectedPitchRows_ != pitches) {
        selectedPitchRows_ = std::move(pitches);
        if (onSelectedPitchRowsChanged)
            onSelectedPitchRowsChanged(selectedPitchRows_);
        repaint();
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

    // Chord clips follow the chord track's colour live (rather than the colour
    // snapshotted onto clip->colour at creation), so their notes match the
    // chord-lane blocks even after the track colour changes.
    juce::Colour base = clip->colour;
    if (const auto* track = TrackManager::getInstance().getTrack(clip->trackId);
        track != nullptr && track->type == TrackType::Chord)
        base = track->colour;

    // Use the colour as-is, but slightly desaturated for multi-clip view
    return clipIds_.size() == 1 ? base : base.withSaturation(0.7f);
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

void PianoRollGridComponent::setPhasePreview(double beats, bool active) {
    phasePreviewBeats_ = beats;
    phasePreviewActive_ = active;
    repaint();
}

void PianoRollGridComponent::setPlayheadPosition(double positionSeconds) {
    if (playheadPosition_ != positionSeconds) {
        playheadPosition_ = positionSeconds;
        repaint();
    }
}

bool PianoRollGridComponent::getPlayheadDisplayX(int& gridLocalX) const {
    if (playheadPosition_ < 0.0 || clipLengthBeats_ <= 0.0)
        return false;

    // Convert seconds to beats
    double tempo = 120.0;
    if (auto* controller = TimelineController::getCurrent())
        tempo = controller->getState().tempo.bpm;
    double secondsPerBeat = 60.0 / tempo;
    double playheadBeats = playheadPosition_ / secondsPerBeat;

    // Only visible when the playhead falls within the clip's time range
    double relBeat = playheadBeats - clipStartBeats_;
    if (relBeat < 0.0 || relBeat > clipLengthBeats_)
        return false;

    double displayBeat = relativeMode_ ? (playheadBeats - clipStartBeats_) : playheadBeats;

    // Wrap playhead within loop region when looping is enabled
    if (loopEnabled_ && loopLengthBeats_ > 0.0) {
        double beatPos = relativeMode_ ? displayBeat : (displayBeat - clipStartBeats_);
        beatPos = std::fmod(beatPos, loopLengthBeats_);
        if (beatPos < 0.0)
            beatPos += loopLengthBeats_;
        displayBeat = relativeMode_ ? beatPos : (clipStartBeats_ + beatPos);
    }

    gridLocalX = beatToPixel(displayBeat);
    return true;
}

void PianoRollGridComponent::setEditCursorPosition(double positionSeconds, bool blinkVisible) {
    editCursorPosition_ = positionSeconds;
    editCursorVisible_ = blinkVisible;
    repaint();
}

// ============================================================================
// DragAndDropTarget — chord block drops
// ============================================================================

bool PianoRollGridComponent::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject())
        return obj->getProperty("type").toString() == "chordBlock";
    return false;
}

void PianoRollGridComponent::itemDragEnter(const SourceDetails& details) {
    double beat = pixelToBeat(details.localPosition.x);
    if (snapEnabled_)
        beat = snapBeatToGrid(beat);
    chordDropActive_ = true;
    chordDropBeat_ = beat;
    repaint();
}

void PianoRollGridComponent::itemDragMove(const SourceDetails& details) {
    double beat = pixelToBeat(details.localPosition.x);
    if (snapEnabled_)
        beat = snapBeatToGrid(beat);
    chordDropBeat_ = beat;
    repaint();
}

void PianoRollGridComponent::itemDragExit(const SourceDetails& /*details*/) {
    chordDropActive_ = false;
    repaint();
}

void PianoRollGridComponent::itemDropped(const SourceDetails& details) {
    chordDropActive_ = false;

    auto* obj = details.description.getDynamicObject();
    if (!obj)
        return;

    if (clipId_ == INVALID_CLIP_ID && selectedClipIds_.empty())
        return;

    double dropBeat = pixelToBeat(details.localPosition.x);
    if (snapEnabled_)
        dropBeat = snapBeatToGrid(dropBeat);

    // Extract notes from drag data
    auto* notesVar = obj->getProperties().getVarPointer("notes");
    if (!notesVar || !notesVar->isArray())
        return;

    std::vector<std::pair<int, int>> notes;
    for (int i = 0; i < notesVar->size(); ++i) {
        auto& pair = (*notesVar)[i];
        if (pair.isArray() && pair.size() >= 2)
            notes.emplace_back(static_cast<int>(pair[0]), static_cast<int>(pair[1]));
    }

    if (notes.empty())
        return;

    juce::String chordName = obj->getProperty("chordName").toString();

    ClipId targetClipId = clipId_;
    if (targetClipId == INVALID_CLIP_ID && !selectedClipIds_.empty())
        targetClipId = selectedClipIds_.front();

    // Enter pending chord state — user must click to set length or press Enter for default
    pendingChord_.clipId = targetClipId;
    pendingChord_.startBeat = dropBeat;
    pendingChord_.previewEndBeat = dropBeat + gridResolutionBeats_;  // Default length preview
    pendingChord_.notes = std::move(notes);
    pendingChord_.chordName = chordName;
    pendingChord_.active = true;
    pendingChord_.blinkOn = true;

    grabKeyboardFocus();
    startTimerHz(3);  // Blink at ~3Hz
    repaint();
}

bool PianoRollGridComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
            return true;
    return false;
}

void PianoRollGridComponent::filesDropped(const juce::StringArray& files, int x, int /*y*/) {
    if (clipId_ == INVALID_CLIP_ID && selectedClipIds_.empty())
        return;

    // Find first .mid file
    juce::File midiFile;
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) {
            midiFile = juce::File(f);
            break;
        }
    }
    if (!midiFile.existsAsFile())
        return;

    // Parse MIDI file
    juce::FileInputStream fis(midiFile);
    if (!fis.openedOk())
        return;

    juce::MidiFile midi;
    if (!midi.readFrom(fis))
        return;

    int ticksPerQN = midi.getTimeFormat();
    if (ticksPerQN <= 0)
        return;

    // Extract embedded chord markers (the reverse of MidiFileWriter's marker
    // writing). Parsed up front; note linkage to each chord happens below.
    const auto chordMarkers = magda::daw::readChordMarkers(midi);

    // Extract full note data (with timing) and simple note list
    struct NoteData {
        int noteNumber;
        int velocity;
        double startBeat;
        double lengthBeats;
    };
    std::vector<NoteData> fullNotes;
    std::vector<std::pair<int, int>> simpleNotes;

    for (int t = 0; t < midi.getNumTracks(); ++t) {
        auto* track = midi.getTrack(t);
        if (!track)
            continue;
        for (int i = 0; i < track->getNumEvents(); ++i) {
            auto& msg = track->getEventPointer(i)->message;
            if (msg.isNoteOn()) {
                simpleNotes.emplace_back(msg.getNoteNumber(), msg.getVelocity());

                double startBeat = msg.getTimeStamp() / ticksPerQN;
                double lengthBeats = 1.0;
                // Find matching note-off by scanning forward
                for (int j = i + 1; j < track->getNumEvents(); ++j) {
                    auto& offMsg = track->getEventPointer(j)->message;
                    if ((offMsg.isNoteOff() || (offMsg.isNoteOn() && offMsg.getVelocity() == 0)) &&
                        offMsg.getNoteNumber() == msg.getNoteNumber()) {
                        lengthBeats = (offMsg.getTimeStamp() - msg.getTimeStamp()) / ticksPerQN;
                        break;
                    }
                }
                fullNotes.push_back(
                    {msg.getNoteNumber(), msg.getVelocity(), startBeat, lengthBeats});
            }
        }
    }

    if (simpleNotes.empty())
        return;

    double dropBeat = pixelToBeat(x);
    if (snapEnabled_)
        dropBeat = snapBeatToGrid(dropBeat);

    ClipId targetClipId = clipId_;
    if (targetClipId == INVALID_CLIP_ID && !selectedClipIds_.empty())
        targetClipId = selectedClipIds_.front();

    // If chord markers are present, insert the full progression directly
    if (!chordMarkers.empty() && onChordDropped) {
        auto* clipData = ClipManager::getInstance().getClip(targetClipId);
        if (!clipData)
            return;

        CompoundOperationScope scope("Add Chord Progression");

        for (const auto& marker : chordMarkers) {
            int groupId = clipData->nextChordGroupId++;

            // Collect notes belonging to this chord (within its beat range)
            std::vector<MidiNote> chordNotes;
            for (const auto& n : fullNotes) {
                if (n.startBeat >= marker.beatPosition &&
                    n.startBeat < marker.beatPosition + marker.lengthBeats) {
                    MidiNote mn;
                    mn.noteNumber = n.noteNumber;
                    mn.velocity = n.velocity;
                    mn.startBeat = dropBeat + n.startBeat;
                    mn.lengthBeats = n.lengthBeats;
                    mn.chordGroup = groupId;
                    chordNotes.push_back(mn);
                }
            }

            if (!chordNotes.empty()) {
                auto noteCmd = std::make_unique<AddMultipleMidiNotesCommand>(
                    targetClipId, std::move(chordNotes), "Add chord notes");
                UndoManager::getInstance().executeCommand(std::move(noteCmd));
            }

            ClipInfo::ChordAnnotation annotation;
            annotation.beatPosition = dropBeat + marker.beatPosition;
            annotation.lengthBeats = marker.lengthBeats;
            annotation.chordName = marker.chordName;
            annotation.chordGroup = groupId;
            auto chordCmd = std::make_unique<AddChordAnnotationCommand>(targetClipId, annotation);
            UndoManager::getInstance().executeCommand(std::move(chordCmd));
        }

        repaint();
        return;
    }

    // No chord markers — single chord, use pending chord flow
    pendingChord_.clipId = targetClipId;
    pendingChord_.startBeat = dropBeat;
    pendingChord_.previewEndBeat = dropBeat + gridResolutionBeats_;
    pendingChord_.notes = std::move(simpleNotes);
    pendingChord_.chordName = midiFile.getFileNameWithoutExtension();
    pendingChord_.active = true;
    pendingChord_.blinkOn = true;

    grabKeyboardFocus();
    startTimerHz(3);
    repaint();
}

void PianoRollGridComponent::timerCallback() {
    if (!pendingChord_.active) {
        stopTimer();
        return;
    }
    pendingChord_.blinkOn = !pendingChord_.blinkOn;
    repaint();
}

void PianoRollGridComponent::confirmPendingChord(double endBeat) {
    if (!pendingChord_.active)
        return;

    double length = endBeat - pendingChord_.startBeat;
    if (length < gridResolutionBeats_)
        length = gridResolutionBeats_;

    if (onChordDropped) {
        rememberAddedNoteLength(length);
        onChordDropped(pendingChord_.clipId, pendingChord_.startBeat, length,
                       std::move(pendingChord_.notes), pendingChord_.chordName);
    }

    cancelPendingChord();
}

void PianoRollGridComponent::cancelPendingChord() {
    pendingChord_.active = false;
    pendingChord_.notes.clear();
    stopTimer();
    repaint();
}

// ============================================================================
// Pitch glide (MPE pitch expression) overlay
// ============================================================================

void PianoRollGridComponent::setPitchExpressionMode(bool enabled) {
    if (pitchExpressionMode_ == enabled)
        return;

    pitchExpressionMode_ = enabled;

    // In expression mode the grid handles all mouse interaction; note
    // components must not swallow clicks.
    for (auto& nc : noteComponents_)
        nc->setInterceptsMouseClicks(!enabled, !enabled);

    if (!enabled && isExpressionDragging_) {
        isExpressionDragging_ = false;
        expressionDragPointIndex_ = -1;
        expressionClipId_ = INVALID_CLIP_ID;
        expressionWorkingPoints_.clear();
    }

    repaint();
}

double PianoRollGridComponent::evaluatePitchExpression(
    const std::vector<MidiPitchExpressionPoint>& points, double relBeat) {
    if (points.empty())
        return 0.0;
    if (relBeat <= points.front().beat)
        return points.front().semitones;
    if (relBeat >= points.back().beat)
        return points.back().semitones;

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (relBeat >= a.beat && relBeat <= b.beat) {
            const double span = b.beat - a.beat;
            if (span <= 0.0)
                return b.semitones;
            const double t = (relBeat - a.beat) / span;
            return a.semitones + t * (b.semitones - a.semitones);
        }
    }
    return points.back().semitones;
}

double PianoRollGridComponent::expressionRelBeatForX(ClipId clipId, const MidiNote& note,
                                                     int x) const {
    const double noteStartDisplayBeat = displayBeatForClipBeat(clipId, note.startBeat);
    const double relBeat = pixelToBeat(x) - noteStartDisplayBeat;
    return juce::jlimit(0.0, note.lengthBeats, relBeat);
}

juce::Point<float> PianoRollGridComponent::expressionPointToScreen(
    ClipId clipId, const MidiNote& note, const MidiPitchExpressionPoint& point) const {
    const double noteStartDisplayBeat = displayBeatForClipBeat(clipId, note.startBeat);
    const float x = static_cast<float>(beatToPixel(noteStartDisplayBeat + point.beat));
    const float centerY =
        static_cast<float>(noteNumberToY(note.noteNumber)) + static_cast<float>(noteHeight_) * 0.5f;
    const float y = centerY - static_cast<float>(point.semitones * noteHeight_);
    return {x, y};
}

std::optional<PianoRollGridComponent::ExpressionHit> PianoRollGridComponent::hitTestExpressionPoint(
    juce::Point<int> pos) const {
    auto& clipManager = ClipManager::getInstance();

    for (ClipId clipId : selectedClipIds_) {
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
            continue;

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];
            if (!note.hasPitchExpression())
                continue;

            for (size_t p = 0; p < note.pitchExpression.size(); ++p) {
                auto screen = expressionPointToScreen(clipId, note, note.pitchExpression[p]);
                if (screen.getDistanceFrom(pos.toFloat()) <=
                    static_cast<float>(EXPRESSION_POINT_HIT_RADIUS)) {
                    return ExpressionHit{clipId, i, static_cast<int>(p)};
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<PianoRollGridComponent::ExpressionHit> PianoRollGridComponent::hitTestExpressionNote(
    juce::Point<int> pos) const {
    auto& clipManager = ClipManager::getInstance();

    std::optional<ExpressionHit> best;
    double bestDistance = std::numeric_limits<double>::max();

    for (ClipId clipId : selectedClipIds_) {
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
            continue;

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            auto visibleNote = clip->midiNotes[i];
            if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, visibleNote))
                continue;

            const auto& note = clip->midiNotes[i];
            const double noteStartDisplayBeat = displayBeatForClipBeat(clipId, note.startBeat);
            const int startX = beatToPixel(noteStartDisplayBeat);
            const int endX = beatToPixel(noteStartDisplayBeat + note.lengthBeats);
            if (pos.x < startX || pos.x >= endX)
                continue;

            // Vertical proximity to the curve at this x (flat center line when
            // the note has no expression yet). Generous capture range so a
            // glide can be grabbed away from the note row, but bounded so
            // clicks far above/below fall through to other notes.
            const double relBeat = expressionRelBeatForX(clipId, note, pos.x);
            const double curveSemitones = evaluatePitchExpression(note.pitchExpression, relBeat);
            const double centerY = noteNumberToY(note.noteNumber) + noteHeight_ * 0.5;
            const double curveY = centerY - curveSemitones * noteHeight_;
            const double distance = std::abs(pos.y - curveY);

            if (distance <= noteHeight_ * 2.0 && distance < bestDistance) {
                bestDistance = distance;
                best = ExpressionHit{clipId, i, -1};
            }
        }
    }
    return best;
}

bool PianoRollGridComponent::handleExpressionMouseDown(const juce::MouseEvent& e) {
    auto& clipManager = ClipManager::getInstance();

    // Right-click: clear glide on the note under the mouse
    if (e.mods.isPopupMenu()) {
        auto hit = hitTestExpressionPoint(e.getPosition());
        if (!hit)
            hit = hitTestExpressionNote(e.getPosition());
        if (!hit)
            return false;

        const auto* clip = clipManager.getClip(hit->clipId);
        if (!clip || hit->noteIndex >= clip->midiNotes.size() ||
            !clip->midiNotes[hit->noteIndex].hasPitchExpression())
            return false;

        juce::PopupMenu menu;
        menu.addItem(1, "Clear Pitch Glide");
        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [this, clipId = hit->clipId, noteIndex = hit->noteIndex](int result) {
                               if (result == 1 && onPitchExpressionChanged)
                                   onPitchExpressionChanged(clipId, noteIndex, {});
                           });
        return true;
    }

    // Block edits on frozen tracks
    {
        const auto* clip = clipManager.getClip(clipId_);
        if (clip) {
            auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
            if (trackInfo && trackInfo->frozen)
                return true;
        }
    }

    // Grab an existing point
    if (auto hit = hitTestExpressionPoint(e.getPosition())) {
        const auto* clip = clipManager.getClip(hit->clipId);
        if (!clip || hit->noteIndex >= clip->midiNotes.size())
            return true;

        expressionClipId_ = hit->clipId;
        expressionNoteIndex_ = hit->noteIndex;
        expressionWorkingPoints_ = clip->midiNotes[hit->noteIndex].pitchExpression;
        expressionDragPointIndex_ = hit->pointIndex;
        isExpressionDragging_ = true;
        repaint();
        return true;
    }

    // Add a new point on the note under the mouse
    if (auto hit = hitTestExpressionNote(e.getPosition())) {
        const auto* clip = clipManager.getClip(hit->clipId);
        if (!clip || hit->noteIndex >= clip->midiNotes.size())
            return true;

        const auto& note = clip->midiNotes[hit->noteIndex];

        MidiPitchExpressionPoint newPoint;
        newPoint.beat = expressionRelBeatForX(hit->clipId, note, e.x);
        const double centerY = noteNumberToY(note.noteNumber) + noteHeight_ * 0.5;
        double semitones = (centerY - e.y) / noteHeight_;
        if (!e.mods.isShiftDown())
            semitones = std::round(semitones);
        newPoint.semitones = juce::jlimit(-MAX_PITCH_EXPRESSION_SEMITONES,
                                          MAX_PITCH_EXPRESSION_SEMITONES, semitones);

        expressionClipId_ = hit->clipId;
        expressionNoteIndex_ = hit->noteIndex;
        expressionWorkingPoints_ = note.pitchExpression;

        auto insertIt = std::upper_bound(
            expressionWorkingPoints_.begin(), expressionWorkingPoints_.end(), newPoint,
            [](const auto& a, const auto& b) { return a.beat < b.beat; });
        expressionDragPointIndex_ =
            static_cast<int>(std::distance(expressionWorkingPoints_.begin(), insertIt));
        expressionWorkingPoints_.insert(insertIt, newPoint);
        isExpressionDragging_ = true;
        repaint();
        return true;
    }

    return false;
}

void PianoRollGridComponent::handleExpressionMouseDrag(const juce::MouseEvent& e) {
    if (!isExpressionDragging_ || expressionDragPointIndex_ < 0 ||
        expressionDragPointIndex_ >= static_cast<int>(expressionWorkingPoints_.size()))
        return;

    const auto* clip = ClipManager::getInstance().getClip(expressionClipId_);
    if (!clip || expressionNoteIndex_ >= clip->midiNotes.size())
        return;

    const auto& note = clip->midiNotes[expressionNoteIndex_];
    auto& point = expressionWorkingPoints_[static_cast<size_t>(expressionDragPointIndex_)];

    // Horizontal: clamp between neighbouring points so ordering is stable
    double minBeat = 0.0;
    double maxBeat = note.lengthBeats;
    if (expressionDragPointIndex_ > 0)
        minBeat = expressionWorkingPoints_[static_cast<size_t>(expressionDragPointIndex_ - 1)].beat;
    if (expressionDragPointIndex_ + 1 < static_cast<int>(expressionWorkingPoints_.size()))
        maxBeat = expressionWorkingPoints_[static_cast<size_t>(expressionDragPointIndex_ + 1)].beat;

    const double noteStartDisplayBeat = displayBeatForClipBeat(expressionClipId_, note.startBeat);
    point.beat = juce::jlimit(minBeat, maxBeat, pixelToBeat(e.x) - noteStartDisplayBeat);

    // Vertical: semitone offset from the note row, snapped unless Shift
    const double centerY = noteNumberToY(note.noteNumber) + noteHeight_ * 0.5;
    double semitones = (centerY - e.y) / noteHeight_;
    if (!e.mods.isShiftDown())
        semitones = std::round(semitones);
    point.semitones =
        juce::jlimit(-MAX_PITCH_EXPRESSION_SEMITONES, MAX_PITCH_EXPRESSION_SEMITONES, semitones);

    repaint();
}

void PianoRollGridComponent::handleExpressionMouseUp(const juce::MouseEvent& /*e*/) {
    if (!isExpressionDragging_)
        return;

    commitExpressionEdit();

    isExpressionDragging_ = false;
    expressionDragPointIndex_ = -1;
    expressionClipId_ = INVALID_CLIP_ID;
    expressionWorkingPoints_.clear();
    repaint();
}

bool PianoRollGridComponent::handleExpressionDoubleClick(const juce::MouseEvent& e) {
    auto hit = hitTestExpressionPoint(e.getPosition());
    if (!hit)
        return false;

    const auto* clip = ClipManager::getInstance().getClip(hit->clipId);
    if (!clip || hit->noteIndex >= clip->midiNotes.size())
        return true;

    auto points = clip->midiNotes[hit->noteIndex].pitchExpression;
    if (hit->pointIndex >= 0 && hit->pointIndex < static_cast<int>(points.size())) {
        points.erase(points.begin() + hit->pointIndex);
        if (onPitchExpressionChanged)
            onPitchExpressionChanged(hit->clipId, hit->noteIndex, std::move(points));
    }
    return true;
}

void PianoRollGridComponent::commitExpressionEdit() {
    if (expressionClipId_ == INVALID_CLIP_ID || !onPitchExpressionChanged)
        return;

    auto points = expressionWorkingPoints_;
    std::sort(points.begin(), points.end(),
              [](const auto& a, const auto& b) { return a.beat < b.beat; });

    onPitchExpressionChanged(expressionClipId_, expressionNoteIndex_, std::move(points));
}

void PianoRollGridComponent::paintExpressionPointLabel(juce::Graphics& g, const MidiNote& note,
                                                       const MidiPitchExpressionPoint& point,
                                                       juce::Point<float> screen) {
    // Resulting pitch at this point: base note + glide offset
    const double totalPitch = note.noteNumber + point.semitones;
    const int nearestNote = juce::jlimit(0, 127, static_cast<int>(std::round(totalPitch)));
    const int cents = static_cast<int>(std::round((totalPitch - nearestNote) * 100.0));

    juce::String text = juce::MidiMessage::getMidiNoteName(nearestNote, true, true, 4);
    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents << "c";

    const juce::Font font(juce::FontOptions(11.0f));
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text);
    const float w = textWidth + 10.0f;
    const float h = 16.0f;

    // Above the point, clamped to the visible component bounds
    float x = screen.x - w * 0.5f;
    float y = screen.y - h - 8.0f;
    x = juce::jlimit(0.0f, static_cast<float>(getWidth()) - w, x);
    if (y < 0.0f)
        y = screen.y + 8.0f;

    juce::Rectangle<float> bubble(x, y, w, h);
    g.setColour(juce::Colour(0xEE202020));
    g.fillRoundedRectangle(bubble, 3.0f);
    g.setColour(juce::Colour(0xFF505050));
    g.drawRoundedRectangle(bubble, 3.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(text, bubble, juce::Justification::centred, false);
}

void PianoRollGridComponent::paintPitchExpression(juce::Graphics& g) {
    auto& clipManager = ClipManager::getInstance();

    for (ClipId clipId : clipIds_) {
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
            continue;

        const bool editable = pitchExpressionMode_ && isClipSelected(clipId);
        const juce::Colour curveColour = getColourForClip(clipId).brighter(0.6f);

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            auto visibleNote = clip->midiNotes[i];
            if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, visibleNote))
                continue;

            const auto& note = clip->midiNotes[i];

            // Use the in-progress working copy for the note being edited
            const bool isEditing =
                isExpressionDragging_ && clipId == expressionClipId_ && i == expressionNoteIndex_;
            const auto& points = isEditing ? expressionWorkingPoints_ : note.pitchExpression;

            if (points.empty() && !editable)
                continue;

            const double noteStartDisplayBeat = displayBeatForClipBeat(clipId, note.startBeat);
            const float startX = static_cast<float>(beatToPixel(noteStartDisplayBeat));
            const float endX =
                static_cast<float>(beatToPixel(noteStartDisplayBeat + note.lengthBeats));
            const float centerY = static_cast<float>(noteNumberToY(note.noteNumber)) +
                                  static_cast<float>(noteHeight_) * 0.5f;

            auto yForSemitones = [&](double semitones) {
                return centerY - static_cast<float>(semitones * noteHeight_);
            };

            juce::Path path;
            path.startNewSubPath(startX, yForSemitones(evaluatePitchExpression(points, 0.0)));
            for (const auto& p : points) {
                const float px = static_cast<float>(beatToPixel(noteStartDisplayBeat + p.beat));
                path.lineTo(juce::jlimit(startX, endX, px), yForSemitones(p.semitones));
            }
            path.lineTo(endX, yForSemitones(evaluatePitchExpression(points, note.lengthBeats)));

            if (pitchExpressionMode_) {
                const bool flat = points.empty();
                g.setColour(curveColour.withAlpha(flat ? 0.25f : 0.9f));
                g.strokePath(path, juce::PathStrokeType(flat ? 1.0f : 2.0f));

                if (editable) {
                    for (size_t p = 0; p < points.size(); ++p) {
                        auto screen = expressionPointToScreen(clipId, note, points[p]);
                        const float r = 3.5f;
                        g.setColour(juce::Colours::white);
                        g.fillEllipse(screen.x - r, screen.y - r, r * 2.0f, r * 2.0f);
                        g.setColour(curveColour.darker(0.6f));
                        g.drawEllipse(screen.x - r, screen.y - r, r * 2.0f, r * 2.0f, 1.0f);

                        // Pitch value label on the dragged or hovered point
                        const bool isDraggedPoint =
                            isEditing && static_cast<int>(p) == expressionDragPointIndex_;
                        const bool isHoveredPoint =
                            !isExpressionDragging_ && hoveredExpressionPoint_ &&
                            *hoveredExpressionPoint_ ==
                                ExpressionHit{clipId, i, static_cast<int>(p)};
                        if (isDraggedPoint || isHoveredPoint)
                            paintExpressionPointLabel(g, note, points[p], screen);
                    }
                }
            } else if (!points.empty()) {
                // Subtle reminder that this note carries a glide
                g.setColour(curveColour.withAlpha(0.35f));
                g.strokePath(path, juce::PathStrokeType(1.5f));
            }
        }
    }
}

}  // namespace magda
