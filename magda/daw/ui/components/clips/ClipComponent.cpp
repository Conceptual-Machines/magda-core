#include "ClipComponent.hpp"

#include <BinaryData.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numeric>
#include <unordered_set>

#include "../../dialogs/AISettingsDialog.hpp"
#include "../../panels/state/PanelController.hpp"
#include "../../state/TimelineController.hpp"
#include "../../state/TimelineEvents.hpp"
#include "../../themes/CursorManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../utils/SelectionPolicy.hpp"
#include "../common/Toast.hpp"
#include "../tracks/TrackContentPanel.hpp"
#include "../waveform/ClipWaveformPainter.hpp"
#include "../waveform/WarpedWaveformRenderer.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/AppPaths.hpp"
#include "core/ChordAnnotationCommands.hpp"
#include "core/ChordProgressionConverter.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/GestureRouter.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/PasteTargetResolver.hpp"
#include "core/SelectionManager.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "stem_separation/DemucsSeparator.hpp"
#include "stem_separation/StemSeparationService.hpp"
#include "transcription/TranscriptionService.hpp"

namespace magda {

namespace {

// These route through the position-aware tempo facade (single source of truth)
// when it's available, falling back to the constant-tempo bpm only before the
// facade is wired. ClipComponent runs on the message thread, where
// TimelineController::getCurrent()->tempoMap() is valid.
double timelineStartSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineStart(*tc->tempoMap());
    return clip.getTimelineStart(bpm);
}

double timelineLengthSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineLength(*tc->tempoMap());
    return clip.getTimelineLength(bpm);
}

double timelineEndSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineEnd(*tc->tempoMap());
    return clip.getTimelineEnd(bpm);
}

// Left-resize moves the clip start, so the preview has to carry the derived
// source-domain phase (offset/loopStart/midiOffset) across as well as the
// placement. Used for the dragged clip and every other clip in the selection.
void applyLeftResizePreview(ClipInfo& target, const ClipInfo& preview, double bpm) {
    ClipOperations::setTimelinePlacement(target, timelineStartSeconds(preview, bpm),
                                         timelineLengthSeconds(preview, bpm), bpm);
    if (auto* targetEvent = target.primaryEvent()) {
        targetEvent->sourceAnchorSamples = audioEventRef(preview).sourceAnchorSamples;
        targetEvent->loopStartSamples = audioEventRef(preview).loopStartSamples;
    }
    target.midiOffset = preview.midiOffset;
    // resizeContainerFromLeft accumulates the start move here, and the piano
    // roll positions notes through it. Leaving it behind makes the notes drift
    // against the clip body for the length of the drag.
    target.midiTrimOffset = preview.midiTrimOffset;
}

// Undo the fields applyLeftResizePreview touched (plus midiTrimOffset, which
// resizeContainerFromLeft accumulates), so the resize commands capture the
// pre-drag state for undo.
void restoreLeftResizePreview(ClipInfo& target, const ClipInfo& snapshot, double startSeconds,
                              double lengthSeconds, double bpm) {
    ClipOperations::setTimelinePlacement(target, startSeconds, lengthSeconds, bpm);
    if (auto* targetEvent = target.primaryEvent()) {
        targetEvent->sourceAnchorSamples = audioEventRef(snapshot).sourceAnchorSamples;
        targetEvent->loopStartSamples = audioEventRef(snapshot).loopStartSamples;
    }
    target.midiOffset = snapshot.midiOffset;
    target.midiTrimOffset = snapshot.midiTrimOffset;
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

// Run chord detection on a MIDI clip and write the result onto the singleton
// chord track as a new "Progression" clip (canonical voicings + linked chord
// blocks). When `replace` is set the chord track's existing clips are cleared
// first; otherwise the new progression is appended. One undo step. (#1506)
void extractChordsToChordTrack(magda::ClipId sourceClipId, bool replace) {
    auto& cm = ClipManager::getInstance();
    const auto* source = cm.getClip(sourceClipId);
    if (source == nullptr || !source->isMidi() || source->midiNotes.empty())
        return;

    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;

    const auto extracted = magda::extractChordsFromNotes(source->midiNotes, beatsPerBar);
    if (extracted.empty()) {
        magda::daw::ui::Toast::showGlobal("No chords detected in clip");
        return;
    }

    // Span the chord clip over the source's beat range so the progression sits
    // beneath the notes it came from.
    const double clipStart = source->placement.startBeat;
    double extractEnd = 0.0;
    for (const auto& ex : extracted)
        extractEnd = std::max(extractEnd, ex.startBeat + ex.lengthBeats);
    const double clipLength = std::max(source->placement.lengthBeats, extractEnd);

    auto& tm = TrackManager::getInstance();
    const auto chordTrackId = tm.ensureChordTrack();
    if (chordTrackId == magda::INVALID_TRACK_ID)
        return;

    magda::CompoundOperationScope scope("Extract Chords to Chord Track");
    auto& undo = UndoManager::getInstance();

    if (replace) {
        for (const auto cid : cm.getClipsOnTrack(chordTrackId))
            undo.executeCommand(std::make_unique<DeleteClipCommand>(cid));
    }

    auto createCmd = std::make_unique<CreateClipCommand>(
        magda::ClipType::MIDI, chordTrackId, BeatPosition{clipStart}, BeatDuration{clipLength});
    auto* createPtr = createCmd.get();
    undo.executeCommand(std::move(createCmd));
    const auto newClipId = createPtr->getCreatedClipId();
    auto* newClip = cm.getClip(newClipId);
    if (newClip == nullptr)
        return;

    for (const auto& ex : extracted) {
        const int groupId = newClip->nextChordGroupId++;

        auto notes =
            magda::buildVoicingNotes(ex.root, ex.quality, ex.startBeat, ex.lengthBeats, 100);
        for (auto& n : notes)
            n.chordGroup = groupId;

        magda::ClipInfo::ChordAnnotation annotation;
        annotation.beatPosition = ex.startBeat;
        annotation.lengthBeats = ex.lengthBeats;
        annotation.chordName = ex.name;
        annotation.chordGroup = groupId;
        undo.executeCommand(std::make_unique<AddChordAnnotationCommand>(newClipId, annotation));

        if (!notes.empty())
            undo.executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
                newClipId, std::move(notes), "Add Chord Voicing"));
    }

    cm.forceNotifyClipPropertyChanged(newClipId);
    magda::daw::ui::Toast::showGlobal(
        replace ? juce::String("Replaced chord track with detected chords")
                : "Extracted " + juce::String(static_cast<int>(extracted.size())) +
                      " chords to chord track");
}

// Bake a chord-track progression onto a normal track as a plain, editable MIDI
// clip: the voicings come across as notes, the chord-lane annotations are
// dropped so the result is decoupled from the chord engine. (#1503)
void sendProgressionToTrack(magda::ClipId chordClipId, magda::TrackId targetTrackId) {
    auto& cm = ClipManager::getInstance();
    const auto* src = cm.getClip(chordClipId);
    if (src == nullptr || !src->isMidi() || targetTrackId == magda::INVALID_TRACK_ID)
        return;

    double tempo = 120.0;
    if (auto* tc = TimelineController::getCurrent())
        tempo = tc->getState().tempo.bpm;

    magda::CompoundOperationScope scope("Progression to MIDI Clip");
    auto& undo = UndoManager::getInstance();

    auto dupCmd = std::make_unique<DuplicateClipCommand>(
        chordClipId, BeatPosition{src->placement.startBeat}, targetTrackId, tempo);
    auto* dupPtr = dupCmd.get();
    undo.executeCommand(std::move(dupCmd));

    const auto newClipId = dupPtr->getDuplicatedClipId();
    auto* newClip = cm.getClip(newClipId);
    if (newClip == nullptr)
        return;

    // Strip the chord-ness: a plain MIDI clip with the voicings baked into notes.
    newClip->chordAnnotations.clear();
    for (auto& n : newClip->midiNotes)
        n.chordGroup = 0;
    newClip->name = src->name;
    cm.forceNotifyClipPropertyChanged(newClipId);

    magda::daw::ui::Toast::showGlobal("Progression copied as MIDI clip");
}

constexpr int MIDI_PREVIEW_MIN_NOTE = 21;   // A0
constexpr int MIDI_PREVIEW_MAX_NOTE = 108;  // C8

// Context-menu IDs for loop-record take selection (one per take), kept clear of
// the fixed item IDs and the 100-range quantize grid.
constexpr int kTakeMenuBaseId = 300;
constexpr int kTakeMenuMaxItems = 64;

// Context-menu IDs for "Send Progression to Track" (one per eligible track),
// kept clear of the take-selection range (300..363).
constexpr int kProgressionTargetBaseId = 400;

