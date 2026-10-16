#include "WaveformEditorContent.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include "../../state/TimelineController.hpp"
#include "../../themes/ActiveTheme.hpp"
#include "../../themes/CursorManager.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"
#include "audio/AudioThumbnailManager.hpp"
#include "audio/CompService.hpp"
#include "audio/WarpMarkerManager.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/Config.hpp"
#include "core/GestureRouter.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "core/WarpMarkerCommands.hpp"
#include "ui/components/timeline/PlayheadStyle.hpp"

namespace magda::daw::ui {

namespace {
// Route clip timeline-seconds through the position-aware tempo facade when
// wired (message thread); fall back to the constant-tempo bpm before injection.
double facadeTimelineStart(const magda::ClipInfo& c, double bpm) {
    if (auto* tc = magda::TimelineController::getCurrent(); tc && tc->tempoMap())
        return c.getTimelineStart(*tc->tempoMap());
    return c.getTimelineStart(bpm);
}
double facadeTimelineLength(const magda::ClipInfo& c, double bpm) {
    if (auto* tc = magda::TimelineController::getCurrent(); tc && tc->tempoMap())
        return c.getTimelineLength(*tc->tempoMap());
    return c.getTimelineLength(bpm);
}
double facadeTimelineEnd(const magda::ClipInfo& c, double bpm) {
    if (auto* tc = magda::TimelineController::getCurrent(); tc && tc->tempoMap())
        return c.getTimelineEnd(*tc->tempoMap());
    return c.getTimelineEnd(bpm);
}
}  // namespace

// ============================================================================
// ScrollNotifyingViewport - Custom viewport that notifies on scroll
// ============================================================================

class WaveformEditorContent::ScrollNotifyingViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;
    juce::Component* timeRulerToRepaint = nullptr;

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        if (onScrolled) {
            onScrolled(getViewPositionX(), getViewPositionY());
        }
        if (timeRulerToRepaint) {
            timeRulerToRepaint->repaint();
        }
    }

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override {
        juce::Viewport::scrollBarMoved(scrollBar, newRangeStart);
        if (timeRulerToRepaint) {
            timeRulerToRepaint->repaint();
        }
    }
};

// ============================================================================
// PlayheadOverlay - Renders playhead over the waveform viewport
// ============================================================================

class WaveformEditorContent::PlayheadOverlay : public juce::Component {
  public:
    explicit PlayheadOverlay(WaveformEditorContent& owner) : owner_(owner) {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        const auto playhead = owner_.playheadDisplayPositions().playback;
        if (!playhead)
            return;
        const int x = owner_.displayPositionToX(*playhead);
        if (x >= 0 && x < getWidth())
            paintPlayheadLine(g, static_cast<float>(x), 0.0f, static_cast<float>(getHeight()));
    }

  private:
    WaveformEditorContent& owner_;
};

int WaveformEditorContent::displayPositionToX(double displayPosition) const {
    return static_cast<int>(displayPosition * horizontalZoom_) + GRID_LEFT_PADDING -
           virtualScrollX_;
}

WaveformEditorContent::PlayheadPositions WaveformEditorContent::playheadDisplayPositions() const {
    PlayheadPositions positions;
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (clip == nullptr)
        return positions;

    const auto& di = cachedDisplayInfo_;
    const double projectBpm = timeRuler_ ? timeRuler_->getTempo() : 120.0;
    const double clipStart = facadeTimelineStart(*clip, projectBpm);
    const double clipEnd = facadeTimelineEnd(*clip, projectBpm);

    // The editor shows source file content, so arrangement time maps to a source position and
    // cursors show only while the arrangement playhead is inside this clip.
    auto timelineDeltaToSourceDelta = [&](double timelineDelta) {
        double sourceDuration = magda::audioEventRef(*clip).sourceDurationSeconds();
        if (magda::audioEventRef(*clip).autoTempo &&
            magda::audioEventRef(*clip).interpTotalBeats > 0.0 && sourceDuration > 0.0 &&
            projectBpm > 0.0) {
            double projectBeats = timelineDelta * projectBpm / 60.0;
            return projectBeats * sourceDuration / magda::audioEventRef(*clip).interpTotalBeats;
        }
        return di.timelineToSource(timelineDelta);
    };
    auto arrangementToDisplay = [&](double arrangementTime) {
        double sourcePos = magda::audioEventRef(*clip).anchorSeconds() +
                           timelineDeltaToSourceDelta(arrangementTime - clipStart);
        return di.sourceToTimeline(sourcePos);
    };

    if (cachedEditPosition_ >= clipStart && cachedEditPosition_ <= clipEnd)
        positions.edit = arrangementToDisplay(cachedEditPosition_);

    if (!cachedIsPlaying_)
        return positions;

    // Session mode: each clip owns its playhead via ClipInfo::sessionPlayheadPos
    if (clip->sessionPlayheadPos >= 0.0) {
        positions.playback = di.sessionPlayheadToDisplayPosition(clip->sessionPlayheadPos);
        return positions;
    }

    const double playPos = cachedPlaybackPosition_;
    if (playPos < clipStart || playPos > clipEnd)
        return positions;

    // Wrap the playhead inside the loop region when looping is enabled
    if (di.isLooped() && di.loopLengthSeconds > 0.0) {
        double wrapped = std::fmod(di.loopOffset + timelineDeltaToSourceDelta(playPos - clipStart),
                                   di.loopRegionLengthSource);
        if (wrapped < 0.0)
            wrapped += di.loopRegionLengthSource;
        positions.playback = di.sourceToTimeline(di.loopRegionStartSource + wrapped);
        return positions;
    }

    positions.playback = arrangementToDisplay(playPos);
    return positions;
}

