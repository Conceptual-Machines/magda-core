#include "DrumGridClipContent.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "../../components/pianoroll/PhaseMarker.hpp"
#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/SelectionPolicy.hpp"
#include "AudioBridge.hpp"
#include "AudioEngine.hpp"
#include "BinaryData.h"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/DrumGridRoles.hpp"
#include "audio/plugins/DrumGridTemplates.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "core/ClipOperations.hpp"
#include "core/DrumkitManager.hpp"
#include "core/GestureRouter.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TimeBendPopup.hpp"
#include "ui/components/pianoroll/MidiDrawerComponent.hpp"
#include "ui/components/pianoroll/NoteComponent.hpp"
#include "ui/components/pianoroll/NoteGridHost.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/layout/LayoutConfig.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"

namespace magda::daw::ui {

//==============================================================================
// Helper: find DrumGridPlugin for a track
//==============================================================================
namespace {
namespace te = tracktion::engine;

// Lookup the (track, device) of the primary instrument plugin for the editing
// clip. Returns {INVALID_TRACK_ID, INVALID_DEVICE_ID} if there's no clip or no
// instrument on the track. Used by every drum-grid read/write of kit metadata.
struct PrimaryInstance {
    magda::TrackId trackId = magda::INVALID_TRACK_ID;
    magda::DeviceId deviceId = magda::INVALID_DEVICE_ID;
    const magda::DeviceInfo* device = nullptr;
    bool valid() const {
        return device != nullptr;
    }
};

PrimaryInstance primaryInstanceForClip(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID)
        return {};
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr)
        return {};
    auto* device = magda::TrackManager::getInstance().getPrimaryInstrument(clip->trackId);
    if (device == nullptr)
        return {};
    return {clip->trackId, device->id, device};
}

const magda::KitRow* findKitRow(const std::vector<magda::KitRow>& rows, int noteNumber) {
    auto it = std::find_if(rows.begin(), rows.end(), [noteNumber](const magda::KitRow& r) {
        return r.noteNumber == noteNumber;
    });
    return it == rows.end() ? nullptr : &(*it);
}

daw::audio::DrumGridPlugin* findDrumGridForTrack(magda::TrackId trackId) {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return nullptr;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return nullptr;

    auto* teTrack = bridge->getAudioTrack(trackId);
    if (!teTrack)
        return nullptr;

    for (auto* plugin : teTrack->pluginList) {
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin))
            return dg;

        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(innerPlugin))
                        return dg;
                }
            }
        }
    }
    return nullptr;
}

}  // namespace

