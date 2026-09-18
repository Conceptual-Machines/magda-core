#include "SlotLauncher.hpp"

#include <algorithm>
#include <cmath>

#include "../../core/ClipManager.hpp"
#include "../../core/TrackManager.hpp"
#include "exec/EngineSession.hpp"
#include "launch/LaunchRequests.hpp"
#include "tap/LaunchTap.hpp"
#include "transport/TempoMap.hpp"

namespace magda::daw::engine_host {
namespace {

engine::SlotKey keyOf(const ClipInfo& clip) {
    return engine::SlotKey{clip.trackId, clip.sceneIndex};
}

/// What one quantize step is worth. The names are the model's own: the four
/// multiples are bars, and the four below them are note values, which is why
/// only the first five are counted off the signature.
double quantizeBeats(LaunchQuantize quantize, double beatsPerBar) {
    switch (quantize) {
        case LaunchQuantize::None:
            return 0.0;
        case LaunchQuantize::EightBars:
            return 8.0 * beatsPerBar;
        case LaunchQuantize::FourBars:
            return 4.0 * beatsPerBar;
        case LaunchQuantize::TwoBars:
            return 2.0 * beatsPerBar;
        case LaunchQuantize::OneBar:
            return beatsPerBar;
        case LaunchQuantize::HalfBar:
            return beatsPerBar / 2.0;
        case LaunchQuantize::QuarterBar:
            return 1.0;
        case LaunchQuantize::EighthBar:
            return 0.5;
        case LaunchQuantize::SixteenthBar:
            return 0.25;
    }

    return 0.0;
}

/// Stop whatever else on @p trackId is sounding, on the beat @p launching
/// starts. Asked inside the launching gesture, so the two land together.
///
/// The engine renders every slot whose handle is playing
/// (SessionPlayback.hpp): one clip per track is the session grid's rule, not
/// the launcher's, so the slot being replaced has to be told to stop.
///
/// Read off @p launcher's states rather than off the track's active clip, which
/// is the user's intent and not what is sounding: a follow action moves a run
/// to a slot nobody launched (#2304).
void handOver(const SlotLauncher& launcher, engine::LaunchRequestQueue::Gesture& gesture,
              TrackId trackId, const engine::SlotKey& launching, std::optional<double> due) {
    auto& clips = ClipManager::getInstance();

    for (const auto clipId : clips.getClipsOnTrack(trackId, ClipView::Session)) {
        const auto* clip = clips.getClip(clipId);
        if (clip == nullptr || keyOf(*clip) == launching)
            continue;

        if (launcher.playState(clipId) == SessionClipPlayState::Stopped)
            continue;

        // A plain stop, not a release: the slot taking over holds the track,
        // and releasing the section here would hand it back for the instant
        // between the two (#2302).
        gesture.stop(keyOf(*clip), due);
    }
}

}  // namespace

void SlotLauncher::launch(ClipId clipId) {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    const auto* clip = clips.getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return;

    auto* track = tracks.getTrack(clip->trackId);
    if (track == nullptr)
        return;

    // Toggle mode: clicking what is already playing stops it. Trigger mode
    // re-launches, which is what a re-click means there.
    if (clip->launchMode == LaunchMode::Toggle && host_.launchTransportPlaying() &&
        track->activeSessionClipId == clipId) {
        stop(clipId);
        return;
    }

    // A session clip always loops: an old project can still carry it switched
    // off, and a slot that played once and fell silent is not what the grid
    // means. Through the model, so the republish carries it.
    if (!clip->loopEnabled) {
        clips.setClipLoopEnabled(clipId, true, host_.launchTempo().bpmAt(0.0));
        clip = clips.getClip(clipId);
        if (clip == nullptr || clip->view != ClipView::Session)
            return;
    }

    auto* session = host_.launchSession();
    if (session == nullptr || !hasHandle(*clip))
        return;

    const auto due = dueBeat(*clip);

    {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        // Before the play and on the same lane, so the length is in place when
        // the run begins: the handle re-triggers on it, and the pass it defines
        // is what the playhead below is wrapped against (LaunchRequests.hpp).
        gesture.setLooping(keyOf(*clip), clip->sessionCycleBeats(host_.launchTempo().bpmAt(0.0)));

        // On the same beat as the launch below, so the track hands over on one
        // sample rather than sounding two slots across the gap.
        if (const auto recording = host_.launchRecordTarget(clip->trackId))
            gesture.stop(*recording, due);
        handOver(*this, gesture, clip->trackId, keyOf(*clip), due);

        gesture.play(keyOf(*clip), due);
    }
    noteAsked(*clip);

    // The user's intent, which outlives any one run and is what a transport
    // stop and start re-launches from.
    track->activeSessionClipId = clipId;

    // A launch supersedes a stop the same track was waiting out.
    stopping_.erase(clip->trackId);
    syncPlaybackModes();

    if (!host_.launchTransportPlaying()) {
        host_.startLaunchTransport();
        wasPlaying_ = true;
    }

    lastState_[clipId] = SessionClipPlayState::Queued;
    playheadClip_ = clipId;
    clips.notifyClipPlaybackStateChanged(clipId);
}

void SlotLauncher::stop(ClipId clipId) {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    const auto* clip = clips.getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return;

    auto stoppedId = clipId;
    auto stoppedKey = keyOf(*clip);
    if (auto* track = tracks.getTrack(clip->trackId); track != nullptr) {
        if (const auto* active = clips.getClip(track->activeSessionClipId);
            active != nullptr && active->trackId == clip->trackId &&
            active->view == ClipView::Session) {
            stoppedId = active->id;
            stoppedKey = keyOf(*active);
        }
        track->activeSessionClipId = INVALID_CLIP_ID;
    }

    refreshRecordTargets();
    if (auto* session = host_.launchSession(); session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        for (const auto& key : slotsOnTrack(clip->trackId))
            gesture.backToArrangement(key);
    }

    stopping_[clip->trackId] = PendingStop{stoppedId, stoppedKey, std::nullopt};
    for (const auto other : clips.getClipsOnTrack(clip->trackId, ClipView::Session))
        asked_.erase(other);
    lastState_.erase(clipId);

    if (auto* mutableClip = clips.getClip(clipId); mutableClip != nullptr)
        mutableClip->sessionPlayheadPos = -1.0;

    syncPlaybackModes();
    clips.notifyClipPlaybackStateChanged(clipId);
}

void SlotLauncher::launchScene(const std::vector<TrackId>& trackIds, int sceneIndex) {
    auto* session = host_.launchSession();
    if (session == nullptr)
        return;

    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    // The scene's own beat, resolved once: every slot in it starts together,
    // and resolving per clip would put two of them on different sides of a
    // boundary the gesture straddled.
    std::optional<double> due;
    std::vector<const ClipInfo*> launching;
    std::vector<TrackId> stopped;

    for (const auto trackId : trackIds) {
        const auto clipId = clips.getClipInSlot(trackId, sceneIndex);
        if (clipId == INVALID_CLIP_ID) {
            // An empty slot in a scene is a stop for that track, which is what
            // makes a scene a whole state rather than a row of launches.
            stopped.push_back(trackId);
            continue;
        }

        const auto* clip = clips.getClip(clipId);
        if (clip == nullptr || clip->view != ClipView::Session)
            continue;

        if (!hasHandle(*clip))
            continue;

        if (!due)
            due = dueBeat(*clip);

        launching.push_back(clip);
    }

    if (launching.empty()) {
        for (const auto trackId : stopped)
            stopTrack(trackId);
        return;
    }

    {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        const auto leader = keyOf(*launching.front());

        for (const auto trackId : trackIds)
            if (const auto recording = host_.launchRecordTarget(trackId);
                recording && recording->sceneIndex != sceneIndex)
                gesture.stop(*recording, due);

        for (const auto* clip : launching) {
            gesture.setLooping(keyOf(*clip),
                               clip->sessionCycleBeats(host_.launchTempo().bpmAt(0.0)));
            handOver(*this, gesture, clip->trackId, keyOf(*clip), due);
        }

        // Every follower joins the leader's run rather than starting one of its
        // own, which is what keeps a scene in phase when it is relaunched
        // (LaunchRequests.hpp).
        gesture.play(leader, due);

        for (const auto* clip : launching)
            if (!(keyOf(*clip) == leader))
                gesture.playSynced(keyOf(*clip), leader, due);
    }

    for (const auto* clip : launching) {
        if (auto* track = tracks.getTrack(clip->trackId); track != nullptr)
            track->activeSessionClipId = clip->id;

        noteAsked(*clip);
        stopping_.erase(clip->trackId);
        lastState_[clip->id] = SessionClipPlayState::Queued;
        clips.notifyClipPlaybackStateChanged(clip->id);
    }

    playheadClip_ = launching.front()->id;
    syncPlaybackModes();

    if (!host_.launchTransportPlaying()) {
        host_.startLaunchTransport();
        wasPlaying_ = true;
    }

    for (const auto trackId : stopped)
        stopTrack(trackId);
}

void SlotLauncher::stopTrack(TrackId trackId) {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    auto* track = tracks.getTrack(trackId);
    if (track == nullptr)
        return;

    refreshRecordTargets();

    if (track->activeSessionClipId == INVALID_CLIP_ID) {
        stopping_[trackId] = PendingStop{};
        if (auto* session = host_.launchSession(); session != nullptr) {
            engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());
            for (const auto& key : slotsOnTrack(trackId))
                gesture.backToArrangement(key);
        }
        syncPlaybackModes();
        return;
    }