void WaveformEditorContent::refreshPlayhead() {
    const auto positions = playheadDisplayPositions();
    if (timeRuler_) {
        timeRuler_->setPlayheadPosition(positions.playback.value_or(-1.0));
        timeRuler_->setPlayheadHandlePosition(positions.edit.value_or(-1.0));
    }
    if (playheadOverlay_)
        playheadOverlay_->repaint();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WaveformEditorContent::WaveformEditorContent() {
    setName("WaveformEditor");

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

    // Register as UndoManager listener (for warp marker refresh on undo/redo)
    magda::UndoManager::getInstance().addListener(this);

    // Refresh transients on a callback when the detector recomputes them.
    magda::AudioThumbnailManager::getInstance().addTransientCacheListener(this);

    // Create time ruler
    timeRuler_ = std::make_unique<magda::TimeRuler>();
    timeRuler_->setDisplayMode(magda::TimeRuler::DisplayMode::BarsBeats);
    timeRuler_->setGestureContext(magda::GestureContext::Waveform);
    timeRuler_->setRelativeMode(relativeTimeMode_);
    timeRuler_->setLeftPadding(GRID_LEFT_PADDING);
    timeRuler_->setCompact(true);

    // Wire up ruler zoom callback — ruler works in ppb, we work in pps
    timeRuler_->onZoomChanged = [this](double newRulerZoom, double /*anchorTime*/,
                                       int anchorScreenX) {
        double bpm = 120.0;
        auto* controller = magda::TimelineController::getCurrent();
        if (controller)
            bpm = controller->getState().tempo.bpm;

        double newZoom = newRulerZoom * bpm / 60.0;
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);
        if (newZoom != horizontalZoom_) {
            int anchorX = anchorScreenX - viewport_->getX();
            performAnchorPointZoom(newZoom / horizontalZoom_, anchorX);
        }
    };

    // Wire up ruler scroll callback
    timeRuler_->onScrollRequested = [this](int deltaX) {
        setVirtualScrollX(virtualScrollX_ + deltaX);
    };

    // Double-click on loop strip → zoom to loop region
    timeRuler_->onZoomToLoopRequested = [this](double startTime, double endTime) {
        zoomToTimeRange(startTime, endTime);
    };

    // A click on the ruler's ticks moves the arrangement playhead to the audio clicked (#3018).
    timeRuler_->onPlayheadPositionClicked = [this](double time, bool /*bypassSnap*/) {
        auto* controller = magda::TimelineController::getCurrent();
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (controller == nullptr || clip == nullptr || clip->view != magda::ClipView::Arrangement)
            return;

        const auto& state = controller->getState();
        const double bpm = isValidBpm(state.tempo.bpm) ? state.tempo.bpm : DEFAULT_BPM;
        // A reversed clip is already drawn in playing order, so read the ruler unmirrored.
        const auto& di = cachedDisplayInfo_;
        const double sourceSeconds = di.sourceFileStart + di.timelineToSource(time);
        const auto beat = magda::ClipOperations::timelineBeatForSourceSeconds(
            *clip, sourceSeconds, state.playhead.getCurrentPositionBeats(), bpm);
        if (beat)
            controller->dispatch(magda::SetPlayheadPositionBeatsEvent{juce::jmax(0.0, *beat)});
    };

    auto commitLoopFromDisplay = [this](double displayStart, double displayEnd) {
        if (editingClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip || !clip->loopEnabled)
            return;

        auto* controller = magda::TimelineController::getCurrent();
        double bpm = controller ? controller->getState().tempo.bpm : magda::DEFAULT_BPM;
        bpm = magda::isValidBpm(bpm) ? bpm : magda::DEFAULT_BPM;

        // displayStart/displayEnd are in timeline seconds (from srcToTimeline in ClipDisplayInfo).
        // Reverse the transform to get source seconds.
        auto timelineToSrc = [&](double t) -> double {
            if (magda::audioEventRef(*clip).autoTempo &&
                magda::audioEventRef(*clip).loopLengthSeconds() > 0.0 &&
                magda::audioEventRef(*clip).loopLengthBeats() > 0.0) {
                return t * magda::audioEventRef(*clip).loopLengthSeconds() /
                       (magda::audioEventRef(*clip).loopLengthBeats() * 60.0 / bpm);
            }
            return (magda::audioEventRef(*clip).speedRatio > 0.0)
                       ? t * magda::audioEventRef(*clip).speedRatio
                       : 0.0;
        };

        double newLoopStart = timelineToSrc(displayStart);
        const double displayLength = displayEnd - displayStart;
        const double newLoopLength = timelineToSrc(displayLength);
        const bool movedWithoutResizing =
            std::abs(displayLength - cachedDisplayInfo_.loopLengthSeconds) <= 1.0e-6;

        if (movedWithoutResizing) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::MoveClipLoopRegionCommand>(editingClipId_, newLoopStart));
        } else if (magda::audioEventRef(*clip).autoTempo &&
                   magda::audioEventRef(*clip).hasInterpretedBpm()) {
            const double newLoopLengthBeats =
                newLoopLength * magda::audioEventRef(*clip).interpBpm / 60.0;
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMusicalClipLoopRangeCommand>(
                    editingClipId_, newLoopStart, newLoopLengthBeats));
        } else {
            // Free playback edits a source region. Tagging its ruler beats as
            // musical would change the selected audio immediately when source and project BPM
            // differ.
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipLoopRangeCommand>(editingClipId_, newLoopStart,
                                                                 newLoopLength, bpm));
        }
    };

    // Push every drag tick through to ClipManager so the looped audio reflects the new region
    // immediately.
    timeRuler_->onLoopRegionChanged = commitLoopFromDisplay;

    addAndMakeVisible(timeRuler_.get());

    propertiesToggle_ = std::make_unique<magda::SvgButton>(
        "PropertiesToggle", BinaryData::right_close_svg, BinaryData::right_close_svgSize);
    propertiesToggle_->setTooltip("Toggle properties panel");
    magda::EditorRail::styleButton(*propertiesToggle_);
    propertiesToggle_->setVisible(true);
    propertiesToggle_->onClick = [this]() {
        if (onTogglePropertiesPanel)
            onTogglePropertiesPanel();
    };
    // Waveform or spectrogram, one choice for every clip (#3065).
    spectrogramToggle_ = std::make_unique<magda::SvgButton>(
        "SpectrogramToggle", BinaryData::spectrogram_svg, BinaryData::spectrogram_svgSize);
    spectrogramToggle_->setTooltip("Show the spectrogram instead of the waveform");
    magda::EditorRail::styleButton(*spectrogramToggle_);
    spectrogramToggle_->setActive(magda::Config::getInstance().getAudioEditorSpectrogram());
    spectrogramToggle_->onClick = [this]() {
        auto& config = magda::Config::getInstance();
        const bool shown = !config.getAudioEditorSpectrogram();
        config.setAudioEditorSpectrogram(shown);
        config.save();
        spectrogramToggle_->setActive(shown);
        gridComponent_->setSpectrogramShown(shown);
    };
    spectrogramToggle_->setVisible(true);

    rail_.setButtons({{spectrogramToggle_.get()}}, {propertiesToggle_.get()});
    addAndMakeVisible(rail_);

    // Create waveform grid component
    gridComponent_ = std::make_unique<WaveformGridComponent>();
    gridComponent_->setSpectrogramShown(magda::Config::getInstance().getAudioEditorSpectrogram());
    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setHorizontalZoom(horizontalZoom_);
    gridComponent_->setTimeRuler(timeRuler_.get());

    // Apply initial grid resolution from num/den defaults
    if (gridNumerator_ > 0) {
        double beats = static_cast<double>(gridNumerator_) * (4.0 / gridDenominator_);
        gridComponent_->setGridResolutionBeats(beats);
        timeRuler_->setGridResolution(beats);
    }

    // Create viewport and add grid
    viewport_ = std::make_unique<ScrollNotifyingViewport>();
    viewport_->setViewedComponent(gridComponent_.get(), false);
    viewport_->setScrollBarsShown(true, false);  // vertical only; horizontal is virtual
    viewport_->timeRulerToRepaint = timeRuler_.get();
    addAndMakeVisible(viewport_.get());

    // Viewport scroll callback — vertical only (horizontal is virtual)
    viewport_->onScrolled = [this](int /*x*/, int /*y*/) {
        // Vertical scroll may still fire; repaint overlays
        if (playheadOverlay_)
            playheadOverlay_->repaint();
    };

    // Create playhead overlay on top of viewport
    playheadOverlay_ = std::make_unique<PlayheadOverlay>(*this);
    addAndMakeVisible(playheadOverlay_.get());

    // Horizontal scroll/zoom bar at the bottom — drives the virtual scroll
    // and the editor's horizontal zoom (drag thumb = scroll, drag edges =
    // zoom). Matches the arranger's ZoomScrollBar pattern.
    horizontalScrollBar_ =
        std::make_unique<magda::ZoomScrollBar>(magda::ZoomScrollBar::Orientation::Horizontal);
    horizontalScrollBar_->onRangeChanged = [this](double start, double end) {
        if (!viewport_ || !gridComponent_)
            return;
        const double rangeWidth = end - start;
        if (rangeWidth <= 0.0)
            return;
        const juce::int64 contentW = gridComponent_->getVirtualContentWidth();
        const int viewW = viewport_->getWidth();
        if (contentW <= 0 || viewW <= 0)
            return;

        // Re-derive zoom from the new visible fraction. visibleFrac in pixel
        // terms equals viewW / contentW today; setting it to rangeWidth means
        // scaling horizontalZoom_ by (current visibleFrac / rangeWidth).
        const double currentVisibleFrac =
            static_cast<double>(viewW) / static_cast<double>(contentW);
        const double newZoom =
            juce::jlimit(MIN_ZOOM, MAX_ZOOM, horizontalZoom_ * (currentVisibleFrac / rangeWidth));

        isUpdatingFromScrollBar_ = true;
        if (newZoom != horizontalZoom_) {
            horizontalZoom_ = newZoom;
            gridComponent_->setHorizontalZoom(horizontalZoom_);
            if (timeRuler_) {
                double bpm = 120.0;
                if (auto* controller = magda::TimelineController::getCurrent())
                    bpm = controller->getState().tempo.bpm;
                timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
                timeRuler_->setTempo(bpm);
            }
            updateGridSize();
        }
        const juce::int64 newContentW = gridComponent_->getVirtualContentWidth();
        setVirtualScrollX(static_cast<int>(start * static_cast<double>(newContentW)));
        isUpdatingFromScrollBar_ = false;
    };
    addAndMakeVisible(horizontalScrollBar_.get());

    // Register as TimelineStateListener
    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        controller->addListener(this);
        const auto& state = controller->getState();
        cachedEditPosition_ = state.playhead.editPosition;
        cachedPlaybackPosition_ = state.playhead.playbackPosition;
        // Session playhead is now per-clip (ClipInfo::sessionPlayheadPos)
        cachedIsPlaying_ = state.playhead.isPlaying;
    }

    // Callback when waveform is edited
    gridComponent_->onWaveformChanged = []() {
        // Could add logic here if needed
    };

    // Warp marker callbacks — route through UndoManager for undo support
    gridComponent_->onWarpMarkerAdd = [this](double sourceTime, double warpTime) {
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<AddWarpMarkerCommand>(editingClipId_, sourceTime, warpTime));
            refreshWarpMarkers();
        }
    };

    gridComponent_->onWarpMarkerMove = [this](int index, double newWarpTime) {
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<MoveWarpMarkerCommand>(editingClipId_, index, newWarpTime));
            refreshWarpMarkers();
        }
    };

    gridComponent_->onWarpMarkerRemove = [this](int index) {
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<RemoveWarpMarkerCommand>(editingClipId_, index));
            refreshWarpMarkers();
        }
    };

    // Warp marker reposition callback (Alt+drag: remove + add at new position)
    gridComponent_->onWarpMarkerReposition = [this](int index, double newSourceTime,
                                                    double newWarpTime) {
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto markers = magda::getClipWarpMarkers(editingClipId_);
            // Keep the dragged marker's index stable and never remove it
            // before an invalid replacement (including a boundary) is rejected.
            if (index <= 0 || index + 1 >= static_cast<int>(markers.size()) ||
                !std::isfinite(newSourceTime) || !std::isfinite(newWarpTime) ||
                newSourceTime <= markers[static_cast<size_t>(index - 1)].sourceTime ||
                newSourceTime >= markers[static_cast<size_t>(index + 1)].sourceTime)
                return;
            CompoundOperationScope scope("Reposition Warp Marker");
            UndoManager::getInstance().executeCommand(
                std::make_unique<RemoveWarpMarkerCommand>(editingClipId_, index));
            UndoManager::getInstance().executeCommand(
                std::make_unique<AddWarpMarkerCommand>(editingClipId_, newSourceTime, newWarpTime));
            refreshWarpMarkers();
        }
    };

    // Slice callbacks
    gridComponent_->onSliceAtWarpMarkers = [this]() { sliceAtWarpMarkers(); };
    gridComponent_->onSliceAtGrid = [this]() { sliceAtGrid(); };
    gridComponent_->onSliceWarpMarkersToDrumGrid = [this]() { sliceWarpMarkersToDrumGrid(); };
    gridComponent_->onSliceAtGridToDrumGrid = [this]() { sliceAtGridToDrumGrid(); };

    // Loop-record takes: a lane click fronts that take as the clip's source and
    // re-syncs (ClipSynchronizer rebuilds the clip and re-attaches the takes).
    gridComponent_->onTakeSelected = [this](int takeIndex) {
        magda::ClipManager::getInstance().setAudioClipCurrentTake(editingClipId_, takeIndex);
    };

    // Comping: a swipe across a take lane assigns that range of the comp to the
    // swiped take. CompService edits the comp section list and re-renders.
    gridComponent_->onCompSectionSet = [this](double startSeconds, double endSeconds,
                                              int takeIndex) {
        magda::CompService::getInstance().setSection(editingClipId_, startSeconds, endSeconds,
                                                     takeIndex);
    };
    gridComponent_->onCompClear = [this]() {
        magda::CompService::getInstance().clearComp(editingClipId_);
    };
    gridComponent_->onTakeDelete = [this](int takeIndex) {
        magda::ClipManager::getInstance().deleteClipTake(editingClipId_, takeIndex);
    };

    // Zoom drag on waveform body, resolved through GestureRouter.
    gridComponent_->onZoomDrag = [this](int deltaX, int deltaY, int anchorX,
                                        const juce::ModifierKeys& mods) {
        // zero deltas signal drag start — capture starting zoom
        if (deltaX == 0 && deltaY == 0) {
            waveformZoomStartZoom_ = horizontalZoom_;
            return;
        }

        double startZoom = waveformZoomStartZoom_;
        if (startZoom <= 0.0)
            startZoom = horizontalZoom_;

        const auto axis = std::abs(deltaX) > std::abs(deltaY) ? magda::GestureAxis::Horizontal
                                                              : magda::GestureAxis::Vertical;
        const int dragDelta = axis == magda::GestureAxis::Horizontal ? deltaX : deltaY;
        const auto gesture = magda::GestureRouter::getInstance().resolveDrag(
            magda::GestureContext::Waveform, magda::GestureArea::Body, axis, mods,
            static_cast<float>(dragDelta), {anchorX, 0});
        if (gesture.type != magda::GestureActionType::ZoomHorizontal)
            return;

        double newZoom = startZoom * std::pow(2.0, gesture.magnitude);
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            // anchorX is already viewport-relative (converted in grid mouseDown)
            performAnchorPointZoom(newZoom / horizontalZoom_, anchorX);
        }
    };

    // Check if there's already a selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->isAudio()) {
            setClip(selectedClip);
        }
    }
}

