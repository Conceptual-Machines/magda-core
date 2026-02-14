#include "DrumGridClipContent.hpp"

#include <algorithm>

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

    std::function<void(magda::ClipId, double, int, int)> onNoteAdded;
    std::function<void(magda::ClipId, size_t)> onNoteDeleted;
    std::function<void(magda::ClipId, size_t, double, int)> onNoteMoved;
    std::function<void(magda::ClipId, size_t, double)> onNoteResized;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        if (!padRows_ || padRows_->empty())
            return;

        int numRows = static_cast<int>(padRows_->size());

        // Draw horizontal row lines
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
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
            // Use integer counter to avoid floating-point drift (important for triplets etc.)
            if (gridResolutionBeats_ > 0.0) {
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.35f));
                int numLines =
                    static_cast<int>(std::ceil((beatsVisible + 1.0) / gridResolutionBeats_));
                for (int i = 0; i <= numLines; i++) {
                    double beat = i * gridResolutionBeats_;
                    if (beat > beatsVisible + 1.0)
                        break;
                    // Skip whole beats (drawn in pass 2/3)
                    if (std::abs(beat - std::round(beat)) < 0.001)
                        continue;
                    int x = static_cast<int>(beat * pixelsPerBeat_) + GRID_LEFT_PADDING;
                    if (x > maxX)
                        break;
                    g.drawVerticalLine(x, 0.0f, gridBottom);
                }
            }

            // Pass 2: Beat lines (always visible)
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
            for (int b = 1; b <= static_cast<int>(beatsVisible) + 1; b++) {
                if (b % tsNum == 0)
                    continue;
                int x =
                    static_cast<int>(static_cast<double>(b) * pixelsPerBeat_) + GRID_LEFT_PADDING;
                if (x > maxX)
                    break;
                g.drawVerticalLine(x, 0.0f, gridBottom);
            }

            // Pass 3: Bar lines (brightest, always visible, drawn last)
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.7f));
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

                    // Color based on velocity — darker for lower velocity
                    float velocityRatio = note.velocity / 127.0f;
                    auto baseColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
                    auto noteColour = baseColour.darker(1.0f - velocityRatio);

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

    void mouseMove(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID) {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }

        auto [noteIndex, row, beat, noteRightEdgeX] = hitTestNote(e);
        if (noteIndex >= 0 && noteRightEdgeX >= 0 && e.x >= noteRightEdgeX - RESIZE_HANDLE_WIDTH) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!padRows_ || padRows_->empty() || clipId_ == magda::INVALID_CLIP_ID)
            return;

        dragState_ = {};

        auto [noteIndex, row, beat, noteRightEdgeX] = hitTestNote(e);

        if (noteIndex >= 0) {
            const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
            dragState_.active = true;
            dragState_.noteIndex = static_cast<size_t>(noteIndex);
            dragState_.originalBeat = beat;
            dragState_.originalRow = row;
            dragState_.currentBeat = beat;
            dragState_.currentRow = row;
            dragState_.hasMoved = false;

            // Check if click is near the right edge → resize mode
            if (noteRightEdgeX >= 0 && e.x >= noteRightEdgeX - RESIZE_HANDLE_WIDTH) {
                dragState_.dragMode = DragMode::ResizeRight;
                if (clip && dragState_.noteIndex < clip->midiNotes.size())
                    dragState_.originalLength = clip->midiNotes[dragState_.noteIndex].lengthBeats;
            } else {
                dragState_.dragMode = DragMode::Move;
            }
        } else if (row >= 0) {
            // Clicked on empty cell — add a new note (snap beat position)
            double addBeat = beat;
            if (snapEnabled_ && gridResolutionBeats_ > 0.0) {
                addBeat = std::floor(beat / gridResolutionBeats_) * gridResolutionBeats_;
            }
            if (onNoteAdded)
                onNoteAdded(clipId_, addBeat, (*padRows_)[row].noteNumber, 100);
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

        if (dragState_.dragMode == DragMode::ResizeRight) {
            // Calculate new length from mouse X position
            const auto& note = clip->midiNotes[dragState_.noteIndex];
            double mouseBeats = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
            double newLength = mouseBeats - note.startBeat;

            // Snap to grid
            double subdivision =
                (snapEnabled_ && gridResolutionBeats_ > 0.0) ? gridResolutionBeats_ : 0.25;
            newLength = std::round(newLength / subdivision) * subdivision;

            // Enforce minimum length
            newLength = juce::jmax(subdivision, newLength);

            if (std::abs(newLength - note.lengthBeats) > 0.001) {
                dragState_.hasMoved = true;
                if (onNoteResized)
                    onNoteResized(clipId_, dragState_.noteIndex, newLength);
            }
        } else if (dragState_.dragMode == DragMode::Move) {
            int row = juce::jlimit(0, static_cast<int>(padRows_->size()) - 1, e.y / rowHeight_);
            double beat = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
            if (beat < 0.0)
                beat = 0.0;
            if (snapEnabled_ && gridResolutionBeats_ > 0.0) {
                beat = std::floor(beat / gridResolutionBeats_) * gridResolutionBeats_;
            }

            if (row != dragState_.currentRow || std::abs(beat - dragState_.currentBeat) > 0.001) {
                dragState_.currentBeat = beat;
                dragState_.currentRow = row;
                dragState_.hasMoved = true;

                int newNoteNumber = (*padRows_)[row].noteNumber;
                if (onNoteMoved)
                    onNoteMoved(clipId_, dragState_.noteIndex, beat, newNoteNumber);
            }
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

        auto [noteIndex, row, beat, noteRightEdgeX] = hitTestNote(e);
        (void)row;
        (void)beat;
        (void)noteRightEdgeX;

        if (noteIndex >= 0) {
            if (onNoteDeleted)
                onNoteDeleted(clipId_, static_cast<size_t>(noteIndex));
        }
    }

  private:
    enum class DragMode { None, Move, ResizeRight };

    static constexpr int RESIZE_HANDLE_WIDTH = 6;

    struct HitResult {
        int noteIndex;  // -1 if no note hit
        int row;
        double beat;
        int noteRightEdgeX;  // pixel X of the note's right edge (-1 if no hit)
    };

    HitResult hitTestNote(const juce::MouseEvent& e) {
        int row = e.y / rowHeight_;
        if (row < 0 || row >= static_cast<int>(padRows_->size()))
            return {-1, -1, 0.0, -1};

        int noteNumber = (*padRows_)[row].noteNumber;
        double rawBeat = static_cast<double>(e.x - GRID_LEFT_PADDING) / pixelsPerBeat_;
        if (rawBeat < 0.0)
            rawBeat = 0.0;
        double snappedBeat = rawBeat;
        if (snapEnabled_ && gridResolutionBeats_ > 0.0) {
            snappedBeat = std::floor(rawBeat / gridResolutionBeats_) * gridResolutionBeats_;
        }

        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        if (!clip)
            return {-1, row, snappedBeat, -1};

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];
            if (note.noteNumber == noteNumber) {
                int noteLeftX =
                    static_cast<int>(note.startBeat * pixelsPerBeat_) + GRID_LEFT_PADDING;
                int noteW = juce::jmax(4, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
                int noteRightX = noteLeftX + noteW;

                if (e.x >= noteLeftX && e.x < noteRightX) {
                    return {static_cast<int>(i), row, snappedBeat, noteRightX};
                }
            }
        }
        return {-1, row, snappedBeat, -1};
    }

    struct DragState {
        bool active = false;
        DragMode dragMode = DragMode::None;
        size_t noteIndex = 0;
        double originalBeat = 0.0;
        int originalRow = 0;
        double currentBeat = 0.0;
        int currentRow = 0;
        bool hasMoved = false;
        double originalLength = 0.0;
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
    double gridResolutionBeats_ = 0.25;
    bool snapEnabled_ = true;
    int timeSigNumerator_ = 4;
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