    const auto clipId = track->activeSessionClipId;
    const auto* clip = clips.getClip(clipId);
    if (clip == nullptr)
        return;

    // There is no run to wait out while transport is stopped, and therefore
    // no later tap acknowledgement that could return this track to Arrangement.
    if (!host_.launchTransportPlaying()) {
        stop(clipId);
        return;
    }

    auto* session = host_.launchSession();
    if (session == nullptr)
        return;

    const auto due = dueBeat(*clip);

    {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());
        for (const auto& key : slotsOnTrack(trackId))
            gesture.backToArrangement(key, due);
    }
    asked_.erase(clipId);

    // Cleared here, so the sweep in processStateEvents knows this track is
    // winding down; the mode stays Session until the handle actually stops, or
    // the arrangement under it would come back before the slot went quiet.
    track->activeSessionClipId = INVALID_CLIP_ID;

    stopping_[trackId] = PendingStop{clipId, keyOf(*clip), due};
    if (due)
        clips.notifyClipPlaybackStateChanged(clipId);

    syncPlaybackModes();
}

void SlotLauncher::stopEverything() {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();
    auto* session = host_.launchSession();

    std::vector<ClipId> stopped;

    for (const auto& track : tracks.getTracks()) {
        if (ownershipOf(track.id) != SessionOwnership::Arrangement)
            stopping_[track.id] = PendingStop{};

        if (track.activeSessionClipId == INVALID_CLIP_ID)
            continue;

        stopped.push_back(track.activeSessionClipId);
        if (const auto* clip = clips.getClip(track.activeSessionClipId))
            stopping_[track.id] = PendingStop{clip->id, keyOf(*clip), std::nullopt};

        if (auto* mutableTrack = tracks.getTrack(track.id); mutableTrack != nullptr)
            mutableTrack->activeSessionClipId = INVALID_CLIP_ID;
    }

    refreshRecordTargets();
    if (session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        for (const auto& track : tracks.getTracks())
            for (const auto& key : slotsOnTrack(track.id))
                gesture.backToArrangement(key);
    }

    for (const auto clipId : stopped) {
        if (auto* clip = clips.getClip(clipId); clip != nullptr)
            clip->sessionPlayheadPos = -1.0;

        clips.notifyClipPlaybackStateChanged(clipId);
    }

    lastState_.clear();
    asked_.clear();
    playheadClip_ = INVALID_CLIP_ID;
    syncPlaybackModes();
}