WaveformEditorContent::~WaveformEditorContent() {
    stopTimer();

    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        controller->removeListener(this);
    }

    magda::ClipManager::getInstance().removeListener(this);
    magda::UndoManager::getInstance().removeListener(this);
    magda::AudioThumbnailManager::getInstance().removeTransientCacheListener(this);

    // Clear look and feel before destruction
}

// ============================================================================
// Layout
// ============================================================================

void WaveformEditorContent::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    g.fillAll(ActiveTheme::getColour(ActiveTheme::MIDI_LANE));
}

void WaveformEditorContent::paintOverChildren(juce::Graphics& g) {
    // Multi-clip selection placeholder (#1499): no single waveform to show,
    // list what is selected instead. Editing happens in the properties panel.
    if (!multiClipNames_.isEmpty()) {
        auto bounds = getLocalBounds();
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getUIFont(14.0f));
        auto headline = juce::String(multiClipNames_.size()) + " clips selected";
        auto textArea = bounds.withSizeKeepingCentre(bounds.getWidth() - 40, 60);
        g.drawText(headline, textArea.removeFromTop(24), juce::Justification::centred);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY).withAlpha(0.6f));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText(multiClipNames_.joinIntoString(", "), textArea, juce::Justification::centredTop,
                   true);
    }

    if (!transientsUpdating_)
        return;

    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    constexpr int overlayW = 172;
    constexpr int overlayH = 30;
    auto area = juce::Rectangle<int>(overlayW, overlayH)
                    .withRightX(bounds.getRight() - 12)
                    .withY(TIME_RULER_HEIGHT + 10)
                    .getIntersection(bounds);
    if (area.isEmpty())
        return;

    g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE).withAlpha(0.88f));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withAlpha(0.9f));
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 6.0f, 1.0f);

    auto spinnerArea = area.removeFromLeft(32).reduced(8).toFloat();
    const auto centre = spinnerArea.getCentre();
    const float radius = juce::jmin(spinnerArea.getWidth(), spinnerArea.getHeight()) * 0.45f;
    for (int i = 0; i < 12; ++i) {
        const float t = static_cast<float>(i) / 12.0f;
        const float angle = transientSpinnerPhase_ + t * juce::MathConstants<float>::twoPi;
        const float alpha = 0.18f + 0.72f * t;
        const auto p = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * radius;
        g.setColour(ActiveTheme::getAccentColour().withAlpha(alpha));
        g.fillEllipse(p.x - 1.6f, p.y - 1.6f, 3.2f, 3.2f);
    }

    g.setColour(ActiveTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText("Updating transients", area.reduced(0, 1), juce::Justification::centredLeft, false);
}

