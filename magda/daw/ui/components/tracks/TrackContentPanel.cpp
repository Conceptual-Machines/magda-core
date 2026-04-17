#include "TrackContentPanel.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>

#include "../../panels/state/PanelController.hpp"
#include "../../state/TimelineEvents.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../utils/TimelineUtils.hpp"
#include "../automation/AutomationLaneComponent.hpp"
#include "../clips/ClipComponent.hpp"
#include "Config.hpp"
#include "core/ClipCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/UndoManager.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

TrackContentPanel::TrackContentPanel() {
    // Load configuration values, converting bars → seconds at default tempo
    auto& config = magda::Config::getInstance();
    TempoState defaultTempo;
    timelineLength = defaultTempo.barsToTime(config.getDefaultTimelineLengthBars());

    // Set up the component
    setSize(1000, 200);
    setOpaque(true);
    setWantsKeyboardFocus(true);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Register as AutomationManager listener
    AutomationManager::getInstance().addListener(this);

    // Build tracks from TrackManager
    tracksChanged();

    // Build clips from ClipManager
    rebuildClipComponents();
}

TrackContentPanel::~TrackContentPanel() {
    // Stop timer for edit cursor blinking
    stopTimer();

    // Unregister from TrackManager
    TrackManager::getInstance().removeListener(this);

    // Unregister from ClipManager
    ClipManager::getInstance().removeListener(this);

    // Unregister from ViewModeController
    ViewModeController::getInstance().removeListener(this);

    // Unregister from AutomationManager
    AutomationManager::getInstance().removeListener(this);

    // Unregister from controller if we have one
    if (timelineController) {
        timelineController->removeListener(this);
    }
}

void TrackContentPanel::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    tracksChanged();  // Rebuild with new visibility settings
}

void TrackContentPanel::repaintVisible() {
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>()) {
        repaint(juce::Rectangle<int>(viewport->getViewPositionX(), viewport->getViewPositionY(),
                                     viewport->getViewWidth(), viewport->getViewHeight()));
    } else {
        juce::Component::repaint();
    }
}

// Clip a repaint rect to only the visible viewport portion (avoids invalidating
// e.g. a 65000px-wide track lane when only 1200px is on screen).
void TrackContentPanel::repaintVisiblePortion(const juce::Rectangle<int>& rect) {
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>()) {
        auto visibleArea =
            juce::Rectangle<int>(viewport->getViewPositionX(), viewport->getViewPositionY(),
                                 viewport->getViewWidth(), viewport->getViewHeight());
        auto clipped = rect.getIntersection(visibleArea);
        if (!clipped.isEmpty())
            repaint(clipped);
    } else {
        repaint(rect);
    }
}

void TrackContentPanel::tracksChanged() {
    groupExtentCacheDirty_ = true;
    // Rebuild track lanes from TrackManager
    trackLanes.clear();
    visibleTrackIds_.clear();
    selectedTrackIndex = -1;

    // Build visible tracks list (respecting hierarchy)
    auto& trackManager = TrackManager::getInstance();
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);

    // Helper lambda to add track and its visible children recursively
    std::function<void(TrackId, int)> addTrackRecursive = [&](TrackId trackId, int depth) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        visibleTrackIds_.push_back(trackId);

        auto lane = std::make_unique<TrackLane>();
        // Use height from view settings
        lane->height = track->viewSettings.getHeight(currentViewMode_);
        trackLanes.push_back(std::move(lane));

        // Add children if group is not collapsed
        if (track->hasChildren() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId, depth + 1);
            }
        }
    };

    // Add all visible top-level tracks (and their children)
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId, 0);
    }

    resized();
    updateClipComponentPositions();
    repaintVisible();
}

void TrackContentPanel::setController(TimelineController* controller) {
    // Unregister from old controller
    if (timelineController) {
        timelineController->removeListener(this);
    }

    timelineController = controller;

    // Register with new controller
    if (timelineController) {
        timelineController->addListener(this);

        // Sync initial state
        const auto& state = timelineController->getState();
        timelineLength = state.timelineLength;
        currentZoom = state.zoom.horizontalZoom;
        displayMode = state.display.timeDisplayMode;
        tempoBPM = state.tempo.bpm;
        timeSignatureNumerator = state.tempo.timeSignatureNumerator;
        timeSignatureDenominator = state.tempo.timeSignatureDenominator;

        repaintVisible();
    }
}

// ===== TimelineStateListener Implementation =====

void TrackContentPanel::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    bool needsRepaint = false;

    // Zoom/scroll changes
    if (hasFlag(changes, ChangeFlags::Zoom) || hasFlag(changes, ChangeFlags::Scroll)) {
        currentZoom = state.zoom.horizontalZoom;
        needsRepaint = true;
    }

    // General cache sync with dirty checks
    if (timelineLength != state.timelineLength) {
        timelineLength = state.timelineLength;
        needsRepaint = true;
    }
    if (displayMode != state.display.timeDisplayMode) {
        displayMode = state.display.timeDisplayMode;
        needsRepaint = true;
    }

    // Tempo affects non-musical-mode clip widths (length * bpm / 60 * ppb)
    if (tempoBPM != state.tempo.bpm) {
        tempoBPM = state.tempo.bpm;
        needsRepaint = true;
    }
    timeSignatureNumerator = state.tempo.timeSignatureNumerator;
    timeSignatureDenominator = state.tempo.timeSignatureDenominator;

    // Manage edit cursor blink timer (unconditional)
    if (state.editCursorPosition >= 0) {
        editCursorBlinkVisible_ = true;
        if (!isTimerRunning()) {
            startTimer(EDIT_CURSOR_BLINK_MS);
        }
    } else {
        if (isTimerRunning()) {
            stopTimer();
        }
    }

    // Edit cursor position changed — repaint immediately (bounded to cursor strip)
    if (hasFlag(changes, ChangeFlags::Selection)) {
        editCursorBlinkVisible_ = true;
        // Restart timer so blink phase begins from now
        if (state.editCursorPosition >= 0) {
            stopTimer();
            startTimer(EDIT_CURSOR_BLINK_MS);
        }
        // Bounded repaint: just the cursor strip on the selected track
        if (selectedTrackIndex >= 0 && selectedTrackIndex < static_cast<int>(trackLanes.size())) {
            int cursorX = timeToPixel(state.editCursorPosition);
            auto trackArea = getTrackLaneArea(selectedTrackIndex);
            repaint(juce::Rectangle<int>(cursorX - 3, trackArea.getY(), 6, trackArea.getHeight()));
        }
    }

    if (needsRepaint) {
        updateClipComponentPositions();
        resized();
        repaintVisible();
    }
}

void TrackContentPanel::rebuildGroupExtentCache() {
    groupExtentCache_.clear();
    auto& clipManager = ClipManager::getInstance();
    auto& trackManager = TrackManager::getInstance();

    for (auto trackId : visibleTrackIds_) {
        auto* trackInfo = trackManager.getTrack(trackId);
        if (!trackInfo || !trackInfo->isGroup())
            continue;

        GroupExtent extent;
        auto descendants = trackManager.getAllDescendants(trackId);
        for (auto childId : descendants) {
            for (auto clipId : clipManager.getClipsOnTrack(childId)) {
                const auto* clip = clipManager.getClip(clipId);
                if (clip && clip->view == ClipView::Arrangement) {
                    if (!extent.hasClips) {
                        extent.earliest = clip->startTime;
                        extent.latest = clip->startTime + clip->length;
                        extent.hasClips = true;
                    } else {
                        extent.earliest = std::min(extent.earliest, clip->startTime);
                        extent.latest = std::max(extent.latest, clip->startTime + clip->length);
                    }
                }
            }
        }
        groupExtentCache_[trackId] = extent;
    }
    groupExtentCacheDirty_ = false;
}

void TrackContentPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    // Rebuild group extent cache if dirty (at most once per paint)
    if (groupExtentCacheDirty_)
        rebuildGroupExtentCache();

    // Grid is now drawn by GridOverlayComponent in MainView
    // This component only draws track lanes with horizontal separators
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        auto laneArea = getTrackLaneArea(static_cast<int>(i));
        if (laneArea.intersects(g.getClipBounds())) {
            paintTrackLane(g, *trackLanes[i], laneArea, static_cast<int>(i) == selectedTrackIndex,
                           static_cast<int>(i));
        }
    }

    // Ghost clips are drawn behind clips (part of background)
    paintClipGhosts(g);
}

void TrackContentPanel::paintOverChildren(juce::Graphics& g) {
    // Draw recording previews on top of any existing clip components
    paintRecordingPreviews(g);

    // Draw edit cursor line on top of clips
    paintEditCursor(g);

    // Draw marquee selection rectangle on top of everything
    paintMarqueeRect(g);

    // Plugin drop: no content-area feedback — the track header highlight is enough

    // Draw drop indicator for file drag-and-drop
    if (showDropIndicator_) {
        int dropX = timeToPixel(dropInsertTime_);

        if (dropTargetTrackIndex_ >= 0 &&
            dropTargetTrackIndex_ < static_cast<int>(trackLanes.size())) {
            // Dropping on an existing track — line spans that track
            int trackY = getTrackYPosition(dropTargetTrackIndex_);
            int trackHeight = getTrackHeight(dropTargetTrackIndex_);

            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.drawLine(static_cast<float>(dropX), static_cast<float>(trackY),
                       static_cast<float>(dropX), static_cast<float>(trackY + trackHeight), 2.0f);
        } else {
            // Dropping on empty area — show drop line spanning a phantom track region
            int topY = getTotalTracksHeight();
            int bottomY = topY + DEFAULT_TRACK_HEIGHT;

            g.setColour(juce::Colours::yellow.withAlpha(0.8f));
            g.drawLine(static_cast<float>(dropX), static_cast<float>(topY),
                       static_cast<float>(dropX), static_cast<float>(bottomY), 2.0f);
        }
    }
}

void TrackContentPanel::resized() {
    // Size the panel to match its content exactly. Using jmax(contentHeight,
    // getHeight()) here would be monotonic — when lanes shrink or hide the
    // panel would stay inflated, producing phantom scrollbar space in the
    // outer viewport and stale pixels revealed by scrolling into the empty
    // area. The outer viewport already handles "content smaller than
    // viewport" by showing the component at its natural size with no
    // scrollbar.
    double beats = timelineLength * tempoBPM / 60.0;
    int contentWidth = static_cast<int>(std::round(beats * currentZoom));
    int contentHeight = juce::jmax(getTotalTracksHeight(), minHeight_);

    setSize(contentWidth, contentHeight);

    // Re-position lane viewports with the current (possibly updated) width.
    // This is needed because lane viewports are sized to getWidth() which may
    // change when the panel is first laid out or when the window is resized.
    updateAutomationLanePositions();
}

void TrackContentPanel::selectTrack(int index) {
    if (index >= 0 && index < static_cast<int>(trackLanes.size())) {
        int oldIndex = selectedTrackIndex;
        selectedTrackIndex = index;

        if (onTrackSelected) {
            onTrackSelected(index);
        }

        // Bounded repaint: old and new track lanes only (clipped to visible viewport)
        if (oldIndex >= 0 && oldIndex < static_cast<int>(trackLanes.size()))
            repaintVisiblePortion(getTrackLaneArea(oldIndex));
        repaintVisiblePortion(getTrackLaneArea(index));
    }
}