//==============================================================================
// DrumGridClipGrid - the actual grid that renders drum hits
//==============================================================================
class DrumGridClipGrid : public juce::Component,
                         public magda::NoteGridHost,
                         public magda::ClipManagerListener {
  public:
    DrumGridClipGrid() {
        setName("DrumGridClipGrid");
        setWantsKeyboardFocus(true);
        magda::ClipManager::getInstance().addListener(this);
    }

    ~DrumGridClipGrid() override {
        magda::ClipManager::getInstance().removeListener(this);
        clearNoteComponents();
    }

    void setPixelsPerBeat(double ppb) {
        pixelsPerBeat_ = ppb;
        updateNoteComponentBounds();
        repaint();
    }
    void setRowHeight(int h) {
        rowHeight_ = h;
        updateNoteComponentBounds();
        repaint();
    }
    void setClipId(magda::ClipId id) {
        clipId_ = id;
    }
    void setPadRows(const std::vector<DrumGridClipContent::PadRow>* rows) {
        padRows_ = rows;
    }
    void setClipStartBeats(double b) {
        clipStartBeats_ = b;
        updateNoteComponentBounds();
        repaint();
    }
    void setClipLengthBeats(double b) {
        clipLengthBeats_ = b;
        repaint();
    }
    void setTimelineLengthBeats(double b) {
        timelineLengthBeats_ = b;
        repaint();
    }
    void setRelativeMode(bool relative) {
        if (relativeMode_ != relative) {
            relativeMode_ = relative;
            updateNoteComponentBounds();
            repaint();
        }
    }
    void setPlayheadPosition(double pos) {
        playheadPosition_ = pos;
        repaint();
    }
    void setGridResolutionBeats(double beats) {
        if (gridResolutionBeats_ != beats) {
            gridResolutionBeats_ = beats;
            repaint();
        }
    }
    void setSnapEnabled(bool enabled) {
        snapEnabled_ = enabled;
    }
    void setOverlayTracks(std::vector<magda::TrackId> trackIds) {
        overlayTrackIds_ = std::move(trackIds);
        repaint();
    }
    void setTimeSignatureNumerator(int n) {
        if (timeSigNumerator_ != n) {
            timeSigNumerator_ = n;
            repaint();
        }
    }
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled) {
        loopOffsetBeats_ = offsetBeats;
        loopLengthBeats_ = lengthBeats;
        loopEnabled_ = enabled;
        repaint();
    }
    void setPhasePreview(double beats, bool active) {
        phasePreviewBeats_ = beats;
        phasePreviewActive_ = active;
        repaint();
    }
    void setEditCursorPosition(double positionSeconds, bool blinkVisible) {
        editCursorPosition_ = positionSeconds;
        editCursorVisible_ = blinkVisible;
        repaint();
    }

    // Callbacks for parent
    std::function<void(magda::ClipId, double, int, double, int)> onNoteAdded;
    std::function<void(magda::ClipId, size_t)> onNoteDeleted;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteMoved;
    std::function<void(magda::ClipId, size_t, double)> onNoteResized;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteCopied;
    std::function<void(magda::ClipId, size_t, bool)> onNoteSelected;
    std::function<void(magda::ClipId, std::vector<size_t>)> onNoteSelectionChanged;
    std::function<void(magda::ClipId, std::vector<size_t>, magda::QuantizeMode, double)>
        onQuantizeNotes;
    std::function<void(magda::ClipId, std::vector<size_t>)> onCopyNotes;
    std::function<void(magda::ClipId)> onPasteNotes;
    std::function<void(magda::ClipId, std::vector<size_t>)> onDuplicateNotes;
    std::function<void(magda::ClipId, std::vector<size_t>)> onDeleteNotes;
    std::function<void(double)> onEditCursorSet;
    std::function<void(int, const juce::MouseWheelDetails&)> onVerticalZoomRequested;

    // Refresh note components from clip data
    void refreshNotes() {
        // Preserve selection: pending positions take priority, else keep current indices
        // Prefer SelectionManager as source of truth (handles duplicate, paste, etc.)
        std::set<size_t> selectedIndices;
        if (pendingSelectPositions_.empty()) {
            const auto& noteSel = magda::SelectionManager::getInstance().getNoteSelection();
            if (noteSel.isValid() && noteSel.clipId == clipId_ && !noteSel.noteIndices.empty()) {
                for (size_t idx : noteSel.noteIndices)
                    selectedIndices.insert(idx);
            } else {
                for (const auto& nc : noteComponents_) {
                    if (nc->isSelected())
                        selectedIndices.insert(nc->getNoteIndex());
                }
            }
        }

        auto pendingPositions = std::move(pendingSelectPositions_);
        pendingSelectPositions_.clear();

        clearNoteComponents();

        if (clipId_ == magda::INVALID_CLIP_ID || !padRows_ || padRows_->empty()) {
            repaint();
            return;
        }

        createNoteComponents();
        updateNoteComponentBounds();

        // Restore selection
        if (!pendingPositions.empty()) {
            // Select notes matching pending copy destinations
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
            if (clip) {
                for (auto& nc : noteComponents_) {
                    size_t idx = nc->getNoteIndex();
                    if (idx >= clip->midiNotes.size())
                        continue;
                    const auto& note = clip->midiNotes[idx];
                    for (const auto& pos : pendingPositions) {
                        if (std::abs(note.startBeat - pos.beat) < 0.001 &&
                            note.noteNumber == pos.noteNumber) {
                            nc->setSelected(true);
                            break;
                        }
                    }
                }
            }
        } else {
            // Restore previous index-based selection
            for (auto& nc : noteComponents_) {
                if (selectedIndices.count(nc->getNoteIndex()) > 0)
                    nc->setSelected(true);
            }
        }

        repaint();
    }

    void syncSelectionFromManager() {
        const auto& noteSel = magda::SelectionManager::getInstance().getNoteSelection();
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
        repaint();
    }

    // -- NoteGridHost overrides --

    double getPixelsPerBeat() const override {
        return pixelsPerBeat_;
    }

    int getNoteHeight() const override {
        return rowHeight_;
    }

    juce::Point<int> getGridScreenPosition() const override {
        return localPointToGlobal(juce::Point<int>());
    }

    // The drum grid has no folded pitch axis; a row delta is a plain semitone
    // delta (preserves the pre-fold behaviour).
    int noteNumberByRowDelta(int startNote, int rowsUp) const override {
        return juce::jlimit(0, 127, startNote + rowsUp);
    }

    void updateNotePosition(magda::NoteComponent* note, double beat, int noteNumber,
                            double length) override {
        if (!note || !padRows_)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(note->getSourceClipId());
        if (clip) {
            magda::MidiNote previewNote;
            previewNote.startBeat = beat;
            previewNote.noteNumber = noteNumber;
            previewNote.lengthBeats = length;
            if (!magda::ClipOperations::constrainMidiNoteToVisibleRange(*clip, previewNote)) {
                note->setVisible(false);
                return;
            }
            beat = previewNote.startBeat;
            noteNumber = previewNote.noteNumber;
            length = previewNote.lengthBeats;
            note->setVisible(true);
        }

        int rowIndex = findRowForNote(noteNumber);
        if (rowIndex < 0)
            return;

        int x = beatToPixel(clipBeatToDisplayBeat(beat));
        int y = rowIndex * rowHeight_;
        int w = juce::jmax(4, static_cast<int>(length * pixelsPerBeat_));
        int h = rowHeight_ - 2;

        note->setBounds(x, y + 1, w, h);
    }

    void setCopyDragPreview(double beat, int noteNumber, double length, juce::Colour colour,
                            bool active, size_t sourceNoteIndex) override {
        copyDragGhosts_.clear();
        if (!active) {
            repaint();
            return;
        }

        // Find the source note to compute the delta
        const auto* srcClip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!srcClip || sourceNoteIndex >= srcClip->midiNotes.size()) {
            copyDragGhosts_.push_back({beat, noteNumber, length, colour});
            repaint();
            return;
        }

        const auto& sourceNote = srcClip->midiNotes[sourceNoteIndex];
        double beatDelta = beat - sourceNote.startBeat;
        int noteDelta = noteNumber - sourceNote.noteNumber;

        auto addGhost = [&](double clipBeat, int ghostNote, double ghostLength) {
            magda::MidiNote previewNote;
            previewNote.startBeat = clipBeat;
            previewNote.noteNumber = ghostNote;
            previewNote.lengthBeats = ghostLength;
            if (!magda::ClipOperations::constrainMidiNoteToVisibleRange(*srcClip, previewNote))
                return;

            copyDragGhosts_.push_back(
                {previewNote.startBeat, previewNote.noteNumber, previewNote.lengthBeats, colour});
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
            int ghostNote = juce::jlimit(0, 127, otherNote.noteNumber + noteDelta);
            addGhost(ghostBeat, ghostNote, otherNote.lengthBeats);
        }

        repaint();
    }

    void updateSelectedNotePositions(magda::NoteComponent* draggedNote, double beatDelta,
                                     int noteDelta) override {
        if (!draggedNote)
            return;
        magda::ClipId dragClipId = draggedNote->getSourceClipId();
        const auto* clip = magda::ClipManager::getInstance().getClip(dragClipId);
        if (!clip)
            return;

        for (auto& nc : noteComponents_) {
            if (nc.get() == draggedNote || !nc->isSelected())
                continue;
            if (nc->getSourceClipId() != dragClipId)
                continue;
            size_t idx = nc->getNoteIndex();
            if (idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            double newBeat = juce::jmax(0.0, note.startBeat + beatDelta);
            int newNote = juce::jlimit(0, 127, note.noteNumber + noteDelta);
            updateNotePosition(nc.get(), newBeat, newNote, note.lengthBeats);
        }
    }

    void updateSelectedNoteLengths(magda::NoteComponent* draggedNote, double lengthDelta) override {
        if (!draggedNote)
            return;
        magda::ClipId dragClipId = draggedNote->getSourceClipId();
        const auto* clip = magda::ClipManager::getInstance().getClip(dragClipId);
        if (!clip)
            return;
        constexpr double MIN_LENGTH = 1.0 / 16.0;

        for (auto& nc : noteComponents_) {
            if (nc.get() == draggedNote || !nc->isSelected())
                continue;
            if (nc->getSourceClipId() != dragClipId)
                continue;
            size_t idx = nc->getNoteIndex();
            if (idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            double newLength = juce::jmax(MIN_LENGTH, note.lengthBeats + lengthDelta);
            updateNotePosition(nc.get(), note.startBeat, note.noteNumber, newLength);
        }
    }

    void updateSelectedNoteLeftResize(magda::NoteComponent* draggedNote,
                                      double lengthDelta) override {
        if (!draggedNote)
            return;
        magda::ClipId dragClipId = draggedNote->getSourceClipId();
        const auto* clip = magda::ClipManager::getInstance().getClip(dragClipId);
        if (!clip)
            return;
        constexpr double MIN_LENGTH = 1.0 / 16.0;
        double beatDelta = -lengthDelta;

        for (auto& nc : noteComponents_) {
            if (nc.get() == draggedNote || !nc->isSelected())
                continue;
            if (nc->getSourceClipId() != dragClipId)
                continue;
            size_t idx = nc->getNoteIndex();
            if (idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            double newLength = juce::jmax(MIN_LENGTH, note.lengthBeats + lengthDelta);
            double newStart = juce::jmax(0.0, note.startBeat + beatDelta);
            updateNotePosition(nc.get(), newStart, note.noteNumber, newLength);
        }
    }

    // -- ClipManagerListener --

    void clipsChanged() override {}
    void clipPropertyChanged(magda::ClipId clipId) override {
        if (clipId == clipId_) {
            // Defer refresh to avoid destroying NoteComponents while their
            // mouse handlers are still executing (use-after-free)
            juce::Component::SafePointer<DrumGridClipGrid> safeThis(this);
            juce::MessageManager::callAsync([safeThis]() {
                if (auto* self = safeThis.getComponent()) {
                    self->refreshNotes();
                }
            });
            return;
        }

        // Ghost overlay is paint-only, so a repaint is enough when an
        // overlay track's clip changes
        if (!overlayTrackIds_.empty()) {
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
            if (clip && std::find(overlayTrackIds_.begin(), overlayTrackIds_.end(),
                                  clip->trackId) != overlayTrackIds_.end()) {
                repaint();
            }
        }
    }
    void clipSelectionChanged(magda::ClipId) override {}

    // -- Component overrides --

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        if (!padRows_ || padRows_->empty())
            return;

        int numRows = static_cast<int>(padRows_->size());

        // Draw horizontal row lines
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        for (int i = 0; i <= numRows; ++i) {
            int y = i * rowHeight_;
            g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
        }

        // Draw vertical grid lines in three passes: subdivisions, beats, bars
        {
            double beatsVisible =
                static_cast<double>(bounds.getWidth() - GRID_LEFT_PADDING) / pixelsPerBeat_;
            float gridBottom = static_cast<float>(numRows * rowHeight_);
            int maxX = bounds.getWidth();
            int tsNum = timeSigNumerator_;

            // Pass 1: Subdivision lines at grid resolution (finest, drawn first)
            if (gridResolutionBeats_ > 0.0) {
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.45f));
                int numLines =
                    static_cast<int>(std::ceil((beatsVisible + 1.0) / gridResolutionBeats_));
                for (int i = 0; i <= numLines; i++) {
                    double beat = i * gridResolutionBeats_;
                    if (beat > beatsVisible + 1.0)
                        break;
                    if (std::abs(beat - std::round(beat)) < 0.001)
                        continue;
                    int x = static_cast<int>(beat * pixelsPerBeat_) + GRID_LEFT_PADDING;
                    if (x > maxX)
                        break;
                    g.drawVerticalLine(x, 0.0f, gridBottom);
                }
            }

            // Pass 2: Beat lines
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.55f));
            for (int b = 1; b <= static_cast<int>(beatsVisible) + 1; b++) {
                if (b % tsNum == 0)
                    continue;
                int x =
                    static_cast<int>(static_cast<double>(b) * pixelsPerBeat_) + GRID_LEFT_PADDING;
                if (x > maxX)
                    break;
                g.drawVerticalLine(x, 0.0f, gridBottom);
            }

            // Pass 3: Bar lines
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.85f));
            for (int bar = 0; bar * tsNum <= static_cast<int>(beatsVisible) + 1; bar++) {
                int x = static_cast<int>(static_cast<double>(bar * tsNum) * pixelsPerBeat_) +
                        GRID_LEFT_PADDING;
                if (x > maxX)
                    break;
                g.drawVerticalLine(x, 0.0f, gridBottom);
            }
        }

        // Ghost notes from overlay tracks (#1281) — painted before the
        // out-of-clip dimming so they recede outside the edited clip
        paintOverlayNotes(g);

        // Draw clip boundaries
        if (clipLengthBeats_ > 0.0) {
            int clipStartX = beatToPixel(relativeMode_ ? 0.0 : clipStartBeats_);
            int clipEndX = beatToPixel((relativeMode_ ? 0.0 : clipStartBeats_) + clipLengthBeats_);

            g.setColour(juce::Colours::black.withAlpha(0.3f));
            if (clipStartX > 0)
                g.fillRect(0, 0, clipStartX, numRows * rowHeight_);
            if (clipEndX < bounds.getWidth())
                g.fillRect(clipEndX, 0, bounds.getWidth() - clipEndX, numRows * rowHeight_);
        }

        // Draw loop region markers
        if (loopEnabled_ && loopLengthBeats_ > 0.0) {
            int loopStartX = beatToPixel(clipBeatToDisplayBeat(loopOffsetBeats_));
            int loopEndX = beatToPixel(clipBeatToDisplayBeat(loopOffsetBeats_ + loopLengthBeats_));

            juce::Colour loopColour = DarkTheme::getColour(DarkTheme::LOOP_MARKER);

            if (loopStartX >= 0 && loopStartX <= bounds.getWidth()) {
                g.setColour(loopColour);
                g.fillRect(loopStartX - 1, 0, 2, numRows * rowHeight_);
            }
            if (loopEndX >= 0 && loopEndX <= bounds.getWidth()) {
                g.setColour(loopColour);
                g.fillRect(loopEndX - 1, 0, 2, numRows * rowHeight_);
            }
        }

        // Draw content offset marker (yellow vertical line)
        if (clipId_ != magda::INVALID_CLIP_ID) {
            const auto* offsetClip = magda::ClipManager::getInstance().getClip(clipId_);
            if (offsetClip && offsetClip->loopEnabled) {
                double offset = phasePreviewActive_ ? phasePreviewBeats_ : offsetClip->midiOffset;
                int offsetX = beatToPixel(clipBeatToDisplayBeat(offset));
                if (offsetX >= 0 && offsetX <= bounds.getWidth()) {
                    magda::paintPhaseMarker(g, offsetClip, offsetX, numRows * rowHeight_,
                                            nearPhaseMarker_, phasePreviewActive_);
                }
            }
        }

        // Draw copy drag ghost previews
        for (const auto& ghost : copyDragGhosts_) {
            int rowIndex = findRowForNote(ghost.noteNumber);
            if (rowIndex >= 0) {
                int gx = beatToPixel(clipBeatToDisplayBeat(ghost.beat));
                int gy = rowIndex * rowHeight_;
                int gw = juce::jmax(4, static_cast<int>(ghost.length * pixelsPerBeat_));
                int gh = rowHeight_ - 2;

                g.setColour(ghost.colour.withAlpha(0.35f));
                g.fillRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                                       static_cast<float>(gw), static_cast<float>(gh), 2.0f);
                g.setColour(ghost.colour.withAlpha(0.6f));
                g.drawRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                                       static_cast<float>(gw), static_cast<float>(gh), 2.0f, 1.0f);
            }
        }

        paintRepeatStampPreview(g);

        // Draw edit cursor line (blinking white)
        if (editCursorPosition_ >= 0.0 && editCursorVisible_) {
            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double cursorBeat = editCursorPosition_ * (tempo / 60.0);
            int cursorX = beatToPixel(cursorBeat);

            if (cursorX >= 0 && cursorX <= bounds.getWidth()) {
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.drawLine(static_cast<float>(cursorX - 1), 0.f, static_cast<float>(cursorX - 1),
                           static_cast<float>(numRows * rowHeight_), 1.f);
                g.drawLine(static_cast<float>(cursorX + 1), 0.f, static_cast<float>(cursorX + 1),
                           static_cast<float>(numRows * rowHeight_), 1.f);
                g.setColour(juce::Colours::white);
                g.drawLine(static_cast<float>(cursorX), 0.f, static_cast<float>(cursorX),
                           static_cast<float>(numRows * rowHeight_), 2.f);
            }
        }

        // Draw playhead — only when within the clip's time range
        if (playheadPosition_ >= 0.0 && clipLengthBeats_ > 0.0) {
            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double playheadBeat = playheadPosition_ * (tempo / 60.0);
            double relBeat = playheadBeat - clipStartBeats_;

            if (relBeat >= 0.0 && relBeat <= clipLengthBeats_) {
                double displayBeat = relativeMode_ ? relBeat : playheadBeat;

                // Wrap playhead within loop region when looping is enabled
                if (loopEnabled_ && loopLengthBeats_ > 0.0) {
                    double beatPos = std::fmod(relBeat - loopOffsetBeats_, loopLengthBeats_);
                    if (beatPos < 0.0)
                        beatPos += loopLengthBeats_;
                    displayBeat = clipBeatToDisplayBeat(loopOffsetBeats_ + beatPos);
                }

                int playheadX = beatToPixel(displayBeat);

                if (playheadX >= 0 && playheadX <= bounds.getWidth()) {
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
                    g.fillRect(playheadX - 1, 0, 2, numRows * rowHeight_);
                }
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

    void resized() override {
        updateNoteComponentBounds();
    }

    void mouseMove(const juce::MouseEvent& e) override {
        if (e.mods.isShiftDown()) {
            setMouseCursor(magda::CursorManager::getInstance().getNoteRepeatCursor());
        } else if (e.mods.isAltDown() && isNearGridLine(e.x)) {
            setMouseCursor(juce::MouseCursor::IBeamCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }

        // Check proximity to phase marker for hover display
        bool wasNear = nearPhaseMarker_;
        nearPhaseMarker_ = false;

        if (clipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
            nearPhaseMarker_ = magda::isNearPhaseMarker(e.x, GRID_LEFT_PADDING, clip);
        }

        if (nearPhaseMarker_ != wasNear) {
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& /*e*/) override {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        if (nearPhaseMarker_) {
            nearPhaseMarker_ = false;
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        // Alt+wheel = vertical zoom (via GestureRouter, #1350); the callback
        // owns the zoom math, the binding only selects the action. A plain
        // wheel falls through to the viewport for content scroll.
        const auto gesture = magda::GestureRouter::getInstance().resolve(
            magda::GestureContext::DrumGrid, wheel, e.mods, e.getPosition());
        if (gesture.type == magda::GestureActionType::ZoomVertical && onVerticalZoomRequested) {
            onVerticalZoomRequested(e.y, wheel);
            return;
        }

        juce::Component::mouseWheelMove(e, wheel);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        isEditCursorClick_ = false;

        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID)
            return;

        // Right-click context menu
        if (e.mods.isPopupMenu()) {
            std::vector<size_t> selectedIndices;
            for (const auto& nc : noteComponents_) {
                if (nc->isSelected())
                    selectedIndices.push_back(nc->getNoteIndex());
            }

            juce::PopupMenu menu;
            bool hasSelection = !selectedIndices.empty();

            menu.addItem(10, "Copy", hasSelection);
            menu.addItem(11, "Paste", magda::ClipManager::getInstance().hasNotesInClipboard());
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
                juce::PopupMenu::Options(), [this, indices = std::move(selectedIndices),
                                             gridRes = gridResolutionBeats_](int result) {
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
                        const magda::QuantizeMode modes[] = {magda::QuantizeMode::StartOnly,
                                                             magda::QuantizeMode::LengthOnly,
                                                             magda::QuantizeMode::StartAndLength};
                        onQuantizeNotes(clipId_, indices, modes[result - 1], gridRes);
                    } else if (result >= 20 && result <= 61 && onQuantizeNotes) {
                        // clang-format off
                        const double gridBeats[] = {
                            4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                            3.0, 1.5, 0.75, 0.375,
                            4.0/3, 2.0/3, 1.0/3, 1.0/6,
                        };
                        // clang-format on
                        const magda::QuantizeMode modes[] = {magda::QuantizeMode::StartOnly,
                                                             magda::QuantizeMode::LengthOnly,
                                                             magda::QuantizeMode::StartAndLength};
                        int offset = result - 20;
                        onQuantizeNotes(clipId_, indices, modes[offset % 3], gridBeats[offset / 3]);
                    }
                });
            return;
        }

        // Alt + click on a grid line -> set edit cursor
        if (e.mods.isAltDown() && isNearGridLine(e.x)) {
            isEditCursorClick_ = true;
            return;
        }

        isDragSelecting_ = false;
        emptyClickRow_ = -1;
        isRepeatStamping_ = false;

        int row = e.y / rowHeight_;
        if (row >= 0 && row < static_cast<int>(padRows_->size())) {
            emptyClickRow_ = row;
            double rawBeat = displayBeatToClipBeat(pixelToBeat(e.x));
            if (rawBeat < 0.0)
                rawBeat = 0.0;
            emptyClickBeat_ = rawBeat;
            if (e.mods.isShiftDown()) {
                isRepeatStamping_ = true;
                repeatStampRow_ = row;
                repeatStampStartBeat_ = emptyClickBeat_;
                repeatStampEndBeat_ = emptyClickBeat_;
                setMouseCursor(magda::CursorManager::getInstance().getNoteRepeatCursor());
                repaint();
                return;
            }
        }
        dragSelectStart_ = e.getPosition();
        dragSelectEnd_ = e.getPosition();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (isEditCursorClick_)
            return;

        if (!padRows_ || padRows_->empty())
            return;

        if (isRepeatStamping_) {
            repeatStampEndBeat_ = displayBeatToClipBeat(pixelToBeat(e.x));
            repaint();
            return;
        }

        if (emptyClickRow_ >= 0) {
            isDragSelecting_ = true;
            dragSelectEnd_ = e.getPosition();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        // Don't deselect on right-click release (context menu was shown)
        if (e.mods.isPopupMenu()) {
            return;
        }

        // Grid line click -> set edit cursor position
        if (isEditCursorClick_) {
            isEditCursorClick_ = false;
            double gridBeat = getNearestGridLineBeat(e.x);

            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double positionSeconds = gridBeat * (60.0 / tempo);

            if (onEditCursorSet) {
                onEditCursorSet(positionSeconds);
            }
            return;
        }

        if (isRepeatStamping_) {
            repeatStampEndBeat_ = displayBeatToClipBeat(pixelToBeat(e.x));
            stampRepeatedNotes();
            isRepeatStamping_ = false;
            emptyClickRow_ = -1;
            repeatStampRow_ = -1;
            repaint();
            return;
        }

        if (isDragSelecting_) {
            // Rubber band selection
            auto selectionRect = juce::Rectangle<int>(dragSelectStart_, dragSelectEnd_);
            bool isAdditive = magda::isAdditiveMarqueeDrag(e.mods);

            if (!isAdditive) {
                for (auto& nc : noteComponents_)
                    nc->setSelected(false);
            }

            for (auto& nc : noteComponents_) {
                if (nc->getBounds().intersects(selectionRect))
                    nc->setSelected(true);
            }

            isDragSelecting_ = false;
            emptyClickRow_ = -1;
            fireSelectionChanged();
            repaint();
            return;
        }

        if (emptyClickRow_ >= 0) {
            // Plain click on empty cell -- add a note
            double addBeat = emptyClickBeat_;
            if (snapEnabled_ && gridResolutionBeats_ > 0.0)
                addBeat = std::floor(addBeat / gridResolutionBeats_) * gridResolutionBeats_;

            if (onNoteAdded)
                onNoteAdded(clipId_, addBeat, (*padRows_)[emptyClickRow_].noteNumber,
                            getDefaultNoteLengthBeats(), defaultNoteVelocity_);

            for (auto& nc : noteComponents_)
                nc->setSelected(false);
            fireSelectionChanged();
        } else {
            // Click on grid background -- deselect all
            if (!e.mods.isCommandDown() && !e.mods.isShiftDown()) {
                for (auto& nc : noteComponents_)
                    nc->setSelected(false);
                fireSelectionChanged();
            }
        }

        emptyClickRow_ = -1;
    }

    bool keyPressed(const juce::KeyPress& key) override {
        // Arrow up/down: move selected notes by semitone (or octave with Shift)
        // Alt+arrows reserved for viewport scrolling
        if (!key.getModifiers().isAltDown() && (key.getKeyCode() == juce::KeyPress::upKey ||
                                                key.getKeyCode() == juce::KeyPress::downKey)) {
            const auto& noteSel = magda::SelectionManager::getInstance().getNoteSelection();
            if (!noteSel.isValid())
                return false;

            int delta = (key.getKeyCode() == juce::KeyPress::upKey) ? 1 : -1;
            if (key.getModifiers().isShiftDown())
                delta *= 12;

            const auto* clip = magda::ClipManager::getInstance().getClip(noteSel.clipId);
            if (!clip || !clip->isMidi())
                return false;

            for (size_t idx : noteSel.noteIndices) {
                if (idx >= clip->midiNotes.size())
                    return false;
                int newNote = clip->midiNotes[idx].noteNumber + delta;
                if (newNote < 0 || newNote > 127)
                    return true;
            }

            for (size_t idx : noteSel.noteIndices) {
                const auto& note = clip->midiNotes[idx];
                auto cmd = std::make_unique<magda::MoveMidiNoteCommand>(
                    noteSel.clipId, idx, note.startBeat, note.noteNumber + delta);
                magda::UndoManager::getInstance().executeCommand(std::move(cmd));
            }
            return true;
        }

        // Cmd+A — Select all notes
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A') {
            if (clipId_ == magda::INVALID_CLIP_ID)
                return false;

            std::vector<size_t> allIndices;
            for (auto& nc : noteComponents_) {
                if (nc->getSourceClipId() == clipId_) {
                    nc->setSelected(true);
                    allIndices.push_back(nc->getNoteIndex());
                }
            }

            if (onNoteSelectionChanged)
                onNoteSelectionChanged(clipId_, allIndices);

            repaint();
            return true;
        }

        // Cmd+D — Duplicate selected notes
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D') {
            if (clipId_ == magda::INVALID_CLIP_ID)
                return true;

            std::vector<size_t> selectedIndices;
            for (const auto& nc : noteComponents_) {
                if (nc->isSelected() && nc->getSourceClipId() == clipId_)
                    selectedIndices.push_back(nc->getNoteIndex());
            }

            if (!selectedIndices.empty() && onDuplicateNotes)
                onDuplicateNotes(clipId_, selectedIndices);

            return true;
        }

        return false;
    }

  private:
    // Copy drag ghost preview state
    struct CopyDragGhost {
        double beat = 0.0;
        int noteNumber = 60;
        double length = 1.0;
        juce::Colour colour;
    };
    std::vector<CopyDragGhost> copyDragGhosts_;

    // Edit cursor click on grid line
    bool isEditCursorClick_ = false;
    static constexpr int GRID_LINE_HIT_TOLERANCE = 3;

    bool isNearGridLine(int mouseX) const {
        if (gridResolutionBeats_ <= 0.0)
            return false;
        double beat = pixelToBeat(mouseX);
        double nearestBeat = std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
        int gridX = beatToPixel(nearestBeat);
        return std::abs(mouseX - gridX) <= GRID_LINE_HIT_TOLERANCE;
    }

    double getNearestGridLineBeat(int mouseX) const {
        double beat = pixelToBeat(mouseX);
        if (gridResolutionBeats_ <= 0.0)
            return beat;
        return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
    }

    int beatToPixel(double beat) const {
        return static_cast<int>(std::round(beat * pixelsPerBeat_)) + GRID_LEFT_PADDING;
    }

    double pixelToBeat(int x) const {
        return static_cast<double>(x - GRID_LEFT_PADDING) / pixelsPerBeat_;
    }

    double clipBeatToDisplayBeat(double beat) const {
        double visibleStart = 0.0;
        if (const auto* clip = magda::ClipManager::getInstance().getClip(clipId_))
            visibleStart = magda::ClipOperations::getMidiVisibleRange(*clip).startBeat;
        return relativeMode_ ? beat - visibleStart : clipStartBeats_ + beat - visibleStart;
    }

    double displayBeatToClipBeat(double beat) const {
        double visibleStart = 0.0;
        if (const auto* clip = magda::ClipManager::getInstance().getClip(clipId_))
            visibleStart = magda::ClipOperations::getMidiVisibleRange(*clip).startBeat;
        return juce::jmax(0.0, relativeMode_ ? beat + visibleStart
                                             : beat - clipStartBeats_ + visibleStart);
    }

    // Rubber band selection state
    bool isDragSelecting_ = false;
    juce::Point<int> dragSelectStart_;
    juce::Point<int> dragSelectEnd_;
    int emptyClickRow_ = -1;
    double emptyClickBeat_ = 0.0;
    bool isRepeatStamping_ = false;
    int repeatStampRow_ = -1;
    double repeatStampStartBeat_ = 0.0;
    double repeatStampEndBeat_ = 0.0;
    double defaultNoteLengthBeats_ = 0.0;  // <= 0 follows current grid
    int defaultNoteVelocity_ = 100;

    static constexpr int GRID_LEFT_PADDING = magda::LayoutConfig::MIDI_GRID_LEFT_PADDING;
    double pixelsPerBeat_ = 50.0;
    int rowHeight_ = 24;
    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;
    const std::vector<DrumGridClipContent::PadRow>* padRows_ = nullptr;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;
    double timelineLengthBeats_ = 0.0;
    double playheadPosition_ = -1.0;
    double editCursorPosition_ = -1.0;  // seconds, -1 = hidden
    bool editCursorVisible_ = true;     // blink state
    double gridResolutionBeats_ = 0.25;
    bool snapEnabled_ = true;
    int timeSigNumerator_ = 4;
    bool relativeMode_ = true;

    // Loop region
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;
    bool nearPhaseMarker_ = false;

    // Phase preview during drag
    double phasePreviewBeats_ = 0.0;
    bool phasePreviewActive_ = false;

    // Note components
    std::vector<std::unique_ptr<magda::NoteComponent>> noteComponents_;

    // Tracks whose MIDI renders as a ghost overlay (paint-only, never interactive)
    std::vector<magda::TrackId> overlayTrackIds_;

    // Pending selection for copy operations (matched by position after refresh)
    struct PendingSelectPos {
        double beat;
        int noteNumber;
    };
    std::vector<PendingSelectPos> pendingSelectPositions_;

    int findRowForNote(int noteNumber) const {
        if (!padRows_)
            return -1;
        for (int r = 0; r < static_cast<int>(padRows_->size()); ++r) {
            if ((*padRows_)[r].noteNumber == noteNumber)
                return r;
        }
        return -1;
    }

    void paintOverlayNotes(juce::Graphics& g) {
        if (overlayTrackIds_.empty() || !padRows_ || padRows_->empty())
            return;

        auto& clipManager = magda::ClipManager::getInstance();

        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent())
            tempo = controller->getState().tempo.bpm;

        // The edited clip's track renders as full note components already
        magda::TrackId activeTrackId = magda::INVALID_TRACK_ID;
        if (const auto* editedClip = clipManager.getClip(clipId_))
            activeTrackId = editedClip->trackId;

        const auto visibleArea = g.getClipBounds();

        for (magda::TrackId overlayTrackId : overlayTrackIds_) {
            if (overlayTrackId == activeTrackId)
                continue;

            for (magda::ClipId overlayClipId : clipManager.getClipsOnTrack(overlayTrackId)) {
                const auto* clip = clipManager.getClip(overlayClipId);
                if (!clip || !clip->isMidi())
                    continue;

                // Ghost notes take the source clip's colour
                const auto colour = clip->colour;

                // Position notes on the shared timeline, shifted by the
                // edited clip's start in relative mode
                const double visibleStart =
                    magda::ClipOperations::getMidiVisibleRange(*clip).startBeat;
                double clipOffsetBeats = clip->getStartBeats(tempo);
                if (relativeMode_)
                    clipOffsetBeats -= clipStartBeats_;

                for (auto note : clip->midiNotes) {
                    if (!magda::ClipOperations::clipMidiNoteToVisibleRange(*clip, note))
                        continue;

                    int rowIndex = findRowForNote(note.noteNumber);
                    if (rowIndex < 0)
                        continue;

                    const double displayBeat = clipOffsetBeats + note.startBeat - visibleStart;
                    const int x = beatToPixel(displayBeat);
                    const int w =
                        juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
                    if (x + w < visibleArea.getX() || x > visibleArea.getRight())
                        continue;

                    const int y = rowIndex * rowHeight_;
                    const auto rect = juce::Rectangle<float>(
                        static_cast<float>(x), static_cast<float>(y + 1), static_cast<float>(w),
                        static_cast<float>(rowHeight_ - 2));
                    g.setColour(colour.withAlpha(0.22f));
                    g.fillRoundedRectangle(rect, 2.0f);
                    g.setColour(colour.withAlpha(0.45f));
                    g.drawRoundedRectangle(rect, 2.0f, 1.0f);
                }
            }
        }
    }

    void fireSelectionChanged() {
        if (!onNoteSelectionChanged)
            return;
        std::vector<size_t> selected;
        for (const auto& nc : noteComponents_) {
            if (nc->isSelected())
                selected.push_back(nc->getNoteIndex());
        }
        onNoteSelectionChanged(clipId_, selected);
    }

    double snapBeatToGrid(double beat) const {
        if (!snapEnabled_ || gridResolutionBeats_ <= 0.0)
            return beat;
        return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
    }

    double getDefaultNoteLengthBeats() const {
        if (defaultNoteLengthBeats_ > 0.0)
            return defaultNoteLengthBeats_;
        return juce::jmax(1.0 / 16.0, gridResolutionBeats_);
    }

    void addDefaultNoteMenuItems(juce::PopupMenu& menu) const {
        juce::PopupMenu lengthMenu;
        lengthMenu.addItem(100, "Current Grid", true, defaultNoteLengthBeats_ <= 0.0);

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
                               std::abs(defaultNoteLengthBeats_ - option.beats) < 0.000001);
        menu.addSubMenu("Default Length", lengthMenu);

        juce::PopupMenu velocityMenu;
        const int velocities[] = {127, 120, 100, 96, 80, 64, 48, 32, 16};
        for (int velocity : velocities)
            velocityMenu.addItem(200 + velocity, juce::String(velocity), true,
                                 defaultNoteVelocity_ == velocity);
        menu.addSubMenu("Default Velocity", velocityMenu);
    }

    bool handleDefaultNoteMenuResult(int result) {
        if (result == 100) {
            defaultNoteLengthBeats_ = 0.0;
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
                return true;
            }
        }

        if (result >= 201 && result <= 327) {
            defaultNoteVelocity_ = juce::jlimit(1, 127, result - 200);
            return true;
        }

        return false;
    }

    std::vector<double> repeatStampBeats() const {
        std::vector<double> beats;
        const double step = juce::jmax(1.0 / 64.0, gridResolutionBeats_);
        double start = juce::jmin(repeatStampStartBeat_, repeatStampEndBeat_);
        double end = juce::jmax(repeatStampStartBeat_, repeatStampEndBeat_);
        if (snapEnabled_ && gridResolutionBeats_ > 0.0) {
            start = std::floor(start / gridResolutionBeats_) * gridResolutionBeats_;
            end = std::floor(end / gridResolutionBeats_) * gridResolutionBeats_;
        }

        for (double beat = start; beat <= end + 0.000001; beat += step)
            beats.push_back(juce::jmax(0.0, beat));

        return beats;
    }

    bool hasNoteAt(int noteNumber, double beat) const {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return false;

        for (const auto& note : clip->midiNotes) {
            if (note.noteNumber == noteNumber && std::abs(note.startBeat - beat) < 0.000001)
                return true;
        }
        return false;
    }

    void stampRepeatedNotes() {
        if (!onNoteAdded || !padRows_ || repeatStampRow_ < 0 ||
            repeatStampRow_ >= static_cast<int>(padRows_->size()))
            return;

        const int noteNumber = (*padRows_)[repeatStampRow_].noteNumber;
        std::vector<double> beats;
        for (double beat : repeatStampBeats()) {
            if (!hasNoteAt(noteNumber, beat)) {
                beats.push_back(beat);
            }
        }

        if (beats.size() > 1)
            magda::UndoManager::getInstance().beginCompoundOperation("Stamp Drum Notes");

        for (double beat : beats)
            onNoteAdded(clipId_, beat, noteNumber, getDefaultNoteLengthBeats(),
                        defaultNoteVelocity_);

        if (beats.size() > 1)
            magda::UndoManager::getInstance().endCompoundOperation();
    }

    void paintRepeatStampPreview(juce::Graphics& g) {
        if (!isRepeatStamping_ || !padRows_ || repeatStampRow_ < 0 ||
            repeatStampRow_ >= static_cast<int>(padRows_->size()))
            return;

        const int y = repeatStampRow_ * rowHeight_;
        const int width =
            juce::jmax(4, static_cast<int>(getDefaultNoteLengthBeats() * pixelsPerBeat_));
        const int height = rowHeight_ - 2;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
        for (double beat : repeatStampBeats()) {
            const int x = beatToPixel(clipBeatToDisplayBeat(beat));
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                   static_cast<float>(width), static_cast<float>(height), 2.0f);
        }
    }

    void createNoteComponents() {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip || !clip->isMidi() || !padRows_)
            return;

        auto noteColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

        for (size_t i = 0; i < clip->midiNotes.size(); i++) {
            auto visibleNote = clip->midiNotes[i];
            if (!magda::ClipOperations::clipMidiNoteToVisibleRange(*clip, visibleNote))
                continue;

            auto noteComp = std::make_unique<magda::NoteComponent>(i, this, clipId_);

            noteComp->onNoteSelected = [this](size_t index, bool isAdditive) {
                if (!isAdditive) {
                    for (auto& nc : noteComponents_) {
                        if (nc->getNoteIndex() != index)
                            nc->setSelected(false);
                    }
                }
                if (onNoteSelected)
                    onNoteSelected(clipId_, index, isAdditive);
                fireSelectionChanged();
            };

            noteComp->onNoteRangeSelected = [this](size_t index) {
                magda::SelectionManager::getInstance().extendNoteSelectionTo(clipId_, index);
                syncSelectionFromManager();
                fireSelectionChanged();
            };

            noteComp->onNoteDeselected = [this](size_t /*index*/) { fireSelectionChanged(); };

            noteComp->onNoteMoved = [this](size_t index, double newBeat, int newNoteNumber) {
                if (!onNoteMoved)
                    return;

                const auto* srcClip = magda::ClipManager::getInstance().getClip(clipId_);
                if (!srcClip || index >= srcClip->midiNotes.size()) {
                    onNoteMoved(clipId_, index, newBeat, newNoteNumber);
                    return;
                }

                const auto& sourceNote = srcClip->midiNotes[index];
                double beatDelta = newBeat - sourceNote.startBeat;
                int noteDelta = newNoteNumber - sourceNote.noteNumber;

                // Move the dragged note
                onNoteMoved(clipId_, index, newBeat, newNoteNumber);

                // Move other selected notes with the same delta
                for (auto& nc : noteComponents_) {
                    if (nc->getNoteIndex() == index)
                        continue;
                    if (!nc->isSelected())
                        continue;

                    size_t otherIndex = nc->getNoteIndex();
                    if (otherIndex >= srcClip->midiNotes.size())
                        continue;

                    const auto& otherNote = srcClip->midiNotes[otherIndex];
                    double otherNewBeat = juce::jmax(0.0, otherNote.startBeat + beatDelta);
                    int otherNewNote = juce::jlimit(0, 127, otherNote.noteNumber + noteDelta);

                    onNoteMoved(clipId_, otherIndex, otherNewBeat, otherNewNote);
                }
            };

            noteComp->onNoteCopied = [this](size_t index, double destBeat, int destNoteNumber) {
                if (!onNoteCopied)
                    return;

                const auto* srcClip = magda::ClipManager::getInstance().getClip(clipId_);
                if (!srcClip || index >= srcClip->midiNotes.size()) {
                    onNoteCopied(clipId_, index, destBeat, destNoteNumber);
                    pendingSelectPositions_.push_back({destBeat, destNoteNumber});
                    return;
                }

                const auto& sourceNote = srcClip->midiNotes[index];
                double beatDelta = destBeat - sourceNote.startBeat;
                int noteDelta = destNoteNumber - sourceNote.noteNumber;

                // Copy the dragged note
                onNoteCopied(clipId_, index, destBeat, destNoteNumber);
                pendingSelectPositions_.push_back({destBeat, destNoteNumber});

                // Copy other selected notes with the same delta
                for (auto& nc : noteComponents_) {
                    if (nc->getNoteIndex() == index)
                        continue;
                    if (!nc->isSelected())
                        continue;

                    size_t otherIndex = nc->getNoteIndex();
                    if (otherIndex >= srcClip->midiNotes.size())
                        continue;

                    const auto& otherNote = srcClip->midiNotes[otherIndex];
                    double otherDestBeat = juce::jmax(0.0, otherNote.startBeat + beatDelta);
                    int otherDestNote = juce::jlimit(0, 127, otherNote.noteNumber + noteDelta);

                    onNoteCopied(clipId_, otherIndex, otherDestBeat, otherDestNote);
                    pendingSelectPositions_.push_back({otherDestBeat, otherDestNote});
                }
            };

            noteComp->onNoteResized = [this](size_t index, double newLength, bool fromStart) {
                (void)fromStart;
                if (onNoteResized)
                    onNoteResized(clipId_, index, newLength);
            };

            noteComp->onNoteDeleted = [this](size_t index) {
                std::vector<size_t> selectedIndices;
                bool indexIsSelected = false;
                for (const auto& nc : noteComponents_) {
                    if (nc->isSelected()) {
                        selectedIndices.push_back(nc->getNoteIndex());
                        if (nc->getNoteIndex() == index)
                            indexIsSelected = true;
                    }
                }

                if (indexIsSelected && selectedIndices.size() > 1 && onDeleteNotes)
                    onDeleteNotes(clipId_, selectedIndices);
                else if (onNoteDeleted)
                    onNoteDeleted(clipId_, index);
            };

            noteComp->snapBeatToGrid = [this](double beat) { return snapBeatToGrid(beat); };

            noteComp->onRightClick = [this](size_t /*index*/, const juce::MouseEvent& /*event*/) {
                std::vector<size_t> selectedIndices;
                for (const auto& nc : noteComponents_) {
                    if (nc->isSelected())
                        selectedIndices.push_back(nc->getNoteIndex());
                }

                juce::PopupMenu menu;
                bool hasSelection = !selectedIndices.empty();

                menu.addItem(10, "Copy", hasSelection);
                menu.addItem(11, "Paste", magda::ClipManager::getInstance().hasNotesInClipboard());
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
                    juce::PopupMenu::Options(), [this, indices = std::move(selectedIndices),
                                                 gridRes = gridResolutionBeats_](int result) {
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
                            const magda::QuantizeMode modes[] = {
                                magda::QuantizeMode::StartOnly, magda::QuantizeMode::LengthOnly,
                                magda::QuantizeMode::StartAndLength};
                            onQuantizeNotes(clipId_, indices, modes[result - 1], gridRes);
                        } else if (result >= 20 && result <= 61 && onQuantizeNotes) {
                            // clang-format off
                            const double gridBeats[] = {
                                4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                3.0, 1.5, 0.75, 0.375,
                                4.0/3, 2.0/3, 1.0/3, 1.0/6,
                            };
                            // clang-format on
                            const magda::QuantizeMode modes[] = {
                                magda::QuantizeMode::StartOnly, magda::QuantizeMode::LengthOnly,
                                magda::QuantizeMode::StartAndLength};
                            int offset = result - 20;
                            onQuantizeNotes(clipId_, indices, modes[offset % 3],
                                            gridBeats[offset / 3]);
                        }
                    });
            };

            noteComp->updateFromNote(visibleNote, noteColour);
            addAndMakeVisible(noteComp.get());
            noteComponents_.push_back(std::move(noteComp));
        }
    }

    void clearNoteComponents() {
        for (auto& noteComp : noteComponents_)
            removeChildComponent(noteComp.get());
        noteComponents_.clear();
    }

    void updateNoteComponentBounds() {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(clipId_);
        if (!clip || !padRows_)
            return;

        auto noteColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

        for (auto& noteComp : noteComponents_) {
            size_t noteIndex = noteComp->getNoteIndex();
            if (noteIndex >= clip->midiNotes.size())
                continue;

            auto note = clip->midiNotes[noteIndex];
            if (!magda::ClipOperations::clipMidiNoteToVisibleRange(*clip, note)) {
                noteComp->setVisible(false);
                continue;
            }
            int rowIndex = findRowForNote(note.noteNumber);
            if (rowIndex < 0)
                continue;

            int x = beatToPixel(clipBeatToDisplayBeat(note.startBeat));
            int y = rowIndex * rowHeight_;
            int w = juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
            int h = rowHeight_ - 2;

            noteComp->setBounds(x, y + 1, w, h);
            noteComp->updateFromNote(note, noteColour);
            noteComp->setVisible(true);
        }
    }
};

