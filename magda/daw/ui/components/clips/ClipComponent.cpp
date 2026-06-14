#include "ClipComponent.hpp"

#include <BinaryData.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numeric>
#include <unordered_set>

#include "../../panels/state/PanelController.hpp"
#include "../../state/TimelineController.hpp"
#include "../../state/TimelineEvents.hpp"
#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/SelectionPolicy.hpp"
#include "../tracks/TrackContentPanel.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/AppPaths.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

namespace {

double timelineStartSeconds(const ClipInfo& clip, double bpm) {
    return clip.getTimelineStart(bpm);
}

double timelineLengthSeconds(const ClipInfo& clip, double bpm) {
    return clip.getTimelineLength(bpm);
}

double timelineEndSeconds(const ClipInfo& clip, double bpm) {
    return clip.getTimelineEnd(bpm);
}

void showMidiClipLibrarySaveFailedAlert() {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "Save MIDI Clip Failed",
        "Could not write the MIDI clip file or add it to the media library.");
}

void showExternalEditorFailedAlert(const juce::String& message) {
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           "Edit in External Editor Failed", message);
}

constexpr int MIDI_PREVIEW_MIN_NOTE = 21;   // A0
constexpr int MIDI_PREVIEW_MAX_NOTE = 108;  // C8

juce::Path makeClippedRoundedRectPath(juce::Rectangle<int> bounds,
                                      juce::Rectangle<int> visibleBounds, float radius) {
    juce::Path path;

    if (bounds.isEmpty() || visibleBounds.isEmpty())
        return path;

    const bool leftEdgeVisible = visibleBounds.getX() <= bounds.getX();
    const bool rightEdgeVisible = visibleBounds.getRight() >= bounds.getRight();

    if (!leftEdgeVisible && !rightEdgeVisible) {
        path.addRectangle(visibleBounds.toFloat());
        return path;
    }

    const float left = static_cast<float>(visibleBounds.getX());
    const float right = static_cast<float>(visibleBounds.getRight());
    const float top = static_cast<float>(visibleBounds.getY());
    const float bottom = static_cast<float>(visibleBounds.getBottom());
    const float boundsLeft = static_cast<float>(bounds.getX());
    const float boundsRight = static_cast<float>(bounds.getRight());
    const float r = juce::jmin(radius, 0.5f * static_cast<float>(visibleBounds.getHeight()),
                               0.5f * static_cast<float>(visibleBounds.getWidth()));

    path.startNewSubPath(leftEdgeVisible ? boundsLeft + r : left, top);
    path.lineTo(rightEdgeVisible ? boundsRight - r : right, top);

    if (rightEdgeVisible)
        path.quadraticTo(boundsRight, top, boundsRight, top + r);
    else
        path.lineTo(right, top);

    path.lineTo(rightEdgeVisible ? boundsRight : right, rightEdgeVisible ? bottom - r : bottom);

    if (rightEdgeVisible)
        path.quadraticTo(boundsRight, bottom, boundsRight - r, bottom);
    else
        path.lineTo(right, bottom);

    path.lineTo(leftEdgeVisible ? boundsLeft + r : left, bottom);

    if (leftEdgeVisible)
        path.quadraticTo(boundsLeft, bottom, boundsLeft, bottom - r);
    else
        path.lineTo(left, bottom);

    path.lineTo(leftEdgeVisible ? boundsLeft : left, leftEdgeVisible ? top + r : top);

    if (leftEdgeVisible)
        path.quadraticTo(boundsLeft, top, boundsLeft + r, top);
    else
        path.lineTo(left, top);

    path.closeSubPath();
    return path;
}

void fillClippedRoundedRect(juce::Graphics& g, juce::Rectangle<int> bounds,
                            juce::Rectangle<int> visibleBounds, juce::Colour colour, float radius) {
    g.setColour(colour);
    g.fillPath(makeClippedRoundedRectPath(bounds, visibleBounds, radius));
}

void strokeClippedRoundedRect(juce::Graphics& g, juce::Rectangle<int> bounds,
                              juce::Rectangle<int> visibleBounds, juce::Colour colour, float radius,
                              float strokeWidth) {
    g.setColour(colour);
    g.strokePath(makeClippedRoundedRectPath(bounds, visibleBounds, radius),
                 juce::PathStrokeType(strokeWidth));
}

void logArrangeRangeSelect(const juce::String& message) {
    const auto line = juce::Time::getCurrentTime().toString(true, true, true, true) +
                      " [ArrangeRangeSelect] " + message;
    DBG(line);
    juce::Logger::writeToLog(line);

    auto logFile = paths::logsDir().getChildFile("arrange-range-select.log");
    logFile.getParentDirectory().createDirectory();
    if (!logFile.appendText(line + "\n", false, false, "\n")) {
        const auto failureLine = "[ArrangeRangeSelect] failed to append dedicated log file: " +
                                 logFile.getFullPathName();
        DBG(failureLine);
        juce::Logger::writeToLog(failureLine);
    }
}

}  // namespace

static float computeFadeGain(float alpha, FadeCurve curve) {
    const float a = alpha * juce::MathConstants<float>::halfPi;
    switch (curve) {
        case FadeCurve::Convex:
            return std::sin(a);
        case FadeCurve::Concave:
            return 1.0f - std::cos(a);
        case FadeCurve::SCurve: {
            float concave = 1.0f - std::cos(a);
            float convex = std::sin(a);
            return (1.0f - alpha) * concave + alpha * convex;
        }
        case FadeCurve::Linear:
        default:
            return alpha;
    }
}

ClipComponent::ClipComponent(ClipId clipId, TrackContentPanel* parent)
    : clipId_(clipId), parentPanel_(parent) {
    setName("ClipComponent");

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Check if this clip is currently selected
    isSelected_ = ClipManager::getInstance().getSelectedClip() == clipId_;
}

ClipComponent::~ClipComponent() {
    stopTimer();
    if (waveformListenerPath_.isNotEmpty())
        AudioThumbnailManager::getInstance().removeThumbnailChangeListener(waveformListenerPath_,
                                                                           this);
    ClipManager::getInstance().removeListener(this);
}

void ClipComponent::updateWaveformLoadListener(const juce::String& audioFilePath) {
    auto& mgr = AudioThumbnailManager::getInstance();
    auto* thumb = audioFilePath.isNotEmpty() ? mgr.getThumbnail(audioFilePath) : nullptr;
    // Listen only while the thumbnail exists and is still streaming in; once it
    // is fully loaded (or the clip has no audio) we want no listener.
    const juce::String wanted =
        (thumb != nullptr && !thumb->isFullyLoaded()) ? audioFilePath : juce::String();
    if (wanted == waveformListenerPath_)
        return;
    if (waveformListenerPath_.isNotEmpty())
        mgr.removeThumbnailChangeListener(waveformListenerPath_, this);
    waveformListenerPath_ = wanted;
    if (waveformListenerPath_.isNotEmpty())
        if (auto* t = mgr.getThumbnail(waveformListenerPath_))
            t->addChangeListener(this);
}

void ClipComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    // The thumbnail streamed in more samples. Repaint to fill the waveform
    // progressively, and drop the listener once it has finished loading.
    const auto* clip = getClipInfo();
    updateWaveformLoadListener(clip != nullptr ? clip->audio().source.filePath : juce::String());
    repaint();
}

void ClipComponent::paint(juce::Graphics& g) {
    if (getWidth() < 1 || getHeight() < 1)
        return;

    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    auto bounds = getLocalBounds();
    auto visibleBounds = bounds.getIntersection(g.getClipBounds());
    if (visibleBounds.isEmpty())
        return;

    // Draw based on clip type
    if (clip->isAudio()) {
        paintAudioClip(g, *clip, bounds);
    } else {
        paintMidiClip(g, *clip, bounds);
    }

    // Draw header (name, loop indicator)
    paintClipHeader(g, *clip, bounds);

    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;

    // Draw loop boundary corner cuts (after header so they cut through everything)
    double srcLength = clip->loopLength;
    if (clip->loopEnabled && srcLength > 0.0) {
        auto clipBounds = getLocalBounds();
        double beatsPerSecond = tempo / 60.0;
        // During resize drag, use preview length so boundaries stay fixed
        double displayLength =
            (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip->getTimelineLength(tempo);
        double clipLengthInBeats = displayLength * beatsPerSecond;
        // Loop length in beats: use authoritative beat value for autoTempo,
        // otherwise derive from source length and speedRatio
        double loopLengthBeats = (clip->autoTempo && clip->loopLengthBeats > 0.0)
                                     ? clip->loopLengthBeats
                                     : srcLength / clip->speedRatio * beatsPerSecond;
        double beatRange = juce::jmax(1.0, clipLengthInBeats);
        int numBoundaries = static_cast<int>(clipLengthInBeats / loopLengthBeats);
        auto markerColour = juce::Colours::lightgrey;

        // Calculate pixel spacing between loop boundaries to scale indicators
        float loopPixelWidth =
            static_cast<float>(loopLengthBeats / beatRange) * clipBounds.getWidth();
        float clipHeight = static_cast<float>(clipBounds.getHeight());

        // Below this per-loop pixel width the markers pack so densely they
        // turn the clip into a solid black mass — hide them entirely.
        constexpr float MIN_LOOP_MARKER_PIXEL_WIDTH = 32.0f;
        if (loopPixelWidth < MIN_LOOP_MARKER_PIXEL_WIDTH)
            numBoundaries = 0;

        for (int i = 1; i <= numBoundaries; ++i) {
            double boundaryBeat = i * loopLengthBeats;
            if (boundaryBeat >= clipLengthInBeats)
                break;

            float bx = static_cast<float>(clipBounds.getX()) +
                       static_cast<float>(boundaryBeat / beatRange) * clipBounds.getWidth();

            // Shadow gradient on right side of boundary (fold effect)
            float shadeWidth = juce::jmin(6.0f, loopPixelWidth * 0.15f);
            if (shadeWidth >= 1.0f) {
                float top = static_cast<float>(clipBounds.getY());
                float bot = static_cast<float>(clipBounds.getBottom());
                juce::ColourGradient shade(juce::Colours::black.withAlpha(0.45f), bx, 0.0f,
                                           juce::Colours::transparentBlack, bx + shadeWidth, 0.0f,
                                           false);
                g.setGradientFill(shade);
                g.fillRect(bx, top, shadeWidth, bot - top);
            }

            // Vertical line at loop boundary
            g.setColour(markerColour.withAlpha(0.7f));
            g.drawVerticalLine(static_cast<int>(bx), static_cast<float>(clipBounds.getY()),
                               static_cast<float>(clipBounds.getBottom()));

            // Scale triangle size: up to 10px, but no more than 1/3 of the loop pixel
            // width or 1/4 of clip height, so they don't overlap when zoomed out
            float cutSize = juce::jmin(6.0f, loopPixelWidth * 0.33f, clipHeight * 0.25f);
            if (cutSize < 2.0f)
                continue;  // Too small to draw meaningfully

            float top = static_cast<float>(clipBounds.getY());
            juce::Path cut;
            // Left triangle
            cut.addTriangle(bx - cutSize, top, bx, top, bx, top + cutSize);
            // Right triangle
            cut.addTriangle(bx, top, bx + cutSize, top, bx, top + cutSize);
            g.setColour(markerColour.withAlpha(0.8f));
            g.fillPath(cut);
        }
    }

    // Draw resize handles if selected
    if (isSelected_) {
        paintResizeHandles(g, bounds);
    }

    // Draw fade handles (selected audio clips only)
    if (isSelected_ && clip->isAudio()) {
        paintFadeHandles(g, *clip, getLocalBounds());
    }

    // Draw volume line (audio clips with non-zero volume, or when hovering/dragging)
    if (clip->isAudio() && (std::abs(clip->volumeDB) > 0.01f || hoverVolumeHandle_ ||
                            dragMode_ == DragMode::VolumeDrag)) {
        auto wfArea = bounds.reduced(2, 0).withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2);
        paintVolumeLine(g, *clip, wfArea);
    }

    // Marquee highlight overlay (during marquee drag)
    if (isMarqueeHighlighted_) {
        fillClippedRoundedRect(g, bounds, visibleBounds, juce::Colours::white.withAlpha(0.2f),
                               CORNER_RADIUS);
    }

    // Frozen overlay — dim clip on frozen tracks
    auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
    if (trackInfo && trackInfo->frozen) {
        fillClippedRoundedRect(g, bounds, visibleBounds, juce::Colours::black.withAlpha(0.35f),
                               CORNER_RADIUS);
    }

    // Session mode overlay — dim arrangement clips when track is in Session mode
    if (trackInfo && trackInfo->playbackMode == TrackPlaybackMode::Session &&
        clip->view == ClipView::Arrangement) {
        fillClippedRoundedRect(g, bounds, visibleBounds, juce::Colours::black.withAlpha(0.35f),
                               CORNER_RADIUS);
    }
}