void TrackContentPanel::trackSelectionChanged(TrackId trackId) {
    int oldIndex = selectedTrackIndex;
    selectedTrackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            selectedTrackIndex = static_cast<int>(i);
            break;
        }
    }
    // Bounded repaint: old and new track lanes only (clipped to visible viewport)
    if (oldIndex >= 0 && oldIndex < static_cast<int>(trackLanes.size()))
        repaintVisiblePortion(getTrackLaneArea(oldIndex));
    if (selectedTrackIndex >= 0)
        repaintVisiblePortion(getTrackLaneArea(selectedTrackIndex));
}

void TrackContentPanel::trackPropertyChanged(int /*trackId*/) {
    // Repaint when track properties change (e.g. playback mode switching
    // between Arrangement and Session) to update visual overlays.
    repaintVisible();
}

int TrackContentPanel::getNumTracks() const {
    return static_cast<int>(trackLanes.size());
}

void TrackContentPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackLanes.size())) {
        height = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, height);
        if (trackLanes[trackIndex]->height == height)
            return;
        trackLanes[trackIndex]->height = height;

        updateClipComponentPositions();
        updateAutomationLanePositions();
        resized();
        repaintVisible();

        if (onTrackHeightChanged) {
            onTrackHeightChanged(trackIndex, height);
        }
    }
}

int TrackContentPanel::getTrackHeight(int trackIndex) const {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackLanes.size())) {
        return trackLanes[trackIndex]->height;
    }
    return DEFAULT_TRACK_HEIGHT;
}

void TrackContentPanel::setZoom(double zoom) {
    zoom = juce::jmax(0.1, zoom);
    if (std::abs(zoom - currentZoom) < 0.001)
        return;
    currentZoom = zoom;
    updateClipComponentPositions();
    updateAutomationLanePositions();
    resized();
    repaintVisible();
}

void TrackContentPanel::setVerticalZoom(double zoom) {
    zoom = juce::jlimit(0.5, 3.0, zoom);
    if (std::abs(zoom - verticalZoom) < 0.001)
        return;
    verticalZoom = zoom;
    updateClipComponentPositions();
    updateAutomationLanePositions();
    resized();
    repaintVisible();
}

void TrackContentPanel::setTimelineLength(double lengthInSeconds) {
    timelineLength = lengthInSeconds;
    resized();
    repaintVisible();
}

void TrackContentPanel::setTimeDisplayMode(TimeDisplayMode mode) {
    displayMode = mode;
    repaintVisible();
}

void TrackContentPanel::setTempo(double bpm) {
    tempoBPM = juce::jlimit(20.0, 999.0, bpm);
    repaintVisible();
}

void TrackContentPanel::setTimeSignature(int numerator, int denominator) {
    timeSignatureNumerator = juce::jlimit(1, 16, numerator);
    timeSignatureDenominator = juce::jlimit(1, 16, denominator);
    repaintVisible();
}

int TrackContentPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        totalHeight += getTrackTotalHeight(static_cast<int>(i));
    }
    return totalHeight;
}

int TrackContentPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < static_cast<int>(trackLanes.size()); ++i) {
        yPosition += getTrackTotalHeight(i);
    }
    return yPosition;
}

void TrackContentPanel::paintTrackLane(juce::Graphics& g, const TrackLane& /*lane*/,
                                       juce::Rectangle<int> area, bool isSelected, int trackIndex) {
    // Background (semi-transparent to let grid show through)
    auto bgColour = isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                               : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND);
    g.setColour(bgColour.withAlpha(0.7f));
    g.fillRect(area);

    // Border (horizontal separators between tracks)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area, 1);

    // Frozen overlay
    if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
        auto* trackInfo = TrackManager::getInstance().getTrack(visibleTrackIds_[trackIndex]);
        if (trackInfo && trackInfo->frozen) {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRect(area);
        }
        // Session mode overlay — dim track lane when in Session mode
        if (trackInfo && trackInfo->playbackMode == TrackPlaybackMode::Session) {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRect(area);
        }

        // Group extent indicator — show the time range covered by all child clips
        if (trackInfo && trackInfo->isGroup()) {
            auto extentIt = groupExtentCache_.find(trackInfo->id);
            bool hasClips = extentIt != groupExtentCache_.end() && extentIt->second.hasClips;
            double earliest = hasClips ? extentIt->second.earliest : 0.0;
            double latest = hasClips ? extentIt->second.latest : 0.0;

            if (hasClips && latest > earliest) {
                int x1 = timeToPixel(earliest);
                int x2 = timeToPixel(latest);
                auto extentArea =
                    juce::Rectangle<int>(x1, area.getY() + 2, x2 - x1, area.getHeight() - 4);

                // Subtle filled background
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.fillRoundedRectangle(extentArea.toFloat(), 3.0f);

                // Outline
                g.setColour(juce::Colours::white.withAlpha(0.15f));
                g.drawRoundedRectangle(extentArea.toFloat(), 3.0f, 1.0f);
            }
        }
    }
}

void TrackContentPanel::paintRecordingPreviews(juce::Graphics& g) {
    if (!audioEngine_)
        return;

    const auto& previews = audioEngine_->getRecordingPreviews();
    if (previews.empty())
        return;

    static int paintCount = 0;
    paintCount++;

    double tempo = tempoBPM;
    double beatsPerSecond = tempo / 60.0;

    constexpr int HEADER_HEIGHT = 16;
    constexpr float CORNER_RADIUS = 4.0f;
    constexpr int MIDI_MAX = 127;
    constexpr int MIDI_RANGE = 127;

    for (const auto& [trackId, preview] : previews) {
        // Find the track index for this trackId
        int trackIndex = -1;
        for (int i = 0; i < static_cast<int>(visibleTrackIds_.size()); ++i) {
            if (visibleTrackIds_[static_cast<size_t>(i)] == trackId) {
                trackIndex = i;
                break;
            }
        }
        if (trackIndex < 0) {
            DBG("RecPreview::paint: track " << trackId << " not in visibleTrackIds_");
            continue;
        }

        // Compute clip bounds in pixels
        int clipX = timeToPixel(preview.startTime);
        int clipEndX = timeToPixel(preview.startTime + preview.currentLength);
        int clipW = juce::jmax(2, clipEndX - clipX);
        int trackY = getTrackYPosition(trackIndex);
        int trackH = getTrackHeight(trackIndex);

        juce::Rectangle<int> bounds(clipX, trackY, clipW, trackH);

        // Use the same colour the final clip will get (based on current clip count)
        juce::Colour baseColour = juce::Colour(Config::getDefaultColour(
            static_cast<int>(ClipManager::getInstance().getArrangementClips().size())));

        if (paintCount % 60 == 1) {
            DBG("RecPreview::paint: track=" << trackId << " bounds=" << bounds.toString()
                                            << " notes=" << preview.notes.size()
                                            << " len=" << preview.currentLength);
        }

        // Background fill
        g.setColour(baseColour.darker(0.3f));
        g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

        // Note area (below header)
        auto noteArea = bounds.reduced(2, HEADER_HEIGHT + 2);

        if (preview.isAudioRecording && !preview.audioPeaks.empty() && noteArea.getHeight() > 5) {
            // Draw audio waveform (symmetric around vertical center)
            g.setColour(baseColour.brighter(0.3f));

            float centerY = static_cast<float>(noteArea.getCentreY());
            float halfHeight = noteArea.getHeight() * 0.5f;
            int numPeaks = static_cast<int>(preview.audioPeaks.size());

            for (int px = noteArea.getX(); px < noteArea.getRight(); ++px) {
                float frac = static_cast<float>(px - noteArea.getX()) /
                             static_cast<float>(noteArea.getWidth());
                int peakIdx = juce::jlimit(0, numPeaks - 1, static_cast<int>(frac * numPeaks));

                float peak = juce::jmax(preview.audioPeaks[peakIdx].peakL,
                                        preview.audioPeaks[peakIdx].peakR);
                peak = juce::jmin(peak, 1.0f);

                float lineHalf = peak * halfHeight;
                if (lineHalf < 0.5f)
                    lineHalf = 0.5f;

                g.drawVerticalLine(px, centerY - lineHalf, centerY + lineHalf);
            }
        } else if (!preview.notes.empty() && noteArea.getHeight() > 5) {
            g.setColour(baseColour.brighter(0.3f));

            double clipLengthInBeats = preview.currentLength * beatsPerSecond;
            double beatRange = juce::jmax(1.0, clipLengthInBeats);

            for (const auto& note : preview.notes) {
                double displayStart = note.startBeat;
                double noteLen = note.lengthBeats;
                if (noteLen < 0.0) {
                    // Open note (being held) — extend to clip end
                    noteLen = clipLengthInBeats - displayStart;
                    if (noteLen < 0.05)
                        noteLen = 0.05;
                }
                double displayEnd = displayStart + noteLen;

                if (displayEnd <= 0.0 || displayStart >= clipLengthInBeats)
                    continue;

                double visibleStart = juce::jmax(0.0, displayStart);
                double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
                double visibleLength = visibleEnd - visibleStart;

                float noteY = noteArea.getY() + (MIDI_MAX - note.noteNumber) *
                                                    noteArea.getHeight() / (MIDI_RANGE + 1);
                float noteHeight =
                    juce::jmax(1.5f, static_cast<float>(noteArea.getHeight()) / (MIDI_RANGE + 1));
                float noteX = noteArea.getX() +
                              static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
                float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                       noteArea.getWidth());

                g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
            }
        }

        // Border
        g.setColour(baseColour);
        g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);

        // Header with "Recording..." label
        auto headerArea = bounds.withHeight(HEADER_HEIGHT);
        g.setColour(baseColour);
        g.fillRoundedRectangle(
            headerArea.toFloat().withBottom(static_cast<float>(headerArea.getBottom() + 2)),
            CORNER_RADIUS);

        if (bounds.getWidth() > 40) {
            g.setColour(juce::Colours::black);
            g.setFont(10.0f);
            g.drawText("Recording...", headerArea.reduced(4, 0), juce::Justification::centredLeft,
                       true);
        }
    }

    // Schedule bounded repaint covering only the recording preview areas.
    // At high zoom the full component can be 65000+ px wide — avoid invalidating all of it.
    juce::Rectangle<int> previewUnion;
    for (const auto& [tId, prev] : previews) {
        int px = timeToPixel(prev.startTime);
        int pxEnd = timeToPixel(prev.startTime + prev.currentLength);
        int tIdx = -1;
        for (int i = 0; i < static_cast<int>(visibleTrackIds_.size()); ++i) {
            if (visibleTrackIds_[static_cast<size_t>(i)] == tId) {
                tIdx = i;
                break;
            }
        }
        if (tIdx >= 0) {
            int tY = getTrackYPosition(tIdx);
            int tH = getTrackHeight(tIdx);
            auto r = juce::Rectangle<int>(px, tY, juce::jmax(2, pxEnd - px), tH);
            previewUnion = previewUnion.isEmpty() ? r : previewUnion.getUnion(r);
        }
    }
    if (!previewUnion.isEmpty())
        repaint(previewUnion.expanded(4));
}

