#include "clip/ClipSnapshotCompiler.hpp"

#include <algorithm>
#include <unordered_map>

#include "clip/ClipStretcher.hpp"
#include "clip/MidiClipCompiler.hpp"
#include "core/ClipFades.hpp"
#include "core/ClipOcclusion.hpp"

namespace magda::engine {

namespace {

/// The model's follow action at the launcher's altitude (#2304). The engine has
/// no track groups to name, so the model's five actions map across whole.
SlotFollow compileFollow(const ClipInfo& clip) {
    const auto action = [&] {
        switch (clip.followAction) {
            case magda::FollowAction::None:
                return SlotAction::none;
            case magda::FollowAction::PlayNext:
                return SlotAction::next;
            case magda::FollowAction::PlayPrevious:
                return SlotAction::previous;
            case magda::FollowAction::PlayRandom:
                return SlotAction::random;
            case magda::FollowAction::Stop:
                return SlotAction::stop;
            case magda::FollowAction::PlayAgain:
                return SlotAction::again;
        }

        return SlotAction::none;
    }();

    return SlotFollow{action, std::max(1, clip.followActionLoopCount),
                      std::max(0.0, clip.followActionDelayBeats), clip.placement.lengthBeats};
}

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
    span.beats = {startBeat, endBeat};
    // Both ends converted, never a length scaled: a beat span occupies
    // different seconds depending on where it sits on the tempo curve.
    span.seconds = {tempoMap.beatToTime(startBeat), tempoMap.beatToTime(endBeat)};
    return span;
}

}  // namespace

