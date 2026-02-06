#include "ClipComponent.hpp"

#include <cmath>

#include "../../panels/state/PanelController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../tracks/TrackContentPanel.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"

namespace magda {

// Matches tracktion::AudioFadeCurve exactly
static float computeFadeGain(float alpha, int fadeType) {
    const float a = alpha * juce::MathConstants<float>::halfPi;
    switch (fadeType) {
        case 2:  // convex
            return std::sin(a);
        case 3:  // concave
            return 1.0f - std::cos(a);
        case 4: {  // sCurve — blend of concave and convex
            float concave = 1.0f - std::cos(a);
            float convex = std::sin(a);
            return (1.0f - alpha) * concave + alpha * convex;
        }
        default:
            return alpha;  // linear (type 1)
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
    ClipManager::getInstance().removeListener(this);
}

void ClipComponent::paint(juce::Graphics& g) {
    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    auto bounds = getLocalBounds();

    // Draw based on clip type
    if (clip->type == ClipType::Audio) {
        paintAudioClip(g, *clip, bounds);
    } else {
        paintMidiClip(g, *clip, bounds);
    }

    // Draw header (name, loop indicator)
    paintClipHeader(g, *clip, bounds);

    // Draw loop boundary corner cuts (after header so they cut through everything)
    double srcLength = clip->loopLength;
    if (clip->loopEnabled && srcLength > 0.0) {
        auto clipBounds = getLocalBounds();
        double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
        double beatsPerSecond = tempo / 60.0;
        // During resize drag, use preview length so boundaries stay fixed
        double displayLength =
            (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip->length;
        double clipLengthInBeats = displayLength * beatsPerSecond;
        // Loop length in beats: use authoritative beat value for autoTempo,
        // otherwise derive from source length and speedRatio
        double loopLengthBeats = (clip->autoTempo && clip->loopLengthBeats > 0.0)
                                     ? clip->loopLengthBeats
                                     : srcLength / clip->speedRatio * beatsPerSecond;
        double beatRange = juce::jmax(1.0, clipLengthInBeats);
        int numBoundaries = static_cast<int>(clipLengthInBeats / loopLengthBeats);
        auto markerColour = juce::Colours::lightgrey;

        for (int i = 1; i <= numBoundaries; ++i) {
            double boundaryBeat = i * loopLengthBeats;
            if (boundaryBeat >= clipLengthInBeats)
                break;

            float bx = static_cast<float>(clipBounds.getX()) +
                       static_cast<float>(boundaryBeat / beatRange) * clipBounds.getWidth();

            // Vertical line at loop boundary
            g.setColour(markerColour.withAlpha(0.35f));
            g.drawVerticalLine(static_cast<int>(bx), static_cast<float>(clipBounds.getY()),
                               static_cast<float>(clipBounds.getBottom()));

            // Triangular notch on both sides of the boundary
            constexpr float cutSize = 8.0f;
            float top = static_cast<float>(clipBounds.getY());
            juce::Path cut;
            // Left triangle
            cut.addTriangle(bx - cutSize, top, bx, top, bx, top + cutSize);
            // Right triangle
            cut.addTriangle(bx, top, bx + cutSize, top, bx, top + cutSize);
            g.fillPath(cut);
        }
    }

    // Draw resize handles if selected
    if (isSelected_) {
        paintResizeHandles(g, bounds);
    }

    // Draw fade handles (selected audio clips only)
    if (isSelected_ && clip->type == ClipType::Audio) {
        paintFadeHandles(g, *clip, getLocalBounds());
    }

    // Marquee highlight overlay (during marquee drag)
    if (isMarqueeHighlighted_) {
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    }

    // Selection border - show for both single selection and multi-selection
    if (isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_)) {
        g.setColour(juce::Colours::white);
        g.drawRect(bounds, 2);
    }
}

void ClipComponent::paintAudioClip(juce::Graphics& g, const ClipInfo& clip,
                                   juce::Rectangle<int> bounds) {
    // Background - slightly darker than clip colour
    auto bgColour = clip.colour.darker(0.3f);
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // Waveform area (below header)
    auto waveformArea = bounds.reduced(2, HEADER_HEIGHT + 2);

    if (clip.audioFilePath.isNotEmpty()) {
        auto& thumbnailManager = AudioThumbnailManager::getInstance();

        // Calculate visible region and file times directly in time domain
        // to avoid integer rounding errors from pixel→time→pixel conversions.
        double clipDisplayLength = clip.length;
        bool isResizeMode =
            (dragMode_ == DragMode::ResizeLeft || dragMode_ == DragMode::ResizeRight);
        bool isStretchMode =
            (dragMode_ == DragMode::StretchLeft || dragMode_ == DragMode::StretchRight);

        if (isDragging_ && (isResizeMode || isStretchMode) && previewLength_ > 0.0) {
            clipDisplayLength = previewLength_;
        }

        double pixelsPerSecond =
            (clipDisplayLength > 0.0)
                ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                : 0.0;

        if (pixelsPerSecond > 0.0) {
            // Reverse: flip graphics horizontally so waveform draws mirrored
            if (clip.isReversed) {
                g.saveState();
                g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f, waveformArea.getCentreX(),
                                                            waveformArea.getCentreY()));
            }

            // Build ClipDisplayInfo for consistent calculations
            double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
            auto di = ClipDisplayInfo::from(clip, tempo);

            // During left resize drag, the offset hasn't been committed yet,
            // so simulate the offset adjustment
            double displayOffset = clip.offset;
            if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
                double trimDelta = dragStartLength_ - previewLength_;
                displayOffset += di.timelineToSource(trimDelta);
            }

            auto waveColour = clip.colour.brighter(0.2f);

            // Get actual file duration
            double fileDuration = 0.0;
            auto* thumbnail = thumbnailManager.getThumbnail(clip.audioFilePath);
            if (thumbnail)
                fileDuration = thumbnail->getTotalLength();

            // Check for warp mode and draw warped waveform if enabled
            bool useWarpedDraw = false;
            std::vector<AudioBridge::WarpMarkerInfo> warpMarkers;

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
                // Warped waveform (non-looped): draw segments between warp markers
                // Sort markers by warpTime
                std::sort(warpMarkers.begin(), warpMarkers.end(),
                          [](const auto& a, const auto& b) { return a.warpTime < b.warpTime; });

                // Draw each segment between consecutive markers
                for (size_t i = 0; i + 1 < warpMarkers.size(); ++i) {
                    double srcStart = warpMarkers[i].sourceTime;
                    double srcEnd = warpMarkers[i + 1].sourceTime;
                    double warpStart = warpMarkers[i].warpTime;
                    double warpEnd = warpMarkers[i + 1].warpTime;

                    // Convert warp times to clip-relative display times
                    double dispStart = warpStart - displayOffset;
                    double dispEnd = warpEnd - displayOffset;

                    // Skip segments outside clip bounds
                    if (dispEnd <= 0.0 || dispStart >= clipDisplayLength)
                        continue;

                    // Clamp to clip bounds
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
                    int pixEnd =
                        waveformArea.getX() + static_cast<int>(dispEnd * pixelsPerSecond + 0.5);
                    int segWidth = pixEnd - pixStart;
                    if (segWidth <= 0)
                        continue;

                    auto drawRect = juce::Rectangle<int>(pixStart, waveformArea.getY(), segWidth,
                                                         waveformArea.getHeight());

                    // Clamp source range to file duration
                    double finalSrcStart = juce::jmax(0.0, srcStart);
                    double finalSrcEnd =
                        fileDuration > 0.0 ? juce::jmin(srcEnd, fileDuration) : srcEnd;
                    if (finalSrcEnd > finalSrcStart) {
                        thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath,
                                                      finalSrcStart, finalSrcEnd, waveColour);
                    }
                }
            } else if (di.isLooped()) {
                // Looped: tile the waveform for each loop cycle
                double loopCycle = di.loopLengthSeconds;

                // File range per cycle: the loop region in the source file
                double fileStart = di.loopStart;
                double fileEnd = di.loopStart + di.sourceLength;
                if (fileDuration > 0.0 && fileEnd > fileDuration)
                    fileEnd = fileDuration;

                // Phase offset: the first tile starts partway through the loop
                double phaseSource = di.loopOffset;
                double phaseTimeline = di.sourceToTimeline(phaseSource);
                bool isFirstTile = (phaseTimeline > 0.001);

                double timePos = 0.0;
                while (timePos < clipDisplayLength) {
                    double tileFileStart = fileStart;
                    double tileFullDuration = loopCycle;

                    if (isFirstTile) {
                        // First tile: start from phase point, shorter duration
                        tileFileStart = fileStart + phaseSource;
                        tileFullDuration = loopCycle - phaseTimeline;
                        isFirstTile = false;
                    }

                    double cycleEnd = juce::jmin(timePos + tileFullDuration, clipDisplayLength);

                    int drawX =
                        waveformArea.getX() + static_cast<int>(timePos * pixelsPerSecond + 0.5);
                    int drawRight =
                        waveformArea.getX() + static_cast<int>(cycleEnd * pixelsPerSecond + 0.5);
                    auto drawRect = juce::Rectangle<int>(
                        drawX, waveformArea.getY(), drawRight - drawX, waveformArea.getHeight());

                    // For partial tiles (last tile cut off by clip end), reduce
                    // the source range proportionally to avoid compressing the
                    // full loop cycle's audio into a shorter pixel rect.
                    double tileDuration = cycleEnd - timePos;
                    double tileSourceLen = fileEnd - tileFileStart;
                    double tileFileEnd = tileFileStart + tileSourceLen;
                    if (tileDuration < tileFullDuration - 0.0001) {
                        double fraction = tileDuration / tileFullDuration;
                        tileFileEnd = tileFileStart + tileSourceLen * fraction;
                    }

                    thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, tileFileStart,
                                                  tileFileEnd, waveColour);
                    timePos += tileFullDuration;
                }
            } else {
                // Non-looped: single draw, clamped to file duration
                double fileStart = displayOffset;
                double fileEnd = displayOffset + di.timelineToSource(clipDisplayLength);

                if (fileDuration > 0.0 && fileEnd > fileDuration)
                    fileEnd = fileDuration;

                double clampedTimelineDuration = di.sourceToTimeline(fileEnd - fileStart);
                int drawWidth = static_cast<int>(clampedTimelineDuration * pixelsPerSecond + 0.5);
                drawWidth = juce::jmin(drawWidth, waveformArea.getWidth());

                auto drawRect = juce::Rectangle<int>(waveformArea.getX(), waveformArea.getY(),
                                                     drawWidth, waveformArea.getHeight());

                thumbnailManager.drawWaveform(g, drawRect, clip.audioFilePath, fileStart, fileEnd,
                                              waveColour);
            }
            // Restore from reverse flip
            if (clip.isReversed)
                g.restoreState();
        }
    } else {
        // Fallback: draw placeholder if no audio source
        g.setColour(clip.colour.brighter(0.2f).withAlpha(0.3f));
        g.drawText("No Audio", waveformArea, juce::Justification::centred);
    }

    // Fade overlays (always shown if fade > 0)
    if (clip.fadeIn > 0.0 || clip.fadeOut > 0.0) {
        double clipDisplayLength = clip.length;
        if (isDragging_ && previewLength_ > 0.0)
            clipDisplayLength = previewLength_;
        double pps = (clipDisplayLength > 0.0)
                         ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                         : 0.0;
        if (pps > 0.0) {
            paintFadeOverlays(g, clip, waveformArea, pps);
        }
    }

    // Border
    g.setColour(clip.colour);
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
}

