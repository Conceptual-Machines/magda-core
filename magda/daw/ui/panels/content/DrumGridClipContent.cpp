#include "DrumGridClipContent.hpp"

#include <algorithm>
#include <set>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "AudioBridge.hpp"
#include "AudioEngine.hpp"
#include "BinaryData.h"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/pianoroll/NoteComponent.hpp"
#include "ui/components/pianoroll/NoteGridHost.hpp"
#include "ui/components/pianoroll/VelocityLaneComponent.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/state/TimelineController.hpp"

namespace magda::daw::ui {

//==============================================================================
// Helper: find DrumGridPlugin for a track
//==============================================================================
namespace {
namespace te = tracktion::engine;

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
    }
    void setClipLengthBeats(double b) {
        clipLengthBeats_ = b;
    }
    void setTimelineLengthBeats(double b) {
        timelineLengthBeats_ = b;
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
    void setTimeSignatureNumerator(int n) {
        if (timeSigNumerator_ != n) {
            timeSigNumerator_ = n;
            repaint();
        }
    }

    // Callbacks for parent
    std::function<void(magda::ClipId, double, int, int)> onNoteAdded;
    std::function<void(magda::ClipId, size_t)> onNoteDeleted;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteMoved;
    std::function<void(magda::ClipId, size_t, double)> onNoteResized;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteCopied;
    std::function<void(magda::ClipId, size_t, bool)> onNoteSelected;

    // Refresh note components from clip data
    void refreshNotes() {
        // Preserve selection: pending positions take priority, else keep current indices
        std::set<size_t> selectedIndices;
        if (pendingSelectPositions_.empty()) {
            for (const auto& nc : noteComponents_) {
                if (nc->isSelected())
                    selectedIndices.insert(nc->getNoteIndex());
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

    // ── NoteGridHost overrides ──────────────────────────────────────────────

    double getPixelsPerBeat() const override {
        return pixelsPerBeat_;
    }

    int getNoteHeight() const override {
        return rowHeight_;
    }

    juce::Point<int> getGridScreenPosition() const override {
        return localPointToGlobal(juce::Point<int>());
    }

    void updateNotePosition(magda::NoteComponent* note, double beat, int noteNumber,
                            double length) override {
        if (!note || !padRows_)
            return;

        int rowIndex = findRowForNote(noteNumber);
        if (rowIndex < 0)
            return;

        int x = static_cast<int>(beat * pixelsPerBeat_) + GRID_LEFT_PADDING;
        int y = rowIndex * rowHeight_;
        int w = juce::jmax(4, static_cast<int>(length * pixelsPerBeat_));
        int h = rowHeight_ - 2;

        note->setBounds(x, y + 1, w, h);
    }

    void setCopyDragPreview(double beat, int noteNumber, double length, juce::Colour colour,
                            bool active) override {
        copyDragGhost_.beat = beat;
        copyDragGhost_.noteNumber = noteNumber;
        copyDragGhost_.length = length;
        copyDragGhost_.colour = colour;
        copyDragGhost_.active = active;
        repaint();
    }

    // ── ClipManagerListener ─────────────────────────────────────────────────

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
        }
    }
    void clipSelectionChanged(magda::ClipId) override {}

    // ── Component overrides ─────────────────────────────────────────────────

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

        // Draw clip boundaries
        if (clipLengthBeats_ > 0.0) {
            int clipStartX = static_cast<int>(clipStartBeats_ * pixelsPerBeat_) + GRID_LEFT_PADDING;
            int clipEndX = static_cast<int>((clipStartBeats_ + clipLengthBeats_) * pixelsPerBeat_) +
                           GRID_LEFT_PADDING;

            g.setColour(juce::Colours::black.withAlpha(0.3f));
            if (clipStartX > 0)
                g.fillRect(0, 0, clipStartX, numRows * rowHeight_);
            if (clipEndX < bounds.getWidth())
                g.fillRect(clipEndX, 0, bounds.getWidth() - clipEndX, numRows * rowHeight_);
        }

        // Draw copy drag ghost preview
        if (copyDragGhost_.active) {
            int rowIndex = findRowForNote(copyDragGhost_.noteNumber);
            if (rowIndex >= 0) {
                int gx = static_cast<int>(copyDragGhost_.beat * pixelsPerBeat_) + GRID_LEFT_PADDING;
                int gy = rowIndex * rowHeight_;
                int gw = juce::jmax(4, static_cast<int>(copyDragGhost_.length * pixelsPerBeat_));
                int gh = rowHeight_ - 2;

                g.setColour(copyDragGhost_.colour.withAlpha(0.35f));
                g.fillRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                                       static_cast<float>(gw), static_cast<float>(gh), 2.0f);
                g.setColour(copyDragGhost_.colour.withAlpha(0.6f));
                g.drawRoundedRectangle(static_cast<float>(gx), static_cast<float>(gy + 1),
                                       static_cast<float>(gw), static_cast<float>(gh), 2.0f, 1.0f);
            }
        }

        // Draw playhead
        if (playheadPosition_ >= 0.0) {
            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double playheadBeat = playheadPosition_ * (tempo / 60.0);
            int playheadX = static_cast<int>(playheadBeat * pixelsPerBeat_) + GRID_LEFT_PADDING;

            if (playheadX >= 0 && playheadX <= bounds.getWidth()) {
                g.setColour(juce::Colours::white);
                g.drawVerticalLine(playheadX, 0.0f, static_cast<float>(numRows * rowHeight_));
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

    void mouseDown(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID)
            return;

        isDragSelecting_ = false;
        emptyClickRow_ = -1;

        int row = e.y / rowHeight_;
        if (row >= 0 && row < static_cast<int>(padRows_->size())) {
            emptyClickRow_ = row;
            double rawBeat = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
            if (rawBeat < 0.0)
                rawBeat = 0.0;
            emptyClickBeat_ = rawBeat;
        }
        dragSelectStart_ = e.getPosition();
        dragSelectEnd_ = e.getPosition();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty())
            return;

        if (emptyClickRow_ >= 0) {
            isDragSelecting_ = true;
            dragSelectEnd_ = e.getPosition();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (isDragSelecting_) {
            // Rubber band selection
            auto selectionRect = juce::Rectangle<int>(dragSelectStart_, dragSelectEnd_);
            bool isAdditive = e.mods.isCommandDown();

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
            repaint();
            return;
        }

        if (emptyClickRow_ >= 0) {
            // Plain click on empty cell — add a note
            double addBeat = emptyClickBeat_;
            if (snapEnabled_ && gridResolutionBeats_ > 0.0)
                addBeat = std::floor(addBeat / gridResolutionBeats_) * gridResolutionBeats_;

            if (onNoteAdded)
                onNoteAdded(clipId_, addBeat, (*padRows_)[emptyClickRow_].noteNumber, 100);

            for (auto& nc : noteComponents_)
                nc->setSelected(false);
        } else {
            // Click on grid background — deselect all
            if (!e.mods.isCommandDown() && !e.mods.isShiftDown()) {
                for (auto& nc : noteComponents_)
                    nc->setSelected(false);
            }
        }

        emptyClickRow_ = -1;
    }

  private:
    // Copy drag ghost preview state
    struct CopyDragGhost {
        double beat = 0.0;
        int noteNumber = 60;
        double length = 1.0;
        juce::Colour colour;
        bool active = false;
    } copyDragGhost_;

    // Rubber band selection state
    bool isDragSelecting_ = false;
    juce::Point<int> dragSelectStart_;
    juce::Point<int> dragSelectEnd_;
    int emptyClickRow_ = -1;
    double emptyClickBeat_ = 0.0;

    static constexpr int GRID_LEFT_PADDING = 2;
    double pixelsPerBeat_ = 50.0;
    int rowHeight_ = 24;
    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;
    const std::vector<DrumGridClipContent::PadRow>* padRows_ = nullptr;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;
    double timelineLengthBeats_ = 0.0;
    double playheadPosition_ = -1.0;
    double gridResolutionBeats_ = 0.25;
    bool snapEnabled_ = true;
    int timeSigNumerator_ = 4;

    // Note components
    std::vector<std::unique_ptr<magda::NoteComponent>> noteComponents_;

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

    double snapBeatToGrid(double beat) const {
        if (!snapEnabled_ || gridResolutionBeats_ <= 0.0)
            return beat;
        return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
    }

    void createNoteComponents() {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip || clip->type != magda::ClipType::MIDI || !padRows_)
            return;

        auto noteColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

        for (size_t i = 0; i < clip->midiNotes.size(); i++) {
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
            };

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
                if (onNoteDeleted)
                    onNoteDeleted(clipId_, index);
            };

            noteComp->snapBeatToGrid = [this](double beat) { return snapBeatToGrid(beat); };

            noteComp->updateFromNote(clip->midiNotes[i], noteColour);
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

            const auto& note = clip->midiNotes[noteIndex];
            int rowIndex = findRowForNote(note.noteNumber);
            if (rowIndex < 0)
                continue;

            int x = static_cast<int>(note.startBeat * pixelsPerBeat_) + GRID_LEFT_PADDING;
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

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));

        if (!padRows_ || padRows_->empty())
            return;

        auto font = magda::FontManager::getInstance().getUIFont(11.0f);
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

            // Pad name
            const auto& padRow = (*padRows_)[i];
            g.setColour(padRow.hasChain ? DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)
                                        : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.drawText(padRow.name,
                       juce::Rectangle<int>(4, y + 1, bounds.getWidth() - 8, rowHeight_ - 2),
                       juce::Justification::centredLeft, true);
        }

        // Right border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawVerticalLine(bounds.getWidth() - 1, 0.0f, static_cast<float>(bounds.getHeight()));
    }

  private:
    const std::vector<DrumGridClipContent::PadRow>* padRows_ = nullptr;
    int rowHeight_ = 24;
    int scrollOffsetY_ = 0;
};

