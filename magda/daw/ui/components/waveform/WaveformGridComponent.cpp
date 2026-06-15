#include "WaveformGridComponent.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <limits>

#include "../../state/TimelineController.hpp"
#include "../../state/TimelineState.hpp"  // GridConstants (shared adaptive grid interval)
#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../timeline/TimeRuler.hpp"
#include "WarpedWaveformRenderer.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipOperations.hpp"

namespace magda::daw::ui {

namespace {
double currentTimelineBpm() {
    if (auto* controller = magda::TimelineController::getCurrent())
        return controller->getState().tempo.bpm;
    return magda::DEFAULT_BPM;
}
}  // namespace

WaveformGridComponent::WaveformGridComponent() {
    setName("WaveformGrid");
}

WaveformGridComponent::~WaveformGridComponent() {
    if (waveformListenerPath_.isNotEmpty())
        magda::AudioThumbnailManager::getInstance().removeThumbnailChangeListener(
            waveformListenerPath_, this);
}

void WaveformGridComponent::updateWaveformLoadListener(const juce::String& audioFilePath) {
    auto& mgr = magda::AudioThumbnailManager::getInstance();
    auto* thumb = audioFilePath.isNotEmpty() ? mgr.getThumbnail(audioFilePath) : nullptr;
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

void WaveformGridComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    const auto* clip = getClip();
    updateWaveformLoadListener(clip != nullptr ? clip->audio().source.filePath : juce::String());
    repaint();
}

void WaveformGridComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    // Check if interaction has settled — if so, clear flag and we'll render high-res.
    // If still active, schedule a deferred repaint for when it settles.
    if (interactionActive_) {
        juce::int64 now = juce::Time::currentTimeMillis();
        if (now - lastInteractionTime_ >= INTERACTION_SETTLE_MS) {
            interactionActive_ = false;
        } else {
            // Schedule a repaint after settle time to render high-res
            juce::Timer::callAfterDelay(
                INTERACTION_SETTLE_MS, [safeThis = juce::Component::SafePointer(this)] {
                    if (safeThis != nullptr && safeThis->interactionActive_) {
                        juce::int64 now = juce::Time::currentTimeMillis();
                        if (now - safeThis->lastInteractionTime_ >= INTERACTION_SETTLE_MS) {
                            safeThis->interactionActive_ = false;
                            safeThis->repaint();
                        }
                    }
                });
        }
    }

    // Background
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = getClip();
        if (clip && clip->isAudio()) {
            paintWaveform(g, *clip);
            paintClipBoundaries(g);
        } else {
            paintNoClipMessage(g);
        }
    } else {
        paintNoClipMessage(g);
    }
}

void WaveformGridComponent::paintWaveform(juce::Graphics& g, const magda::ClipInfo& clip) {
    if (clip.audio().source.filePath.isEmpty())
        return;

    auto layout = computeWaveformLayout(clip);
    if (layout.rect.isEmpty())
        return;

    paintWaveformBackground(g, clip, layout);
    paintWaveformThumbnail(g, clip, layout);
    paintWaveformOverlays(g, clip, layout);
}

WaveformGridComponent::WaveformLayout WaveformGridComponent::computeWaveformLayout(
    const magda::ClipInfo& clip) const {
    juce::ignoreUnused(clip);
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return {};

    double displayStartTime = getDisplayStartTime();
    int positionPixels = timeToPixel(displayStartTime);

    double displayLength = getDrawableTimelineLength();

    int clipEndPixel = timeToPixel(displayStartTime + displayLength);
    int widthPixels = clipEndPixel - positionPixels;
    if (widthPixels <= 0)
        return {};

    auto rect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Clip to visible component bounds for efficient drawing
    auto visibleRect = rect.getIntersection(getLocalBounds());

    return {rect, visibleRect, clipEndPixel};
}

void WaveformGridComponent::paintWaveformBackground(juce::Graphics& g, const magda::ClipInfo& clip,
                                                    const WaveformLayout& layout) {
    // Use visible rect for drawing to avoid huge fill operations on large files
    auto drawRect = layout.visibleRect;
    if (drawRect.isEmpty())
        return;

    int clipStartPixel = timeToPixel(getDisplayStartTime());
    int clipEndPixel = layout.clipEndPixel;

    auto outOfBoundsColour = clip.colour.darker(0.7f);

    // Left out-of-bounds region (before clip start)
    if (drawRect.getX() < clipStartPixel) {
        int outOfBoundsRight = juce::jmin(clipStartPixel, drawRect.getRight());
        auto leftOutOfBounds = drawRect.withRight(outOfBoundsRight).withLeft(drawRect.getX());
        if (!leftOutOfBounds.isEmpty()) {
            g.setColour(outOfBoundsColour);
            g.fillRect(leftOutOfBounds);
        }
    }

    // In-bounds region
    int inBoundsLeft = juce::jmax(drawRect.getX(), clipStartPixel);
    int inBoundsRight = juce::jmin(drawRect.getRight(), clipEndPixel);
    if (inBoundsLeft < inBoundsRight) {
        auto inBoundsRect = drawRect.withLeft(inBoundsLeft).withRight(inBoundsRight);
        g.setColour(clip.colour.darker(0.4f));
        g.fillRect(inBoundsRect);
    }

    // Right out-of-bounds region (beyond clip end)
    if (drawRect.getRight() > clipEndPixel) {
        int oobLeft = juce::jmax(drawRect.getX(), clipEndPixel);
        auto rightOutOfBounds = drawRect.withLeft(oobLeft);
        if (!rightOutOfBounds.isEmpty()) {
            g.setColour(clip.colour.darker(0.85f));
            g.fillRect(rightOutOfBounds);
        }
    }
}