//==============================================================================
// DrumGridRowLabels - left sidebar showing pad names
//==============================================================================
class DrumGridRowLabels : public juce::Component {
  public:
    DrumGridRowLabels() {
        setName("DrumGridRowLabels");
    }

    void setPadRows(const std::vector<DrumGridClipContent::PadRow>* rows) {
        padRows_ = rows;
        repaint();
    }
    void setRowHeight(int h) {
        rowHeight_ = h;
        repaint();
    }
    void setScrollOffset(int y) {
        scrollOffsetY_ = y;
        repaint();
    }

    // Live-input highlight: tints the pad row while a monitored note is held,
    // mirroring the piano roll keyboard's pressed-key highlight.
    void setNotePressed(int noteNumber, bool pressed) {
        const bool changed = pressed ? pressedNotes_.insert(noteNumber).second
                                     : (pressedNotes_.erase(noteNumber) > 0);
        if (changed)
            repaint();
    }
    void clearPressedNotes() {
        if (!pressedNotes_.empty()) {
            pressedNotes_.clear();
            repaint();
        }
    }

    // Callback: noteNumber, isNoteOn
    std::function<void(int, bool)> onNotePreview;
    std::function<void(int, const juce::MouseWheelDetails&)> onVerticalZoomRequested;
    std::function<void(int /*noteNumber*/, juce::String /*newLabel*/)> onRowLabelCommitted;
    std::function<void(int /*noteNumber*/, juce::Point<int> /*screenPos*/)> onRowContextMenu;