//==============================================================================
// DrumGridClipContent implementation
//==============================================================================
DrumGridClipContent::DrumGridClipContent() {
    setName("DrumGridClipContent");

    // Create controls toggle button (bar chart icon)
    controlsToggle_ = std::make_unique<magda::SvgButton>(
        "ControlsToggle", BinaryData::bar_chart_svg, BinaryData::bar_chart_svgSize);
    controlsToggle_->setTooltip("Toggle velocity lane");
    controlsToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    controlsToggle_->setActive(velocityDrawerOpen_);
    controlsToggle_->onClick = [this]() {
        velocityDrawerOpen_ = !velocityDrawerOpen_;
        controlsToggle_->setActive(velocityDrawerOpen_);
        updateVelocityLane();
        resized();
        repaint();
    };
    addAndMakeVisible(controlsToggle_.get());

    // Create velocity lane component
    velocityLane_ = std::make_unique<magda::VelocityLaneComponent>();
    velocityLane_->setLeftPadding(GRID_LEFT_PADDING);
    velocityLane_->onVelocityChanged = [this](magda::ClipId clipId, size_t noteIndex,
                                              int newVelocity) {
        auto cmd =
            std::make_unique<magda::SetMidiNoteVelocityCommand>(clipId, noteIndex, newVelocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        velocityLane_->refreshNotes();
    };
    addChildComponent(velocityLane_.get());  // Start hidden

    // Create row labels
    rowLabels_ = std::make_unique<DrumGridRowLabels>();
    rowLabels_->setRowHeight(ROW_HEIGHT);
    addAndMakeVisible(rowLabels_.get());

    // Add DrumGrid-specific components to viewport repaint list
    viewport_->componentsToRepaint.push_back(rowLabels_.get());

    // Create grid component
    gridComponent_ = std::make_unique<DrumGridClipGrid>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setRowHeight(ROW_HEIGHT);
    gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
    gridComponent_->setSnapEnabled(snapEnabled_);
    if (auto* controller = magda::TimelineController::getCurrent()) {
        gridComponent_->setTimeSignatureNumerator(
            controller->getState().tempo.timeSignatureNumerator);
    }

    // Set up callbacks
    gridComponent_->onNoteAdded = [](magda::ClipId clipId, double beat, int noteNumber,
                                     int velocity) {
        double defaultLength = 0.25;  // 16th note for drums
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(clipId, beat, noteNumber,
                                                               defaultLength, velocity);
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

    viewport_->setViewedComponent(gridComponent_.get(), false);

    // If base found a selected clip, set it up
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        setClip(editingClipId_);
    }
}

DrumGridClipContent::~DrumGridClipContent() = default;

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

void DrumGridClipContent::onScrollPositionChanged(int scrollX, int scrollY) {
    rowLabels_->setScrollOffset(scrollY);
    if (velocityLane_) {
        velocityLane_->setScrollOffset(scrollX);
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

void DrumGridClipContent::resized() {
    auto bounds = getLocalBounds();

    // Skip sidebar (painted in paint())
    bounds.removeFromLeft(SIDEBAR_WIDTH);

    // Position sidebar icon at the bottom of the sidebar
    int iconSize = 22;
    int iconPadding = (SIDEBAR_WIDTH - iconSize) / 2;
    controlsToggle_->setBounds(iconPadding, getHeight() - iconSize - iconPadding, iconSize,
                               iconSize);

    // Velocity drawer at bottom (if open)
    if (velocityDrawerOpen_) {
        auto drawerArea = bounds.removeFromBottom(VELOCITY_LANE_HEIGHT + VELOCITY_HEADER_HEIGHT);
        drawerArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
        drawerArea.removeFromLeft(LABEL_WIDTH);
        velocityLane_->setBounds(drawerArea);
        velocityLane_->setVisible(true);
    } else {
        velocityLane_->setVisible(false);
    }

    // Time ruler at top
    auto headerArea = bounds.removeFromTop(RULER_HEIGHT);
    headerArea.removeFromLeft(LABEL_WIDTH);  // Align with grid
    timeRuler_->setBounds(headerArea);

    // Row labels on left
    auto labelsArea = bounds.removeFromLeft(LABEL_WIDTH);
    rowLabels_->setBounds(labelsArea);

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
    // Cmd/Ctrl + scroll = horizontal zoom (uses shared base method)
    if (e.mods.isCommandDown()) {
        double zoomFactor = 1.0 + (wheel.deltaY * 0.1);
        int mouseXInViewport = e.x - SIDEBAR_WIDTH - LABEL_WIDTH;
        performWheelZoom(zoomFactor, mouseXInViewport);
        return;
    }

    // Forward to time ruler area for horizontal scroll
    if (e.y < RULER_HEIGHT && e.x >= SIDEBAR_WIDTH + LABEL_WIDTH) {
        if (timeRuler_->onScrollRequested) {
            float delta = (wheel.deltaX != 0.0f) ? wheel.deltaX : wheel.deltaY;
            int scrollAmount = static_cast<int>(-delta * 100.0f);
            if (scrollAmount != 0)
                timeRuler_->onScrollRequested(scrollAmount);
        }
        return;
    }

    // Regular scroll — forward to viewport for vertical/horizontal scrolling
    if (viewport_) {
        int deltaX = static_cast<int>(-wheel.deltaX * 100.0f);
        int deltaY = static_cast<int>(-wheel.deltaY * 100.0f);
        viewport_->setViewPosition(viewport_->getViewPositionX() + deltaX,
                                   viewport_->getViewPositionY() + deltaY);
    }
}

// ============================================================================
// Activation
// ============================================================================

void DrumGridClipContent::onActivated() {
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::MIDI) {
            setClip(selectedClip);
        }
    }
    repaint();
}

void DrumGridClipContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void DrumGridClipContent::clipsChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            gridComponent_->setClipId(magda::INVALID_CLIP_ID);
            velocityLane_->setClip(magda::INVALID_CLIP_ID);
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
    if (clip && clip->type == magda::ClipType::MIDI) {
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
    findDrumGrid();
    buildPadRows();

    gridComponent_->setClipId(clipId);
    gridComponent_->setPadRows(&padRows_);
    gridComponent_->refreshNotes();
    rowLabels_->setPadRows(&padRows_);

    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();
    repaint();
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
        clipStartBeats = clip->startTime / secondsPerBeat;
        clipLengthBeats = clip->length / secondsPerBeat;
    }

    int numRows = juce::jmax(1, static_cast<int>(padRows_.size()));
    int gridWidth = juce::jmax(viewport_->getWidth(),
                               static_cast<int>(displayLengthBeats * horizontalZoom_) + 100);
    int gridHeight = numRows * ROW_HEIGHT;

    gridComponent_->setSize(gridWidth, gridHeight);
    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
    gridComponent_->setTimelineLengthBeats(displayLengthBeats);
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
}