void SlotLauncher::noteAsked(const ClipInfo& clip) {
    // Everything else on the track is being handed over, so its mark is stale.
    for (const auto other :
         ClipManager::getInstance().getClipsOnTrack(clip.trackId, ClipView::Session))
        asked_.erase(other);

    const auto* tap = tapFor(clip);
    const auto reading = tap != nullptr ? tap->read() : engine::LaunchTap::Reading{};
    asked_[clip.id] = Asked{.playing = reading.playing,
                            .queued = static_cast<int>(reading.queued),
                            .holdsSection = reading.holdsSection,
                            .elapsedBeats = reading.elapsedBeats};
}

SessionClipPlayState SlotLauncher::playState(ClipId clipId) const {
    if (!host_.launchTransportPlaying())
        return SessionClipPlayState::Stopped;

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return SessionClipPlayState::Stopped;

    const auto* tap = tapFor(*clip);
    if (tap == nullptr)
        return SessionClipPlayState::Stopped;

    const auto reading = tap->read();
    if (const auto asked = asked_.find(clipId); asked != asked_.end()) {
        const auto& at = asked->second;
        const bool unanswered =
            reading.playing == at.playing && static_cast<int>(reading.queued) == at.queued &&
            reading.holdsSection == at.holdsSection && reading.elapsedBeats == at.elapsedBeats;
        if (unanswered)
            return SessionClipPlayState::Queued;
        asked_.erase(asked);
    }

    if (reading.playing)
        return SessionClipPlayState::Playing;

    return reading.queued == engine::LaunchTap::Queued::play ? SessionClipPlayState::Queued
                                                             : SessionClipPlayState::Stopped;
}