    // Initial label text to seed the inline editor with. Set by the parent.
    std::function<juce::String(int /*noteNumber*/)> getRowLabel;

    void startRenameRow(int noteNumber) {
        if (!padRows_)
            return;
        int rowIndex = -1;
        for (int i = 0; i < static_cast<int>(padRows_->size()); ++i) {
            if ((*padRows_)[static_cast<size_t>(i)].noteNumber == noteNumber) {
                rowIndex = i;
                break;
            }
        }
        if (rowIndex < 0)
            return;

        rowEditor_ = std::make_unique<juce::TextEditor>();
        rowEditor_->setMultiLine(false);
        rowEditor_->setReturnKeyStartsNewLine(false);
        rowEditor_->setBorder({1, 2, 1, 2});
        rowEditor_->setIndents(4, 1);
        rowEditor_->setColour(juce::TextEditor::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND));
        rowEditor_->setColour(juce::TextEditor::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        rowEditor_->setColour(juce::TextEditor::outlineColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        rowEditor_->setColour(juce::TextEditor::focusedOutlineColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        juce::String seed;
        if (getRowLabel)
            seed = getRowLabel(noteNumber);
        if (seed.isEmpty())
            seed = (*padRows_)[static_cast<size_t>(rowIndex)].name;
        rowEditor_->setText(seed, juce::dontSendNotification);
        rowEditor_->selectAll();

        int y = rowIndex * rowHeight_ - scrollOffsetY_;
        rowEditor_->setBounds(0, y, juce::jmax(0, getWidth() - PLAY_BTN_WIDTH), rowHeight_);
        addAndMakeVisible(rowEditor_.get());
        rowEditor_->grabKeyboardFocus();

        editingNote_ = noteNumber;
        rowEditor_->onReturnKey = [this]() { commitRename(); };
        rowEditor_->onEscapeKey = [this]() { cancelRename(); };
        rowEditor_->onFocusLost = [this]() { commitRename(); };
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));

        if (!padRows_ || padRows_->empty())
            return;

        auto font = magda::FontManager::getInstance().getUIFont(
            static_cast<float>(juce::jlimit(8, 12, rowHeight_ - 4)));
        g.setFont(font);

        int numRows = static_cast<int>(padRows_->size());
        for (int i = 0; i < numRows; ++i) {
            int y = i * rowHeight_ - scrollOffsetY_;
            if (y + rowHeight_ < 0 || y > bounds.getHeight())
                continue;

            // Alternating row background
            if (i % 2 == 0) {
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.08f));
                g.fillRect(0, y, bounds.getWidth(), rowHeight_);
            }

