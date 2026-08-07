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

        // Rejected before anything is resolved, not while resolving. Occlusion
        // and the crossfade queries take every clip in the lane they are given
        // at face value: neither checks the view or the track, so a session
        // clip or one belonging to somewhere else would cover its neighbours
        // for as long as it sat in the lane, and diagnosing it afterwards would
        // not give back what it had already silenced.
        std::vector<ClipInfo> arrangement;
        arrangement.reserve(lane.clips.size());
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

            arrangement.push_back(clip);
        }

        const auto audibleSpans = computeAudibleSpans(arrangement);

        for (const auto& clip : arrangement) {
            const auto label = clipLabel(lane.trackId, clip.id);

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

            if (clip.events().empty()) {
                snapshot.diagnostics.push_back(label +
                                               "is an audio clip with no events, it plays nothing");
                continue;
            }

            AudioClipPlayback audio;
            audio.clipId = clip.id;
            audio.span = span;
            audio.silenced = std::move(silenced);

            // The rule is the model's, the seconds are this map's. Which edge a
            // crossfade covers, and over which beats, comes from
            // effectiveFadesIn; how long those beats last is then asked of the
            // tempo map, like every other span here, rather than of one bpm
            // reading. Two clips that start under different tempos would
            // otherwise be told different lengths for the one overlap they
            // share, and neither would match the spans it sits inside.
            //
            // The clamp is the model's too, applied to the exact seconds: a
            // clip too short for both its fades scales them to fit rather than
            // playing fades that outrun it.
            const auto fades =
                effectiveFadesIn(arrangement, clip.id, tempoMap.bpmAt(clip.placement.startBeat));
            const auto& primary = audioEventRef(clip);

            const auto crossfadeSeconds = [&tempoMap](const CrossfadeInfo& overlap) {
                return tempoMap.beatToTime(overlap.endBeat) -
                       tempoMap.beatToTime(overlap.startBeat);
            };
            double fadeIn = fades.xfIn ? crossfadeSeconds(*fades.xfIn) : primary.fadeInSeconds;
            double fadeOut = fades.xfOut ? crossfadeSeconds(*fades.xfOut) : primary.fadeOutSeconds;

            const double clipSeconds = tempoMap.beatToTime(clip.placement.endBeat()) -
                                       tempoMap.beatToTime(clip.placement.startBeat);
            if (const double total = fadeIn + fadeOut; clipSeconds > 0.0 && total > clipSeconds) {
                const double scale = clipSeconds / total;
                fadeIn *= scale;
                fadeOut *= scale;
            }

            audio.fadeInSeconds = fadeIn;
            audio.fadeOutSeconds = fadeOut;

            // Both faces of the same two lengths, converted here because the
            // audio thread has no tempo map to convert them with. Off the ends
            // of the audible span rather than of the placement, because that is
            // what a fade shapes (ClipVoice.hpp).
            audio.fadeInBeats = tempoMap.timeToBeat(span.startSeconds + fadeIn) - span.startBeat;
            audio.fadeOutBeats = span.endBeat - tempoMap.timeToBeat(span.endSeconds - fadeOut);

            // The clip's edges are the primary event's edges, so they carry its
            // curves. A curve that is not one is reported once, below, where
            // every event is checked rather than only this one.
            bool curveValid = true;
            audio.fadeInCurve = curveFrom(primary.fadeInType, curveValid);
            audio.fadeOutCurve = curveFrom(primary.fadeOutType, curveValid);
            audio.fadeInBehaviour = primary.fadeInBehaviour;
            audio.fadeOutBehaviour = primary.fadeOutBehaviour;

            audio.gainDb = clip.volumeDB + clip.gainDB;
            audio.pan = clip.pan;
            audio.launchFadeSamples = clip.launchFadeSamples;

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
                // Playing backwards is reading the file from its far end
                // (io/SourceReaders.hpp), so an event that does it needs to
                // know where that end is. Everything else is placed from the
                // front and does not care how much is behind it.
                if (event.reversed && !(source->second->durationSeconds > 0.0)) {
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) +
                        " plays reversed and source " + std::to_string(event.sourceId) +
                        " has no known length to turn about, so it plays nothing");
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

                // The event's own, unresolved. The clip's edges play the
                // resolved pair above; an event inside a clip holding several
                // of them fades with these, and promoting only the primary
                // event's would lose every other event's.
                playback.fadeInSeconds = event.fadeInSeconds;
                playback.fadeOutSeconds = event.fadeOutSeconds;
                playback.fadeInCurve = curveFrom(event.fadeInType, curveValid);
                if (!curveValid)
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) + " fade-in curve " +
                        std::to_string(event.fadeInType) + " is not a curve, it fades linearly");
                playback.fadeOutCurve = curveFrom(event.fadeOutType, curveValid);
                if (!curveValid)
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) + " fade-out curve " +
                        std::to_string(event.fadeOutType) + " is not a curve, it fades linearly");
                playback.fadeInBehaviour = event.fadeInBehaviour;
                playback.fadeOutBehaviour = event.fadeOutBehaviour;

                audio.events.push_back(std::move(playback));
            }

            // Every event it had names something that cannot be read, so there
            // is nothing to play and nothing to allocate a voice for. Each one
            // was reported as it was dropped; a clip whose events all went that
            // way leaves the snapshot with them.
            if (audio.events.empty())
                continue;

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