void TrackContentPanel::paintEditCursor(juce::Graphics& g) {
    if (!timelineController) {
        return;
    }

    // Determine which track to draw the cursor on
    int cursorTrackIndex = selectedTrackIndex;

    // If no track is selected, fall back to the selected clip's track
    if (cursorTrackIndex < 0) {
        auto selectedClipId = magda::SelectionManager::getInstance().getSelectedClip();
        if (selectedClipId != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId);
            if (clip) {
                for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
                    if (visibleTrackIds_[i] == clip->trackId) {
                        cursorTrackIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
    }

    if (cursorTrackIndex < 0) {
        return;
    }

    const auto& state = timelineController->getState();
    double editCursorPos = state.editCursorPosition;

    // Don't draw if position is invalid (< 0 means hidden)
    if (editCursorPos < 0 || editCursorPos > timelineLength) {
        return;
    }

    // Blink effect - only draw when visible
    if (!editCursorBlinkVisible_) {
        return;
    }

    // Calculate X position
    int cursorX = timeToPixel(editCursorPos);

    // Only draw on selected track(s)
    auto trackArea = getTrackLaneArea(cursorTrackIndex);
    if (trackArea.isEmpty()) {
        return;
    }

    // Draw edit cursor as a prominent white line
    float top = static_cast<float>(trackArea.getY());
    float bottom = static_cast<float>(trackArea.getBottom());
    float x = static_cast<float>(cursorX);

    // Draw glow/shadow for visibility over grid lines
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawLine(x - 1.0f, top, x - 1.0f, bottom, 1.0f);
    g.drawLine(x + 1.0f, top, x + 1.0f, bottom, 1.0f);

    // Draw main white cursor line
    g.setColour(juce::Colours::white);
    g.drawLine(x, top, x, bottom, 2.0f);
}

juce::Rectangle<int> TrackContentPanel::getTrackLaneArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackLanes.size())) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackLanes[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition, getWidth(), height);
}

bool TrackContentPanel::isInSelectableArea(int x, int y) const {
    // Check if we're in an empty track area (not on a clip)
    // For now, entire track area is selectable since we don't have clips yet
    // In the future, check if clicking on upper half of clips
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        if (getTrackLaneArea(static_cast<int>(i)).contains(x, y)) {
            return true;
        }
    }
    return false;
}

double TrackContentPanel::pixelToTime(int pixel) const {
    if (currentZoom > 0 && tempoBPM > 0)
        return pixelToBeats(pixel) * 60.0 / tempoBPM;
    return 0.0;
}

int TrackContentPanel::beatsToPixel(double beats) const {
    return static_cast<int>(std::round(beats * currentZoom)) + LayoutConfig::TIMELINE_LEFT_PADDING;
}

double TrackContentPanel::pixelToBeats(int pixel) const {
    if (currentZoom > 0)
        return static_cast<double>(pixel - LayoutConfig::TIMELINE_LEFT_PADDING) / currentZoom;
    return 0.0;
}

int TrackContentPanel::timeToPixel(double time) const {
    return beatsToPixel(time * tempoBPM / 60.0);
}

int TrackContentPanel::getTrackIndexAtY(int y) const {
    int currentY = 0;
    for (size_t i = 0; i < trackLanes.size(); ++i) {
        int trackHeight = static_cast<int>(trackLanes[i]->height * verticalZoom);
        // Only match within the track's own lane area, not automation lanes below it
        if (y >= currentY && y < currentY + trackHeight) {
            return static_cast<int>(i);
        }
        // Advance past both the track and its automation lanes
        currentY += getTrackTotalHeight(static_cast<int>(i));
    }
    return -1;  // Not in any track (or in an automation lane)
}

bool TrackContentPanel::isOnExistingSelection(int x, int y) const {
    // Check if there's an active selection in the controller
    if (!timelineController) {
        return false;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return false;
    }

    // Check horizontal bounds (time-based)
    double clickTime = pixelToTime(x);
    if (clickTime < selection.startTime || clickTime > selection.endTime) {
        return false;
    }

    // Check vertical bounds (track-based)
    int trackIndex = getTrackIndexAtY(y);
    if (trackIndex < 0) {
        return false;
    }

    // Check if this track is part of the selection
    return selection.includesTrack(trackIndex);
}

bool TrackContentPanel::isOnSelectionEdge(int x, int y, bool& isLeftEdge) const {
    if (!timelineController) {
        return false;
    }

    const auto& selection = timelineController->getState().selection;
    if (!selection.isActive()) {
        return false;
    }

    // Check vertical bounds (must be on a selected track)
    int trackIndex = getTrackIndexAtY(y);
    if (trackIndex < 0 || !selection.includesTrack(trackIndex)) {
        return false;
    }

    // Check if mouse is near the edges (within EDGE_THRESHOLD pixels)
    static constexpr int EDGE_THRESHOLD = 8;
    int startX = timeToPixel(selection.startTime);
    int endX = timeToPixel(selection.endTime);

    if (std::abs(x - startX) <= EDGE_THRESHOLD) {
        isLeftEdge = true;
        return true;
    }

    if (std::abs(x - endX) <= EDGE_THRESHOLD) {
        isLeftEdge = false;
        return true;
    }

    return false;
}

void TrackContentPanel::mouseDown(const juce::MouseEvent& event) {
    // Grab keyboard focus so we can receive key events (like 'B' for blade)
    grabKeyboardFocus();

    // Right-click is handled in mouseUp (context menu); don't start drag/selection
    if (event.mods.isPopupMenu())
        return;

    // Store initial mouse position for click vs drag detection
    mouseDownX = event.x;
    mouseDownY = event.y;

    // Capture Shift state and starting track index for per-track selection
    isShiftHeld = event.mods.isShiftDown();
    selectionStartTrackIndex = getTrackIndexAtY(event.y);

    // Reset drag type
    currentDragType_ = DragType::None;

    // Zone-based behavior:
    // Upper half of track = clip operations
    // Lower half of track = time selection operations
    bool inUpperZone = isInUpperTrackZone(event.y);
    bool onClip = getClipComponentAt(event.x, event.y) != nullptr;

    // Select track based on click position - but ONLY in upper zone
    // (Lower zone is for timeline operations, shouldn't affect track selection)
    if (inUpperZone) {
        for (size_t i = 0; i < trackLanes.size(); ++i) {
            if (getTrackLaneArea(static_cast<int>(i)).contains(event.getPosition())) {
                selectTrack(static_cast<int>(i));
                break;
            }
        }
    }

    if (inUpperZone) {
        // UPPER ZONE: Clip operations
        // Clear time selection when clicking in upper zone outside the selection area
        if (timelineController && timelineController->getState().selection.isActive()) {
            if (!isOnExistingSelection(event.x, event.y)) {
                if (onTimeSelectionChanged) {
                    onTimeSelectionChanged(-1.0, -1.0, {});
                }
            }
        }
        if (!onClip) {
            // Clicked empty space in upper zone - deselect clips (unless Cmd held)
            if (!event.mods.isCommandDown()) {
                SelectionManager::getInstance().clearSelection();
            }
        }
        // If on clip, the ClipComponent handles mouse events
        // Prepare for potential marquee if they drag
        if (!onClip && isInSelectableArea(event.x, event.y)) {
            isCreatingSelection = true;
            isMovingSelection = false;
        }
        // Ensure we keep keyboard focus after upper zone operations
        // (selectTrack or selection changes might have shifted focus)
        grabKeyboardFocus();
    } else {
        // LOWER ZONE: Time selection / edit cursor operations
        // Explicitly preserve clip selection when clicking in lower zone
        // (User might be positioning edit cursor to split selected clips)

        // LOWER ZONE: Time selection operations
        bool isLeftEdge = false;
        if (isOnSelectionEdge(event.x, event.y, isLeftEdge)) {
            // Clicked on edge of existing time selection - prepare to resize it
            const auto& selection = timelineController->getState().selection;
            isMovingSelection = false;
            isCreatingSelection = false;
            currentDragType_ =
                isLeftEdge ? DragType::ResizeSelectionLeft : DragType::ResizeSelectionRight;
            moveDragStartTime = pixelToTime(event.x);
            moveSelectionOriginalStart = selection.startTime;
            moveSelectionOriginalEnd = selection.endTime;
            moveSelectionOriginalTracks = selection.trackIndices;

            // Capture all clips within the time selection for trimming
            originalClipsInSelection_.clear();
            const auto& clips = ClipManager::getInstance().getArrangementClips();
            for (const auto& clip : clips) {
                // Check if clip's track is in the selection
                auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip.trackId);
                if (it == visibleTrackIds_.end()) {
                    continue;
                }

                int trackIndex = static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
                if (!selection.includesTrack(trackIndex)) {
                    continue;
                }

                // Check if clip overlaps with selection time range
                double clipEnd = clip.startTime + clip.length;
                if (clip.startTime < selection.endTime && clipEnd > selection.startTime) {
                    ClipOriginalData data;
                    data.originalStartTime = clip.startTime;
                    data.originalLength = clip.length;
                    data.originalTrackId = clip.trackId;
                    originalClipsInSelection_[clip.id] = data;
                }
            }
            return;
        } else if (isOnExistingSelection(event.x, event.y)) {
            // Clicked inside existing time selection - prepare to move it
            const auto& selection = timelineController->getState().selection;
            isMovingSelection = true;
            isCreatingSelection = false;
            currentDragType_ = DragType::MoveSelection;
            moveDragStartTime = pixelToTime(event.x);
            moveSelectionOriginalStart = selection.startTime;
            moveSelectionOriginalEnd = selection.endTime;
            moveSelectionOriginalTracks = selection.trackIndices;

            // Defer split to first drag motion (so click-without-drag doesn't split)
            needsSplitOnFirstDrag_ = true;
            return;
        } else {
            // Clicked outside time selection in lower zone - clear it and start new one
            if (timelineController && timelineController->getState().selection.isActive()) {
                // Clear existing time selection
                if (onTimeSelectionChanged) {
                    onTimeSelectionChanged(-1.0, -1.0, {});
                }
            }
            // Prepare for new time selection
            if (isInSelectableArea(event.x, event.y)) {
                isCreatingSelection = true;
                isMovingSelection = false;
                selectionStartTime = juce::jmax(0.0, pixelToTime(event.x));

                // Apply snap to grid if callback is set
                if (snapTimeToGrid) {
                    selectionStartTime = snapTimeToGrid(selectionStartTime);
                }

                selectionEndTime = selectionStartTime;
            }
        }
    }
}