            // Row separator
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
            g.drawHorizontalLine(y + rowHeight_, 0.0f, static_cast<float>(bounds.getWidth()));

            const auto& padRow = (*padRows_)[i];

            // Live-input highlight for monitored notes currently held.
            if (pressedNotes_.count(padRow.noteNumber) != 0) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
                g.fillRect(0, y, bounds.getWidth(), rowHeight_);
            }

            if (rowHeight_ >= 10) {
                int textX = 4;
                int textRight = bounds.getWidth() - PLAY_BTN_WIDTH - 4;

                // Role short-tag pill (drawn before the label, if role is set)
                if (padRow.role.isNotEmpty()) {
                    auto shortTag =
                        magda::daw::audio::drum_grid_roles::shortTagForRole(padRow.role);
                    if (shortTag.isNotEmpty()) {
                        const int pillH = juce::jmin(rowHeight_ - 4, 14);
                        const int pillW = juce::jmax(18, shortTag.length() * 8 + 8);
                        juce::Rectangle<float> pill(
                            static_cast<float>(textX),
                            static_cast<float>(y + (rowHeight_ - pillH) / 2),
                            static_cast<float>(pillW), static_cast<float>(pillH));
                        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.85f));
                        g.fillRoundedRectangle(pill, 3.0f);
                        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
                        g.drawText(shortTag, pill.toNearestInt(), juce::Justification::centred,
                                   false);
                        textX += pillW + 4;
                    }
                }

                // Pad name (after the role pill)
                g.setColour(padRow.hasChain ? DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)
                                            : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(padRow.name,
                           juce::Rectangle<int>(textX, y + 1, juce::jmax(0, textRight - textX),
                                                rowHeight_ - 2),
                           juce::Justification::centredLeft, true);
            }

            // Play button (small triangle on the right)
            auto btnBounds = getPlayButtonBounds(i);
            bool isPlaying = (playingNoteNumber_ == padRow.noteNumber);
            bool isHovered = (hoverRow_ == i);

            if (isPlaying) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            } else if (isHovered) {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.7f));
            } else {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.3f));
            }

            // Draw play triangle
            auto triArea = btnBounds.toFloat().reduced(4.0f, rowHeight_ >= 10 ? 5.0f : 2.0f);
            juce::Path triangle;
            triangle.addTriangle(triArea.getX(), triArea.getY(), triArea.getX(),
                                 triArea.getBottom(), triArea.getRight(), triArea.getCentreY());
            g.fillPath(triangle);
        }

        // Right border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawVerticalLine(bounds.getWidth() - 1, 0.0f, static_cast<float>(bounds.getHeight()));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int row = getRowAtY(e.y);
        if (row < 0 || !padRows_)
            return;

        int noteNumber = (*padRows_)[static_cast<size_t>(row)].noteNumber;

        if (e.mods.isPopupMenu()) {
            if (onRowContextMenu)
                onRowContextMenu(noteNumber, e.getScreenPosition());
            return;
        }

        auto btnBounds = getPlayButtonBounds(row);
        if (btnBounds.contains(e.getPosition())) {
            playingNoteNumber_ = noteNumber;
            if (onNotePreview)
                onNotePreview(noteNumber, true);
            repaint();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!padRows_)
            return;
        int row = getRowAtY(e.y);
        if (row < 0)
            return;
        // Ignore double-clicks on the play button.
        if (getPlayButtonBounds(row).contains(e.getPosition()))
            return;
        startRenameRow((*padRows_)[static_cast<size_t>(row)].noteNumber);
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override {
        if (playingNoteNumber_ >= 0) {
            if (onNotePreview)
                onNotePreview(playingNoteNumber_, false);
            playingNoteNumber_ = -1;
            repaint();
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int row = getRowAtY(e.y);
        if (row != hoverRow_) {
            hoverRow_ = row;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& /*e*/) override {
        if (hoverRow_ >= 0) {
            hoverRow_ = -1;
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        // Alt+wheel = vertical zoom (via GestureRouter, #1350); the callback
        // owns the zoom math, the binding only selects the action. A plain
        // wheel falls through to the viewport for content scroll.
        const auto gesture = magda::GestureRouter::getInstance().resolve(
            magda::GestureContext::DrumGrid, wheel, e.mods, e.getPosition());
        if (gesture.type == magda::GestureActionType::ZoomVertical && onVerticalZoomRequested) {
            onVerticalZoomRequested(e.y, wheel);
            return;
        }

        juce::Component::mouseWheelMove(e, wheel);
    }

  private:
    static constexpr int PLAY_BTN_WIDTH = 16;

    const std::vector<DrumGridClipContent::PadRow>* padRows_ = nullptr;
    int rowHeight_ = 24;
    int scrollOffsetY_ = 0;
    int playingNoteNumber_ = -1;
    int hoverRow_ = -1;
    std::set<int> pressedNotes_;  // live-monitored notes currently held
    std::unique_ptr<juce::TextEditor> rowEditor_;
    int editingNote_ = -1;

    void commitRename() {
        if (rowEditor_ == nullptr || editingNote_ < 0)
            return;
        const int note = editingNote_;
        const auto newText = rowEditor_->getText().trim();
        editingNote_ = -1;
        // Defer destruction so we don't free the editor inside its own focus-lost callback.
        juce::MessageManager::callAsync(
            [weak = juce::Component::SafePointer<DrumGridRowLabels>(this)]() {
                if (weak == nullptr)
                    return;
                weak->rowEditor_.reset();
                weak->repaint();
            });
        if (onRowLabelCommitted)
            onRowLabelCommitted(note, newText);
    }

    void cancelRename() {
        editingNote_ = -1;
        rowEditor_.reset();
        repaint();
    }

    int getRowAtY(int y) const {
        if (!padRows_ || padRows_->empty())
            return -1;
        int row = (y + scrollOffsetY_) / rowHeight_;
        if (row < 0 || row >= static_cast<int>(padRows_->size()))
            return -1;
        return row;
    }

    juce::Rectangle<int> getPlayButtonBounds(int row) const {
        int y = row * rowHeight_ - scrollOffsetY_;
        return {getWidth() - PLAY_BTN_WIDTH, y, PLAY_BTN_WIDTH, rowHeight_};
    }
};

//==============================================================================
// DrumGridLabelDivider - thin vertical strip between row labels and the grid
// that drags to resize the labels column.
//==============================================================================
class DrumGridLabelDivider : public juce::Component {
  public:
    DrumGridLabelDivider() {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    std::function<void(int /*deltaX*/)> onDragDelta;

    void paint(juce::Graphics& g) override {
        // Idle: invisible (inherits the parent's bg). Hover/drag: subtle accent
        // so the user can see the hit zone they grabbed.
        if (isMouseOverOrDragging()) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            g.fillRect(getLocalBounds());
        }
    }

    void mouseDown(const juce::MouseEvent&) override {
        dragStartX_ = 0;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (onDragDelta)
            onDragDelta(e.getDistanceFromDragStartX() - dragStartX_);
        dragStartX_ = e.getDistanceFromDragStartX();
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragStartX_ = 0;
    }

  private:
    int dragStartX_ = 0;
};

//==============================================================================
// DrumGridClipContent implementation
//==============================================================================
DrumGridClipContent::DrumGridClipContent() {
    setName("DrumGridClipContent");
    if (timeRuler_)
        timeRuler_->setGestureContext(magda::GestureContext::DrumGrid);

    // Create controls toggle button (bar chart icon)
    controlsToggle_ = std::make_unique<magda::SvgButton>(
        "ControlsToggle", BinaryData::bar_chart_svg, BinaryData::bar_chart_svgSize);
    controlsToggle_->setTooltip("Toggle velocity lane");
    controlsToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    controlsToggle_->setActive(velocityLaneVisible_);
    controlsToggle_->onClick = [this]() {
        velocityLaneVisible_ = !velocityLaneVisible_;
        refreshLaneDrawer();
        updateLaneToggleStates();
    };
    addAndMakeVisible(controlsToggle_.get());

    // Fold toggle (collapse to pads that have notes) — mirrors the piano roll.
    foldToggle_ = std::make_unique<magda::SvgButton>("FoldToggle", BinaryData::iconfoldboldm_svg,
                                                     BinaryData::iconfoldboldm_svgSize);
    foldToggle_->setTooltip("Fold to used pads");
    foldToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    foldToggle_->setActive(foldEnabled_);
    foldToggle_->onClick = [this]() {
        foldEnabled_ = !foldEnabled_;
        foldToggle_->setActive(foldEnabled_);
        applyFold();
    };
    addAndMakeVisible(foldToggle_.get());

    // CC lanes button (opens the drawer + the add-lane menu) — same affordance
    // as the piano roll so drum clips can add CC / pitchbend lanes.
    ccLanesBtn_ = std::make_unique<magda::SvgButton>("CCLanes", BinaryData::iconccboldm_svg,
                                                     BinaryData::iconccboldm_svgSize);
    ccLanesBtn_->setTooltip("Add CC / pitchbend lane");
    ccLanesBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    ccLanesBtn_->onClick = [this]() {
        // Adding a CC lane opens the drawer on its own (without the velocity
        // lane) via onLanesChanged.
        if (midiDrawer_)
            midiDrawer_->showAddLaneMenu();
    };
    addAndMakeVisible(ccLanesBtn_.get());

    verticalZoomStrip_ = std::make_unique<VerticalZoomStrip>(MIN_ROW_HEIGHT, MAX_ROW_HEIGHT);
    verticalZoomStrip_->setGestureContext(magda::GestureContext::DrumGrid);
    verticalZoomStrip_->getValue = [this]() { return rowHeight_; };
    verticalZoomStrip_->onZoomChanged = [this](int newHeight, int anchorScreenY) {
        const int anchorContentY = anchorScreenY + viewport_->getViewPositionY();
        const int anchorRow = juce::jlimit(0, juce::jmax(0, static_cast<int>(padRows_.size()) - 1),
                                           anchorContentY / juce::jmax(1, rowHeight_));
        setRowHeightAnchored(newHeight, anchorRow, anchorScreenY, true);
    };
    addAndMakeVisible(verticalZoomStrip_.get());

    // Create row labels
    rowLabels_ = std::make_unique<DrumGridRowLabels>();
    rowLabels_->setRowHeight(rowHeight_);
    rowLabels_->onNotePreview = [this](int noteNumber, bool isNoteOn) {
        if (editingClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && clip->trackId != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().previewNote(clip->trackId, noteNumber,
                                                           isNoteOn ? 100 : 0, isNoteOn);
        }
    };
    rowLabels_->onVerticalZoomRequested = [this](int labelsY,
                                                 const juce::MouseWheelDetails& wheel) {
        const int anchorContentY = labelsY + viewport_->getViewPositionY();
        const int anchorRow = juce::jlimit(0, juce::jmax(0, static_cast<int>(padRows_.size()) - 1),
                                           anchorContentY / juce::jmax(1, rowHeight_));
        const int heightDelta = wheel.deltaY > 0 ? 2 : -2;
        setRowHeightAnchored(rowHeight_ + heightDelta, anchorRow, labelsY, true);
    };
    rowLabels_->getRowLabel = [this](int noteNumber) -> juce::String {
        auto inst = primaryInstanceForClip(editingClipId_);
        if (!inst.valid())
            return {};
        const auto* row = findKitRow(inst.device->kitRows, noteNumber);
        return row != nullptr ? row->label : juce::String();
    };
    rowLabels_->onRowLabelCommitted = [this](int noteNumber, juce::String newLabel) {
        auto inst = primaryInstanceForClip(editingClipId_);
        if (!inst.valid())
            return;
        magda::TrackManager::getInstance().setDeviceKitRowLabel(inst.trackId, inst.deviceId,
                                                                noteNumber, newLabel);
    };
    rowLabels_->onRowContextMenu = [this](int noteNumber, juce::Point<int> screenPos) {
        showRowContextMenu(noteNumber, screenPos);
    };
    addAndMakeVisible(rowLabels_.get());

    labelDivider_ = std::make_unique<DrumGridLabelDivider>();
    labelDivider_->onDragDelta = [this](int dx) { setLabelWidth(labelWidth_ + dx); };
    addAndMakeVisible(labelDivider_.get());

    // Add DrumGrid-specific components to viewport repaint list
    viewport_->componentsToRepaint.push_back(rowLabels_.get());

    // Create grid component
    gridComponent_ = std::make_unique<DrumGridClipGrid>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setRowHeight(rowHeight_);
    gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
    gridComponent_->setSnapEnabled(snapEnabled_);
    // Apply any overlay tracks chosen in another editor session
    applyOverlayTracks();
    gridComponent_->onVerticalZoomRequested = [this](int gridY,
                                                     const juce::MouseWheelDetails& wheel) {
        const int anchorScreenY = gridY - viewport_->getViewPositionY();
        const int anchorRow = juce::jlimit(0, juce::jmax(0, static_cast<int>(padRows_.size()) - 1),
                                           gridY / juce::jmax(1, rowHeight_));
        const int heightDelta = wheel.deltaY > 0 ? 2 : -2;
        setRowHeightAnchored(rowHeight_ + heightDelta, anchorRow, anchorScreenY, true);
    };
    if (auto* controller = magda::TimelineController::getCurrent()) {
        gridComponent_->setTimeSignatureNumerator(
            controller->getState().tempo.timeSignatureNumerator);
    }

    // Set up callbacks
    gridComponent_->onNoteAdded = [](magda::ClipId clipId, double beat, int noteNumber,
                                     double length, int velocity) {
        auto cmd =
            std::make_unique<magda::AddMidiNoteCommand>(clipId, beat, noteNumber, length, velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    gridComponent_->onNoteDeleted = [](magda::ClipId clipId, size_t noteIndex) {
        auto cmd = std::make_unique<magda::DeleteMidiNoteCommand>(clipId, noteIndex);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    gridComponent_->onNoteMoved = [](magda::ClipId clipId, size_t noteIndex, double newBeat,
                                     int newNoteNumber) {
        auto cmd =
            std::make_unique<magda::MoveMidiNoteCommand>(clipId, noteIndex, newBeat, newNoteNumber);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    gridComponent_->onNoteResized = [](magda::ClipId clipId, size_t noteIndex, double newLength) {
        auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(clipId, noteIndex, newLength);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    gridComponent_->onNoteCopied = [](magda::ClipId clipId, size_t noteIndex, double destBeat,
                                      int destNoteNumber) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (!clip || noteIndex >= clip->midiNotes.size())
            return;
        const auto& srcNote = clip->midiNotes[noteIndex];
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(
            clipId, destBeat, destNoteNumber, srcNote.lengthBeats, srcNote.velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    gridComponent_->onNoteSelected = [](magda::ClipId clipId, size_t noteIndex, bool isAdditive) {
        if (isAdditive) {
            magda::SelectionManager::getInstance().addNoteToSelection(clipId, noteIndex);
        } else {
            magda::SelectionManager::getInstance().selectNote(clipId, noteIndex);
        }
    };

    gridComponent_->onNoteSelectionChanged = [this](magda::ClipId clipId,
                                                    std::vector<size_t> noteIndices) {
        if (noteIndices.empty()) {
            magda::SelectionManager::getInstance().clearNoteSelection();
        } else {
            magda::SelectionManager::getInstance().selectNotes(clipId, noteIndices);
        }
        setVelocityLaneSelectedNotes(noteIndices);
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
        if (!clip || !clip->isMidi())
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
    gridComponent_->onDuplicateNotes = [this](magda::ClipId clipId,
                                              std::vector<size_t> noteIndices) {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
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
                gridComponent_->syncSelectionFromManager();
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

    viewport_->setViewedComponent(gridComponent_.get(), false);

    // Setup MIDI drawer (stacked lanes: velocity + CC + pitchbend)
    setupMidiDrawer();

    // If base found a selected clip, set it up
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        setClip(editingClipId_);
    }
}

DrumGridClipContent::~DrumGridClipContent() {
    uninstallMidiNoteMonitor();
}

int DrumGridClipContent::getMaxVerticalScroll() const {
    if (!viewport_ || !gridComponent_)
        return 0;

    const int viewHeight =
        viewport_->getViewHeight() > 0 ? viewport_->getViewHeight() : viewport_->getHeight();
    return juce::jmax(0, gridComponent_->getHeight() - juce::jmax(0, viewHeight));
}

int DrumGridClipContent::clampVerticalScrollY(int scrollY) const {
    return juce::jlimit(0, getMaxVerticalScroll(), scrollY);
}

void DrumGridClipContent::clampViewportVerticalScroll() {
    if (!viewport_)
        return;

    const int currentY = viewport_->getViewPositionY();
    const int clampedY = clampVerticalScrollY(currentY);
    if (clampedY != currentY)
        viewport_->setViewPosition(viewport_->getViewPositionX(), clampedY);
}

void DrumGridClipContent::setRowHeight(int height, bool persist) {
    const int clampedHeight = juce::jlimit(MIN_ROW_HEIGHT, MAX_ROW_HEIGHT, height);
    if (clampedHeight == rowHeight_)
        return;

    rowHeight_ = clampedHeight;
    if (gridComponent_)
        gridComponent_->setRowHeight(rowHeight_);
    if (rowLabels_)
        rowLabels_->setRowHeight(rowHeight_);

    updateGridSize();
    clampViewportVerticalScroll();

    if (persist && editingClipId_ != magda::INVALID_CLIP_ID) {
        magda::ClipManager::getInstance().setClipMidiEditorRowHeight(editingClipId_, rowHeight_);
    }
}

void DrumGridClipContent::setRowHeightAnchored(int height, int anchorRow, int anchorScreenY,
                                               bool persist) {
    const int previousHeight = rowHeight_;
    setRowHeight(height, persist);
    if (rowHeight_ == previousHeight || !viewport_)
        return;

    const int newAnchorY = anchorRow * rowHeight_;
    const int newScrollY = clampVerticalScrollY(newAnchorY - anchorScreenY);
    viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
}

void DrumGridClipContent::loadRowHeightFromClip(magda::ClipId clipId) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip && clip->isMidi()) {
        setRowHeight(clip->midiEditorRowHeight > 0 ? clip->midiEditorRowHeight : DEFAULT_ROW_HEIGHT,
                     false);
    }
}

// ============================================================================
// MidiEditorContent virtual implementations
// ============================================================================

void DrumGridClipContent::setGridPixelsPerBeat(double ppb) {
    if (gridComponent_)
        gridComponent_->setPixelsPerBeat(ppb);
}

void DrumGridClipContent::setGridPlayheadPosition(double position) {
    if (gridComponent_)
        gridComponent_->setPlayheadPosition(position);
}

void DrumGridClipContent::setGridEditCursorPosition(double pos, bool visible) {
    if (gridComponent_)
        gridComponent_->setEditCursorPosition(pos, visible);
}

void DrumGridClipContent::onScrollPositionChanged(int scrollX, int scrollY) {
    rowLabels_->setScrollOffset(scrollY);
    if (midiDrawer_) {
        midiDrawer_->setScrollOffset(scrollX);
    }
}

void DrumGridClipContent::onGridResolutionChanged() {
    if (gridComponent_) {
        gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
        gridComponent_->setSnapEnabled(snapEnabled_);

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

void DrumGridClipContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Draw sidebar on the left
    auto sidebarArea = getLocalBounds().removeFromLeft(SIDEBAR_WIDTH);
    drawSidebar(g, sidebarArea);

    // MidiDrawerComponent has its own tab bar — no legacy velocity header needed
}

void DrumGridClipContent::resized() {
    auto bounds = getLocalBounds();

    // Skip sidebar (painted in paint())
    bounds.removeFromLeft(SIDEBAR_WIDTH);

    // Sidebar icons: fold toggle at the top, controls (velocity/CC) at the
    // bottom — mirrors the piano roll's sidebar layout.
    int iconSize = 22;
    int iconPadding = (SIDEBAR_WIDTH - iconSize) / 2;
    if (foldToggle_)
        foldToggle_->setBounds(iconPadding, RULER_HEIGHT + iconPadding, iconSize, iconSize);
    controlsToggle_->setBounds(iconPadding, getHeight() - iconSize - iconPadding, iconSize,
                               iconSize);
    if (ccLanesBtn_)
        ccLanesBtn_->setBounds(iconPadding, getHeight() - 2 * (iconSize + iconPadding), iconSize,
                               iconSize);

    // MIDI drawer at bottom (if open)
    if (velocityDrawerOpen_) {
        auto drawerArea = bounds.removeFromBottom(drawerHeight_);
        if (midiDrawer_) {
            midiDrawer_->setLeftMargin(ZOOM_STRIP_WIDTH + labelWidth_ + LABEL_DIVIDER_WIDTH);
            midiDrawer_->setBounds(drawerArea);
            midiDrawer_->setVisible(true);
        }
    } else {
        if (midiDrawer_)
            midiDrawer_->setVisible(false);
    }

    // Time ruler at top
    auto headerArea = bounds.removeFromTop(RULER_HEIGHT);
    headerArea.removeFromLeft(ZOOM_STRIP_WIDTH + labelWidth_ +
                              LABEL_DIVIDER_WIDTH);  // Align with grid
    timeRuler_->setBounds(headerArea);

    auto zoomStripArea = bounds.removeFromLeft(ZOOM_STRIP_WIDTH);
    verticalZoomStrip_->setBounds(zoomStripArea);

    // Row labels on left
    auto labelsArea = bounds.removeFromLeft(labelWidth_);
    rowLabels_->setBounds(labelsArea);

    // Draggable divider between row labels and the grid
    auto dividerArea = bounds.removeFromLeft(LABEL_DIVIDER_WIDTH);
    if (labelDivider_)
        labelDivider_->setBounds(dividerArea);

    // Viewport fills the rest
    viewport_->setBounds(bounds);

    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();
}

// ============================================================================
// Mouse
// ============================================================================

void DrumGridClipContent::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    // Modifier-driven zoom is resolved through GestureRouter (#1350) so the
    // bindings are configurable; each branch keeps its own zoom math, and the
    // positional plain-wheel scrolling below stays in this handler.
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::DrumGrid, wheel, e.mods, e.getPosition());

    // Horizontal (timebase) zoom about the cursor.
    if (gesture.type == magda::GestureActionType::ZoomHorizontal) {
        double zoomFactor = std::pow(2.0, static_cast<double>(gesture.magnitude));
        int mouseXInViewport =
            e.x - SIDEBAR_WIDTH - ZOOM_STRIP_WIDTH - labelWidth_ - LABEL_DIVIDER_WIDTH;
        performWheelZoom(zoomFactor, mouseXInViewport);
        return;
    }

    // Vertical (row height) zoom.
    if (gesture.type == magda::GestureActionType::ZoomVertical) {
        const int mouseYInContent = e.y - RULER_HEIGHT + viewport_->getViewPositionY();
        const int anchorRow = juce::jlimit(0, juce::jmax(0, static_cast<int>(padRows_.size()) - 1),
                                           mouseYInContent / juce::jmax(1, rowHeight_));
        const int heightDelta = gesture.magnitude > 0.0f ? 2 : -2;
        setRowHeightAnchored(rowHeight_ + heightDelta, anchorRow, e.y - RULER_HEIGHT, true);
        return;
    }

    const bool overTimeRuler = e.y < RULER_HEIGHT && e.x >= SIDEBAR_WIDTH + ZOOM_STRIP_WIDTH +
                                                                labelWidth_ + LABEL_DIVIDER_WIDTH;
    if (gesture.type == magda::GestureActionType::ScrollHorizontal && overTimeRuler) {
        if (timeRuler_->onScrollRequested) {
            int scrollAmount = static_cast<int>(-gesture.magnitude);
            if (scrollAmount != 0)
                timeRuler_->onScrollRequested(scrollAmount);
        }
        return;
    }

    if (gesture.type == magda::GestureActionType::ScrollHorizontal && viewport_) {
        viewport_->setViewPosition(viewport_->getViewPositionX() -
                                       static_cast<int>(gesture.magnitude),
                                   viewport_->getViewPositionY());
        return;
    }

    if (gesture.type == magda::GestureActionType::ScrollVertical && viewport_) {
        viewport_->setViewPosition(viewport_->getViewPositionX(),
                                   clampVerticalScrollY(viewport_->getViewPositionY() -
                                                        static_cast<int>(gesture.magnitude)));
    }
}

// ============================================================================
// Activation
// ============================================================================

void DrumGridClipContent::onActivated() {
    installMidiNoteMonitor();

    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->isMidi()) {
            setClip(selectedClip);
        }
    }
    startTimer(500);  // Poll pad names at 2Hz
    repaint();
}

void DrumGridClipContent::onDeactivated() {
    uninstallMidiNoteMonitor();
    if (rowLabels_)
        rowLabels_->clearPressedNotes();
    stopTimer();
}

void DrumGridClipContent::highlightMonitoredNote(int noteNumber, bool noteOn) {
    if (rowLabels_)
        rowLabels_->setNotePressed(noteNumber, noteOn);
}

void DrumGridClipContent::ensureMonitoredNoteVisible(int noteNumber) {
    if (!viewport_ || rowHeight_ <= 0)
        return;

    int rowIndex = -1;
    for (int i = 0; i < static_cast<int>(padRows_.size()); ++i) {
        if (padRows_[static_cast<size_t>(i)].noteNumber == noteNumber) {
            rowIndex = i;
            break;
        }
    }
    if (rowIndex < 0)
        return;

    const int rowTop = rowIndex * rowHeight_;
    const int rowBottom = rowTop + rowHeight_;
    const int viewTop = viewport_->getViewPositionY();
    const int viewHeight = viewport_->getHeight();
    const int viewBottom = viewTop + viewHeight;

    // Already fully visible — leave the view untouched.
    if (rowTop >= viewTop && rowBottom <= viewBottom)
        return;

    const int target = (rowTop < viewTop) ? rowTop : rowBottom - viewHeight;
    const int newScrollY = clampVerticalScrollY(juce::jmax(0, target));
    if (newScrollY == viewTop)
        return;

    viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void DrumGridClipContent::clipsChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            gridComponent_->setClipId(magda::INVALID_CLIP_ID);
            if (midiDrawer_)
                midiDrawer_->setClip(magda::INVALID_CLIP_ID);
        }
    }
    MidiEditorContent::clipsChanged();
    updateVelocityLane();
}

void DrumGridClipContent::clipSelectionChanged(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID) {
        editingClipId_ = magda::INVALID_CLIP_ID;
        drumGrid_ = nullptr;
        gridComponent_->setClipId(magda::INVALID_CLIP_ID);
        padRows_.clear();
        updateGridSize();
        updateTimeRuler();
        repaint();
        return;
    }

    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip && clip->isMidi()) {
        loadRowHeightFromClip(clipId);
        setClip(clipId);
    }
}

