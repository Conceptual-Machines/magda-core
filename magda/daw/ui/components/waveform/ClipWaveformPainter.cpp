#include "ClipWaveformPainter.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <vector>

#include "WarpedWaveformRenderer.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"

namespace magda::daw::ui {

void paintClipWaveform(juce::Graphics& g, const ClipInfo& clip, ClipId clipId,
                       juce::Rectangle<int> waveformArea, double clipDisplayLength,
                       const ClipWaveformSpec& spec) {
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

    auto* thumbnail = thumbnailManager.getThumbnail(clip.audio().source.filePath);
    if (thumbnail == nullptr) {
        // Broken file: there is nothing to tile, so draw the placeholder once
        // over the whole area. The tiles below are clipped to the repaint damage,
        // and the placeholder decides what it can show from the width it is
        // given — fed a narrow strip it drops its label, so a partial repaint
        // erased it and left the clip looking fine (#2026).
        AudioThumbnailManager::drawMissingFilePlaceholder(g, waveformArea);
        return;
    }

    if (clip.isReversed) {
        g.saveState();
        g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, waveformArea.getCentreX(),
                                                    waveformArea.getCentreY()));
    }

    const double tempo = spec.tempo;

    const double fileDuration = thumbnail->getTotalLength();

    // Build display info with the real file duration so loop-region
    // fields get clamped against the file extent. Without this the
    // factory falls back to a clip-length-derived extent and the loop
    // clamp branch is skipped, which leaves loopRegionLengthSource
    // potentially extending past the file.
    auto di = ClipDisplayInfo::from(clip, tempo, fileDuration);

    const double displayOffset = spec.previewOffset.value_or(clip.offset);

    const bool thick = spec.thick;
    const auto waveColour = spec.colour;
    float gainLinear = juce::Decibels::decibelsToGain(clip.volumeDB + clip.gainDB);

    bool useWarpedDraw = false;
    std::vector<WarpMarkerInfo> warpMarkers;
    if (clip.warpEnabled) {
        auto* audioEngine = TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            auto* bridge = audioEngine->getAudioBridge();
            if (bridge) {
                warpMarkers = bridge->getWarpMarkers(clipId);
                useWarpedDraw = warpMarkers.size() >= 2;
            }
        }
    }

    if (useWarpedDraw) {
        // Warp takes priority over loop tiling, through the SAME shared renderer
        // the warp editor uses -- so the arrangement and editor can never drift.
        // Audio clips default to loopEnabled (autoTempo beat clips especially), so
        // the old !isLooped() gate dropped every warped clip into the loop-tiling
        // path below, which ignores warp markers entirely. Looped warp clips now
        // tile the warped content; non-looped draw a single pass. The transform is
        // tempo-aware (sourceToTimeline), matching the editor.
        double minWarp = warpMarkers.front().warpTime;
        double maxWarp = warpMarkers.front().warpTime;
        for (const auto& m : warpMarkers) {
            minWarp = std::min(minWarp, m.warpTime);
            maxWarp = std::max(maxWarp, m.warpTime);
        }

        daw::ui::WarpedWaveformSpec wspec;
        wspec.clipArea = juce::Rectangle<int>(
            waveformArea.getX(), waveformArea.getY(),
            juce::jmin(waveformArea.getWidth(),
                       static_cast<int>(clipDisplayLength * pixelsPerSecond + 0.5)),
            waveformArea.getHeight());
        wspec.warpToPixelX = [&](double warpSeconds) {
            return waveformArea.getX() +
                   di.sourceToTimeline(warpSeconds - displayOffset) * pixelsPerSecond;
        };
        wspec.fileDuration = fileDuration;
        wspec.colour = waveColour;
        wspec.verticalScale = gainLinear;
        wspec.useHighRes = true;
        wspec.thick = thick;
        wspec.looped = di.isLooped();
        wspec.cycleWarp = maxWarp - minWarp;
        daw::ui::drawWarpedWaveform(g, thumbnailManager, clip.audio().source.filePath, warpMarkers,
                                    wspec);
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
        if (spec.previewOffset && spec.previewLoopStart) {
            phaseSource =
                wrapPhase(*spec.previewOffset - *spec.previewLoopStart, di.loopRegionLengthSource);
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
                                                  gainLinear, true, thick);

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
                                          fileEnd, waveColour, gainLinear, true, thick);
    }

    if (clip.isReversed)
        g.restoreState();
}

}  // namespace magda::daw::ui