void TrackContentPanel::mouseDrag(const juce::MouseEvent& event) {
    if (currentDragType_ == DragType::ResizeSelectionLeft ||
        currentDragType_ == DragType::ResizeSelectionRight) {
        // Resizing time selection edge
        double currentTime = pixelToTime(event.x);

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            currentTime = snapTimeToGrid(currentTime);
        }

        double newStart = moveSelectionOriginalStart;
        double newEnd = moveSelectionOriginalEnd;

        if (currentDragType_ == DragType::ResizeSelectionLeft) {
            // Resizing left edge
            newStart = juce::jlimit(0.0, moveSelectionOriginalEnd - 0.1, currentTime);
        } else {
            // Resizing right edge
            newEnd = juce::jlimit(moveSelectionOriginalStart + 0.1, timelineLength, currentTime);
        }

        // Update time selection visually
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(newStart, newEnd, moveSelectionOriginalTracks);
        }
    } else if (isMovingSelection) {
        // Split clips at selection boundaries on first drag motion
        if (needsSplitOnFirstDrag_) {
            needsSplitOnFirstDrag_ = false;
            splitClipsAtSelectionBoundaries();
            captureClipsInTimeSelection();
        }

        // Calculate time delta from drag start
        double currentTime = pixelToTime(event.x);
        double deltaTime = currentTime - moveDragStartTime;

        // Calculate new selection bounds
        double newStart = moveSelectionOriginalStart + deltaTime;
        double newEnd = moveSelectionOriginalEnd + deltaTime;

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            double snappedStart = snapTimeToGrid(newStart);
            double snapDelta = snappedStart - newStart;
            newStart = snappedStart;
            newEnd += snapDelta;
            deltaTime += snapDelta;  // Adjust delta for clip movement
        }

        // Clamp to timeline bounds
        double duration = moveSelectionOriginalEnd - moveSelectionOriginalStart;
        if (newStart < 0) {
            newStart = 0;
            newEnd = duration;
            deltaTime = -moveSelectionOriginalStart;
        }
        if (newEnd > timelineLength) {
            newEnd = timelineLength;
            newStart = timelineLength - duration;
            deltaTime = newStart - moveSelectionOriginalStart;
        }

        // Move clips visually along with the time selection
        moveClipsWithTimeSelection(deltaTime);

        // Notify about selection change (preserve original track indices)
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(newStart, newEnd, moveSelectionOriginalTracks);
        }
    } else if (isMarqueeActive_) {
        // Already in marquee mode - continue updating
        updateMarqueeSelection(event.getPosition());
    } else if (isCreatingSelection) {
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);
        int dragDistance = juce::jmax(deltaX, deltaY);

        // Determine mode based on where the drag STARTED (upper vs lower track zone)
        if (currentDragType_ == DragType::None && dragDistance > DRAG_START_THRESHOLD) {
            // Upper half of track = marquee selection, lower half = time selection
            if (isInUpperTrackZone(mouseDownY)) {
                // Start marquee selection
                isCreatingSelection = false;
                startMarqueeSelection(juce::Point<int>(mouseDownX, mouseDownY));
                updateMarqueeSelection(event.getPosition());
                return;
            } else {
                // Start time selection
                currentDragType_ = DragType::TimeSelection;
            }
        }

        // Update time selection end time
        selectionEndTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            selectionEndTime = snapTimeToGrid(selectionEndTime);
        }

        // Track the current track under the mouse for multi-track selection
        selectionEndTrackIndex = getTrackIndexAtY(event.y);

        // Clamp to valid track range (handle dragging above/below track area)
        if (selectionEndTrackIndex < 0) {
            // If above first track, select first track; if below last, select last
            if (event.y < 0) {
                selectionEndTrackIndex = 0;
            } else {
                selectionEndTrackIndex = static_cast<int>(trackLanes.size()) - 1;
            }
        }

        // Build track indices set: include all tracks between start and end
        std::set<int> trackIndices;
        if (isShiftHeld) {
            // Shift held = all tracks (empty set)
        } else if (selectionStartTrackIndex >= 0 && selectionEndTrackIndex >= 0) {
            // Include all tracks from start to end (inclusive)
            int minTrack = juce::jmin(selectionStartTrackIndex, selectionEndTrackIndex);
            int maxTrack = juce::jmax(selectionStartTrackIndex, selectionEndTrackIndex);
            for (int i = minTrack; i <= maxTrack; ++i) {
                trackIndices.insert(i);
            }
        }

        // Notify about selection change
        if (onTimeSelectionChanged) {
            double start = juce::jmin(selectionStartTime, selectionEndTime);
            double end = juce::jmax(selectionStartTime, selectionEndTime);
            onTimeSelectionChanged(start, end, trackIndices);
        }
    }
}

void TrackContentPanel::mouseUp(const juce::MouseEvent& event) {
    // Right-click on empty space shows context menu
    if (event.mods.isPopupMenu()) {
        if (getClipComponentAt(event.x, event.y) == nullptr) {
            showEmptySpaceContextMenu(event);
        }
        return;
    }

    if (currentDragType_ == DragType::ResizeSelectionLeft ||
        currentDragType_ == DragType::ResizeSelectionRight) {
        // Finalize time selection resize and trim clips
        double currentTime = pixelToTime(event.x);
        if (snapTimeToGrid) {
            currentTime = snapTimeToGrid(currentTime);
        }

        double newStart = moveSelectionOriginalStart;
        double newEnd = moveSelectionOriginalEnd;

        if (currentDragType_ == DragType::ResizeSelectionLeft) {
            newStart = juce::jlimit(0.0, moveSelectionOriginalEnd - 0.1, currentTime);
        } else {
            newEnd = juce::jlimit(moveSelectionOriginalStart + 0.1, timelineLength, currentTime);
        }

        // Trim all captured clips to the new selection bounds using undo commands
        auto& clipManager = ClipManager::getInstance();

        // Collect all trim operations
        std::vector<std::pair<ClipId, std::pair<double, bool>>> trimOperations;

        for (const auto& [clipId, originalData] : originalClipsInSelection_) {
            const auto* clip = clipManager.getClip(clipId);
            if (!clip)
                continue;

            // Check if clip overlaps with new selection
            if (clip->startTime < newEnd && clip->getEndTime() > newStart) {
                // Calculate new clip bounds (intersection of clip and selection)
                double clipNewStart = std::max(clip->startTime, newStart);
                double clipNewEnd = std::min(clip->getEndTime(), newEnd);
                double newLength = clipNewEnd - clipNewStart;

                if (newLength > 0.01) {  // At least 10ms
                    // Trim from left if needed
                    if (clipNewStart > clip->startTime) {
                        trimOperations.push_back({clipId, {newLength, true}});
                    }
                    // Trim from right if needed
                    else if (clipNewEnd < clip->getEndTime()) {
                        trimOperations.push_back({clipId, {newLength, false}});
                    }
                }
            }
        }

        // Use compound operation if trimming multiple clips
        if (trimOperations.size() > 1)
            UndoManager::getInstance().beginCompoundOperation("Trim Clips");

        for (const auto& [clipId, params] : trimOperations) {
            auto cmd = std::make_unique<ResizeClipCommand>(clipId, params.first, params.second,
                                                           getTempo());
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        if (trimOperations.size() > 1)
            UndoManager::getInstance().endCompoundOperation();

        // Move edit cursor to the trimmed edge position
        if (timelineController) {
            double cursorPosition =
                currentDragType_ == DragType::ResizeSelectionLeft ? newStart : newEnd;
            timelineController->dispatch(SetEditCursorEvent{cursorPosition});
        }

        // Update time selection to reflect new bounds
        if (onTimeSelectionChanged) {
            onTimeSelectionChanged(newStart, newEnd, moveSelectionOriginalTracks);
        }

        // Clear drag state
        currentDragType_ = DragType::None;
        moveDragStartTime = -1.0;
        moveSelectionOriginalStart = -1.0;
        moveSelectionOriginalEnd = -1.0;
        moveSelectionOriginalTracks.clear();
        originalClipsInSelection_.clear();
        return;
    } else if (isMovingSelection) {
        // Calculate final delta time to commit clips
        double currentTime = pixelToTime(event.x);
        double deltaTime = currentTime - moveDragStartTime;

        // Apply same snap logic as in mouseDrag
        double newStart = moveSelectionOriginalStart + deltaTime;
        if (snapTimeToGrid) {
            double snappedStart = snapTimeToGrid(newStart);
            double snapDelta = snappedStart - newStart;
            deltaTime += snapDelta;
        }

        // Clamp to timeline bounds
        double duration = moveSelectionOriginalEnd - moveSelectionOriginalStart;
        newStart = moveSelectionOriginalStart + deltaTime;
        double newEnd = moveSelectionOriginalEnd + deltaTime;
        if (newStart < 0) {
            deltaTime = -moveSelectionOriginalStart;
        }
        if (newEnd > timelineLength) {
            deltaTime = (timelineLength - duration) - moveSelectionOriginalStart;
        }

        // Commit clip positions
        commitClipsInTimeSelection(deltaTime);

        // Finalize move - the selection has already been updated via mouseDrag
        isMovingSelection = false;
        needsSplitOnFirstDrag_ = false;
        moveDragStartTime = -1.0;
        moveSelectionOriginalStart = -1.0;
        moveSelectionOriginalEnd = -1.0;
        moveSelectionOriginalTracks.clear();
        currentDragType_ = DragType::None;
        return;
    }

    // Handle marquee selection completion
    if (isMarqueeActive_) {
        finishMarqueeSelection(event.mods.isShiftDown());
        currentDragType_ = DragType::None;
        return;
    }

    // Check if this was a simple click (not drag) in upper zone - set edit cursor
    // This handles clicks that didn't become marquee or time selection
    int deltaX = std::abs(event.x - mouseDownX);
    int deltaY = std::abs(event.y - mouseDownY);
    bool wasClick = (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD);
    bool wasInUpperZone = isInUpperTrackZone(mouseDownY);
    bool clickedOnClip = getClipComponentAt(mouseDownX, mouseDownY) != nullptr;

    if (wasClick && wasInUpperZone && !clickedOnClip &&
        isInSelectableArea(mouseDownX, mouseDownY)) {
        // Simple click in upper zone empty space - set edit cursor
        double clickTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

        // Apply snap to grid if callback is set
        if (snapTimeToGrid) {
            clickTime = snapTimeToGrid(clickTime);
        }

        // Select the track that was clicked on so cursor is visible
        int trackIndex = getTrackIndexAtY(mouseDownY);
        if (trackIndex >= 0) {
            selectTrack(trackIndex);
        }

        // Dispatch edit cursor change through controller
        if (timelineController) {
            timelineController->dispatch(SetEditCursorEvent{clickTime});
        }

        // Re-grab keyboard focus after track selection (which may trigger callbacks that steal
        // focus)
        grabKeyboardFocus();

        // Prevent lower zone logic from also running
        isCreatingSelection = false;
    }

    if (isCreatingSelection) {
        isCreatingSelection = false;

        // Check if this was a click or a drag using pixel-based threshold
        int deltaX = std::abs(event.x - mouseDownX);
        int deltaY = std::abs(event.y - mouseDownY);

        if (deltaX <= DRAG_THRESHOLD && deltaY <= DRAG_THRESHOLD) {
            // It was a click in lower zone
            // Don't set edit cursor if clicking on existing time selection
            // (user might be about to double-click to create clip)
            if (!isOnExistingSelection(event.x, event.y)) {
                double clickTime =
                    juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

                // Apply snap to grid if callback is set
                if (snapTimeToGrid) {
                    clickTime = snapTimeToGrid(clickTime);
                }

                // Only select track if no clips are currently selected
                // (selectTrack triggers SelectionManager which clears clip selection)
                if (SelectionManager::getInstance().getSelectedClipCount() == 0) {
                    int trackIndex = getTrackIndexAtY(event.y);
                    if (trackIndex >= 0) {
                        selectTrack(trackIndex);
                    }
                }

                // Dispatch edit cursor change through controller (separate from playhead)
                if (timelineController) {
                    timelineController->dispatch(SetEditCursorEvent{clickTime});
                }

                // Re-grab keyboard focus after track selection (which may trigger callbacks that
                // steal focus)
                grabKeyboardFocus();
            }
        } else {
            // It was a drag - finalize time selection
            selectionEndTime = juce::jmax(0.0, juce::jmin(timelineLength, pixelToTime(event.x)));

            // Apply snap to grid if callback is set
            if (snapTimeToGrid) {
                selectionEndTime = snapTimeToGrid(selectionEndTime);
            }

            // Get final track index from mouse position
            selectionEndTrackIndex = getTrackIndexAtY(event.y);
            if (selectionEndTrackIndex < 0) {
                if (event.y < 0) {
                    selectionEndTrackIndex = 0;
                } else {
                    selectionEndTrackIndex = static_cast<int>(trackLanes.size()) - 1;
                }
            }

            // Normalize so start < end
            double start = juce::jmin(selectionStartTime, selectionEndTime);
            double end = juce::jmax(selectionStartTime, selectionEndTime);

            // Only create selection if it has meaningful duration
            if (end - start > 0.01) {  // At least 10ms selection
                // Build track indices set: include all tracks between start and end
                std::set<int> trackIndices;
                if (isShiftHeld) {
                    // Shift held = all tracks (empty set)
                } else if (selectionStartTrackIndex >= 0 && selectionEndTrackIndex >= 0) {
                    // Include all tracks from start to end (inclusive)
                    int minTrack = juce::jmin(selectionStartTrackIndex, selectionEndTrackIndex);
                    int maxTrack = juce::jmax(selectionStartTrackIndex, selectionEndTrackIndex);
                    for (int i = minTrack; i <= maxTrack; ++i) {
                        trackIndices.insert(i);
                    }
                }

                if (onTimeSelectionChanged) {
                    onTimeSelectionChanged(start, end, trackIndices);
                }

                // Shift+drag: split clips at selection boundaries
                if (isShiftHeld) {
                    splitClipsAtSelectionBoundaries();
                }
            }
        }

        selectionStartTime = -1.0;
        selectionEndTime = -1.0;
        selectionStartTrackIndex = -1;
        selectionEndTrackIndex = -1;
        isShiftHeld = false;
        currentDragType_ = DragType::None;
    }
}

void TrackContentPanel::mouseDoubleClick(const juce::MouseEvent& event) {
    // Double-clicking on an existing time selection creates a clip (only if no clip underneath)
    if (isOnExistingSelection(event.x, event.y) &&
        getClipComponentAt(event.x, event.y) == nullptr) {
        createClipFromTimeSelection();
    }
    // Double-clicking empty space (no clip, no selection) creates a 1-bar MIDI clip
    else if (getClipComponentAt(event.x, event.y) == nullptr) {
        int trackIndex = getTrackIndexAtY(event.y);
        if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
            TrackId trackId = visibleTrackIds_[trackIndex];
            double clickTime = pixelToTime(event.x);
            double startTime = snapTimeToGrid ? snapTimeToGrid(clickTime) : clickTime;
            createMidiClipAtPosition(trackId, startTime);
        }
    }
}