// ============================================================================
// Public methods
// ============================================================================

void DrumGridClipContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ == clipId && drumGrid_ != nullptr)
        return;

    editingClipId_ = clipId;
    loadRowHeightFromClip(editingClipId_);
    findDrumGrid();
    buildPadRows();

    gridComponent_->setClipId(clipId);
    gridComponent_->setPadRows(&padRows_);
    gridComponent_->refreshNotes();
    rowLabels_->setPadRows(&padRows_);

    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();
    scrollToClipStartForTimeMode();

    // Center on notes (or C-2 if empty)
    centerOnNotes();

    repaint();
}

void DrumGridClipContent::centerOnNotes() {
    if (!viewport_)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);

    int targetRow = -1;
    if (clip && !clip->midiNotes.empty()) {
        // Find note range and center on midpoint
        int minNote = 127;
        int maxNote = 0;
        for (const auto& note : clip->midiNotes) {
            minNote = juce::jmin(minNote, note.noteNumber);
            maxNote = juce::jmax(maxNote, note.noteNumber);
        }
        int midNote = (minNote + maxNote) / 2;

        // Find the row index for this note (rows are reversed: high notes at top)
        for (int i = 0; i < static_cast<int>(padRows_.size()); ++i) {
            if (padRows_[static_cast<size_t>(i)].noteNumber == midNote) {
                targetRow = i;
                break;
            }
        }
    }

    if (targetRow < 0) {
        // No notes or note not found — scroll to bottom (C-2)
        int totalHeight = static_cast<int>(padRows_.size()) * rowHeight_;
        int scrollY = juce::jmax(0, totalHeight - viewport_->getHeight());
        viewport_->setViewPosition(viewport_->getViewPositionX(), scrollY);
    } else {
        // Center on the target row
        int rowY = targetRow * rowHeight_;
        int scrollY = juce::jmax(0, rowY - (viewport_->getHeight() / 2) + (rowHeight_ / 2));
        viewport_->setViewPosition(viewport_->getViewPositionX(), scrollY);
    }
}