void ClipComponent::paintMidiClip(juce::Graphics& g, const ClipInfo& clip,
                                  juce::Rectangle<int> bounds) {
    // Background
    auto bgColour = clip.colour.darker(0.3f);
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // MIDI note representation area
    auto noteArea = bounds.reduced(2, HEADER_HEIGHT + 2);

    // Calculate clip length in beats using actual tempo
    // During resize drag, use preview length so notes stay fixed
    double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    double beatsPerSecond = tempo / 60.0;
    double displayLength = (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip.length;
    double clipLengthInBeats = displayLength * beatsPerSecond;
    double midiOffset =
        (clip.view == ClipView::Session || clip.loopEnabled) ? clip.midiOffset : 0.0;

    // Draw MIDI notes if we have them
    if (!clip.midiNotes.empty() && noteArea.getHeight() > 5) {
        g.setColour(clip.colour.brighter(0.3f));

        // Use absolute MIDI range (0-127) for consistent vertical positioning across all clips
        const int MIDI_MAX = 127;
        const int MIDI_RANGE = 127;
        double beatRange = juce::jmax(1.0, clipLengthInBeats);

        // For MIDI clips, convert source region to beats
        double midiSrcLength =
            clip.loopLength > 0.0 ? clip.loopLength : clip.length * clip.speedRatio;
        double loopLengthBeats =
            midiSrcLength > 0 ? midiSrcLength * beatsPerSecond : clipLengthInBeats;
        if (clip.loopEnabled && loopLengthBeats > 0.0) {
            // Looping: draw notes repeating across the full clip length
            double loopStart = clip.loopStart * beatsPerSecond;
            double loopEnd = loopStart + loopLengthBeats;
            int numRepetitions = static_cast<int>(std::ceil(clipLengthInBeats / loopLengthBeats));

            for (int rep = 0; rep < numRepetitions; ++rep) {
                for (const auto& note : clip.midiNotes) {
                    double noteBeat = note.startBeat - midiOffset;

                    // Only draw notes within the loop region
                    if (noteBeat < loopStart || noteBeat >= loopEnd)
                        continue;

                    double displayStart = (noteBeat - loopStart) + rep * loopLengthBeats;
                    double displayEnd = displayStart + note.lengthBeats;

                    // Clamp note end to the loop boundary within this repetition
                    double repEnd = (rep + 1) * loopLengthBeats;
                    displayEnd = juce::jmin(displayEnd, repEnd);

                    // Skip notes completely outside clip bounds
                    if (displayEnd <= 0.0 || displayStart >= clipLengthInBeats)
                        continue;

                    // Clip to visible range
                    double visibleStart = juce::jmax(0.0, displayStart);
                    double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
                    double visibleLength = visibleEnd - visibleStart;

                    float noteY = noteArea.getY() + (MIDI_MAX - note.noteNumber) *
                                                        noteArea.getHeight() / (MIDI_RANGE + 1);
                    float noteHeight = juce::jmax(1.5f, static_cast<float>(noteArea.getHeight()) /
                                                            (MIDI_RANGE + 1));
                    float noteX = noteArea.getX() + static_cast<float>(visibleStart / beatRange) *
                                                        noteArea.getWidth();
                    float noteWidth = juce::jmax(
                        2.0f, static_cast<float>(visibleLength / beatRange) * noteArea.getWidth());

                    g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
                }
            }
        } else {
            // Non-looping: draw notes once (existing behavior)
            for (const auto& note : clip.midiNotes) {
                double displayStart = note.startBeat - midiOffset;
                double displayEnd = displayStart + note.lengthBeats;

                if (displayEnd <= 0 || displayStart >= clipLengthInBeats)
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
    }

    // Border
    g.setColour(clip.colour);
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
}

void ClipComponent::paintClipHeader(juce::Graphics& g, const ClipInfo& clip,
                                    juce::Rectangle<int> bounds) {
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);

    // Header background
    g.setColour(clip.colour);
    g.fillRoundedRectangle(headerArea.toFloat().withBottom(headerArea.getBottom() + 2),
                           CORNER_RADIUS);

    // Clip name
    if (bounds.getWidth() > MIN_WIDTH_FOR_NAME) {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(clip.name, headerArea.reduced(4, 0), juce::Justification::centredLeft, true);
    }

    // Musical mode indicator (auto-tempo)
    if (clip.autoTempo && clip.type == ClipType::Audio) {
        auto musicalArea = headerArea.removeFromRight(14).reduced(2);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xa9"), musicalArea,
                   juce::Justification::centred, false);
    }

    // Loop indicator
    if (clip.loopEnabled) {
        auto loopArea = headerArea.removeFromRight(14).reduced(2);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.drawText("L", loopArea, juce::Justification::centred, false);
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
                float gain = computeFadeGain(alpha, clip.fadeInType);
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
                float gain = computeFadeGain(alpha, clip.fadeInType);
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
                float gain = computeFadeGain(1.0f - alpha, clip.fadeOutType);
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
                float gain = computeFadeGain(1.0f - alpha, clip.fadeOutType);
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
    auto waveformArea = bounds.reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0 || waveformArea.getHeight() <= 0)
        return;

    double clipDisplayLength = clip.length;
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

void ClipComponent::resized() {
    // Nothing to do - clip bounds are set by parent
}

bool ClipComponent::hitTest(int x, int y) {
    // Determine if click is in upper vs lower zone based on TRACK height, not clip height
    // This ensures zone detection is consistent with TrackContentPanel::isInUpperTrackZone

    if (!parentPanel_) {
        // Fallback to clip-based detection
        int midY = getHeight() / 2;
        return y < midY && x >= 0 && x < getWidth();
    }

    // Convert local y to parent coordinates
    int parentY = getY() + y;

    // Check if click is in lower half of the track
    // Using the same logic as TrackContentPanel::isInUpperTrackZone
    int trackIndex = parentPanel_->getTrackIndexAtY(parentY);
    if (trackIndex < 0) {
        // Can't determine track, use clip-based fallback
        int midY = getHeight() / 2;
        return y < midY && x >= 0 && x < getWidth();
    }

    // Calculate track midpoint (same as isInUpperTrackZone)
    int trackY = parentPanel_->getTrackYPosition(trackIndex);
    int trackHeight = parentPanel_->getTrackHeight(trackIndex);
    int trackMidY = trackY + trackHeight / 2;

    // If click is in lower half of the track, let parent handle it
    if (parentY >= trackMidY) {
        return false;
    }

    // Click is in upper zone - check x bounds
    return x >= 0 && x < getWidth() && y >= 0;
}

// ============================================================================
// Mouse Handling
// ============================================================================

void ClipComponent::mouseDown(const juce::MouseEvent& e) {
    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

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
        if (c->type == ClipType::MIDI) {
            pc.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                  daw::ui::PanelContentType::PianoRoll);
        } else {
            pc.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                  daw::ui::PanelContentType::WaveformEditor);
        }
    };

    // Handle Cmd/Ctrl+click for toggle selection
    if (e.mods.isCommandDown()) {
        selectionManager.toggleClipSelection(clipId_);
        // Update local state
        isSelected_ = selectionManager.isClipSelected(clipId_);

        // Open editor panel for updated selection
        ensureEditorOpen(clipId_);

        // Don't start dragging on Cmd+click - it's just for selection
        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    // Handle Shift+click on edges for stretch; Shift+body falls through to drag for duplicate
    if (e.mods.isShiftDown()) {
        if (isOnLeftEdge(e.x) || isOnRightEdge(e.x)) {
            // Shift+edge = stretch mode — fall through to drag setup below
        }
        // Shift+body = fall through to normal selection + drag setup (duplicate on drag)
    }

    // Handle Alt+click for blade/split
    if (e.mods.isAltDown() && !e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        // Calculate split time from click position
        if (parentPanel_) {
            auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
            double splitTime = parentPanel_->pixelToTime(parentPos.x);

            // Apply snap if available
            if (snapTimeToGrid) {
                splitTime = snapTimeToGrid(splitTime);
            }

            // Verify split time is within clip bounds
            if (splitTime > clip->startTime && splitTime < clip->startTime + clip->length) {
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
    DBG("ClipComponent::mouseDown - clipId=" << clipId_ << ", isAlreadySelected="
                                             << (isAlreadySelected ? "YES" : "NO")
                                             << ", selectedCount=" << selectedCount);

    if (isAlreadySelected && selectedCount > 1) {
        // Clicking on a clip that's already selected in a multi-selection
        // Keep the multi-selection on mouseDown (user might be about to drag all of them)
        // but flag for deselection on mouseUp if no drag occurs
        DBG("  -> Keeping multi-selection (already selected), will deselect on mouseUp if no drag");
        isSelected_ = true;
        shouldDeselectOnMouseUp_ = true;
    } else {
        // Clicking on unselected clip - select only this one
        DBG("  -> Selecting only this clip");
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
    dragStartTime_ = clip->startTime;
    dragStartLength_ = clip->length;
    dragStartTrackId_ = clip->trackId;
    dragStartAudioOffset_ = clip->offset;

    // Cache file duration for resize clamping
    dragStartFileDuration_ = 0.0;
    if (clip->type == ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
        auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
        if (thumbnail)
            dragStartFileDuration_ = thumbnail->getTotalLength();
    }

    // Initialize preview state
    previewStartTime_ = clip->startTime;
    previewLength_ = clip->length;
    isDragging_ = false;

    // Determine drag mode based on click position
    // Fade handles take priority over resize edges (they check y-range, edges don't)
    if (isSelected_ && isOnFadeInHandle(e.x, e.y)) {
        if (e.mods.isShiftDown()) {
            // Shift+click: cycle fade-in type (1→2→3→4→1)
            dragStartClipSnapshot_ = *clip;
            int newType = (clip->fadeInType % 4) + 1;
            ClipManager::getInstance().setFadeInType(clipId_, newType);
            auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        dragMode_ = DragMode::FadeIn;
        dragStartFadeIn_ = clip->fadeIn;
        dragStartClipSnapshot_ = *clip;
        repaint();
        return;
    }
    if (isSelected_ && isOnFadeOutHandle(e.x, e.y)) {
        if (e.mods.isShiftDown()) {
            // Shift+click: cycle fade-out type (1→2→3→4→1)
            dragStartClipSnapshot_ = *clip;
            int newType = (clip->fadeOutType % 4) + 1;
            ClipManager::getInstance().setFadeOutType(clipId_, newType);
            auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        dragMode_ = DragMode::FadeOut;
        dragStartFadeOut_ = clip->fadeOut;
        dragStartClipSnapshot_ = *clip;
        repaint();
        return;
    }

    // Shift+edge = stretch mode (time-stretches audio source along with clip)
    if (isOnLeftEdge(e.x)) {
        if (e.mods.isShiftDown() && clip->type == ClipType::Audio &&
            clip->audioFilePath.isNotEmpty()) {
            dragMode_ = DragMode::StretchLeft;
            dragStartSpeedRatio_ = clip->speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeLeft;
        }
    } else if (isOnRightEdge(e.x)) {
        if (e.mods.isShiftDown() && clip->type == ClipType::Audio &&
            clip->audioFilePath.isNotEmpty()) {
            dragMode_ = DragMode::StretchRight;
            dragStartSpeedRatio_ = clip->speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeRight;
        }
    } else {
        dragMode_ = DragMode::Move;
    }

    repaint();
}

void ClipComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::None || !parentPanel_) {
        return;
    }

    const auto* clip = getClipInfo();
    if (!clip) {
        return;
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

    // Shift+drag to duplicate: mark for duplication (created in mouseUp to avoid re-entrancy)
    if (dragMode_ == DragMode::Move && e.mods.isShiftDown() && !isDuplicating_) {
        isDuplicating_ = true;
    }

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
                    int ghostX = parentPanel_->timeToPixel(finalTime);
                    double lengthBeats = dragStartLength_ * tempoBPM / 60.0;
                    int ghostWidth = static_cast<int>(lengthBeats * pixelsPerBeat);
                    juce::Rectangle<int> ghostBounds(ghostX, getY(), juce::jmax(10, ghostWidth),
                                                     getHeight());
                    parentPanel_->setClipGhost(clipId_, ghostBounds, clip->colour);
                }
                // Don't move the original clip component
            } else {
                // Normal move: update component position
                int newX = parentPanel_->timeToPixel(finalTime);
                double lengthBeats = dragStartLength_ * tempoBPM / 60.0;
                int newWidth = static_cast<int>(lengthBeats * pixelsPerBeat);
                setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            }
            break;
        }

        case DragMode::ResizeLeft: {
            // Work in time domain: resizing from left changes start time and length
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double endTime = dragStartTime_ + dragStartLength_;  // End stays fixed
            double finalStartTime = rawStartTime;

            // Magnetic snap for left edge
            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaBeats = std::abs((snappedTime - rawStartTime) * tempoBPM / 60.0);
                double snapDeltaPixels = snapDeltaBeats * pixelsPerBeat;
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalStartTime = snappedTime;
                }
            }

            // Ensure minimum length
            finalStartTime = juce::jmin(finalStartTime, endTime - 0.1);
            double finalLength = endTime - finalStartTime;

            // Clamp to file duration for non-looped audio clips (can't reveal past file start)
            if (dragStartFileDuration_ > 0.0 && !clip->loopEnabled) {
                double maxLength = dragStartLength_ + dragStartAudioOffset_ * dragStartSpeedRatio_;
                if (finalLength > maxLength) {
                    finalLength = maxLength;
                    finalStartTime = endTime - finalLength;
                }
            }

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            // Throttled update so waveform editor stays in sync during drag
            if (resizeThrottle_.check()) {
                auto& cm = magda::ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    ClipOperations::resizeContainerAbsolute(*mutableClip, finalStartTime,
                                                            finalLength);
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }

            // Convert to pixels (using parent's method to account for padding)
            int newX = parentPanel_->timeToPixel(finalStartTime);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(finalLengthBeats * pixelsPerBeat);
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

            previewLength_ = finalLength;

            // Throttled update so waveform editor stays in sync during drag
            if (resizeThrottle_.check()) {
                auto& cm = magda::ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    ClipOperations::resizeContainerFromRight(*mutableClip, finalLength);
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }

            // Convert to pixels (using parent's method to account for padding)
            int newX = parentPanel_->timeToPixel(dragStartTime_);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(finalLengthBeats * pixelsPerBeat);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        case DragMode::StretchRight: {
            // Shift+right edge: stretch clip and audio source proportionally
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

            // Clamp by stretch factor limits [0.25, 4.0]
            double stretchRatio = finalLength / dragStartLength_;
            double newSpeedRatio = dragStartSpeedRatio_ * stretchRatio;
            newSpeedRatio = juce::jlimit(0.25, 4.0, newSpeedRatio);
            // Back-compute the allowed length from the clamped stretch factor
            finalLength = dragStartLength_ * (newSpeedRatio / dragStartSpeedRatio_);

            previewLength_ = finalLength;

            int newX = parentPanel_->timeToPixel(dragStartTime_);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(finalLengthBeats * pixelsPerBeat);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update to audio engine
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    ClipOperations::stretchAbsolute(*mutableClip, newSpeedRatio, finalLength);
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
                double maxFadeIn = ci ? ci->length - ci->fadeOut : dragStartLength_;
                newFadeIn = juce::jmin(newFadeIn, juce::jmax(0.0, maxFadeIn));
                ClipManager::getInstance().setFadeIn(clipId_, newFadeIn);
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
                double maxFadeOut = ci ? ci->length - ci->fadeIn : dragStartLength_;
                newFadeOut = juce::jmin(newFadeOut, juce::jmax(0.0, maxFadeOut));
                ClipManager::getInstance().setFadeOut(clipId_, newFadeOut);
                repaint();
            }
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

            // Clamp by stretch factor limits
            double stretchRatio = finalLength / dragStartLength_;
            double newSpeedRatio = dragStartSpeedRatio_ * stretchRatio;
            newSpeedRatio = juce::jlimit(0.25, 4.0, newSpeedRatio);
            finalLength = dragStartLength_ * (newSpeedRatio / dragStartSpeedRatio_);
            finalStartTime = endTime - finalLength;

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            int newX = parentPanel_->timeToPixel(finalStartTime);
            double finalLengthBeats = finalLength * tempoBPM / 60.0;
            int newWidth = static_cast<int>(finalLengthBeats * pixelsPerBeat);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update to audio engine
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    double rightEdge = dragStartTime_ + dragStartLength_;
                    ClipOperations::stretchAbsoluteFromLeft(*mutableClip, newSpeedRatio,
                                                            finalLength, rightEdge);
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
    // Handle right-click for context menu
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
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

        // Now apply snapping and commit to ClipManager
        switch (savedDragMode) {
            case DragMode::Move: {
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
                    auto cmd = std::make_unique<DuplicateClipCommand>(clipId_, finalStartTime,
                                                                      targetTrackId);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                    ClipId newClipId = cmdPtr->getDuplicatedClipId();
                    if (newClipId != INVALID_CLIP_ID) {
                        SelectionManager::getInstance().selectClip(newClipId);
                    }
                    // Reset duplication state
                    isDuplicating_ = false;
                    duplicateClipId_ = INVALID_CLIP_ID;
                } else {
                    // Normal move: update original clip position
                    if (onClipMoved) {
                        onClipMoved(clipId_, finalStartTime);
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

                // Restore clip to pre-drag state before committing.
                // Throttled drag updates modified startTime/length directly
                // without adjusting offset — ResizeClipCommand needs the
                // original state to compute the correct offset delta.
                {
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_)) {
                        c->startTime = dragStartTime_;
                        c->length = dragStartLength_;
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

                // Restore clip length to pre-drag state before committing.
                // Throttled drag updates modified length directly — the
                // command needs the original state for correct undo capture.
                {
                    auto& cm = ClipManager::getInstance();
                    if (auto* c = cm.getClip(clipId_)) {
                        c->length = dragStartLength_;
                    }
                }

                if (onClipResized) {
                    onClipResized(clipId_, finalLength, false);
                }
                break;
            }

            case DragMode::FadeIn: {
                {
                    auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
                    double pps = (dragStartLength_ > 0.0)
                                     ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                                     : 0.0;
                    if (pps > 0.0) {
                        double fadeInPx = static_cast<double>(e.x - wfArea.getX());
                        double newFadeIn = juce::jmax(0.0, fadeInPx / pps);
                        const auto* ci = getClipInfo();
                        double maxFadeIn = ci ? ci->length - ci->fadeOut : dragStartLength_;
                        newFadeIn = juce::jmin(newFadeIn, juce::jmax(0.0, maxFadeIn));
                        ClipManager::getInstance().setFadeIn(clipId_, newFadeIn);
                    }
                }
                {
                    auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
                break;
            }

            case DragMode::FadeOut: {
                {
                    auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
                    double pps = (dragStartLength_ > 0.0)
                                     ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                                     : 0.0;
                    if (pps > 0.0) {
                        double fadeOutPx = static_cast<double>(wfArea.getRight() - e.x);
                        double newFadeOut = juce::jmax(0.0, fadeOutPx / pps);
                        const auto* ci = getClipInfo();
                        double maxFadeOut = ci ? ci->length - ci->fadeIn : dragStartLength_;
                        newFadeOut = juce::jmin(newFadeOut, juce::jmax(0.0, maxFadeOut));
                        ClipManager::getInstance().setFadeOut(clipId_, newFadeOut);
                    }
                }
                {
                    auto cmd = std::make_unique<SetFadeCommand>(clipId_, dragStartClipSnapshot_);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
                break;
            }

            case DragMode::StretchRight: {
                stretchThrottle_.reset();

                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    double endTime = snapTimeToGrid(dragStartTime_ + finalLength);
                    finalLength = endTime - dragStartTime_;
                }

                // Compute final stretch factor from drag-start values
                double stretchRatio = finalLength / dragStartLength_;
                double newSpeedRatio = dragStartSpeedRatio_ * stretchRatio;
                newSpeedRatio = juce::jlimit(0.25, 4.0, newSpeedRatio);
                finalLength = dragStartLength_ * (newSpeedRatio / dragStartSpeedRatio_);

                // Apply final values
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    ClipOperations::stretchAbsolute(*clip, newSpeedRatio, finalLength);
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

                // Compute final speed ratio from drag-start values
                double stretchRatio = finalLength / dragStartLength_;
                double newSpeedRatio = dragStartSpeedRatio_ * stretchRatio;
                newSpeedRatio = juce::jlimit(0.25, 4.0, newSpeedRatio);
                finalLength = dragStartLength_ * (newSpeedRatio / dragStartSpeedRatio_);
                finalStartTime = endTime - finalLength;

                // Apply final values
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    ClipOperations::stretchAbsoluteFromLeft(*clip, newSpeedRatio, finalLength,
                                                            endTime);
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

    hoverLeftEdge_ = isOnLeftEdge(e.x);
    hoverRightEdge_ = isOnRightEdge(e.x);

    // Check fade handle hover (selected audio clips only)
    if (isSelected_) {
        hoverFadeIn_ = isOnFadeInHandle(e.x, e.y);
        hoverFadeOut_ = isOnFadeOutHandle(e.x, e.y);
    } else {
        hoverFadeIn_ = false;
        hoverFadeOut_ = false;
    }

    // Always update cursor to check for Alt key (blade mode) and Shift key (stretch mode)
    updateCursor(e.mods.isAltDown(), e.mods.isShiftDown());

    if (hoverLeftEdge_ != wasHoverLeft || hoverRightEdge_ != wasHoverRight ||
        hoverFadeIn_ != wasHoverFadeIn || hoverFadeOut_ != wasHoverFadeOut) {
        repaint();
    }
}

void ClipComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    hoverLeftEdge_ = false;
    hoverRightEdge_ = false;
    hoverFadeIn_ = false;
    hoverFadeOut_ = false;
    updateCursor(false, false);
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
    if (!clip || clip->type != ClipType::Audio)
        return false;

    auto waveformArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0)
        return false;

    // Check y is in handle zone (top of waveform area)
    if (y < waveformArea.getY() || y > waveformArea.getY() + FADE_HANDLE_HIT_WIDTH)
        return false;

    double pps =
        (clip->length > 0.0) ? static_cast<double>(waveformArea.getWidth()) / clip->length : 0.0;
    if (pps <= 0.0)
        return false;

    float handleX =
        static_cast<float>(waveformArea.getX()) + static_cast<float>(clip->fadeIn * pps);
    return std::abs(static_cast<float>(x) - handleX) <= FADE_HANDLE_HIT_WIDTH * 0.5f;
}

bool ClipComponent::isOnFadeOutHandle(int x, int y) const {
    const auto* clip = getClipInfo();
    if (!clip || clip->type != ClipType::Audio)
        return false;

    auto waveformArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
    if (waveformArea.getWidth() <= 0)
        return false;

    if (y < waveformArea.getY() || y > waveformArea.getY() + FADE_HANDLE_HIT_WIDTH)
        return false;

    double pps =
        (clip->length > 0.0) ? static_cast<double>(waveformArea.getWidth()) / clip->length : 0.0;
    if (pps <= 0.0)
        return false;

    float handleX =
        static_cast<float>(waveformArea.getRight()) - static_cast<float>(clip->fadeOut * pps);
    return std::abs(static_cast<float>(x) - handleX) <= FADE_HANDLE_HIT_WIDTH * 0.5f;
}

void ClipComponent::updateCursor(bool isAltDown, bool isShiftDown) {
    // Alt key = blade/scissors mode
    if (isAltDown) {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    bool isClipSelected = SelectionManager::getInstance().isClipSelected(clipId_);

    if (isClipSelected && (hoverFadeIn_ || hoverFadeOut_)) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
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
        // Grab cursor when selected (can drag)
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

    juce::PopupMenu menu;

    // Copy/Cut/Paste
    menu.addItem(1, "Copy", hasSelection);
    menu.addItem(2, "Cut", hasSelection);
    menu.addItem(3, "Paste");  // Always available (will check clipboard when clicked)
    menu.addSeparator();

    // Duplicate
    menu.addItem(4, "Duplicate", hasSelection);
    menu.addSeparator();

    // Split / Trim
    menu.addItem(5, "Split / Trim", hasSelection);
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
            return ca->startTime < cb->startTime;
        });
        canJoin = true;
        for (size_t i = 1; i < sorted.size() && canJoin; ++i) {
            JoinClipsCommand testCmd(sorted[i - 1], sorted[i]);
            canJoin = testCmd.canExecute();
        }
    }
    menu.addItem(8, "Join Clips", canJoin);
    menu.addSeparator();

    // Delete
    menu.addItem(6, "Delete", hasSelection);
    menu.addSeparator();

    // Loop Settings (only for single clip)
    if (!isMultiSelection) {
        menu.addItem(7, "Loop Settings...");
    }

    // Render Clip(s) - available for audio clips (single or multi-selection)
    {
        bool allAudio = true;
        if (isMultiSelection) {
            for (auto cid : selectionManager.getSelectedClips()) {
                auto* c = clipManager.getClip(cid);
                if (!c || c->type != ClipType::Audio) {
                    allAudio = false;
                    break;
                }
            }
        } else {
            const auto* clipInfo = getClipInfo();
            allAudio = clipInfo && clipInfo->type == ClipType::Audio;
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
                                pasteTime = std::max(pasteTime, clip->startTime + clip->length);
                            }
                        }
                    }
                    auto cmd = std::make_unique<PasteClipCommand>(pasteTime);
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
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().beginCompoundOperation("Duplicate Clips");
                    for (auto clipId : selectedClips) {
                        auto cmd = std::make_unique<DuplicateClipCommand>(clipId);
                        UndoManager::getInstance().executeCommand(std::move(cmd));
                    }
                    if (selectedClips.size() > 1)
                        UndoManager::getInstance().endCompoundOperation();
                }
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
                            if (c && splitTime > c->startTime &&
                                splitTime < c->startTime + c->length) {
                                toSplit.push_back(cid);
                            }
                        }
                        if (!toSplit.empty()) {
                            if (toSplit.size() > 1)
                                UndoManager::getInstance().beginCompoundOperation("Split Clips");
                            for (auto cid : toSplit) {
                                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                                auto cmd =
                                    std::make_unique<SplitClipCommand>(cid, splitTime, tempo);
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

            case 7:  // Loop Settings
                // TODO: Show loop settings dialog
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Loop Settings",
                                                       "Loop settings dialog not yet implemented");
                break;

            case 8: {  // Join Clips
                auto selectedClips = selectionManager.getSelectedClips();
                if (selectedClips.size() >= 2) {
                    std::vector<ClipId> sorted(selectedClips.begin(), selectedClips.end());
                    std::sort(sorted.begin(), sorted.end(), [&](ClipId a, ClipId b) {
                        auto* ca = clipManager.getClip(a);
                        auto* cb = clipManager.getClip(b);
                        if (!ca || !cb)
                            return false;
                        return ca->startTime < cb->startTime;
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

            default:
                break;
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