void WaveformEditorContent::resized() {
    auto bounds = getLocalBounds();

    // Guard against too-small bounds (panel being resized very small)
    int minHeight = TIME_RULER_HEIGHT + 1;
    if (bounds.getHeight() < minHeight || bounds.getWidth() <= 0) {
        // Hide everything when too small to avoid zero-sized paint
        timeRuler_->setBounds(0, 0, 0, 0);
        viewport_->setBounds(0, 0, 0, 0);
        if (playheadOverlay_)
            playheadOverlay_->setBounds(0, 0, 0, 0);
        if (horizontalScrollBar_)
            horizontalScrollBar_->setBounds(0, 0, 0, 0);
        return;
    }

    rail_.setBounds(bounds.removeFromLeft(magda::EditorRail::kWidth));

    auto rulerArea = bounds.removeFromTop(TIME_RULER_HEIGHT);
    timeRuler_->setBounds(rulerArea);

    // Horizontal scroll/zoom bar at the bottom
    if (horizontalScrollBar_ && bounds.getHeight() > H_SCROLLBAR_HEIGHT) {
        auto scrollBarArea = bounds.removeFromBottom(H_SCROLLBAR_HEIGHT);
        horizontalScrollBar_->setBounds(scrollBarArea);
    } else if (horizontalScrollBar_) {
        horizontalScrollBar_->setBounds(0, 0, 0, 0);
    }

    // Viewport fills remaining space
    viewport_->setBounds(bounds);

    // Playhead overlay covers the viewport area
    if (playheadOverlay_) {
        playheadOverlay_->setBounds(bounds);
    }

    // Set grid minimum height to match viewport's visible height so waveform fills the space
    if (gridComponent_) {
        gridComponent_->setMinimumHeight(viewport_->getMaximumVisibleHeight());
        gridComponent_->setParentWidth(viewport_->getWidth());
    }

    // Update grid size
    updateGridSize();

    // Refresh the horizontal scroll bar — viewport width or content width
    // may have changed, which shifts the visible fraction.
    updateHorizontalScrollBar();
}