size_t ClipComponent::computeWaveformHash(const ClipInfo& clip) {
    size_t h = 0;
    auto combine = [&](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(std::hash<juce::String>{}(clip.audio().source.filePath));
    combine(std::hash<double>{}(clip.placement.lengthBeats));
    combine(std::hash<double>{}(clip.offset));
    combine(std::hash<double>{}(clip.speedRatio));
    combine(std::hash<float>{}(clip.volumeDB));
    combine(std::hash<float>{}(clip.gainDB));
    combine(std::hash<bool>{}(clip.isReversed));
    combine(std::hash<bool>{}(clip.loopEnabled));
    combine(std::hash<double>{}(clip.loopStart));
    combine(std::hash<double>{}(clip.loopLength));
    combine(std::hash<double>{}(clip.loopLengthBeats));
    combine(std::hash<double>{}(clip.audio().interpretation.totalBeats));
    combine(std::hash<bool>{}(clip.warpEnabled));
    combine(std::hash<bool>{}(clip.autoTempo));
    combine(std::hash<double>{}(clip.fadeIn));
    combine(std::hash<double>{}(clip.fadeOut));
    combine(static_cast<size_t>(clip.colour.getARGB()));
    return h;
}

void ClipComponent::timerCallback() {
    if (mouseIsOver_) {
        const auto mods = juce::ModifierKeys::currentModifiers;
        updateCursor(mods);
        startTimer(50);
    } else {
        stopTimer();
    }

    repaint();
}

void ClipComponent::paintAudioClipDirect(juce::Graphics& g, const ClipInfo& clip,
                                         juce::Rectangle<int> waveformArea,
                                         double clipDisplayLength,
                                         juce::Colour waveColourOverride) {
    auto& thumbnailManager = AudioThumbnailManager::getInstance();

    double pixelsPerSecond = (clipDisplayLength > 0.0)
                                 ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                                 : 0.0;

    if (pixelsPerSecond <= 0.0)
        return;

    // Visible X range from graphics clip region — skip waveform tiles that are off-screen
    auto visClip = g.getClipBounds();
    int visLeft = visClip.getX();
    int visRight = visClip.getRight();

    // Clip a draw rect to the visible area and adjust the source time range accordingly.
    // Returns false if the rect is entirely off-screen.
    auto clipToVisible = [&](juce::Rectangle<int>& rect, double& srcStart, double& srcEnd) -> bool {
        int rectLeft = rect.getX();
        int rectRight = rect.getRight();
        if (rectRight <= visLeft || rectLeft >= visRight)
            return false;
        int clippedLeft = juce::jmax(rectLeft, visLeft);
        int clippedRight = juce::jmin(rectRight, visRight);
        int origWidth = rectRight - rectLeft;
        if (origWidth > 0) {
            double srcRange = srcEnd - srcStart;
            double fracLeft = static_cast<double>(clippedLeft - rectLeft) / origWidth;
            double fracRight = static_cast<double>(clippedRight - rectLeft) / origWidth;
            srcStart = srcStart + fracLeft * srcRange;
            srcEnd = srcStart + (fracRight - fracLeft) * srcRange;
        }
        rect = juce::Rectangle<int>(clippedLeft, rect.getY(), clippedRight - clippedLeft,
                                    rect.getHeight());
        return rect.getWidth() > 0;
    };

    if (clip.isReversed) {
        g.saveState();
        g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, waveformArea.getCentreX(),
                                                    waveformArea.getCentreY()));
    }

    double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;

    double fileDuration = 0.0;
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audio().source.filePath);
    if (thumbnail)
        fileDuration = thumbnail->getTotalLength();

    // Build display info with the real file duration so loop-region
    // fields get clamped against the file extent. Without this the
    // factory falls back to a clip-length-derived extent and the loop
    // clamp branch is skipped, which leaves loopRegionLengthSource
    // potentially extending past the file.
    auto di = ClipDisplayInfo::from(clip, tempo, fileDuration);

    double displayOffset = clip.offset;
    if (isDragging_ && dragMode_ == DragMode::ResizeLeft)
        displayOffset = resizePreviewClip_.offset;

    const bool selected = isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_);
    const auto waveColour =
        waveColourOverride.isTransparent() ? juce::Colours::black : waveColourOverride;
    float gainLinear = juce::Decibels::decibelsToGain(clip.volumeDB + clip.gainDB);

    bool useWarpedDraw = false;
    std::vector<WarpMarkerInfo> warpMarkers;
    if (clip.warpEnabled) {
        auto* audioEngine = TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            auto* bridge = audioEngine->getAudioBridge();
            if (bridge) {
                warpMarkers = bridge->getWarpMarkers(clipId_);
                useWarpedDraw = warpMarkers.size() >= 2;
            }
        }
    }

    if (useWarpedDraw && !di.isLooped()) {
        std::sort(warpMarkers.begin(), warpMarkers.end(),
                  [](const auto& a, const auto& b) { return a.warpTime < b.warpTime; });

        for (size_t i = 0; i + 1 < warpMarkers.size(); ++i) {
            double srcStart = warpMarkers[i].sourceTime;
            double srcEnd = warpMarkers[i + 1].sourceTime;
            double warpStart = warpMarkers[i].warpTime;
            double warpEnd = warpMarkers[i + 1].warpTime;

            double dispStart = warpStart - displayOffset;
            double dispEnd = warpEnd - displayOffset;
            if (dispEnd <= 0.0 || dispStart >= clipDisplayLength)
                continue;
            if (dispStart < 0.0) {
                double ratio = -dispStart / (dispEnd - dispStart);
                srcStart += ratio * (srcEnd - srcStart);
                dispStart = 0.0;
            }
            if (dispEnd > clipDisplayLength) {
                double ratio = (clipDisplayLength - dispStart) / (dispEnd - dispStart);
                srcEnd = srcStart + ratio * (srcEnd - srcStart);
                dispEnd = clipDisplayLength;
            }

            int pixStart =
                waveformArea.getX() + static_cast<int>(dispStart * pixelsPerSecond + 0.5);
            int pixEnd = waveformArea.getX() + static_cast<int>(dispEnd * pixelsPerSecond + 0.5);
            int segWidth = pixEnd - pixStart;
            if (segWidth <= 0)
                continue;

            auto drawRect = juce::Rectangle<int>(pixStart, waveformArea.getY(), segWidth,
                                                 waveformArea.getHeight());
            double finalSrcStart = juce::jmax(0.0, srcStart);
            double finalSrcEnd = fileDuration > 0.0 ? juce::jmin(srcEnd, fileDuration) : srcEnd;
            if (finalSrcEnd > finalSrcStart &&
                clipToVisible(drawRect, finalSrcStart, finalSrcEnd)) {
                thumbnailManager.drawWaveform(g, drawRect, clip.audio().source.filePath,
                                              finalSrcStart, finalSrcEnd, waveColour, gainLinear,
                                              true, selected);
            }
        }
    } else if (di.isLooped()) {
        double sourceDurationForBeats = clip.audio().source.durationSeconds;
        if (sourceDurationForBeats <= 0.0 && fileDuration > 0.0)
            sourceDurationForBeats = fileDuration;
        if (sourceDurationForBeats <= 0.0)
            sourceDurationForBeats = di.fileExtentSource();
        const double projectBpm = isValidBpm(tempo) ? tempo : DEFAULT_BPM;

        auto timelineDeltaToPreviewSource = [&](double timelineDelta) {
            if (clip.autoTempo && clip.audio().interpretation.totalBeats > 0.0 &&
                sourceDurationForBeats > 0.0) {
                double projectBeats = timelineDelta * projectBpm / 60.0;
                return projectBeats * sourceDurationForBeats /
                       clip.audio().interpretation.totalBeats;
            }
            return di.timelineToSource(timelineDelta);
        };

        auto sourceDeltaToPreviewTimeline = [&](double sourceDelta) {
            if (clip.autoTempo && clip.audio().interpretation.totalBeats > 0.0 &&
                sourceDurationForBeats > 0.0) {
                double sourceBeats =
                    sourceDelta * clip.audio().interpretation.totalBeats / sourceDurationForBeats;
                return sourceBeats * 60.0 / projectBpm;
            }
            return di.sourceToTimeline(sourceDelta);
        };

        double loopCycle = di.loopLengthSeconds;
        if (clip.autoTempo && clip.loopLengthBeats > 0.0)
            loopCycle = clip.loopLengthBeats * 60.0 / projectBpm;
        // These were named "fileStart/End" but actually hold the loop
        // region's bounds (in source-time). Renamed to match what they
        // really are; per-tile rendering reads from this loop subset, not
        // the whole file.
        double loopRegionStart = di.loopRegionStartSource;
        double loopRegionEnd = di.loopRegionStartSource + di.loopRegionLengthSource;
        if (fileDuration > 0.0 && loopRegionEnd > fileDuration)
            loopRegionEnd = fileDuration;
        double phaseSource = di.loopOffset;
        if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
            phaseSource = wrapPhase(resizePreviewClip_.offset - resizePreviewClip_.loopStart,
                                    di.loopRegionLengthSource);
        }
        double phaseTimeline = sourceDeltaToPreviewTimeline(phaseSource);
        bool isFirstTile = (phaseTimeline > 0.001);

        double timePos = 0.0;
        while (timePos < clipDisplayLength) {
            double tileFileStart = loopRegionStart;
            double tileFullDuration = loopCycle;
            if (isFirstTile) {
                // Render the partial loop fragment from (loopStart + phase)
                // to loopRegionEnd. Floating-point wrap edge cases can put
                // phase right at the loop boundary, producing a zero-length
                // fragment — fall through to the regular tile so we still
                // draw the rest of the clip instead of breaking out.
                const double partialStart = loopRegionStart + phaseSource;
                const double partialDuration =
                    sourceDeltaToPreviewTimeline(loopRegionEnd - partialStart);
                if (partialDuration > 0.0001) {
                    tileFileStart = partialStart;
                    tileFullDuration = partialDuration;
                }
                isFirstTile = false;
            }
            if (tileFullDuration <= 0.0001)
                break;
            double cycleEnd = juce::jmin(timePos + tileFullDuration, clipDisplayLength);
            double remainingTileDuration = cycleEnd - timePos;
            double segmentTime = timePos;
            double segmentSourceStart = tileFileStart;
            int safety = 0;
            while (remainingTileDuration > 0.0001 && safety++ < 128) {
                if (segmentSourceStart >= loopRegionEnd - 0.0001)
                    segmentSourceStart = loopRegionStart;

                double remainingSource = loopRegionEnd - segmentSourceStart;
                double fullSegmentDuration = sourceDeltaToPreviewTimeline(remainingSource);
                if (remainingSource <= 0.0001 || fullSegmentDuration <= 0.0001)
                    break;

                double segmentDuration = juce::jmin(remainingTileDuration, fullSegmentDuration);
                double segmentEnd = segmentTime + segmentDuration;
                int segmentX =
                    waveformArea.getX() + static_cast<int>(segmentTime * pixelsPerSecond + 0.5);
                int segmentRight =
                    waveformArea.getX() + static_cast<int>(segmentEnd * pixelsPerSecond + 0.5);
                auto segmentRect =
                    juce::Rectangle<int>(segmentX, waveformArea.getY(), segmentRight - segmentX,
                                         waveformArea.getHeight());

                double segmentSourceEnd =
                    segmentSourceStart + timelineDeltaToPreviewSource(segmentDuration);
                segmentSourceEnd = juce::jmin(segmentSourceEnd, loopRegionEnd);
                if (clipToVisible(segmentRect, segmentSourceStart, segmentSourceEnd))
                    thumbnailManager.drawWaveform(g, segmentRect, clip.audio().source.filePath,
                                                  segmentSourceStart, segmentSourceEnd, waveColour,
                                                  gainLinear, true, selected);

                segmentTime = segmentEnd;
                remainingTileDuration -= segmentDuration;
                if (segmentDuration >= fullSegmentDuration - 0.0001)
                    segmentSourceStart = loopRegionStart;
                else
                    segmentSourceStart = segmentSourceEnd;
            }
            timePos += tileFullDuration;
        }
    } else {
        double fileStart = displayOffset;
        double fileEnd = displayOffset + di.timelineToSource(clipDisplayLength);
        if (fileDuration > 0.0 && fileEnd > fileDuration)
            fileEnd = fileDuration;
        double clampedTimelineDuration = di.sourceToTimeline(fileEnd - fileStart);
        int drawWidth = static_cast<int>(clampedTimelineDuration * pixelsPerSecond + 0.5);
        drawWidth = juce::jmin(drawWidth, waveformArea.getWidth());
        auto drawRect = juce::Rectangle<int>(waveformArea.getX(), waveformArea.getY(), drawWidth,
                                             waveformArea.getHeight());
        if (clipToVisible(drawRect, fileStart, fileEnd))
            thumbnailManager.drawWaveform(g, drawRect, clip.audio().source.filePath, fileStart,
                                          fileEnd, waveColour, gainLinear, true, selected);
    }

    if (clip.isReversed)
        g.restoreState();
}

void ClipComponent::paintAudioClip(juce::Graphics& g, const ClipInfo& clip,
                                   juce::Rectangle<int> bounds) {
    auto visibleBounds = bounds.getIntersection(g.getClipBounds());
    if (visibleBounds.isEmpty())
        return;

    auto waveformArea = bounds.reduced(2, 0).withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2);

    double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    double clipDisplayLength = clip.getTimelineLength(tempo);
    if (isDragging_) {
        bool isResizeMode =
            (dragMode_ == DragMode::ResizeLeft || dragMode_ == DragMode::ResizeRight);
        bool isStretchMode =
            (dragMode_ == DragMode::StretchLeft || dragMode_ == DragMode::StretchRight);
        if ((isResizeMode || isStretchMode) && previewLength_ > 0.0)
            clipDisplayLength = previewLength_;
    }

    // Repaint as the thumbnail streams in (progressive fill). Registering a
    // change listener is reliable regardless of mouse hover, unlike the old
    // poll timer which only repainted while hovered, so long loads no longer
    // look frozen until they finish.
    updateWaveformLoadListener(clip.audio().source.filePath);

    // Draw directly — no offscreen cache.  AudioThumbnail is already a
    // pre-computed waveform cache (512 samples/point) so drawing from it is fast.
    auto bgColour = clip.colour.darker(0.3f);
    fillClippedRoundedRect(g, bounds, visibleBounds, bgColour, CORNER_RADIUS);

    if (clip.audio().source.filePath.isNotEmpty())
        paintAudioClipDirect(g, clip, waveformArea, clipDisplayLength);

    strokeClippedRoundedRect(g, bounds, visibleBounds, clip.colour.withAlpha(0.45f), CORNER_RADIUS,
                             1.0f);

    // Fade overlays
    if (clip.fadeIn > 0.0 || clip.fadeOut > 0.0) {
        double pps = (clipDisplayLength > 0.0)
                         ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                         : 0.0;
        if (pps > 0.0)
            paintFadeOverlays(g, clip, waveformArea, pps);
    }
}

void ClipComponent::paintMidiClip(juce::Graphics& g, const ClipInfo& clip,
                                  juce::Rectangle<int> bounds) {
    auto visibleBounds = bounds.getIntersection(g.getClipBounds());
    if (visibleBounds.isEmpty())
        return;

    auto bgColour = clip.colour.darker(0.3f);
    fillClippedRoundedRect(g, bounds, visibleBounds, bgColour, CORNER_RADIUS);

    auto noteArea = bounds.withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2);
    paintMidiNotes(g, clip, noteArea, juce::Colours::black);

    strokeClippedRoundedRect(g, bounds, visibleBounds, clip.colour.withAlpha(0.45f), CORNER_RADIUS,
                             1.0f);
}

void ClipComponent::paintMidiNotes(juce::Graphics& g, const ClipInfo& clip,
                                   juce::Rectangle<int> noteArea, juce::Colour noteColour) {
    if (clip.midiNotes.empty() || noteArea.getHeight() <= 5)
        return;

    g.setColour(noteColour);

    double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    double beatsPerSecond = tempo / 60.0;
    double displayLength =
        (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip.getTimelineLength(tempo);
    double clipLengthInBeats = displayLength * beatsPerSecond;

    double midiSrcLength =
        clip.loopLength > 0.0 ? clip.loopLength : displayLength * clip.speedRatio;
    double loopLengthBeats =
        clip.loopLengthBeats > 0.0
            ? clip.loopLengthBeats
            : (midiSrcLength > 0.0 ? midiSrcLength * beatsPerSecond : clipLengthInBeats);

    double midiOffset;
    if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
        midiOffset =
            clip.loopEnabled ? resizePreviewClip_.midiOffset : resizePreviewClip_.midiTrimOffset;
    } else {
        midiOffset = clip.loopEnabled ? clip.midiOffset : clip.midiTrimOffset;
    }

    double loopStart = clip.loopStart * beatsPerSecond;
    double loopEnd = loopStart + loopLengthBeats;

    auto noteCanDisplay = [&](const MidiNote& note) {
        const double noteStart = note.startBeat;
        const double noteEnd = note.startBeat + note.lengthBeats;

        if (clip.loopEnabled && loopLengthBeats > 0.0)
            return noteEnd > loopStart && noteStart < loopEnd;

        const double displayStart = note.startBeat - midiOffset;
        const double displayEnd = displayStart + note.lengthBeats;
        return displayEnd > 0.0 && displayStart < clipLengthInBeats;
    };

    bool hasVisibleNote = false;
    for (const auto& note : clip.midiNotes) {
        if (!noteCanDisplay(note))
            continue;

        hasVisibleNote = true;
        break;
    }

    if (!hasVisibleNote)
        return;

    constexpr int minNote = MIDI_PREVIEW_MIN_NOTE;
    constexpr int maxNote = MIDI_PREVIEW_MAX_NOTE;
    int noteRange = juce::jmax(12, maxNote - minNote);
    double beatRange = juce::jmax(1.0, clipLengthInBeats);

    if (clip.loopEnabled && loopLengthBeats > 0.0) {
        int numRepetitions = static_cast<int>(std::ceil(clipLengthInBeats / loopLengthBeats));

        for (int rep = 0; rep < numRepetitions; ++rep) {
            for (const auto& note : clip.midiNotes) {
                double sourceStart = juce::jmax(note.startBeat, loopStart);
                double sourceEnd = juce::jmin(note.startBeat + note.lengthBeats, loopEnd);
                double sourceLength = sourceEnd - sourceStart;
                if (sourceLength <= 0.0)
                    continue;

                double noteBeat =
                    loopStart + wrapPhase(sourceStart - midiOffset - loopStart, loopLengthBeats);

                if (noteBeat < loopStart || noteBeat >= loopEnd)
                    continue;

                double displayStart = (noteBeat - loopStart) + rep * loopLengthBeats;
                double displayEnd = displayStart + sourceLength;

                double repEnd = (rep + 1) * loopLengthBeats;
                displayEnd = juce::jmin(displayEnd, repEnd);

                if (displayEnd <= 0.0 || displayStart >= clipLengthInBeats)
                    continue;

                double visibleStart = juce::jmax(0.0, displayStart);
                double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
                double visibleLength = visibleEnd - visibleStart;

                float noteY = noteArea.getY() + static_cast<float>(maxNote - note.noteNumber) /
                                                    (noteRange + 1) * noteArea.getHeight();
                float noteHeight =
                    juce::jmax(2.0f, static_cast<float>(noteArea.getHeight()) / (noteRange + 1));
                float noteX = noteArea.getX() +
                              static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
                float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                       noteArea.getWidth());

                g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
            }
        }
    } else {
        for (const auto& note : clip.midiNotes) {
            double displayStart = note.startBeat - midiOffset;
            double displayEnd = displayStart + note.lengthBeats;

            if (displayEnd <= 0 || displayStart >= clipLengthInBeats)
                continue;

            double visibleStart = juce::jmax(0.0, displayStart);
            double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
            double visibleLength = visibleEnd - visibleStart;

            float noteY = noteArea.getY() + static_cast<float>(maxNote - note.noteNumber) /
                                                (noteRange + 1) * noteArea.getHeight();
            float noteHeight =
                juce::jmax(2.0f, static_cast<float>(noteArea.getHeight()) / (noteRange + 1));
            float noteX = noteArea.getX() +
                          static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
            float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                   noteArea.getWidth());

            g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
        }
    }
}