void WaveformGridComponent::paintWaveformThumbnail(juce::Graphics& g, const magda::ClipInfo& clip,
                                                   const WaveformLayout& layout) {
    auto waveformRect = layout.rect;
    auto visibleRect = layout.visibleRect;
    if (visibleRect.isEmpty())
        return;

    auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
    // Repaint as the thumbnail streams in (progressive fill while loading).
    updateWaveformLoadListener(clip.audio().source.filePath);
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audio().source.filePath);
    double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
    // The waveform editor only ever shows the focused clip, so the waveform is
    // always rendered as if "selected" — black, to match arrangement-view styling.
    auto waveColour = juce::Colours::black;
    float gainLinear = juce::Decibels::decibelsToGain(clip.volumeDB + clip.gainDB);
    auto vertZoom = static_cast<float>(verticalZoom_) * gainLinear;

    // During active interaction (zoom/drag/scroll), use fast thumbnail path
    // to avoid blocking the message thread with raw sample disk reads.
    // Full resolution renders after interaction settles.
    bool useHighRes = !interactionActive_;

    g.saveState();
    if (g.reduceClipRegion(visibleRect)) {
        // Reverse: flip graphics horizontally so waveform draws mirrored
        if (clip.isReversed) {
            g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, visibleRect.getCentreX(),
                                                        visibleRect.getCentreY()));
        }

        if (warpMode_ && !warpMarkers_.empty()) {
            paintWarpedWaveform(g, clip, waveformRect, waveColour, vertZoom);
        } else {
            // Linear drawing. The full source file is the drawable range
            // in both REL and non-REL modes — the loop region is metadata
            // overlaid on top, never used to gate the file extent.
            double displayStart = displayInfo_.sourceFileStart;
            double displayEnd = displayInfo_.sourceFileEnd;

            if (fileDuration > 0.0 && displayEnd > fileDuration)
                displayEnd = fileDuration;

            // Compute the time range for the visible portion only
            double drawableTimelineLength = getDrawableTimelineLength();
            int audioWidthPixels =
                static_cast<int>(std::ceil(drawableTimelineLength * horizontalZoom_));
            int audioLeft = waveformRect.getX();
            int audioWidth = juce::jmin(audioWidthPixels, waveformRect.getWidth());

            auto drawRect = visibleRect.reduced(0, 4);

            if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0 && audioWidth > 0) {
                double totalDisplayTime = displayEnd - displayStart;
                if (totalDisplayTime > 0.0) {
                    double tStart =
                        displayStart + totalDisplayTime *
                                           static_cast<double>(visibleRect.getX() - audioLeft) /
                                           audioWidth;
                    double tEnd =
                        displayStart + totalDisplayTime *
                                           static_cast<double>(visibleRect.getRight() - audioLeft) /
                                           audioWidth;
                    tStart = juce::jlimit(displayStart, displayEnd, tStart);
                    tEnd = juce::jlimit(displayStart, displayEnd, tEnd);
                    thumbnailManager.drawWaveform(g, drawRect, clip.audio().source.filePath, tStart,
                                                  tEnd, waveColour, vertZoom, useHighRes);
                }
            }
        }
    }
    g.restoreState();

    // When looped, draw the remaining source audio beyond the loop end
    // so user can see and extend the loop range.
    // This must be OUTSIDE the clipped region above.
    if (showPostLoop_ && displayInfo_.isLooped() &&
        displayInfo_.fileExtentTimeline() > displayInfo_.loopEndPositionSeconds) {
        double remainingStart = displayInfo_.loopEndPositionSeconds;
        double remainingEnd = displayInfo_.fileExtentTimeline();
        // Source file range: convert timeline positions back to source file via ClipDisplayInfo
        double remainingFileStart = displayInfo_.displayPositionToSourceTime(remainingStart);
        double remainingFileEnd = displayInfo_.displayPositionToSourceTime(remainingEnd);

        if (fileDuration > 0.0 && remainingFileEnd > fileDuration)
            remainingFileEnd = fileDuration;

        int startX = waveformRect.getX() + static_cast<int>(remainingStart * horizontalZoom_);
        int endX = waveformRect.getX() + static_cast<int>(remainingEnd * horizontalZoom_);
        auto remainingRect = juce::Rectangle<int>(startX, waveformRect.getY(), endX - startX,
                                                  waveformRect.getHeight());
        // Clip to visible bounds
        auto visibleBounds = getLocalBounds();
        auto clippedRemaining = remainingRect.getIntersection(visibleBounds);
        auto drawRect = clippedRemaining.reduced(0, 4);
        if (drawRect.getWidth() > 0 && drawRect.getHeight() > 0) {
            // Adjust time range to visible portion
            double totalFileTime = remainingFileEnd - remainingFileStart;
            int remWidth = remainingRect.getWidth();
            double visFileStart = remainingFileStart;
            double visFileEnd = remainingFileEnd;
            if (remWidth > 0 && totalFileTime > 0.0) {
                visFileStart =
                    remainingFileStart +
                    totalFileTime *
                        static_cast<double>(clippedRemaining.getX() - remainingRect.getX()) /
                        remWidth;
                visFileEnd =
                    remainingFileStart +
                    totalFileTime *
                        static_cast<double>(clippedRemaining.getRight() - remainingRect.getX()) /
                        remWidth;
            }
            if (clip.isReversed) {
                g.saveState();
                g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, drawRect.getCentreX(),
                                                            drawRect.getCentreY()));
            }
            // Draw dimmer to indicate it's outside the loop
            auto dimColour = waveColour.withAlpha(0.4f);
            thumbnailManager.drawWaveform(g, drawRect, clip.audio().source.filePath, visFileStart,
                                          visFileEnd, dimColour, vertZoom, useHighRes);
            if (clip.isReversed)
                g.restoreState();
        }
    }
}

void WaveformGridComponent::paintWaveformOverlays(juce::Graphics& g, const magda::ClipInfo& clip,
                                                  const WaveformLayout& layout) {
    auto visibleRect = layout.visibleRect;
    if (visibleRect.isEmpty())
        return;

    // Beat grid overlay (after waveform, before markers)
    paintBeatGrid(g, clip);

    // Draw transient markers whenever detection data is present. Warp markers
    // are an editing overlay, but they must not hide transient sensitivity
    // feedback from the waveform editor.
    if (!transientTimes_.isEmpty()) {
        paintTransientMarkers(g, clip);
    }

    if (warpMode_ && !warpMarkers_.empty()) {
        paintWarpMarkers(g, clip);
    }

    // Center line — clipped to visible rect
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(visibleRect.getCentreY(), static_cast<float>(visibleRect.getX()),
                         static_cast<float>(visibleRect.getRight()));

    // Clip boundary indicator line at clip end
    if (layout.clipEndPixel > visibleRect.getX() && layout.clipEndPixel < visibleRect.getRight()) {
        g.setColour(DarkTheme::getAccentColour().withAlpha(0.8f));
        g.fillRect(layout.clipEndPixel - 1, visibleRect.getY(), 2, visibleRect.getHeight());
    }

    // Clip info overlay — show file name on the waveform
    g.setColour(clip.colour);
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    juce::String displayName = clip.audio().source.filePath.isNotEmpty()
                                   ? juce::File(clip.audio().source.filePath).getFileName()
                                   : clip.name;
    g.drawText(displayName, visibleRect.reduced(8, 4), juce::Justification::topLeft, true);

    // Border — draw only the visible edges
    g.setColour(clip.colour.withAlpha(0.5f));
    // Top and bottom edges
    g.fillRect(visibleRect.getX(), visibleRect.getY(), visibleRect.getWidth(), 1);
    g.fillRect(visibleRect.getX(), visibleRect.getBottom() - 1, visibleRect.getWidth(), 1);
    // Left edge (only if waveform start is visible)
    if (layout.rect.getX() >= visibleRect.getX())
        g.fillRect(layout.rect.getX(), visibleRect.getY(), 1, visibleRect.getHeight());
    // Right edge (only if waveform end is visible)
    if (layout.rect.getRight() <= visibleRect.getRight())
        g.fillRect(layout.rect.getRight() - 1, visibleRect.getY(), 1, visibleRect.getHeight());

    // Trim handles (only if edges are visible)
    g.setColour(clip.colour.brighter(0.4f));
    if (layout.rect.getX() >= visibleRect.getX() - 3)
        g.fillRect(layout.rect.getX(), visibleRect.getY(), 3, visibleRect.getHeight());
    if (layout.rect.getRight() <= visibleRect.getRight() + 3)
        g.fillRect(layout.rect.getRight() - 3, visibleRect.getY(), 3, visibleRect.getHeight());
}