void DrumGridClipContent::drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));

    auto labelArea = area.removeFromLeft(LABEL_WIDTH);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    g.drawText("Velocity", labelArea.reduced(4, 0), juce::Justification::centredLeft, true);
}

void DrumGridClipContent::updateVelocityLane() {
    if (!velocityLane_)
        return;

    velocityLane_->setClip(editingClipId_);
    velocityLane_->setPixelsPerBeat(horizontalZoom_);
    velocityLane_->setRelativeMode(true);
    velocityLane_->setClipStartBeats(0.0);

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
        double clipLengthBeats = clip->length / secondsPerBeat;
        velocityLane_->setClipStartBeats(clipStartBeats);
        velocityLane_->setClipLengthBeats(clipLengthBeats);
    }

    if (viewport_) {
        velocityLane_->setScrollOffset(viewport_->getViewPositionX());
    }

    velocityLane_->refreshNotes();
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
        // Count active pads
        numPads_ = 16;  // Default to 16 visible rows
        const auto& chains = drumGrid_->getChains();
        if (!chains.empty()) {
            // Find the highest pad index used
            int maxPadIdx = 0;
            for (const auto& chain : chains) {
                int padIdx = chain->lowNote - baseNote_;
                if (padIdx > maxPadIdx)
                    maxPadIdx = padIdx;
            }
            numPads_ = juce::jmax(16, maxPadIdx + 1);
        }
    }
}