// ============================================================================
// Grid sizing (DrumGrid-specific)
// ============================================================================

void DrumGridClipContent::updateGridSize() {
    auto& clipManager = magda::ClipManager::getInstance();
    const auto* clip =
        editingClipId_ != magda::INVALID_CLIP_ID ? clipManager.getClip(editingClipId_) : nullptr;

    double tempo = 120.0;
    double timelineLength = 300.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timelineLength = state.timelineLength;
    }
    double secondsPerBeat = 60.0 / tempo;
    double displayLengthBeats = timelineLength / secondsPerBeat;

    double clipStartBeats = 0.0;
    double clipLengthBeats = 0.0;
    if (clip) {
        if (clip->loopEnabled || clip->view == magda::ClipView::Session) {
            clipStartBeats = 0.0;
        } else {
            clipStartBeats = clip->placement.startBeat;
        }
        clipLengthBeats = clip->placement.lengthBeats;
    }

    int numRows = juce::jmax(1, static_cast<int>(padRows_.size()));
    int gridWidth = juce::jmax(viewport_->getWidth(),
                               static_cast<int>(displayLengthBeats * horizontalZoom_) + 100);
    int gridHeight = numRows * rowHeight_;

    gridComponent_->setSize(gridWidth, gridHeight);
    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
    gridComponent_->setTimelineLengthBeats(displayLengthBeats);

    // Pass loop region data to grid
    if (clip) {
        double beatsPerSecond = tempo / 60.0;
        double loopOffsetBeats = clip->loopStart * beatsPerSecond;
        // MIDI clips use loopLengthBeats directly; audio clips derive from loopLength (seconds)
        double sourceLengthBeats =
            clip->loopLengthBeats > 0.0 ? clip->loopLengthBeats : clip->loopLength * beatsPerSecond;
        gridComponent_->setLoopRegion(loopOffsetBeats, sourceLengthBeats, clip->loopEnabled);
    } else {
        gridComponent_->setLoopRegion(0.0, 0.0, false);
    }

    clampViewportVerticalScroll();
}

void DrumGridClipContent::updateGridLoopRegion() {
    if (draggingLoopRegion_) {
        gridComponent_->setLoopRegion(previewLoopStartBeats_, previewLoopLengthBeats_, true);
    }
}

void DrumGridClipContent::setGridPhasePreview(double beats, bool active) {
    gridComponent_->setPhasePreview(beats, active);
}

void DrumGridClipContent::applyOverlayTracks() {
    if (gridComponent_)
        gridComponent_->setOverlayTracks(overlayTrackIds_);
}

// ============================================================================
// Drawing helpers
// ============================================================================

void DrumGridClipContent::drawSidebar(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(area.getRight() - 1, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

    // Hairline dividers framing the icon clusters, matching the piano roll:
    // the top tool group (fold) and the bottom lane-toggle group (CC /
    // velocity). Layout mirrors resized() so the lines sit in the gaps.
    const int iconSize = 22;
    const int padding = (SIDEBAR_WIDTH - iconSize) / 2;

    const int topClusterBottom = RULER_HEIGHT + padding + iconSize;
    const int bottomClusterTop = getHeight() - 2 * (iconSize + padding);
    const int topDividerY = topClusterBottom + padding / 2;
    const int bottomDividerY = bottomClusterTop - padding / 2;

    const float x1 = static_cast<float>(area.getX() + 5);
    const float x2 = static_cast<float>(area.getRight() - 5);
    if (foldToggle_ && bottomDividerY - topDividerY > padding) {
        g.drawHorizontalLine(topDividerY, x1, x2);
        g.drawHorizontalLine(bottomDividerY, x1, x2);
    }
}

void DrumGridClipContent::updateVelocityLane() {
    if (!midiDrawer_)
        return;

    // Common setup (clip, ppb, scroll, relative mode follows relativeTimeMode_).
    // The previous override to setRelativeMode(true) here was wrong: the
    // DrumGrid grid itself honours relativeTimeMode_ for its own coordinate
    // system, and forcing the drawer to a different mode parks the velocity
    // stems off-screen whenever the grid is in ABS view.
    MidiEditorContent::updateMidiDrawer();

    // Set clip length (DrumGrid-specific) so the drawer can render the loop
    // region overlay correctly.
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;
    if (clip) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double clipLengthBeats = clip->getLengthInBeats(tempo);
        midiDrawer_->setClipLengthBeats(clipLengthBeats);
    }

    midiDrawer_->refreshAll();
}