void WaveformGridComponent::paintBeatGrid(juce::Graphics& g, const magda::ClipInfo& clip) {
    juce::ignoreUnused(clip);
    if ((gridResolution_ == GridResolution::Off && customGridBeats_ <= 0.0) || !timeRuler_)
        return;

    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = getDisplayStartTime();
    double fileExtent = displayInfo_.fileExtentTimeline();
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(fileExtent * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    double gridBeats = getGridResolutionBeats();
    if (gridBeats <= 0.0)
        return;

    double bpm = timeRuler_->getTempo();
    if (bpm <= 0.0)
        return;
    double secondsPerBeat = 60.0 / bpm;
    double beatsPerBar = static_cast<double>(timeRuler_->getTimeSigNumerator());

    // Match the arrangement (GridConstants::computeGridInterval): never draw grid
    // lines or bar numbers denser than ~50px. The display interval grows to
    // multi-bar steps when zoomed out, so a long clip shows e.g. one line every
    // 16 bars instead of a wall. A finer snap resolution still snaps but does not
    // force the display denser.
    const double pixelsPerBeat = secondsPerBeat * horizontalZoom_;
    constexpr int kMinGridLinePx = 50;  // matches LayoutConfig::minGridPixelSpacing
    {
        const int timeSigNum = juce::jmax(1, static_cast<int>(beatsPerBar));
        const double frac =
            magda::GridConstants::findBeatSubdivision(pixelsPerBeat, kMinGridLinePx);
        const double adaptiveBeats =
            (frac > 0.0)
                ? frac
                : static_cast<double>(timeSigNum) * magda::GridConstants::findBarMultiple(
                                                        pixelsPerBeat, timeSigNum, kMinGridLinePx);
        if (adaptiveBeats > gridBeats)
            gridBeats = adaptiveBeats;
    }
    double secondsPerGrid = gridBeats * secondsPerBeat;

    // Grid origin in display seconds. In REL display time is clip-local, so
    // active clip start is 0. In ABS display time is project timeline seconds.
    double originDisplay = timeRuler_ ? timeRuler_->getBarOrigin() : 0.0;

    // Compute visible time range from pixel bounds and iterate only that range
    // (avoids iterating the entire file extent for large audio files)
    int visibleLeft = 0;
    int visibleRight = getWidth();

    double visibleStartDisplay = pixelToTime(visibleLeft);
    double visibleEndDisplay = pixelToTime(visibleRight);

    // Clamp to waveform extent
    double waveformStartDisplay = displayStartTime;
    double waveformEndDisplay = displayStartTime + fileExtent;
    visibleStartDisplay = juce::jmax(visibleStartDisplay, waveformStartDisplay);
    visibleEndDisplay = juce::jmin(visibleEndDisplay, waveformEndDisplay);

    // First grid line at or before the visible start.
    double startK = std::floor((visibleStartDisplay - originDisplay) / secondsPerGrid);
    double iterStart = originDisplay + startK * secondsPerGrid;
    double iterEnd = visibleEndDisplay + secondsPerGrid;

    auto& fontMgr = FontManager::getInstance();

    for (double displayTime = iterStart; displayTime < iterEnd; displayTime += secondsPerGrid) {
        int px = timeToPixel(displayTime);

        if (px < waveformRect.getX() || px > waveformRect.getRight())
            continue;

        // Beat position relative to the grid origin
        double beatPos = (displayTime - originDisplay) / secondsPerBeat;
        // Round to avoid floating-point drift
        double beatPosRounded = std::round(beatPos * 1000.0) / 1000.0;
        bool isBar = (std::fmod(std::abs(beatPosRounded), beatsPerBar) < 0.001);
        bool isBeat = (std::fmod(std::abs(beatPosRounded), 1.0) < 0.001);

        if (isBar) {
            g.setColour(juce::Colour(0xFF707070));
        } else if (isBeat) {
            g.setColour(juce::Colour(0xFF585858));
        } else {
            g.setColour(juce::Colour(0xFF454545));
        }

        g.drawVerticalLine(px, static_cast<float>(waveformRect.getY()),
                           static_cast<float>(waveformRect.getBottom()));

        // Draw bar number at bar lines
        if (isBar) {
            int barNumber = static_cast<int>(std::round(beatPosRounded / beatsPerBar)) + 1;
            g.setColour(juce::Colour(0xFFAAAAAA));
            g.setFont(fontMgr.getUIFont(9.0f));
            g.drawText(juce::String(barNumber), px + 2, waveformRect.getBottom() - 14, 30, 12,
                       juce::Justification::centredLeft, false);
        }
    }
}

void WaveformGridComponent::paintWarpedWaveform(juce::Graphics& g, const magda::ClipInfo& clip,
                                                juce::Rectangle<int> waveformRect,
                                                juce::Colour waveColour, float vertZoom) {
    // Warp rendering goes through the shared renderer so the editor and the
    // arrangement clip can never drift. The editor is a source-domain view: a
    // single pass, no loop tiling. warpToPixelX maps a marker's warp-time through
    // the same tempo-aware sourceToTimeline() used everywhere else.
    auto& thumbnailManager = magda::AudioThumbnailManager::getInstance();
    auto* thumbnail = thumbnailManager.getThumbnail(clip.audio().source.filePath);
    const double fileDuration = thumbnail ? thumbnail->getTotalLength() : 0.0;
    const double displayStartTime = getDisplayStartTime();

    WarpedWaveformSpec spec;
    spec.clipArea = waveformRect.getIntersection(getLocalBounds()).reduced(0, 4);
    spec.warpToPixelX = [this, displayStartTime](double warpSeconds) {
        return (double)timeToPixel(displayInfo_.sourceToTimeline(warpSeconds) + displayStartTime);
    };
    spec.fileDuration = fileDuration;
    spec.colour = waveColour;
    spec.verticalScale = vertZoom;
    spec.useHighRes = true;
    spec.looped = false;  // source-domain view: one pass, arrangement handles tiling
    drawWarpedWaveform(g, thumbnailManager, clip.audio().source.filePath, warpMarkers_, spec);
}

void WaveformGridComponent::paintClipBoundaries(juce::Graphics& g) {
    if (clipLength_ <= 0.0 && displayInfo_.fileExtentTimeline() <= 0.0) {
        return;
    }

    auto bounds = getLocalBounds();
    bool isLooped = displayInfo_.isLooped();
    const auto* clip = getClip();
    const bool canResizeClipEnd = clip && !clip->loopEnabled && !clip->autoTempo;

    // Use theme's loop marker colour (green)
    auto loopColour = DarkTheme::getColour(DarkTheme::LOOP_MARKER);

    double baseTime = getDisplayStartTime();
    const double sampleStart = getSampleStartPositionSeconds();
    const double offsetPosition = displayInfo_.offsetPositionSeconds;

    // Loop boundaries - only shown when loop is enabled
    if (isLooped && displayInfo_.loopLengthSeconds > 0.0) {
        // Loop markers from ClipDisplayInfo (at real source positions)
        double loopStartPos = displayInfo_.loopStartPositionSeconds;
        double loopEndPos = displayInfo_.loopEndPositionSeconds;

        // Loop start marker
        int loopStartX = timeToPixel(baseTime + loopStartPos);
        g.setColour(loopColour.withAlpha(0.8f));
        g.fillRect(loopStartX - 1, 0, 2, bounds.getHeight());

        // Loop end marker
        int loopEndX = timeToPixel(baseTime + loopEndPos);
        g.setColour(loopColour.withAlpha(0.8f));
        g.fillRect(loopEndX - 1, 0, 3, bounds.getHeight());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("L", loopEndX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
    }

    const bool hasVisibleLoopPhase = isLooped && displayInfo_.loopLengthSeconds > 0.0;

    // Offset marker (orange) — only meaningful in non-loop mode. In loop mode
    // offset is represented by the phase marker inside the loop region.
    if (!isLooped) {
        int offsetX = timeToPixel(baseTime + offsetPosition);
        auto offsetColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
        float offsetAlpha = 0.8f;
        g.setColour(offsetColour.withAlpha(offsetAlpha));
        g.fillRect(offsetX - 1, 0, 2, bounds.getHeight());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("O", offsetX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
    }

    // Loop phase marker (orange) — only visible when looped, shows phase within loop region
    if (hasVisibleLoopPhase) {
        int phaseX = timeToPixel(baseTime + displayInfo_.loopPhasePositionSeconds);
        auto phaseColour = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
        g.setColour(phaseColour.withAlpha(0.8f));
        g.fillRect(phaseX - 1, 0, 2, bounds.getHeight());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("P", phaseX + 3, 2, 12, 12, juce::Justification::centredLeft, false);
    }

    // Ghost overlays — dim everything outside the active source region
    // When pre/post loop visibility is off, use fully opaque overlay to hide those regions
    {
        float leftGhostAlpha = showPreLoop_ ? 0.7f : 1.0f;
        float rightGhostAlpha = showPostLoop_ ? 0.7f : 1.0f;
        auto bgColour = DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND);
        int clipStartX = timeToPixel(baseTime + sampleStart);

        // In loop mode, the right boundary is the loop end.
        // In non-loop/non-beat mode, the right boundary is the active clip end
        // (offset + length), so the unselected source tail is greyed out while
        // the fixed source-file end marker remains visible.
        int rightBoundaryX;
        if (isLooped) {
            rightBoundaryX = timeToPixel(baseTime + displayInfo_.loopEndPositionSeconds);
        } else if (canResizeClipEnd && clipLength_ > 0.0) {
            const double activeClipEnd =
                juce::jmin(displayInfo_.offsetPositionSeconds + clipLength_,
                           displayInfo_.fileExtentTimeline());
            rightBoundaryX = timeToPixel(baseTime + activeClipEnd);
        } else {
            rightBoundaryX = timeToPixel(baseTime + displayInfo_.fileExtentTimeline());
        }

        // Left ghost: dim everything before the active region start
        // In loop mode: grey out before loop start (offset is phase, not trim)
        // In non-loop mode: grey out before clip start (offset)
        {
            int leftBoundaryX;
            if (isLooped) {
                leftBoundaryX = timeToPixel(baseTime + displayInfo_.loopStartPositionSeconds);
            } else if (canResizeClipEnd) {
                leftBoundaryX = timeToPixel(baseTime + offsetPosition);
            } else {
                leftBoundaryX = clipStartX;
            }
            int leftEdge = bounds.getX();
            if (leftBoundaryX > leftEdge) {
                g.setColour(bgColour.withAlpha(leftGhostAlpha));
                g.fillRect(juce::Rectangle<int>(leftEdge, bounds.getY(), leftBoundaryX - leftEdge,
                                                bounds.getHeight()));
            }
        }

        // Right ghost: everything after the active region boundary
        int rightEdge = bounds.getRight();
        if (rightBoundaryX < rightEdge) {
            g.setColour(bgColour.withAlpha(rightGhostAlpha));
            g.fillRect(juce::Rectangle<int>(rightBoundaryX, bounds.getY(),
                                            rightEdge - rightBoundaryX, bounds.getHeight()));
        }
    }

    // Clip boundary markers — drawn AFTER ghost overlays so they're visible on top
    if (!isLooped) {
        auto markerColour = juce::Colour(0xFFAAAAAA);

        int clipStartX = timeToPixel(baseTime + sampleStart);
        {
            g.setColour(markerColour);
            g.fillRect(clipStartX - 2, 0, 3, bounds.getHeight());
            juce::ColourGradient grad(markerColour.withAlpha(0.5f), clipStartX - 2.0f, 0.0f,
                                      markerColour.withAlpha(0.0f), clipStartX - 10.0f, 0.0f,
                                      false);
            g.setGradientFill(grad);
            g.fillRect(clipStartX - 10, 0, 8, bounds.getHeight());
        }

        int clipEndX = timeToPixel(baseTime + displayInfo_.fileExtentTimeline());
        {
            g.setColour(markerColour);
            g.fillRect(clipEndX, 0, 3, bounds.getHeight());
            juce::ColourGradient grad(markerColour.withAlpha(0.5f), clipEndX + 3.0f, 0.0f,
                                      markerColour.withAlpha(0.0f), clipEndX + 11.0f, 0.0f, false);
            g.setGradientFill(grad);
            g.fillRect(clipEndX + 3, 0, 8, bounds.getHeight());
        }

        if (canResizeClipEnd) {
            const double activeClipEnd = offsetPosition + clipLength_;
            if (activeClipEnd > offsetPosition &&
                activeClipEnd < displayInfo_.fileExtentTimeline() - 0.0001) {
                int activeClipEndX = timeToPixel(baseTime + activeClipEnd);
                auto activeEndColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
                g.setColour(activeEndColour.withAlpha(0.9f));
                g.fillRect(activeClipEndX - 1, 0, 2, bounds.getHeight());
            }
        }
    }
}

void WaveformGridComponent::paintTransientMarkers(juce::Graphics& g, const magda::ClipInfo& clip) {
    juce::ignoreUnused(clip);
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = getDisplayStartTime();
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(displayInfo_.fileExtentTimeline() * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    // Faint line for the body, with a solid handle triangle at the top so a
    // transient reads as a marker and is never mistaken for a grid line.
    const juce::Colour lineColour = juce::Colours::white.withAlpha(0.25f);
    const juce::Colour handleColour = juce::Colours::white.withAlpha(0.85f);
    constexpr float kHandleHalfW = 4.0f;
    constexpr float kHandleH = 6.0f;

    // Visible pixel range for culling
    int visibleLeft = 0;
    int visibleRight = getWidth();
    int inSourceCount = 0;
    int drawnCount = 0;
    int firstDrawnPx = std::numeric_limits<int>::max();
    double firstDrawnTime = 0.0;

    auto drawMarkersForCycle = [&](double cycleOffset, double sourceStart, double sourceEnd) {
        const float top = static_cast<float>(waveformRect.getY());
        const float bottom = static_cast<float>(waveformRect.getBottom());
        for (double t : transientTimes_) {
            if (t < sourceStart || t >= sourceEnd)
                continue;
            ++inSourceCount;

            // Convert source time to timeline display time via ClipDisplayInfo
            double displayTime = displayInfo_.sourceToTimeline(t - sourceStart) + cycleOffset;
            double absDisplayTime = displayTime + displayStartTime;
            int px = timeToPixel(absDisplayTime);

            // Cull outside visible bounds
            if (px < visibleLeft || px > visibleRight)
                continue;

            // Cull outside waveform rect
            if (px < waveformRect.getX() || px > waveformRect.getRight())
                continue;

            g.setColour(lineColour);
            g.drawVerticalLine(px, top, bottom);

            // Downward-pointing handle triangle flush with the top edge.
            const float fx = static_cast<float>(px);
            juce::Path handle;
            handle.addTriangle(fx - kHandleHalfW, top, fx + kHandleHalfW, top, fx, top + kHandleH);
            g.setColour(handleColour);
            g.fillPath(handle);
            ++drawnCount;
            if (px < firstDrawnPx) {
                firstDrawnPx = px;
                firstDrawnTime = t;
            }
        }
    };

    // Linear transient markers across the full source file.
    double sourceStart = displayInfo_.sourceFileStart;
    double sourceEnd = displayInfo_.sourceFileEnd;
    drawMarkersForCycle(0.0, sourceStart, sourceEnd);

    juce::ignoreUnused(drawnCount, inSourceCount, firstDrawnPx, firstDrawnTime);
}

void WaveformGridComponent::paintNoClipMessage(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(14.0f));
    g.drawText("No audio clip selected", bounds, juce::Justification::centred, false);
}

void WaveformGridComponent::resized() {
    // Grid size is managed by updateGridSize()
}

// ============================================================================
// Configuration
// ============================================================================

void WaveformGridComponent::setClip(magda::ClipId clipId) {
    editingClipId_ = clipId;
    transientTimes_.clear();

    // Always update clip info (even if same clip, properties may have changed)
    const auto* clip = getClip();
    if (clip) {
        double bpm = timeRuler_ ? timeRuler_->getTempo() : 120.0;
        clipStartTime_ = clip->getTimelineStart(bpm);
        clipLength_ = clip->getTimelineLength(bpm);
    } else {
        clipStartTime_ = 0.0;
        clipLength_ = 0.0;
    }

    updateGridSize();
    debugLogGeometry("setClip");
    repaint();
}

void WaveformGridComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        updateGridSize();
        debugLogGeometry("setRelativeMode");
        repaint();
    }
}

