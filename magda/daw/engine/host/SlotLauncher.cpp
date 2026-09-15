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

/// Whether anything is sounding, which decides whether the first launch of a
/// set waits for a boundary or starts where it was asked.
bool anythingActive() {
    const auto& tracks = TrackManager::getInstance().getTracks();
    return std::ranges::any_of(
        tracks, [](const auto& track) { return track.activeSessionClipId != INVALID_CLIP_ID; });
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
    if (clip->launchMode == LaunchMode::Toggle && track->activeSessionClipId == clipId) {
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
    juce::Logger::writeToLog("[tempo] launch at beat " +
                             (due ? juce::String(*due, 3) : juce::String("next")) + ": " +
                             ClipManager::describeLoopGeometry(*clip));

    {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        // Before the play and on the same lane, so the length is in place when
        // the run begins: the handle re-triggers on it, and the pass it defines
        // is what the playhead below is wrapped against (LaunchRequests.hpp).
        gesture.setLooping(keyOf(*clip), clip->sessionCycleBeats());

        // On the same beat as the launch below, so the track hands over on one
        // sample rather than sounding two slots across the gap.
        handOver(*this, gesture, clip->trackId, keyOf(*clip), due);

        gesture.play(keyOf(*clip), due);
    }

    // The user's intent, which outlives any one run and is what a transport
    // stop and start re-launches from.
    track->activeSessionClipId = clipId;
    syncPlaybackModes();

    // A launch supersedes a stop the same track was waiting out.
    stopping_.erase(clip->trackId);

    if (!host_.launchTransportPlaying())
        host_.startLaunchTransport();

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

    if (auto* track = tracks.getTrack(clip->trackId);
        track != nullptr && track->activeSessionClipId == clipId)
        track->activeSessionClipId = INVALID_CLIP_ID;

    if (auto* session = host_.launchSession(); session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        // Not a plain stop: a slot that stops without releasing keeps holding
        // its track, and the arrangement under it stays silent (#2302).
        gesture.backToArrangement(keyOf(*clip));
    }

    lastState_.erase(clipId);
    stopping_.erase(clip->trackId);

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

        for (const auto* clip : launching) {
            gesture.setLooping(keyOf(*clip), clip->sessionCycleBeats());
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

        stopping_.erase(clip->trackId);
        lastState_[clip->id] = SessionClipPlayState::Queued;
        clips.notifyClipPlaybackStateChanged(clip->id);
    }

    playheadClip_ = launching.front()->id;
    syncPlaybackModes();

    if (!host_.launchTransportPlaying())
        host_.startLaunchTransport();

    for (const auto trackId : stopped)
        stopTrack(trackId);
}

void SlotLauncher::stopTrack(TrackId trackId) {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();

    auto* track = tracks.getTrack(trackId);
    if (track == nullptr || track->activeSessionClipId == INVALID_CLIP_ID)
        return;

    const auto clipId = track->activeSessionClipId;
    const auto* clip = clips.getClip(clipId);
    if (clip == nullptr)
        return;

    auto* session = host_.launchSession();
    if (session == nullptr)
        return;

    const auto due = dueBeat(*clip);

    {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());
        gesture.stop(keyOf(*clip), due);
    }

    // Cleared here, so the sweep in processStateEvents knows this track is
    // winding down; the mode stays Session until the handle actually stops, or
    // the arrangement under it would come back before the slot went quiet.
    track->activeSessionClipId = INVALID_CLIP_ID;

    // Only when there is a wait to draw: an unquantized stop lands on the next
    // block and there is no window for the affordance to blink in.
    if (due) {
        stopping_.insert(trackId);
        clips.notifyClipPlaybackStateChanged(clipId);
    }
}

void SlotLauncher::stopEverything() {
    auto& clips = ClipManager::getInstance();
    auto& tracks = TrackManager::getInstance();
    auto* session = host_.launchSession();

    std::vector<ClipId> stopped;

    for (const auto& track : tracks.getTracks()) {
        if (track.activeSessionClipId == INVALID_CLIP_ID)
            continue;

        stopped.push_back(track.activeSessionClipId);

        if (auto* mutableTrack = tracks.getTrack(track.id); mutableTrack != nullptr)
            mutableTrack->activeSessionClipId = INVALID_CLIP_ID;
    }

    if (session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        // Every slot, not only what the model called active: a follow action
        // can have moved the run to a slot nothing here ever launched.
        for (const auto& clip : clips.getSessionClips())
            gesture.backToArrangement(keyOf(clip));
    }

    for (const auto clipId : stopped) {
        if (auto* clip = clips.getClip(clipId); clip != nullptr)
            clip->sessionPlayheadPos = -1.0;

        clips.notifyClipPlaybackStateChanged(clipId);
    }

    forget();
    syncPlaybackModes();
}