void ClipComponent::paintClipHeader(juce::Graphics& g, const ClipInfo& clip,
                                    juce::Rectangle<int> bounds) {
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);

    // Selected clips paint a black header in place of the clip-coloured one.
    // This replaces the old white selection rectangle so it can't fight overlay
    // UI (e.g. controller scene-view rectangles).
    const bool selected = isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_);
    const auto headerColour = selected ? juce::Colours::black : clip.colour;
    const auto headerForeground =
        selected ? juce::Colours::white : DarkTheme::getColour(DarkTheme::BACKGROUND);

    auto visibleHeaderArea =
        headerArea.withBottom(headerArea.getBottom() + 2).getIntersection(g.getClipBounds());
    if (visibleHeaderArea.isEmpty())
        return;

    fillClippedRoundedRect(g, headerArea.withBottom(headerArea.getBottom() + 2), visibleHeaderArea,
                           headerColour, CORNER_RADIUS);

    // Clip name
    if (bounds.getWidth() > MIN_WIDTH_FOR_NAME) {
        auto nameArea = headerArea.withWidth(juce::jmin(headerArea.getWidth(), 300)).reduced(4, 0);
        if (nameArea.intersects(g.getClipBounds())) {
            g.setColour(headerForeground);
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(clip.name, nameArea, juce::Justification::centredLeft, true);
        }
    }

    // Musical mode indicator (auto-tempo)
    if (clip.autoTempo && clip.isAudio() && headerArea.getWidth() > 16) {
        auto musicalArea = headerArea.removeFromRight(14).reduced(2);
        g.setColour(headerForeground);
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xa9"), musicalArea,
                   juce::Justification::centred, false);
    }

    // Loop indicator (infinito/infinity icon).
    // Cache one drawable per foreground variant — selection flips foreground,
    // so we can't bake a single colour at construction.
    if (clip.loopEnabled && headerArea.getWidth() > 16) {
        headerArea.removeFromRight(2);  // right padding
        auto loopArea = headerArea.removeFromRight(14).reduced(1);
        if (loopArea.getWidth() > 0 && loopArea.getHeight() > 0) {
            static auto makeIcon = [](juce::Colour fg) {
                auto icon = juce::Drawable::createFromImageData(BinaryData::infinito_svg,
                                                                BinaryData::infinito_svgSize);
                if (icon)
                    icon->replaceColour(juce::Colour(0xFFB3B3B3), fg);
                return icon;
            };
            static auto normalIcon = makeIcon(DarkTheme::getColour(DarkTheme::BACKGROUND));
            static auto selectedIcon = makeIcon(juce::Colours::white);
            const auto& icon = selected ? selectedIcon : normalIcon;
            if (icon)
                icon->drawWithin(g, loopArea.toFloat(), juce::RectanglePlacement::centred, 1.0f);
        }
    }
}

void ClipComponent::paintResizeHandles(juce::Graphics& g, juce::Rectangle<int> bounds) {
    auto handleColour = juce::Colours::white.withAlpha(0.5f);

    // Left handle
    auto leftHandle = bounds.removeFromLeft(RESIZE_HANDLE_WIDTH);
    if (hoverLeftEdge_) {
        g.setColour(handleColour);
        g.fillRect(leftHandle);
    }

    // Right handle
    auto rightHandle = bounds.removeFromRight(RESIZE_HANDLE_WIDTH);
    if (hoverRightEdge_) {
        g.setColour(handleColour);
        g.fillRect(rightHandle);
    }
}

void ClipComponent::paintFadeOverlays(juce::Graphics& g, const ClipInfo& clip,
                                      juce::Rectangle<int> waveformArea, double pixelsPerSecond) {
    constexpr int NUM_STEPS = 32;
    float areaTop = static_cast<float>(waveformArea.getY());
    float areaBottom = static_cast<float>(waveformArea.getBottom());
    float areaHeight = areaBottom - areaTop;
    float areaLeft = static_cast<float>(waveformArea.getX());
    float areaRight = static_cast<float>(waveformArea.getRight());

    // Fade-in overlay
    if (clip.fadeIn > 0.0) {
        float fadeInPx = juce::jmin(static_cast<float>(clip.fadeIn * pixelsPerSecond),
                                    static_cast<float>(waveformArea.getWidth()));
        if (fadeInPx > 1.0f) {
            // Build overlay path: darkens area above the fade curve
            juce::Path overlay;
            overlay.startNewSubPath(areaLeft, areaTop);
            overlay.lineTo(areaLeft + fadeInPx, areaTop);

            // Trace the fade curve from right to left (gain 1→0)
            for (int i = NUM_STEPS; i >= 0; --i) {
                float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
                float gain = computeFadeGain(alpha, static_cast<FadeCurve>(clip.fadeInType));
                float x = areaLeft + alpha * fadeInPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                overlay.lineTo(x, y);
            }
            overlay.closeSubPath();

            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillPath(overlay);

            // Stroke the fade curve line
            juce::Path curveLine;
            for (int i = 0; i <= NUM_STEPS; ++i) {
                float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
                float gain = computeFadeGain(alpha, static_cast<FadeCurve>(clip.fadeInType));
                float x = areaLeft + alpha * fadeInPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                if (i == 0)
                    curveLine.startNewSubPath(x, y);
                else
                    curveLine.lineTo(x, y);
            }
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.strokePath(curveLine, juce::PathStrokeType(1.5f));
        }
    }

    // Fade-out overlay
    if (clip.fadeOut > 0.0) {
        float fadeOutPx = juce::jmin(static_cast<float>(clip.fadeOut * pixelsPerSecond),
                                     static_cast<float>(waveformArea.getWidth()));
        if (fadeOutPx > 1.0f) {
            float fadeStart = areaRight - fadeOutPx;

            // Build overlay path: darkens area above the fade curve
            juce::Path overlay;
            overlay.startNewSubPath(fadeStart, areaTop);
            overlay.lineTo(areaRight, areaTop);
            // Right edge down to bottom (gain = 0 at right edge)
            overlay.lineTo(areaRight, areaBottom);

            // Trace the fade curve from right to left (gain 0→1)
            for (int i = NUM_STEPS; i >= 0; --i) {
                float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
                // alpha=0 at fadeStart (gain=1), alpha=1 at areaRight (gain=0)
                float gain =
                    computeFadeGain(1.0f - alpha, static_cast<FadeCurve>(clip.fadeOutType));
                float x = fadeStart + alpha * fadeOutPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                overlay.lineTo(x, y);
            }
            overlay.closeSubPath();

            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillPath(overlay);

            // Stroke the fade curve line
            juce::Path curveLine;
            for (int i = 0; i <= NUM_STEPS; ++i) {
                float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
                float gain =
                    computeFadeGain(1.0f - alpha, static_cast<FadeCurve>(clip.fadeOutType));
                float x = fadeStart + alpha * fadeOutPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                if (i == 0)
                    curveLine.startNewSubPath(x, y);
                else
                    curveLine.lineTo(x, y);
            }
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.strokePath(curveLine, juce::PathStrokeType(1.5f));
        }
    }
}

void ClipComponent::paintFadeHandles(juce::Graphics& g, const ClipInfo& clip,
                                     juce::Rectangle<int> bounds) {
    auto waveformArea = bounds.reduced(2, 0).withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2);
    if (waveformArea.getWidth() <= 0 || waveformArea.getHeight() <= 0)
        return;

    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    double clipDisplayLength = clip.getTimelineLength(tempo);
    double pixelsPerSecond = (clipDisplayLength > 0.0)
                                 ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                                 : 0.0;
    if (pixelsPerSecond <= 0.0)
        return;

    float hs = static_cast<float>(FADE_HANDLE_SIZE);
    float half = hs * 0.5f;
    float waveTop = static_cast<float>(waveformArea.getY());

    auto handleColour = juce::Colour(DarkTheme::ACCENT_ORANGE);

    // Fade-in handle: only visible on hover
    if (hoverFadeIn_) {
        float fadeInPx = static_cast<float>(clip.fadeIn * pixelsPerSecond);
        float cx = static_cast<float>(waveformArea.getX()) + fadeInPx;
        g.setColour(handleColour);
        g.fillRect(cx - half, waveTop, hs, hs);
    }

    // Fade-out handle: only visible on hover
    if (hoverFadeOut_) {
        float fadeOutPx = static_cast<float>(clip.fadeOut * pixelsPerSecond);
        float cx = static_cast<float>(waveformArea.getRight()) - fadeOutPx;
        g.setColour(handleColour);
        g.fillRect(cx - half, waveTop, hs, hs);
    }
}

void ClipComponent::paintVolumeLine(juce::Graphics& g, const ClipInfo& clip,
                                    juce::Rectangle<int> waveformArea) {
    if (waveformArea.getWidth() <= 0 || waveformArea.getHeight() <= 0)
        return;

    float gainLinear = juce::Decibels::decibelsToGain(clip.volumeDB);
    gainLinear = juce::jlimit(0.0f, 1.0f, gainLinear);

    // Y position: top = 0 dB (unity/full), bottom = -inf (silence)
    float lineY = static_cast<float>(waveformArea.getY()) +
                  (1.0f - gainLinear) * static_cast<float>(waveformArea.getHeight());

    // Draw the gain line
    auto lineColour = juce::Colours::white.withAlpha(
        hoverVolumeHandle_ || dragMode_ == DragMode::VolumeDrag ? 0.8f : 0.4f);
    g.setColour(lineColour);
    auto visibleWaveformArea = waveformArea.getIntersection(g.getClipBounds());
    if (visibleWaveformArea.isEmpty())
        return;
    g.drawHorizontalLine(static_cast<int>(lineY), static_cast<float>(visibleWaveformArea.getX()),
                         static_cast<float>(visibleWaveformArea.getRight()));

    // Show dB text during drag
    if (dragMode_ == DragMode::VolumeDrag) {
        juce::String dbText;
        if (clip.volumeDB <= -100.0f)
            dbText = "-inf dB";
        else
            dbText = juce::String(clip.volumeDB, 1) + " dB";
        g.setColour(juce::Colours::white);
        g.setFont(10.0f);
        g.drawText(dbText, waveformArea.getX() + 4, static_cast<int>(lineY) - 14, 60, 14,
                   juce::Justification::centredLeft);
    }
}

void ClipComponent::resized() {
    // Nothing to do - clip bounds are set by parent
}

bool ClipComponent::hitTest(int x, int y) {
    if (x < 0 || x >= getWidth() || y < 0 || y >= getHeight())
        return false;

    // Be transparent to a plain (unmodified, non-edge) click that lands on an
    // active time selection covering this clip, so the gesture goes straight to
    // the panel's time-selection machinery and the panel owns the drag. Routing
    // it through this component instead breaks mid-drag: splitting at the
    // selection boundaries rebuilds (destroys) every ClipComponent, killing the
    // drag (you had to drag twice). Clip resize edges and modified clicks
    // (copy/select/blade/erase/context menu) still hit the clip.
    if (parentPanel_ != nullptr) {
        const auto mods = juce::ModifierKeys::getCurrentModifiers();
        if (!mods.isAnyModifierKeyDown() && !mods.isPopupMenu() && !isOnLeftEdge(x) &&
            !isOnRightEdge(x)) {
            const int panelX = getX() + x;
            const int panelY = getY() + y;
            if (parentPanel_->pointInTimeSelection(panelX, panelY) ||
                parentPanel_->pointOnTimeSelectionEdge(panelX, panelY)) {
                return false;
            }
        }
    }

    return true;
}

// ============================================================================
// Mouse Handling
// ============================================================================