juce::String DrumGridClipContent::resolvePadName(int padIndex) const {
    int noteNumber = baseNote_ + padIndex;

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

void DrumGridClipContent::buildPadRows() {
    padRows_.clear();

    // Also check clip notes for any notes outside the default range
    std::set<int> notesInClip;
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip) {
            for (const auto& note : clip->midiNotes)
                notesInClip.insert(note.noteNumber);
        }
    }

    for (int i = 0; i < numPads_; ++i) {
        int noteNumber = baseNote_ + i;
        bool hasChain = false;
        if (drumGrid_) {
            hasChain = (drumGrid_->getChainForNote(noteNumber) != nullptr);
        }

        // Show row if it has a chain or has notes in the clip
        bool hasNotes = notesInClip.count(noteNumber) > 0;
        if (hasChain || hasNotes || i < 16) {
            PadRow row;
            row.noteNumber = noteNumber;
            row.name = resolvePadName(i);
            row.hasChain = hasChain;
            padRows_.push_back(row);
        }
    }

    // Also add any notes outside the pad range that exist in the clip
    for (int noteNum : notesInClip) {
        if (noteNum < baseNote_ || noteNum >= baseNote_ + numPads_) {
            PadRow row;
            row.noteNumber = noteNum;
            row.name = juce::MidiMessage::getMidiNoteName(noteNum, true, true, 3);
            row.hasChain = false;
            padRows_.push_back(row);
        }
    }

    // Reverse so lower notes appear at the bottom (higher notes at the top)
    std::reverse(padRows_.begin(), padRows_.end());
}

}  // namespace magda::daw::ui
