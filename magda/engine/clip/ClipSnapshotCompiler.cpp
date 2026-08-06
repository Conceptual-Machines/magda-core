#include "clip/ClipSnapshotCompiler.hpp"

#include <algorithm>
#include <unordered_map>

#include "core/ClipFades.hpp"
#include "core/ClipOcclusion.hpp"

namespace magda::engine {

namespace {

std::string clipLabel(TrackId trackId, ClipId clipId) {
    return "track " + std::to_string(trackId) + " clip " + std::to_string(clipId) + ": ";
}

/// A fade curve is a pinned project-file integer, so a value outside the four
/// that exist came from a file nothing in MAGDA wrote. Linear is what an
/// unreadable curve plays as, and it is reported rather than assumed.
FadeCurve curveFrom(int stored, bool& valid) {
    valid = stored >= static_cast<int>(FadeCurve::Linear) &&
            stored <= static_cast<int>(FadeCurve::SCurve);
    return valid ? static_cast<FadeCurve>(stored) : FadeCurve::Linear;
}

SnapshotSpan spanFromBeats(double startBeat, double endBeat, const TempoMap& tempoMap) {
    SnapshotSpan span;
    span.startBeat = startBeat;
    span.endBeat = endBeat;
    // Both ends converted, never a length scaled: a beat span occupies
    // different seconds depending on where it sits on the tempo curve.
    span.startSeconds = tempoMap.beatToTime(startBeat);
    span.endSeconds = tempoMap.beatToTime(endBeat);
    return span;
}

}  // namespace

ClipSnapshot compileClipSnapshot(const std::vector<ClipLane>& lanes,
                                 const std::vector<ClipSourceInfo>& sources,
                                 const TempoMap& tempoMap) {
    ClipSnapshot snapshot;
    snapshot.tempoFingerprint = tempoMap.fingerprint();

    std::unordered_map<SourceId, const ClipSourceInfo*> sourceById;
    sourceById.reserve(sources.size());
    for (const auto& source : sources)
        sourceById[source.id] = &source;

    for (const auto& lane : lanes) {
        TrackClipPlayback track;
        track.trackId = lane.trackId;

        const auto audibleSpans = computeAudibleSpans(lane.clips);

        for (const auto& clip : lane.clips) {
            const auto label = clipLabel(lane.trackId, clip.id);

            if (clip.trackId != lane.trackId) {
                snapshot.diagnostics.push_back(label + "belongs to track " +
                                               std::to_string(clip.trackId) +
                                               ", it is not played on this lane");
                continue;
            }
            if (clip.view != ClipView::Arrangement) {
                snapshot.diagnostics.push_back(
                    label + "is a session clip in an arrangement lane, it is not played here");
                continue;
            }

            // Disabled by hand (#1736). Occlusion already treats it as covering
            // nothing; that it plays nothing itself is decided here.
            if (!clip.enabled)
                continue;

            const auto found = audibleSpans.find(clip.id);
            if (found == audibleSpans.end() || !found->second.audible)
                continue;  // covered end to end, which is what covering means

            const auto& audible = found->second;
            const auto span = spanFromBeats(audible.startBeat, audible.endBeat(), tempoMap);

            std::vector<SnapshotSpan> silenced;
            silenced.reserve(audible.silenced.size());
            for (const auto& hole : audible.silenced)
                silenced.push_back(spanFromBeats(hole.start.value, hole.end.value, tempoMap));

            if (clip.isMidi()) {
                MidiClipPlayback midi;
                midi.clipId = clip.id;
                midi.span = span;
                midi.silenced = std::move(silenced);
                midi.notes = clip.midiNotes;
                midi.cc = clip.midiCCData;
                midi.pitchBend = clip.midiPitchBendData;
                midi.loopEnabled = clip.loopEnabled;
                midi.loopStartBeats = clip.loopStartBeats;
                midi.loopLengthBeats = clip.loopLengthBeats;
                midi.offsetBeats = clip.midiOffset;
                midi.trimOffsetBeats = clip.midiTrimOffset;
                midi.grooveTemplate = clip.grooveTemplate.toStdString();
                midi.grooveStrength = clip.grooveStrength;
                track.midi.push_back(std::move(midi));
                continue;
            }

            AudioClipPlayback audio;
            audio.clipId = clip.id;
            audio.span = span;
            audio.silenced = std::move(silenced);

            // Straight from the model, at the tempo where the clip starts: the
            // arrangement draws its curves from this same call, and a fade
            // played differently from the one drawn is the bug the shared rule
            // exists to prevent.
            const auto fades =
                effectiveFadesIn(lane.clips, clip.id, tempoMap.bpmAt(clip.placement.startBeat));
            audio.fadeInSeconds = fades.fadeInSeconds;
            audio.fadeOutSeconds = fades.fadeOutSeconds;

            const auto& primary = audioEventRef(clip);
            bool curveValid = true;
            audio.fadeInCurve = curveFrom(primary.fadeInType, curveValid);
            if (!curveValid)
                snapshot.diagnostics.push_back(label + "fade-in curve " +
                                               std::to_string(primary.fadeInType) +
                                               " is not a curve, it fades linearly");
            audio.fadeOutCurve = curveFrom(primary.fadeOutType, curveValid);
            if (!curveValid)
                snapshot.diagnostics.push_back(label + "fade-out curve " +
                                               std::to_string(primary.fadeOutType) +
                                               " is not a curve, it fades linearly");
            audio.fadeInBehaviour = primary.fadeInBehaviour;
            audio.fadeOutBehaviour = primary.fadeOutBehaviour;

            audio.gainDb = clip.volumeDB + clip.gainDB;
            audio.pan = clip.pan;
            audio.launchFadeSamples = clip.launchFadeSamples;

            if (clip.events().empty()) {
                snapshot.diagnostics.push_back(label +
                                               "is an audio clip with no events, it plays nothing");
                continue;
            }

            for (const auto& event : clip.events()) {
                const auto source = sourceById.find(event.sourceId);
                if (source == sourceById.end()) {
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) + " names source " +
                        std::to_string(event.sourceId) +
                        ", which is not in the source table, so it plays nothing");
                    continue;
                }
                if (!(source->second->sampleRate > 0.0)) {
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) + " names source " +
                        std::to_string(event.sourceId) +
                        ", whose sample rate is unknown, so it plays nothing");
                    continue;
                }