void WaveformGridComponent::setHorizontalZoom(double pixelsPerSecond) {
    if (horizontalZoom_ != pixelsPerSecond) {
        horizontalZoom_ = pixelsPerSecond;
        interactionActive_ = true;
        lastInteractionTime_ = juce::Time::currentTimeMillis();
        updateGridSize();
        repaint();
    }
}

void WaveformGridComponent::setVerticalZoom(double zoom) {
    if (verticalZoom_ != zoom) {
        verticalZoom_ = zoom;
        repaint();
    }
}

void WaveformGridComponent::updateClipPosition(double startTime, double length) {
    // Don't update cached values during a drag — they serve as the stable
    // reference for delta calculations.  Updating mid-drag causes a feedback
    // loop where each drag step compounds on the previous one.
    if (dragMode_ != DragMode::None)
        return;

    clipStartTime_ = startTime;
    clipLength_ = length;
    updateGridSize();
    debugLogGeometry("updateClipPosition");
    repaint();
}

void WaveformGridComponent::setDisplayInfo(const magda::ClipDisplayInfo& info) {
    displayInfo_ = info;
    updateGridSize();
    debugLogGeometry("setDisplayInfo");
    repaint();
}

void WaveformGridComponent::setTransientTimes(const juce::Array<double>& times) {
    transientTimes_ = times;
    repaint();
}

void WaveformGridComponent::setGridResolution(GridResolution resolution) {
    if (gridResolution_ != resolution) {
        gridResolution_ = resolution;
        repaint();
    }
}