bool SlotLauncher::stopPending(TrackId trackId) const {
    const auto pending = stopping_.find(trackId);
    return pending != stopping_.end() && pending->second.dueMonotonicBeat.has_value();
}

double SlotLauncher::playheadSeconds(ClipId clipId) const {
    if (!host_.launchTransportPlaying())
        return -1.0;

    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return -1.0;

    const auto* tap = tapFor(*clip);
    if (tap == nullptr)
        return -1.0;

    const auto reading = tap->read();
    if (!reading.playing)
        return -1.0;

    const auto material = materialOf(*clip, host_.launchTempo().bpmAt(0.0));
    auto beats = reading.elapsedBeats;

    if (material.looping && material.passBeats > 0.0)
        beats = std::fmod(beats, material.passBeats);
    else if (material.passBeats > 0.0)
        beats = std::min(beats, material.passBeats);

    const auto bpm = host_.launchTempo().bpmAt(0.0);
    return bpm > 0.0 ? beats * 60.0 / bpm : -1.0;
}

std::unordered_map<ClipId, double> SlotLauncher::playheads() const {
    std::unordered_map<ClipId, double> positions;

    for (const auto& clip : ClipManager::getInstance().getSessionClips()) {
        const auto seconds = playheadSeconds(clip.id);
        if (seconds >= 0.0)
            positions[clip.id] = seconds;
    }

    return positions;
}