void TrackContentPanel::createMidiClipAtPosition(TrackId trackId, double startTime) {
    double barLength = (timeSignatureNumerator * 60.0) / tempoBPM;

    auto cmd = std::make_unique<CreateClipCommand>(ClipType::MIDI, trackId, startTime, barLength);
    UndoManager::getInstance().executeCommand(std::move(cmd));

    auto clipId = ClipManager::getInstance().getClipAtPosition(trackId, startTime);
    if (clipId != INVALID_CLIP_ID) {
        SelectionManager::getInstance().selectClip(clipId);
    }
}

void TrackContentPanel::showEmptySpaceContextMenu(const juce::MouseEvent& event) {
    int trackIndex = getTrackIndexAtY(event.y);
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];
    double clickTime = pixelToTime(event.x);
    double startTime = snapTimeToGrid ? snapTimeToGrid(clickTime) : clickTime;

    // Check frozen state
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    bool isFrozen = trackInfo && trackInfo->frozen;

    auto& clipManager = ClipManager::getInstance();
    bool hasClipboard = clipManager.hasClipsInClipboard();

    juce::PopupMenu menu;
    menu.addItem(1, "Create MIDI Clip", !isFrozen);
    menu.addSeparator();
    menu.addItem(2, "Paste", !isFrozen && hasClipboard);
    menu.addItem(3, "Select All");

    auto safeThis = juce::Component::SafePointer<TrackContentPanel>(this);

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, trackId, startTime](int result) {
        if (result == 0)
            return;

        switch (result) {
            case 1: {  // Create MIDI Clip
                if (safeThis)
                    safeThis->createMidiClipAtPosition(trackId, startTime);
                break;
            }
            case 2: {  // Paste
                auto cmd = std::make_unique<PasteClipCommand>(startTime);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));

                const auto& pastedClips = cmdPtr->getPastedClipIds();
                if (!pastedClips.empty()) {
                    std::unordered_set<ClipId> newSelection(pastedClips.begin(), pastedClips.end());
                    SelectionManager::getInstance().selectClips(newSelection);
                }
                break;
            }
            case 3: {  // Select All
                const auto& allClips = ClipManager::getInstance().getArrangementClips();
                std::unordered_set<ClipId> allClipIds;
                for (const auto& clip : allClips) {
                    allClipIds.insert(clip.id);
                }
                SelectionManager::getInstance().selectClips(allClipIds);
                break;
            }
        }
    });
}

void TrackContentPanel::timerCallback() {
    // Toggle edit cursor blink state
    editCursorBlinkVisible_ = !editCursorBlinkVisible_;

    // Bounded repaint: only the cursor strip, not the entire 65000px component
    if (timelineController && selectedTrackIndex >= 0 &&
        selectedTrackIndex < static_cast<int>(trackLanes.size())) {
        double editCursorPos = timelineController->getState().editCursorPosition;
        if (editCursorPos >= 0) {
            int cursorX = timeToPixel(editCursorPos);
            auto trackArea = getTrackLaneArea(selectedTrackIndex);
            repaint(juce::Rectangle<int>(cursorX - 3, trackArea.getY(), 6, trackArea.getHeight()));
        }
    }

    // Update cursor when modifier keys change (e.g. shift for split mode)
    if (isMouseOver(true)) {
        auto mousePos = getMouseXYRelative();
        bool shiftNow = juce::ModifierKeys::currentModifiers.isShiftDown();
        if (shiftNow != lastShiftState_) {
            lastShiftState_ = shiftNow;
            updateCursorForPosition(mousePos.x, mousePos.y, shiftNow);
        }
    }
}

void TrackContentPanel::mouseMove(const juce::MouseEvent& event) {
    updateCursorForPosition(event.x, event.y, event.mods.isShiftDown());
}

bool TrackContentPanel::isInUpperTrackZone(int y) const {
    int trackIndex = getTrackIndexAtY(y);
    if (trackIndex < 0) {
        return false;
    }

    auto trackArea = getTrackLaneArea(trackIndex);
    int trackMidY = trackArea.getY() + trackArea.getHeight() / 2;

    return y < trackMidY;
}

void TrackContentPanel::updateCursorForPosition(int x, int y, bool shiftHeld) {
    // Check track zone first
    bool inUpperZone = isInUpperTrackZone(y);

    if (inUpperZone) {
        // UPPER ZONE: Clip operations
        // Check if over a clip - clip handles its own cursor
        if (getClipComponentAt(x, y) != nullptr) {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }
        // Empty space in upper zone - crosshair for marquee selection
        if (isInSelectableArea(x, y)) {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    } else {
        // LOWER ZONE: Time selection operations
        if (isInSelectableArea(x, y)) {
            bool isLeftEdge = false;
            if (isOnSelectionEdge(x, y, isLeftEdge)) {
                // Over edge of time selection - show resize cursor
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            } else if (isOnExistingSelection(x, y)) {
                // Over existing time selection
                // Shift = split at boundaries cursor, otherwise grab hand
                // TODO: Replace CrosshairCursor with a custom closed-fist icon
                if (shiftHeld) {
                    setMouseCursor(juce::MouseCursor::CrosshairCursor);
                } else {
                    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                }
            } else {
                // Empty space - I-beam for creating time selection
                setMouseCursor(juce::MouseCursor::IBeamCursor);
            }
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }
}

// ============================================================================
// ClipManagerListener Implementation
// ============================================================================

void TrackContentPanel::clipsChanged() {
    groupExtentCacheDirty_ = true;
    rebuildClipComponents();
}

void TrackContentPanel::clipPropertyChanged(ClipId clipId) {
    groupExtentCacheDirty_ = true;
    // Find the clip component and update its position/size
    for (auto& clipComp : clipComponents_) {
        if (clipComp->getClipId() == clipId) {
            // Skip if THIS clip is the one being dragged (it manages its own bounds)
            if (clipComp->isCurrentlyDragging()) {
                break;
            }
            // Update all clip positions (updateClipComponentPositions already
            // skips dragging clips internally)
            updateClipComponentPositions();
            break;
        }
    }
}

void TrackContentPanel::clipSelectionChanged(ClipId clipId) {
    // Grab keyboard focus to ensure shortcuts work after selection changes
    grabKeyboardFocus();

    // Derive selected track from clip so edit cursor draws on the right lane
    if (clipId != INVALID_CLIP_ID) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            selectedTrackIndex = -1;
            for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
                if (visibleTrackIds_[i] == clip->trackId) {
                    selectedTrackIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Repaint to update selection visuals
    repaintVisible();
}

// ============================================================================
// Clip Management
// ============================================================================

void TrackContentPanel::rebuildClipComponents() {
    // Remove all existing clip components
    clipComponents_.clear();

    // Get only arrangement clips (timeline-based)
    const auto& clips = ClipManager::getInstance().getArrangementClips();

    // Create a component for each clip that belongs to a visible track
    for (const auto& clip : clips) {
        // Check if clip's track is visible
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip.trackId);
        if (it == visibleTrackIds_.end()) {
            continue;  // Track not visible
        }

        auto clipComp = std::make_unique<ClipComponent>(clip.id, this);

        // Set up callbacks - all clip operations go through the undo system
        clipComp->onClipMoved = [](ClipId id, double newStartTime) {
            auto cmd = std::make_unique<MoveClipCommand>(id, newStartTime);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        };

        clipComp->onClipMovedToTrack = [](ClipId id, TrackId newTrackId) {
            auto cmd = std::make_unique<MoveClipToTrackCommand>(id, newTrackId);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        };

        clipComp->onClipResized = [this](ClipId id, double newLength, bool fromStart) {
            const auto* draggedClip = ClipManager::getInstance().getClip(id);
            if (!draggedClip)
                return;
            double lengthDelta = newLength - draggedClip->length;

            const auto& selected = SelectionManager::getInstance().getSelectedClips();
            auto clipsToResize = (selected.size() > 1 && selected.count(id))
                                     ? selected
                                     : std::unordered_set<ClipId>{id};

            if (clipsToResize.size() > 1)
                UndoManager::getInstance().beginCompoundOperation("Resize Clips");

            for (auto cid : clipsToResize) {
                const auto* c = ClipManager::getInstance().getClip(cid);
                if (!c)
                    continue;
                double clipLen = juce::jmax(0.1, c->length + lengthDelta);
                auto cmd = std::make_unique<ResizeClipCommand>(cid, clipLen, fromStart, getTempo());
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }

            if (clipsToResize.size() > 1)
                UndoManager::getInstance().endCompoundOperation();
        };

        clipComp->onClipSelected = [](ClipId id) {
            SelectionManager::getInstance().selectClip(id);

            // Auto-open the appropriate editor when a clip is selected
            const auto* clip = ClipManager::getInstance().getClip(id);
            if (!clip)
                return;

            auto& panelController = daw::ui::PanelController::getInstance();

            // Open bottom panel — BottomPanel's clipSelectionChanged handles
            // the PianoRoll vs DrumGrid choice, respecting the user's preference.
            panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
            if (clip->type == ClipType::Audio) {
                panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                   daw::ui::PanelContentType::WaveformEditor);
            }
        };

        clipComp->onClipDoubleClicked = [](ClipId id) {
            // Double-click toggles the bottom panel closed (single click already opened it)
            const auto* clip = ClipManager::getInstance().getClip(id);
            if (!clip)
                return;

            auto& panelController = daw::ui::PanelController::getInstance();
            bool isCollapsed =
                panelController.getPanelState(daw::ui::PanelLocation::Bottom).collapsed;
            if (isCollapsed) {
                // If somehow collapsed, open it
                panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
                if (clip->type == ClipType::Audio) {
                    panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                       daw::ui::PanelContentType::WaveformEditor);
                }
            } else {
                // Already open - double-click closes it
                panelController.setCollapsed(daw::ui::PanelLocation::Bottom, true);
            }
        };

        clipComp->onClipSplit = [this](ClipId id, double splitTime) {
            auto cmd = std::make_unique<SplitClipCommand>(id, splitTime, getTempo());
            UndoManager::getInstance().executeCommand(std::move(cmd));

            // Get the created clip ID for selection (we need to look it up)
            // The split command stores the created ID, but we don't have access to it here
            // For now, the selection will be handled by the command or we need to refactor
        };

        // Wire up render callbacks (bubble up to MainWindow which has engine access)
        clipComp->onClipRenderRequested = [this](ClipId id) {
            if (onClipRenderRequested)
                onClipRenderRequested(id);
        };
        clipComp->onRenderTimeSelectionRequested = [this]() {
            if (onRenderTimeSelectionRequested)
                onRenderTimeSelectionRequested();
        };
        clipComp->onBounceInPlaceRequested = [this](ClipId id) {
            if (onBounceInPlaceRequested)
                onBounceInPlaceRequested(id);
        };
        clipComp->onBounceToNewTrackRequested = [this](ClipId id) {
            if (onBounceToNewTrackRequested)
                onBounceToNewTrackRequested(id);
        };

        // Wire up grid snapping
        clipComp->snapTimeToGrid = snapTimeToGrid;

        addAndMakeVisible(clipComp.get());
        clipComponents_.push_back(std::move(clipComp));
    }

    updateClipComponentPositions();
}