void ClipComponent::mouseDown(const juce::MouseEvent& e) {
    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    // Check if track is frozen
    auto* trackInfoForFreeze = TrackManager::getInstance().getTrack(clip->trackId);
    bool isFrozen = trackInfoForFreeze && trackInfoForFreeze->frozen;

    // Ensure parent panel has keyboard focus so shortcuts work
    if (parentPanel_) {
        parentPanel_->grabKeyboardFocus();
    }

    auto& selectionManager = SelectionManager::getInstance();
    bool isAlreadySelected = selectionManager.isClipSelected(clipId_);

    // Helper: ensure editor panel is open for the current clip type
    auto ensureEditorOpen = [](ClipId id) {
        const auto* c = ClipManager::getInstance().getClip(id);
        if (!c)
            return;
        auto& pc = daw::ui::PanelController::getInstance();
        pc.setCollapsed(daw::ui::PanelLocation::Bottom, false);
        // Don't force a specific MIDI editor tab — BottomPanel's clipSelectionChanged
        // handles the PianoRoll vs DrumGrid choice, respecting the user's preference.
        if (c->isAudio()) {
            pc.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                  daw::ui::PanelContentType::WaveformEditor);
        }
    };

    const bool isModifiedSelectionClick = magda::isToggleSelectClick(e.mods) && !e.mods.isAltDown();
    const bool isBladeClick = e.mods.isAltDown() && e.mods.isCommandDown() && !e.mods.isShiftDown();
    const bool isEraseClick = e.mods.isShiftDown() && e.mods.isCtrlDown();

    if (e.mods.isShiftDown()) {
        logArrangeRangeSelect(
            "ClipComponent::mouseDown clip=" + juce::String(static_cast<int>(clipId_)) +
            " track=" + juce::String(static_cast<int>(clip->trackId)) + " x=" + juce::String(e.x) +
            " y=" + juce::String(e.y) + " shift=" + juce::String(e.mods.isShiftDown() ? 1 : 0) +
            " cmd=" + juce::String(e.mods.isCommandDown() ? 1 : 0) +
            " ctrl=" + juce::String(e.mods.isCtrlDown() ? 1 : 0) +
            " alt=" + juce::String(e.mods.isAltDown() ? 1 : 0) +
            " popup=" + juce::String(e.mods.isPopupMenu() ? 1 : 0) + " alreadySelected=" +
            juce::String(isAlreadySelected ? 1 : 0) + " selectedCountBefore=" +
            juce::String(static_cast<int>(selectionManager.getSelectedClipCount())) +
            " anchorBefore=" + juce::String(static_cast<int>(selectionManager.getAnchorClip())) +
            " frozen=" + juce::String(isFrozen ? 1 : 0) +
            " rangePolicy=" + juce::String(magda::isRangeSelectClick(e.mods) ? 1 : 0) +
            " erasePolicy=" + juce::String(isEraseClick ? 1 : 0) +
            " leftEdge=" + juce::String(isOnLeftEdge(e.x) ? 1 : 0) +
            " rightEdge=" + juce::String(isOnRightEdge(e.x) ? 1 : 0) +
            " fadeIn=" + juce::String(isOnFadeInHandle(e.x, e.y) ? 1 : 0) +
            " fadeOut=" + juce::String(isOnFadeOutHandle(e.x, e.y) ? 1 : 0) +
            " volume=" + juce::String(isOnVolumeHandle(e.x, e.y) ? 1 : 0));
    }

    // Frozen tracks: allow selection (so piano roll shows content) but block editing
    if (isFrozen && (!e.mods.isPopupMenu() || isModifiedSelectionClick)) {
        // Still allow click-to-select and modifier-click toggle
        if (isModifiedSelectionClick) {
            if (e.mods.isShiftDown())
                logArrangeRangeSelect("ClipComponent frozen branch: toggle selection");
            selectionManager.toggleClipSelection(clipId_);
        } else if (magda::isRangeSelectClick(e.mods)) {
            logArrangeRangeSelect("ClipComponent frozen branch: extending range to clip=" +
                                  juce::String(static_cast<int>(clipId_)));
            selectionManager.extendSelectionTo(clipId_);
        } else {
            if (e.mods.isShiftDown())
                logArrangeRangeSelect("ClipComponent frozen branch: plain select fallback");
            selectionManager.selectClip(clipId_);
        }
        isSelected_ = selectionManager.isClipSelected(clipId_);
        ensureEditorOpen(clipId_);
        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    // Lower half of the clip body is a time-selection zone, just like empty lane
    // space: forward a plain click there to the panel so it draws an I-beam time
    // selection instead of grabbing/moving the clip. (Grabbing an *existing*
    // selection is handled earlier by hitTest making this component transparent,
    // so the panel owns that drag directly.) Edges keep resize priority, and the
    // clip-op modifiers (select/copy/blade/erase/context) are handled elsewhere.
    const bool plainLowerZoneClick =
        parentPanel_ != nullptr && e.y >= getHeight() / 2 && !isOnLeftEdge(e.x) &&
        !isOnRightEdge(e.x) && !e.mods.isAltDown() && !e.mods.isCommandDown() &&
        !e.mods.isCtrlDown() && !e.mods.isShiftDown() && !e.mods.isPopupMenu();
    if (plainLowerZoneClick) {
        dragMode_ = DragMode::None;
        forwardingToPanel_ = true;
        parentPanel_->forwardLowerZoneMouseDown(e.getEventRelativeTo(parentPanel_));
        return;
    }

    // Shift+Ctrl-click acts as an eraser for the clip under the cursor. If the
    // clicked clip is part of a multi-selection, erase the selected group.
    if (isEraseClick) {
        logArrangeRangeSelect("ClipComponent erase branch: Shift+Ctrl consumed for delete");
        std::vector<ClipId> clipIds;
        const auto& selected = selectionManager.getSelectedClips();
        if (selected.count(clipId_) && selected.size() > 1) {
            clipIds.assign(selected.begin(), selected.end());
        } else {
            clipIds.push_back(clipId_);
        }

        dragMode_ = DragMode::None;
        juce::MessageManager::callAsync([clipIds = std::move(clipIds)]() {
            if (clipIds.size() > 1)
                UndoManager::getInstance().beginCompoundOperation("Delete Clips");

            for (auto id : clipIds) {
                UndoManager::getInstance().executeCommand(std::make_unique<DeleteClipCommand>(id));
            }

            if (clipIds.size() > 1)
                UndoManager::getInstance().endCompoundOperation();

            SelectionManager::getInstance().clearSelection();
        });
        return;
    }

    // Cmd-click toggles clip selection without starting a drag.
    if (isModifiedSelectionClick) {
        if (e.mods.isShiftDown())
            logArrangeRangeSelect("ClipComponent modified-selection branch: toggle selection");
        selectionManager.toggleClipSelection(clipId_);
        isSelected_ = selectionManager.isClipSelected(clipId_);

        if (isSelected_) {
            ensureEditorOpen(clipId_);
        }

        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    // Shift+edge = stretch (falls through to drag setup); Shift+body = range
    // select from the anchor, applied immediately. Dragging afterwards moves
    // the selected range via the multi-drag path.
    bool didRangeSelect = false;
    pendingAltAction_ = false;
    if (e.mods.isShiftDown()) {
        if (magda::isRangeSelectClick(e.mods)) {
            logArrangeRangeSelect("ClipComponent range branch: extending to clip=" +
                                  juce::String(static_cast<int>(clipId_)) + " edgeHit=" +
                                  juce::String((isOnLeftEdge(e.x) || isOnRightEdge(e.x)) ? 1 : 0));
            selectionManager.extendSelectionTo(clipId_);
            didRangeSelect = true;
            isSelected_ = selectionManager.isClipSelected(clipId_);
            logArrangeRangeSelect(
                "ClipComponent range branch complete: selectedNow=" +
                juce::String(isSelected_ ? 1 : 0) + " selectedCountAfter=" +
                juce::String(static_cast<int>(selectionManager.getSelectedClipCount())) +
                " anchorAfter=" + juce::String(static_cast<int>(selectionManager.getAnchorClip())));
            if (isSelected_) {
                ensureEditorOpen(clipId_);
            }
            logArrangeRangeSelect(
                "ClipComponent range branch after editor-open: selectedCount=" +
                juce::String(static_cast<int>(selectionManager.getSelectedClipCount())) +
                " anchor=" + juce::String(static_cast<int>(selectionManager.getAnchorClip())));
        }
    } else if (e.mods.isAltDown() && !e.mods.isCommandDown()) {
        // Alt+body: copy on drag, edit cursor on click — both resolve later,
        // so the selection stays untouched for now
        pendingAltAction_ = true;
    }

    // Handle Cmd+Alt+click for blade/split (click-only gesture, no drag)
    if (isBladeClick) {
        // Calculate split time from click position
        if (parentPanel_) {
            auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
            double splitTime = parentPanel_->pixelToTime(parentPos.x);

            // Apply snap if available
            if (snapTimeToGrid) {
                splitTime = snapTimeToGrid(splitTime);
            }

            // Verify split time is within clip bounds
            const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
            const double clipStart = clip->getTimelineStart(tempo);
            const double clipEnd = clipStart + clip->getTimelineLength(tempo);
            if (splitTime > clipStart && splitTime < clipEnd) {
                if (onClipSplit) {
                    onClipSplit(clipId_, splitTime);
                }
            }
        }
        dragMode_ = DragMode::None;
        return;
    }

    // If clicking on a clip that's already part of a multi-selection,
    // keep the selection and prepare for potential multi-drag
    size_t selectedCount = selectionManager.getSelectedClipCount();

    if (pendingAltAction_) {
        // Selection deferred: drag start copies, plain release places the edit cursor
    } else if (didRangeSelect) {
        logArrangeRangeSelect("ClipComponent preserving range selection through normal click path");
        isSelected_ = selectionManager.isClipSelected(clipId_);
    } else if (isAlreadySelected && selectedCount > 1) {
        isSelected_ = true;
        shouldDeselectOnMouseUp_ = true;
    } else {
        if (e.mods.isShiftDown()) {
            logArrangeRangeSelect(
                "ClipComponent normal select fallback after Shift; this should only "
                "happen for non-range Shift gestures");
        }
        selectionManager.selectClip(clipId_);
        isSelected_ = true;

        // Notify parent to update piano roll
        if (onClipSelected) {
            onClipSelected(clipId_);
        }
    }

    // Store drag start info - use parent's coordinate space so position
    // is stable when we move the component via setBounds()
    if (parentPanel_) {
        dragStartPos_ = e.getEventRelativeTo(parentPanel_).getPosition();
    } else {
        dragStartPos_ = e.getPosition();
    }
    dragStartBoundsPos_ = getBounds().getPosition();
    {
        const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
        dragStartTime_ = clip->getTimelineStart(tempo);
        dragStartLength_ = clip->getTimelineLength(tempo);
    }
    dragStartTrackId_ = clip->trackId;
    dragStartAudioOffset_ = clip->offset;

    // Cache file duration for resize clamping
    dragStartFileDuration_ = 0.0;
    if (clip->isAudio() && clip->audio().source.filePath.isNotEmpty()) {
        auto* thumbnail =
            AudioThumbnailManager::getInstance().getThumbnail(clip->audio().source.filePath);
        if (thumbnail)
            dragStartFileDuration_ = thumbnail->getTotalLength();
    }

    // Initialize preview state
    previewStartTime_ = dragStartTime_;
    previewLength_ = dragStartLength_;
    isDragging_ = false;

    // Determine drag mode based on click position
    // Fade handles take priority over resize edges (they check y-range, edges don't)
    if (isSelected_ && isOnFadeInHandle(e.x, e.y)) {
        if (e.mods.isShiftDown()) {
            // Shift+click: cycle fade-in type (1→2→3→4→1)
            int newType = (clip->fadeInType % 4) + 1;
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetClipFadeInTypeCommand>(clipId_, newType));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        dragMode_ = DragMode::FadeIn;
        dragStartFadeIn_ = clip->fadeIn;
        dragStartClipSnapshot_ = *clip;
        // Capture selected clips' state for multi-fade
        dragStartSelectedFadeSnapshots_.clear();
        const auto& selected = SelectionManager::getInstance().getSelectedClips();
        if (selected.size() > 1 && selected.count(clipId_)) {
            auto& cm = ClipManager::getInstance();
            for (auto cid : selected) {
                if (cid == clipId_)
                    continue;
                const auto* c = cm.getClip(cid);
                if (c && c->isAudio())
                    dragStartSelectedFadeSnapshots_[cid] = *c;
            }
        }
        repaint();
        return;
    }
    if (isSelected_ && isOnFadeOutHandle(e.x, e.y)) {
        if (e.mods.isShiftDown()) {
            // Shift+click: cycle fade-out type (1→2→3→4→1)
            int newType = (clip->fadeOutType % 4) + 1;
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetClipFadeOutTypeCommand>(clipId_, newType));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        dragMode_ = DragMode::FadeOut;
        dragStartFadeOut_ = clip->fadeOut;
        dragStartClipSnapshot_ = *clip;
        // Capture selected clips' state for multi-fade
        dragStartSelectedFadeSnapshots_.clear();
        const auto& selected = SelectionManager::getInstance().getSelectedClips();
        if (selected.size() > 1 && selected.count(clipId_)) {
            auto& cm = ClipManager::getInstance();
            for (auto cid : selected) {
                if (cid == clipId_)
                    continue;
                const auto* c = cm.getClip(cid);
                if (c && c->isAudio())
                    dragStartSelectedFadeSnapshots_[cid] = *c;
            }
        }
        repaint();
        return;
    }

    // Volume handle (top edge of waveform area, audio clips only)
    if (isSelected_ && isOnVolumeHandle(e.x, e.y)) {
        dragMode_ = DragMode::VolumeDrag;
        dragStartVolumeDB_ = clip->volumeDB;
        dragStartClipSnapshot_ = *clip;
        // Capture selected clips' state for multi-volume
        dragStartSelectedFadeSnapshots_.clear();
        const auto& selected = SelectionManager::getInstance().getSelectedClips();
        if (selected.size() > 1 && selected.count(clipId_)) {
            auto& cm = ClipManager::getInstance();
            for (auto cid : selected) {
                if (cid == clipId_)
                    continue;
                const auto* c = cm.getClip(cid);
                if (c && c->isAudio())
                    dragStartSelectedFadeSnapshots_[cid] = *c;
            }
        }
        repaint();
        return;
    }

    // Shift+edge = stretch mode (time-stretches audio source or scales MIDI notes)
    if (isOnLeftEdge(e.x)) {
        if (e.mods.isShiftDown() &&
            ((clip->isAudio() && clip->audio().source.filePath.isNotEmpty()) || clip->isMidi())) {
            dragMode_ = DragMode::StretchLeft;
            dragStartSpeedRatio_ = clip->speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeLeft;
            dragStartClipSnapshot_ = *clip;
            resizePreviewClip_ = *clip;
        }
    } else if (isOnRightEdge(e.x)) {
        if (e.mods.isShiftDown() &&
            ((clip->isAudio() && clip->audio().source.filePath.isNotEmpty()) || clip->isMidi())) {
            dragMode_ = DragMode::StretchRight;
            dragStartSpeedRatio_ = clip->speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeRight;
            dragStartClipSnapshot_ = *clip;
            // Capture original lengths of other selected clips for multi-resize
            dragStartSelectedLengths_.clear();
            dragStartSelectedClipSnapshots_.clear();
            multiResizeMaxDelta_ = std::numeric_limits<double>::max();
            const auto& selected = SelectionManager::getInstance().getSelectedClips();
            if (selected.size() > 1 && selected.count(clipId_)) {
                auto& cm = ClipManager::getInstance();
                const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                for (auto cid : selected) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;
                    if (cid != clipId_) {
                        dragStartSelectedLengths_[cid] = timelineLengthSeconds(*c, tempo);
                        dragStartSelectedClipSnapshots_[cid] = *c;
                    }

                    // Find max resize before hitting next non-selected clip
                    auto trackClips = cm.getClipsOnTrack(c->trackId);
                    const double cStart = timelineStartSeconds(*c, tempo);
                    const double cEnd = timelineEndSeconds(*c, tempo);
                    for (auto otherId : trackClips) {
                        if (selected.count(otherId))
                            continue;
                        const auto* other = cm.getClip(otherId);
                        if (other && timelineStartSeconds(*other, tempo) > cStart) {
                            double gap = timelineStartSeconds(*other, tempo) - cEnd;
                            multiResizeMaxDelta_ = juce::jmin(multiResizeMaxDelta_, gap);
                        }
                    }
                }
            }
        }
    } else {
        dragMode_ = DragMode::Move;
    }

    // Bring to front so the dragged/resized clip renders on top of neighbours
    if (dragMode_ != DragMode::None)
        toFront(false);

    repaint();
}