void SlotLauncher::processStateEvents() {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    const auto playing = host_.launchTransportPlaying();

    // EngineHost delivers ordinary transport edges synchronously. Keep this
    // fallback for hosts that change transport state independently.
    if (wasPlaying_ && !playing)
        transportStopped();
    else if (!wasPlaying_ && playing)
        transportStarted();

    wasPlaying_ = playing;

    // A stopped tap can take an audio callback to acknowledge its stop. The
    // active clip remains the intent to restore on the next transport start.
    if (!playing) {
        syncPlaybackModes();
        return;
    }

    refreshRecordTargets();
    const auto* session = host_.launchSession();

    for (auto pending = stopping_.begin(); pending != stopping_.end();) {
        const auto trackId = pending->first;
        const auto stopped = pending->second;
        const auto* clip = clips.getClip(stopped.clipId);
        const auto targetStillPublished = stopped.target && clip != nullptr &&
                                          clip->view == ClipView::Session &&
                                          keyOf(*clip) == *stopped.target && session != nullptr &&
                                          session->launchTap(*stopped.target) != nullptr;
        const auto reachedDue =
            (stopped.target && !targetStillPublished) || !stopped.dueMonotonicBeat ||
            (session != nullptr && session->syncPoint().monotonicBeat >= *stopped.dueMonotonicBeat);
        const auto keys = slotsOnTrack(trackId);
        const auto acknowledged =
            reachedDue && std::ranges::none_of(keys, [session](const auto& key) {
                const auto* tap = session != nullptr ? session->launchTap(key) : nullptr;
                if (tap == nullptr)
                    return false;
                const auto reading = tap->read();
                return reading.playing || reading.holdsSection ||
                       reading.queued != engine::LaunchTap::Queued::nothing;
            });

        if (!acknowledged) {
            ++pending;
            continue;
        }

        pending = stopping_.erase(pending);

        if (clip != nullptr)
            clips.notifyClipPlaybackStateChanged(clip->id);
    }

    // Every slot rather than the ones the model calls active: a follow action
    // ends one run and begins another inside the audio thread, and the slot it
    // moved to is only ever heard of here (#2304).
    for (const auto& clip : clips.getSessionClips()) {
        const auto state = playState(clip.id);

        auto* track = tracks.getTrack(clip.trackId);
        if (track == nullptr)
            continue;

        const auto sounding =
            state == SessionClipPlayState::Playing || state == SessionClipPlayState::Queued;
        const auto currentIntent = track->activeSessionClipId;
        const auto currentIntentStopped =
            currentIntent == INVALID_CLIP_ID ||
            (currentIntent != clip.id && playState(currentIntent) == SessionClipPlayState::Stopped);

        if (sounding && !stopping_.contains(clip.trackId) &&
            !host_.launchRecordTarget(clip.trackId) && currentIntent != clip.id &&
            currentIntentStopped) {
            track->activeSessionClipId = clip.id;
            playheadClip_ = clip.id;
        }

        // A run that ended on its own: a one-shot reaching its end, or a stop
        // that has come due. The model's intent is settled here rather than
        // left pointing at a silent slot.
        if (!sounding && track->activeSessionClipId == clip.id) {
            track->activeSessionClipId = INVALID_CLIP_ID;
        }

        if (!sounding) {
            if (auto* mutableClip = clips.getClip(clip.id); mutableClip != nullptr)
                mutableClip->sessionPlayheadPos = -1.0;
        }

        auto& last = lastState_[clip.id];
        if (state != last) {
            last = state;
            clips.notifyClipPlaybackStateChanged(clip.id);
        }
    }

    syncPlaybackModes();

    if (playheadClip_ != INVALID_CLIP_ID && !anythingActive())
        playheadClip_ = INVALID_CLIP_ID;
}

void SlotLauncher::forget() {
    lastState_.clear();
    asked_.clear();
    stopping_.clear();
    recordTargets_.clear();
    playheadClip_ = INVALID_CLIP_ID;
    wasPlaying_ = false;
}

void SlotLauncher::transportStopped() {
    stopForTransport();
    wasPlaying_ = false;
}

void SlotLauncher::transportStarted() {
    relaunchActive();
    wasPlaying_ = true;
}

void SlotLauncher::recordTargetsChanged() {
    refreshRecordTargets();
    syncPlaybackModes();
}

void SlotLauncher::recordTargetLaunched(TrackId trackId) {
    auto& tracks = TrackManager::getInstance();
    auto displaced = INVALID_CLIP_ID;
    if (auto* track = tracks.getTrack(trackId); track != nullptr) {
        displaced = track->activeSessionClipId;
        track->activeSessionClipId = INVALID_CLIP_ID;
    }

    if (const auto* followed = ClipManager::getInstance().getClip(playheadClip_);
        followed != nullptr && followed->trackId == trackId)
        playheadClip_ = INVALID_CLIP_ID;

    for (const auto clipId : ClipManager::getInstance().getClipsOnTrack(trackId, ClipView::Session))
        asked_.erase(clipId);
    if (displaced != INVALID_CLIP_ID)
        ClipManager::getInstance().notifyClipPlaybackStateChanged(displaced);
    stopping_.erase(trackId);
    refreshRecordTargets();
    syncPlaybackModes();
}