GridResolution WaveformGridComponent::getGridResolution() const {
    return gridResolution_;
}

void WaveformGridComponent::setTimeRuler(magda::TimeRuler* ruler) {
    timeRuler_ = ruler;
    repaint();
}

void WaveformGridComponent::setGridResolutionBeats(double beats) {
    if (customGridBeats_ != beats) {
        customGridBeats_ = beats;
        repaint();
    }
}

double WaveformGridComponent::getGridResolutionBeats() const {
    if (customGridBeats_ > 0.0)
        return customGridBeats_;
    switch (gridResolution_) {
        case GridResolution::Bar:
            return timeRuler_ ? static_cast<double>(timeRuler_->getTimeSigNumerator()) : 4.0;
        case GridResolution::Beat:
            return 1.0;
        case GridResolution::Eighth:
            return 0.5;
        case GridResolution::Sixteenth:
            return 0.25;
        case GridResolution::ThirtySecond:
            return 0.125;
        case GridResolution::Off:
        default:
            return 0.0;
    }
}

double WaveformGridComponent::snapTimeToGrid(double time) const {
    double beatsPerGrid = getGridResolutionBeats();
    double bpm = timeRuler_ ? timeRuler_->getTempo() : 0.0;
    if (beatsPerGrid <= 0.0 || bpm <= 0.0) {
        return time;
    }
    double secondsPerGrid = beatsPerGrid * 60.0 / bpm;
    double origin = timeRuler_ ? displayInfo_.sourceToTimeline(timeRuler_->getBarOrigin()) : 0.0;
    double snapped = std::round((time - origin) / secondsPerGrid) * secondsPerGrid + origin;
    return snapped;
}

void WaveformGridComponent::setSnapEnabled(bool enabled) {
    snapEnabled_ = enabled;
}

bool WaveformGridComponent::isSnapEnabled() const {
    return snapEnabled_;
}

void WaveformGridComponent::setWarpMode(bool enabled) {
    if (warpMode_ != enabled) {
        warpMode_ = enabled;
        hoveredMarkerIndex_ = -1;
        draggingMarkerIndex_ = -1;
        if (!enabled) {
            warpMarkers_.clear();
        }
        repaint();
    }
}

void WaveformGridComponent::setWarpMarkers(const std::vector<magda::WarpMarkerInfo>& markers) {
    warpMarkers_ = markers;
    repaint();
}

void WaveformGridComponent::setScrollOffset(int x, int y) {
    if (scrollOffsetX_ != x || scrollOffsetY_ != y) {
        scrollOffsetX_ = x;
        scrollOffsetY_ = y;
        interactionActive_ = true;
        lastInteractionTime_ = juce::Time::currentTimeMillis();
        repaint();
    }
}

void WaveformGridComponent::setMinimumHeight(int height) {
    if (minimumHeight_ != height) {
        minimumHeight_ = juce::jmax(100, height);
        updateGridSize();
    }
}

void WaveformGridComponent::updateGridSize() {
    const auto* clip = getClip();
    if (!clip) {
        virtualContentWidth_ = parentWidth_;
        setSize(parentWidth_, 400);
        return;
    }

    // Calculate total virtual content width based on mode
    double totalTime = 0.0;
    const double fileExtentTimeline = displayInfo_.fileExtentTimeline();
    if (relativeMode_) {
        totalTime = juce::jmax(fileExtentTimeline, getDrawableTimelineLength()) + 10.0;
    } else {
        double leftPaddingTime = std::max(10.0, clipStartTime_ * 0.5);
        totalTime = clipStartTime_ + fileExtentTimeline + 10.0 + leftPaddingTime;
    }

    virtualContentWidth_ =
        static_cast<juce::int64>(totalTime * horizontalZoom_ + LEFT_PADDING + RIGHT_PADDING);

    // Component is always viewport-sized — scrolling is virtual
    setSize(parentWidth_, minimumHeight_);
}

juce::int64 WaveformGridComponent::getVirtualContentWidth() const {
    return virtualContentWidth_;
}