void ClipComponent::mouseDrag(const juce::MouseEvent& e) {
    // Lower-zone time-selection gesture: keep driving the panel's selection.
    if (forwardingToPanel_) {
        if (parentPanel_)
            parentPanel_->forwardLowerZoneMouseDrag(e.getEventRelativeTo(parentPanel_));
        return;
    }

    if (dragMode_ == DragMode::None || !parentPanel_) {
        return;
    }

    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    // Block editing on frozen tracks
    auto* trackInfoDrag = TrackManager::getInstance().getTrack(clip->trackId);
    if (trackInfoDrag && trackInfoDrag->frozen) {
        return;
    }

    // Force the grid overlay to repaint cleanly for this drag tick. Moving the
    // clip via setBounds only invalidates the clip's own region; the overlay
    // sibling stacked above the viewport otherwise keeps stale grid lines over
    // the area the clip just left (a trail, most visible with audio waveforms).
    if (parentPanel_ && parentPanel_->onClipDragOverlayRepaint)
        parentPanel_->onClipDragOverlayRepaint();

    // A pending Alt action resolves to copy-drag once a real drag starts
    // (a plain Alt release places the edit cursor in mouseUp instead)
    if (pendingAltAction_) {
        if (e.getDistanceFromDragStart() < 4)
            return;  // still a click
        pendingAltAction_ = false;
        auto& sm = SelectionManager::getInstance();
        const bool partOfMultiSelection =
            sm.getSelectedClipCount() > 1 && sm.isClipSelected(clipId_);
        if (dragMode_ == DragMode::Move && !partOfMultiSelection) {
            if (!sm.isClipSelected(clipId_)) {
                sm.selectClip(clipId_);
                isSelected_ = true;
                if (onClipSelected) {
                    onClipSelected(clipId_);
                }
            }
            isDuplicating_ = true;
        }
    }

    // Check if this is a multi-clip drag
    auto& selectionManager = SelectionManager::getInstance();
    bool isMultiDrag = dragMode_ == DragMode::Move && selectionManager.getSelectedClipCount() > 1 &&
                       selectionManager.isClipSelected(clipId_);

    if (isMultiDrag) {
        // Delegate to parent for coordinated multi-clip movement
        if (!isDragging_) {
            // First drag event - start multi-clip drag
            parentPanel_->startMultiClipDrag(clipId_,
                                             e.getEventRelativeTo(parentPanel_).getPosition());
            isDragging_ = true;
        } else {
            // Continue multi-clip drag
            parentPanel_->updateMultiClipDrag(e.getEventRelativeTo(parentPanel_).getPosition());
        }
        return;
    }

    // Single clip drag logic
    isDragging_ = true;

    // Convert pixel delta to time delta
    // getZoom() returns pixels per beat (ppb)
    double pixelsPerBeat = parentPanel_->getZoom();
    if (pixelsPerBeat <= 0) {
        return;
    }
    double tempoBPM = parentPanel_->getTempo();

    // Use parent's coordinate space for stable delta calculation
    // (component position changes during drag, but parent doesn't move)
    auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
    int deltaX = parentPos.x - dragStartPos_.x;
    // deltaX / ppb = deltaBeats, then convert to seconds
    double deltaBeats = deltaX / pixelsPerBeat;
    double deltaTime = deltaBeats * 60.0 / tempoBPM;

    switch (dragMode_) {
        case DragMode::Move: {
            // Work entirely in time domain, then convert to pixels at the end
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double finalTime = rawStartTime;

            // Magnetic snap: if close to grid, snap to it
            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaBeats = std::abs((snappedTime - rawStartTime) * tempoBPM / 60.0);
                double snapDeltaPixels = snapDeltaBeats * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalTime = snappedTime;
                }
            }

            previewStartTime_ = finalTime;

            if (isDuplicating_) {
                // Alt+drag duplicate: show ghost at NEW position, keep original in place
                const auto* clip = getClipInfo();
                if (clip && parentPanel_) {
                    double finalBeats = finalTime * tempoBPM / 60.0;
                    int ghostX = parentPanel_->beatsToPixel(finalBeats);
                    double lengthBeats = dragStartLength_ * tempoBPM / 60.0;
                    int ghostWidth = static_cast<int>(std::round(lengthBeats * pixelsPerBeat));
                    juce::Rectangle<int> ghostBounds(ghostX, getY(), juce::jmax(10, ghostWidth),
                                                     getHeight());
                    parentPanel_->setClipGhost(clipId_, ghostBounds, clip->colour);
                }
                // Don't move the original clip component
            } else {
                // Normal move: update component position
                double finalBeats = finalTime * tempoBPM / 60.0;
                int newX = parentPanel_->beatsToPixel(finalBeats);
                double lengthBeats = dragStartLength_ * tempoBPM / 60.0;
                int newWidth = static_cast<int>(std::round(lengthBeats * pixelsPerBeat));
                setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

                // Show ghost on target track when dragging across tracks
                auto screenPos = e.getScreenPosition();
                auto parentPos = parentPanel_->getScreenBounds().getPosition();
                int localY = screenPos.y - parentPos.y;
                int trackIndex = parentPanel_->getTrackIndexAtY(localY);

                if (trackIndex >= 0) {
                    auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
                        ViewModeController::getInstance().getViewMode());

                    if (trackIndex < static_cast<int>(visibleTracks.size()) &&
                        visibleTracks[trackIndex] != dragStartTrackId_) {
                        // Over a different track — show ghost
                        int targetY = parentPanel_->getTrackYPosition(trackIndex);
                        int targetH = parentPanel_->getTrackTotalHeight(trackIndex);
                        const auto* clip = getClipInfo();
                        juce::Rectangle<int> ghostBounds(newX, targetY, juce::jmax(10, newWidth),
                                                         targetH);
                        parentPanel_->setClipGhost(clipId_, ghostBounds,
                                                   clip ? clip->colour : juce::Colours::grey);
                    } else {
                        // Back on source track — clear ghost
                        parentPanel_->clearClipGhost(clipId_);
                    }
                } else {
                    // Outside any track — clear ghost
                    parentPanel_->clearClipGhost(clipId_);
                }
            }
            break;
        }

        case DragMode::ResizeLeft: {
            // Work in beats domain: deltaBeats is already computed above
            double dragStartBeats = dragStartTime_ * tempoBPM / 60.0;
            double dragStartLenBeats = dragStartLength_ * tempoBPM / 60.0;
            double rawStartBeats = juce::jmax(0.0, dragStartBeats + deltaBeats);
            double endBeats = dragStartBeats + dragStartLenBeats;  // End stays fixed
            double finalStartBeats = rawStartBeats;

            // Magnetic snap for left edge (snap works in seconds, convert)
            if (snapTimeToGrid) {
                double rawStartTime = finalStartBeats * 60.0 / tempoBPM;
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snappedBeats = snappedTime * tempoBPM / 60.0;
                double snapDeltaPixels = std::abs(snappedBeats - finalStartBeats) * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalStartBeats = snappedBeats;
                }
            }

            // Ensure minimum length (0.1 seconds in beats)
            double minLenBeats = 0.1 * tempoBPM / 60.0;
            finalStartBeats = juce::jmin(finalStartBeats, endBeats - minLenBeats);
            double finalLenBeats = endBeats - finalStartBeats;

            // Convert to seconds for ClipOperations
            double finalStartTime = finalStartBeats * 60.0 / tempoBPM;
            double finalLength = finalLenBeats * 60.0 / tempoBPM;

            // Clamp to file duration for non-looped audio clips
            if (dragStartFileDuration_ > 0.0 && !clip->loopEnabled) {
                double maxLength = dragStartLength_ + dragStartAudioOffset_ * dragStartSpeedRatio_;
                if (finalLength > maxLength) {
                    finalLength = maxLength;
                    finalStartTime = (dragStartTime_ + dragStartLength_) - finalLength;
                    finalStartBeats = finalStartTime * tempoBPM / 60.0;
                    finalLenBeats = finalLength * tempoBPM / 60.0;
                }
            }

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            // Compute preview clip from scratch (single source of truth)
            resizePreviewClip_ = dragStartClipSnapshot_;
            ClipOperations::resizeContainerFromLeft(resizePreviewClip_, finalLength, tempoBPM);
            if (!resizePreviewClip_.loopEnabled && resizePreviewClip_.isAudio()) {
                resizePreviewClip_.loopStart = resizePreviewClip_.offset;
            }

            // Throttled: sync to TE for waveform/audio playback
            if (resizeThrottle_.check()) {
                auto& cm = magda::ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    ClipOperations::setTimelinePlacement(
                        *mutableClip, timelineStartSeconds(resizePreviewClip_, tempoBPM),
                        timelineLengthSeconds(resizePreviewClip_, tempoBPM), tempoBPM);
                    mutableClip->offset = resizePreviewClip_.offset;
                    mutableClip->loopStart = resizePreviewClip_.loopStart;
                    mutableClip->midiOffset = resizePreviewClip_.midiOffset;
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }

            // Position using beats domain (matches updateClipComponentPositions)
            int newX = parentPanel_->beatsToPixel(finalStartBeats);
            int newWidth = static_cast<int>(std::round(finalLenBeats * pixelsPerBeat));
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        case DragMode::ResizeRight: {
            // Work in time domain: resizing from right changes length only
            double rawEndTime = dragStartTime_ + dragStartLength_ + deltaTime;
            double finalEndTime = rawEndTime;

            // Magnetic snap for right edge (end time)
            if (snapTimeToGrid) {
                double snappedEndTime = snapTimeToGrid(rawEndTime);
                double snapDeltaBeats = std::abs((snappedEndTime - rawEndTime) * tempoBPM / 60.0);
                double snapDeltaPixels = snapDeltaBeats * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalEndTime = snappedEndTime;
                }
            }

            // Ensure minimum length
            double finalLength = juce::jmax(0.1, finalEndTime - dragStartTime_);

            // Clamp to file duration for non-looped audio clips (can't resize past file end)
            if (dragStartFileDuration_ > 0.0 && !clip->loopEnabled) {
                double maxLength =
                    (dragStartFileDuration_ - dragStartAudioOffset_) * dragStartSpeedRatio_;
                finalLength = juce::jmin(finalLength, maxLength);
            }

            // Clamp to avoid overlapping next non-selected clip
            if (!dragStartSelectedLengths_.empty()) {
                double maxLength = dragStartLength_ + multiResizeMaxDelta_;
                finalLength = juce::jmin(finalLength, maxLength);
            }

            previewLength_ = finalLength;

            // Throttled update so waveform editor stays in sync during drag
            if (resizeThrottle_.check()) {
                auto& cm = magda::ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    double lengthDelta = finalLength - dragStartLength_;
                    ClipOperations::resizeContainerFromRight(*mutableClip, finalLength, tempoBPM);

                    std::vector<magda::ClipId> changedClips;
                    changedClips.push_back(clipId_);

                    // Also update other selected clips with the same delta
                    for (auto& [cid, origLen] : dragStartSelectedLengths_) {
                        if (auto* otherClip = cm.getClip(cid)) {
                            double otherLen = juce::jmax(0.1, origLen + lengthDelta);
                            ClipOperations::resizeContainerFromRight(*otherClip, otherLen,
                                                                     tempoBPM);
                            changedClips.push_back(cid);
                        }
                    }

                    cm.forceNotifyMultipleClipPropertiesChanged(changedClips);
                }
            }

            // Position in beats domain (matches updateClipComponentPositions)
            double startBeats = dragStartTime_ * tempoBPM / 60.0;
            int newX = parentPanel_->beatsToPixel(startBeats);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(std::round(finalLengthBeats * pixelsPerBeat));
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        case DragMode::StretchRight: {
            // Shift+right edge: stretch clip proportionally
            double rawEndTime = dragStartTime_ + dragStartLength_ + deltaTime;
            double finalEndTime = rawEndTime;

            if (snapTimeToGrid) {
                double snappedEndTime = snapTimeToGrid(rawEndTime);
                double snapDeltaBeats = std::abs((snappedEndTime - rawEndTime) * tempoBPM / 60.0);
                double snapDeltaPixels = snapDeltaBeats * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalEndTime = snappedEndTime;
                }
            }

            double finalLength = juce::jmax(0.1, finalEndTime - dragStartTime_);

            // Clamp stretch ratio
            double stretchRatio = finalLength / dragStartLength_;
            stretchRatio = juce::jlimit(0.25, 4.0, stretchRatio);
            finalLength = dragStartLength_ * stretchRatio;

            // For audio: compute speed ratio (longer = slower)
            double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;

            previewLength_ = finalLength;

            double startBeatsStrR = dragStartTime_ * tempoBPM / 60.0;
            int newX = parentPanel_->beatsToPixel(startBeatsStrR);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(std::round(finalLengthBeats * pixelsPerBeat));
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    if (mutableClip->isMidi()) {
                        // Scale MIDI notes from original snapshot
                        mutableClip->midiNotes = dragStartClipSnapshot_.midiNotes;
                        ClipOperations::stretchMidiNotes(*mutableClip, stretchRatio);
                        ClipOperations::resizeContainerFromRight(*mutableClip, finalLength,
                                                                 tempoBPM);
                    } else {
                        ClipOperations::stretchAbsolute(*mutableClip, newSpeedRatio, finalLength,
                                                        tempoBPM);
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }
            break;
        }

        case DragMode::FadeIn: {
            auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
            double pps = (dragStartLength_ > 0.0)
                             ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                             : 0.0;
            if (pps > 0.0) {
                double fadeInPx = static_cast<double>(e.x - wfArea.getX());
                double newFadeIn = juce::jmax(0.0, fadeInPx / pps);
                const auto* ci = getClipInfo();
                double maxFadeIn =
                    ci ? timelineLengthSeconds(*ci, tempoBPM) - ci->fadeOut : dragStartLength_;
                newFadeIn = juce::jmin(newFadeIn, juce::jmax(0.0, maxFadeIn));
                double fadeDelta = newFadeIn - dragStartFadeIn_;
                auto& cm = ClipManager::getInstance();
                cm.setFadeIn(clipId_, newFadeIn);
                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;
                    double otherFade = juce::jmax(0.0, snap.fadeIn + fadeDelta);
                    otherFade =
                        juce::jmin(otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, tempoBPM) -
                                                                  c->fadeOut));
                    cm.setFadeIn(cid, otherFade);
                }
                repaint();
            }
            break;
        }

        case DragMode::FadeOut: {
            auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
            double pps = (dragStartLength_ > 0.0)
                             ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                             : 0.0;
            if (pps > 0.0) {
                double fadeOutPx = static_cast<double>(wfArea.getRight() - e.x);
                double newFadeOut = juce::jmax(0.0, fadeOutPx / pps);
                const auto* ci = getClipInfo();
                double maxFadeOut =
                    ci ? timelineLengthSeconds(*ci, tempoBPM) - ci->fadeIn : dragStartLength_;
                newFadeOut = juce::jmin(newFadeOut, juce::jmax(0.0, maxFadeOut));
                double fadeDelta = newFadeOut - dragStartFadeOut_;
                auto& cm = ClipManager::getInstance();
                cm.setFadeOut(clipId_, newFadeOut);
                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;
                    double otherFade = juce::jmax(0.0, snap.fadeOut + fadeDelta);
                    otherFade =
                        juce::jmin(otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, tempoBPM) -
                                                                  c->fadeIn));
                    cm.setFadeOut(cid, otherFade);
                }
                repaint();
            }
            break;
        }

        case DragMode::VolumeDrag: {
            // Convert vertical delta to dB (~1 dB per 2px, up = louder)
            auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
            int deltaY = parentPos.y - dragStartPos_.y;
            float dbDelta = static_cast<float>(-deltaY) * 0.5f;  // Up = louder
            float newGainDB = juce::jlimit(-100.0f, 0.0f, dragStartVolumeDB_ + dbDelta);
            auto& cm = ClipManager::getInstance();
            cm.setClipVolumeDB(clipId_, newGainDB);
            for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                float otherDB = juce::jlimit(-100.0f, 0.0f, snap.volumeDB + dbDelta);
                cm.setClipVolumeDB(cid, otherDB);
            }
            repaint();
            break;
        }

        case DragMode::StretchLeft: {
            // Shift+left edge: stretch from left, right edge stays fixed
            double endTime = dragStartTime_ + dragStartLength_;
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double finalStartTime = rawStartTime;

            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaBeats = std::abs((snappedTime - rawStartTime) * tempoBPM / 60.0);
                double snapDeltaPixels = snapDeltaBeats * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalStartTime = snappedTime;
                }
            }

            finalStartTime = juce::jmin(finalStartTime, endTime - 0.1);
            double finalLength = endTime - finalStartTime;

            // Clamp stretch ratio
            double stretchRatio = finalLength / dragStartLength_;
            stretchRatio = juce::jlimit(0.25, 4.0, stretchRatio);
            finalLength = dragStartLength_ * stretchRatio;
            finalStartTime = endTime - finalLength;

            // For audio: compute speed ratio (longer = slower)
            double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            double finalStartBeatsStrL = finalStartTime * tempoBPM / 60.0;
            int newX = parentPanel_->beatsToPixel(finalStartBeatsStrL);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(std::round(finalLengthBeats * pixelsPerBeat));
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    double rightEdge = dragStartTime_ + dragStartLength_;
                    if (mutableClip->isMidi()) {
                        mutableClip->midiNotes = dragStartClipSnapshot_.midiNotes;
                        ClipOperations::stretchMidiNotes(*mutableClip, stretchRatio);
                        ClipOperations::setTimelinePlacement(*mutableClip, finalStartTime,
                                                             finalLength, tempoBPM);
                    } else {
                        ClipOperations::stretchAbsoluteFromLeft(*mutableClip, newSpeedRatio,
                                                                finalLength, rightEdge, tempoBPM);
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }
            break;
        }

        default:
            break;
    }

    // Emit real-time preview event via ClipManager (for global listeners like PianoRoll)
    ClipManager::getInstance().notifyClipDragPreview(clipId_, previewStartTime_, previewLength_);

    // Also call local callback if set
    if (onClipDragPreview) {
        onClipDragPreview(clipId_, previewStartTime_, previewLength_);
    }
}