SessionClipPlayState SlotLauncher::playState(ClipId clipId) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return SessionClipPlayState::Stopped;

    const auto* tap = tapFor(*clip);
    if (tap == nullptr)
        return SessionClipPlayState::Stopped;

    const auto reading = tap->read();
    if (reading.playing)
        return SessionClipPlayState::Playing;

    return reading.queued == engine::LaunchTap::Queued::play ? SessionClipPlayState::Queued
                                                             : SessionClipPlayState::Stopped;
}

bool SlotLauncher::stopPending(TrackId trackId) const {
    return stopping_.contains(trackId);
}

double SlotLauncher::playheadSeconds(ClipId clipId) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return -1.0;

    const auto* tap = tapFor(*clip);
    if (tap == nullptr)
        return -1.0;

    const auto reading = tap->read();
    if (!reading.playing)
        return -1.0;

    const auto material = materialOf(*clip);
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

    if (wasPlaying_ && !playing) {
        wasPlaying_ = playing;
        stopForTransport();
        return;
    }

    if (!wasPlaying_ && playing)
        relaunchActive();

    wasPlaying_ = playing;

    auto modesChanged = false;

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

        if (sounding && track->activeSessionClipId != clip.id) {
            track->activeSessionClipId = clip.id;
            playheadClip_ = clip.id;
            stopping_.erase(clip.trackId);
            modesChanged = true;
        }

        // A run that ended on its own: a one-shot reaching its end, or a stop
        // that has come due. The model's intent is settled here rather than
        // left pointing at a silent slot.
        if (!sounding && track->activeSessionClipId == clip.id) {
            track->activeSessionClipId = INVALID_CLIP_ID;
            modesChanged = true;
        }

        if (!sounding) {
            stopping_.erase(clip.trackId);

            if (auto* mutableClip = clips.getClip(clip.id); mutableClip != nullptr)
                mutableClip->sessionPlayheadPos = -1.0;
        }

        auto& last = lastState_[clip.id];
        if (state != last) {
            last = state;
            clips.notifyClipPlaybackStateChanged(clip.id);
        }
    }

    if (modesChanged)
        syncPlaybackModes();

    if (playheadClip_ != INVALID_CLIP_ID && !anythingActive())
        playheadClip_ = INVALID_CLIP_ID;
}

void SlotLauncher::forget() {
    lastState_.clear();
    stopping_.clear();
    playheadClip_ = INVALID_CLIP_ID;
    wasPlaying_ = false;
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

SlotLauncher::Material SlotLauncher::materialOf(const ClipInfo& clip) {
    // The cycle the engine re-triggers the slot on (ClipSnapshot.hpp), so the
    // only modulus a playhead can wrap against without drifting from what is
    // sounding.
    return {.passBeats = clip.sessionCycleBeats(), .looping = clip.loopEnabled};
}

void SlotLauncher::stopForTransport() {
    auto& clips = ClipManager::getInstance();

    if (auto* session = host_.launchSession(); session != nullptr) {
        engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

        for (const auto& clip : clips.getSessionClips())
            gesture.stop(keyOf(clip));
    }

    // The intent stays: what each track was playing is what it plays again when
    // the transport rolls. Only the runs end, so a slot starts its material
    // from the top rather than from wherever the transport stopped.
    for (const auto& clip : clips.getSessionClips()) {
        if (auto* mutableClip = clips.getClip(clip.id); mutableClip != nullptr)
            mutableClip->sessionPlayheadPos = -1.0;

        if (lastState_.contains(clip.id))
            clips.notifyClipPlaybackStateChanged(clip.id);
    }

    lastState_.clear();
    stopping_.clear();
}

void SlotLauncher::relaunchActive() {
    auto& clips = ClipManager::getInstance();
    auto* session = host_.launchSession();
    if (session == nullptr)
        return;

    std::vector<const ClipInfo*> relaunching;

    for (const auto& track : TrackManager::getInstance().getTracks()) {
        if (track.activeSessionClipId == INVALID_CLIP_ID)
            continue;

        if (playState(track.activeSessionClipId) != SessionClipPlayState::Stopped)
            continue;

        if (const auto* clip = clips.getClip(track.activeSessionClipId); clip != nullptr)
            relaunching.push_back(clip);
    }

    if (relaunching.empty())
        return;

    // One gesture: everything the transport stopped starts again together, in
    // the phase it had before.
    engine::LaunchRequestQueue::Gesture gesture(session->launchRequests());

    for (const auto* clip : relaunching) {
        gesture.setLooping(keyOf(*clip), clip->sessionCycleBeats());
        gesture.play(keyOf(*clip));
    }
}

void SlotLauncher::syncPlaybackModes() {
    auto& tracks = TrackManager::getInstance();

    for (const auto& track : tracks.getTracks())
        tracks.setTrackPlaybackMode(track.id, track.activeSessionClipId != INVALID_CLIP_ID
                                                  ? TrackPlaybackMode::Session
                                                  : TrackPlaybackMode::Arrangement);
}

}  // namespace magda::daw::engine_host