void WaveformGridComponent::setParentWidth(int w) {
    if (parentWidth_ != w) {
        parentWidth_ = juce::jmax(1, w);
        updateGridSize();
    }
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

int WaveformGridComponent::timeToPixel(double time) const {
    return static_cast<int>(time * horizontalZoom_) + LEFT_PADDING - scrollOffsetX_;
}

double WaveformGridComponent::pixelToTime(int x) const {
    return (x + scrollOffsetX_ - LEFT_PADDING) / horizontalZoom_;
}

double WaveformGridComponent::getSampleStartPositionSeconds() const {
    return displayInfo_.loopStartPositionSeconds;
}

double WaveformGridComponent::getDisplayStartTime() const {
    if (relativeMode_)
        return 0.0;
    return clipStartTime_ - getSampleStartPositionSeconds();
}

double WaveformGridComponent::getDrawableTimelineLength() const {
    // The drawable extent is the full source file in timeline-time, in
    // both REL and non-REL modes. Looping never gates this — the loop
    // region is metadata drawn on top, not a visibility window.
    const double fileExtent = displayInfo_.fileExtentTimeline();
    if (fileExtent > 0.0)
        return fileExtent;
    return clipLength_;
}

void WaveformGridComponent::debugLogGeometry(const char* context) const {
    juce::ignoreUnused(context);
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformGridComponent::mouseDown(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        return;
    }

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || !clip->isAudio() || clip->audio().source.filePath.isEmpty()) {
        return;
    }

    int x = event.x;
    bool shiftHeld = event.mods.isShiftDown();

    // Right-click context menu (all modes)
    if (event.mods.isPopupMenu()) {
        showContextMenu(event);
        return;
    }

    // Warp mode interaction
    if (warpMode_) {
        bool altHeld = event.mods.isAltDown();

        // Shift + click inside waveform = zoom (instead of adding warp marker)
        if (shiftHeld && isInsideWaveform(x, *clip)) {
            dragMode_ = DragMode::Zoom;
            zoomDragStartX_ = x;
            zoomDragStartY_ = event.y;
            zoomDragAnchorX_ = x;  // already viewport-relative (component is viewport-sized)
            if (onZoomDrag)
                onZoomDrag(0, 0, zoomDragAnchorX_, event.mods);  // Signal drag start
            return;
        }

        // Check if clicking on an existing marker to drag it
        int markerIndex = findMarkerAtPixel(x);
        if (markerIndex >= 0) {
            if (altHeld) {
                // Alt+drag = reposition marker (move without stretching)
                dragMode_ = DragMode::RepositionWarpMarker;
                draggingMarkerIndex_ = markerIndex;
                dragStartWarpTime_ = warpMarkers_[static_cast<size_t>(markerIndex)].warpTime;
                dragStartSourceTime_ = warpMarkers_[static_cast<size_t>(markerIndex)].sourceTime;
                dragStartX_ = x;
            } else {
                // Normal drag = stretch (change warp time only)
                dragMode_ = DragMode::MoveWarpMarker;
                draggingMarkerIndex_ = markerIndex;
                dragStartWarpTime_ = warpMarkers_[static_cast<size_t>(markerIndex)].warpTime;
                dragStartX_ = x;
            }
            return;
        }

        // Click on waveform in warp mode: add new marker
        // Markers are placed exactly where clicked (at transient positions).
        // Grid snapping only applies when MOVING markers, not when placing them.
        if (isInsideWaveform(x, *clip)) {
            double clickTime = pixelToTime(x);
            // Convert from display time to file-relative time
            double fileRelativeTime = clickTime - getDisplayStartTime();

            // Convert timeline position to source file time (absolute warp time)
            double warpTime = displayInfo_.timelineToSource(fileRelativeTime);

            // Find the corresponding sourceTime by interpolating from existing markers.
            // The warp curve maps warpTime -> sourceTime, so we need to find what
            // source position is currently playing at this warp time.
            double sourceTime = warpTime;  // Default to identity if no markers

            if (warpMarkers_.size() >= 2) {
                // Sort markers by warpTime to find the segment containing our click
                std::vector<std::pair<double, double>> sorted;  // (warpTime, sourceTime)
                for (const auto& m : warpMarkers_) {
                    sorted.push_back({m.warpTime, m.sourceTime});
                }
                std::sort(sorted.begin(), sorted.end());

                // Find the two markers that span our warpTime
                for (size_t i = 0; i + 1 < sorted.size(); ++i) {
                    if (sorted[i].first <= warpTime && sorted[i + 1].first >= warpTime) {
                        double warpDuration = sorted[i + 1].first - sorted[i].first;
                        if (warpDuration > 0.0) {
                            double ratio = (warpTime - sorted[i].first) / warpDuration;
                            sourceTime = sorted[i].second +
                                         ratio * (sorted[i + 1].second - sorted[i].second);
                        } else {
                            sourceTime = sorted[i].second;
                        }
                        break;
                    }
                }
            }

            if (onWarpMarkerAdd) {
                onWarpMarkerAdd(sourceTime, warpTime);
            }
        }
        return;
    }

    // Non-warp mode: standard trim/stretch interaction.
    // Phase marker takes priority over edge resize so a click on the "P" marker doesn't
    // fall through to the source-extent right-edge hit-test (which used to silently shorten
    // the loop when the phase landed near the file end).
    const bool nearPhaseMarker = isNearPhaseMarker(x, *clip);
    const bool nearLeftEdge = isNearLeftEdge(x, *clip);
    const bool nearRightEdge = isNearRightEdge(x, *clip);

    // Shift = force zoom-drag, even when over the phase marker. The phase
    // marker only exists when loop is on, and its 20px hit zone otherwise
    // swallows shift-drags that the user intended as zoom, making zoom feel
    // broken specifically inside the loop region. Matches the warp-mode
    // branch's shift+inside-waveform → zoom precedence above.
    if (shiftHeld && isInsideWaveform(x, *clip)) {
        dragMode_ = DragMode::Zoom;
        zoomDragStartX_ = x;
        zoomDragStartY_ = event.y;
        zoomDragAnchorX_ = x;
        if (onZoomDrag)
            onZoomDrag(0, 0, zoomDragAnchorX_, event.mods);
        return;
    }

    if (nearPhaseMarker) {
        dragMode_ = DragMode::PhaseMarker;
    } else if (nearLeftEdge) {
        dragMode_ = shiftHeld ? DragMode::StretchLeft : DragMode::ResizeLeft;
    } else if (nearRightEdge && !clip->loopEnabled && !clip->autoTempo) {
        dragMode_ = DragMode::ResizeRight;
    } else if (nearRightEdge && shiftHeld) {
        dragMode_ = DragMode::StretchRight;
    } else if (isInsideWaveform(x, *clip)) {
        // Inside waveform but not near edges — zoom drag
        dragMode_ = DragMode::Zoom;
        zoomDragStartX_ = x;
        zoomDragStartY_ = event.y;
        zoomDragAnchorX_ = x;  // already viewport-relative (component is viewport-sized)
        if (onZoomDrag)
            onZoomDrag(0, 0, zoomDragAnchorX_, event.mods);  // Signal drag start
        return;
    } else {
        dragMode_ = DragMode::None;
        return;
    }

    dragStartX_ = x;
    dragStartAudioOffset_ = clip->loopEnabled ? clip->loopStart : clip->offset;
    const double projectBpm = currentTimelineBpm();
    dragStartStartTime_ = clip->getTimelineStart(projectBpm);
    dragStartSpeedRatio_ = clip->speedRatio;
    dragStartClipLength_ = clip->getTimelineLength(projectBpm);

    if (dragMode_ == DragMode::PhaseMarker) {
        double phase = clip->offset - clip->loopStart;
        if (clip->loopLength > 0.0) {
            phase =
                std::fmod(std::fmod(phase, clip->loopLength) + clip->loopLength, clip->loopLength);
        } else {
            phase = juce::jmax(0.0, phase);
        }
        dragStartLoopOffset_ = phase;
    }

    // Use full file extent (timeline-time) for resize operations — that's
    // the visual boundary in the waveform editor. Falls back to clip
    // length if the file extent isn't known (no thumbnail yet).
    dragStartLength_ = displayInfo_.fileExtentTimeline();
    if (dragStartLength_ <= 0.0) {
        dragStartLength_ = clip->getTimelineLength(projectBpm);
    }

    // Cache file duration for trim clamping
    dragStartFileDuration_ = 0.0;
    auto* thumbnail =
        magda::AudioThumbnailManager::getInstance().getThumbnail(clip->audio().source.filePath);
    if (thumbnail) {
        dragStartFileDuration_ = thumbnail->getTotalLength();
    }
}