void TrackContentPanel::updateClipComponentPositions() {
    // Get visible horizontal range from parent viewport for culling
    int visibleLeft = 0;
    int visibleRight = getWidth();
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>()) {
        visibleLeft = viewport->getViewPositionX();
        visibleRight = visibleLeft + viewport->getViewWidth();
    }

    for (auto& clipComp : clipComponents_) {
        const auto* clip = ClipManager::getInstance().getClip(clipComp->getClipId());
        if (!clip) {
            continue;
        }

        // Skip clips that are being dragged - they manage their own position
        if (clipComp->isCurrentlyDragging()) {
            continue;
        }

        // Find the track index
        auto it = std::find(visibleTrackIds_.begin(), visibleTrackIds_.end(), clip->trackId);
        if (it == visibleTrackIds_.end()) {
            clipComp->setVisible(false);
            continue;
        }

        int trackIndex = static_cast<int>(std::distance(visibleTrackIds_.begin(), it));
        auto trackArea = getTrackLaneArea(trackIndex);

        // Calculate clip bounds in beat domain (currentZoom is ppb).
        // Always use beat values when available — avoids seconds→beats
        // floating point drift that causes position to shift with zoom.
        double startBeats =
            (clip->startBeats >= 0.0) ? clip->startBeats : clip->startTime * tempoBPM / 60.0;
        double clipBeats =
            (clip->lengthBeats > 0.0) ? clip->lengthBeats : clip->length * tempoBPM / 60.0;
        int clipX = beatsToPixel(startBeats);
        int clipWidth = static_cast<int>(std::round(clipBeats * currentZoom));

        // Cull clips entirely outside the visible viewport area
        if (clipX + clipWidth < visibleLeft || clipX > visibleRight) {
            clipComp->setVisible(false);
            continue;
        }

        // Inset from track edges
        int clipY = trackArea.getY() + 2;
        int clipHeight = trackArea.getHeight() - 4;
        int finalWidth = juce::jmax(10, clipWidth);

        auto newBounds = juce::Rectangle<int>(clipX, clipY, finalWidth, clipHeight);
        if (clipComp->getBounds() != newBounds)
            clipComp->setBounds(newBounds);
        clipComp->setVisible(true);
    }
}

// ============================================================================
// Marquee Selection
// ============================================================================

void TrackContentPanel::startMarqueeSelection(const juce::Point<int>& startPoint) {
    isMarqueeActive_ = true;
    marqueeStartPoint_ = startPoint;
    marqueeRect_ = juce::Rectangle<int>(startPoint.x, startPoint.y, 0, 0);
    marqueePreviewClips_.clear();
    currentDragType_ = DragType::Marquee;
}

void TrackContentPanel::updateMarqueeSelection(const juce::Point<int>& currentPoint) {
    if (!isMarqueeActive_) {
        return;
    }

    // Calculate marquee rectangle from start and current point
    int x1 = juce::jmin(marqueeStartPoint_.x, currentPoint.x);
    int y1 = juce::jmin(marqueeStartPoint_.y, currentPoint.y);
    int x2 = juce::jmax(marqueeStartPoint_.x, currentPoint.x);
    int y2 = juce::jmax(marqueeStartPoint_.y, currentPoint.y);

    marqueeRect_ = juce::Rectangle<int>(x1, y1, x2 - x1, y2 - y1);

    // Update highlighted clips
    updateMarqueeHighlights();
    repaintVisible();
}

void TrackContentPanel::finishMarqueeSelection(bool addToSelection) {
    if (!isMarqueeActive_) {
        return;
    }

    isMarqueeActive_ = false;

    // Get all clips in the marquee rectangle
    auto clipsInRect = getClipsInRect(marqueeRect_);

    // Only update selection if we actually captured some clips
    // This prevents accidental selection clearing from tiny marquee drags
    if (!clipsInRect.empty() || marqueeRect_.getWidth() > 10 || marqueeRect_.getHeight() > 10) {
        if (addToSelection) {
            // Add to existing selection (Shift key held)
            for (ClipId clipId : clipsInRect) {
                SelectionManager::getInstance().addClipToSelection(clipId);
            }
        } else {
            // Replace selection
            SelectionManager::getInstance().selectClips(clipsInRect);
        }

        // Auto-open the appropriate editor panel for the selected clips
        if (!clipsInRect.empty()) {
            ClipId firstId = *clipsInRect.begin();
            const auto* clip = ClipManager::getInstance().getClip(firstId);
            if (clip) {
                auto& panelController = daw::ui::PanelController::getInstance();
                panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
                if (clip->type == ClipType::Audio) {
                    panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                                       daw::ui::PanelContentType::WaveformEditor);
                }
            }
        }
    }
    // If marquee was tiny and caught nothing, preserve existing selection

    // Clear marquee preview highlights
    for (auto& clipComp : clipComponents_) {
        clipComp->setMarqueeHighlighted(false);
    }
    marqueePreviewClips_.clear();
    marqueeRect_ = juce::Rectangle<int>();

    repaintVisible();
}

std::unordered_set<ClipId> TrackContentPanel::getClipsInRect(
    const juce::Rectangle<int>& rect) const {
    std::unordered_set<ClipId> result;

    for (const auto& clipComp : clipComponents_) {
        if (clipComp->getBounds().intersects(rect)) {
            result.insert(clipComp->getClipId());
        }
    }

    return result;
}

void TrackContentPanel::paintMarqueeRect(juce::Graphics& g) {
    if (!isMarqueeActive_ || marqueeRect_.isEmpty()) {
        return;
    }

    // Semi-transparent white fill
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.fillRect(marqueeRect_);

    // White border
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.drawRect(marqueeRect_, 1);
}

void TrackContentPanel::updateMarqueeHighlights() {
    auto clipsInRect = getClipsInRect(marqueeRect_);

    // Update clip components
    for (auto& clipComp : clipComponents_) {
        bool inMarquee = clipsInRect.find(clipComp->getClipId()) != clipsInRect.end();
        clipComp->setMarqueeHighlighted(inMarquee);
    }

    marqueePreviewClips_ = clipsInRect;
}

bool TrackContentPanel::checkIfMarqueeNeeded(const juce::Point<int>& currentPoint) const {
    // Create a rectangle from drag start to current point
    int x1 = juce::jmin(mouseDownX, currentPoint.x);
    int y1 = juce::jmin(mouseDownY, currentPoint.y);
    int x2 = juce::jmax(mouseDownX, currentPoint.x);
    int y2 = juce::jmax(mouseDownY, currentPoint.y);

    // Ensure minimum dimensions for intersection check
    // (a zero-height rect won't intersect anything)
    int width = juce::jmax(1, x2 - x1);
    int height = juce::jmax(1, y2 - y1);

    // Expand vertically to cover the track the user clicked in
    // This ensures horizontal drags still detect clips
    int trackIndex = getTrackIndexAtY(mouseDownY);
    if (trackIndex >= 0) {
        auto trackArea = getTrackLaneArea(trackIndex);
        y1 = trackArea.getY();
        height = trackArea.getHeight();
    }

    juce::Rectangle<int> dragRect(x1, y1, width, height);

    // Check if any clips are intersected by the drag rectangle
    for (const auto& clipComp : clipComponents_) {
        if (clipComp->getBounds().intersects(dragRect)) {
            return true;  // Marquee selection needed
        }
    }

    return false;  // Time selection (no clips crossed)
}

// ============================================================================
// Keyboard Handling
// ============================================================================