void ClipComponent::mouseUp(const juce::MouseEvent& e) {
    // Finish a forwarded lower-zone time-selection gesture on the panel.
    if (forwardingToPanel_) {
        forwardingToPanel_ = false;
        if (parentPanel_)
            parentPanel_->forwardLowerZoneMouseUp(e.getEventRelativeTo(parentPanel_));
        return;
    }

    // Handle right-click for context menu
    if (e.mods.isPopupMenu() && !(e.mods.isShiftDown() && e.mods.isCtrlDown())) {
        showContextMenu();
        return;
    }

    // Alt+click released without a drag: place the edit cursor at the click
    // position (the documented gesture; Cmd+Alt is the blade, Alt+drag copies)
    if (pendingAltAction_) {
        pendingAltAction_ = false;
        if (!isDragging_) {
            if (parentPanel_) {
                auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
                double cursorSeconds = parentPanel_->pixelToTime(parentPos.x);
                if (snapTimeToGrid)
                    cursorSeconds = snapTimeToGrid(cursorSeconds);
                if (auto* controller = TimelineController::getCurrent()) {
                    const double bpm = controller->getState().tempo.bpm;
                    controller->dispatch(SetEditCursorEvent{cursorSeconds * bpm / 60.0});
                }
            }
            dragMode_ = DragMode::None;
            return;
        }
    }

    // Check if we were doing a multi-clip drag
    auto& selectionManager = SelectionManager::getInstance();
    if (isDragging_ && parentPanel_ && selectionManager.getSelectedClipCount() > 1 &&
        selectionManager.isClipSelected(clipId_) && dragMode_ == DragMode::Move) {
        // Finish multi-clip drag via parent
        parentPanel_->finishMultiClipDrag();
        dragMode_ = DragMode::None;
        isDragging_ = false;
        shouldDeselectOnMouseUp_ = false;
        return;
    }

    if (isDragging_ && dragMode_ != DragMode::None) {
        // Clear drag state BEFORE committing so that clipPropertyChanged notifications
        // aren't skipped — this allows the parent to relayout the component to match
        // the committed clip data, preventing a flash of stretched waveform.
        auto savedDragMode = dragMode_;
        dragMode_ = DragMode::None;
        isDragging_ = false;
        isCommitting_ = true;
        const double commitTempoBPM = parentPanel_ ? parentPanel_->getTempo() : 120.0;

        // Now apply snapping and commit to ClipManager
        switch (savedDragMode) {
            case DragMode::Move: {
                // SafePointer guard: overlap resolution during move/duplicate can
                // trigger rebuildClipComponents() which destroys this component.
                juce::Component::SafePointer<ClipComponent> safeThis(this);

                double finalStartTime = previewStartTime_;
                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                }
                finalStartTime = juce::jmax(0.0, finalStartTime);

                // Determine target track
                TrackId targetTrackId = dragStartTrackId_;
                if (parentPanel_) {
                    auto screenPos = e.getScreenPosition();
                    auto parentPos = parentPanel_->getScreenBounds().getPosition();
                    int localY = screenPos.y - parentPos.y;
                    int trackIndex = parentPanel_->getTrackIndexAtY(localY);

                    if (trackIndex >= 0) {
                        auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
                            ViewModeController::getInstance().getViewMode());

                        if (trackIndex < static_cast<int>(visibleTracks.size())) {
                            targetTrackId = visibleTracks[trackIndex];
                        }
                    }
                }

                if (isDuplicating_) {
                    // Clear the ghost before creating the duplicate
                    if (parentPanel_) {
                        parentPanel_->clearClipGhost(clipId_);
                    }

                    // Shift+drag duplicate: create duplicate at final position via undo command
                    double dupTempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                    auto cmd = std::make_unique<DuplicateClipCommand>(
                        clipId_, BeatPosition{finalStartTime * dupTempo / 60.0}, targetTrackId,
                        dupTempo);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                    // Select the duplicate — must happen before SafePointer check
                    // because rebuildClipComponents() during execute destroys this
                    // component, making safeThis null and skipping the selection.
                    ClipId newClipId = cmdPtr->getDuplicatedClipId();
                    if (newClipId != INVALID_CLIP_ID) {
                        SelectionManager::getInstance().selectClip(newClipId);
                    }
                    if (safeThis == nullptr)
                        return;
                    // Reset duplication state
                    isDuplicating_ = false;
                    duplicateClipId_ = INVALID_CLIP_ID;
                } else {
                    // Clear cross-track ghost before committing
                    if (parentPanel_) {
                        parentPanel_->clearClipGhost(clipId_);
                    }

                    // Normal move: update original clip position
                    if (onClipMoved) {
                        onClipMoved(clipId_, finalStartTime);
                        if (safeThis == nullptr)
                            return;
                    }
                    if (targetTrackId != dragStartTrackId_ && onClipMovedToTrack) {
                        onClipMovedToTrack(clipId_, targetTrackId);
                    }
                }
                break;
            }

            case DragMode::ResizeLeft: {
                resizeThrottle_.reset();
                double finalStartTime = previewStartTime_;
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                    finalLength = dragStartLength_ - (finalStartTime - dragStartTime_);
                }

                finalStartTime = juce::jmax(0.0, finalStartTime);
                finalLength = juce::jmax(0.1, finalLength);

                // Restore only the fields modified by the throttled drag updates.
                // ResizeClipCommand needs the original state to compute correctly.
                {
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_)) {
                        ClipOperations::setTimelinePlacement(*c, dragStartTime_, dragStartLength_,
                                                             commitTempoBPM);
                        c->offset = dragStartClipSnapshot_.offset;
                        c->loopStart = dragStartClipSnapshot_.loopStart;
                        c->midiOffset = dragStartClipSnapshot_.midiOffset;
                        c->midiTrimOffset = dragStartClipSnapshot_.midiTrimOffset;
                    }
                }

                if (onClipResized) {
                    onClipResized(clipId_, finalLength, true);
                }
                break;
            }

            case DragMode::ResizeRight: {
                resizeThrottle_.reset();
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    double endTime = snapTimeToGrid(dragStartTime_ + finalLength);
                    finalLength = endTime - dragStartTime_;
                }

                finalLength = juce::jmax(0.1, finalLength);

                // Restore all clips to pre-drag state before committing.
                // Throttled drag updates modified lengths directly — the
                // commands need original state for correct undo capture.
                {
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_)) {
                        ClipOperations::setTimelinePlacement(*c, dragStartTime_, dragStartLength_,
                                                             commitTempoBPM);
                    }
                    for (auto& [cid, origLen] : dragStartSelectedLengths_) {
                        if (auto* c = cm.getClip(cid)) {
                            if (auto it = dragStartSelectedClipSnapshots_.find(cid);
                                it != dragStartSelectedClipSnapshots_.end()) {
                                ClipOperations::setTimelinePlacement(
                                    *c, timelineStartSeconds(it->second, commitTempoBPM), origLen,
                                    commitTempoBPM);
                            } else {
                                ClipOperations::setTimelinePlacement(
                                    *c, timelineStartSeconds(*c, commitTempoBPM), origLen,
                                    commitTempoBPM);
                            }
                        }
                    }
                }

                if (onClipResized) {
                    onClipResized(clipId_, finalLength, false);
                }
                dragStartSelectedLengths_.clear();
                dragStartSelectedClipSnapshots_.clear();
                break;
            }

            case DragMode::FadeIn: {
                // Capture final fade value before restoring
                double finalFadeIn = 0.0;
                {
                    auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
                    double pps = (dragStartLength_ > 0.0)
                                     ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                                     : 0.0;
                    if (pps > 0.0) {
                        double fadeInPx = static_cast<double>(e.x - wfArea.getX());
                        finalFadeIn = juce::jmax(0.0, fadeInPx / pps);
                        const auto* ci = getClipInfo();
                        double maxFadeIn =
                            ci ? timelineLengthSeconds(*ci, commitTempoBPM) - ci->fadeOut
                               : dragStartLength_;
                        finalFadeIn = juce::jmin(finalFadeIn, juce::jmax(0.0, maxFadeIn));
                    }
                }
                {
                    // Restore all clips to pre-drag state for correct undo capture
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_))
                        c->fadeIn = dragStartClipSnapshot_.fadeIn;
                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        if (auto* c = cm.getClip(cid))
                            c->fadeIn = snap.fadeIn;
                    }

                    double fadeDelta = finalFadeIn - dragStartFadeIn_;
                    bool isMulti = !dragStartSelectedFadeSnapshots_.empty();
                    if (isMulti)
                        UndoManager::getInstance().beginCompoundOperation("Adjust Fades");

                    auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
                    cm.setFadeIn(clipId_, finalFadeIn);
                    UndoManager::getInstance().executeCommand(std::move(cmd));

                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        const auto* c = cm.getClip(cid);
                        if (!c)
                            continue;
                        double otherFade = juce::jmax(0.0, snap.fadeIn + fadeDelta);
                        otherFade = juce::jmin(
                            otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, commitTempoBPM) -
                                                           c->fadeOut));
                        cm.setFadeIn(cid, otherFade);
                        auto otherCmd = std::make_unique<SetFadeCommand>(cid, snap);
                        UndoManager::getInstance().executeCommand(std::move(otherCmd));
                    }

                    if (isMulti)
                        UndoManager::getInstance().endCompoundOperation();
                }
                dragStartSelectedFadeSnapshots_.clear();
                break;
            }

            case DragMode::FadeOut: {
                // Capture final fade value before restoring
                double finalFadeOut = 0.0;
                {
                    auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
                    double pps = (dragStartLength_ > 0.0)
                                     ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                                     : 0.0;
                    if (pps > 0.0) {
                        double fadeOutPx = static_cast<double>(wfArea.getRight() - e.x);
                        finalFadeOut = juce::jmax(0.0, fadeOutPx / pps);
                        const auto* ci = getClipInfo();
                        double maxFadeOut =
                            ci ? timelineLengthSeconds(*ci, commitTempoBPM) - ci->fadeIn
                               : dragStartLength_;
                        finalFadeOut = juce::jmin(finalFadeOut, juce::jmax(0.0, maxFadeOut));
                    }
                }
                {
                    // Restore all clips to pre-drag state for correct undo capture
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_))
                        c->fadeOut = dragStartClipSnapshot_.fadeOut;
                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        if (auto* c = cm.getClip(cid))
                            c->fadeOut = snap.fadeOut;
                    }

                    double fadeDelta = finalFadeOut - dragStartFadeOut_;
                    bool isMulti = !dragStartSelectedFadeSnapshots_.empty();
                    if (isMulti)
                        UndoManager::getInstance().beginCompoundOperation("Adjust Fades");

                    auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
                    cm.setFadeOut(clipId_, finalFadeOut);
                    UndoManager::getInstance().executeCommand(std::move(cmd));

                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        const auto* c = cm.getClip(cid);
                        if (!c)
                            continue;
                        double otherFade = juce::jmax(0.0, snap.fadeOut + fadeDelta);
                        otherFade = juce::jmin(
                            otherFade,
                            juce::jmax(0.0, timelineLengthSeconds(*c, commitTempoBPM) - c->fadeIn));
                        cm.setFadeOut(cid, otherFade);
                        auto otherCmd = std::make_unique<SetFadeCommand>(cid, snap);
                        UndoManager::getInstance().executeCommand(std::move(otherCmd));
                    }

                    if (isMulti)
                        UndoManager::getInstance().endCompoundOperation();
                }
                dragStartSelectedFadeSnapshots_.clear();
                break;
            }

            case DragMode::VolumeDrag: {
                // Restore all clips to pre-drag state for correct undo capture
                auto& cm = ClipManager::getInstance();
                const auto* current = cm.getClip(clipId_);
                float finalDB = current ? current->volumeDB : dragStartVolumeDB_;
                float dbDelta = finalDB - dragStartVolumeDB_;

                if (auto* c = cm.getClip(clipId_))
                    c->volumeDB = dragStartClipSnapshot_.volumeDB;
                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    if (auto* c = cm.getClip(cid))
                        c->volumeDB = snap.volumeDB;
                }

                bool isMulti = !dragStartSelectedFadeSnapshots_.empty();
                if (isMulti)
                    UndoManager::getInstance().beginCompoundOperation("Adjust Volumes");

                cm.setClipVolumeDB(clipId_, finalDB);
                auto cmd = std::make_unique<SetVolumeCommand>(clipId_, dragStartClipSnapshot_);
                UndoManager::getInstance().executeCommand(std::move(cmd));

                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    float otherDB = juce::jlimit(-100.0f, 0.0f, snap.volumeDB + dbDelta);
                    cm.setClipVolumeDB(cid, otherDB);
                    auto otherCmd = std::make_unique<SetVolumeCommand>(cid, snap);
                    UndoManager::getInstance().executeCommand(std::move(otherCmd));
                }

                if (isMulti)
                    UndoManager::getInstance().endCompoundOperation();
                dragStartSelectedFadeSnapshots_.clear();
                break;
            }

            case DragMode::StretchRight: {
                stretchThrottle_.reset();

                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    double endTime = snapTimeToGrid(dragStartTime_ + finalLength);
                    finalLength = endTime - dragStartTime_;
                }

                // Clamp stretch ratio
                double stretchRatio = finalLength / dragStartLength_;
                stretchRatio = juce::jlimit(0.25, 4.0, stretchRatio);
                finalLength = dragStartLength_ * stretchRatio;
                double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;

                // Restore original state for undo capture, then apply final
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    if (clip->isMidi()) {
                        clip->midiNotes = dragStartClipSnapshot_.midiNotes;
                        ClipOperations::stretchMidiNotes(*clip, stretchRatio);
                        ClipOperations::resizeContainerFromRight(*clip, finalLength, tempo);
                    } else {
                        ClipOperations::stretchAbsolute(*clip, newSpeedRatio, finalLength, tempo);
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }

                // Register with undo system (beforeState saved at mouseDown)
                auto cmd = std::make_unique<StretchClipCommand>(clipId_, dragStartClipSnapshot_);
                UndoManager::getInstance().executeCommand(std::move(cmd));
                break;
            }

            case DragMode::StretchLeft: {
                stretchThrottle_.reset();

                double endTime = dragStartTime_ + dragStartLength_;
                double finalStartTime = previewStartTime_;
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                    finalLength = endTime - finalStartTime;
                }

                // Clamp stretch ratio
                double stretchRatio = finalLength / dragStartLength_;
                stretchRatio = juce::jlimit(0.25, 4.0, stretchRatio);
                finalLength = dragStartLength_ * stretchRatio;
                finalStartTime = endTime - finalLength;
                double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;

                // Apply final values
                double tempoLeft = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    if (clip->isMidi()) {
                        clip->midiNotes = dragStartClipSnapshot_.midiNotes;
                        ClipOperations::stretchMidiNotes(*clip, stretchRatio);
                        ClipOperations::setTimelinePlacement(*clip, finalStartTime, finalLength,
                                                             tempoLeft);
                    } else {
                        ClipOperations::stretchAbsoluteFromLeft(*clip, newSpeedRatio, finalLength,
                                                                endTime, tempoLeft);
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }

                // Register with undo system (beforeState saved at mouseDown)
                auto cmd = std::make_unique<StretchClipCommand>(clipId_, dragStartClipSnapshot_);
                UndoManager::getInstance().executeCommand(std::move(cmd));
                break;
            }

            default:
                break;
        }
        isCommitting_ = false;
    } else {
        // No drag occurred — if this was a plain click on a multi-selected clip,
        // reduce to single selection (standard DAW behavior)
        if (shouldDeselectOnMouseUp_) {
            auto& sm = SelectionManager::getInstance();
            logArrangeRangeSelect("ClipComponent::mouseUp collapsing multi-selection to clip=" +
                                  juce::String(static_cast<int>(clipId_)) +
                                  " dragMode=None noDrag selectedCountBefore=" +
                                  juce::String(static_cast<int>(sm.getSelectedClipCount())));
            sm.selectClip(clipId_);
            isSelected_ = true;

            if (onClipSelected) {
                onClipSelected(clipId_);
            }
        }

        dragMode_ = DragMode::None;
        isDragging_ = false;
    }

    shouldDeselectOnMouseUp_ = false;
}

