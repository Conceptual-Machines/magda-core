#include "DrumGridClipContent.hpp"

#include <algorithm>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "AudioBridge.hpp"
#include "AudioEngine.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
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
class DrumGridClipGrid : public juce::Component {
  public:
    DrumGridClipGrid() {
        setName("DrumGridClipGrid");
    }

    void setPixelsPerBeat(double ppb) {
        pixelsPerBeat_ = ppb;
        repaint();
    }
    void setRowHeight(int h) {
        rowHeight_ = h;
        repaint();
    }
    void setClipId(magda::ClipId id) {
        clipId_ = id;
        repaint();
    }
    void setPadRows(const std::vector<DrumGridClipContent::PadRow>* rows) {
        padRows_ = rows;
        repaint();
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

    std::function<void(magda::ClipId, double, int, int)> onNoteAdded;
    std::function<void(magda::ClipId, size_t)> onNoteDeleted;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteMoved;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        if (!padRows_ || padRows_->empty())
            return;

        int numRows = static_cast<int>(padRows_->size());

        // Get time signature
        int timeSigNumerator = 4;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            timeSigNumerator = controller->getState().tempo.timeSignatureNumerator;
        }

        // Draw horizontal row lines
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        for (int i = 0; i <= numRows; ++i) {
            int y = i * rowHeight_;
            g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
        }