std::optional<double> SlotLauncher::dueBeat(const ClipInfo& clip) const {
    auto* session = host_.launchSession();
    if (session == nullptr || clip.launchQuantize == LaunchQuantize::None)
        return std::nullopt;

    // Nothing to be in phase with yet. A first launch into a stopped transport
    // would otherwise wait out a whole bar of silence before anything sounded,
    // and the boundary it waited for only arrives once the transport rolls.
    if (!host_.launchTransportPlaying() || !anythingActive())
        return std::nullopt;

    const auto period = quantizeBeats(clip.launchQuantize, host_.launchBeatsPerBar());
    if (period <= 0.0)
        return std::nullopt;

    // Counted on the signature under the cursor. Under this engine a project
    // holds one signature throughout (#2554), so there is no change for a
    // boundary this far ahead to cross.
    const auto sync = session->syncPoint();
    const auto boundary = std::floor(sync.beat / period + 1.0) * period;

    return sync.monotonicAt(boundary);
}

const engine::LaunchTap* SlotLauncher::tapFor(const ClipInfo& clip) const {
    const auto* session = host_.launchSession();
    return session == nullptr ? nullptr : session->launchTap(keyOf(clip));
}

bool SlotLauncher::hasHandle(const ClipInfo& clip) const {
    // A slot gets a handle from the publish that names it, and a slot that
    // compiled to nothing playable -- an empty MIDI clip -- is never named. A
    // request against one is dropped on arrival, so it is said here instead of
    // looking like a launcher that ignored a click.
    if (tapFor(clip) != nullptr)
        return true;

    juce::Logger::writeToLog("[engine] slot " + juce::String(clip.trackId) + ":" +
                             juce::String(clip.sceneIndex) +
                             " has nothing published to launch, so the launch was dropped");
    return false;
}

SlotLauncher::Material SlotLauncher::materialOf(const ClipInfo& clip, double projectBpm) {
    // The cycle the engine re-triggers the slot on (ClipSnapshot.hpp), so the
    // only modulus a playhead can wrap against without drifting from what is
    // sounding.
    return {.passBeats = clip.sessionCycleBeats(projectBpm), .looping = clip.loopEnabled};
}

void SlotLauncher::stopForTransport() {
    auto& clips = ClipManager::getInstance();
    refreshRecordTargets();

    if (auto* session = host_.launchSession(); session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        for (const auto& track : TrackManager::getInstance().getTracks())
            for (const auto& key : slotsOnTrack(track.id))
                gesture.stop(key);
    }

    // Ordinary active intent stays: what each track was playing is what it
    // plays again when the transport rolls. A pending stop already cleared its
    // intent, so clearing the acknowledgement below returns that track to the
    // arrangement rather than resurrecting it on restart.
    for (const auto& clip : clips.getSessionClips()) {
        if (auto* mutableClip = clips.getClip(clip.id); mutableClip != nullptr)
            mutableClip->sessionPlayheadPos = -1.0;

        if (lastState_.contains(clip.id))
            clips.notifyClipPlaybackStateChanged(clip.id);
    }

    lastState_.clear();
    asked_.clear();
    stopping_.clear();
    syncPlaybackModes();
}