void ClipComponent::mouseMove(const juce::MouseEvent& e) {
    bool wasHoverLeft = hoverLeftEdge_;
    bool wasHoverRight = hoverRightEdge_;
    bool wasHoverFadeIn = hoverFadeIn_;
    bool wasHoverFadeOut = hoverFadeOut_;
    bool wasHoverVolume = hoverVolumeHandle_;

    bool wasHoverLowerZone = hoverLowerZone_;

    hoverLeftEdge_ = isOnLeftEdge(e.x);
    hoverRightEdge_ = isOnRightEdge(e.x);

    // Lower half (away from the resize edges) is the time-selection zone.
    // (When a time selection covers this point hitTest() makes the clip
    // transparent, so the panel handles the grab/resize cursor there.)
    hoverLowerZone_ = e.y >= getHeight() / 2 && !hoverLeftEdge_ && !hoverRightEdge_;

    // Check fade handle hover (selected audio clips only)
    if (isSelected_) {
        hoverFadeIn_ = isOnFadeInHandle(e.x, e.y);
        hoverFadeOut_ = isOnFadeOutHandle(e.x, e.y);
        // Volume handle: only when not on fade handles or edges
        hoverVolumeHandle_ = !hoverFadeIn_ && !hoverFadeOut_ && !hoverLeftEdge_ &&
                             !hoverRightEdge_ && isOnVolumeHandle(e.x, e.y);
    } else {
        hoverFadeIn_ = false;
        hoverFadeOut_ = false;
        hoverVolumeHandle_ = false;
    }

    // Always update cursor to check modifier-driven tools.
    updateCursor(e.mods);

    if (hoverLeftEdge_ != wasHoverLeft || hoverRightEdge_ != wasHoverRight ||
        hoverFadeIn_ != wasHoverFadeIn || hoverFadeOut_ != wasHoverFadeOut ||
        hoverVolumeHandle_ != wasHoverVolume || hoverLowerZone_ != wasHoverLowerZone) {
        repaint();
    }
}

void ClipComponent::mouseEnter(const juce::MouseEvent& e) {
    mouseIsOver_ = true;
    startTimer(50);
    updateCursor(e.mods);
}

void ClipComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    mouseIsOver_ = false;
    hoverLeftEdge_ = false;
    hoverRightEdge_ = false;
    hoverFadeIn_ = false;
    hoverFadeOut_ = false;
    hoverVolumeHandle_ = false;
    hoverLowerZone_ = false;
    updateCursor();
    repaint();
}

void ClipComponent::mouseDoubleClick(const juce::MouseEvent& /*e*/) {
    if (onClipDoubleClicked) {
        onClipDoubleClicked(clipId_);
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void ClipComponent::clipsChanged() {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    // Clip may have been deleted
    const auto* clip = getClipInfo();
    if (!clip) {
        // This clip was deleted - parent should remove this component
        return;
    }
    repaint();
}

void ClipComponent::clipPropertyChanged(ClipId clipId) {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    if (clipId == clipId_) {
        repaint();
    }
}

void ClipComponent::clipSelectionChanged(ClipId clipId) {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    bool wasSelected = isSelected_;
    // Check both single clip selection and multi-clip selection
    isSelected_ = (clipId == clipId_) || SelectionManager::getInstance().isClipSelected(clipId_);

    if (wasSelected != isSelected_) {
        repaint();
    }
}

// ============================================================================
// Selection
// ============================================================================

void ClipComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        repaint();
    }
}

void ClipComponent::setMarqueeHighlighted(bool highlighted) {
    if (isMarqueeHighlighted_ != highlighted) {
        isMarqueeHighlighted_ = highlighted;
        repaint();
    }
}

bool ClipComponent::isPartOfMultiSelection() const {
    auto& selectionManager = SelectionManager::getInstance();
    return selectionManager.getSelectedClipCount() > 1 && selectionManager.isClipSelected(clipId_);
}

// ============================================================================
// Helpers
// ============================================================================

bool ClipComponent::isOnLeftEdge(int x) const {
    return x < RESIZE_HANDLE_WIDTH;
}

bool ClipComponent::isOnRightEdge(int x) const {
    return x > getWidth() - RESIZE_HANDLE_WIDTH;
}

bool ClipComponent::isOnFadeInHandle(int x, int y) const {
    const auto* clip = getClipInfo();
    if (!clip || !clip->isAudio())
        return false;

    auto waveformArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0)
        return false;

    // Check y is in handle zone (top of waveform area)
    if (y < waveformArea.getY() || y > waveformArea.getY() + FADE_HANDLE_HIT_WIDTH)
        return false;

    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    const double clipLength = clip->getTimelineLength(tempo);
    double pps =
        (clipLength > 0.0) ? static_cast<double>(waveformArea.getWidth()) / clipLength : 0.0;
    if (pps <= 0.0)
        return false;

    float handleX =
        static_cast<float>(waveformArea.getX()) + static_cast<float>(clip->fadeIn * pps);
    return std::abs(static_cast<float>(x) - handleX) <= FADE_HANDLE_HIT_WIDTH * 0.5f;
}

bool ClipComponent::isOnFadeOutHandle(int x, int y) const {
    const auto* clip = getClipInfo();
    if (!clip || !clip->isAudio())
        return false;

    auto waveformArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0)
        return false;

    if (y < waveformArea.getY() || y > waveformArea.getY() + FADE_HANDLE_HIT_WIDTH)
        return false;

    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    const double clipLength = clip->getTimelineLength(tempo);
    double pps =
        (clipLength > 0.0) ? static_cast<double>(waveformArea.getWidth()) / clipLength : 0.0;
    if (pps <= 0.0)
        return false;

    float handleX =
        static_cast<float>(waveformArea.getRight()) - static_cast<float>(clip->fadeOut * pps);
    return std::abs(static_cast<float>(x) - handleX) <= FADE_HANDLE_HIT_WIDTH * 0.5f;
}

bool ClipComponent::isOnVolumeHandle(int x, int y) const {
    juce::ignoreUnused(x);
    const auto* clip = getClipInfo();
    if (!clip || !clip->isAudio())
        return false;

    auto waveformArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0 || waveformArea.getHeight() <= 0)
        return false;

    // Hit test near the actual volume line position (±6px tolerance)
    float volumeLinear = juce::Decibels::decibelsToGain(clip->volumeDB);
    volumeLinear = juce::jlimit(0.0f, 1.0f, volumeLinear);
    float lineY = static_cast<float>(waveformArea.getY()) +
                  ((1.0f - volumeLinear) * static_cast<float>(waveformArea.getHeight()));
    return std::abs(static_cast<float>(y) - lineY) <= 6.0f;
}