        // Draw vertical beat lines
        double beatsVisible =
            static_cast<double>(bounds.getWidth() - GRID_LEFT_PADDING) / pixelsPerBeat_;
        for (int beat = 0; beat <= static_cast<int>(beatsVisible) + 1; ++beat) {
            int x = static_cast<int>(beat * pixelsPerBeat_) + GRID_LEFT_PADDING;
            if (x > bounds.getWidth())
                break;

            bool isBar = (beat % timeSigNumerator) == 0;
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(isBar ? 0.5f : 0.15f));
            g.drawVerticalLine(x, 0.0f, static_cast<float>(numRows * rowHeight_));
        }

        // Draw clip boundaries
        if (clipLengthBeats_ > 0.0) {
            int clipStartX = static_cast<int>(clipStartBeats_ * pixelsPerBeat_) + GRID_LEFT_PADDING;
            int clipEndX = static_cast<int>((clipStartBeats_ + clipLengthBeats_) * pixelsPerBeat_) +
                           GRID_LEFT_PADDING;

            // Dim areas outside clip
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            if (clipStartX > 0)
                g.fillRect(0, 0, clipStartX, numRows * rowHeight_);
            if (clipEndX < bounds.getWidth())
                g.fillRect(clipEndX, 0, bounds.getWidth() - clipEndX, numRows * rowHeight_);
        }

        // Draw MIDI note rectangles
        if (clipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
            if (clip) {
                for (const auto& note : clip->midiNotes) {
                    // Find which row this note belongs to
                    int rowIndex = -1;
                    for (int r = 0; r < numRows; ++r) {
                        if ((*padRows_)[r].noteNumber == note.noteNumber) {
                            rowIndex = r;
                            break;
                        }
                    }
                    if (rowIndex < 0)
                        continue;

                    int x = static_cast<int>(note.startBeat * pixelsPerBeat_) + GRID_LEFT_PADDING;
                    int y = rowIndex * rowHeight_;
                    int w = juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
                    int h = rowHeight_ - 2;

                    // Color based on velocity
                    float alpha = 0.5f + 0.5f * (note.velocity / 127.0f);
                    auto noteColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(alpha);

                    g.setColour(noteColour);
                    g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                           static_cast<float>(w), static_cast<float>(h), 2.0f);

                    // Border
                    g.setColour(noteColour.brighter(0.3f));
                    g.drawRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1),
                                           static_cast<float>(w), static_cast<float>(h), 2.0f,
                                           1.0f);
                }
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
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID)
            return;

        dragState_ = {};

        auto [noteIndex, row, beat] = hitTestNote(e);

        if (noteIndex >= 0) {
            // Clicked on an existing note — prepare for potential drag
            dragState_.active = true;
            dragState_.noteIndex = static_cast<size_t>(noteIndex);
            dragState_.originalBeat = beat;
            dragState_.originalRow = row;
            dragState_.currentBeat = beat;
            dragState_.currentRow = row;
            dragState_.hasMoved = false;
        } else if (row >= 0) {
            // Clicked on empty cell — add a new note
            if (onNoteAdded)
                onNoteAdded(clipId_, beat, (*padRows_)[row].noteNumber, 100);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragState_.active || !padRows_ || padRows_->empty())
            return;

        // Validate note index is still in range (could be stale after a delete)
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip || dragState_.noteIndex >= clip->midiNotes.size()) {
            dragState_ = {};
            return;
        }

        int row = juce::jlimit(0, static_cast<int>(padRows_->size()) - 1, e.y / rowHeight_);
        double beat = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
        if (beat < 0.0)
            beat = 0.0;
        double subdivision = 0.25;
        beat = std::floor(beat / subdivision) * subdivision;

        if (row != dragState_.currentRow || std::abs(beat - dragState_.currentBeat) > 0.001) {
            dragState_.currentBeat = beat;
            dragState_.currentRow = row;
            dragState_.hasMoved = true;

            int newNoteNumber = (*padRows_)[row].noteNumber;
            if (onNoteMoved)
                onNoteMoved(clipId_, dragState_.noteIndex, beat, newNoteNumber);
        }
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override {
        dragState_ = {};
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID)
            return;

        // Cancel any active drag — the note is about to be deleted
        dragState_ = {};

        auto [noteIndex, row, beat] = hitTestNote(e);
        (void)row;
        (void)beat;

        if (noteIndex >= 0) {
            if (onNoteDeleted)
                onNoteDeleted(clipId_, static_cast<size_t>(noteIndex));
        }
    }

  private:
    struct HitResult {
        int noteIndex;  // -1 if no note hit
        int row;
        double beat;
    };

    HitResult hitTestNote(const juce::MouseEvent& e) {
        int row = e.y / rowHeight_;
        if (row < 0 || row >= static_cast<int>(padRows_->size()))
            return {-1, -1, 0.0};

        int noteNumber = (*padRows_)[row].noteNumber;
        double beat = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
        if (beat < 0.0)
            beat = 0.0;
        double subdivision = 0.25;
        beat = std::floor(beat / subdivision) * subdivision;

        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return {-1, row, beat};

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];
            if (note.noteNumber == noteNumber && note.startBeat <= beat &&
                note.startBeat + note.lengthBeats > beat) {
                return {static_cast<int>(i), row, beat};
            }
        }
        return {-1, row, beat};
    }

    struct DragState {
        bool active = false;
        size_t noteIndex = 0;
        double originalBeat = 0.0;
        int originalRow = 0;
        double currentBeat = 0.0;
        int currentRow = 0;
        bool hasMoved = false;
    };
    DragState dragState_;
    static constexpr int GRID_LEFT_PADDING = 2;
    double pixelsPerBeat_ = 50.0;
    int rowHeight_ = 24;
    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;
    const std::vector<DrumGridClipContent::PadRow>* padRows_ = nullptr;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;
    double timelineLengthBeats_ = 0.0;
    double playheadPosition_ = -1.0;
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

void DrumGridClipContent::onScrollPositionChanged(int /*scrollX*/, int scrollY) {
    rowLabels_->setScrollOffset(scrollY);
}

// ============================================================================
// Paint / Layout
// ============================================================================

void DrumGridClipContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void DrumGridClipContent::resized() {
    auto bounds = getLocalBounds();

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
}

// ============================================================================
// Mouse
// ============================================================================

void DrumGridClipContent::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    // Cmd/Ctrl + scroll = horizontal zoom (uses shared base method)
    if (e.mods.isCommandDown()) {
        double zoomFactor = 1.0 + (wheel.deltaY * 0.1);
        int mouseXInViewport = e.x - LABEL_WIDTH;
        performWheelZoom(zoomFactor, mouseXInViewport);
        return;
    }

    // Forward to time ruler area for horizontal scroll
    if (e.y < RULER_HEIGHT && e.x >= LABEL_WIDTH) {
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
        }
    }
    MidiEditorContent::clipsChanged();
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
    rowLabels_->setPadRows(&padRows_);

    updateGridSize();
    updateTimeRuler();
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