void WaveformGridComponent::mouseDrag(const juce::MouseEvent& event) {
    if (dragMode_ == DragMode::None) {
        return;
    }
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        return;
    }

    // Zoom drag
    if (dragMode_ == DragMode::Zoom) {
        const int deltaX = event.x - zoomDragStartX_;
        const int deltaY = zoomDragStartY_ - event.y;
        const int cursorDelta = std::abs(deltaX) > std::abs(deltaY) ? deltaX : deltaY;
        if (cursorDelta > 0) {
            setMouseCursor(magda::CursorManager::getInstance().getZoomInCursor());
        } else if (cursorDelta < 0) {
            setMouseCursor(magda::CursorManager::getInstance().getZoomOutCursor());
        }
        if (onZoomDrag) {
            onZoomDrag(deltaX, deltaY, zoomDragAnchorX_, event.mods);
        }
        return;
    }

    // Warp marker drag
    if (dragMode_ == DragMode::MoveWarpMarker) {
        auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip)
            return;

        // Pixel delta → timeline delta, then convert to source file delta
        double timelineDelta = (event.x - dragStartX_) / horizontalZoom_;
        // Convert timeline delta to source time delta
        double sourceDelta = displayInfo_.timelineToSource(timelineDelta);
        double newWarpTime = dragStartWarpTime_ + sourceDelta;
        if (newWarpTime < 0.0)
            newWarpTime = 0.0;

        // Snap to grid when snap is enabled and Alt is not held
        if (snapEnabled_ && !event.mods.isAltDown()) {
            double timelinePos = displayInfo_.sourceToTimeline(newWarpTime);
            timelinePos = snapTimeToGrid(timelinePos);
            newWarpTime = displayInfo_.timelineToSource(timelinePos);
        }

        if (draggingMarkerIndex_ >= 0 && onWarpMarkerMove) {
            onWarpMarkerMove(draggingMarkerIndex_, newWarpTime);
        }
        return;
    }

    // Warp marker reposition drag (Alt+drag: move without stretching)
    if (dragMode_ == DragMode::RepositionWarpMarker) {
        auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip)
            return;

        double timelineDelta = (event.x - dragStartX_) / horizontalZoom_;
        double sourceDelta = displayInfo_.timelineToSource(timelineDelta);

        // Move both sourceTime and warpTime by the same source-domain delta
        // This preserves the stretch relationship at this marker
        double newSourceTime = dragStartSourceTime_ + sourceDelta;
        double newWarpTime = dragStartWarpTime_ + sourceDelta;
        if (newSourceTime < 0.0)
            newSourceTime = 0.0;
        if (newWarpTime < 0.0)
            newWarpTime = 0.0;

        if (draggingMarkerIndex_ >= 0 && onWarpMarkerReposition) {
            onWarpMarkerReposition(draggingMarkerIndex_, newSourceTime, newWarpTime);
        }
        return;
    }

    // Get clip for direct modification during drag (performance optimization)
    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->audio().source.filePath.isEmpty())
        return;

    double deltaSeconds = (event.x - dragStartX_) / horizontalZoom_;

    // Calculate absolute values from original drag start values
    switch (dragMode_) {
        case DragMode::ResizeLeft: {
            double fileDelta = deltaSeconds * dragStartSpeedRatio_;
            double newOffset = dragStartAudioOffset_ + fileDelta;
            if (dragStartFileDuration_ > 0.0)
                newOffset = juce::jmin(newOffset, dragStartFileDuration_);
            newOffset = juce::jmax(0.0, newOffset);
            if (snapEnabled_) {
                double timelineOffset = displayInfo_.sourceToTimeline(newOffset);
                timelineOffset = snapTimeToGrid(timelineOffset);
                newOffset = displayInfo_.timelineToSource(timelineOffset);
                if (dragStartFileDuration_ > 0.0)
                    newOffset = juce::jmin(newOffset, dragStartFileDuration_);
                newOffset = juce::jmax(0.0, newOffset);
            }

            double bpm = timeRuler_ ? timeRuler_->getTempo() : magda::DEFAULT_BPM;
            if (clip->loopEnabled) {
                magda::ClipOperations::moveLoopStart(*clip, newOffset, dragStartFileDuration_, bpm);
            } else {
                // Non-loop editor left-handle is phase/offset only. The source
                // region anchor (loopStart/sample start) is not editable from
                // this handle. Clip length is clamped to the remaining source so
                // arrangement clips don't extend past the waveform.
                magda::ClipOperations::setAudioOffsetPreservingSourceRegion(
                    *clip, newOffset, dragStartFileDuration_, bpm);
            }
            break;
        }
        case DragMode::PhaseMarker: {
            // Phase is in source seconds, wrapped into [0, loopLength). Convert the timeline-
            // pixel delta to a source-time delta and add it to the captured start phase.
            double timelineDelta = (event.x - dragStartX_) / horizontalZoom_;
            double sourceDelta = displayInfo_.timelineToSource(timelineDelta);
            double newPhase = dragStartLoopOffset_ + sourceDelta;
            if (clip->loopLength > 0.0) {
                newPhase = std::fmod(std::fmod(newPhase, clip->loopLength) + clip->loopLength,
                                     clip->loopLength);
            } else {
                newPhase = juce::jmax(0.0, newPhase);
            }
            if (snapEnabled_) {
                double timelinePhase = displayInfo_.sourceToTimeline(newPhase);
                timelinePhase = snapTimeToGrid(timelinePhase);
                newPhase = displayInfo_.timelineToSource(timelinePhase);
                if (clip->loopLength > 0.0) {
                    newPhase = std::fmod(std::fmod(newPhase, clip->loopLength) + clip->loopLength,
                                         clip->loopLength);
                } else {
                    newPhase = juce::jmax(0.0, newPhase);
                }
            }
            magda::ClipOperations::setAudioLoopPhaseClamped(*clip, newPhase);
            break;
        }
        case DragMode::ResizeRight: {
            double newLength = dragStartClipLength_ + deltaSeconds;
            newLength = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, newLength);

            const double offsetPosition = displayInfo_.sourceToTimeline(dragStartAudioOffset_);
            if (snapEnabled_) {
                double activeEnd = snapTimeToGrid(offsetPosition + newLength);
                newLength =
                    juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, activeEnd - offsetPosition);
            }

            if (dragStartFileDuration_ > 0.0) {
                const double maxSourceLength =
                    juce::jmax(0.0, dragStartFileDuration_ - dragStartAudioOffset_);
                const double maxTimelineLength = displayInfo_.sourceToTimeline(maxSourceLength);
                newLength = juce::jlimit(
                    magda::ClipOperations::MIN_CLIP_LENGTH,
                    juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, maxTimelineLength),
                    newLength);
            }

            double bpm = timeRuler_ ? timeRuler_->getTempo() : 120.0;
            magda::ClipOperations::resizeContainerFromRight(*clip, newLength, bpm);
            break;
        }
        case DragMode::StretchRight: {
            // Stretch = only change speedRatio. clip.length and loop markers stay fixed.
            // speedRatio is a speed factor: timeline = source / speedRatio
            // wider visual → lower speedRatio → slower playback (stretched)
            // narrower visual → higher speedRatio → faster playback (compressed)
            double newExtent = dragStartLength_ + deltaSeconds;
            newExtent = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, newExtent);
            double stretchRatio = newExtent / dragStartLength_;
            double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;
            newSpeedRatio = juce::jlimit(magda::ClipOperations::MIN_SPEED_RATIO,
                                         magda::ClipOperations::MAX_SPEED_RATIO, newSpeedRatio);
            clip->speedRatio = newSpeedRatio;
            break;
        }
        case DragMode::StretchLeft: {
            // Stretch from left = only change speedRatio. clip.length and loop markers stay fixed.
            // speedRatio is a speed factor: timeline = source / speedRatio
            // wider visual → lower speedRatio → slower playback (stretched)
            // narrower visual → higher speedRatio → faster playback (compressed)
            double newExtent = dragStartLength_ - deltaSeconds;
            newExtent = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH, newExtent);
            double stretchRatio = newExtent / dragStartLength_;
            double newSpeedRatio = dragStartSpeedRatio_ / stretchRatio;
            newSpeedRatio = juce::jlimit(magda::ClipOperations::MIN_SPEED_RATIO,
                                         magda::ClipOperations::MAX_SPEED_RATIO, newSpeedRatio);
            clip->speedRatio = newSpeedRatio;
            break;
        }
        default:
            break;
    }

    // Rebuild displayInfo_ immediately so paint uses consistent values
    // (the throttled notification from WaveformEditorContent would otherwise
    // leave displayInfo_ stale relative to the clip we just modified).
    {
        double bpm = timeRuler_ ? timeRuler_->getTempo() : 120.0;
        displayInfo_ = magda::ClipDisplayInfo::from(*clip, bpm, dragStartFileDuration_);
        clipLength_ = clip->getTimelineLength(bpm);
        clipStartTime_ = clip->getTimelineStart(bpm);
    }

    // Use fast paint during drag
    interactionActive_ = true;
    lastInteractionTime_ = juce::Time::currentTimeMillis();

    // Repaint locally for immediate feedback
    repaint();

    // Throttled notification to update arrangement view (every 50ms)
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    if (currentTime - lastDragUpdateTime_ >= DRAG_UPDATE_INTERVAL_MS) {
        lastDragUpdateTime_ = currentTime;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    }

    if (onWaveformChanged) {
        onWaveformChanged();
    }
}

void WaveformGridComponent::mouseUp(const juce::MouseEvent& /*event*/) {
    if (dragMode_ == DragMode::Zoom) {
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ == DragMode::MoveWarpMarker) {
        draggingMarkerIndex_ = -1;
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ == DragMode::RepositionWarpMarker) {
        draggingMarkerIndex_ = -1;
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ != DragMode::None && editingClipId_ != magda::INVALID_CLIP_ID) {
        // Clear drag mode BEFORE notifying so that updateClipPosition() can
        // update the cached values with the final clip state.
        dragMode_ = DragMode::None;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    } else {
        dragMode_ = DragMode::None;
    }
}