                AudioEventPlayback playback;
                playback.eventId = event.id;
                playback.sourceId = event.sourceId;
                playback.filePath = source->second->path;
                playback.sourceSampleRate = source->second->sampleRate;
                playback.sourceDurationSeconds = source->second->durationSeconds;

                const double startBeat = clip.placement.startBeat + event.startBeat;
                playback.span = spanFromBeats(startBeat, startBeat + event.lengthBeats, tempoMap);

                playback.anchorSamples = event.sourceAnchorSamples;
                playback.loopStartSamples = event.loopStartSamples;
                playback.loopLengthSamples = event.loopLengthSamples;
                playback.loopEnabled = clip.loopEnabled;

                playback.interpBpm = event.interpBpm;
                playback.autoTempo = event.autoTempo;
                playback.speedRatio = event.speedRatio;
                playback.timeStretchMode = event.getEffectiveTimeStretchMode();
                playback.analogPitch = event.isAnalogPitchActive();
                playback.warpEnabled = event.warpEnabled;
                playback.warpMarkers = event.warpMarkers;

                playback.autoPitch = event.autoPitch;
                playback.autoPitchMode = event.autoPitchMode;
                playback.pitchChange = event.pitchChange;
                playback.transpose = event.transpose;

                playback.reversed = event.reversed;
                playback.leftChannelActive = event.leftChannelActive;
                playback.rightChannelActive = event.rightChannelActive;
                playback.gainDb = event.gainDB;

                audio.events.push_back(std::move(playback));
            }

            track.audio.push_back(std::move(audio));
        }

        // Sorted by where they start, ties by id: two compiles of one model
        // have to produce one snapshot, and a lane is held in whatever order
        // the model happens to keep it.
        const auto byStart = [](const auto& a, const auto& b) {
            if (a.span.startBeat != b.span.startBeat)
                return a.span.startBeat < b.span.startBeat;
            return a.clipId < b.clipId;
        };
        std::sort(track.audio.begin(), track.audio.end(), byStart);
        std::sort(track.midi.begin(), track.midi.end(), byStart);

        if (!track.audio.empty() || !track.midi.empty())
            snapshot.tracks.push_back(std::move(track));
    }

    // ClipSnapshot::find binary searches this.
    std::sort(snapshot.tracks.begin(), snapshot.tracks.end(),
              [](const TrackClipPlayback& a, const TrackClipPlayback& b) {
                  return a.trackId < b.trackId;
              });

    return snapshot;
}

}  // namespace magda::engine