bool TrackContentPanel::keyPressed(const juce::KeyPress& key) {
    auto& selectionManager = SelectionManager::getInstance();

    // Note: Cmd+Z / Cmd+Shift+Z (undo/redo) are handled globally by
    // MainComponent's ApplicationCommandManager key mappings.

    // Cmd/Ctrl+A: Select all clips
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        std::unordered_set<ClipId> allClips;
        for (const auto& clipComp : clipComponents_) {
            allClips.insert(clipComp->getClipId());
        }
        selectionManager.selectClips(allClips);
        return true;
    }

    // Escape: Clear selection
    if (key == juce::KeyPress::escapeKey) {
        selectionManager.clearSelection();
        if (isMarqueeActive_) {
            isMarqueeActive_ = false;
            marqueePreviewClips_.clear();
            for (auto& clipComp : clipComponents_) {
                clipComp->setMarqueeHighlighted(false);
            }
            repaintVisible();
        }
        if (isMovingMultipleClips_) {
            cancelMultiClipDrag();
        }
        return true;
    }

    // Delete/Backspace: time-selection delete takes priority, then selected clips
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (timelineController && timelineController->getState().selection.isVisuallyActive()) {
            const auto& state = timelineController->getState();
            const auto& sel = state.selection;

            // Resolve track IDs from selection indices
            std::vector<TrackId> trackIds;
            if (sel.trackIndices.empty()) {
                trackIds = visibleTrackIds_;
            } else {
                for (int idx : sel.trackIndices) {
                    if (idx >= 0 && idx < static_cast<int>(visibleTrackIds_.size()))
                        trackIds.push_back(visibleTrackIds_[idx]);
                }
            }

            auto cmd = std::make_unique<DeleteTimeSelectionCommand>(sel.startTime, sel.endTime,
                                                                    trackIds, state.tempo.bpm);
            UndoManager::getInstance().executeCommand(std::move(cmd));

            timelineController->dispatch(SetEditCursorEvent{sel.startTime});
            timelineController->dispatch(ClearTimeSelectionEvent{});
            return true;
        }

        const auto& selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Copy to vector since we're modifying during iteration
            std::vector<ClipId> clipsToDelete(selectedClips.begin(), selectedClips.end());

            // Use compound operation to group all deletes into single undo step
            if (clipsToDelete.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Delete Clips");
            }

            for (ClipId clipId : clipsToDelete) {
                auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                UndoManager::getInstance().executeCommand(std::move(cmd));
            }

            if (clipsToDelete.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            selectionManager.clearSelection();
            grabKeyboardFocus();  // Keep focus for subsequent operations
            return true;
        }
    }

    // Cmd/Ctrl+D: Duplicate selected clips
    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) {
        const auto& selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Use compound operation to group all duplicates into single undo step
            if (selectedClips.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Duplicate Clips");
            }

            std::vector<std::unique_ptr<DuplicateClipCommand>> commands;
            for (ClipId clipId : selectedClips) {
                auto cmd = std::make_unique<DuplicateClipCommand>(clipId);
                commands.push_back(std::move(cmd));
            }

            // Execute commands and collect new IDs
            std::unordered_set<ClipId> newClipIds;
            for (auto& cmd : commands) {
                DuplicateClipCommand* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));
                ClipId newId = cmdPtr->getDuplicatedClipId();
                if (newId != INVALID_CLIP_ID) {
                    newClipIds.insert(newId);
                }
            }

            if (selectedClips.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            // Select the new duplicates
            if (!newClipIds.empty()) {
                selectionManager.selectClips(newClipIds);
            }
            grabKeyboardFocus();  // Keep focus for subsequent operations
            return true;
        }
    }

    // NOTE: Cmd+C, Cmd+V, Cmd+X are now handled by ApplicationCommandManager in MainWindow
    // These old handlers have been removed to prevent double-handling

    // B: Blade - Split clips at edit cursor position
    // Works on selected clips if they contain the cursor, otherwise splits any clip under cursor
    if (key == juce::KeyPress('b')) {
        if (!timelineController) {
            return false;
        }

        const auto& state = timelineController->getState();
        double splitTime = state.editCursorPosition;

        // Can't split if edit cursor is not set
        if (splitTime < 0) {
            return false;
        }

        // Collect clips to split
        std::vector<ClipId> clipsToSplit;
        const auto& selectedClips = selectionManager.getSelectedClips();

        // First, check if any selected clips contain the edit cursor
        for (ClipId clipId : selectedClips) {
            const auto* clip = ClipManager::getInstance().getClip(clipId);
            if (clip && clip->containsTime(splitTime)) {
                clipsToSplit.push_back(clipId);
            }
        }

        // If no selected clips at cursor, find ANY arrangement clip that contains the cursor
        if (clipsToSplit.empty()) {
            const auto& allClips = ClipManager::getInstance().getArrangementClips();
            for (const auto& clip : allClips) {
                if (clip.containsTime(splitTime)) {
                    clipsToSplit.push_back(clip.id);
                }
            }
        }

        // Nothing to split
        if (clipsToSplit.empty()) {
            return false;
        }

        // Use compound operation to group all splits into single undo step
        if (clipsToSplit.size() > 1) {
            UndoManager::getInstance().beginCompoundOperation("Split Clips");
        }

        // Split each clip through the undo system
        for (ClipId clipId : clipsToSplit) {
            auto cmd = std::make_unique<SplitClipCommand>(clipId, splitTime, getTempo());
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        if (clipsToSplit.size() > 1) {
            UndoManager::getInstance().endCompoundOperation();
        }

        return true;
    }

    // Forward unhandled keys up the parent chain for command manager processing
    // Walk up past the Viewport to reach MainView/MainComponent where ApplicationCommandManager
    // lives
    auto* parent = getParentComponent();
    while (parent != nullptr) {
        if (parent->keyPressed(key)) {
            return true;
        }
        parent = parent->getParentComponent();
    }

    return false;  // Key not handled
}

// ============================================================================
// AutomationManagerListener Implementation
// ============================================================================

void TrackContentPanel::syncAutomationLaneVisibility() {
    visibleAutomationLanes_.clear();

    auto& manager = AutomationManager::getInstance();
    if (!manager.isGlobalLaneVisibilityEnabled())
        return;  // Global override: treat all lanes as hidden

    for (auto trackId : visibleTrackIds_) {
        auto laneIds = manager.getLanesForTrack(trackId);
        for (auto laneId : laneIds) {
            const auto* lane = manager.getLane(laneId);
            if (lane && lane->visible) {
                visibleAutomationLanes_[trackId].push_back(laneId);
            }
        }
    }
}

void TrackContentPanel::automationLanesChanged() {
    syncAutomationLaneVisibility();
    rebuildAutomationLaneComponents();
    updateClipComponentPositions();
    resized();
    repaintVisible();
}

void TrackContentPanel::automationLanePropertyChanged(AutomationLaneId /*laneId*/) {
    // Check if visibility changed by comparing with current state
    auto oldVisibleLanes = visibleAutomationLanes_;
    syncAutomationLaneVisibility();

    bool visibilityChanged = (oldVisibleLanes != visibleAutomationLanes_);

    if (visibilityChanged) {
        // Visibility changed - need to rebuild components
        rebuildAutomationLaneComponents();
    } else {
        // Just a property change (like height) - update positions only
        updateAutomationLanePositions();
    }

    updateClipComponentPositions();
    resized();
    repaintVisible();
}

// ============================================================================
// Automation Lane Management
// ============================================================================

void TrackContentPanel::showAutomationLane(TrackId trackId, AutomationLaneId laneId) {
    juce::ignoreUnused(trackId);
    // Set visibility through AutomationManager - listener will sync and rebuild
    AutomationManager::getInstance().setLaneVisible(laneId, true);
}

void TrackContentPanel::hideAutomationLane(TrackId trackId, AutomationLaneId laneId) {
    juce::ignoreUnused(trackId);
    // Set visibility through AutomationManager - listener will sync and rebuild
    AutomationManager::getInstance().setLaneVisible(laneId, false);
}

void TrackContentPanel::toggleAutomationLane(TrackId trackId, AutomationLaneId laneId) {
    if (isAutomationLaneVisible(trackId, laneId)) {
        hideAutomationLane(trackId, laneId);
    } else {
        showAutomationLane(trackId, laneId);
    }
}

bool TrackContentPanel::isAutomationLaneVisible(TrackId trackId, AutomationLaneId laneId) const {
    auto it = visibleAutomationLanes_.find(trackId);
    if (it != visibleAutomationLanes_.end()) {
        const auto& lanes = it->second;
        return std::find(lanes.begin(), lanes.end(), laneId) != lanes.end();
    }
    return false;
}

int TrackContentPanel::getTrackTotalHeight(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackLanes.size())) {
        return 0;
    }

    // Base track height
    int totalHeight = static_cast<int>(trackLanes[trackIndex]->height * verticalZoom);

    // Add visible automation lanes
    if (trackIndex < static_cast<int>(visibleTrackIds_.size())) {
        TrackId trackId = visibleTrackIds_[trackIndex];
        totalHeight += getVisibleAutomationLanesHeight(trackId);
    }

    return totalHeight;
}

int TrackContentPanel::getVisibleAutomationLanesHeight(TrackId trackId) const {
    int totalHeight = 0;

    auto it = visibleAutomationLanes_.find(trackId);
    if (it != visibleAutomationLanes_.end()) {
        auto& manager = AutomationManager::getInstance();
        for (auto laneId : it->second) {
            const auto* lane = manager.getLane(laneId);
            if (lane && lane->visible) {
                int laneHeight = lane->expanded ? (AutomationLaneComponent::HEADER_HEIGHT +
                                                   static_cast<int>(lane->height * verticalZoom) +
                                                   AutomationLaneComponent::RESIZE_HANDLE_HEIGHT)
                                                : AutomationLaneComponent::HEADER_HEIGHT;
                totalHeight += laneHeight;
            }
        }
    }

    return totalHeight;
}

void TrackContentPanel::rebuildAutomationLaneComponents() {
    // Remove old lane components
    for (auto& entry : automationLaneComponents_)
        if (entry.component)
            removeChildComponent(entry.component.get());
    automationLaneComponents_.clear();

    auto& manager = AutomationManager::getInstance();

    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        TrackId trackId = visibleTrackIds_[i];

        auto it = visibleAutomationLanes_.find(trackId);
        if (it == visibleAutomationLanes_.end())
            continue;

        for (auto laneId : it->second) {
            const auto* lane = manager.getLane(laneId);
            if (!lane || !lane->visible)
                continue;

            AutomationLaneEntry entry;
            entry.trackId = trackId;
            entry.laneId = laneId;
            entry.component = std::make_unique<AutomationLaneComponent>(laneId);
            entry.component->setPixelsPerSecond(currentZoom * tempoBPM / 60.0);
            entry.component->setPixelsPerBeat(currentZoom);
            entry.component->setTempoBPM(tempoBPM);
            entry.component->snapTimeToGrid = snapBeatsToGrid;
            entry.component->getGridSpacingBeats = getGridSpacingBeats;

            entry.component->onHeightChanged = [this](AutomationLaneId, int) {
                updateAutomationLanePositions();
                updateClipComponentPositions();
                resized();
                repaintVisible();
            };

            addAndMakeVisible(*entry.component);
            automationLaneComponents_.push_back(std::move(entry));
        }
    }
    updateAutomationLanePositions();
}

void TrackContentPanel::updateAutomationLanePositions() {
    auto& manager = AutomationManager::getInstance();

    // Position each lane component directly under its track row.
    // Lanes stack inline; the outer content panel viewport handles scrolling.
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        TrackId trackId = visibleTrackIds_[i];
        int trackIndex = static_cast<int>(i);
        int y = getTrackYPosition(trackIndex) +
                static_cast<int>(trackLanes[trackIndex]->height * verticalZoom);

        auto laneIt = visibleAutomationLanes_.find(trackId);
        if (laneIt == visibleAutomationLanes_.end())
            continue;

        for (auto laneId : laneIt->second) {
            const auto* lane = manager.getLane(laneId);
            if (!lane || !lane->visible)
                continue;

            int height = lane->expanded ? (AutomationLaneComponent::HEADER_HEIGHT +
                                           static_cast<int>(lane->height * verticalZoom) +
                                           AutomationLaneComponent::RESIZE_HANDLE_HEIGHT)
                                        : AutomationLaneComponent::HEADER_HEIGHT;

            for (auto& entry : automationLaneComponents_) {
                if (entry.trackId == trackId && entry.laneId == laneId) {
                    entry.component->setBounds(0, y, getWidth(), height);
                    entry.component->setPixelsPerSecond(currentZoom * tempoBPM / 60.0);
                    entry.component->setPixelsPerBeat(currentZoom);
                    entry.component->setTempoBPM(tempoBPM);
                    break;
                }
            }

            y += height;
        }
    }
}

// =============================================================================
// File Drag-and-Drop Implementation
// =============================================================================

bool TrackContentPanel::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& file : files) {
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".aiff") ||
            file.endsWithIgnoreCase(".aif") || file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".ogg") || file.endsWithIgnoreCase(".flac") ||
            file.endsWithIgnoreCase(".mid") || file.endsWithIgnoreCase(".midi")) {
            return true;
        }
    }
    return false;
}

void TrackContentPanel::fileDragEnter(const juce::StringArray& /*files*/, int x, int y) {
    dropInsertTime_ = juce::jmax(0.0, pixelToTime(x));
    if (snapTimeToGrid) {
        dropInsertTime_ = snapTimeToGrid(dropInsertTime_);
    }
    dropTargetTrackIndex_ = getTrackIndexAtY(y);
    showDropIndicator_ = true;
    repaintVisible();
}