// ============================================================================
// Panel Lifecycle
// ============================================================================

void WaveformEditorContent::onActivated() {
    // Check for selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->isAudio()) {
            setClip(selectedClip);
        }
    }
}

void WaveformEditorContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformEditorContent::mouseDown(const juce::MouseEvent& event) {
    bool overHeader = event.y < TIME_RULER_HEIGHT;
    if (overHeader) {
        headerDragActive_ = true;
        headerDragStartX_ = event.x;
        headerDragStartY_ = event.y;
        headerDragAnchorX_ = event.x - viewport_->getX();
        headerDragStartZoom_ = horizontalZoom_;
    }
}

void WaveformEditorContent::mouseDrag(const juce::MouseEvent& event) {
    if (headerDragActive_) {
        const int deltaX = event.x - headerDragStartX_;
        const int deltaY = headerDragStartY_ - event.y;
        const auto axis = std::abs(deltaX) > std::abs(deltaY) ? magda::GestureAxis::Horizontal
                                                              : magda::GestureAxis::Vertical;
        const int dragDelta = axis == magda::GestureAxis::Horizontal ? deltaX : deltaY;
        const auto gesture = magda::GestureRouter::getInstance().resolveDrag(
            magda::GestureContext::Waveform, magda::GestureArea::Header, axis, event.mods,
            static_cast<float>(dragDelta), {headerDragAnchorX_, headerDragStartY_});
        if (gesture.type != magda::GestureActionType::ZoomHorizontal)
            return;

        double newZoom = headerDragStartZoom_ * std::pow(2.0, gesture.magnitude);
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            if (dragDelta > 0) {
                setMouseCursor(magda::CursorManager::getInstance().getZoomInCursor());
            } else if (dragDelta < 0) {
                setMouseCursor(magda::CursorManager::getInstance().getZoomOutCursor());
            }

            performAnchorPointZoom(newZoom / horizontalZoom_, headerDragAnchorX_);
        }
    }
}

void WaveformEditorContent::mouseUp(const juce::MouseEvent& /*event*/) {
    headerDragActive_ = false;
}

void WaveformEditorContent::mouseMove(const juce::MouseEvent& event) {
    bool overHeader = event.y < TIME_RULER_HEIGHT;
    if (overHeader) {
        setMouseCursor(magda::CursorManager::getInstance().getZoomCursor());
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void WaveformEditorContent::mouseWheelMove(const juce::MouseEvent& event,
                                           const juce::MouseWheelDetails& wheel) {
    // Wheel scrolls the sample view horizontally, via GestureRouter (#1350).
    // The Waveform binding carries the editor's original sensitivity (800), so
    // magnitude reproduces the previous raw-delta * 800 step.
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::Waveform, wheel, event.mods, event.getPosition());
    if (gesture.type == magda::GestureActionType::ScrollHorizontal) {
        setVirtualScrollX(virtualScrollX_ - static_cast<int>(gesture.magnitude));
        return;
    }

    if (gesture.type == magda::GestureActionType::ZoomHorizontal) {
        int anchorX = event.x - viewport_->getX();
        performAnchorPointZoom(std::pow(2.0, static_cast<double>(gesture.magnitude)), anchorX);
    }
}

void WaveformEditorContent::mouseMagnify(const juce::MouseEvent& event, float scaleFactor) {
    int anchorX = event.x - viewport_->getX();
    performAnchorPointZoom(static_cast<double>(scaleFactor), anchorX);
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void WaveformEditorContent::clipsChanged() {
    // Check if our clip was deleted
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            editingClipId_ = magda::INVALID_CLIP_ID;
            gridComponent_->setClip(magda::INVALID_CLIP_ID);
        }
    }
}

void WaveformEditorContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Issue #1157: read clip position/length through the accessors —
            // for autoTempo clips these compute from beats × projectBPM live,
            // so the loop bar in the waveform editor stays aligned with the
            // inspector beat readout after a project-tempo change. Reading
            // clip->startTime / clip->length directly was the cause of the
            // user-reported drift where the green loop bracket spanned a
            // different number of bars than `lengthBeats` claimed.
            double currentBpm = 120.0;
            if (auto* tc = magda::TimelineController::getCurrent())
                currentBpm = tc->getState().tempo.bpm;
            const double clipStart = facadeTimelineStart(*clip, currentBpm);
            const double clipLength = facadeTimelineLength(*clip, currentBpm);

            // Update clip boundaries (needed for resize)
            // and display info (offset marker, loop markers).
            gridComponent_->updateClipPosition(clipStart, clipLength);
            timeRuler_->setClipLength(clipLength);

            updateDisplayInfo(*clip);

            // Bar/grid origin is timeline-domain. Source offset must not move it.
            if (clip->view != magda::ClipView::Session && !clip->loopEnabled) {
                timeRuler_->setBarOrigin(0.0);
            }

            // Update warp mode state
            bool warpEnabled = magda::audioEventRef(*clip).warpEnabled;
            gridComponent_->setWarpMode(warpEnabled);

            // Latched first: the edit below notifies, and lands back here.
            const bool wasWarpEnabled = std::exchange(wasWarpEnabled_, warpEnabled);
            if (warpEnabled && !wasWarpEnabled)
                magda::seedWarpMarkersFromTransients(editingClipId_,
                                                     cachedBpm_ > 0.0 ? cachedBpm_ : 120.0);
            else if (!warpEnabled && wasWarpEnabled)
                magda::clearWarpMarkers(editingClipId_);
            refreshWarpMarkers();
        }
        syncGainEnvelope();

        // Check if cached transients were invalidated (e.g. sensitivity changed)
        if (clip->isAudio() && !magda::audioEventRef(*clip).sourceFilePath().isEmpty()) {
            const auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(
                magda::audioEventRef(*clip).sourceFilePath());
            if (cached) {
                gridComponent_->setTransientTimes(*cached);
                transientsCached_ = true;
                setTransientsUpdating(false);
            } else {
                transientsCached_ = false;
                requestTransientDetection();
            }
        }

        updateGridSize();
        repaint();
    }
}