// Length of a crossfade created from the context menu (#1499). The handles
// resize it afterwards; setCrossfadeRegionBeats clamps it into both clips.
constexpr double kDefaultCrossfadeBeats = 0.5;

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
    updateWaveformLoadListener(clip != nullptr ? magda::audioEventRef(*clip).sourceFilePath()
                                               : juce::String());
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
    if (isChordClip(*clip)) {
        paintChordClip(g, *clip, bounds);
    } else if (clip->isAudio()) {
        paintAudioClip(g, *clip, bounds);
    } else {
        paintMidiClip(g, *clip, bounds);
    }

    // Draw header (name, loop indicator)
    paintClipHeader(g, *clip, bounds);

    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;

    // Draw loop boundary corner cuts (after header so they cut through
    // everything). The toggle is the clip's; where the loop LENGTH comes from
    // depends on content: MIDI keeps it in clip beats, audio derives it from
    // the event's source region.
    const auto* loopEvent = clip->primaryEvent();
    const double beatsPerSecondForLoop = tempo / 60.0;
    double loopCutLengthBeats = 0.0;
    if (clip->loopEnabled) {
        if (loopEvent == nullptr) {
            loopCutLengthBeats = clip->loopLengthBeats;
        } else if (const double srcLength = loopEvent->loopLengthSeconds(); srcLength > 0.0) {
            loopCutLengthBeats = (loopEvent->autoTempo && loopEvent->loopLengthBeats() > 0.0)
                                     ? loopEvent->loopLengthBeats()
                                     : srcLength / loopEvent->speedRatio * beatsPerSecondForLoop;
        }
    }

    if (loopCutLengthBeats > 0.0) {
        auto clipBounds = getLocalBounds();
        double beatsPerSecond = beatsPerSecondForLoop;
        // During resize drag, use preview length so boundaries stay fixed
        double displayLength =
            (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip->getTimelineLength(tempo);
        double clipLengthInBeats = displayLength * beatsPerSecond;
        // Loop length in beats: use authoritative beat value for autoTempo,
        // otherwise derive from source length and speedRatio
        const double loopLengthBeats = loopCutLengthBeats;
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

    // Where this clip stands on another one (#2003). A stack is otherwise
    // indistinguishable from a single clip, since the clip on top is opaque and
    // the material it silences is completely hidden. Hatching the covering
    // stretch says "there is a clip under here" at a glance. It stays inside
    // the body: hatching the header too turned the clip into a black striped
    // block that read as broken rather than as stacked.
    if (!coveringRanges_.empty() && parentPanel_ != nullptr) {
        const int clipLeft = getX();
        const auto body = bounds.withTrimmedTop(HEADER_HEIGHT);

        for (const auto& range : coveringRanges_) {
            const int from = parentPanel_->beatsToPixel(range.start.value) - clipLeft;
            const int to = parentPanel_->beatsToPixel(range.end.value) - clipLeft;
            auto region = body.getIntersection(juce::Rectangle<int>(
                from, body.getY(), juce::jmax(1, to - from), body.getHeight()));
            if (region.isEmpty())
                continue;

            juce::Graphics::ScopedSaveState clipped(g);
            g.reduceClipRegion(region);

            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.fillRect(region);

            // Diagonal hatch, one line every 6px, running the full height so it
            // reads at any clip size.
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            const float height = static_cast<float>(region.getHeight());
            for (float x = static_cast<float>(region.getX()) - height;
                 x < static_cast<float>(region.getRight()); x += 6.0f) {
                g.drawLine(x, static_cast<float>(region.getBottom()), x + height,
                           static_cast<float>(region.getY()), 1.0f);
            }

            // Edges of the covered stretch, so a cover starting or ending inside
            // this clip has a readable boundary.
            g.setColour(juce::Colours::white.withAlpha(0.28f));
            g.drawVerticalLine(region.getX(), static_cast<float>(region.getY()),
                               static_cast<float>(region.getBottom()));
            g.drawVerticalLine(region.getRight() - 1, static_cast<float>(region.getY()),
                               static_cast<float>(region.getBottom()));
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

    // Disabled overlay (#1736) — heavier than the ghost treatment and the
    // frozen/session dims, and it covers the header too, so a disabled clip
    // reads as "off" at a glance (ghosts keep a full-colour header).
    if (!clip->enabled) {
        fillClippedRoundedRect(g, bounds, visibleBounds, juce::Colours::black.withAlpha(0.55f),
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
    combine(std::hash<juce::String>{}(audioEventRef(clip).sourceFilePath()));
    combine(std::hash<double>{}(clip.placement.lengthBeats));
    combine(std::hash<double>{}(audioEventRef(clip).anchorSeconds()));
    combine(std::hash<double>{}(audioEventRef(clip).speedRatio));
    combine(std::hash<float>{}(clip.volumeDB));
    combine(std::hash<float>{}(clip.gainDB));
    combine(std::hash<bool>{}(audioEventRef(clip).reversed));
    combine(std::hash<bool>{}(clip.loopEnabled));
    combine(std::hash<double>{}(audioEventRef(clip).loopStartSeconds()));
    combine(std::hash<double>{}(audioEventRef(clip).loopLengthSeconds()));
    combine(std::hash<double>{}(clip.loopLengthBeats));
    combine(std::hash<long long>{}(audioEventRef(clip).loopLengthSamples));
    combine(std::hash<double>{}(audioEventRef(clip).interpTotalBeats));
    combine(std::hash<bool>{}(audioEventRef(clip).warpEnabled));
    combine(std::hash<bool>{}(audioEventRef(clip).autoTempo));
    combine(std::hash<double>{}(audioEventRef(clip).fadeInSeconds));
    combine(std::hash<double>{}(audioEventRef(clip).fadeOutSeconds));
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
    // Rendering lives in the shared painter (also used by the automation
    // clip editor's track ghost); this wrapper supplies the arrangement
    // component's state: tempo, selection stroke, and resize-drag preview.
    daw::ui::ClipWaveformSpec spec;
    spec.tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    spec.colour = waveColourOverride.isTransparent() ? juce::Colours::black : waveColourOverride;
    spec.thick = isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_);
    if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
        spec.previewOffset = audioEventRef(resizePreviewClip_).anchorSeconds();
        spec.previewLoopStart = audioEventRef(resizePreviewClip_).loopStartSeconds();
    }
    daw::ui::paintClipWaveform(g, clip, clipId_, waveformArea, clipDisplayLength, spec);
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
    updateWaveformLoadListener(audioEventRef(clip).sourceFilePath());

    // Draw directly — no offscreen cache.  AudioThumbnail is already a
    // pre-computed waveform cache (512 samples/point) so drawing from it is fast.
    // Ghost clips paint a translucent body + dimmed waveform so link-group
    // members read as mirrors of shared content.
    const bool ghosted = ClipManager::getInstance().isGhostClip(clipId_);
    auto bgColour = deriveClipBody(clip.colour);
    if (ghosted)
        bgColour = bgColour.withAlpha(0.55f);
    fillClippedRoundedRect(g, bounds, visibleBounds, bgColour, CORNER_RADIUS);

    if (audioEventRef(clip).sourceFilePath().isNotEmpty())
        paintAudioClipDirect(g, clip, waveformArea, clipDisplayLength,
                             ghosted ? juce::Colours::black.withAlpha(0.65f) : juce::Colour());

    strokeClippedRoundedRect(g, bounds, visibleBounds, deriveTrackSwatch(clip.colour, 0.45f),
                             CORNER_RADIUS, 1.0f);

    // Fade overlays (crossfaded edges show the overlap-derived fade, #1499)
    const auto fades = computeEffectiveFades(clip);
    if (fades.fadeInSeconds > 0.0 || fades.fadeOutSeconds > 0.0) {
        double pps = (clipDisplayLength > 0.0)
                         ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                         : 0.0;
        if (pps > 0.0)
            paintFadeOverlays(g, clip, fades, waveformArea, pps);
    }
}

ClipComponent::EffectiveFades ClipComponent::computeEffectiveFades(const ClipInfo& clip) const {
    EffectiveFades fades;
    fades.fadeInSeconds = audioEventRef(clip).fadeInSeconds;
    fades.fadeOutSeconds = audioEventRef(clip).fadeOutSeconds;
    if (!clip.isAudio() || clip.view != ClipView::Arrangement)
        return fades;

    auto& cm = ClipManager::getInstance();
    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
    if (auto xf = cm.getCrossfadeAtStart(clipId_)) {
        fades.xfIn = xf;
        fades.fadeInSeconds = xf->lengthBeats() * 60.0 / tempo;
    }
    if (auto xf = cm.getCrossfadeAtEnd(clipId_)) {
        fades.xfOut = xf;
        fades.fadeOutSeconds = xf->lengthBeats() * 60.0 / tempo;
    }
    return fades;
}

ClipId ClipComponent::findCrossfadeNeighbour(bool atStart) const {
    return ClipManager::getInstance().findCrossfadeNeighbour(clipId_, atStart);
}

void ClipComponent::paintMidiClip(juce::Graphics& g, const ClipInfo& clip,
                                  juce::Rectangle<int> bounds) {
    auto visibleBounds = bounds.getIntersection(g.getClipBounds());
    if (visibleBounds.isEmpty())
        return;

    // Ghost clips paint a translucent body + dimmed notes (see paintAudioClip).
    const bool ghosted = ClipManager::getInstance().isGhostClip(clipId_);
    auto bgColour = deriveClipBody(clip.colour);
    if (ghosted)
        bgColour = bgColour.withAlpha(0.55f);
    fillClippedRoundedRect(g, bounds, visibleBounds, bgColour, CORNER_RADIUS);

    auto noteArea = bounds.withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2);
    paintMidiNotes(g, clip, noteArea,
                   ghosted ? juce::Colours::black.withAlpha(0.65f) : juce::Colours::black);

    strokeClippedRoundedRect(g, bounds, visibleBounds, deriveTrackSwatch(clip.colour, 0.45f),
                             CORNER_RADIUS, 1.0f);
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

    // MIDI loops in clip beats on the container. There is no audio event here
    // and no source region to consult.
    double loopLengthBeats = clip.loopLengthBeats > 0.0 ? clip.loopLengthBeats : clipLengthInBeats;

    double midiOffset;
    if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
        midiOffset =
            clip.loopEnabled ? resizePreviewClip_.midiOffset : resizePreviewClip_.midiTrimOffset;
    } else {
        midiOffset = clip.loopEnabled ? clip.midiOffset : clip.midiTrimOffset;
    }

    double loopStart = clip.loopStartBeats;
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

bool ClipComponent::isChordClip(const ClipInfo& clip) const {
    const auto* track = TrackManager::getInstance().getTrack(clip.trackId);
    return track != nullptr && track->type == TrackType::Chord;
}

void ClipComponent::paintChordClip(juce::Graphics& g, const ClipInfo& clip,
                                   juce::Rectangle<int> bounds) {
    auto visibleBounds = bounds.getIntersection(g.getClipBounds());
    if (visibleBounds.isEmpty())
        return;

    const bool selected = isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_);

    // Translucent base so the timeline shows through (track-map style) rather
    // than a solid pastel card. Selection makes the clip body read as the track
    // colour while keeping the black selected header separate.
    auto bgColour = deriveTrackSwatch(clip.colour, selected ? 0.24f : 0.16f);
    fillClippedRoundedRect(g, bounds, visibleBounds, bgColour, CORNER_RADIUS);

    auto blockArea = bounds.withTrimmedTop(HEADER_HEIGHT + 2).withTrimmedBottom(2).reduced(2, 0);
    if (!clip.chordAnnotations.empty() && blockArea.getHeight() > 5 && blockArea.getWidth() > 2) {
        const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
        const double beatsPerSecond = tempo / 60.0;
        const double displayLength =
            (isDragging_ && previewLength_ > 0.0) ? previewLength_ : clip.getTimelineLength(tempo);
        const double beatRange = juce::jmax(1.0, displayLength * beatsPerSecond);

        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        // The chord blocks (glassy card + spine) take the chord track's colour
        // live, so they stay correct after a track recolour (matches the
        // piano-roll grid notes for chord clips).
        auto blockColour = DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
        if (auto* chordTrack = magda::TrackManager::getInstance().getTrack(
                magda::TrackManager::getInstance().getChordTrackId()))
            blockColour = chordTrack->colour;

        auto drawBlock = [&](const juce::String& name, double startBeat, double lengthBeats) {
            const double visibleStart = juce::jmax(0.0, startBeat);
            const double visibleEnd = juce::jmin(beatRange, startBeat + lengthBeats);
            const double visibleLength = visibleEnd - visibleStart;
            if (visibleLength <= 0.0)
                return;
            const float x = blockArea.getX() +
                            static_cast<float>(visibleStart / beatRange) * blockArea.getWidth();
            const float w = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                 blockArea.getWidth());
            juce::Rectangle<float> block(x, static_cast<float>(blockArea.getY()), w,
                                         static_cast<float>(blockArea.getHeight()));
            auto inner = block.reduced(1.0f, 0.0f);
            // Glassy translucent block: a soft vertical gradient instead of a
            // flat pastel fill, with a bright top edge and a solid accent spine.
            g.setGradientFill(juce::ColourGradient(blockColour.withAlpha(0.42f), inner.getX(),
                                                   inner.getY(), blockColour.withAlpha(0.14f),
                                                   inner.getX(), inner.getBottom(), false));
            g.fillRoundedRectangle(inner, 2.0f);
            g.setColour(blockColour.withAlpha(0.85f));
            g.fillRect(inner.getX(), inner.getY(), 2.0f, inner.getHeight());  // accent spine
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawRoundedRectangle(inner, 2.0f, 1.0f);  // subtle glass edge
            if (w > MIN_WIDTH_FOR_NAME) {
                g.setColour(juce::Colours::white);
                g.drawText(name, block.toNearestInt().reduced(4, 0),
                           juce::Justification::centredLeft, true);
            }
        };

        // A looped clip tiles its source chords across the timeline, the same way
        // paintMidiNotes repeats notes.
        // Chords annotate MIDI, so the loop is clip beats on the container.
        const double loopLengthBeats =
            clip.loopLengthBeats > 0.0 ? clip.loopLengthBeats : beatRange;

        if (clip.loopEnabled && loopLengthBeats > 0.5) {
            const int reps = static_cast<int>(std::ceil(beatRange / loopLengthBeats));
            for (int r = 0; r < reps; ++r) {
                const double base = r * loopLengthBeats;
                if (base >= beatRange)
                    break;
                for (const auto& chord : clip.chordAnnotations) {
                    if (chord.beatPosition >= loopLengthBeats)
                        continue;  // belongs past this loop iteration
                    const double len =
                        juce::jmin(chord.lengthBeats, loopLengthBeats - chord.beatPosition);
                    drawBlock(chord.chordName, base + chord.beatPosition, len);
                }
            }
        } else {
            for (const auto& chord : clip.chordAnnotations)
                drawBlock(chord.chordName, chord.beatPosition, chord.lengthBeats);
        }
    }

    strokeClippedRoundedRect(g, bounds, visibleBounds, deriveTrackSwatch(clip.colour, 0.45f),
                             CORNER_RADIUS, 1.0f);
}