void TrackContentPanel::fileDragMove(const juce::StringArray& /*files*/, int x, int y) {
    dropInsertTime_ = juce::jmax(0.0, pixelToTime(x));
    if (snapTimeToGrid) {
        dropInsertTime_ = snapTimeToGrid(dropInsertTime_);
    }
    dropTargetTrackIndex_ = getTrackIndexAtY(y);
    repaintVisible();
}

void TrackContentPanel::fileDragExit(const juce::StringArray& /*files*/) {
    showDropIndicator_ = false;
    repaintVisible();
}

void TrackContentPanel::filesDropped(const juce::StringArray& files, int x, int y) {
    showDropIndicator_ = false;
    repaintVisible();

    // Determine drop position (clamp to timeline start, snap if enabled)
    double dropTime = juce::jmax(0.0, pixelToTime(x));
    if (snapTimeToGrid) {
        dropTime = snapTimeToGrid(dropTime);
    }
    int trackIndex = getTrackIndexAtY(y);

    TrackId targetTrackId = INVALID_TRACK_ID;

    if (trackIndex >= 0 && trackIndex < static_cast<int>(visibleTrackIds_.size())) {
        // Dropped on an existing track
        targetTrackId = visibleTrackIds_[trackIndex];
        auto* track = TrackManager::getInstance().getTrack(targetTrackId);
        if (!track)
            return;

        // Block drops on group/aux tracks (no clip timeline)
        if (track->type == TrackType::Group || track->type == TrackType::Aux) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Drop Failed",
                "Files cannot be dropped on group or aux tracks.");
            return;
        }

        // Block drops on tracks with a DrumGrid plugin
        for (const auto& element : track->chainElements) {
            if (isDevice(element) && getDevice(element).pluginId.containsIgnoreCase("drumgrid")) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Drop Failed",
                    "Files cannot be dropped on Drum Grid tracks.");
                return;
            }
        }
    }
    // If targetTrackId is still INVALID, we'll create a new track below

    // Separate audio and MIDI files
    juce::StringArray audioFiles, midiFiles;
    for (const auto& filePath : files) {
        if (filePath.endsWithIgnoreCase(".mid") || filePath.endsWithIgnoreCase(".midi"))
            midiFiles.add(filePath);
        else if (filePath.endsWithIgnoreCase(".wav") || filePath.endsWithIgnoreCase(".aiff") ||
                 filePath.endsWithIgnoreCase(".aif") || filePath.endsWithIgnoreCase(".mp3") ||
                 filePath.endsWithIgnoreCase(".ogg") || filePath.endsWithIgnoreCase(".flac"))
            audioFiles.add(filePath);
    }

    int importedCount = 0;

    // Import audio files
    if (!audioFiles.isEmpty()) {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        double currentTime = dropTime;
        CompoundOperationScope scope("Import Audio Files");

        for (const auto& filePath : audioFiles) {
            juce::File audioFile(filePath);
            if (!audioFile.existsAsFile())
                continue;

            if (targetTrackId == INVALID_TRACK_ID) {
                juce::String trackName = audioFile.getFileNameWithoutExtension();
                auto createTrackCmd =
                    std::make_unique<CreateTrackCommand>(TrackType::Audio, trackName);
                auto* createTrackPtr = createTrackCmd.get();
                UndoManager::getInstance().executeCommand(std::move(createTrackCmd));
                targetTrackId = createTrackPtr->getCreatedTrackId();
                if (targetTrackId == INVALID_TRACK_ID)
                    return;
                TrackManager::getInstance().setSelectedTrack(targetTrackId);
            }

            double fileDuration = 4.0;
            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
                    formatManager.createReaderFor(audioFile))) {
                fileDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            }

            auto cmd = std::make_unique<CreateClipCommand>(
                ClipType::Audio, targetTrackId, currentTime, fileDuration, filePath.toStdString());
            UndoManager::getInstance().executeCommand(std::move(cmd));

            currentTime += fileDuration + 0.5;
            importedCount++;
        }
    }

    // Import MIDI files
    if (!midiFiles.isEmpty()) {
        double projectTempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        if (projectTempo <= 0.0)
            projectTempo = 120.0;

        CompoundOperationScope scope("Import MIDI Files");

        for (const auto& filePath : midiFiles) {
            juce::File midiFile(filePath);
            if (!midiFile.existsAsFile())
                continue;

            // Parse MIDI file using Tracktion Engine
            juce::OwnedArray<tracktion::MidiList> lists;
            juce::Array<tracktion::BeatPosition> tempoChangeBeatNumbers;
            juce::Array<double> bpms;
            juce::Array<int> numerators, denominators;
            tracktion::BeatDuration songLength;

            if (!tracktion::MidiList::readSeparateTracksFromFile(
                    midiFile, lists, tempoChangeBeatNumbers, bpms, numerators, denominators,
                    songLength, false)) {
                continue;
            }

            if (lists.isEmpty())
                continue;

            // For single-track files dropped on an existing track, reuse that track
            // For multi-track files or drops on empty area, create new tracks
            bool createNewTracks = (lists.size() > 1) || (targetTrackId == INVALID_TRACK_ID);

            for (int listIdx = 0; listIdx < lists.size(); ++listIdx) {
                auto* list = lists[listIdx];
                if (list->getNumNotes() == 0 && list->getNumControllerEvents() == 0)
                    continue;

                // Compute clip length in beats, round up to next bar
                double lengthBeats = songLength.inBeats();
                if (lengthBeats <= 0.0)
                    lengthBeats = list->getLastBeatNumber().inBeats();
                if (lengthBeats <= 0.0)
                    lengthBeats = 4.0;

                // Round up to whole bars
                int beatsPerBar = 4;
                if (!numerators.isEmpty() && numerators[0] > 0)
                    beatsPerBar = numerators[0];
                double bars = std::ceil(lengthBeats / beatsPerBar);
                lengthBeats = bars * beatsPerBar;

                double clipDuration = (lengthBeats * 60.0) / projectTempo;

                // Create track if needed
                TrackId clipTrackId = targetTrackId;
                if (createNewTracks) {
                    juce::String trackName = list->getImportedFileName();
                    if (trackName.isEmpty())
                        trackName = midiFile.getFileNameWithoutExtension();
                    if (lists.size() > 1)
                        trackName += " " + juce::String(listIdx + 1);

                    auto createTrackCmd =
                        std::make_unique<CreateTrackCommand>(TrackType::Audio, trackName);
                    auto* createTrackPtr = createTrackCmd.get();
                    UndoManager::getInstance().executeCommand(std::move(createTrackCmd));
                    clipTrackId = createTrackPtr->getCreatedTrackId();
                    if (clipTrackId == INVALID_TRACK_ID)
                        continue;
                }

                // Create MIDI clip
                auto cmd = std::make_unique<CreateClipCommand>(ClipType::MIDI, clipTrackId,
                                                               dropTime, clipDuration);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));

                ClipId clipId = cmdPtr->getCreatedClipId();
                if (clipId == INVALID_CLIP_ID)
                    continue;

                auto* clip = ClipManager::getInstance().getClip(clipId);
                if (!clip)
                    continue;

                // Populate MIDI notes
                for (auto* note : list->getNotes()) {
                    MidiNote midiNote;
                    midiNote.noteNumber = note->getNoteNumber();
                    midiNote.velocity = note->getVelocity();
                    midiNote.startBeat = note->getStartBeat().inBeats();
                    midiNote.lengthBeats = note->getLengthBeats().inBeats();
                    clip->midiNotes.push_back(midiNote);
                }

                // Populate CC data
                for (int i = 0; i < list->getNumControllerEvents(); ++i) {
                    auto* event = list->getControllerEvent(i);
                    int type = event->getType();

                    if (type == tracktion::MidiControllerEvent::pitchWheelType) {
                        MidiPitchBendData pb;
                        pb.value = event->getControllerValue();
                        pb.beatPosition = event->getBeatPosition().inBeats();
                        clip->midiPitchBendData.push_back(pb);
                    } else if (type >= 0 && type <= 127) {
                        MidiCCData cc;
                        cc.controller = type;
                        cc.value = event->getControllerValue();
                        cc.beatPosition = event->getBeatPosition().inBeats();
                        clip->midiCCData.push_back(cc);
                    }
                }

                // Set beat-based length and name from MIDI track/file
                clip->lengthBeats = lengthBeats;
                clip->startBeats = (dropTime * projectTempo) / 60.0;
                auto clipName = list->getImportedFileName();
                if (clipName.isEmpty())
                    clipName = midiFile.getFileNameWithoutExtension();
                clip->name = clipName;

                // Extract chord markers from MIDI marker meta events (type 6)
                // Format: "CHORD:name:lengthBeats"
                {
                    juce::FileInputStream fis(midiFile);
                    if (fis.openedOk()) {
                        juce::MidiFile rawMidi;
                        rawMidi.readFrom(fis);
                        int ticksPerQN = rawMidi.getTimeFormat();
                        if (ticksPerQN > 0) {
                            for (int t = 0; t < rawMidi.getNumTracks(); ++t) {
                                auto* track = rawMidi.getTrack(t);
                                if (!track)
                                    continue;
                                for (int e = 0; e < track->getNumEvents(); ++e) {
                                    auto& msg = track->getEventPointer(e)->message;
                                    if (msg.isTextMetaEvent() && msg.getMetaEventType() == 6) {
                                        auto text = msg.getTextFromTextMetaEvent();
                                        if (text.startsWith("CHORD:")) {
                                            auto parts = juce::StringArray::fromTokens(
                                                text.substring(6), ":", "");
                                            if (parts.size() >= 2) {
                                                ClipInfo::ChordAnnotation ann;
                                                ann.beatPosition = msg.getTimeStamp() / ticksPerQN;
                                                // Last token is length; rest is chord name
                                                ann.lengthBeats =
                                                    parts[parts.size() - 1].getDoubleValue();
                                                parts.remove(parts.size() - 1);
                                                ann.chordName = parts.joinIntoString(":");
                                                if (ann.lengthBeats <= 0.0)
                                                    ann.lengthBeats = 4.0;
                                                clip->chordAnnotations.push_back(ann);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId);
                importedCount++;
            }
        }
    }

    if (importedCount > 0) {
        DBG("TrackContentPanel: Imported " << importedCount << " files");
    }
}

// =============================================================================
// Plugin Drag-and-Drop Implementation (DragAndDropTarget)
// =============================================================================

bool TrackContentPanel::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        return obj->getProperty("type").toString() == "plugin";
    }
    return false;
}

void TrackContentPanel::itemDragEnter(const SourceDetails& details) {
    showPluginDropOverlay_ = true;
    pluginDropTrackIndex_ = getTrackIndexAtY(details.localPosition.y);
    repaintVisible();
}

void TrackContentPanel::itemDragMove(const SourceDetails& details) {
    int prev = pluginDropTrackIndex_;
    pluginDropTrackIndex_ = getTrackIndexAtY(details.localPosition.y);
    if (pluginDropTrackIndex_ != prev)
        repaintVisible();
}

void TrackContentPanel::itemDragExit(const SourceDetails& /*details*/) {
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    repaintVisible();
}

void TrackContentPanel::itemDropped(const SourceDetails& details) {
    showPluginDropOverlay_ = false;
    pluginDropTrackIndex_ = -1;
    repaintVisible();

    if (auto* obj = details.description.getDynamicObject()) {
        auto device = TrackManager::deviceInfoFromPluginObject(*obj);
        TrackType trackType = TrackType::Audio;
        juce::String pluginName = obj->getProperty("name").toString();
        auto cmd = std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }
}

}  // namespace magda