void WaveformGridComponent::mouseMove(const juce::MouseEvent& event) {
    if (editingClipId_ == magda::INVALID_CLIP_ID) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto* clip = getClip();
    if (!clip || clip->audio().source.filePath.isEmpty()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    int x = event.x;

    // Warp mode: update hover state
    if (warpMode_) {
        int newHovered = findMarkerAtPixel(x);
        if (newHovered != hoveredMarkerIndex_) {
            hoveredMarkerIndex_ = newHovered;
            repaint();
        }

        if (newHovered >= 0 && event.mods.isAltDown()) {
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        } else if (newHovered >= 0) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        } else if (event.mods.isShiftDown() && isInsideWaveform(x, *clip)) {
            setMouseCursor(magda::CursorManager::getInstance().getZoomCursor());
        } else if (isInsideWaveform(x, *clip)) {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        return;
    }

    if (isNearPhaseMarker(x, *clip)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else if (isNearLeftEdge(x, *clip)) {
        if (event.mods.isShiftDown()) {
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        } else {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
    } else if (isNearRightEdge(x, *clip)) {
        if (!clip->loopEnabled && !clip->autoTempo)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (event.mods.isShiftDown())
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    } else if (isInsideWaveform(x, *clip)) {
        setMouseCursor(magda::CursorManager::getInstance().getZoomCursor());
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// ============================================================================
// Hit Testing Helpers
// ============================================================================

bool WaveformGridComponent::isNearLeftEdge(int x, const magda::ClipInfo& clip) const {
    juce::ignoreUnused(clip);
    int leftEdgeX = timeToPixel(getDisplayStartTime() + displayInfo_.offsetPositionSeconds);
    return std::abs(x - leftEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isNearRightEdge(int x, const magda::ClipInfo& clip) const {
    double rightEdgePosition = displayInfo_.fileExtentTimeline();
    if (!clip.loopEnabled && !clip.autoTempo) {
        const double activeClipEnd = displayInfo_.offsetPositionSeconds + clipLength_;
        if (activeClipEnd > displayInfo_.offsetPositionSeconds)
            rightEdgePosition = activeClipEnd;
    }

    int rightEdgeX = timeToPixel(getDisplayStartTime() + rightEdgePosition);
    return std::abs(x - rightEdgeX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isNearPhaseMarker(int x, const magda::ClipInfo& clip) const {
    const bool loopActive = clip.loopEnabled || clip.autoTempo;
    if (!loopActive || displayInfo_.loopLengthSeconds <= 0.0)
        return false;
    int phaseX = timeToPixel(getDisplayStartTime() + displayInfo_.loopPhasePositionSeconds);
    return std::abs(x - phaseX) <= EDGE_GRAB_DISTANCE;
}

bool WaveformGridComponent::isInsideWaveform(int x, const magda::ClipInfo& clip) const {
    juce::ignoreUnused(clip);
    double displayStartTime = getDisplayStartTime();
    int leftEdgeX = timeToPixel(displayStartTime);
    int rightEdgeX = timeToPixel(displayStartTime + displayInfo_.fileExtentTimeline());
    return x > leftEdgeX + EDGE_GRAB_DISTANCE && x < rightEdgeX - EDGE_GRAB_DISTANCE;
}

// ============================================================================
// Private Helpers
// ============================================================================

const magda::ClipInfo* WaveformGridComponent::getClip() const {
    return magda::ClipManager::getInstance().getClip(editingClipId_);
}

// ============================================================================
// Warp Marker Painting
// ============================================================================

void WaveformGridComponent::paintWarpMarkers(juce::Graphics& g, const magda::ClipInfo& clip) {
    juce::ignoreUnused(clip);
    auto bounds = getLocalBounds().reduced(LEFT_PADDING, TOP_PADDING);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    double displayStartTime = getDisplayStartTime();
    int positionPixels = timeToPixel(displayStartTime);
    int widthPixels = static_cast<int>(displayInfo_.fileExtentTimeline() * horizontalZoom_);
    if (widthPixels <= 0)
        return;

    auto waveformRect =
        juce::Rectangle<int>(positionPixels, bounds.getY(), widthPixels, bounds.getHeight());

    int visibleLeft = 0;
    int visibleRight = getWidth();

    // Skip first and last markers (TE's boundary markers at 0 and sourceLen)
    // Only draw user-created markers
    int numMarkers = static_cast<int>(warpMarkers_.size());
    for (int i = 1; i < numMarkers - 1; ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];

        // Warp time is in source file seconds — convert to timeline display time
        double displayTime = displayInfo_.sourceToTimeline(marker.warpTime) + displayStartTime;
        int px = timeToPixel(displayTime);

        // Cull outside visible bounds and waveform rect
        if (px < visibleLeft || px > visibleRight)
            continue;
        if (px < waveformRect.getX() || px > waveformRect.getRight())
            continue;

        // Determine colour: hovered marker is brighter
        bool isHovered = (i == hoveredMarkerIndex_);
        bool isDragging = (i == draggingMarkerIndex_);
        auto markerColour = juce::Colours::yellow;

        if (isDragging) {
            markerColour = markerColour.brighter(0.3f);
        } else if (isHovered) {
            markerColour = markerColour.brighter(0.15f);
        } else {
            markerColour = markerColour.withAlpha(0.7f);
        }

        // Draw vertical line (2px wide)
        g.setColour(markerColour);
        g.fillRect(px - 1, waveformRect.getY(), 2, waveformRect.getHeight());

        // Draw small triangle handle at top
        juce::Path triangle;
        float fx = static_cast<float>(px);
        float fy = static_cast<float>(waveformRect.getY());
        triangle.addTriangle(fx - 4.0f, fy, fx + 4.0f, fy, fx, fy + 6.0f);
        g.fillPath(triangle);
    }
}

// ============================================================================
// Warp Marker Helpers
// ============================================================================

int WaveformGridComponent::findMarkerAtPixel(int x) const {
    const auto* clip = getClip();
    if (!clip)
        return -1;

    double displayStartTime = getDisplayStartTime();

    // Skip first and last markers (TE's boundary markers)
    // Only allow interaction with user-created markers
    int numMarkers = static_cast<int>(warpMarkers_.size());
    for (int i = 1; i < numMarkers - 1; ++i) {
        const auto& marker = warpMarkers_[static_cast<size_t>(i)];
        double displayTime = displayInfo_.sourceToTimeline(marker.warpTime) + displayStartTime;
        int px = timeToPixel(displayTime);
        if (std::abs(x - px) <= WARP_MARKER_HIT_DISTANCE)
            return i;
    }
    return -1;
}

double WaveformGridComponent::snapToNearestTransient(double time) const {
    static constexpr double SNAP_THRESHOLD = 0.05;  // 50ms snap distance
    double closest = time;
    double closestDist = SNAP_THRESHOLD;

    for (double t : transientTimes_) {
        double dist = std::abs(t - time);
        if (dist < closestDist) {
            closestDist = dist;
            closest = t;
        }
    }
    return closest;
}

void WaveformGridComponent::showContextMenu(const juce::MouseEvent& event) {
    juce::PopupMenu menu;

    if (timeRuler_ && timeRuler_->getBarOrigin() != 0.0)
        menu.addItem(2, "Reset Beat Grid Origin");
    menu.addSeparator();
    menu.addItem(3, "Show Pre-Marker Audio", true, showPreLoop_);
    menu.addItem(4, "Show Post-Marker Audio", true, showPostLoop_);

    int markerIndex = -1;
    if (warpMode_) {
        markerIndex = findMarkerAtPixel(event.x);
        if (markerIndex >= 0) {
            menu.addSeparator();
            menu.addItem(5, "Remove Warp Marker");
        }
    }

    // Slice operations
    menu.addSeparator();
    bool canSliceAtMarkers = warpMode_ && warpMarkers_.size() > 2;
    menu.addItem(6, "Slice at Warp Markers In Place", canSliceAtMarkers);
    menu.addItem(8, "Slice at Warp Markers to Drum Grid", canSliceAtMarkers);
    bool canSliceAtGrid =
        (gridResolution_ != GridResolution::Off || customGridBeats_ > 0.0) && timeRuler_ != nullptr;
    menu.addItem(7, "Slice at Grid In Place", canSliceAtGrid);
    menu.addItem(9, "Slice at Grid to Drum Grid", canSliceAtGrid);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, markerIndex](int result) {
        if (result == 2) {
            if (timeRuler_)
                timeRuler_->setBarOrigin(0.0);
            repaint();
        } else if (result == 3) {
            showPreLoop_ = !showPreLoop_;
            repaint();
        } else if (result == 4) {
            showPostLoop_ = !showPostLoop_;
            repaint();
        } else if (result == 5 && markerIndex >= 0 && onWarpMarkerRemove) {
            onWarpMarkerRemove(markerIndex);
        } else if (result == 6 && onSliceAtWarpMarkers) {
            onSliceAtWarpMarkers();
        } else if (result == 7 && onSliceAtGrid) {
            onSliceAtGrid();
        } else if (result == 8 && onSliceWarpMarkersToDrumGrid) {
            onSliceWarpMarkersToDrumGrid();
        } else if (result == 9 && onSliceAtGridToDrumGrid) {
            onSliceAtGridToDrumGrid();
        }
    });
}

void WaveformGridComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (!warpMode_ || editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    int markerIndex = findMarkerAtPixel(event.x);
    if (markerIndex >= 0 && onWarpMarkerRemove) {
        onWarpMarkerRemove(markerIndex);
    }
}

}  // namespace magda::daw::ui