void SlotLauncher::relaunchActive() {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();
    auto* session = host_.launchSession();
    if (session == nullptr)
        return;

    std::vector<const ClipInfo*> relaunching;

    auto repairedStaleIntent = false;
    for (const auto& track : tracks.getTracks()) {
        if (track.activeSessionClipId == INVALID_CLIP_ID)
            continue;

        const auto* clip = clips.getClip(track.activeSessionClipId);
        if (clip == nullptr || clip->view != ClipView::Session || clip->trackId != track.id) {
            if (auto* mutableTrack = tracks.getTrack(track.id); mutableTrack != nullptr)
                mutableTrack->activeSessionClipId = INVALID_CLIP_ID;
            repairedStaleIntent = true;
            continue;
        }

        relaunching.push_back(clip);
    }

    if (repairedStaleIntent)
        syncPlaybackModes();

    if (relaunching.empty())
        return;

    // One gesture: everything the transport stopped starts again together, in
    // the phase it had before.
    engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

    for (const auto* clip : relaunching) {
        gesture.setLooping(keyOf(*clip), clip->sessionCycleBeats(host_.launchTempo().bpmAt(0.0)));
        gesture.play(keyOf(*clip));
        noteAsked(*clip);
    }
}

bool SlotLauncher::anythingActive() const {
    const auto& tracks = TrackManager::getInstance().getTracks();
    return std::ranges::any_of(tracks, [this](const auto& track) {
        return ownershipOf(track.id) != SessionOwnership::Arrangement;
    });
}

SlotLauncher::SessionOwnership SlotLauncher::ownershipOf(TrackId trackId) const {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!host_.launchTransportPlaying())
        return track != nullptr && track->activeSessionClipId != INVALID_CLIP_ID
                   ? SessionOwnership::Retained
                   : SessionOwnership::Arrangement;

    const auto* session = host_.launchSession();
    if (session != nullptr) {
        for (const auto& key : slotsOnTrack(trackId)) {
            const auto* tap = session->launchTap(key);
            if (tap != nullptr && tap->read().holdsSection)
                return SessionOwnership::Held;
        }
    }

    if (stopping_.contains(trackId))
        return SessionOwnership::Queued;

    if (track != nullptr && track->activeSessionClipId != INVALID_CLIP_ID) {
        if (playState(track->activeSessionClipId) == SessionClipPlayState::Queued)
            return SessionOwnership::Queued;
    }

    if (host_.launchRecordTarget(trackId))
        return SessionOwnership::Queued;

    return SessionOwnership::Arrangement;
}

std::vector<engine::SlotKey> SlotLauncher::slotsOnTrack(TrackId trackId) const {
    std::vector<engine::SlotKey> keys;
    for (const auto clipId :
         ClipManager::getInstance().getClipsOnTrack(trackId, ClipView::Session)) {
        if (const auto* clip = ClipManager::getInstance().getClip(clipId))
            keys.push_back(keyOf(*clip));
    }

    for (const auto& key : recordTargets_) {
        if (key.trackId != trackId || std::ranges::find(keys, key) != keys.end())
            continue;
        keys.push_back(key);
    }
    return keys;
}

void SlotLauncher::refreshRecordTargets() {
    const auto& tracks = TrackManager::getInstance().getTracks();
    for (const auto& track : tracks) {
        const auto target = host_.launchRecordTarget(track.id);
        if (target && std::ranges::find(recordTargets_, *target) == recordTargets_.end())
            recordTargets_.push_back(*target);
    }

    const auto* session = host_.launchSession();
    std::erase_if(recordTargets_, [this, session](const auto& key) {
        if (host_.launchRecordTarget(key.trackId) == key)
            return false;
        const auto* tap = session != nullptr ? session->launchTap(key) : nullptr;
        if (tap == nullptr)
            return true;
        const auto reading = tap->read();
        return !reading.playing && !reading.holdsSection &&
               reading.queued == engine::LaunchTap::Queued::nothing;
    });
}

void SlotLauncher::syncPlaybackModes() {
    refreshRecordTargets();
    auto& tracks = TrackManager::getInstance();

    for (const auto& track : tracks.getTracks()) {
        const auto ownership = ownershipOf(track.id);
        const auto session =
            ownership == SessionOwnership::Held || ownership == SessionOwnership::Retained;
        tracks.setTrackPlaybackMode(track.id, session ? TrackPlaybackMode::Session
                                                      : TrackPlaybackMode::Arrangement);
    }
}

}  // namespace magda::daw::engine_host