void WaveformEditorContent::transientsChanged(const juce::String& filePath) {
    // Detection finished for a clip we're no longer editing: drop the spinner
    // (it would otherwise stick — nothing else clears it).
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        setTransientsUpdating(false);
        return;
    }
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (clip == nullptr || !clip->isAudio()) {
        setTransientsUpdating(false);
        return;
    }
    if (magda::audioEventRef(*clip).sourceFilePath() != filePath)
        return;
    const auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(filePath);
    if (cached) {
        double bpm = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent())
            bpm = controller->getState().tempo.bpm;

        const double clipStart = facadeTimelineStart(*clip, bpm);
        const double clipLength = facadeTimelineLength(*clip, bpm);
        gridComponent_->setClip(editingClipId_);
        gridComponent_->updateClipPosition(clipStart, clipLength);
        timeRuler_->setClipLength(clipLength);
        updateDisplayInfo(*clip);
        updateGridSize();

        gridComponent_->setTransientTimes(*cached);
        gridComponent_->repaint();
        if (viewport_)
            viewport_->repaint();
        repaint();
        transientsCached_ = true;
        setTransientsUpdating(false);
        return;
    }

    transientsCached_ = false;
    setTransientsUpdating(true);
}

void WaveformEditorContent::clipSelectionChanged(magda::ClipId clipId) {
    // Auto-switch to the selected clip if it's an audio clip
    if (clipId != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->isAudio()) {
            setClip(clipId);
        }
    }
}

// ============================================================================
// UndoManagerListener
// ============================================================================

void WaveformEditorContent::undoStateChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        refreshWarpMarkers();
    }
}

// ============================================================================
// TimelineStateListener
// ============================================================================

void WaveformEditorContent::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    // Playhead changes
    if (hasFlag(changes, ChangeFlags::Playhead)) {
        cachedEditPosition_ = state.playhead.editPosition;
        cachedPlaybackPosition_ = state.playhead.playbackPosition;
        // Session playhead is now per-clip (ClipInfo::sessionPlayheadPos)
        cachedIsPlaying_ = state.playhead.isPlaying;
        refreshPlayhead();
    }

    // Tempo changes - BPM zoom scaling + ruler sync
    if (hasFlag(changes, ChangeFlags::Tempo)) {
        double newBpm = state.tempo.bpm;

        // Scale pps zoom to keep visual bar width constant when BPM changes.
        // new_pps = old_pps * new_bpm / old_bpm  (keeps ppb constant)
        if (cachedBpm_ > 0.0 && std::abs(newBpm - cachedBpm_) > 0.01) {
            horizontalZoom_ =
                juce::jlimit(MIN_ZOOM, MAX_ZOOM, horizontalZoom_ * newBpm / cachedBpm_);
            gridComponent_->setHorizontalZoom(horizontalZoom_);
            timeRuler_->setZoom(horizontalZoom_ * 60.0 / newBpm);
            updateGridSize();
        }
        cachedBpm_ = newBpm;

        // Sync tempo to the TimeRuler so beat grid and snap stay in sync
        timeRuler_->setTempo(newBpm);
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);

        // Issue #1157: refresh the ruler's clipLength here too. For autoTempo
        // clips length-in-seconds = lengthBeats × 60 / bpm, so a tempo change
        // changes it. Without this, the ruler kept the old-tempo seconds and
        // drew the loop bar at the wrong number of bars (the user-visible
        // drift in the screenshots: green loop region spanning 9-10 bars
        // when lengthBeats said 16 = 4 bars).
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
            if (clip) {
                const double clipLength = facadeTimelineLength(*clip, newBpm);
                const double clipStart = facadeTimelineStart(*clip, newBpm);
                timeRuler_->setClipLength(clipLength);
                if (gridComponent_)
                    gridComponent_->updateClipPosition(clipStart, clipLength);
                updateDisplayInfo(*clip);
            }
        }
        gridComponent_->repaint();
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void WaveformEditorContent::setMultiClipSelection(
    const std::unordered_set<magda::ClipId>& clipIds) {
    juce::StringArray names;
    if (clipIds.size() > 1) {
        auto& cm = magda::ClipManager::getInstance();
        for (auto cid : clipIds) {
            if (const auto* clip = cm.getClip(cid))
                names.add(clip->name);
        }
        names.sort(true);
    }
    if (names == multiClipNames_)
        return;

    multiClipNames_ = std::move(names);
    if (!multiClipNames_.isEmpty()) {
        setClip(magda::INVALID_CLIP_ID);
        setTransientsUpdating(false);
        // Hide the grid so its own "No audio clip selected" empty state
        // doesn't show through underneath the placeholder.
        if (viewport_)
            viewport_->setVisible(false);
    } else if (viewport_) {
        viewport_->setVisible(true);
    }
    repaint();
}

void WaveformEditorContent::setClip(magda::ClipId clipId) {
    if (clipId != magda::INVALID_CLIP_ID) {
        multiClipNames_.clear();
        if (viewport_)
            viewport_->setVisible(true);
    }
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        transientsCached_ = false;
        gridComponent_->setClip(clipId);

        // Update time ruler with clip info
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            setRelativeTimeMode(true);

            // Get tempo from TimelineController
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }
            cachedBpm_ = bpm;

            timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
            timeRuler_->setTempo(bpm);
            // Issue #1157: read through the accessors so autoTempo clips get
            // a length/start derived live from beats × bpm.
            const double clipStart = facadeTimelineStart(*clip, bpm);
            const double clipLength = facadeTimelineLength(*clip, bpm);
            gridComponent_->updateClipPosition(clipStart, clipLength);
            timeRuler_->setTimeOffset(0.0);
            timeRuler_->setClipLength(clipLength);

            // Compute display info first — needed for bar origin calculation
            updateDisplayInfo(*clip);

            // Source offset describes which audio samples play at the clip boundary.
            // It must not shift the timeline/bar origin.
            timeRuler_->setBarOrigin(0.0);
        }

        // Update warp mode state
        if (clip) {
            bool warpEnabled = magda::audioEventRef(*clip).warpEnabled;
            gridComponent_->setWarpMode(warpEnabled);
            wasWarpEnabled_ = warpEnabled;

            refreshWarpMarkers();
        }
        syncGainEnvelope();

        // Check for cached transients or request async detection.
        if (clip && clip->isAudio() && !magda::audioEventRef(*clip).sourceFilePath().isEmpty()) {
            const auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(
                magda::audioEventRef(*clip).sourceFilePath());
            if (cached) {
                gridComponent_->setTransientTimes(*cached);
                transientsCached_ = true;
                setTransientsUpdating(false);
            } else {
                requestTransientDetection();
            }
        } else {
            // No audio target (deselected / multi-selection / MIDI): nothing
            // is being waited on — clear the spinner or it sticks forever.
            setTransientsUpdating(false);
        }

        updateGridSize();
        scrollToClipStart();
        repaint();
    }
}