ClipSnapshot compileClipSnapshot(const std::vector<ClipLane>& lanes,
                                 const std::vector<ClipSourceInfo>& sources,
                                 const TempoMap& tempoMap, const GrooveTemplateSet& grooves) {
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

                midi.fold.clipStartBeat = clip.placement.startBeat;
                midi.fold.trimOffsetBeats = std::max(0.0, clip.midiTrimOffset);
                midi.fold.offsetBeats = clip.midiOffset;
                midi.fold.loopEnabled = clip.loopEnabled && clip.loopLengthBeats > 0.0;
                midi.fold.loopStartBeats = clip.loopStartBeats;
                midi.fold.loopLengthBeats = clip.loopLengthBeats;

                // The floor between two points of one densified curve, in this
                // clip's own domain. Seconds is what it means, because
                // smoothness is a wall-clock property and a beat-anchored floor
                // would run at eight hertz at thirty BPM.
                const auto bpm = tempoMap.bpmAt(clip.placement.startBeat);
                midi.events = compileMidiEvents(clip, kCurveFloorSeconds * bpm / 60.0);

                const auto grooveName = clip.grooveTemplate.toStdString();
                if (!grooveName.empty() && !grooves.contains(grooveName))
                    snapshot.diagnostics.push_back(label + "asks for groove '" + grooveName +
                                                   "', which this installation does not have");

                midi.groove = grooves.compile(grooveName, clip.grooveStrength);

                // An empty MIDI clip is carried rather than dropped, unlike an
                // audio clip with no events. A clip with no notes yet is an
                // ordinary thing to have on a lane and it still covers what is
                // under it, whereas an audio clip with nothing to read is a clip
                // that cannot exist.
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
            audio.fadeInBeats = tempoMap.timeToBeat(span.seconds.start + fadeIn) - span.beats.start;
            audio.fadeOutBeats = span.beats.end - tempoMap.timeToBeat(span.seconds.end - fadeOut);

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
                // Inverted, and known to be invertible before anything plays it
                // (WarpMap.hpp). Nothing here mirrors it for a reversed event:
                // reverse stays a read-time coordinate change in
                // EventPlacement.hpp. A marker that cannot be part of a
                // monotonic map is dropped here; saying how many is the
                // difference between a clip whose timing is not what its editor
                // shows and one that says so.
                //
                // The flag travels beside the map because an event that warps
                // with no markers is still a warped event: identity as a map,
                // and on the beat face with a stretcher as an interpretation.
                playback.warpEnabled = event.warpEnabled;

                if (event.warpEnabled) {
                    auto warp = compileWarpMap(event.warpMarkers);
                    playback.warp = std::move(warp.map);

                    if (warp.droppedMarkers > 0)
                        snapshot.diagnostics.push_back(
                            label + "event " + std::to_string(event.id) + " has " +
                            std::to_string(warp.droppedMarkers) +
                            " warp marker(s) that do not run forwards, so they are dropped and "
                            "that stretch of the clip plays at its neighbours' rate");

                    // The steepest segment is what the clip actually asks to be
                    // played at somewhere inside it, and a stretcher will not go
                    // there (ClipStretcher.hpp). The average the pre-roll is
                    // sized against would not notice.
                    if (playback.warp.maxSourcePerWarp() > kMaxStretchRate)
                        snapshot.diagnostics.push_back(
                            label + "event " + std::to_string(event.id) +
                            " has warp markers asking for a rate past what a stretcher will run "
                            "at, so that stretch of it plays at the limit instead");
                }

                playback.autoPitch = event.autoPitch;
                playback.autoPitchMode = event.autoPitchMode;
                playback.pitchChange = event.pitchChange;
                playback.transpose = event.transpose;

                // Half of what auto pitch means is an offset from the project's
                // pitch sequence, and there is no pitch track in the engine yet
                // (#1891). The clip plays, at its own transpose, which is the
                // other half; saying so here is the difference between a known
                // gap and a clip that is quietly a few semitones out.
                if (event.autoPitch)
                    snapshot.diagnostics.push_back(
                        label + "event " + std::to_string(event.id) +
                        " follows the pitch track, which the engine does not have yet, so it "
                        "plays its own transpose alone");

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
            if (a.span.beats.start != b.span.beats.start)
                return a.span.beats.start < b.span.beats.start;
            return a.clipId < b.clipId;
        };
        std::sort(track.audio.begin(), track.audio.end(), byStart);
        std::sort(track.midi.begin(), track.midi.end(), byStart);

        // The slots, each compiled as its own single-clip lane at the origin.
        //
        // Recursing rather than repeating: a session clip and an arrangement clip
        // are the same material and have to sound the same, so the one that is
        // dragged from a slot onto the timeline cannot go through a second
        // implementation of fades, warp and events. What a slot changes is where
        // the material sits, which is nowhere, so the placement is normalised and
        // everything else is asked of the same compile the arrangement uses.
        //
        // Off the audio thread, on the publishing side, so a compile per slot
        // costs a publish rather than a block.
        for (const auto& clip : lane.session) {
            const auto label = clipLabel(lane.trackId, clip.id);

            if (clip.trackId != lane.trackId) {
                snapshot.diagnostics.push_back(label + "belongs to track " +
                                               std::to_string(clip.trackId) +
                                               ", it is not played on this lane");
                continue;
            }
            if (clip.view != ClipView::Session) {
                snapshot.diagnostics.push_back(
                    label + "is an arrangement clip in a session lane, it is not played here");
                continue;
            }
            if (clip.sceneIndex < 0) {
                snapshot.diagnostics.push_back(label + "is a session clip in no scene");
                continue;
            }
            if (!clip.enabled)
                continue;

            // Nothing writes a session clip's placement: the scene index is its
            // position and the beat is leftover. Normalised rather than trusted,
            // so a slot always compiles from the origin whatever is in the field.
            auto normalised = clip;
            normalised.view = ClipView::Arrangement;
            normalised.setPlacementBeats(0.0, clip.placement.lengthBeats);

            ClipLane slotLane;
            slotLane.trackId = lane.trackId;
            slotLane.clips.push_back(normalised);

            auto compiled = compileClipSnapshot({slotLane}, sources, tempoMap, grooves);

            for (auto& diagnostic : compiled.diagnostics)
                snapshot.diagnostics.push_back(std::move(diagnostic));

            if (compiled.tracks.empty())
                continue;

            SessionSlotPlayback slot;
            slot.sceneIndex = clip.sceneIndex;
            slot.lengthBeats = clip.placement.lengthBeats;
            slot.follow = compileFollow(clip);
            slot.audio = std::move(compiled.tracks.front().audio);
            slot.midi = std::move(compiled.tracks.front().midi);

            if (slot.audio.empty() && slot.midi.empty())
                continue;

            track.session.push_back(std::move(slot));
        }

        std::sort(track.session.begin(), track.session.end(),
                  [](const SessionSlotPlayback& a, const SessionSlotPlayback& b) {
                      return a.sceneIndex < b.sceneIndex;
                  });

        // A track earns an entry by having something to play, in either view. A
        // session-only track is a real one: nothing is in its arrangement and
        // its slots are still waiting to be launched.
        if (!track.audio.empty() || !track.midi.empty() || !track.session.empty())
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