// ============================================================================
// DrumGrid-specific helpers
// ============================================================================

void DrumGridClipContent::findDrumGrid() {
    drumGrid_ = nullptr;
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip)
        return;

    drumGrid_ = findDrumGridForTrack(clip->trackId);
    if (drumGrid_) {
        baseNote_ = daw::audio::DrumGridPlugin::baseNote;
        numPads_ = daw::audio::DrumGridPlugin::maxPads;
    }
}

juce::String DrumGridClipContent::resolvePadName(int padIndex) const {
    int noteNumber = baseNote_ + padIndex;

    // Instance-level label wins over device-derived names.
    {
        auto inst = primaryInstanceForClip(editingClipId_);
        if (inst.valid()) {
            const auto* row = findKitRow(inst.device->kitRows, noteNumber);
            if (row != nullptr && row->label.isNotEmpty())
                return row->label;
        }
    }

    if (drumGrid_) {
        const auto* chain = drumGrid_->getChainForNote(noteNumber);
        if (chain) {
            // Check if chain has a custom name
            if (chain->name.isNotEmpty())
                return chain->name;

            // Check for MagdaSamplerPlugin with loaded sample
            for (const auto& plugin : chain->plugins) {
                if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                    auto sampleFile = sampler->getSampleFile();
                    if (sampleFile.existsAsFile())
                        return sampleFile.getFileNameWithoutExtension();
                }
            }

            // Has chain but no sample - show first plugin name
            if (!chain->plugins.empty())
                return chain->plugins[0]->getName();
        }
    }

    // Fallback: MIDI note name
    return juce::MidiMessage::getMidiNoteName(noteNumber, true, true, 3);
}

void DrumGridClipContent::onFoldMapChanged() {
    // Drum fold works off the pad-row set (not the pitch fold map): rebuild it
    // with the current fold filter, then re-point and repaint the grid + labels.
    buildPadRows();
    if (gridComponent_) {
        gridComponent_->setPadRows(&padRows_);
        gridComponent_->refreshNotes();
        gridComponent_->repaint();
    }
    if (rowLabels_) {
        rowLabels_->setPadRows(&padRows_);
        rowLabels_->repaint();
    }
}

void DrumGridClipContent::recenterOnNotes() {
    // After a fold toggle the row set changes height; bring the (now top-packed)
    // used rows into view.
    if (viewport_)
        viewport_->setViewPosition(viewport_->getViewPositionX(), 0);
}

void DrumGridClipContent::updateLaneToggleStates() {
    if (controlsToggle_)
        controlsToggle_->setActive(velocityLaneVisible_);
    if (ccLanesBtn_ && midiDrawer_)
        ccLanesBtn_->setActive(midiDrawer_->hasExtraLanes());
}

void DrumGridClipContent::buildPadRows() {
    padRows_.clear();

    auto inst = primaryInstanceForClip(editingClipId_);

    // Fold: when enabled, keep only pads that actually have notes in the clip
    // (hide empty drum lanes). Falls back to the full kit when the clip has no
    // notes so there's always something to edit.
    const std::vector<int> usedNotes = foldEnabled_ ? collectUsedPitches() : std::vector<int>{};
    const bool filterToUsed = foldEnabled_ && !usedNotes.empty();

    for (int i = 0; i < numPads_; ++i) {
        int noteNumber = baseNote_ + i;
        if (filterToUsed &&
            std::find(usedNotes.begin(), usedNotes.end(), noteNumber) == usedNotes.end())
            continue;
        bool hasChain = false;
        if (drumGrid_) {
            hasChain = (drumGrid_->getChainForNote(noteNumber) != nullptr);
        }

        PadRow row;
        row.noteNumber = noteNumber;
        row.name = resolvePadName(i);
        row.hasChain = hasChain;
        if (inst.valid()) {
            if (const auto* kitRow = findKitRow(inst.device->kitRows, noteNumber))
                row.role = kitRow->role;
        }
        padRows_.push_back(row);
    }

    // Reverse so lower notes appear at the bottom (higher notes at the top)
    std::reverse(padRows_.begin(), padRows_.end());
}

void DrumGridClipContent::refreshPadRowNames() {
    auto inst = primaryInstanceForClip(editingClipId_);
    bool changed = false;
    for (auto& row : padRows_) {
        int padIndex = row.noteNumber - baseNote_;
        if (padIndex < 0 || padIndex >= numPads_)
            continue;

        juce::String newName = resolvePadName(padIndex);
        bool newHasChain = false;
        if (drumGrid_)
            newHasChain = (drumGrid_->getChainForNote(row.noteNumber) != nullptr);
        juce::String newRole;
        if (inst.valid()) {
            if (const auto* kitRow = findKitRow(inst.device->kitRows, row.noteNumber))
                newRole = kitRow->role;
        }

        if (row.name != newName || row.hasChain != newHasChain || row.role != newRole) {
            row.name = newName;
            row.hasChain = newHasChain;
            row.role = newRole;
            changed = true;
        }
    }

    if (changed && rowLabels_)
        rowLabels_->repaint();
}

void DrumGridClipContent::timerCallback() {
    refreshPadRowNames();
}

void DrumGridClipContent::setLabelWidth(int newWidth) {
    int clamped = juce::jlimit(MIN_LABEL_WIDTH, MAX_LABEL_WIDTH, newWidth);
    if (clamped == labelWidth_)
        return;
    labelWidth_ = clamped;
    resized();
    repaint();
}

void DrumGridClipContent::applyTemplateToClip(
    const daw::audio::drum_grid_templates::Template& templ) {
    auto inst = primaryInstanceForClip(editingClipId_);
    if (!inst.valid() || templ.numRows <= 0 || templ.rows == nullptr)
        return;
    // Stamp template entries onto the lowest N rows in MIDI-note ascending
    // order (kick → lowest note → bottom row in the editor).
    std::vector<magda::KitRow> rows;
    rows.reserve(static_cast<size_t>(templ.numRows));
    int applied = 0;
    for (int padIndex = 0; padIndex < numPads_ && applied < templ.numRows; ++padIndex) {
        int noteNumber = baseNote_ + padIndex;
        const auto& trow = templ.rows[applied];
        magda::KitRow r;
        r.noteNumber = noteNumber;
        r.label = juce::String(trow.label);
        r.role = juce::String(trow.role);
        rows.push_back(std::move(r));
        ++applied;
    }
    magda::TrackManager::getInstance().setDeviceKitRows(inst.trackId, inst.deviceId, rows);
    refreshPadRowNames();
}

void DrumGridClipContent::applyDrumkitToClip(const juce::String& drumkitName) {
    auto inst = primaryInstanceForClip(editingClipId_);
    if (!inst.valid())
        return;
    auto kitRows = magda::DrumkitManager::getInstance().loadDrumkit(drumkitName);
    if (kitRows.empty())
        return;
    std::vector<magda::KitRow> rows;
    rows.reserve(kitRows.size());
    for (const auto& kr : kitRows) {
        magda::KitRow r;
        r.noteNumber = kr.noteNumber;
        r.label = kr.label;
        r.role = kr.role;
        rows.push_back(std::move(r));
    }
    magda::TrackManager::getInstance().setDeviceKitRows(inst.trackId, inst.deviceId, rows);
    refreshPadRowNames();
}

void DrumGridClipContent::promptSaveDrumkit() {
    auto inst = primaryInstanceForClip(editingClipId_);
    if (!inst.valid() || inst.device->kitRows.empty())
        return;  // nothing to save

    auto window = std::make_shared<juce::AlertWindow>(
        "Save Drumkit", "Name for this drumkit:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", "My Drumkit");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DrumGridClipContent> safeThis(this);
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([safeThis, window](int result) mutable {
            if (result != 1 || safeThis == nullptr) {
                window.reset();
                return;
            }
            auto name = window->getTextEditorContents("name").trim();
            window.reset();
            if (name.isEmpty())
                return;
            auto inst = primaryInstanceForClip(safeThis->editingClipId_);
            if (!inst.valid())
                return;
            std::vector<magda::DrumkitManager::Row> rows;
            rows.reserve(inst.device->kitRows.size());
            for (const auto& kitRow : inst.device->kitRows) {
                magda::DrumkitManager::Row r;
                r.noteNumber = kitRow.noteNumber;
                r.label = kitRow.label;
                r.role = kitRow.role;
                rows.push_back(std::move(r));
            }
            magda::DrumkitManager::getInstance().saveDrumkit(name, rows);
        }),
        true);
}

void DrumGridClipContent::showRowContextMenu(int noteNumber, juce::Point<int> screenPos) {
    auto inst = primaryInstanceForClip(editingClipId_);
    if (!inst.valid())
        return;

    juce::PopupMenu menu;

    // "Set instrument" submenu (full role vocabulary + None at the top)
    juce::PopupMenu instrumentMenu;
    juce::String currentRole;
    if (const auto* row = findKitRow(inst.device->kitRows, noteNumber))
        currentRole = row->role;

    instrumentMenu.addItem(1, "None", true, currentRole.isEmpty());
    instrumentMenu.addSeparator();
    int instrumentItemId = 100;
    std::vector<juce::String> roleIds;
    roleIds.reserve(daw::audio::drum_grid_roles::kRoles.size());
    for (const auto& r : daw::audio::drum_grid_roles::kRoles) {
        instrumentMenu.addItem(instrumentItemId++, juce::String(r.displayLabel), true,
                               currentRole == r.id);
        roleIds.emplace_back(r.id);
    }
    menu.addSubMenu("Set instrument", instrumentMenu);

    menu.addItem(2, "Rename label...");
    menu.addItem(3, "Clear label", true, false);
    menu.addItem(4, "Clear role", true, !currentRole.isEmpty());

    juce::PopupMenu templateMenu;
    int templateItemId = 500;
    for (const auto& t : daw::audio::drum_grid_templates::kBuiltIn)
        templateMenu.addItem(templateItemId++, juce::String(t.name));

    auto savedDrumkits = magda::DrumkitManager::getInstance().listDrumkits();
    if (!savedDrumkits.empty()) {
        templateMenu.addSeparator();
        int drumkitItemId = 700;
        for (const auto& kit : savedDrumkits)
            templateMenu.addItem(drumkitItemId++, kit.name);
    }
    menu.addSubMenu("Apply template", templateMenu);

    menu.addSeparator();
    menu.addItem(5, "Save as drumkit...");

    std::vector<juce::String> savedDrumkitNames;
    savedDrumkitNames.reserve(savedDrumkits.size());
    for (const auto& kit : savedDrumkits)
        savedDrumkitNames.push_back(kit.name);

    auto rect = juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(rect),
        [this, noteNumber, roleIds, savedDrumkitNames](int result) {
            constexpr int numBuiltIn =
                static_cast<int>(daw::audio::drum_grid_templates::kBuiltIn.size());
            if (result == 0)
                return;
            auto inst = primaryInstanceForClip(editingClipId_);
            if (!inst.valid())
                return;
            auto& tm = magda::TrackManager::getInstance();
            if (result == 1) {
                tm.setDeviceKitRowRole(inst.trackId, inst.deviceId, noteNumber, juce::String());
            } else if (result == 2) {
                if (rowLabels_)
                    rowLabels_->startRenameRow(noteNumber);
            } else if (result == 3) {
                tm.setDeviceKitRowLabel(inst.trackId, inst.deviceId, noteNumber, juce::String());
            } else if (result == 4) {
                tm.setDeviceKitRowRole(inst.trackId, inst.deviceId, noteNumber, juce::String());
            } else if (result == 5) {
                promptSaveDrumkit();
            } else if (result >= 100 && result < 100 + static_cast<int>(roleIds.size())) {
                tm.setDeviceKitRowRole(inst.trackId, inst.deviceId, noteNumber,
                                       roleIds[static_cast<size_t>(result - 100)]);
            } else if (result >= 500 && result < 500 + numBuiltIn) {
                applyTemplateToClip(
                    daw::audio::drum_grid_templates::kBuiltIn[static_cast<size_t>(result - 500)]);
            } else if (result >= 700 && result < 700 + static_cast<int>(savedDrumkitNames.size())) {
                applyDrumkitToClip(savedDrumkitNames[static_cast<size_t>(result - 700)]);
            }
        });
}

}  // namespace magda::daw::ui