void WaveformEditorContent::setRelativeTimeMode(bool relative) {
    juce::ignoreUnused(relative);

    const bool changed = !relativeTimeMode_;
    relativeTimeMode_ = true;

    gridComponent_->setRelativeMode(true);
    timeRuler_->setRelativeMode(true);

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (clip && timeRuler_) {
        double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
        updateDisplayInfo(*clip);
        timeRuler_->setTimeOffset(0.0);
        timeRuler_->setClipLength(facadeTimelineLength(*clip, bpm));
        timeRuler_->setBarOrigin(0.0);
    }

    updateGridSize();
    if (changed) {
        scrollToClipStart();
        repaint();
    }
}

void WaveformEditorContent::setPropertiesPanelOpen(bool open) {
    propertiesToggle_->updateSvgData(
        open ? BinaryData::right_close_svg : BinaryData::right_open_svg,
        open ? BinaryData::right_close_svgSize : BinaryData::right_open_svgSize);
}

void WaveformEditorContent::setSnapEnabledFromUI(bool enabled) {
    snapEnabled_ = enabled;
    if (gridComponent_)
        gridComponent_->setSnapEnabled(enabled);
    if (timeRuler_)
        timeRuler_->setSnapEnabled(enabled);
}

bool WaveformEditorContent::editingClipHasMultipleTakes() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    return clip && clip->isAudio() && clip->audio().takes.size() > 1;
}

void WaveformEditorContent::syncGainEnvelope() {
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    const auto editable = clip && clip->isAudio();
    const auto takeLanes = editable && clip->audio().takes.size() > 1 && clip->takesExpanded;
    const auto shown = editable && clip->gainEnvelopeShown && !takeLanes;

    gridComponent_->setGainEnvelopeShown(shown);
}

bool WaveformEditorContent::areTakesExpanded() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    return clip && clip->isAudio() && clip->takesExpanded;
}

void WaveformEditorContent::setTakesExpanded(bool expanded) {
    auto& cm = magda::ClipManager::getInstance();
    auto* clip = cm.getClip(editingClipId_);
    if (!clip || !clip->isAudio())
        return;
    clip->takesExpanded = expanded;
    cm.forceNotifyClipPropertyChanged(editingClipId_);
}

// ============================================================================
// Private Helpers
// ============================================================================

int WaveformEditorContent::getMaxVirtualScrollX() const {
    if (!gridComponent_ || !viewport_)
        return 0;
    juce::int64 maxScroll =
        gridComponent_->getVirtualContentWidth() - static_cast<juce::int64>(viewport_->getWidth());
    if (maxScroll <= 0)
        return 0;
    // Cap to INT_MAX so downstream int math stays safe
    return static_cast<int>(
        juce::jmin(maxScroll, static_cast<juce::int64>(std::numeric_limits<int>::max())));
}

void WaveformEditorContent::setVirtualScrollX(int x) {
    x = juce::jlimit(0, getMaxVirtualScrollX(), x);
    virtualScrollX_ = x;
    if (gridComponent_)
        gridComponent_->setScrollOffset(x, 0);
    if (timeRuler_)
        timeRuler_->setScrollOffset(x);
    refreshPlayhead();
    if (!isUpdatingFromScrollBar_)
        updateHorizontalScrollBar();
}

void WaveformEditorContent::updateHorizontalScrollBar() {
    if (!horizontalScrollBar_ || !gridComponent_ || !viewport_)
        return;
    const juce::int64 contentW = gridComponent_->getVirtualContentWidth();
    const int viewW = viewport_->getWidth();
    if (contentW <= 0 || viewW <= 0)
        return;
    const double start = static_cast<double>(virtualScrollX_) / static_cast<double>(contentW);
    const double end = static_cast<double>(virtualScrollX_ + viewW) / static_cast<double>(contentW);
    horizontalScrollBar_->setVisibleRange(start, juce::jmin(1.0, end));
}

void WaveformEditorContent::updateGridSize() {
    if (gridComponent_) {
        gridComponent_->updateGridSize();

        // Update time ruler length
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && timeRuler_) {
            double fileDuration = 0.0;
            if (magda::audioEventRef(*clip).sourceFilePath().isNotEmpty()) {
                auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    magda::audioEventRef(*clip).sourceFilePath());
                if (thumbnail) {
                    fileDuration = thumbnail->getTotalLength();
                }
            }
            double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
            auto info = magda::ClipDisplayInfo::from(*clip, bpm, fileDuration);
            double totalTime = info.fileExtentTimeline();

            // Ensure the ruler extends at least to the right edge of the viewport.
            // Padding is in whole bars so the ruler ends on a musically sensible boundary.
            double bpmForPad = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
            double barSeconds = 4.0 * 60.0 / bpmForPad;  // one 4/4 bar in seconds
            double viewportEndTime = 0.0;
            if (viewport_ && gridComponent_ && horizontalZoom_ > 0.0) {
                int viewW = viewport_->getWidth();
                viewportEndTime = static_cast<double>(virtualScrollX_ + viewW) / horizontalZoom_;
            }
            double rulerLength =
                juce::jmax(totalTime + barSeconds * 4.0, viewportEndTime + barSeconds);
            timeRuler_->setTimelineLength(rulerLength);
        }
    }
}

void WaveformEditorContent::scrollToClipStart() {
    setVirtualScrollX(0);
}