void ClipComponent::updateCursor(const juce::ModifierKeys& mods) {
    const bool isShiftDown = mods.isShiftDown();

    if (isShiftDown && mods.isCtrlDown()) {
        setMouseCursor(CursorManager::getInstance().getEraseCursor());
        return;
    }

    // Cmd+Alt = blade (scissors), Alt alone = copy-drag
    if (mods.isAltDown() && !isShiftDown) {
        if (mods.isCommandDown()) {
            setMouseCursor(CursorManager::getInstance().getBladeCursor());
        } else {
            setMouseCursor(juce::MouseCursor::CopyingCursor);
        }
        return;
    }

    bool isClipSelected = SelectionManager::getInstance().isClipSelected(clipId_);

    if (isClipSelected && (hoverFadeIn_ || hoverFadeOut_)) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (isClipSelected && hoverVolumeHandle_) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // Lower half of the body is a time-selection zone (I-beam), regardless of
    // selection state. Edge-resize and the selected-clip handles above keep
    // priority since hoverLowerZone_ already excludes the edges.
    if (hoverLowerZone_) {
        setMouseCursor(juce::MouseCursor::IBeamCursor);
        return;
    }

    if (isClipSelected && (hoverLeftEdge_ || hoverRightEdge_)) {
        if (isShiftDown) {
            // Shift+edge = stretch cursor
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        } else {
            // Resize cursor only when selected
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
    } else if (isClipSelected) {
        // Shift over the body is a selection gesture now (range select);
        // the copy cursor lives on Alt, handled above
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        // Normal cursor when not selected (need to click to select first)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

const ClipInfo* ClipComponent::getClipInfo() const {
    return ClipManager::getInstance().getClip(clipId_);
}

void ClipComponent::showContextMenu() {
    auto& clipManager = ClipManager::getInstance();
    auto& selectionManager = SelectionManager::getInstance();

    // Get selection state
    bool hasSelection = selectionManager.getSelectedClipCount() > 0;
    bool isMultiSelection = selectionManager.getSelectedClipCount() > 1;
    bool isThisClipSelected = selectionManager.isClipSelected(clipId_);

    // If right-clicking on an unselected clip, select it first
    if (!isThisClipSelected) {
        selectionManager.selectClip(clipId_);
        hasSelection = true;
        isMultiSelection = false;
    }

    // Check if track is frozen — disable destructive editing if so
    const auto* clipForMenu = getClipInfo();
    bool isFrozen = false;
    if (clipForMenu) {
        auto* ti = TrackManager::getInstance().getTrack(clipForMenu->trackId);
        isFrozen = ti && ti->frozen;
    }
    bool canEdit = hasSelection && !isFrozen;

    // "Duplicate Time Selection" is enabled when an active, visible time
    // selection exists — mirrors the gate Cmd+D uses in MainWindowCommands
    // and the empty-area menu in TrackContentPanel.
    bool hasTimeSelection = false;
    if (parentPanel_ && parentPanel_->getTimelineController()) {
        const auto& sel = parentPanel_->getTimelineController()->getState().selection;
        hasTimeSelection = sel.isVisuallyActive();
    }

    juce::PopupMenu menu;

    // Copy/Cut/Paste
    menu.addItem(1, "Copy", hasSelection);  // Copy is always allowed
    menu.addItem(2, "Cut", canEdit);
    menu.addItem(3, "Paste", !isFrozen);
    menu.addSeparator();

    // Duplicate
    menu.addItem(4, "Duplicate", canEdit);
    menu.addItem(18, "Duplicate With Automation", canEdit);
    menu.addItem(19, "Duplicate Without Automation", canEdit);
    menu.addItem(17, "Duplicate Time Selection", !isFrozen && hasTimeSelection);
    menu.addSeparator();

    // Split / Trim
    menu.addItem(5, "Split / Trim", canEdit);

    // Slice operations (single audio clip only)
    bool canSliceAtMarkers = false;
    bool canSliceAtGrid = false;
    if (!isMultiSelection && canEdit) {
        const auto* singleClip = getClipInfo();
        if (singleClip && singleClip->isAudio()) {
            // Check for warp markers
            if (singleClip->warpEnabled) {
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                if (audioEngine) {
                    auto* bridge = audioEngine->getAudioBridge();
                    if (bridge) {
                        auto markers = bridge->getWarpMarkers(clipId_);
                        canSliceAtMarkers = markers.size() > 2;
                    }
                }
            }
            // Only enable grid slicing when snap interval is positive
            if (parentPanel_ && parentPanel_->getTimelineController()) {
                double gridInterval =
                    parentPanel_->getTimelineController()->getState().getSnapInterval();
                canSliceAtGrid = gridInterval > 0.0;
            }
        }
    }
    menu.addItem(13, "Slice at Warp Markers In Place", canSliceAtMarkers);
    menu.addItem(15, "Slice at Warp Markers to Drum Grid", canSliceAtMarkers);
    menu.addItem(14, "Slice at Grid In Place", canSliceAtGrid);
    menu.addItem(16, "Slice at Grid to Drum Grid", canSliceAtGrid);
    menu.addSeparator();

    bool canEditExternally = false;
    if (!isMultiSelection && canEdit) {
        const auto* singleClip = getClipInfo();
        canEditExternally = singleClip && singleClip->isAudio() &&
                            juce::File(singleClip->audio().source.filePath).existsAsFile();
    }
    menu.addItem(21, "Edit in External Editor", canEditExternally);
    menu.addSeparator();

    // Join Clips (need 2+ adjacent clips on same track)
    bool canJoin = false;
    if (selectionManager.getSelectedClipCount() >= 2) {
        auto selected = selectionManager.getSelectedClips();
        std::vector<ClipId> sorted(selected.begin(), selected.end());
        std::sort(sorted.begin(), sorted.end(), [&](ClipId a, ClipId b) {
            auto* ca = clipManager.getClip(a);
            auto* cb = clipManager.getClip(b);
            if (!ca || !cb)
                return false;
            const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
            return timelineStartSeconds(*ca, tempo) < timelineStartSeconds(*cb, tempo);
        });
        canJoin = true;
        for (size_t i = 1; i < sorted.size() && canJoin; ++i) {
            JoinClipsCommand testCmd(sorted[i - 1], sorted[i]);
            canJoin = testCmd.canExecute();
        }
    }
    menu.addItem(8, "Join Clips", canJoin && !isFrozen);
    menu.addSeparator();

    // Delete
    menu.addItem(6, "Delete", canEdit);
    menu.addSeparator();

    // Quantize (MIDI clips only)
    {
        bool hasMidi = false;
        bool canSaveSingleMidi = false;
        if (isMultiSelection) {
            for (auto cid : selectionManager.getSelectedClips()) {
                auto* c = clipManager.getClip(cid);
                if (c && c->isMidi() && !c->midiNotes.empty()) {
                    hasMidi = true;
                    break;
                }
            }
        } else {
            const auto* ci = getClipInfo();
            hasMidi = ci && ci->isMidi() && !ci->midiNotes.empty();
            canSaveSingleMidi = ci && ci->isMidi() && clipManager.canSaveClipToLibrary(ci->id);
        }

        if (hasMidi) {
            menu.addItem(20, "Save MIDI Clip to Library", canSaveSingleMidi);
            menu.addSeparator();

            juce::PopupMenu quantizeMenu;

            // "Current Grid" option (IDs 97-99)
            bool hasGrid = false;
            if (parentPanel_ && parentPanel_->getTimelineController()) {
                const auto& state = parentPanel_->getTimelineController()->getState();
                double gridBeats = GridConstants::computeGridInterval(
                    state.display.gridQuantize, state.zoom.horizontalZoom,
                    state.tempo.timeSignatureNumerator, 50);
                hasGrid = gridBeats > 0.0;
            }
            {
                juce::PopupMenu modeMenu;
                modeMenu.addItem(97, "Start");
                modeMenu.addItem(98, "Length");
                modeMenu.addItem(99, "Start & Length");
                quantizeMenu.addSubMenu("Current Grid", modeMenu, canEdit && hasGrid);
            }
            quantizeMenu.addSeparator();

            // Grid values in beats
            // Straight: 1/1=4, 1/2=2, 1/4=1, 1/8=0.5, 1/16=0.25, 1/32=0.125
            // Dotted (1.5x): 1/2.=3, 1/4.=1.5, 1/8.=0.75, 1/16.=0.375
            // Triplet (2/3x): 1/2T=4/3, 1/4T=2/3, 1/8T=1/3, 1/16T=1/6
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

            // IDs: 100+ (14 grids x 3 modes)
            int itemId = 100;
            for (const auto& grid : grids) {
                juce::PopupMenu modeMenu;
                modeMenu.addItem(itemId++, "Start");
                modeMenu.addItem(itemId++, "Length");
                modeMenu.addItem(itemId++, "Start & Length");
                quantizeMenu.addSubMenu(grid.name, modeMenu, canEdit);
            }
            menu.addSubMenu("Quantize", quantizeMenu, canEdit);
            menu.addSeparator();
        }
    }

    // Render Clip(s) - available for audio clips (single or multi-selection)
    {
        bool allAudio = true;
        if (isMultiSelection) {
            for (auto cid : selectionManager.getSelectedClips()) {
                auto* c = clipManager.getClip(cid);
                if (!c || !c->isAudio()) {
                    allAudio = false;
                    break;
                }
            }
        } else {
            const auto* clipInfo = getClipInfo();
            allAudio = clipInfo && clipInfo->isAudio();
        }
        if (allAudio) {
            menu.addSeparator();
            menu.addItem(9, isMultiSelection ? "Render Selected Clip(s)" : "Render Selected Clip");
        }
    }

    // Render Time Selection - always available
    {
        bool hasTimeSelection = false;
        if (parentPanel_ && parentPanel_->getTimelineController()) {
            const auto& state = parentPanel_->getTimelineController()->getState();
            hasTimeSelection = state.selection.isActive() && !state.selection.visuallyHidden;
        }
        menu.addItem(10, "Render Time Selection", hasTimeSelection);
    }

    // Bounce operations
    {
        menu.addSeparator();

        // Bounce In Place: only for MIDI clips on tracks with an instrument
        bool canBounceInPlace = false;
        if (!isMultiSelection) {
            const auto* clipInfo = getClipInfo();
            if (clipInfo && clipInfo->isMidi()) {
                auto* trackInfo = TrackManager::getInstance().getTrack(clipInfo->trackId);
                canBounceInPlace = trackInfo && trackInfo->hasInstrument();
            }
        }
        menu.addItem(11, "Bounce In Place", canBounceInPlace && !isFrozen);

        // Bounce To New Track: available for any clip
        menu.addItem(12, "Bounce To New Track", hasSelection && !isFrozen);
    }

    // Show menu
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, &clipManager,
                                                    &selectionManager](int result) {
        if (result == 0)
            return;  // Cancelled

        switch (result) {
            case 1: {  // Copy
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    clipManager.copyToClipboard(selectedClips);
                }
                break;
            }

            case 2: {  // Cut
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    clipManager.copyToClipboard(selectedClips);
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().beginCompoundOperation("Cut Clips");
                    for (auto clipId : selectedClips) {
                        auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                        UndoManager::getInstance().executeCommand(std::move(cmd));
                    }
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().endCompoundOperation();
                    selectionManager.clearSelection();
                }
                break;
            }

            case 3: {  // Paste
                if (clipManager.hasClipsInClipboard()) {
                    auto selectedClips = selectionManager.getSelectedClips();
                    double pasteTime = 0.0;
                    if (!selectedClips.empty()) {
                        for (auto clipId : selectedClips) {
                            const auto* clip = clipManager.getClip(clipId);
                            if (clip) {
                                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                                pasteTime = std::max(pasteTime, clip->getTimelineStart(tempo) +
                                                                    clip->getTimelineLength(tempo));
                            }
                        }
                    }
                    const double bpm = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                    auto cmd =
                        std::make_unique<PasteClipCommand>(BeatPosition{pasteTime * bpm / 60.0});
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                    const auto& pastedIds = cmdPtr->getPastedClipIds();
                    if (!pastedIds.empty()) {
                        std::unordered_set<ClipId> newSelection(pastedIds.begin(), pastedIds.end());
                        selectionManager.selectClips(newSelection);
                    }
                }
                break;
            }

            case 4: {  // Duplicate
                if (parentPanel_)
                    parentPanel_->duplicateSelectedArrangementClips(false);
                break;
            }

            case 18: {  // Duplicate With Automation
                if (parentPanel_)
                    parentPanel_->duplicateSelectedArrangementClips(true);
                break;
            }

            case 19: {  // Duplicate Without Automation
                if (parentPanel_)
                    parentPanel_->duplicateSelectedArrangementClips(false);
                break;
            }

            case 20: {  // Save MIDI Clip to Library
                if (!clipManager.saveClipToLibrary(clipId_)) {
                    showMidiClipLibrarySaveFailedAlert();
                }
                break;
            }

            case 21: {  // Edit in External Editor
                juce::String error;
                if (!clipManager.editAudioClipSourceInExternalEditor(clipId_, error)) {
                    showExternalEditorFailedAlert(error);
                }
                break;
            }

            case 17: {  // Duplicate Time Selection
                if (!parentPanel_ || !parentPanel_->getTimelineController())
                    break;
                auto& tc = *parentPanel_->getTimelineController();
                const auto& sel = tc.getState().selection;
                if (!sel.isVisuallyActive())
                    break;

                std::vector<TrackId> trackIds;
                auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
                    ViewModeController::getInstance().getViewMode());
                if (sel.isAllTracks()) {
                    trackIds = visibleTracks;
                } else {
                    for (int idx : sel.trackIndices) {
                        if (idx >= 0 && idx < static_cast<int>(visibleTracks.size()))
                            trackIds.push_back(visibleTracks[idx]);
                    }
                }

                clipManager.copyTimeRangeToClipboard(sel.startTime, sel.endTime, trackIds,
                                                     tc.getState().tempo.bpm);
                if (!clipManager.hasClipsInClipboard())
                    break;

                auto cmd = std::make_unique<PasteClipCommand>(
                    BeatPosition{sel.endTime * tc.getState().tempo.bpm / 60.0});
                UndoManager::getInstance().executeCommand(std::move(cmd));

                double duration = sel.endTime - sel.startTime;
                tc.dispatch(
                    SetTimeSelectionEvent{sel.endTime, sel.endTime + duration, sel.trackIndices});
                break;
            }

            case 5: {  // Split / Trim
                // Split selected clips at edit cursor
                if (parentPanel_ && parentPanel_->getTimelineController()) {
                    double splitTime =
                        parentPanel_->getTimelineController()->getState().editCursorPosition;
                    if (splitTime >= 0) {
                        auto selectedClips = selectionManager.getSelectedClips();
                        std::vector<ClipId> toSplit;
                        for (auto cid : selectedClips) {
                            const auto* c = clipManager.getClip(cid);
                            const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                            if (c && splitTime > timelineStartSeconds(*c, tempo) &&
                                splitTime < timelineEndSeconds(*c, tempo)) {
                                toSplit.push_back(cid);
                            }
                        }
                        if (!toSplit.empty()) {
                            if (toSplit.size() > 1)
                                UndoManager::getInstance().beginCompoundOperation("Split Clips");
                            for (auto cid : toSplit) {
                                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                                auto cmd = std::make_unique<SplitClipCommand>(
                                    cid, BeatPosition{splitTime * tempo / 60.0}, tempo);
                                UndoManager::getInstance().executeCommand(std::move(cmd));
                            }
                            if (toSplit.size() > 1)
                                UndoManager::getInstance().endCompoundOperation();
                        }
                    }
                }
                break;
            }

            case 6: {  // Delete
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().beginCompoundOperation("Delete Clips");
                    for (auto clipId : selectedClips) {
                        auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                        UndoManager::getInstance().executeCommand(std::move(cmd));
                    }
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().endCompoundOperation();
                }
                selectionManager.clearSelection();
                break;
            }

            case 8: {  // Join Clips
                auto selectedClips = selectionManager.getSelectedClips();
                if (selectedClips.size() >= 2) {
                    std::vector<ClipId> sorted(selectedClips.begin(), selectedClips.end());
                    std::sort(sorted.begin(), sorted.end(), [&](ClipId a, ClipId b) {
                        auto* ca = clipManager.getClip(a);
                        auto* cb = clipManager.getClip(b);
                        if (!ca || !cb)
                            return false;
                        const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                        return timelineStartSeconds(*ca, tempo) < timelineStartSeconds(*cb, tempo);
                    });

                    if (sorted.size() > 2)
                        UndoManager::getInstance().beginCompoundOperation("Join Clips");

                    ClipId leftId = sorted[0];
                    for (size_t i = 1; i < sorted.size(); ++i) {
                        double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                        auto cmd = std::make_unique<JoinClipsCommand>(leftId, sorted[i], tempo);
                        if (cmd->canExecute()) {
                            UndoManager::getInstance().executeCommand(std::move(cmd));
                        }
                    }

                    if (sorted.size() > 2)
                        UndoManager::getInstance().endCompoundOperation();

                    selectionManager.selectClips({leftId});
                }
                break;
            }

            case 9: {  // Render Clip
                if (onClipRenderRequested) {
                    onClipRenderRequested(clipId_);
                }
                break;
            }

            case 10: {  // Render Time Selection
                if (onRenderTimeSelectionRequested) {
                    onRenderTimeSelectionRequested();
                }
                break;
            }

            case 11: {  // Bounce In Place
                if (onBounceInPlaceRequested) {
                    onBounceInPlaceRequested(clipId_);
                }
                break;
            }

            case 12: {  // Bounce To New Track
                if (onBounceToNewTrackRequested) {
                    onBounceToNewTrackRequested(clipId_);
                }
                break;
            }

            case 13: {  // Slice at Warp Markers
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                sliceClipAtWarpMarkers(clipId_, tempo, bridge);
                break;
            }

            case 14: {  // Slice at Grid
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                double gridInterval = 0.0;
                if (parentPanel_ && parentPanel_->getTimelineController()) {
                    gridInterval =
                        parentPanel_->getTimelineController()->getState().getSnapInterval();
                }
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                sliceClipAtGrid(clipId_, gridInterval, tempo, bridge);
                break;
            }

            case 15: {  // Slice at Warp Markers to Drum Grid
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                sliceWarpMarkersToDrumGrid(clipId_, tempo, bridge);
                break;
            }

            case 16: {  // Slice at Grid to Drum Grid
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                double gridInterval = 0.0;
                if (parentPanel_ && parentPanel_->getTimelineController()) {
                    gridInterval =
                        parentPanel_->getTimelineController()->getState().getSnapInterval();
                }
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                sliceAtGridToDrumGrid(clipId_, gridInterval, tempo, bridge);
                break;
            }

            default: {
                // Quantize with current grid: IDs 97-99
                if (result >= 97 && result <= 99) {
                    const QuantizeMode modes[] = {QuantizeMode::StartOnly, QuantizeMode::LengthOnly,
                                                  QuantizeMode::StartAndLength};
                    QuantizeMode mode = modes[result - 97];
                    double grid = 1.0;
                    if (parentPanel_ && parentPanel_->getTimelineController()) {
                        const auto& state = parentPanel_->getTimelineController()->getState();
                        grid = GridConstants::computeGridInterval(
                            state.display.gridQuantize, state.zoom.horizontalZoom,
                            state.tempo.timeSignatureNumerator, 50);
                    }

                    auto selectedClips = selectionManager.getSelectedClips();
                    std::vector<ClipId> midiClips;
                    for (auto cid : selectedClips) {
                        auto* c = clipManager.getClip(cid);
                        if (c && c->isMidi() && !c->midiNotes.empty()) {
                            midiClips.push_back(cid);
                        }
                    }

                    if (!midiClips.empty()) {
                        if (midiClips.size() > 1)
                            UndoManager::getInstance().beginCompoundOperation("Quantize Clips");
                        for (auto cid : midiClips) {
                            auto* c = clipManager.getClip(cid);
                            if (!c)
                                continue;
                            std::vector<size_t> allIndices(c->midiNotes.size());
                            std::iota(allIndices.begin(), allIndices.end(), 0);
                            auto cmd = std::make_unique<QuantizeMidiNotesCommand>(
                                cid, std::move(allIndices), grid, mode);
                            UndoManager::getInstance().executeCommand(std::move(cmd));
                        }
                        if (midiClips.size() > 1)
                            UndoManager::getInstance().endCompoundOperation();
                    }
                }

                // Quantize items: IDs 100-141 (14 grids x 3 modes)
                if (result >= 100 && result <= 141) {
                    // clang-format off
                    const double gridBeats[] = {
                        4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                        3.0, 1.5, 0.75, 0.375,
                        4.0/3, 2.0/3, 1.0/3, 1.0/6,
                    };
                    // clang-format on
                    const QuantizeMode modes[] = {QuantizeMode::StartOnly, QuantizeMode::LengthOnly,
                                                  QuantizeMode::StartAndLength};
                    int offset = result - 100;
                    int gridIdx = offset / 3;
                    int modeIdx = offset % 3;
                    double grid = gridBeats[gridIdx];
                    QuantizeMode mode = modes[modeIdx];

                    auto selectedClips = selectionManager.getSelectedClips();
                    std::vector<ClipId> midiClips;
                    for (auto cid : selectedClips) {
                        auto* c = clipManager.getClip(cid);
                        if (c && c->isMidi() && !c->midiNotes.empty()) {
                            midiClips.push_back(cid);
                        }
                    }

                    if (!midiClips.empty()) {
                        if (midiClips.size() > 1)
                            UndoManager::getInstance().beginCompoundOperation("Quantize Clips");
                        for (auto cid : midiClips) {
                            auto* c = clipManager.getClip(cid);
                            if (!c)
                                continue;
                            std::vector<size_t> allIndices(c->midiNotes.size());
                            std::iota(allIndices.begin(), allIndices.end(), 0);
                            auto cmd = std::make_unique<QuantizeMidiNotesCommand>(
                                cid, std::move(allIndices), grid, mode);
                            UndoManager::getInstance().executeCommand(std::move(cmd));
                        }
                        if (midiClips.size() > 1)
                            UndoManager::getInstance().endCompoundOperation();
                    }
                }
                break;
            }
        }
    });
}

bool ClipComponent::keyPressed(const juce::KeyPress& key) {
    // ClipComponent doesn't handle any keys itself
    // Forward all keys to parent panel which will handle them or forward up the chain
    if (parentPanel_) {
        return parentPanel_->keyPressed(key);
    }

    return false;
}

}  // namespace magda