void ClipComponent::paintClipHeader(juce::Graphics& g, const ClipInfo& clip,
                                    juce::Rectangle<int> bounds) {
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);

    // Selected clips paint a black header in place of the clip-coloured one.
    // This replaces the old white selection rectangle so it can't fight overlay
    // UI (e.g. controller scene-view rectangles).
    const bool selected = isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_);
    const auto headerColour = selected ? juce::Colours::black : deriveTrackSwatch(clip.colour);
    const auto headerForeground =
        selected ? juce::Colours::white : DarkTheme::getColour(DarkTheme::BACKGROUND);

    auto visibleHeaderArea =
        headerArea.withBottom(headerArea.getBottom() + 2).getIntersection(g.getClipBounds());
    if (visibleHeaderArea.isEmpty())
        return;

    fillClippedRoundedRect(g, headerArea.withBottom(headerArea.getBottom() + 2), visibleHeaderArea,
                           headerColour, CORNER_RADIUS);

    // Ghost clips (link-group members) show a link glyph at the left of the
    // header and an italicised name.
    const bool ghosted = ClipManager::getInstance().isGhostClip(clipId_);
    if (ghosted && headerArea.getWidth() > HEADER_HEIGHT + 4) {
        auto iconArea = headerArea.removeFromLeft(HEADER_HEIGHT).reduced(3);
        if (iconArea.intersects(g.getClipBounds())) {
            static const auto linkIcon = juce::Drawable::createFromImageData(
                BinaryData::link_flat_svg, BinaryData::link_flat_svgSize);
            if (linkIcon) {
                auto themedIcon = linkIcon->createCopy();
                themedIcon->replaceColour(juce::Colour(0xFFB3B3B3), headerForeground);
                DarkTheme::applyToSvgIcon(*themedIcon);
                themedIcon->drawWithin(g, iconArea.toFloat(), juce::RectanglePlacement::centred,
                                       1.0f);
            }
        }
    }

    // Chord clips show the chord glyph at the left of the header.
    if (isChordClip(clip) && headerArea.getWidth() > HEADER_HEIGHT + 4) {
        auto iconArea = headerArea.removeFromLeft(HEADER_HEIGHT).reduced(3);
        if (iconArea.intersects(g.getClipBounds())) {
            static const auto chordIcon = juce::Drawable::createFromImageData(
                BinaryData::iconchordboldm_svg, BinaryData::iconchordboldm_svgSize);
            if (chordIcon) {
                auto themedIcon = chordIcon->createCopy();
                themedIcon->replaceColour(juce::Colour(0xFFB3B3B3), headerForeground);
                DarkTheme::applyToSvgIcon(*themedIcon);
                themedIcon->drawWithin(g, iconArea.toFloat(), juce::RectanglePlacement::centred,
                                       1.0f);
            }
        }
    }

    // Clip name (italic for ghost clips, with the instance index appended —
    // group members share their name, the #index tells them apart)
    if (bounds.getWidth() > MIN_WIDTH_FOR_NAME) {
        auto nameArea = headerArea.withWidth(juce::jmin(headerArea.getWidth(), 300)).reduced(4, 0);
        if (nameArea.intersects(g.getClipBounds())) {
            g.setColour(headerForeground);
            auto nameFont = FontManager::getInstance().getUIFont(10.0f);
            g.setFont(ghosted ? nameFont.italicised() : nameFont);
            auto displayName = clip.name;
            if (ghosted) {
                const int groupIndex = ClipManager::getInstance().getLinkGroupIndex(clipId_);
                if (groupIndex > 0)
                    displayName += " #" + juce::String(groupIndex);
            }
            g.drawText(displayName, nameArea, juce::Justification::centredLeft, true);
        }
    }

    // Musical mode indicator (auto-tempo)
    if (audioEventRef(clip).autoTempo && clip.isAudio() && headerArea.getWidth() > 16) {
        auto musicalArea = headerArea.removeFromRight(14).reduced(2);
        g.setColour(headerForeground);
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xa9"), musicalArea,
                   juce::Justification::centred, false);
    }

    // Loop indicator: the transport's circular-arrows loop glyph, so "loop"
    // reads the same everywhere (and stays distinct from the ghost link icon).
    // Cache one drawable per foreground variant — selection flips foreground,
    // so we can't bake a single colour at construction.
    if (clip.loopEnabled && headerArea.getWidth() > 16) {
        headerArea.removeFromRight(2);  // right padding
        // Same box as the ghost link icon on the left (HEADER_HEIGHT reduced
        // by 3) so the two header glyphs read at the same size.
        auto loopArea = headerArea.removeFromRight(HEADER_HEIGHT).reduced(3);
        if (loopArea.getWidth() > 0 && loopArea.getHeight() > 0) {
            static const auto loopIcon = juce::Drawable::createFromImageData(
                BinaryData::loop_icon_svg, BinaryData::loop_icon_svgSize);
            if (loopIcon) {
                auto themedIcon = loopIcon->createCopy();
                themedIcon->replaceColour(juce::Colour(0xFFBCBCBC), headerForeground);
                DarkTheme::applyToSvgIcon(*themedIcon);
                themedIcon->drawWithin(g, loopArea.toFloat(), juce::RectanglePlacement::centred,
                                       1.0f);
            }
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
                                      const EffectiveFades& fades,
                                      juce::Rectangle<int> waveformArea, double pixelsPerSecond) {
    constexpr int NUM_STEPS = 32;
    float areaTop = static_cast<float>(waveformArea.getY());
    float areaBottom = static_cast<float>(waveformArea.getBottom());
    float areaHeight = areaBottom - areaTop;
    float areaLeft = static_cast<float>(waveformArea.getX());
    float areaRight = static_cast<float>(waveformArea.getRight());

    // The counterpart curve of a crossfade (the other clip's fade across the
    // same overlap) — drawn by both components of the pair so the X reads the
    // same whichever one is on top.
    auto strokeCounterpartCurve = [&](float regionStartPx, float regionWidthPx, FadeCurve type,
                                      bool descending) {
        juce::Path curve;
        for (int i = 0; i <= NUM_STEPS; ++i) {
            float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
            float gain = computeFadeGain(descending ? 1.0f - alpha : alpha, type);
            float x = regionStartPx + alpha * regionWidthPx;
            float y = areaTop + (1.0f - gain) * areaHeight;
            if (i == 0)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    };

    // Fade-in overlay
    if (fades.fadeInSeconds > 0.0) {
        float fadeInPx = juce::jmin(static_cast<float>(fades.fadeInSeconds * pixelsPerSecond),
                                    static_cast<float>(waveformArea.getWidth()));
        if (fadeInPx > 1.0f) {
            // Build overlay path: darkens area above the fade curve
            juce::Path overlay;
            overlay.startNewSubPath(areaLeft, areaTop);
            overlay.lineTo(areaLeft + fadeInPx, areaTop);

            // Trace the fade curve from right to left (gain 1→0)
            for (int i = NUM_STEPS; i >= 0; --i) {
                float alpha = static_cast<float>(i) / static_cast<float>(NUM_STEPS);
                float gain =
                    computeFadeGain(alpha, static_cast<FadeCurve>(audioEventRef(clip).fadeInType));
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
                float gain =
                    computeFadeGain(alpha, static_cast<FadeCurve>(audioEventRef(clip).fadeInType));
                float x = areaLeft + alpha * fadeInPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                if (i == 0)
                    curveLine.startNewSubPath(x, y);
                else
                    curveLine.lineTo(x, y);
            }
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.strokePath(curveLine, juce::PathStrokeType(1.5f));

            // Crossfade: overlay the previous clip's fade-out curve (the X)
            if (fades.xfIn) {
                auto* other = ClipManager::getInstance().getClip(fades.xfIn->leftClipId);
                strokeCounterpartCurve(
                    areaLeft, fadeInPx,
                    static_cast<FadeCurve>(other ? audioEventRef(*other).fadeOutType : 1), true);
            }
        }
    }

    // Fade-out overlay
    if (fades.fadeOutSeconds > 0.0) {
        float fadeOutPx = juce::jmin(static_cast<float>(fades.fadeOutSeconds * pixelsPerSecond),
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
                float gain = computeFadeGain(
                    1.0f - alpha, static_cast<FadeCurve>(audioEventRef(clip).fadeOutType));
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
                float gain = computeFadeGain(
                    1.0f - alpha, static_cast<FadeCurve>(audioEventRef(clip).fadeOutType));
                float x = fadeStart + alpha * fadeOutPx;
                float y = areaTop + (1.0f - gain) * areaHeight;
                if (i == 0)
                    curveLine.startNewSubPath(x, y);
                else
                    curveLine.lineTo(x, y);
            }
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.strokePath(curveLine, juce::PathStrokeType(1.5f));

            // Crossfade: overlay the next clip's fade-in curve (the X)
            if (fades.xfOut) {
                auto* other = ClipManager::getInstance().getClip(fades.xfOut->rightClipId);
                strokeCounterpartCurve(
                    fadeStart, fadeOutPx,
                    static_cast<FadeCurve>(other ? audioEventRef(*other).fadeInType : 1), false);
            }
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

    auto handleColour = DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION);
    const auto fades = computeEffectiveFades(clip);

    // Fade-in handle: only visible on hover
    if (hoverFadeIn_) {
        float fadeInPx = static_cast<float>(fades.fadeInSeconds * pixelsPerSecond);
        float cx = static_cast<float>(waveformArea.getX()) + fadeInPx;
        g.setColour(handleColour);
        g.fillRect(cx - half, waveTop, hs, hs);
    }

    // Fade-out handle: only visible on hover
    if (hoverFadeOut_) {
        float fadeOutPx = static_cast<float>(fades.fadeOutSeconds * pixelsPerSecond);
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

void ClipComponent::captureMultiResizeSnapshots() {
    dragStartSelectedLengths_.clear();
    dragStartSelectedClipSnapshots_.clear();

    const auto& selected = SelectionManager::getInstance().getSelectedClips();
    if (selected.size() <= 1 || selected.count(clipId_) == 0)
        return;

    auto& cm = ClipManager::getInstance();
    const double tempo = parentPanel_ ? parentPanel_->getTempo() : DEFAULT_BPM;
    for (auto cid : selected) {
        if (cid == clipId_)
            continue;
        if (const auto* c = cm.getClip(cid)) {
            dragStartSelectedLengths_[cid] = timelineLengthSeconds(*c, tempo);
            dragStartSelectedClipSnapshots_[cid] = *c;
        }
    }
}

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

    // Helper: point the bottom panel at the editor for the current clip type.
    // It never expands a collapsed panel — selecting a clip is not a request to
    // reopen a panel the user collapsed (issue #1963); only explicit gestures
    // such as double-click do that.
    auto focusEditorTab = [](ClipId id) {
        const auto* c = ClipManager::getInstance().getClip(id);
        if (!c)
            return;
        // Don't force a specific MIDI editor tab — BottomPanel's clipSelectionChanged
        // handles the PianoRoll vs DrumGrid choice, respecting the user's preference.
        if (c->isAudio()) {
            daw::ui::PanelController::getInstance().setActiveTabByType(
                daw::ui::PanelLocation::Bottom, daw::ui::PanelContentType::WaveformEditor);
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
        focusEditorTab(clipId_);
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
            focusEditorTab(clipId_);
        }

        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    // Shift+edge = stretch (falls through to drag setup); Shift+body = range
    // select from the anchor, applied immediately. Dragging afterwards moves
    // the selected range via the multi-drag path.
    bool didRangeSelect = false;
    pendingCopyDragAction_ = false;
    pendingCopyDragIsGhost_ = false;
    if (magda::GestureRouter::getInstance().isDuplicateAsGhostOnDrag(
            magda::GestureContext::Arrangement, e.mods)) {
        // Ghost-copy modifier (default Alt+Shift): checked before the Shift
        // branch, which would otherwise swallow the combination. The copy
        // joins the source's link group and mirrors its content.
        pendingCopyDragAction_ = true;
        pendingCopyDragIsGhost_ = true;
    } else if (e.mods.isShiftDown()) {
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
                focusEditorTab(clipId_);
            }
            logArrangeRangeSelect(
                "ClipComponent range branch after editor-open: selectedCount=" +
                juce::String(static_cast<int>(selectionManager.getSelectedClipCount())) +
                " anchor=" + juce::String(static_cast<int>(selectionManager.getAnchorClip())));
        }
    } else if (magda::GestureRouter::getInstance().isDuplicateOnDrag(
                   magda::GestureContext::Arrangement, e.mods)) {
        // Copy-drag modifier on the body (default Alt, customisable in
        // Preferences → Gestures): copy on drag, edit cursor on click — both
        // resolve later, so the selection stays untouched for now.
        pendingCopyDragAction_ = true;
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

    if (pendingCopyDragAction_) {
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
    dragStartAudioOffset_ = magda::audioEventRef(*clip).anchorSeconds();

    // Cache file duration for resize clamping
    dragStartFileDuration_ = 0.0;
    if (clip->isAudio() && magda::audioEventRef(*clip).sourceFilePath().isNotEmpty()) {
        auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(
            magda::audioEventRef(*clip).sourceFilePath());
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
            int newType = (magda::audioEventRef(*clip).fadeInType % 4) + 1;
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetClipFadeInTypeCommand>(clipId_, newType));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        // Crossfaded start edge: the handle drives the overlap with the
        // previous clip (moves its end) instead of this clip's own fade-in.
        if (auto xfIn = computeEffectiveFades(*clip).xfIn) {
            if (const auto* other = ClipManager::getInstance().getClip(xfIn->leftClipId)) {
                dragMode_ = DragMode::CrossfadeIn;
                crossfadeDragPair_ = *xfIn;
                dragStartClipSnapshot_ = *clip;
                crossfadeOtherSnapshot_ = *other;
                repaint();
                return;
            }
        }
        dragMode_ = DragMode::FadeIn;
        dragStartFadeIn_ = magda::audioEventRef(*clip).fadeInSeconds;
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
            int newType = (magda::audioEventRef(*clip).fadeOutType % 4) + 1;
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetClipFadeOutTypeCommand>(clipId_, newType));
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
        // Crossfaded end edge: the handle drives the overlap with the next
        // clip (moves its start) instead of this clip's own fade-out.
        if (auto xfOut = computeEffectiveFades(*clip).xfOut) {
            if (const auto* other = ClipManager::getInstance().getClip(xfOut->rightClipId)) {
                dragMode_ = DragMode::CrossfadeOut;
                crossfadeDragPair_ = *xfOut;
                dragStartClipSnapshot_ = *clip;
                crossfadeOtherSnapshot_ = *other;
                repaint();
                return;
            }
        }
        dragMode_ = DragMode::FadeOut;
        dragStartFadeOut_ = magda::audioEventRef(*clip).fadeOutSeconds;
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
            ((clip->isAudio() && magda::audioEventRef(*clip).sourceFilePath().isNotEmpty()) ||
             clip->isMidi())) {
            dragMode_ = DragMode::StretchLeft;
            dragStartSpeedRatio_ = magda::audioEventRef(*clip).speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeLeft;
            dragStartClipSnapshot_ = *clip;
            resizePreviewClip_ = *clip;
            // Capture original state of other selected clips so the drag can
            // preview them resizing together (the commit path already does)
            captureMultiResizeSnapshots();

            // Mirror of the ResizeRight clamp below: the gesture applies one
            // delta to every selected clip, so it has to stop at whichever
            // clip meets a non-selected neighbour first. Without this the
            // preview runs straight through clips the user cannot see, and
            // resolveOverlaps silently trims or deletes them on mouse up.
            multiResizeMaxLeftDelta_ = std::numeric_limits<double>::max();
            const auto& selectedLeft = SelectionManager::getInstance().getSelectedClips();
            if (selectedLeft.size() > 1 && selectedLeft.count(clipId_)) {
                auto& cm = ClipManager::getInstance();
                const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                for (auto cid : selectedLeft) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;

                    const double cStart = timelineStartSeconds(*c, tempo);
                    // Bar 0 is a hard wall even with no neighbour in the way.
                    multiResizeMaxLeftDelta_ = juce::jmin(multiResizeMaxLeftDelta_, cStart);

                    for (auto otherId : cm.getClipsOnTrack(c->trackId)) {
                        if (selectedLeft.count(otherId))
                            continue;
                        const auto* other = cm.getClip(otherId);
                        if (other && timelineStartSeconds(*other, tempo) < cStart) {
                            const double gap = cStart - timelineEndSeconds(*other, tempo);
                            multiResizeMaxLeftDelta_ = juce::jmin(multiResizeMaxLeftDelta_, gap);
                        }
                    }
                }
                multiResizeMaxLeftDelta_ = juce::jmax(0.0, multiResizeMaxLeftDelta_);
            }
        }
    } else if (isOnRightEdge(e.x)) {
        if (e.mods.isShiftDown() &&
            ((clip->isAudio() && magda::audioEventRef(*clip).sourceFilePath().isNotEmpty()) ||
             clip->isMidi())) {
            dragMode_ = DragMode::StretchRight;
            dragStartSpeedRatio_ = magda::audioEventRef(*clip).speedRatio;
            dragStartClipSnapshot_ = *clip;
        } else {
            dragMode_ = DragMode::ResizeRight;
            dragStartClipSnapshot_ = *clip;
            // Capture original lengths of other selected clips for multi-resize
            captureMultiResizeSnapshots();
            multiResizeMaxDelta_ = std::numeric_limits<double>::max();
            const auto& selected = SelectionManager::getInstance().getSelectedClips();
            if (selected.size() > 1 && selected.count(clipId_)) {
                auto& cm = ClipManager::getInstance();
                const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                for (auto cid : selected) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;

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
    if (pendingCopyDragAction_) {
        if (e.getDistanceFromDragStart() < 4)
            return;  // still a click
        pendingCopyDragAction_ = false;
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
            isDuplicatingGhost_ = pendingCopyDragIsGhost_;
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
                // Alt+drag duplicate: show ghost at the NEW position, following the
                // mouse onto the target track, keeping the original in place.
                const auto* clip = getClipInfo();
                if (clip && parentPanel_) {
                    double finalBeats = finalTime * tempoBPM / 60.0;
                    int ghostX = parentPanel_->beatsToPixel(finalBeats);
                    double lengthBeats = dragStartLength_ * tempoBPM / 60.0;
                    int ghostWidth = static_cast<int>(std::round(lengthBeats * pixelsPerBeat));

                    // Follow the mouse vertically so the ghost lands on whatever
                    // track the copy will drop onto (matches the mouseUp target).
                    int ghostY = getY();
                    int ghostH = getHeight();
                    const int localY =
                        e.getScreenPosition().y - parentPanel_->getScreenBounds().getPosition().y;
                    const int trackIndex = parentPanel_->getTrackIndexAtY(localY);
                    if (trackIndex >= 0) {
                        ghostY = parentPanel_->getTrackYPosition(trackIndex);
                        ghostH = parentPanel_->getTrackTotalHeight(trackIndex);
                    }

                    juce::Rectangle<int> ghostBounds(ghostX, ghostY, juce::jmax(10, ghostWidth),
                                                     ghostH);
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

            // Clamp so no clip in the selection overruns a preceding
            // non-selected clip (or bar 0).
            if (!dragStartSelectedLengths_.empty()) {
                const double maxLength = dragStartLength_ + multiResizeMaxLeftDelta_;
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
                if (auto* previewEvent = resizePreviewClip_.primaryEvent())
                    previewEvent->loopStartSamples = previewEvent->sourceAnchorSamples;
            }

            // Throttled: sync to TE for waveform/audio playback
            if (resizeThrottle_.check()) {
                auto& cm = magda::ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    applyLeftResizePreview(*mutableClip, resizePreviewClip_, tempoBPM);

                    std::vector<magda::ClipId> changedClips;
                    changedClips.push_back(clipId_);

                    // Also preview the other selected clips with the same delta,
                    // each recomputed from its own pre-drag snapshot
                    const double lengthDelta = finalLength - dragStartLength_;
                    for (auto& [cid, origLen] : dragStartSelectedLengths_) {
                        auto snapshotIt = dragStartSelectedClipSnapshots_.find(cid);
                        if (snapshotIt == dragStartSelectedClipSnapshots_.end())
                            continue;
                        auto* otherClip = cm.getClip(cid);
                        if (!otherClip)
                            continue;

                        ClipInfo otherPreview = snapshotIt->second;
                        ClipOperations::resizeContainerFromLeft(
                            otherPreview, juce::jmax(0.1, origLen + lengthDelta), tempoBPM);
                        if (auto* otherEvent = otherPreview.primaryEvent();
                            otherEvent != nullptr && !otherPreview.loopEnabled) {
                            otherEvent->loopStartSamples = otherEvent->sourceAnchorSamples;
                        }

                        applyLeftResizePreview(*otherClip, otherPreview, tempoBPM);
                        changedClips.push_back(cid);
                    }

                    cm.forceNotifyMultipleClipPropertiesChanged(changedClips);
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
                    ci ? timelineLengthSeconds(*ci, tempoBPM) - audioEventRef(*ci).fadeOutSeconds
                       : dragStartLength_;
                newFadeIn = juce::jmin(newFadeIn, juce::jmax(0.0, maxFadeIn));
                double fadeDelta = newFadeIn - dragStartFadeIn_;
                auto& cm = ClipManager::getInstance();
                cm.setFadeIn(clipId_, newFadeIn);
                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;
                    double otherFade =
                        juce::jmax(0.0, audioEventRef(snap).fadeInSeconds + fadeDelta);
                    otherFade = juce::jmin(
                        otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, tempoBPM) -
                                                       magda::audioEventRef(*c).fadeOutSeconds));
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
                    ci ? timelineLengthSeconds(*ci, tempoBPM) - audioEventRef(*ci).fadeInSeconds
                       : dragStartLength_;
                newFadeOut = juce::jmin(newFadeOut, juce::jmax(0.0, maxFadeOut));
                double fadeDelta = newFadeOut - dragStartFadeOut_;
                auto& cm = ClipManager::getInstance();
                cm.setFadeOut(clipId_, newFadeOut);
                for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                    const auto* c = cm.getClip(cid);
                    if (!c)
                        continue;
                    double otherFade =
                        juce::jmax(0.0, audioEventRef(snap).fadeOutSeconds + fadeDelta);
                    otherFade = juce::jmin(
                        otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, tempoBPM) -
                                                       magda::audioEventRef(*c).fadeInSeconds));
                    cm.setFadeOut(cid, otherFade);
                }
                repaint();
            }
            break;
        }

        case DragMode::CrossfadeIn: {
            // This clip's geometry is fixed; the handle moves the previous
            // clip's end (the overlap end). Overlap start = our start.
            auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
            double pps = (dragStartLength_ > 0.0)
                             ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                             : 0.0;
            if (pps > 0.0) {
                double px = juce::jmax(0.0, static_cast<double>(e.x - wfArea.getX()));
                double newEndBeat = crossfadeDragPair_.startBeat + (px / pps) * tempoBPM / 60.0;
                ClipManager::getInstance().setCrossfadeRegionBeats(
                    crossfadeDragPair_.leftClipId, crossfadeDragPair_.rightClipId,
                    crossfadeDragPair_.startBeat, newEndBeat, tempoBPM);
                repaint();
            }
            break;
        }

        case DragMode::CrossfadeOut: {
            // The handle moves the next clip's start (the overlap start).
            // Overlap end = our end.
            auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
            double pps = (dragStartLength_ > 0.0)
                             ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                             : 0.0;
            if (pps > 0.0) {
                double px = juce::jmax(0.0, static_cast<double>(wfArea.getRight() - e.x));
                double newStartBeat = crossfadeDragPair_.endBeat - (px / pps) * tempoBPM / 60.0;
                ClipManager::getInstance().setCrossfadeRegionBeats(
                    crossfadeDragPair_.leftClipId, crossfadeDragPair_.rightClipId, newStartBeat,
                    crossfadeDragPair_.endBeat, tempoBPM);
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
    if (pendingCopyDragAction_) {
        pendingCopyDragAction_ = false;
        pendingCopyDragIsGhost_ = false;
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

        // SafePointer guard: any commit below can trigger rebuildClipComponents()
        // (overlap resolution, model listeners relayouting the arrangement),
        // which destroys this component mid-switch. Checked after the switch
        // before touching any member.
        juce::Component::SafePointer<ClipComponent> safeThis(this);

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

                    // Copy-on-drag: create duplicate at final position via undo command
                    double dupTempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                    auto cmd = std::make_unique<DuplicateClipCommand>(
                        clipId_, BeatPosition{finalStartTime * dupTempo / 60.0}, targetTrackId,
                        dupTempo, -1, isDuplicatingGhost_);
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
                    isDuplicatingGhost_ = false;
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
                        restoreLeftResizePreview(*c, dragStartClipSnapshot_, dragStartTime_,
                                                 dragStartLength_, commitTempoBPM);
                    }
                    // Same for the other selected clips previewed during the drag
                    for (auto& [cid, origLen] : dragStartSelectedLengths_) {
                        auto snapshotIt = dragStartSelectedClipSnapshots_.find(cid);
                        if (snapshotIt == dragStartSelectedClipSnapshots_.end())
                            continue;
                        if (auto* c = cm.getClip(cid)) {
                            restoreLeftResizePreview(
                                *c, snapshotIt->second,
                                timelineStartSeconds(snapshotIt->second, commitTempoBPM), origLen,
                                commitTempoBPM);
                        }
                    }
                }

                if (onClipResized) {
                    onClipResized(clipId_, finalLength, true);
                }
                dragStartSelectedLengths_.clear();
                dragStartSelectedClipSnapshots_.clear();
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
                        double maxFadeIn = ci ? timelineLengthSeconds(*ci, commitTempoBPM) -
                                                    audioEventRef(*ci).fadeOutSeconds
                                              : dragStartLength_;
                        finalFadeIn = juce::jmin(finalFadeIn, juce::jmax(0.0, maxFadeIn));
                    }
                }
                {
                    // Restore all clips to pre-drag state for correct undo capture
                    auto& cm = ClipManager::getInstance();
                    if (auto* e = primaryEventOf(cm.getClip(clipId_)))
                        e->fadeInSeconds =
                            magda::audioEventRef(dragStartClipSnapshot_).fadeInSeconds;
                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        if (auto* e = primaryEventOf(cm.getClip(cid)))
                            e->fadeInSeconds = audioEventRef(snap).fadeInSeconds;
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
                        double otherFade =
                            juce::jmax(0.0, audioEventRef(snap).fadeInSeconds + fadeDelta);
                        otherFade = juce::jmin(
                            otherFade,
                            juce::jmax(0.0, timelineLengthSeconds(*c, commitTempoBPM) -
                                                magda::audioEventRef(*c).fadeOutSeconds));
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
                        double maxFadeOut = ci ? timelineLengthSeconds(*ci, commitTempoBPM) -
                                                     audioEventRef(*ci).fadeInSeconds
                                               : dragStartLength_;
                        finalFadeOut = juce::jmin(finalFadeOut, juce::jmax(0.0, maxFadeOut));
                    }
                }
                {
                    // Restore all clips to pre-drag state for correct undo capture
                    auto& cm = ClipManager::getInstance();
                    if (auto* e = primaryEventOf(cm.getClip(clipId_)))
                        e->fadeOutSeconds =
                            magda::audioEventRef(dragStartClipSnapshot_).fadeOutSeconds;
                    for (auto& [cid, snap] : dragStartSelectedFadeSnapshots_) {
                        if (auto* e = primaryEventOf(cm.getClip(cid)))
                            e->fadeOutSeconds = audioEventRef(snap).fadeOutSeconds;
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
                        double otherFade =
                            juce::jmax(0.0, audioEventRef(snap).fadeOutSeconds + fadeDelta);
                        otherFade = juce::jmin(
                            otherFade, juce::jmax(0.0, timelineLengthSeconds(*c, commitTempoBPM) -
                                                           magda::audioEventRef(*c).fadeInSeconds));
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

            case DragMode::CrossfadeIn:
            case DragMode::CrossfadeOut: {
                const bool isIn = savedDragMode == DragMode::CrossfadeIn;

                // Capture the final overlap region from the mouse position
                double finalStart = crossfadeDragPair_.startBeat;
                double finalEnd = crossfadeDragPair_.endBeat;
                auto wfArea = getLocalBounds().reduced(2, HEADER_HEIGHT + 2);
                double pps = (dragStartLength_ > 0.0)
                                 ? static_cast<double>(wfArea.getWidth()) / dragStartLength_
                                 : 0.0;
                if (pps > 0.0) {
                    if (isIn) {
                        double px = juce::jmax(0.0, static_cast<double>(e.x - wfArea.getX()));
                        finalEnd =
                            crossfadeDragPair_.startBeat + (px / pps) * commitTempoBPM / 60.0;
                    } else {
                        double px = juce::jmax(0.0, static_cast<double>(wfArea.getRight() - e.x));
                        finalStart =
                            crossfadeDragPair_.endBeat - (px / pps) * commitTempoBPM / 60.0;
                    }
                }

                // Restore both clips to pre-drag state so the command captures
                // it as the undo state, then apply the final region undoably.
                auto& cm = ClipManager::getInstance();
                if (auto* c = cm.getClip(clipId_))
                    *c = dragStartClipSnapshot_;
                const ClipId otherId =
                    isIn ? crossfadeDragPair_.leftClipId : crossfadeDragPair_.rightClipId;
                if (auto* c = cm.getClip(otherId))
                    *c = crossfadeOtherSnapshot_;

                UndoManager::getInstance().executeCommand(std::make_unique<SetCrossfadeCommand>(
                    crossfadeDragPair_.leftClipId, crossfadeDragPair_.rightClipId, finalStart,
                    finalEnd, commitTempoBPM));
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
        // The commit may have destroyed this component (see SafePointer above);
        // touch no members after this point if it did.
        if (safeThis == nullptr)
            return;
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

    // Gesture over: the pre-drag cursor may be stale for wherever the mouse
    // ended up (#1720). mouseIsOver_ still gates this inside the helper.
    refreshHoverFromMouse();
}

void ClipComponent::mouseMove(const juce::MouseEvent& e) {
    bool wasHoverLeft = hoverLeftEdge_;
    bool wasHoverRight = hoverRightEdge_;
    bool wasHoverFadeIn = hoverFadeIn_;
    bool wasHoverFadeOut = hoverFadeOut_;
    bool wasHoverVolume = hoverVolumeHandle_;

    bool wasHoverLowerZone = hoverLowerZone_;

    // Zone model lives in the shared hit tester (#1719). Notes on the zones:
    // the lower half (away from the resize edges) is the time-selection zone
    // — when a time selection covers this point hitTest() makes the clip
    // transparent, so the panel handles the grab/resize cursor there. Fade
    // and volume handles exist on selected audio clips only.
    const auto hit = interaction::clipHit(e.x, e.y, makeHitSnapshot());
    hoverLeftEdge_ = hit.onLeftEdge;
    hoverRightEdge_ = hit.onRightEdge;
    hoverLowerZone_ = hit.lowerHalf;
    hoverFadeIn_ = hit.onFadeIn;
    hoverFadeOut_ = hit.onFadeOut;
    hoverVolumeHandle_ = hit.onVolume;

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

void ClipComponent::modifierKeysChanged(const juce::ModifierKeys& mods) {
    // Modifier tools (Alt copy, Cmd+Alt blade, Shift+Ctrl erase, Shift
    // stretch) must swap the cursor without waiting for a mouse move
    // (#1720). The hover flags don't depend on modifiers, so re-running the
    // cursor table is enough. During a drag the gesture owns the cursor.
    if (mouseIsOver_ && !juce::Component::isMouseButtonDownAnywhere())
        updateCursor(mods);
    juce::Component::modifierKeysChanged(mods);
}

void ClipComponent::refreshHoverFromMouse() {
    if (!mouseIsOver_ || juce::Component::isMouseButtonDownAnywhere())
        return;

    const auto pos = getMouseXYRelative();
    const auto hit = interaction::clipHit(pos.x, pos.y, makeHitSnapshot());
    hoverLeftEdge_ = hit.onLeftEdge;
    hoverRightEdge_ = hit.onRightEdge;
    hoverLowerZone_ = hit.lowerHalf;
    hoverFadeIn_ = hit.onFadeIn;
    hoverFadeOut_ = hit.onFadeOut;
    hoverVolumeHandle_ = hit.onVolume;
    updateCursor(juce::ModifierKeys::getCurrentModifiers());
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
        return;
    }

    // A same-track audio neighbour's geometry drives this clip's crossfade
    // rendering (#1499): moving it out of (or into) the overlap must refresh
    // the fade display here, not just on the moved clip.
    const auto* clip = getClipInfo();
    if (clip && clip->isAudio()) {
        const auto* other = ClipManager::getInstance().getClip(clipId);
        if (other && other->isAudio() && other->trackId == clip->trackId)
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

void ClipComponent::setCoveringRanges(std::vector<BeatRange> ranges) {
    if (ranges.size() == coveringRanges_.size()) {
        constexpr double tolBeats = 1e-6;
        bool same = true;
        for (size_t i = 0; i < ranges.size() && same; ++i) {
            same = std::abs(ranges[i].start.value - coveringRanges_[i].start.value) < tolBeats &&
                   std::abs(ranges[i].end.value - coveringRanges_[i].end.value) < tolBeats;
        }
        if (same)
            return;
    }
    coveringRanges_ = std::move(ranges);
    repaint();
}

void ClipComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        // Selection changes the hit zones under a stationary mouse (fade and
        // volume handles appear, the body becomes grabbable) — re-derive the
        // hover flags and cursor instead of waiting for a mouse move (#1720).
        refreshHoverFromMouse();
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

// The fade/volume handle geometry lives in the shared hit tester (#1721);
// these delegate so gesture dispatch and the cursor share one zone model.
// The raw (selection-ungated) variants match the historical predicates —
// callers apply their own isSelected_ gates.
bool ClipComponent::isOnFadeInHandle(int x, int y) const {
    return interaction::clipFadeInHandleHit(x, y, makeHitSnapshot());
}

bool ClipComponent::isOnFadeOutHandle(int x, int y) const {
    return interaction::clipFadeOutHandleHit(x, y, makeHitSnapshot());
}

bool ClipComponent::isOnVolumeHandle(int x, int y) const {
    juce::ignoreUnused(x);
    return interaction::clipVolumeLineHit(y, makeHitSnapshot());
}

interaction::ClipSnapshot ClipComponent::makeHitSnapshot() const {
    interaction::ClipSnapshot s;
    s.width = getWidth();
    s.height = getHeight();
    s.selected = isSelected_;

    const auto* clip = getClipInfo();
    s.isAudio = clip != nullptr && clip->isAudio();
    if (s.isAudio) {
        const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
        s.clipLengthSeconds = clip->getTimelineLength(tempo);
        const auto fades = computeEffectiveFades(*clip);
        s.fadeInSeconds = fades.fadeInSeconds;
        s.fadeOutSeconds = fades.fadeOutSeconds;
        s.volumeGainLinear = juce::Decibels::decibelsToGain(clip->volumeDB);
    }
    return s;
}

void ClipComponent::updateCursor(const juce::ModifierKeys& mods) {
    // Cursor policy lives in the shared hit tester's table (#1719); the
    // hover flags were derived from the same hit test in mouseMove, so the
    // cursor always matches what a click here would do.
    interaction::ClipHit hit;
    hit.onLeftEdge = hoverLeftEdge_;
    hit.onRightEdge = hoverRightEdge_;
    hit.onFadeIn = hoverFadeIn_;
    hit.onFadeOut = hoverFadeOut_;
    hit.onVolume = hoverVolumeHandle_;
    hit.lowerHalf = hoverLowerZone_;

    const bool isClipSelected = SelectionManager::getInstance().isClipSelected(clipId_);
    setMouseCursor(interaction::toJuceCursor(
        interaction::clipCursor(hit, isClipSelected, interaction::ModifierSnapshot::from(mods))));
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

    // Chord progression clips get a trimmed menu: no audio slicing, automation
    // duplicates, render, bounce, transcribe, or MIDI-library save.
    const bool isChord = clipForMenu && isChordClip(*clipForMenu);

    // Eligible destinations for "Send Progression to Track" (chord clips only):
    // regular hybrid tracks, which can host the baked MIDI clip. Captured below
    // so the async handler can map menu IDs back to track IDs.
    std::vector<TrackId> progressionTargets;
    if (isChord && !isMultiSelection) {
        for (const auto& t : TrackManager::getInstance().getTracks())
            if (t.type == TrackType::Audio)
                progressionTargets.push_back(t.id);
    }

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

    // Bake the progression onto a regular track as a plain MIDI clip (#1503).
    if (isChord && !isMultiSelection) {
        juce::PopupMenu targetMenu;
        const auto& tracks = TrackManager::getInstance().getTracks();
        int idx = 0;
        for (const auto targetId : progressionTargets) {
            juce::String label = "Track " + juce::String(targetId);
            for (const auto& t : tracks)
                if (t.id == targetId) {
                    label = t.name.isNotEmpty() ? t.name : label;
                    break;
                }
            targetMenu.addItem(kProgressionTargetBaseId + idx, label);
            ++idx;
        }
        menu.addSubMenu("Send Progression to Track", targetMenu, !progressionTargets.empty());
        menu.addSeparator();
    }

    // Duplicate
    menu.addItem(4, "Duplicate", canEdit);
    if (!isChord) {
        menu.addItem(18, "Duplicate With Automation", canEdit);
        menu.addItem(19, "Duplicate Without Automation", canEdit);
        menu.addItem(17, "Duplicate Time Selection", !isFrozen && hasTimeSelection);
        // Ghost clips: the copy joins the source's link group and mirrors
        // its content; Make Unique detaches a member from its group.
        menu.addItem(25, "Duplicate as Ghost", canEdit);
        if (clipManager.isGhostClip(clipId_))
            menu.addItem(26, "Make Unique", canEdit);
    }
    menu.addSeparator();

    // Split / Trim
    menu.addItem(5, "Split / Trim", canEdit);

    // Slice operations. In-place slicing applies to the whole selection (each
    // selected audio clip is sliced); the "to Drum Grid" variants stay single
    // clip, since each would spawn its own drum-grid track.
    bool canSliceAtMarkers = false;       // single audio clip with warp markers
    bool canSliceAtGrid = false;          // single audio clip, grid snap active
    bool canSliceAtMarkersMulti = false;  // every selected clip is audio + warp
    bool canSliceAtGridMulti = false;     // every selected clip is audio, grid on
    if (!isChord && canEdit) {
        double gridInterval = 0.0;
        if (parentPanel_ && parentPanel_->getTimelineController())
            gridInterval = parentPanel_->getTimelineController()->getState().getSnapInterval();
        auto* audioEngine = TrackManager::getInstance().getAudioEngine();
        auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;

        auto hasWarpMarkers = [&](const ClipInfo* c, ClipId id) {
            return c && c->isAudio() && magda::audioEventRef(*c).warpEnabled && bridge &&
                   bridge->getWarpMarkers(id).size() > 2;
        };

        if (isMultiSelection) {
            bool allAudio = true;
            bool anyWarp = false;
            for (auto cid : selectionManager.getSelectedClips()) {
                const auto* c = clipManager.getClip(cid);
                if (!c || !c->isAudio()) {
                    allAudio = false;
                    break;
                }
                if (hasWarpMarkers(c, cid))
                    anyWarp = true;
            }
            canSliceAtMarkersMulti = allAudio && anyWarp;
            canSliceAtGridMulti = allAudio && gridInterval > 0.0;
        } else {
            const auto* singleClip = getClipInfo();
            if (singleClip && singleClip->isAudio()) {
                canSliceAtMarkers = hasWarpMarkers(singleClip, clipId_);
                canSliceAtGrid = gridInterval > 0.0;
            }
        }
    }
    if (!isChord) {
        menu.addItem(13, "Slice at Warp Markers In Place",
                     canSliceAtMarkers || canSliceAtMarkersMulti);
        menu.addItem(15, "Slice at Warp Markers to Drum Grid", canSliceAtMarkers);
        menu.addItem(14, "Slice at Grid In Place", canSliceAtGrid || canSliceAtGridMulti);
        menu.addItem(16, "Slice at Grid to Drum Grid", canSliceAtGrid);
    }
    menu.addSeparator();

    // Loop-record takes: pick which captured pass plays back. Single audio clip
    // with more than one take only. IDs 300+ (one per take).
    {
        const auto* takeClip = isMultiSelection ? nullptr : getClipInfo();
        if (takeClip && takeClip->isAudio() && takeClip->audio().takes.size() > 1) {
            juce::PopupMenu takesMenu;
            const auto& takes = takeClip->audio().takes;
            for (int i = 0; i < static_cast<int>(takes.size()); ++i)
                takesMenu.addItem(kTakeMenuBaseId + i, "Take " + juce::String(i + 1), canEdit,
                                  i == takeClip->audio().currentTakeIndex);
            menu.addSubMenu("Takes", takesMenu, canEdit);
            menu.addSeparator();
        }
    }

    if (!isChord) {
        bool canEditExternally = false;
        if (!isMultiSelection && canEdit) {
            const auto* singleClip = getClipInfo();
            canEditExternally =
                singleClip && singleClip->isAudio() &&
                juce::File(magda::audioEventRef(*singleClip).sourceFilePath()).existsAsFile();
        }
        menu.addItem(21, "Edit in External Editor", canEditExternally);

        // Transcribe to MIDI (audio clips only; needs the bundled model)
        bool canTranscribe = false;
        if (!isMultiSelection && canEdit) {
            const auto* singleClip = getClipInfo();
            canTranscribe =
                singleClip && singleClip->isAudio() &&
                juce::File(magda::audioEventRef(*singleClip).sourceFilePath()).existsAsFile() &&
                magda::transcription::TranscriptionService::getInstance().isAvailable();
        }
        menu.addItem(22, "Transcribe to MIDI", canTranscribe);

        // Split into stems (#1288): audio clips only, one item per engine.
        {
            bool canSplit = false;
            if (!isMultiSelection && canEdit) {
                const auto* singleClip = getClipInfo();
                canSplit =
                    singleClip && singleClip->isAudio() &&
                    juce::File(magda::audioEventRef(*singleClip).sourceFilePath()).existsAsFile();
            }
            auto& stemService = magda::stems::StemSeparationService::getInstance();
            juce::PopupMenu stemMenu;
            stemMenu.addItem(50, "Harmonic / Percussive (HPSS)",
                             canSplit &&
                                 stemService.isEngineAvailable(
                                     magda::stems::StemSeparationService::Engine::Hpss) &&
                                 !stemService.isBusy());
            // Demucs needs the ONNX backend (not built on Intel macOS) and
            // its weights. Backend-but-no-weights stays clickable: the
            // action deep-links to the download page ("..." marks it).
            if (magda::stems::DemucsSeparator::backendAvailable()) {
                const bool demucsInstalled = stemService.isEngineAvailable(
                    magda::stems::StemSeparationService::Engine::Demucs);
                stemMenu.addItem(51,
                                 juce::String("Vocals / Drums / Bass / Other (Demucs)") +
                                     (demucsInstalled ? "" : "..."),
                                 canSplit && !stemService.isBusy());
                const bool spleeterInstalled = stemService.isEngineAvailable(
                    magda::stems::StemSeparationService::Engine::Spleeter);
                stemMenu.addItem(52,
                                 juce::String("Vocals / Accompaniment (Spleeter)") +
                                     (spleeterInstalled ? "" : "..."),
                                 canSplit && !stemService.isBusy());
            }
            menu.addSubMenu("Split into Stems", stemMenu, canSplit);
        }
        menu.addSeparator();
    }

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

    // Crossfades (#1499): create at a butt joint / remove an existing one
    {
        bool hasXfadeIn = false;
        bool hasXfadeOut = false;
        bool canXfadePrev = false;
        bool canXfadeNext = false;
        if (!isMultiSelection && canEdit && clipForMenu && clipForMenu->isAudio()) {
            hasXfadeIn = clipManager.getCrossfadeAtStart(clipId_).has_value();
            hasXfadeOut = clipManager.getCrossfadeAtEnd(clipId_).has_value();
            canXfadePrev = !hasXfadeIn && findCrossfadeNeighbour(true) != INVALID_CLIP_ID;
            canXfadeNext = !hasXfadeOut && findCrossfadeNeighbour(false) != INVALID_CLIP_ID;
        }
        if (canXfadePrev || canXfadeNext || hasXfadeIn || hasXfadeOut) {
            if (canXfadePrev)
                menu.addItem(45, "Crossfade with Previous Clip");
            if (canXfadeNext)
                menu.addItem(46, "Crossfade with Next Clip");
            if (hasXfadeIn)
                menu.addItem(47, "Remove Start Crossfade");
            if (hasXfadeOut)
                menu.addItem(48, "Remove End Crossfade");
            menu.addSeparator();
        }
    }

    // Enable/disable (#1736): disabled clips do not play.
    if (clipForMenu)
        menu.addItem(27, clipForMenu->enabled ? "Disable Clip" : "Enable Clip", canEdit);

    // Delete
    menu.addItem(6, "Delete", canEdit);
    menu.addSeparator();

    // Quantize (MIDI clips only)
    {
        bool hasMidi = false;
        bool canSaveMidi = false;
        bool canRenderMidiLoop = false;
        if (isMultiSelection) {
            for (auto cid : selectionManager.getSelectedClips()) {
                auto* c = clipManager.getClip(cid);
                if (c && c->isMidi() && !c->midiNotes.empty())
                    hasMidi = true;
                if (c && c->isMidi() && clipManager.canSaveClipToLibrary(cid))
                    canSaveMidi = true;
            }
        } else {
            const auto* ci = getClipInfo();
            hasMidi = ci && ci->isMidi() && !ci->midiNotes.empty();
            canSaveMidi = ci && ci->isMidi() && clipManager.canSaveClipToLibrary(ci->id);
            canRenderMidiLoop = ci && ci->isMidi() && ci->loopEnabled && ci->loopLengthBeats > 0.0;
        }

        if (hasMidi) {
            // Progressions (chord-track clips) save to the library too — their
            // chords round-trip as CHORD: markers — so the Save item is shown
            // for every MIDI clip. Extracting chords onto the chord track only
            // makes sense for clips that aren't already on it.
            menu.addItem(
                20, isMultiSelection ? "Save MIDI Clips to Library" : "Save MIDI Clip to Library",
                canSaveMidi);
            if (!isChord) {
                juce::PopupMenu extractMenu;
                extractMenu.addItem(23, "Append");
                extractMenu.addItem(24, "Replace Chord Track");
                menu.addSubMenu("Extract Chords to Chord Track", extractMenu);
            }
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

        const bool canFlattenStack = !FlattenClipStackCommand::collectStack(clipId_).empty();
        if (canRenderMidiLoop || canFlattenStack) {
            // One entry for both jobs: unroll this clip's loop, or fold the
            // clips stacked with it into one (#2003).
            menu.addItem(28, canFlattenStack ? "Flatten Clips" : "Flatten MIDI Loop", canEdit);
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

    // Render Time Selection - always available (not for chord progressions)
    if (!isChord) {
        bool hasTimeSelection = false;
        bool hasLoop = false;
        bool hasCursor = false;
        if (parentPanel_ && parentPanel_->getTimelineController()) {
            const auto& state = parentPanel_->getTimelineController()->getState();
            hasTimeSelection = state.selection.isActive() && !state.selection.visuallyHidden;
            hasLoop = state.loop.isValid();
            hasCursor = state.editCursorPosition >= 0.0;
        }
        menu.addItem(10, "Render Time Selection", hasTimeSelection);
        menu.addSeparator();
        menu.addItem(30, "Insert Time", hasTimeSelection);
        menu.addItem(31, "Duplicate Time Range", hasTimeSelection);
        menu.addItem(32, "Duplicate Loop Range", hasLoop);
        menu.addItem(33, "Split All Tracks at Cursor", hasCursor);

        // Range Editing submenu mirrors the Edit menu's Copy/Cut/Delete/Paste-Ripple set.
        const bool hasClipboard = ClipManager::getInstance().hasClipsInClipboard();
        juce::PopupMenu rangeMenu;
        rangeMenu.addItem(34, "Copy Time Range", hasTimeSelection);
        rangeMenu.addItem(35, "Cut Time Range", hasTimeSelection);
        rangeMenu.addItem(36, "Delete Time Range", hasTimeSelection);
        rangeMenu.addSeparator();
        rangeMenu.addItem(37, "Copy Loop Range", hasLoop);
        rangeMenu.addItem(38, "Cut Loop Range", hasLoop);
        rangeMenu.addItem(39, "Delete Loop Range", hasLoop);
        rangeMenu.addSeparator();
        rangeMenu.addItem(40, "Paste (Ripple)", hasClipboard);
        menu.addSubMenu("Range Editing", rangeMenu);
    }

    // Bounce operations (not for chord progressions)
    if (!isChord) {
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
    menu.showMenuAsync(juce::PopupMenu::Options(), [this,
                                                    safeThis =
                                                        juce::Component::SafePointer<ClipComponent>(
                                                            this),
                                                    progressionTargets, &clipManager,
                                                    &selectionManager](int result) {
        // The menu is modal-async: the clip (and its parent panel) can be
        // destroyed while it is open, e.g. a project load/close or track delete
        // rebuilds every clip via ClipManager::clearAllClips(). Bail before
        // touching any member — parentPanel_ would otherwise be a dangling
        // non-null pointer (crash in parentPanel_->getTempo()).
        if (safeThis == nullptr)
            return;

        if (result == 0)
            return;  // Cancelled

        // Loop-record take selection (IDs 300+): front the chosen pass as the
        // clip source and re-sync. ClipSynchronizer detects the source change,
        // rebuilds the TE clip, and re-attaches the take alternates.
        if (result >= kTakeMenuBaseId && result < kTakeMenuBaseId + kTakeMenuMaxItems) {
            const int takeIndex = result - kTakeMenuBaseId;
            auto* c = clipManager.getClip(clipId_);
            if (c && c->isAudio() && takeIndex >= 0 &&
                takeIndex < static_cast<int>(c->audio().takes.size())) {
                clipManager.setAudioClipCurrentTake(clipId_, takeIndex);
            }
            return;
        }

        // Send Progression to Track (IDs 400+): bake the chord progression onto
        // the chosen regular track as a plain MIDI clip.
        if (result >= kProgressionTargetBaseId &&
            result < kProgressionTargetBaseId + static_cast<int>(progressionTargets.size())) {
            const auto targetId =
                progressionTargets[static_cast<size_t>(result - kProgressionTargetBaseId)];
            sendProgressionToTrack(clipId_, targetId);
            return;
        }

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
                    TrackId contextTrackId = INVALID_TRACK_ID;
                    if (const auto* contextClip = clipManager.getClip(clipId_))
                        contextTrackId = contextClip->trackId;
                    const auto target =
                        resolvePasteTarget(ViewModeController::getInstance().getViewMode(),
                                           PasteTrackMode::PinToResolvedTrack,
                                           PasteInvocation::fromContextTrack(contextTrackId));
                    if (!target.ok)
                        break;
                    auto cmd = std::make_unique<PasteClipCommand>(
                        BeatPosition{pasteTime * bpm / 60.0}, target.trackId);
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

            case 25: {  // Duplicate as Ghost
                if (parentPanel_)
                    parentPanel_->duplicateSelectedArrangementClips(false, true);
                break;
            }

            case 26: {  // Make Unique (detach from link group)
                std::vector<ClipId> targets;
                if (selectionManager.getSelectedClipCount() > 1 &&
                    selectionManager.isClipSelected(clipId_)) {
                    for (auto cid : selectionManager.getSelectedClips())
                        if (clipManager.isGhostClip(cid))
                            targets.push_back(cid);
                } else if (clipManager.isGhostClip(clipId_)) {
                    targets.push_back(clipId_);
                }
                if (targets.empty())
                    break;
                auto& undoManager = UndoManager::getInstance();
                const bool compound = targets.size() > 1;
                if (compound)
                    undoManager.beginCompoundOperation("Make Clips Unique");
                for (auto cid : targets)
                    undoManager.executeCommand(std::make_unique<MakeClipUniqueCommand>(cid));
                if (compound)
                    undoManager.endCompoundOperation();
                break;
            }

            case 27: {  // Enable/Disable Clip(s) (#1736)
                // Uniform set: the clicked clip decides the target state (the
                // same clip whose state decides the menu label), so a mixed
                // selection ends up uniform instead of each clip inverting.
                const auto* clicked = clipManager.getClip(clipId_);
                if (!clicked)
                    break;
                const bool newState = !clicked->enabled;
                std::vector<ClipId> targets;
                if (selectionManager.getSelectedClipCount() > 1 &&
                    selectionManager.isClipSelected(clipId_)) {
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->enabled != newState)
                            targets.push_back(cid);
                    }
                } else {
                    targets.push_back(clipId_);
                }
                if (targets.empty())
                    break;
                auto& undoManager = UndoManager::getInstance();
                const bool compound = targets.size() > 1;
                if (compound)
                    undoManager.beginCompoundOperation(newState ? "Enable Clips" : "Disable Clips");
                for (auto cid : targets) {
                    undoManager.executeCommand(std::make_unique<SetClipPropertyCommand>(
                        cid, newState ? "Enable Clip" : "Disable Clip",
                        [newState](auto& manager, ClipId id) {
                            manager.setClipEnabled(id, newState);
                        }));
                }
                if (compound)
                    undoManager.endCompoundOperation();
                break;
            }

            case 28: {  // Flatten (#1737 loop unroll, #2003 stack merge)
                const auto* clip = clipManager.getClip(clipId_);
                if (clip == nullptr || !clip->isMidi())
                    break;

                // Overlapping clips fold into one; otherwise this is the
                // single-clip loop unroll.
                if (!FlattenClipStackCommand::collectStack(clipId_).empty()) {
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<FlattenClipStackCommand>(clipId_));
                } else if (clip->loopEnabled && clip->loopLengthBeats > 0.0) {
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<FlattenMidiClipCommand>(clipId_));
                }
                break;
            }

            case 20: {  // Save MIDI Clip(s) to Library
                if (selectionManager.getSelectedClipCount() > 1) {
                    bool anyFailed = false;
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->isMidi() && clipManager.canSaveClipToLibrary(cid)) {
                            if (!clipManager.saveClipToLibrary(cid))
                                anyFailed = true;
                        }
                    }
                    if (anyFailed)
                        showMidiClipLibrarySaveFailedAlert();
                } else if (!clipManager.saveClipToLibrary(clipId_)) {
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

            case 22: {  // Transcribe to MIDI
                magda::daw::ui::Toast::showGlobal("Transcribing audio to MIDI...");
                magda::transcription::TranscriptionService::getInstance().transcribeAudioClip(
                    clipId_, [](magda::ClipId newClipId, juce::String err) {
                        if (newClipId == magda::INVALID_CLIP_ID)
                            magda::daw::ui::Toast::showGlobal(
                                err.isNotEmpty() ? err : juce::String("Transcription failed"));
                        else
                            magda::daw::ui::Toast::showGlobal("Transcription complete");
                    });
                break;
            }

            case 50:    // Split into Stems: HPSS
            case 51:    // Split into Stems: Demucs
            case 52: {  // Split into Stems: Spleeter
                const auto engine = result == 50 ? magda::stems::StemSeparationService::Engine::Hpss
                                    : result == 51
                                        ? magda::stems::StemSeparationService::Engine::Demucs
                                        : magda::stems::StemSeparationService::Engine::Spleeter;
                auto& stemService = magda::stems::StemSeparationService::getInstance();
                if (!stemService.isEngineAvailable(engine)) {
                    // Weights not downloaded yet: deep-link to the page that
                    // installs them instead of failing.
                    AISettingsDialog::showDialog(getTopLevelComponent(), "Stems");
                    break;
                }
                stemService.splitClipIntoStems(
                    clipId_, engine, [](magda::TrackId groupTrackId, juce::String err) {
                        if (groupTrackId == magda::INVALID_TRACK_ID)
                            magda::daw::ui::Toast::showGlobal(
                                err.isNotEmpty() ? err : juce::String("Stem split failed"));
                        else
                            magda::daw::ui::Toast::showGlobal("Stem split complete");
                    });
                break;
            }

            case 23: {  // Extract Chords to Chord Track (Append)
                if (selectionManager.getSelectedClipCount() > 1) {
                    UndoManager::getInstance().beginCompoundOperation(
                        "Extract Chords to Chord Track");
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->isMidi())
                            extractChordsToChordTrack(cid, false);
                    }
                    UndoManager::getInstance().endCompoundOperation();
                } else {
                    extractChordsToChordTrack(clipId_, false);
                }
                break;
            }

            case 24: {  // Extract Chords to Chord Track (Replace)
                if (selectionManager.getSelectedClipCount() > 1) {
                    UndoManager::getInstance().beginCompoundOperation(
                        "Extract Chords to Chord Track");
                    // Replace clears the chord track once (first clip), then the
                    // rest append so all selected clips' chords land on the track.
                    bool replace = true;
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->isMidi()) {
                            extractChordsToChordTrack(cid, replace);
                            replace = false;
                        }
                    }
                    UndoManager::getInstance().endCompoundOperation();
                } else {
                    extractChordsToChordTrack(clipId_, true);
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

            case 45:    // Crossfade with Previous Clip
            case 46: {  // Crossfade with Next Clip
                const bool atStart = result == 45;
                const ClipId neighbourId = findCrossfadeNeighbour(atStart);
                const auto* c = clipManager.getClip(clipId_);
                const auto* neighbour = clipManager.getClip(neighbourId);
                if (c && neighbour) {
                    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                    const double centre =
                        atStart ? (c->placement.startBeat + neighbour->placement.endBeat()) * 0.5
                                : (neighbour->placement.startBeat + c->placement.endBeat()) * 0.5;
                    const double half = kDefaultCrossfadeBeats * 0.5;
                    const ClipId leftId = atStart ? neighbourId : clipId_;
                    const ClipId rightId = atStart ? clipId_ : neighbourId;
                    UndoManager::getInstance().executeCommand(std::make_unique<SetCrossfadeCommand>(
                        leftId, rightId, centre - half, centre + half, tempo));
                }
                break;
            }

            case 47:    // Remove Start Crossfade
            case 48: {  // Remove End Crossfade
                auto xf = result == 47 ? clipManager.getCrossfadeAtStart(clipId_)
                                       : clipManager.getCrossfadeAtEnd(clipId_);
                if (xf) {
                    const double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                    // Butt the joint at the overlap centre
                    const double centre = (xf->startBeat + xf->endBeat) * 0.5;
                    UndoManager::getInstance().executeCommand(std::make_unique<SetCrossfadeCommand>(
                        xf->leftClipId, xf->rightClipId, centre, centre, tempo));
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

            case 30: {  // Insert Time (ripple)
                if (onInsertTimeRequested) {
                    onInsertTimeRequested();
                }
                break;
            }

            case 31: {  // Duplicate Time Range (ripple)
                if (onDuplicateTimeRangeRequested) {
                    onDuplicateTimeRangeRequested();
                }
                break;
            }

            case 32: {  // Duplicate Loop Range (ripple, all tracks)
                if (onDuplicateLoopRangeRequested) {
                    onDuplicateLoopRangeRequested();
                }
                break;
            }

            case 33: {  // Split All Tracks at Cursor
                if (onSplitAllTracksAtCursorRequested) {
                    onSplitAllTracksAtCursorRequested();
                }
                break;
            }

            case 34: {  // Copy Time Range
                if (onCopyTimeRangeRequested) {
                    onCopyTimeRangeRequested();
                }
                break;
            }

            case 35: {  // Cut Time Range
                if (onCutTimeRangeRequested) {
                    onCutTimeRangeRequested();
                }
                break;
            }

            case 36: {  // Delete Time Range
                if (onDeleteTimeRangeRequested) {
                    onDeleteTimeRangeRequested();
                }
                break;
            }

            case 37: {  // Copy Loop Range
                if (onCopyLoopRangeRequested) {
                    onCopyLoopRangeRequested();
                }
                break;
            }

            case 38: {  // Cut Loop Range
                if (onCutLoopRangeRequested) {
                    onCutLoopRangeRequested();
                }
                break;
            }

            case 39: {  // Delete Loop Range
                if (onDeleteLoopRangeRequested) {
                    onDeleteLoopRangeRequested();
                }
                break;
            }

            case 40: {  // Paste (Ripple)
                if (onPasteRippleRequested) {
                    onPasteRippleRequested();
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

            case 13: {  // Slice at Warp Markers In Place
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                if (selectionManager.getSelectedClipCount() > 1) {
                    UndoManager::getInstance().beginCompoundOperation(
                        "Slice Clips at Warp Markers");
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->isAudio())
                            sliceClipAtWarpMarkers(cid, tempo, bridge);
                    }
                    UndoManager::getInstance().endCompoundOperation();
                } else {
                    sliceClipAtWarpMarkers(clipId_, tempo, bridge);
                }
                break;
            }

            case 14: {  // Slice at Grid In Place
                double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
                double gridInterval = 0.0;
                if (parentPanel_ && parentPanel_->getTimelineController()) {
                    gridInterval =
                        parentPanel_->getTimelineController()->getState().getSnapInterval();
                }
                auto* audioEngine = TrackManager::getInstance().getAudioEngine();
                auto* bridge = audioEngine ? audioEngine->getAudioBridge() : nullptr;
                if (selectionManager.getSelectedClipCount() > 1) {
                    UndoManager::getInstance().beginCompoundOperation("Slice Clips at Grid");
                    for (auto cid : selectionManager.getSelectedClips()) {
                        const auto* c = clipManager.getClip(cid);
                        if (c && c->isAudio())
                            sliceClipAtGrid(cid, gridInterval, tempo, bridge);
                    }
                    UndoManager::getInstance().endCompoundOperation();
                } else {
                    sliceClipAtGrid(clipId_, gridInterval, tempo, bridge);
                }
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