void WaveformEditorContent::updateDisplayInfo(const magda::ClipInfo& clip) {
    double bpm = 120.0;
    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        bpm = controller->getState().tempo.bpm;
    }

    // Get file duration for source extent calculation
    double fileDuration = 0.0;
    if (audioEventRef(clip).sourceFilePath().isNotEmpty()) {
        auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
            audioEventRef(clip).sourceFilePath());
        if (thumbnail) {
            fileDuration = thumbnail->getTotalLength();
        }
    }

    auto info = magda::ClipDisplayInfo::from(clip, bpm, fileDuration);
    cachedDisplayInfo_ = info;
    gridComponent_->setDisplayInfo(info);

    // Update time ruler loop region (green when active, grey when disabled)
    // Display anchored at file start — loop markers at real source positions
    if (timeRuler_) {
        bool showMarkers = clip.loopEnabled && audioEventRef(clip).loopLengthSeconds() > 0.0;
        bool loopIsActive = clip.loopEnabled;
        double loopStartPos = info.loopStartPositionSeconds;
        double loopLen = info.loopLengthSeconds;
        timeRuler_->setLoopRegion(loopStartPos, loopLen, showMarkers, loopIsActive);

        timeRuler_->setClipContentOffset(0.0);

        // Offset marker is shown on the waveform grid; no separate ruler marker needed
        timeRuler_->setLoopPhaseMarker(0.0, false);
    }
}

void WaveformEditorContent::performAnchorPointZoom(double zoomFactor, int anchorX) {
    if (!std::isfinite(zoomFactor) || zoomFactor <= 0.0 || !gridComponent_)
        return;

    if (viewport_)
        anchorX = juce::jlimit(0, juce::jmax(0, viewport_->getWidth() - 1), anchorX);

    // anchorX is in viewport-local coordinates. Convert to time using current virtual scroll.
    double anchorTime = gridComponent_->pixelToTime(anchorX);
    const double maxAnchorTime =
        juce::jmax(0.0, static_cast<double>(gridComponent_->getVirtualContentWidth()) /
                            juce::jmax(1.0, horizontalZoom_));
    anchorTime = juce::jlimit(0.0, maxAnchorTime, anchorTime);

    // Apply zoom
    double newZoom = horizontalZoom_ * zoomFactor;
    newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

    if (newZoom != horizontalZoom_) {
        horizontalZoom_ = newZoom;

        // Update grid component
        gridComponent_->setHorizontalZoom(horizontalZoom_);

        // Update time ruler
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && timeRuler_) {
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }
            timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
            timeRuler_->setTempo(bpm);
        }

        updateGridSize();

        // Recompute scroll so anchorTime stays at anchorX
        int newScrollX =
            static_cast<int>(anchorTime * horizontalZoom_) + GRID_LEFT_PADDING - anchorX;
        setVirtualScrollX(newScrollX);
    }
}

// ============================================================================
// Zoom to time range
// ============================================================================

void WaveformEditorContent::zoomToTimeRange(double startTime, double endTime) {
    if (endTime <= startTime)
        return;

    double duration = endTime - startTime;
    double padding = duration * 0.05;

    int viewWidth = viewport_ ? viewport_->getWidth() : getWidth();
    double newZoom = static_cast<double>(viewWidth) / (duration + padding * 2.0);
    newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

    horizontalZoom_ = newZoom;
    gridComponent_->setHorizontalZoom(horizontalZoom_);

    // Update time ruler (uses ppb)
    if (timeRuler_) {
        double bpm = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            bpm = controller->getState().tempo.bpm;
        }
        timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
        timeRuler_->setTempo(bpm);
    }

    updateGridSize();

    int scrollX = static_cast<int>((startTime - padding) * horizontalZoom_) + GRID_LEFT_PADDING;
    setVirtualScrollX(juce::jmax(0, scrollX));
}

// ============================================================================
// Warp Helpers
// ============================================================================

void WaveformEditorContent::refreshWarpMarkers() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;
    gridComponent_->setWarpMarkers(magda::getClipWarpMarkers(editingClipId_));
}

void WaveformEditorContent::requestTransientDetection() {
    if (transientsCached_)
        return;
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    setTransientsUpdating(true);
    if (magda::WarpMarkerManager::getInstance().getTransientTimes(editingClipId_)) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && !magda::audioEventRef(*clip).sourceFilePath().isEmpty()) {
            const auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(
                magda::audioEventRef(*clip).sourceFilePath());
            if (cached) {
                gridComponent_->setTransientTimes(*cached);
                setTransientsUpdating(false);
            }
        }
        transientsCached_ = true;
    }
}

void WaveformEditorContent::setTransientsUpdating(bool updating) {
    if (transientsUpdating_ == updating)
        return;

    transientsUpdating_ = updating;
    transientSpinnerPhase_ = 0.0f;
    if (transientsUpdating_) {
        startTimerHz(30);
    } else {
        stopTimer();
    }
    repaint();
}

void WaveformEditorContent::timerCallback() {
    if (!transientsUpdating_) {
        stopTimer();
        return;
    }

    transientSpinnerPhase_ += 0.22f;
    if (transientSpinnerPhase_ > juce::MathConstants<float>::twoPi)
        transientSpinnerPhase_ -= juce::MathConstants<float>::twoPi;
    repaint();
}

// ============================================================================
// Slice Helpers
// ============================================================================

void WaveformEditorContent::sliceAtWarpMarkers() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    double tempo = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
    magda::sliceClipAtWarpMarkers(editingClipId_, tempo);

    editingClipId_ = magda::INVALID_CLIP_ID;
    gridComponent_->setClip(magda::INVALID_CLIP_ID);
}

void WaveformEditorContent::sliceAtGrid() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
    double gridBeats = gridComponent_->getGridResolutionBeats();
    if (gridBeats <= 0.0)
        return;

    double gridInterval = gridBeats * 60.0 / bpm;
    magda::sliceClipAtGrid(editingClipId_, gridInterval, bpm);

    editingClipId_ = magda::INVALID_CLIP_ID;
    gridComponent_->setClip(magda::INVALID_CLIP_ID);
}

void WaveformEditorContent::sliceWarpMarkersToDrumGrid() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    double tempo = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
    magda::sliceWarpMarkersToDrumGrid(editingClipId_, tempo);
}

void WaveformEditorContent::sliceAtGridToDrumGrid() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
    double gridBeats = gridComponent_->getGridResolutionBeats();
    if (gridBeats <= 0.0)
        return;

    double gridInterval = gridBeats * 60.0 / bpm;
    magda::sliceAtGridToDrumGrid(editingClipId_, gridInterval, bpm);
}

}  // namespace magda::daw::ui
