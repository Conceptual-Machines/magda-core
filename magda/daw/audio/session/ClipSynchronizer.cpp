#include "session/ClipSynchronizer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <unordered_set>

#include "../../core/ClipManager.hpp"
#include "../../core/ClipOperations.hpp"
#include "../../core/TempoUtils.hpp"
#include "../../core/TrackManager.hpp"
#include "ArrangementClipSyncPlanner.hpp"
#include "ClipLaunchQuantization.hpp"
#include "TrackController.hpp"
#include "WarpMarkerManager.hpp"

namespace magda {

namespace {

// Project tempo sampled at the clip's start beat (curve-aware), for the
// source<->time conversions TE's time-only APIs (clip offset, non-autoTempo
// loop range) still require. Never sample at time 0: that ignores the tempo
// automation and is wrong the moment the tempo isn't constant.
double projectBpmAtClip(te::Edit& edit, const ClipInfo& clip) {
    return edit.tempoSequence.getBpmAtBeat(te::BeatPosition::fromBeats(clip.placement.startBeat));
}

double timelineLengthBeats(const ClipInfo& clip, double bpm) {
    if (clip.placement.lengthBeats > 0.0)
        return clip.placement.lengthBeats;
    if (clip.lengthBeats > 0.0)
        return clip.lengthBeats;
    const double resolvedBpm = isValidBpm(bpm) ? bpm : DEFAULT_BPM;
    return clip.getTimelineLength(resolvedBpm) * resolvedBpm / 60.0;
}

te::FollowAction toTracktionFollowAction(FollowAction action) {
    switch (action) {
        case FollowAction::None:
            return te::FollowAction::none;
        case FollowAction::PlayNext:
            return te::FollowAction::trackNext;
        case FollowAction::PlayPrevious:
            return te::FollowAction::trackPrevious;
        case FollowAction::PlayRandom:
            return te::FollowAction::trackAny;
        case FollowAction::Stop:
            return te::FollowAction::globalStop;
        case FollowAction::PlayAgain:
            return te::FollowAction::globalPlayAgain;
    }
    return te::FollowAction::none;
}

// The bridge keys its engine-id map on ClipId because Phase A of #1901 gives
// every audio clip exactly one event: one clip is one TE clip. When a clip can
// hold several events, the audioEventRef() calls below become a loop over
// ClipInfo::events() and the map grows an EventId.

bool isSessionLooping(const ClipInfo& clip) {
    return clip.view == ClipView::Session || clip.loopEnabled;
}

double followActionBaseLengthBeats(const ClipInfo& clip, double bpm) {
    if (isSessionLooping(clip)) {
        if (clip.isAudio() && audioEventRef(clip).autoTempo) {
            auto [_, loopLengthBeats] = ClipOperations::getAutoTempoBeatRange(audioEventRef(clip));
            if (loopLengthBeats > 0.0)
                return loopLengthBeats;
        }

        if (clip.loopLengthInBeats(bpm) > 0.0)
            return clip.loopLengthInBeats(bpm);

        const double sourceLength =
            audioEventRef(clip).sourceLengthSeconds(clip.getTimelineLength(bpm));
        const double speed =
            audioEventRef(clip).speedRatio > 0.0 ? audioEventRef(clip).speedRatio : 1.0;
        if (sourceLength > 0.0)
            return (sourceLength / speed) * bpm / 60.0;
    }

    return timelineLengthBeats(clip, bpm);
}

bool syncFollowActionToTracktionClip(te::Clip& teClip, const ClipInfo& clip, double bpm) {
    bool changed = false;
    auto* followActions = teClip.getFollowActions();
    if (!followActions)
        return false;

    auto removeAllActions = [&] {
        std::vector<te::FollowActions::Action*> actions(followActions->getActions().begin(),
                                                        followActions->getActions().end());
        for (auto* action : actions) {
            followActions->removeAction(*action);
            changed = true;
        }
    };

    const auto desiredAction = toTracktionFollowAction(clip.followAction);

    if (desiredAction == te::FollowAction::none) {
        removeAllActions();
        return changed;
    }

    auto actions = followActions->getActions();
    if (actions.size() != 1) {
        removeAllActions();
        auto& action = followActions->addAction();
        action.action = desiredAction;
        action.weight = 1.0;
        changed = true;
    } else {
        auto* action = actions.front();
        if (action->action.get() != desiredAction) {
            action->action = desiredAction;
            changed = true;
        }
        if (std::abs(action->weight.get() - 1.0) > 0.0001) {
            action->weight = 1.0;
            changed = true;
        }
    }

    if (teClip.followActionDurationType.get() != te::Clip::FollowActionDurationType::beats) {
        teClip.followActionDurationType = te::Clip::FollowActionDurationType::beats;
        changed = true;
    }

    const double baseBeats = followActionBaseLengthBeats(clip, bpm);
    const int loopCount = juce::jmax(1, clip.followActionLoopCount);
    const double durationBeats = (isSessionLooping(clip) ? baseBeats * loopCount : baseBeats) +
                                 juce::jmax(0.0, clip.followActionDelayBeats);
    const auto duration = te::BeatDuration::fromBeats(juce::jmax(0.0, durationBeats));
    if (std::abs(teClip.followActionBeats.get().inBeats() - duration.inBeats()) > 0.0001) {
        teClip.followActionBeats = duration;
        changed = true;
    }

    const double loops = static_cast<double>(loopCount);
    if (std::abs(teClip.followActionNumLoops.get() - loops) > 0.0001) {
        teClip.followActionNumLoops = loops;
        changed = true;
    }

    return changed;
}

constexpr double warpMarkerSyncEpsilonSeconds = 1.0e-6;

bool warpMarkerMapsMatch(const juce::Array<te::WarpMarker*>& engineMarkers,
                         const std::vector<WarpMarker>& savedMarkers) {
    if (engineMarkers.size() != static_cast<int>(savedMarkers.size()))
        return false;

    for (size_t i = 0; i < savedMarkers.size(); ++i) {
        const auto* engineMarker = engineMarkers[static_cast<int>(i)];
        const auto& savedMarker = savedMarkers[i];
        if (engineMarker == nullptr ||
            std::abs(engineMarker->sourceTime.inSeconds() - savedMarker.sourceTime) >
                warpMarkerSyncEpsilonSeconds ||
            std::abs(engineMarker->warpTime.inSeconds() - savedMarker.warpTime) >
                warpMarkerSyncEpsilonSeconds) {
            return false;
        }
    }

    return true;
}

bool restoreWarpMarkersIfNeeded(te::WarpTimeManager& warpManager,
                                const std::vector<WarpMarker>& markers) {
    // A valid map needs both boundary markers. In particular, do not insert a
    // lone saved marker on top of TE's recreated boundaries: that can create a
    // zero-length segment in WarpTimeManager's stretch-ratio calculation.
    if (markers.size() < 2)
        return false;

    const auto& existingMarkers = warpManager.getMarkers();

    // More than the two default boundaries means the live map has already been
    // edited. Never overwrite it from an older model snapshot.
    if (existingMarkers.size() > 2 || warpMarkerMapsMatch(existingMarkers, markers))
        return false;

    // removeAllMarkers deliberately recreates TE's start/end boundaries, so
    // inserting the complete saved list would duplicate both boundaries.
    warpManager.removeAllMarkers();

    auto& defaultMarkers = warpManager.getMarkers();
    warpManager.moveMarker(0, te::TimePosition::fromSeconds(markers.front().warpTime));
    warpManager.moveMarker(defaultMarkers.size() - 1,
                           te::TimePosition::fromSeconds(markers.back().warpTime));

    for (size_t i = 1; i + 1 < markers.size(); ++i) {
        warpManager.insertMarker(
            te::WarpMarker(te::TimePosition::fromSeconds(markers[i].sourceTime),
                           te::TimePosition::fromSeconds(markers[i].warpTime)));
    }

    return true;
}

bool syncWarpStateToTracktionClip(te::WaveAudioClip& audioClip, const ClipInfo& clip) {
    bool changed = false;

    if (!audioEventRef(clip).warpEnabled) {
        if (audioClip.getWarpTime()) {
            audioClip.setWarpTime(false);
            changed = true;
        }
        return changed;
    }

    if (!audioClip.getWarpTime()) {
        audioClip.setWarpTime(true);
        changed = true;
    }

    if (!audioEventRef(clip).warpMarkers.empty()) {
        auto& warpManager = audioClip.getWarpTimeManager();

        // A newly-created WarpTimeManager contains only its two default
        // boundary markers. Replace those with the marker map copied from the
        // source clip, but never overwrite an already-live edited map.
        if (restoreWarpMarkersIfNeeded(warpManager, audioEventRef(clip).warpMarkers))
            changed = true;
    }

    return changed;
}

}  // namespace

void ClipSynchronizer::reallocateAndNotify() {
    if (auto* ctx = edit_.getCurrentPlaybackContext()) {
        edit_.getTransport().editHasChanged();
        ctx->reallocate();
        if (onGraphReallocated)
            onGraphReallocated();
    } else {
    }
}

static void syncAudioSourceInterpretationToLoopInfo(te::WaveAudioClip& audioClip,
                                                    const ClipInfo& clip) {
    auto waveInfo = audioClip.getWaveInfo();
    auto& li = audioClip.getLoopInfo();

    if (audioEventRef(clip).interpBpm > 0.0 &&
        std::abs(li.getBpm(waveInfo) - audioEventRef(clip).interpBpm) > 0.1) {
        li.setBpm(audioEventRef(clip).interpBpm, waveInfo);
    }

    if (audioEventRef(clip).interpTotalBeats > 0.0 &&
        std::abs(li.getNumBeats() - audioEventRef(clip).interpTotalBeats) > 0.001) {
        li.setNumBeats(audioEventRef(clip).interpTotalBeats);
    }
}

/// Seed the interpretation from Tracktion's loopInfo, filling gaps only. The
/// file duration is a Source fact, so it lands on the pooled source.
static void seedInterpretationFromLoopInfo(ClipInfo& clip, double numBeats, double bpm) {
    auto* event = clip.primaryEvent();
    if (event == nullptr)
        return;

    event->seedInterpretation(numBeats, bpm);

    // A Source is shared by every clip on the file, so this estimate is only
    // ever a stand-in for facts we could not read: an unresolved source with no
    // duration at all. Once the file opens, probe() owns both numbers, and one
    // event's odd interpBpm must not restate them for unrelated clips.
    if (auto* source = SourcePool::getInstance().getMutable(event->sourceId);
        source != nullptr && !source->isResolved() && source->durationSeconds <= 0.0 &&
        event->interpTotalBeats > 0.0 && event->interpBpm > 0.0) {
        source->durationSeconds = event->interpTotalBeats * 60.0 / event->interpBpm;
    }
}

/// A session clip imported before its source beat domain was known carries a
/// zero-length loop region, meaning "the whole source". Give it a real one now
/// that the interpretation has arrived.
static void initialiseSourceLoopRegionFromMetadata(ClipInfo& clip) {
    auto* event = clip.primaryEvent();
    if (event == nullptr || !event->autoTempo || event->loopLengthSamples > 0)
        return;
    if (event->interpBpm <= 0.0 || event->interpTotalBeats <= 0.0)
        return;

    const double startBeats = juce::jmin(event->loopStartBeats(), event->interpTotalBeats);
    event->setLoopStartBeats(startBeats);
    event->setLoopLengthBeats(juce::jmax(0.0, event->interpTotalBeats - startBeats));
}

ClipSynchronizer::ClipSynchronizer(te::Edit& edit, TrackController& trackController,
                                   WarpMarkerManager& warpMarkerManager)
    : edit_(edit),
      trackController_(trackController),
      warpSync_(edit_, warpMarkerManager, clipIds_,
                [this](ClipId clipId) { return getSessionTeClip(clipId); }) {
    ClipManager::getInstance().addListener(this);
    TrackManager::getInstance().addListener(this);
}

ClipSynchronizer::~ClipSynchronizer() {
    ClipManager::getInstance().removeListener(this);
    TrackManager::getInstance().removeListener(this);
}

// =============================================================================
// TrackManagerListener Interface
// =============================================================================

void ClipSynchronizer::syncPlaybackModeToEngine(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    auto* audioTrack = trackController_.getAudioTrack(trackId);
    if (trackInfo && audioTrack) {
        bool newVal = (trackInfo->playbackMode == TrackPlaybackMode::Session);
        audioTrack->playSlotClips = newVal;
    }
}

void ClipSynchronizer::trackPropertyChanged(int trackId) {
    syncPlaybackModeToEngine(trackId);
}

// =============================================================================
// ClipManagerListener Interface
// =============================================================================

void ClipSynchronizer::clipsChanged() {
    auto& clipManager = ClipManager::getInstance();

    // Only sync arrangement clips - session clips are managed by SessionClipScheduler
    const auto& arrangementClips = clipManager.getArrangementClips();
    const auto& sessionClips = clipManager.getSessionClips();

    auto arrangementPlan = buildArrangementClipSyncPlan(edit_, trackController_, arrangementClips,
                                                        sessionClips, clipIds_);

    // Remove deleted clips from engine
    for (ClipId clipId : arrangementPlan.clipsToRemove) {
        removeClipFromEngine(clipId);
    }

    bool arrangementTopologyChanged = !arrangementPlan.clipsToRemove.empty();
    for (ClipId clipId : arrangementPlan.clipsToSync) {
        arrangementTopologyChanged =
            syncArrangementClipToEngine(clipId) || arrangementTopologyChanged;
    }

    // A clip that arrived, moved or went away changes what the clips sharing
    // its lane play (#2003), and nothing else would tell them.
    arrangementTopologyChanged = syncOcclusionChanges() || arrangementTopologyChanged;

    // Sync session clips to ClipSlots
    bool sessionClipsSynced = false;
    for (const auto& clip : sessionClips) {
        if (syncSessionClipToSlot(clip.id)) {
            sessionClipsSynced = true;
        }
    }

    // Force graph rebuild when clip topology changes. Tracktion's live
    // playback context doesn't automatically pick up newly inserted
    // arrangement WaveAudioClips, so split/duplicate copies can exist in the
    // edit but stay silent until the graph is rebuilt.
    if (arrangementTopologyChanged || sessionClipsSynced)
        reallocateAndNotify();
}

void ClipSynchronizer::clipPropertyChanged(ClipId clipId) {
    const bool clipNeedsRealloc = syncClipPropertyToEngine(clipId);
    const bool laneNeedsRealloc = syncOcclusionChanges();
    if (clipNeedsRealloc || laneNeedsRealloc)
        reallocateAndNotify();
}

void ClipSynchronizer::clipPropertiesChanged(const std::vector<ClipId>& clipIds) {
    std::unordered_set<ClipId> seen;
    seen.reserve(clipIds.size());

    bool needsGraphReallocation = false;
    for (auto clipId : clipIds) {
        if (!seen.insert(clipId).second)
            continue;

        needsGraphReallocation = syncClipPropertyToEngine(clipId) || needsGraphReallocation;
    }

    needsGraphReallocation = syncOcclusionChanges() || needsGraphReallocation;

    if (needsGraphReallocation)
        reallocateAndNotify();
}

bool ClipSynchronizer::syncClipPropertyToEngine(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("ClipSynchronizer::syncClipPropertyToEngine: clip " << clipId
                                                                << " not found in ClipManager");
        return false;
    }
    if (clip->view == ClipView::Session) {
        bool needsGraphReallocation = false;

        // Session clip property changed (e.g. sceneIndex set after creation).
        // Try to sync it to a slot if not already synced.
        if (clip->sceneIndex >= 0) {
            bool synced = syncSessionClipToSlot(clipId);

            if (synced) {
                // New clip synced — rebuild graph so SlotControlNode is created.
                return true;
            } else {
                // Clip already synced — propagate property changes to TE clip.
                // Only update properties that have actually changed to avoid
                // disrupting a playing LaunchHandle.
                auto* teClip = getSessionTeClip(clipId);
                if (teClip) {
                    // Enabled toggle (#1736) — same guarded write as the
                    // arrangement path in syncArrangementClipToEngine.
                    {
                        const bool wantDisabled = !clip->enabled;
                        if (teClip->disabled.get() != wantDisabled)
                            teClip->disabled = wantDisabled;
                    }

                    // Push the length to TE in beats and let the engine resolve
                    // the seconds from its tempo sequence. This stays correct
                    // under a tempo ramp with no re-push, and avoids the
                    // beat->seconds round-trip that drifts when the tempo curve
                    // varies (issue #1157).
                    const double clipLengthBeats = clip->placement.lengthBeats;
                    if (std::abs(teClip->getLengthBeats().inBeats() - clipLengthBeats) > 1.0e-6) {
                        teClip->setLength(te::BeatDuration::fromBeats(clipLengthBeats), false);
                    }

                    // Update launch quantization (lightweight CachedValue, always safe)
                    auto* lq = teClip->getLaunchQuantisation();
                    if (lq) {
                        lq->type = clip_launch::toTracktionLaunchQType(clip->launchQuantize);
                    }
                    const double followBpm = edit_.tempoSequence.getBpmAtBeat(
                        te::BeatPosition::fromBeats(clip->placement.startBeat));
                    needsGraphReallocation =
                        syncFollowActionToTracktionClip(*teClip, *clip, followBpm) ||
                        needsGraphReallocation;

                    // AutoTempo handling for audio clips
                    bool isAutoTempoAudio = clip->isAudio() && audioEventRef(*clip).autoTempo;

                    // configureSessionAutoTempo applies the stretch mode
                    // itself; remember what it was so the stretch-mode block
                    // below still detects the switch and requests the
                    // explicit graph rebuild (proxy off + reallocation).
                    std::optional<te::TimeStretcher::Mode> stretchModeBefore;

                    if (isAutoTempoAudio) {
                        auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip);
                        if (audioClip) {
                            stretchModeBefore = audioClip->getTimeStretchMode();
                            configureSessionAutoTempo(audioClip, clip);
                        }
                    } else {
                        // Note: do NOT call setAutoTempo(false) here.
                        // TE's ClipOwner auto-enables autoTempo on session slot clips
                        // and toggling it breaks the audio pipeline.

                        // Time-based loop state (existing behavior)
                        double projectBpm = projectBpmAtClip(edit_, *clip);
                        if (clip->loopEnabled && clip->isMidi()) {
                            // MIDI loops in clip beats. Routing it through the
                            // audio event's source region reads an empty one
                            // and loops the whole container instead.
                            const double loopBeats = clip->loopLengthBeats > 0.0
                                                         ? clip->loopLengthBeats
                                                         : clip->getLengthInBeats(projectBpm);
                            teClip->setLoopRangeBeats(
                                {te::BeatPosition::fromBeats(clip->loopStartBeats),
                                 te::BeatPosition::fromBeats(clip->loopStartBeats + loopBeats)});
                        } else if (clip->loopEnabled) {
                            const double timelineLength = clip->getTimelineLength(projectBpm);
                            if (audioEventRef(*clip).sourceLengthSeconds(timelineLength) > 0.0) {
                                teClip->setLoopRange(te::TimeRange(
                                    te::TimePosition::fromSeconds(
                                        audioEventRef(*clip).engineLoopStartSeconds()),
                                    te::TimePosition::fromSeconds(
                                        audioEventRef(*clip).engineLoopEndSeconds(
                                            timelineLength))));
                            }
                        } else if (teClip->isLooping()) {
                            teClip->disableLooping();
                        }

                        // Neutralize embedded tempo metadata: set source BPM =
                        // project BPM so the auto-enabled autoTempo doesn't cause
                        // unwanted speed changes.
                        if (clip->isAudio()) {
                            if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
                                auto& li = audioClip->getLoopInfo();
                                auto waveInfo = audioClip->getWaveInfo();
                                li.setBpm(projectBpm, waveInfo);
                            }
                        }
                    }

                    // Update looping on the launch handle
                    auto launchHandle = teClip->getLaunchHandle();
                    if (launchHandle) {
                        if (clip->loopEnabled) {
                            if (isAutoTempoAudio) {
                                // AutoTempo: loop beats come from beat fields
                                double bpm = projectBpmAtClip(edit_, *clip);
                                auto [loopStartBeats, loopLengthBeats] =
                                    ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));
                                if (loopLengthBeats > 0.0)
                                    launchHandle->setLooping(
                                        te::BeatDuration::fromBeats(loopLengthBeats));
                            } else if (clip->isMidi()) {
                                // Already clip beats, same as launchSessionClip.
                                const double loopBeats =
                                    clip->loopLengthBeats > 0.0
                                        ? clip->loopLengthBeats
                                        : clip->getLengthInBeats(projectBpmAtClip(edit_, *clip));
                                launchHandle->setLooping(te::BeatDuration::fromBeats(loopBeats));
                            } else {
                                double bpm = projectBpmAtClip(edit_, *clip);
                                double loopLengthSeconds = audioEventRef(*clip).sourceLengthSeconds(
                                                               clip->getTimelineLength(bpm)) /
                                                           audioEventRef(*clip).speedRatio;
                                double bps = bpm / 60.0;
                                double loopLengthBeats = loopLengthSeconds * bps;
                                launchHandle->setLooping(
                                    te::BeatDuration::fromBeats(loopLengthBeats));
                            }
                        } else {
                            launchHandle->setLooping(std::nullopt);
                        }
                    }

                    // Sync session-applicable audio clip properties
                    if (clip->isAudio()) {
                        auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip);
                        if (audioClip) {
                            // Pitch
                            bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
                            if (audioEventRef(*clip).autoPitch != audioClip->getAutoPitch())
                                audioClip->setAutoPitch(isAnalog ? false
                                                                 : audioEventRef(*clip).autoPitch);
                            if (isAnalog) {
                                if (std::abs(audioClip->getPitchChange()) > 0.001f)
                                    audioClip->setPitchChange(0.0f);
                            } else {
                                if (std::abs(audioClip->getPitchChange() -
                                             audioEventRef(*clip).pitchChange) > 0.001f)
                                    audioClip->setPitchChange(audioEventRef(*clip).pitchChange);
                            }
                            if (audioClip->getTransposeSemiTones(false) !=
                                audioEventRef(*clip).transpose)
                                audioClip->setTranspose(audioEventRef(*clip).transpose);
                            // Playback
                            if (audioEventRef(*clip).reversed != audioClip->getIsReversed())
                                audioClip->setIsReversed(audioEventRef(*clip).reversed);
                            // Per-Clip Mix
                            {
                                float combinedGain = clip->volumeDB + clip->gainDB;
                                if (std::abs(audioClip->getGainDB() - combinedGain) > 0.001f)
                                    audioClip->setGainDB(combinedGain);
                            }
                            if (std::abs(audioClip->getPan() - clip->pan) > 0.001f)
                                audioClip->setPan(clip->pan);

                            if (audioClip->getLaunchFadeSamples() != clip->launchFadeSamples)
                                audioClip->setLaunchFadeSamples(clip->launchFadeSamples);

                            auto desiredMode = static_cast<te::TimeStretcher::Mode>(
                                audioEventRef(*clip).timeStretchMode);
                            if (!isAnalog && desiredMode == te::TimeStretcher::disabled &&
                                (audioEventRef(*clip).autoTempo ||
                                 audioEventRef(*clip).warpEnabled ||
                                 std::abs(audioEventRef(*clip).speedRatio - 1.0) > 0.001))
                                desiredMode = te::TimeStretcher::defaultMode;
                            if (isAnalog)
                                desiredMode = te::TimeStretcher::disabled;

                            const auto modeBefore =
                                stretchModeBefore.value_or(audioClip->getTimeStretchMode());
                            if (modeBefore != desiredMode ||
                                audioClip->getTimeStretchMode() != desiredMode) {
                                audioClip->setUsesProxy(false);
                                audioClip->setTimeStretchMode(desiredMode);
                                needsGraphReallocation = true;
                            }

                            needsGraphReallocation =
                                syncWarpStateToTracktionClip(*audioClip, *clip) ||
                                needsGraphReallocation;
                        }
                    }

                    // Re-sync MIDI notes from ClipManager to the TE MidiClip
                    if (clip->isMidi()) {
                        if (auto* midiClip = dynamic_cast<te::MidiClip*>(teClip)) {
                            auto& sequence = midiClip->getSequence();
                            sequence.clear(nullptr);

                            // For MIDI, use beat-authoritative clip length as boundary.
                            const double bpm = projectBpmAtClip(edit_, *clip);
                            double clipLengthBeats = timelineLengthBeats(*clip, bpm);
                            for (const auto& note : clip->midiNotes) {
                                double start = note.startBeat;
                                double length = note.lengthBeats;

                                // Skip or truncate notes at the clip boundary
                                if (clip->loopEnabled) {
                                    if (start >= clipLengthBeats)
                                        continue;
                                    double noteEnd = start + length;
                                    if (noteEnd > clipLengthBeats)
                                        length = clipLengthBeats - start;
                                }

                                sequence.addNote(
                                    note.noteNumber, te::BeatPosition::fromBeats(start),
                                    te::BeatDuration::fromBeats(length), note.velocity, 0, nullptr);
                            }
                        }
                    }

                }  // if (teClip)
            }      // else (already synced)
        }          // if (sceneIndex >= 0)
        return needsGraphReallocation;
    }

    return syncArrangementClipToEngine(clipId);
}

void ClipSynchronizer::clipSelectionChanged(ClipId clipId) {
    // Selection changed - we don't need to do anything here
    // The UI will handle this
    juce::ignoreUnused(clipId);
}

// =============================================================================
// Arrangement Clip Operations
// =============================================================================

void ClipSynchronizer::syncClipToEngine(ClipId clipId) {
    if (syncArrangementClipToEngine(clipId))
        reallocateAndNotify();
}

namespace {

/// Every arrangement clip sharing a lane, which is the unit occlusion works on.
std::vector<ClipInfo> arrangementClipsOnTrack(TrackId trackId) {
    auto& cm = ClipManager::getInstance();
    std::vector<ClipInfo> clips;
    for (ClipId id : cm.getClipsOnTrack(trackId, ClipView::Arrangement)) {
        if (const auto* clip = cm.getClip(id))
            clips.push_back(*clip);
    }
    return clips;
}

AudibleSpan spanFor(const std::unordered_map<ClipId, AudibleSpan>& spans, const ClipInfo& clip) {
    auto it = spans.find(clip.id);
    if (it != spans.end())
        return it->second;
    // No entry means nothing occluded it: play the whole clip.
    return AudibleSpan{clip.placement.startBeat, clip.placement.lengthBeats,
                       clip.placement.lengthBeats > 0.0};
}

bool sameSpan(const AudibleSpan& a, const AudibleSpan& b) {
    constexpr double tolBeats = 1e-6;
    if (a.audible != b.audible || std::abs(a.startBeat - b.startBeat) >= tolBeats ||
        std::abs(a.lengthBeats - b.lengthBeats) >= tolBeats) {
        return false;
    }
    // Holes count as much as the edges: a clip dropped into the middle of a
    // MIDI part leaves its span alone and changes only which notes play.
    if (a.silenced.size() != b.silenced.size())
        return false;
    for (size_t i = 0; i < a.silenced.size(); ++i) {
        if (std::abs(a.silenced[i].start.value - b.silenced[i].start.value) >= tolBeats ||
            std::abs(a.silenced[i].end.value - b.silenced[i].end.value) >= tolBeats) {
            return false;
        }
    }
    return true;
}

/// Notes a clip cannot play because another clip sits over them. Only plain
/// MIDI clips get here: the engine is handed their notes one by one, so a hole
/// costs nothing but leaving some out, and the clip itself stays whole and
/// editable. A looped clip repeats its content, so a hole in one iteration
/// cannot be expressed in the single note list TE gets — ClipManager splits
/// those instead, which for a looped clip is pure windowing and keeps the notes.
void dropNotesUnderCovers(ClipInfo& audible, const ClipInfo& clip, const AudibleSpan& span) {
    if (!clip.isMidi() || clip.loopEnabled || span.silenced.empty())
        return;

    // Note positions are content beats; the visible range says which content
    // beat the container starts at, so this maps a note onto the timeline the
    // same way the sync loop below does.
    const auto range = ClipOperations::getMidiVisibleRange(clip);
    const double contentOriginBeat = clip.placement.startBeat - range.startBeat;

    auto& notes = audible.midiNotes;
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const MidiNote& note) {
                                   const double timelineBeat = contentOriginBeat + note.startBeat;
                                   for (const auto& cover : span.silenced) {
                                       if (timelineBeat >= cover.start.value &&
                                           timelineBeat < cover.end.value)
                                           return true;
                                   }
                                   return false;
                               }),
                notes.end());
}

/// The clip as the engine should play it. Narrowing the container is the same
/// operation as dragging an edge in: the read offset follows, and the notes and
/// the source behind the covered part stay untouched — which is what lets the
/// model keep the clip whole while only its audible part reaches TE.
ClipInfo narrowToSpan(const ClipInfo& clip, const AudibleSpan& span, double bpm) {
    constexpr double tolBeats = 1e-6;
    ClipInfo audible = clip;
    if (!span.audible || !isValidBpm(bpm))
        return audible;

    // Before the container moves, while note positions still line up with the
    // placement the covers were computed against.
    dropNotesUnderCovers(audible, clip, span);

    const double clipEndBeat = clip.placement.startBeat + clip.placement.lengthBeats;
    if (span.startBeat > clip.placement.startBeat + tolBeats) {
        const double lengthFromLeft = (clipEndBeat - span.startBeat) * 60.0 / bpm;
        ClipOperations::resizeContainerFromLeft(audible, lengthFromLeft, bpm);
    }
    if (span.endBeat() < clipEndBeat - tolBeats) {
        ClipOperations::resizeContainerFromRight(audible, span.lengthBeats * 60.0 / bpm, bpm);
    }
    return audible;
}

}  // namespace

bool ClipSynchronizer::syncArrangementClipToEngine(ClipId clipId) {
    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("syncArrangementClipToEngine: Clip not found: " << clipId);
        return false;
    }

    // Only sync arrangement clips - session clips are managed by SessionClipScheduler
    if (clip->view == ClipView::Session) {
        return false;
    }

    const auto spans = computeAudibleSpans(arrangementClipsOnTrack(clip->trackId));
    return syncArrangementClipWithSpan(clipId, *clip, spanFor(spans, *clip));
}

bool ClipSynchronizer::syncArrangementClipWithSpan(ClipId clipId, const ClipInfo& clip,
                                                   const AudibleSpan& span) {
    const ClipInfo audible = narrowToSpan(clip, span, projectBpmAtClip(edit_, clip));

    // Route to appropriate sync method by type
    bool needsGraphReallocation = false;
    if (audible.isMidi()) {
        needsGraphReallocation = syncMidiClipToEngine(clipId, &audible);
    } else if (audible.isAudio()) {
        needsGraphReallocation = syncAudioClipToEngine(clipId, &audible);
    } else {
        DBG("syncClipToEngine: Unknown clip type for clip " << clipId);
        return false;
    }

    lastSyncedSpans_[clipId] = span;

    // Enabled toggle (#1736), plus clips covered end to end: a clip with no
    // audible span left keeps its place in the model and in the engine, it just
    // does not play, so uncovering it needs no restore step (#2003). TE skips
    // disabled clips when building the playback graph and restarts playback
    // itself on IDs::disabled changes, so no reallocation is needed here.
    // Guarded read-before-write: assigning an equal value to a CachedValue
    // still using its default would create the property and trigger a spurious
    // restart on first sync.
    if (auto* teClip = getArrangementTeClip(clipId)) {
        const bool wantDisabled = !clip.enabled || !span.audible;
        if (teClip->disabled.get() != wantDisabled)
            teClip->disabled = wantDisabled;
    }

    return needsGraphReallocation;
}

bool ClipSynchronizer::syncOcclusionChanges() {
    auto& cm = ClipManager::getInstance();
    const auto arrangementClips = cm.getArrangementClips();

    std::unordered_map<TrackId, std::vector<ClipInfo>> byTrack;
    for (const auto& clip : arrangementClips)
        byTrack[clip.trackId].push_back(clip);

    bool needsGraphReallocation = false;
    std::unordered_set<ClipId> live;
    live.reserve(arrangementClips.size());

    for (const auto& [trackId, clips] : byTrack) {
        const auto spans = computeAudibleSpans(clips);
        for (const auto& clip : clips) {
            live.insert(clip.id);
            const auto span = spanFor(spans, clip);
            auto known = lastSyncedSpans_.find(clip.id);
            if (known != lastSyncedSpans_.end()) {
                if (sameSpan(known->second, span))
                    continue;
            } else if (sameSpan(span, AudibleSpan{clip.placement.startBeat,
                                                  clip.placement.lengthBeats, span.audible})) {
                // Never seen and nothing covering it: the ordinary sync path
                // already puts this clip where it belongs, so record what it
                // plays and leave it alone rather than re-syncing every clip in
                // the edit on the first pass.
                lastSyncedSpans_[clip.id] = span;
                continue;
            }
            needsGraphReallocation =
                syncArrangementClipWithSpan(clip.id, clip, span) || needsGraphReallocation;
        }
    }

    for (auto it = lastSyncedSpans_.begin(); it != lastSyncedSpans_.end();)
        it = live.count(it->first) ? std::next(it) : lastSyncedSpans_.erase(it);

    return needsGraphReallocation;
}

void ClipSynchronizer::removeTeClipByEngineId(const std::string& engineId) {
    for (auto* track : tracktion::getAudioTracks(edit_)) {
        for (auto* clip : track->getClips()) {
            if (clip->itemID.toString().toStdString() == engineId) {
                clip->removeFromParent();
                return;
            }
        }

        for (auto* slot : track->getClipSlotList().getClipSlots()) {
            auto* slotClip = slot ? slot->getClip() : nullptr;
            if (slotClip && slotClip->itemID.toString().toStdString() == engineId) {
                slotClip->removeFromParent();
                return;
            }
        }
    }
}

void ClipSynchronizer::removeClipFromEngine(ClipId clipId) {
    // Remove clip from engine
    auto engineId = clipIds_.getEngineId(clipId);
    if (!engineId) {
        DBG("removeClipFromEngine: Clip not in engine: " << clipId);
        return;
    }

    removeTeClipByEngineId(*engineId);
    clipIds_.erase(clipId);
    DBG("removeClipFromEngine: Removed clip " << clipId);
}

te::Clip* ClipSynchronizer::getArrangementTeClip(ClipId clipId) const {
    auto engineId = clipIds_.getEngineId(clipId);
    if (!engineId)
        return nullptr;

    for (auto* track : te::getAudioTracks(edit_)) {
        for (auto* teClip : track->getClips()) {
            if (teClip->itemID.toString().toStdString() == *engineId)
                return teClip;
        }
    }
    return nullptr;
}

std::optional<std::string> ClipSynchronizer::getArrangementEngineId(ClipId clipId) const {
    return clipIds_.getEngineId(clipId);
}

// =============================================================================
// Session Clip Operations
// =============================================================================

bool ClipSynchronizer::syncSessionClipToSlot(ClipId clipId) {
    namespace te = tracktion;

    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Clip " << clipId
                                                             << " not found in ClipManager");
        return false;
    }
    if (clip->view != ClipView::Session || clip->sceneIndex < 0)
        return false;

    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Track " << clip->trackId
                                                              << " not found for clip " << clipId);
        return false;
    }

    // Ensure enough scenes (and slots on all tracks) exist
    edit_.getSceneList().ensureNumberOfScenes(clip->sceneIndex + 1);

    // Get the slot for this clip
    auto slots = audioTrack->getClipSlotList().getClipSlots();

    if (clip->sceneIndex >= static_cast<int>(slots.size())) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Slot index out of range for clip " << clipId);
        return false;
    }

    auto* slot = slots[clip->sceneIndex];
    if (!slot)
        return false;

    bool needsGraphReallocation = false;
    if (auto engineId = clipIds_.getEngineId(clipId)) {
        auto* mappedClip = getSessionTeClip(clipId);
        if (mappedClip) {
            if (mappedClip != slot->getClip()) {
                removeTeClipByEngineId(*engineId);
                clipIds_.erase(clipId);
                needsGraphReallocation = true;
            }
        } else {
            removeTeClipByEngineId(*engineId);
            clipIds_.erase(clipId);
            needsGraphReallocation = true;
        }
    }

    // If the source file changed under an existing audio slot clip (e.g. Save As
    // migrated temp project media), recreate it so TE follows ClipManager.
    if (auto* existingSlotClip = slot->getClip()) {
        if (clip->isAudio()) {
            juce::File desiredAudioFile(audioEventRef(*clip).sourceFilePath());
            if (desiredAudioFile.existsAsFile()) {
                if (auto* existingAudioClip = dynamic_cast<te::WaveAudioClip*>(existingSlotClip)) {
                    if (existingAudioClip->getOriginalFile() != desiredAudioFile) {
                        existingAudioClip->removeFromParent();
                        clipIds_.erase(clipId);
                        needsGraphReallocation = true;
                    } else {
                        return needsGraphReallocation;
                    }
                } else {
                    return needsGraphReallocation;
                }
            } else {
                return needsGraphReallocation;
            }
        } else {
            return needsGraphReallocation;
        }
    }

    // If slot still has a clip, skip (already synced)
    if (slot->getClip() != nullptr)
        return needsGraphReallocation;

    // Create the TE clip directly in the slot (NOT on the track then moved).
    // TE's free functions insertWaveClip(ClipOwner&, ...) and insertMIDIClip(ClipOwner&, ...)
    // accept ClipSlot as a ClipOwner, creating the clip's ValueTree directly in the slot.
    if (clip->isAudio()) {
        if (audioEventRef(*clip).sourceFilePath().isEmpty())
            return false;

        juce::File audioFile(audioEventRef(*clip).sourceFilePath());
        if (!audioFile.existsAsFile()) {
            DBG("ClipSynchronizer::syncSessionClipToSlot: Audio file not found: "
                << audioEventRef(*clip).sourceFilePath());
            return false;
        }

        // Create the clip in the slot. insertWaveClip only takes a time range,
        // so let the engine resolve the beat length to time; the length is then
        // anchored in beats below (TE owns the tempo sequence).
        auto& ts = edit_.tempoSequence;
        const double lengthBeats = clip->placement.lengthBeats;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                       ts.toTime(te::BeatPosition::fromBeats(lengthBeats)));

        auto clipRef = te::insertWaveClip(*slot, audioFile.getFileNameWithoutExtension(), audioFile,
                                          te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

        if (!clipRef)
            return false;

        auto* audioClipPtr = clipRef.get();
        audioClipPtr->setLength(te::BeatDuration::fromBeats(lengthBeats), false);
        clipIds_.set(clipId, audioClipPtr->itemID.toString().toStdString());

        // Enabled toggle (#1736): apply at creation so a disabled clip loaded
        // from a project never enters the playback graph enabled. Write only
        // when disabled — the property defaults to enabled and an explicit
        // equal write would trigger a spurious TE restart.
        if (!clip->enabled)
            audioClipPtr->disabled = true;

        // Populate source file metadata from TE's loopInfo. For a freshly
        // imported session clip, loopLengthBeats starts as a sentinel and is
        // initialised from the already-stored source loop seconds once the
        // source beat domain is known.
        {
            auto& loopInfoRef = audioClipPtr->getLoopInfo();
            auto waveInfo = audioClipPtr->getWaveInfo();
            if (auto* mutableClip = cm.getClip(clipId)) {
                bool sourceInterpretationBpmWasUnset = audioEventRef(*mutableClip).interpBpm <= 0.0;
                seedInterpretationFromLoopInfo(*mutableClip, loopInfoRef.getNumBeats(),
                                               loopInfoRef.getBpm(waveInfo));
                initialiseSourceLoopRegionFromMetadata(*mutableClip);
                if (sourceInterpretationBpmWasUnset && audioEventRef(*mutableClip).autoTempo) {
                    double projectBpm = projectBpmAtClip(edit_, *clip);
                    cm.refreshDerivedSeconds(clipId, projectBpm);
                    cm.forceNotifyClipPropertyChanged(clipId);
                }
            }
        }

        if (audioEventRef(*clip).autoTempo) {
            configureSessionAutoTempo(audioClipPtr, clip);
        } else {
            // =============================================================
            // TIME-BASED MODE — existing behavior
            // =============================================================

            // Set timestretcher mode — keep disabled when mode is 0 and speedRatio is 1.0
            {
                bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
                auto stretchMode =
                    static_cast<te::TimeStretcher::Mode>(audioEventRef(*clip).timeStretchMode);
                if (!isAnalog && stretchMode == te::TimeStretcher::disabled &&
                    (std::abs(audioEventRef(*clip).speedRatio - 1.0) > 0.001 ||
                     audioEventRef(*clip).warpEnabled))
                    stretchMode = te::TimeStretcher::defaultMode;
                if (isAnalog)
                    stretchMode = te::TimeStretcher::disabled;
                audioClipPtr->setTimeStretchMode(stretchMode);
            }

            // Set speed ratio (BEFORE offset, since TE offset
            // is in stretched time and must be set after speed ratio)
            if (std::abs(audioEventRef(*clip).speedRatio - 1.0) > 0.001) {
                if (audioClipPtr->getAutoTempo()) {
                    audioClipPtr->setAutoTempo(false);
                }
                audioClipPtr->setSpeedRatio(audioEventRef(*clip).speedRatio);
            }

            // Set file offset (trim point) - relative to loop start, in stretched time
            {
                double bpm = projectBpmAtClip(edit_, *clip);
                audioClipPtr->setOffset(te::TimeDuration::fromSeconds(
                    audioEventRef(*clip).engineOffsetSeconds(clip->loopEnabled)));
            }

            // Set looping properties
            double bpm = projectBpmAtClip(edit_, *clip);
            if (clip->loopEnabled &&
                audioEventRef(*clip).sourceLengthSeconds(clip->getTimelineLength(bpm)) > 0.0) {
                audioClipPtr->setLoopRange(te::TimeRange(
                    te::TimePosition::fromSeconds(audioEventRef(*clip).engineLoopStartSeconds()),
                    te::TimePosition::fromSeconds(
                        audioEventRef(*clip).engineLoopEndSeconds(clip->getTimelineLength(bpm)))));
            }

            // TE's ClipOwner auto-enables autoTempo on all session slot clips.
            // Neutralize embedded tempo metadata by setting source BPM = project
            // BPM so the stretch ratio is 1.0 and no unwanted speed change occurs.
            {
                double projectBpm = projectBpmAtClip(edit_, *clip);
                auto& li = audioClipPtr->getLoopInfo();
                auto waveInfo = audioClipPtr->getWaveInfo();
                li.setBpm(projectBpm, waveInfo);
            }
        }

        // Set per-clip launch quantization
        audioClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = audioClipPtr->getLaunchQuantisation()) {
            lq->type = clip_launch::toTracktionLaunchQType(clip->launchQuantize);
        }
        syncFollowActionToTracktionClip(*audioClipPtr, *clip, projectBpmAtClip(edit_, *clip));

        // Sync session-applicable audio properties at creation
        {
            bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
            if (!isAnalog && audioEventRef(*clip).autoPitch)
                audioClipPtr->setAutoPitch(true);
            if (isAnalog) {
                // Analog pitch: don't send pitchChange to TE (resampling handles it)
            } else if (std::abs(audioEventRef(*clip).pitchChange) > 0.001f) {
                audioClipPtr->setPitchChange(audioEventRef(*clip).pitchChange);
            }
        }
        if (audioEventRef(*clip).transpose != 0)
            audioClipPtr->setTranspose(audioEventRef(*clip).transpose);
        if (audioEventRef(*clip).reversed)
            audioClipPtr->setIsReversed(true);
        {
            float combinedGain = clip->volumeDB + clip->gainDB;
            if (std::abs(combinedGain) > 0.001f)
                audioClipPtr->setGainDB(combinedGain);
        }
        if (std::abs(clip->pan) > 0.001f)
            audioClipPtr->setPan(clip->pan);

        // No setFadeIn/setFadeOut here: te::EditNodeBuilder skips
        // FadeInOutNode for ClipRole::launcher, so any value written to
        // te::AudioClipBase::fadeIn/fadeOut never reaches the audio graph
        // for session clips. Per-clip launch shaping uses launchFadeSamples
        // (read by SlotControlNode).
        if (clip->launchFadeSamples != 256)
            audioClipPtr->setLaunchFadeSamples(clip->launchFadeSamples);

        // Set LaunchHandle looping state at creation time so it's ready before first launch
        if (auto lh = audioClipPtr->getLaunchHandle()) {
            if (clip->loopEnabled) {
                if (audioEventRef(*clip).autoTempo) {
                    double bpm = projectBpmAtClip(edit_, *clip);
                    auto [loopStartBeats, loopLengthBeats] =
                        ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));
                    if (loopLengthBeats > 0.0)
                        lh->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
                } else {
                    double bpm = projectBpmAtClip(edit_, *clip);
                    const double sourceLength =
                        audioEventRef(*clip).sourceLengthSeconds(clip->getTimelineLength(bpm));
                    if (sourceLength > 0.0) {
                        double loopDurationBeats =
                            (sourceLength / audioEventRef(*clip).speedRatio) * (bpm / 60.0);
                        lh->setLooping(te::BeatDuration::fromBeats(loopDurationBeats));
                    }
                }
            }
        }

        // Force WarpTimeManager creation now so its constructor's warp marker
        // insertions (which trigger TreeWatcher → restartPlayback) happen during
        // initial sync rather than lazily during playback (which causes a click),
        // then restore any marker map carried by a copied/project-loaded clip.
        audioClipPtr->getWarpTimeManager();
        syncWarpStateToTracktionClip(*audioClipPtr, *clip);

        return true;

    } else if (clip->isMidi()) {
        // Create the MIDI clip in the slot. insertMIDIClip only takes a time
        // range, so let the engine resolve the beat length to time; the length
        // is then anchored in beats below.
        auto& ts = edit_.tempoSequence;
        const double lengthBeats = clip->placement.lengthBeats;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                       ts.toTime(te::BeatPosition::fromBeats(lengthBeats)));

        auto clipRef = te::insertMIDIClip(*slot, timeRange);
        if (!clipRef)
            return false;

        auto* midiClipPtr = clipRef.get();
        midiClipPtr->setLength(te::BeatDuration::fromBeats(lengthBeats), false);
        clipIds_.set(clipId, midiClipPtr->itemID.toString().toStdString());

        // Enabled toggle (#1736) — same as the audio branch above.
        if (!clip->enabled)
            midiClipPtr->disabled = true;

        // Force offset to 0 — note shifting is handled manually below
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

        // Add MIDI notes (skip/truncate at loop boundary to prevent stuck notes)
        // Apply midiOffset: exclude notes before offset, shift remaining notes
        auto& sequence = midiClipPtr->getSequence();
        double bpm = projectBpmAtClip(edit_, *clip);
        // A MIDI clip's loop is already clip beats. Deriving it from an audio
        // event would read an empty region and loop the whole clip instead.
        double loopStartBeat = clip->loopStartBeats;
        double loopLengthBeats = clip->loopLengthBeats;
        double loopEndBeat = loopStartBeat + loopLengthBeats;
        double effectiveOffset = clip->midiOffset;

        for (const auto& note : clip->midiNotes) {
            double start = note.startBeat;
            double length = note.lengthBeats;

            // Apply loop boundary first (on original positions)
            if (clip->loopEnabled && loopLengthBeats > 0.0) {
                if (start >= loopEndBeat)
                    continue;
                double noteEnd = start + length;
                if (noteEnd > loopEndBeat)
                    length = loopEndBeat - start;
            }

            // Skip notes entirely before the offset
            if (start + length <= effectiveOffset)
                continue;

            // Shift note start by offset
            double shiftedStart = start - effectiveOffset;
            if (shiftedStart < 0.0) {
                length += shiftedStart;  // Trim the beginning
                shiftedStart = 0.0;
            }

            if (length > 0.0)
                sequence.addNote(note.noteNumber, te::BeatPosition::fromBeats(shiftedStart),
                                 te::BeatDuration::fromBeats(length), note.velocity, 0, nullptr);
        }

        // Set looping if enabled. A v1 project can still carry the 0
        // sentinel, which would otherwise ask TE to loop nothing.
        if (clip->loopEnabled) {
            const double rangeLengthBeats =
                loopLengthBeats > 0.0 ? loopLengthBeats : clip->getLengthInBeats(bpm);
            midiClipPtr->setLoopRangeBeats(
                {te::BeatPosition::fromBeats(loopStartBeat),
                 te::BeatPosition::fromBeats(loopStartBeat + rangeLengthBeats)});
        }

        // Set per-clip launch quantization
        midiClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = midiClipPtr->getLaunchQuantisation()) {
            lq->type = clip_launch::toTracktionLaunchQType(clip->launchQuantize);
        }
        syncFollowActionToTracktionClip(*midiClipPtr, *clip, bpm);

        // Set LaunchHandle looping state at creation time. Same whole-clip
        // fallback as the loop range above: a legacy clip carrying the 0
        // sentinel would otherwise get a loop range but a one-shot handle,
        // and stop after a single pass.
        if (auto lh = midiClipPtr->getLaunchHandle()) {
            if (clip->loopEnabled) {
                const double handleBeats =
                    loopLengthBeats > 0.0 ? loopLengthBeats : clip->getLengthInBeats(bpm);
                if (handleBeats > 0.0)
                    lh->setLooping(te::BeatDuration::fromBeats(handleBeats));
            }
        }

        return true;
    }

    return needsGraphReallocation;
}

void ClipSynchronizer::removeSessionClipFromSlot(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (teClip)
        teClip->removeFromParent();
    clipIds_.erase(clipId);
}

void ClipSynchronizer::launchSessionClip(ClipId clipId, bool forceImmediate) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        return;
    }

    // Update LaunchHandle looping state before play.
    // NOTE: Do NOT call teClip->setLoopRange() / setLoopRangeBeats() here!
    // Those modify clip ValueTree properties (loopStartBeats, loopLengthBeats,
    // autoTempo) which TE's TreeWatcher detects and calls restartPlayback(),
    // triggering a graph rebuild mid-playback that causes an audible click.
    // The clip's loop range is already set during syncSessionClipToSlot() and
    // kept up-to-date by clipPropertyChanged().
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        if (clip->loopEnabled) {
            double bpm = projectBpmAtClip(edit_, *clip);
            double srcLength =
                audioEventRef(*clip).sourceLengthSeconds(clip->getTimelineLength(bpm));
            if (clip->isAudio() && audioEventRef(*clip).autoTempo) {
                auto [loopStartBeats, loopLengthBeats] =
                    ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));
                if (loopLengthBeats > 0.0) {
                    launchHandle->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
                }
            } else if (clip->isAudio() && srcLength > 0.0) {
                double loopDurationBeats =
                    (srcLength / audioEventRef(*clip).speedRatio) * (bpm / 60.0);
                launchHandle->setLooping(te::BeatDuration::fromBeats(loopDurationBeats));
            } else if (clip->isMidi()) {
                // Already clip beats, with the whole-clip fallback for the 0
                // sentinel a legacy project can still carry.
                const double handleBeats = clip->loopLengthBeats > 0.0
                                               ? clip->loopLengthBeats
                                               : clip->getLengthInBeats(bpm);
                if (handleBeats > 0.0)
                    launchHandle->setLooping(te::BeatDuration::fromBeats(handleBeats));
            }
        } else {
            launchHandle->setLooping(std::nullopt);
        }
    }

    // Track playback mode is managed by SessionClipScheduler::syncTrackPlaybackModes()
    // which runs before this method is called.

    auto qType = (clip && !forceImmediate)
                     ? clip_launch::toTracktionLaunchQType(clip->launchQuantize)
                     : te::LaunchQType::none;

    // Override the TE slot's own launch quantize to match our intent.
    // Without this, play(std::nullopt) uses the slot's stored quantize
    // (e.g. OneBar), causing a delay even when we want immediate launch.
    if (auto* lq = teClip->getLaunchQuantisation()) {
        lq->type = qType;
    }

    // Calculate the target beat (nullopt = immediate).
    auto targetBeat = (qType != te::LaunchQType::none)
                          ? clip_launch::computeQuantizedBeat(edit_, clip->launchQuantize)
                          : std::optional<te::MonotonicBeat>{};

    // Store the precise quantized launch time for SessionRecorder
    if (targetBeat && clip) {
        if (auto quantizedTime = clip_launch::toEditTimeSeconds(edit_, *targetBeat))
            lastLaunchTimeByTrack_[clip->trackId] = *quantizedTime;
    }

    // Stop other clips on the same track:
    // - Playing clips: stop at the SAME target beat (no gap)
    // - Queued clips: cancel immediately (stop with nullopt)
    if (clip) {
        auto& cm = ClipManager::getInstance();
        for (const auto& otherClip : cm.getSessionClips()) {
            if (otherClip.trackId == clip->trackId && otherClip.id != clipId) {
                auto* otherTeClip = getSessionTeClip(otherClip.id);
                if (!otherTeClip)
                    continue;
                auto otherLH = otherTeClip->getLaunchHandle();
                if (!otherLH)
                    continue;
                auto otherPlayState = otherLH->getPlayingStatus();
                auto otherQueuedState = otherLH->getQueuedStatus();
                if (otherPlayState == te::LaunchHandle::PlayState::playing) {
                    otherLH->stop(targetBeat ? *targetBeat : std::optional<te::MonotonicBeat>{});
                } else if (otherQueuedState &&
                           *otherQueuedState == te::LaunchHandle::QueueState::playQueued) {
                    otherLH->stop(std::nullopt);
                }
            }
        }
    }

    if (!targetBeat) {
        // For immediate launches, use transport position as fallback
        if (clip) {
            lastLaunchTimeByTrack_[clip->trackId] = edit_.getTransport().position.get().inSeconds();
        }
        DBG("ClipSync: play(nullopt) — immediate launch for clip "
            << clipId << " forceImmediate=" << (int)forceImmediate);
        launchHandle->play(std::nullopt);
    } else {
        DBG("ClipSync: play(beat " << targetBeat->v.inBeats() << ") — quantized launch for clip "
                                   << clipId << " qType=" << static_cast<int>(qType));
        launchHandle->play(*targetBeat);
    }
}

double ClipSynchronizer::getLastLaunchTimeForTrack(TrackId trackId) const {
    auto it = lastLaunchTimeByTrack_.find(trackId);
    return (it != lastLaunchTimeByTrack_.end()) ? it->second : 0.0;
}

void ClipSynchronizer::stopSessionClipQueued(ClipId clipId, LaunchQuantize quantize) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip)
        return;

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle)
        return;

    auto targetBeat = clip_launch::computeQuantizedBeat(edit_, quantize);
    launchHandle->stop(targetBeat ? *targetBeat : std::optional<te::MonotonicBeat>{});

    // Reset synth plugins to prevent stuck MIDI notes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    if (clip->isMidi()) {
        auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
        if (audioTrack) {
            for (auto* plugin : audioTrack->pluginList) {
                if (plugin->isSynth())
                    plugin->reset();
            }
        }
    }
}

void ClipSynchronizer::stopSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        return;
    }

    launchHandle->stop(std::nullopt);

    // Reset synth plugins to prevent stuck MIDI notes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    if (clip->isMidi()) {
        auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
        if (audioTrack) {
            for (auto* plugin : audioTrack->pluginList) {
                if (plugin->isSynth()) {
                    plugin->reset();
                }
            }
        }
    }
    // Track playback mode is managed by SessionClipScheduler, not here.
}

te::Clip* ClipSynchronizer::getSessionTeClip(ClipId clipId) {
    auto engineId = clipIds_.getEngineId(clipId);
    if (!engineId)
        return nullptr;

    for (auto* track : tracktion::getAudioTracks(edit_)) {
        for (auto* slot : track->getClipSlotList().getClipSlots()) {
            auto* teClip = slot ? slot->getClip() : nullptr;
            if (teClip && teClip->itemID.toString().toStdString() == *engineId)
                return teClip;
        }
    }
    return nullptr;
}

// =============================================================================
// Session AutoTempo Helper
// =============================================================================

void ClipSynchronizer::configureSessionAutoTempo(te::WaveAudioClip* audioClip,
                                                 const ClipInfo* clip) {
    // Sync source interpretation to TE's loopInfo. AutoTempo playback uses both BPM and
    // source beat count to map source time to timeline beats.
    syncAudioSourceInterpretationToLoopInfo(*audioClip, *clip);

    // Auto-tempo requires stretching, but it must still honour the user's
    // explicit SoundTouch/Signalsmith selection. Fresh slot clips otherwise
    // retain TE's default mode, making the old modes sound identical to
    // Signalsmith until a later property edit happens to rebuild the graph.
    auto desiredMode = static_cast<te::TimeStretcher::Mode>(audioEventRef(*clip).timeStretchMode);
    if (desiredMode == te::TimeStretcher::disabled)
        desiredMode = te::TimeStretcher::defaultMode;

    if (audioClip->getTimeStretchMode() != desiredMode)
        audioClip->setTimeStretchMode(desiredMode);

    // Force speedRatio to 1.0 (TE requirement for autoTempo)
    if (std::abs(audioClip->getSpeedRatio() - 1.0) > 0.001)
        audioClip->setSpeedRatio(1.0);

    // Enable autoTempo
    if (!audioClip->getAutoTempo())
        audioClip->setAutoTempo(true);

    // Set offset — for autoTempo, convert source seconds to timeline seconds
    double bpmForOffset = projectBpmAtClip(edit_, *clip);
    audioClip->setOffset(te::TimeDuration::fromSeconds(
        audioEventRef(*clip).engineOffsetSeconds(clip->loopEnabled, bpmForOffset)));

    // Set beat-based loop range using the same helper as arrangement path
    if (clip->loopEnabled) {
        double bpm = projectBpmAtClip(edit_, *clip);
        auto [loopStartBeats, loopLengthBeats] =
            ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));
        if (loopLengthBeats > 0.0) {
            audioClip->setLoopRangeBeats(
                te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                              te::BeatDuration::fromBeats(loopLengthBeats)));
        }
    } else if (audioClip->isLooping()) {
        audioClip->disableLooping();
    }
}

// =============================================================================
// Warp Marker Operations
// =============================================================================

void ClipSynchronizer::setTransientSensitivity(ClipId clipId, float sensitivity) {
    warpSync_.setTransientSensitivity(clipId, sensitivity);
}

bool ClipSynchronizer::getTransientTimes(ClipId clipId) {
    return warpSync_.getTransientTimes(clipId);
}

void ClipSynchronizer::enableWarp(ClipId clipId) {
    warpSync_.enableWarp(clipId);
}

void ClipSynchronizer::disableWarp(ClipId clipId) {
    warpSync_.disableWarp(clipId);
}

std::vector<WarpMarkerInfo> ClipSynchronizer::getWarpMarkers(ClipId clipId) {
    return warpSync_.getWarpMarkers(clipId);
}

int ClipSynchronizer::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    return warpSync_.addWarpMarker(clipId, sourceTime, warpTime);
}

double ClipSynchronizer::moveWarpMarker(ClipId clipId, int index, double newWarpTime) {
    return warpSync_.moveWarpMarker(clipId, index, newWarpTime);
}

void ClipSynchronizer::removeWarpMarker(ClipId clipId, int index) {
    warpSync_.removeWarpMarker(clipId, index);
}

// =============================================================================
// CC/PitchBend Interpolation Helper
// =============================================================================

/**
 * @brief Generate interpolated CC/PB controller events between curve points.
 *
 * For Step curves, only the original event is emitted. For Linear (with tension)
 * and Bezier curves, intermediate events are generated every 1/64 beat to produce
 * smooth MIDI controller output instead of staircase steps.
 *
 * @tparam EventType  MidiCCData or MidiPitchBendData
 * @param sequence    The Tracktion MIDI sequence to add events to
 * @param events      Sorted events to interpolate between
 * @param controllerType  CC number or pitchWheelType
 * @param effectiveOffset Beat offset to subtract from positions
 * @param visibleStart    Start of visible range in beats
 * @param visibleEnd      End of visible range in beats
 * @param contentLengthBeats  Maximum beat position
 */
template <typename EventType>
static void interpolateCCEvents(te::MidiList& sequence, const std::vector<EventType>& events,
                                int controllerType, double effectiveOffset, double visibleStart,
                                double visibleEnd, double contentLengthBeats) {
    if (events.empty())
        return;

    // Make a sorted copy
    auto sorted = events;
    std::sort(sorted.begin(), sorted.end(), [](const EventType& a, const EventType& b) {
        return a.beatPosition < b.beatPosition;
    });

    // 1/16 beat is finer than any synth can audibly resolve (~32 Hz at 120 BPM)
    // and emits 4x fewer events than the previous 1/64. Density above this was
    // tipping fragile synths (e.g. Wave Manuel) into deadlock at clip starts /
    // loop wraps, where catch-up controller events collapse into one buffer.
    constexpr double kStepSize = 1.0 / 16.0;

    // Tracktion Engine stores all controller values in 14-bit range (0-16383).
    // CC values (0-127) must be left-shifted by 7 bits; pitch bend is already 14-bit.
    const bool isPitchBend = (controllerType == te::MidiControllerEvent::pitchWheelType);
    const int maxValue = isPitchBend ? 16383 : 127;

    auto addEvent = [&](double beatPos, int value) {
        double adjusted = beatPos - effectiveOffset;
        if (adjusted >= 0.0 && adjusted < contentLengthBeats) {
            int teValue = isPitchBend ? value : (value << 7);
            sequence.addControllerEvent(te::BeatPosition::fromBeats(adjusted), controllerType,
                                        teValue, nullptr);
        }
    };

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& ev = sorted[i];

        // Skip events outside visible range
        if (ev.beatPosition < visibleStart || ev.beatPosition >= visibleEnd)
            continue;

        if (ev.curveType == MidiCurveType::Step || i == sorted.size() - 1) {
            // Step: just emit the single event value (held until next event)
            // Also emit last event as-is since there's no next point to interpolate toward
            addEvent(ev.beatPosition, ev.value);
            continue;
        }

        // We have a next event to interpolate toward
        const auto& next = sorted[i + 1];
        double beatStart = ev.beatPosition;
        double beatEnd = next.beatPosition;
        double span = beatEnd - beatStart;

        if (span <= 0.0) {
            addEvent(ev.beatPosition, ev.value);
            continue;
        }

        double v1 = static_cast<double>(ev.value);
        double v2 = static_cast<double>(next.value);

        // Skip dense interpolation across constant-value segments: emitting
        // dozens of identical pitch-wheel/CC events every beat is pure waste
        // and can deadlock fragile synths when bursts collapse into one buffer.
        if (static_cast<int>(std::round(v1)) == static_cast<int>(std::round(v2))) {
            addEvent(ev.beatPosition, ev.value);
            continue;
        }

        if (ev.curveType == MidiCurveType::Linear) {
            // Generate interpolated events every kStepSize beats
            double tension = ev.tension;
            for (double beat = beatStart; beat < beatEnd; beat += kStepSize) {
                if (beat < visibleStart || beat >= visibleEnd)
                    continue;

                double t = (beat - beatStart) / span;

                // Apply tension (same formula as CurveSnapshot::evaluate / CurveEditorBase)
                double curvedT;
                if (std::abs(tension) < 0.001) {
                    curvedT = t;
                } else if (tension > 0) {
                    curvedT = std::pow(t, 1.0 + tension * 2.0);
                } else {
                    curvedT = 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
                }

                int val =
                    std::clamp(static_cast<int>(std::round(v1 + curvedT * (v2 - v1))), 0, maxValue);
                addEvent(beat, val);
            }
        } else if (ev.curveType == MidiCurveType::Bezier) {
            // Cubic bezier interpolation using in/out handles
            // Control points in normalized (beat, value) space:
            //   P0 = (beatStart, v1)
            //   P1 = (beatStart + outHandle.dx, v1 + outHandle.dy * valueRange)
            //   P2 = (beatEnd + inHandle.dx, v2 + inHandle.dy * valueRange)
            //   P3 = (beatEnd, v2)
            double p0x = beatStart, p0y = v1;
            double p1x = beatStart + ev.outHandle.dx;
            double p1y = v1 + ev.outHandle.dy * (v2 - v1);
            double p2x = beatEnd + next.inHandle.dx;
            double p2y = v2 + next.inHandle.dy * (v2 - v1);
            double p3x = beatEnd, p3y = v2;

            juce::ignoreUnused(p0x, p1x, p2x, p3x);

            for (double beat = beatStart; beat < beatEnd; beat += kStepSize) {
                if (beat < visibleStart || beat >= visibleEnd)
                    continue;

                double t = (beat - beatStart) / span;

                // Cubic bezier: B(t) = (1-t)^3*P0 + 3(1-t)^2*t*P1 + 3(1-t)*t^2*P2 + t^3*P3
                double u = 1.0 - t;
                double val = u * u * u * p0y + 3.0 * u * u * t * p1y + 3.0 * u * t * t * p2y +
                             t * t * t * p3y;

                addEvent(beat, std::clamp(static_cast<int>(std::round(val)), 0, maxValue));
            }
        }
    }
}

// Add per-note pitch glide (MPE pitch expression) children to a TE note.
//
// MAGDA stores sparse editable points (linear segments); TE emits one raw
// pitchbend message per expression child with no interpolation, so segments
// are densified here at the same 1/16-beat granularity as interpolateCCEvents.
//
// clipShift accounts for the note having been clipped at the visible-range
// left edge: expression beats are relative to the original note start, the
// TE note starts at the clipped position.
static void addPitchExpressionToTeNote(te::MidiNote& teNote, const MidiNote& note, double clipShift,
                                       double visibleLengthBeats) {
    constexpr double kStepSize = 1.0 / 16.0;
    constexpr float kMaxSemitones = 48.0f;  // TE's fixed MPE pitchbend conversion range

    auto sorted = note.pitchExpression;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.beat < b.beat; });

    // Evaluate the curve at a beat position (relative to original note start):
    // hold first value before the first point, linear between points, hold last.
    auto valueAt = [&sorted](double beat) {
        if (beat <= sorted.front().beat)
            return sorted.front().semitones;
        if (beat >= sorted.back().beat)
            return sorted.back().semitones;
        for (size_t i = 0; i + 1 < sorted.size(); ++i) {
            const auto& a = sorted[i];
            const auto& b = sorted[i + 1];
            if (beat >= a.beat && beat <= b.beat) {
                const double span = b.beat - a.beat;
                if (span <= 0.0)
                    return b.semitones;
                const double t = (beat - a.beat) / span;
                return a.semitones + t * (b.semitones - a.semitones);
            }
        }
        return sorted.back().semitones;
    };

    auto addExpressionEvent = [&teNote](double relBeat, double semitones) {
        auto value = juce::jlimit(-kMaxSemitones, kMaxSemitones, static_cast<float>(semitones));
        te::MidiExpression::createAndAddExpressionToNote(
            teNote.state, te::IDs::PITCHBEND, te::BeatPosition::fromBeats(relBeat), value, nullptr);
    };

    // Initial value at note-on so playback starts on the curve
    addExpressionEvent(0.0, valueAt(clipShift));

    for (size_t i = 0; i < sorted.size(); ++i) {
        const double pointBeat = sorted[i].beat - clipShift;
        if (pointBeat <= 0.0 || pointBeat > visibleLengthBeats)
            continue;

        // Densify the segment leading into this point when the value changes
        const double prevBeat = std::max(0.0, (i > 0 ? sorted[i - 1].beat : 0.0) - clipShift);
        const double v1 = valueAt(prevBeat + clipShift);
        const double v2 = sorted[i].semitones;

        if (std::abs(v2 - v1) > 0.001) {
            for (double b = prevBeat + kStepSize; b < pointBeat; b += kStepSize)
                addExpressionEvent(b, valueAt(b + clipShift));
        }

        addExpressionEvent(pointBeat, v2);
    }
}

// =============================================================================
// Private Sync Helpers
// =============================================================================

bool ClipSynchronizer::syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip) {
    // Get the Tracktion AudioTrack for this MAGDA track
    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("syncClipToEngine: Tracktion track not found for MAGDA track: " << clip->trackId);
        return false;
    }

    namespace te = tracktion;
    te::MidiClip* midiClipPtr = nullptr;
    bool needsGraphReallocation = false;

    // Check if clip already exists in Tracktion Engine
    if (auto engineId = clipIds_.getEngineId(clipId)) {
        // Clip exists - find it and update

        // Find the MidiClip in the track
        for (auto* teClip : audioTrack->getClips()) {
            if (teClip->itemID.toString().toStdString() == *engineId) {
                midiClipPtr = dynamic_cast<te::MidiClip*>(teClip);
                break;
            }
        }

        // Clip not found on expected track — it may have moved.
        // Remove the old TE clip from whichever track still holds it.
        if (!midiClipPtr) {
            DBG("ClipSynchronizer: MIDI clip moved or stale, removing old TE clip " << clipId);
            removeTeClipByEngineId(*engineId);
            clipIds_.erase(clipId);
            needsGraphReallocation = true;
        }
    }

    // Create clip if it doesn't exist
    if (!midiClipPtr) {
        // Use beats-based positioning via TE's tempo sequence (always correct regardless of tempo)
        auto startPos =
            edit_.tempoSequence.beatsToTime(te::BeatPosition::fromBeats(clip->startBeats));
        auto endPos = edit_.tempoSequence.beatsToTime(
            te::BeatPosition::fromBeats(clip->startBeats + clip->lengthBeats));
        auto timeRange = te::TimeRange(startPos, endPos);

        auto clipRef = audioTrack->insertMIDIClip(timeRange, nullptr);
        if (!clipRef) {
            DBG("syncClipToEngine: Failed to create MIDI clip");
            return false;
        }

        midiClipPtr = clipRef.get();
        needsGraphReallocation = true;

        // Store clip ID mapping (use clip's EditItemID as string)
        std::string engineClipId = midiClipPtr->itemID.toString().toStdString();
        clipIds_.set(clipId, engineClipId);
    }

    // Update clip position/length using beats-based positioning via TE's tempo sequence
    // This ensures correct positioning regardless of when TE's tempo is updated
    {
        auto startPos =
            edit_.tempoSequence.beatsToTime(te::BeatPosition::fromBeats(clip->startBeats));
        auto endPos = edit_.tempoSequence.beatsToTime(
            te::BeatPosition::fromBeats(clip->startBeats + clip->lengthBeats));
        midiClipPtr->setStart(startPos, true, false);
        midiClipPtr->setEnd(endPos, false);
    }

    // Set up internal looping on the TE clip
    if (clip->loopEnabled && clip->loopLengthBeats > 0.0) {
        // Use the stored loop region length, not the clip container length.
        // This is a MIDI clip: its loop is clip beats on the clip itself.
        double loopBeats = clip->loopLengthBeats;
        auto& tempoSeq = edit_.tempoSequence;
        auto loopStartTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(0.0));
        auto loopEndTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(loopBeats));

        midiClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        midiClipPtr->setLoopRangeBeats(
            {te::BeatPosition::fromBeats(0.0), te::BeatPosition::fromBeats(loopBeats)});

        // Set TE offset from midiOffset (beats) so playback starts at the phase position
        double bpm = projectBpmAtClip(edit_, *clip);
        double phaseSeconds = clip->midiOffset * (60.0 / bpm);
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(phaseSeconds));
    } else {
        midiClipPtr->disableLooping();
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));
    }

    // Groove/Shuffle/Swing — only touch TE when values differ to avoid
    // unnecessary clearCachedLoopSequence()/changed() side-effects
    if (midiClipPtr->getGrooveTemplate() != clip->grooveTemplate)
        midiClipPtr->setGrooveTemplate(clip->grooveTemplate);
    if (midiClipPtr->getGrooveStrength() != clip->grooveStrength)
        midiClipPtr->setGrooveStrength(clip->grooveStrength);

    // Clear existing notes and rebuild from ClipManager
    auto& sequence = midiClipPtr->getSequence();
    sequence.clear(nullptr);

    const auto visibleRange = ClipOperations::getMidiVisibleRange(*clip);
    const double contentLengthBeats = visibleRange.lengthBeats;
    const double effectiveOffset = visibleRange.startBeat;
    const double visibleStart = visibleRange.startBeat;
    const double visibleEnd = visibleRange.endBeat();

    // Add notes to TE sequence — notes stay at original positions,
    // TE offset + looping handles phase wrapping natively
    bool anyPitchExpression = false;
    for (const auto& note : clip->midiNotes) {
        auto visibleNote = note;
        if (!ClipOperations::clipMidiNoteToVisibleRange(*clip, visibleNote))
            continue;

        const double adjustedStart = visibleNote.startBeat - effectiveOffset;
        if (visibleNote.lengthBeats > 0.0 && adjustedStart < contentLengthBeats) {
            auto* teNote = sequence.addNote(
                note.noteNumber, te::BeatPosition::fromBeats(adjustedStart),
                te::BeatDuration::fromBeats(visibleNote.lengthBeats), note.velocity, 0, nullptr);

            if (teNote != nullptr && note.hasPitchExpression()) {
                anyPitchExpression = true;
                addPitchExpressionToTeNote(*teNote, note, visibleNote.startBeat - note.startBeat,
                                           visibleNote.lengthBeats);
            }
        }
    }

    // Per-note pitch glides require MPE playback (each note on its own channel)
    if (midiClipPtr->getMPEMode() != anyPitchExpression)
        midiClipPtr->setMPEMode(anyPitchExpression);

    // Add CC events with interpolation (grouped by controller number)
    {
        std::map<int, std::vector<MidiCCData>> ccByController;
        for (const auto& cc : clip->midiCCData)
            ccByController[cc.controller].push_back(cc);

        for (const auto& [ccNum, ccEvents] : ccByController) {
            interpolateCCEvents(sequence, ccEvents, ccNum, effectiveOffset, visibleStart,
                                visibleEnd, contentLengthBeats);
        }
    }

    // Add pitch bend events with interpolation. Skip entirely when every
    // event in the clip sits at the wheel-rest value (8192) — emitting a
    // stream of "no-op" pitch wheels is pointless and triggers a deadlock
    // in fragile synths (see #1193). Real curves that return to rest are
    // preserved because they contain at least one non-rest event.
    constexpr int kPitchWheelRest = 8192;
    const bool allAtRest =
        !clip->midiPitchBendData.empty() &&
        std::all_of(clip->midiPitchBendData.begin(), clip->midiPitchBendData.end(),
                    [](const auto& ev) { return ev.value == kPitchWheelRest; });

    if (!allAtRest) {
        interpolateCCEvents(sequence, clip->midiPitchBendData,
                            te::MidiControllerEvent::pitchWheelType, effectiveOffset, visibleStart,
                            visibleEnd, contentLengthBeats);
    }

    return needsGraphReallocation;
}

void ClipSynchronizer::applyModelTakesToTeClip(tracktion::WaveAudioClip& teClip,
                                               const ClipInfo& clip) {
    if (!clip.isAudio())
        return;

    const auto& takes = clip.audio().takes;
    if (takes.empty())
        return;

    // Already populated (e.g. a plain property re-sync on an existing clip).
    // Re-adding would duplicate the take list.
    if (teClip.hasAnyTakes())
        return;

    // Build the takes tree with absolute direct file references. We deliberately
    // do NOT use WaveAudioClip::addTake(File): that writes a relative path
    // (SourceFileReference::setToDirectFileReference(f, /*useRelativePath*/ true)),
    // which asserts in findPathFromFile when the edit has no on-disk edit file -
    // MAGDA never saves a .tracktionedit. Absolute references avoid the assert and
    // resolve reliably. The clip source already points at the active take, so
    // getCurrentTake resolves by matching source references (no setCurrentTake,
    // which assumes project-item takes these direct file references are not).
    namespace te = tracktion;
    auto takesTree = teClip.state.getOrCreateChildWithName(te::IDs::TAKES, nullptr);
    for (const auto& take : takes) {
        juce::ValueTree takeTree(te::IDs::TAKE);
        {
            te::SourceFileReference sfr(teClip.edit, takeTree, te::IDs::source);
            sfr.setToDirectFileReference(juce::File(take.filePath), /*useRelativePath*/ false);
        }
        takesTree.addChild(takeTree, -1, nullptr);
    }
}

bool ClipSynchronizer::syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip) {
    namespace te = tracktion;

    // 1. Get Tracktion track
    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("ClipSynchronizer: Track not found for audio clip " << clipId);
        return false;
    }

    // 2. Check if clip already synced
    te::WaveAudioClip* audioClipPtr = nullptr;
    bool needsGraphReallocation = false;
    bool createdAudioClip = false;
    if (auto engineId = clipIds_.getEngineId(clipId)) {
        // UPDATE existing clip
        bool removedForSourceChange = false;

        // Find clip in track by engine ID
        for (auto* teClip : audioTrack->getClips()) {
            if (teClip->itemID.toString().toStdString() == *engineId) {
                audioClipPtr = dynamic_cast<te::WaveAudioClip*>(teClip);
                break;
            }
        }

        // Source path changed under an existing model clip (e.g. Save As migrated temp media).
        // Recreate the TE clip so playback, warp, and thumbnails resolve the durable file.
        if (audioClipPtr) {
            juce::File desiredAudioFile(audioEventRef(*clip).sourceFilePath());
            if (desiredAudioFile.existsAsFile() &&
                audioClipPtr->getOriginalFile() != desiredAudioFile) {
                DBG("ClipSynchronizer: Audio source changed, recreating TE clip " << clipId);
                audioClipPtr->removeFromParent();
                clipIds_.erase(clipId);
                audioClipPtr = nullptr;
                removedForSourceChange = true;
                needsGraphReallocation = true;
            }
        }

        // Clip not found on expected track — it may have moved.
        // Remove the old TE clip from whichever track still holds it.
        if (!audioClipPtr && !removedForSourceChange) {
            DBG("ClipSynchronizer: Clip moved or stale, removing old TE clip " << clipId);
            removeTeClipByEngineId(*engineId);
            clipIds_.erase(clipId);
            needsGraphReallocation = true;
        }
    }

    // 3. CREATE new clip if doesn't exist
    if (!audioClipPtr) {
        if (audioEventRef(*clip).sourceFilePath().isEmpty()) {
            DBG("ClipSynchronizer: No audio file for clip " << clipId);
            return needsGraphReallocation;
        }
        juce::File audioFile(audioEventRef(*clip).sourceFilePath());
        if (!audioFile.existsAsFile()) {
            DBG("ClipSynchronizer: Audio file not found: "
                << audioEventRef(*clip).sourceFilePath());
            return needsGraphReallocation;
        }

        // Curve-aware: place the new clip by converting its beat range through
        // insertWaveClip only accepts a time ClipPosition, so let the engine's
        // tempo sequence resolve the beat range to time (curve-exact). Section 4
        // below re-anchors the clip in beats immediately after, so this is just
        // the initial placement.
        auto& ts = edit_.tempoSequence;
        auto timeRange =
            te::TimeRange(ts.toTime(te::BeatPosition::fromBeats(clip->placement.startBeat)),
                          ts.toTime(te::BeatPosition::fromBeats(clip->placement.endBeat())));

        auto clipRef =
            insertWaveClip(*audioTrack, audioFile.getFileNameWithoutExtension(), audioFile,
                           te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

        if (!clipRef) {
            DBG("ClipSynchronizer: Failed to create WaveAudioClip");
            return needsGraphReallocation;
        }

        audioClipPtr = clipRef.get();
        createdAudioClip = true;
        needsGraphReallocation = true;

        // Set timestretcher mode at creation time
        // When timeStretchMode is 0 (disabled), keep it disabled — TE's
        // getActualTimeStretchMode() will auto-upgrade to defaultMode when
        // autoPitch/autoTempo/pitchChange require it.
        // Force defaultMode when speedRatio != 1.0 or warp is enabled.
        // Analog pitch: force disabled mode (pure resampling via speedRatio).
        {
            bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
            auto stretchMode =
                static_cast<te::TimeStretcher::Mode>(audioEventRef(*clip).timeStretchMode);
            if (!isAnalog && stretchMode == te::TimeStretcher::disabled &&
                (std::abs(audioEventRef(*clip).speedRatio - 1.0) > 0.001 ||
                 audioEventRef(*clip).warpEnabled))
                stretchMode = te::TimeStretcher::defaultMode;
            if (isAnalog)
                stretchMode = te::TimeStretcher::disabled;
            audioClipPtr->setTimeStretchMode(stretchMode);
        }
        audioClipPtr->setUsesProxy(false);

        // Populate source file metadata from TE's loopInfo. See the matching
        // session-clip path above.
        {
            auto& loopInfoRef = audioClipPtr->getLoopInfo();
            auto waveInfo = audioClipPtr->getWaveInfo();
            auto& cm = ClipManager::getInstance();
            if (auto* mutableClip = cm.getClip(clipId)) {
                bool sourceInterpretationBpmWasUnset = audioEventRef(*mutableClip).interpBpm <= 0.0;
                seedInterpretationFromLoopInfo(*mutableClip, loopInfoRef.getNumBeats(),
                                               loopInfoRef.getBpm(waveInfo));
                initialiseSourceLoopRegionFromMetadata(*mutableClip);
                if (sourceInterpretationBpmWasUnset && audioEventRef(*mutableClip).autoTempo) {
                    double projectBpm = projectBpmAtClip(edit_, *clip);
                    cm.refreshDerivedSeconds(clipId, projectBpm);
                    cm.forceNotifyClipPropertyChanged(clipId);
                }
            }
        }

        // Store bidirectional mapping
        std::string engineClipId = audioClipPtr->itemID.toString().toStdString();
        clipIds_.set(clipId, engineClipId);

        DBG("ClipSynchronizer: Created WaveAudioClip (engine ID: " << engineClipId << ")");
    }

    // Re-attach loop-record takes (no-op for ordinary clips). Runs for both the
    // create path (record / project load) and the update path that follows a
    // create on a fresh recording.
    applyModelTakesToTeClip(*audioClipPtr, *clip);

    const bool useSourceBeatProcessing =
        audioEventRef(*clip).autoTempo || audioEventRef(*clip).warpEnabled;

    // A project load or graph recreation can create a TE clip whose model is
    // already reversed. Seed the new forward clip with MAGDA's canonical trim
    // coordinates before asking Tracktion to mirror them into proxy space.
    // Otherwise reverseLoopPoints() mirrors TE's default offset (zero), losing
    // the selected source slice until the user toggles reverse off and on again.
    if (createdAudioClip && audioEventRef(*clip).reversed) {
        const double bpm = projectBpmAtClip(edit_, *clip);

        audioClipPtr->setStart(te::BeatPosition::fromBeats(clip->placement.startBeat), false, true);
        audioClipPtr->setLength(te::BeatDuration::fromBeats(clip->placement.lengthBeats), false);

        if (useSourceBeatProcessing) {
            syncAudioSourceInterpretationToLoopInfo(*audioClipPtr, *clip);
            if (!audioClipPtr->getAutoTempo())
                audioClipPtr->setAutoTempo(true);
            if (std::abs(audioClipPtr->getSpeedRatio() - 1.0) > 0.001)
                audioClipPtr->setSpeedRatio(1.0);

            if (clip->loopEnabled) {
                auto [loopStartBeats, loopLengthBeats] =
                    ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));
                audioClipPtr->setLoopRangeBeats(
                    te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                                  te::BeatDuration::fromBeats(loopLengthBeats)));
            }
        } else {
            if (audioClipPtr->getAutoTempo())
                audioClipPtr->setAutoTempo(false);
            if (std::abs(audioClipPtr->getSpeedRatio() - audioEventRef(*clip).speedRatio) > 0.001)
                audioClipPtr->setSpeedRatio(audioEventRef(*clip).speedRatio);

            if (clip->loopEnabled &&
                audioEventRef(*clip).sourceLengthSeconds(clip->getTimelineLength(bpm)) > 0.0) {
                audioClipPtr->setLoopRange(te::TimeRange(
                    te::TimePosition::fromSeconds(audioEventRef(*clip).engineLoopStartSeconds()),
                    te::TimePosition::fromSeconds(
                        audioEventRef(*clip).engineLoopEndSeconds(clip->getTimelineLength(bpm)))));
            }
        }

        audioClipPtr->setOffset(te::TimeDuration::fromSeconds(
            audioEventRef(*clip).engineOffsetSeconds(clip->loopEnabled, bpm)));
    }

    // 3b. REVERSE — must be handled before position/loop/offset sync.
    // setIsReversed triggers updateReversedState() which:
    //   1. Points source to the original file
    //   2. Starts async render of reversed proxy (if reversing)
    //   3. Calls reverseLoopPoints() to transform offset/loop range
    //   4. Calls changed() which updates thumbnails
    // We MUST return after this — the subsequent sync steps would overwrite
    // TE's reversed offset/loop with our canonical original-source values.
    // The playback graph rebuild is deferred until the proxy file is ready.
    if (audioEventRef(*clip).reversed != audioClipPtr->getIsReversed()) {
        audioClipPtr->setIsReversed(audioEventRef(*clip).reversed);

        // Tracktion mirrors its offset/loop values into reversed-proxy coordinates.
        // Those are engine implementation details: ClipInfo remains in the original
        // source-file domain so editors, saves, undo, and later property changes keep
        // referring to the user's selected range.

        // Check if the reversed proxy file is ready
        auto playbackFile = audioClipPtr->getPlaybackFile();
        if (playbackFile.isValid()) {
            // Source-file changes are picked up by Tracktion on its next message-cycle
            // restart. Reallocating synchronously inside this property callback can build
            // a node while the source transition is still settling.
            edit_.restartPlayback();
            return needsGraphReallocation;
        } else {
            pendingReverseClipId_ = clipId;
        }

        // A newly-created reversed clip still needs to enter the playback graph now;
        // the reverse proxy timer will reallocate again when the proxy becomes playable.
        return needsGraphReallocation;  // Don't let subsequent sync steps overwrite TE's reversed
                                        // state
    }

    // 4. UPDATE clip position/length
    // Push the placement to TE in beats. The engine owns the tempo sequence and
    // resolves the seconds itself, so the clip stays anchored to its musical
    // position under a tempo ramp with no re-push (no beat->seconds round-trip
    // that would drift and cut playback short on a downward ramp).
    const double startBeat = clip->placement.startBeat;
    const double lengthBeats = clip->placement.lengthBeats;

    const double currentStartBeat = audioClipPtr->getStartBeats().inBeats();
    const double currentLengthBeats = audioClipPtr->getLengthBeats().inBeats();

    bool needsPositionUpdate = std::abs(currentStartBeat - startBeat) > 1.0e-6 ||
                               std::abs(currentLengthBeats - lengthBeats) > 1.0e-6;

    if (needsPositionUpdate) {
        // preserveSync=false keeps the content read offset; keepLength=true
        // moves the clip first, then setLength applies the exact beat length.
        audioClipPtr->setStart(te::BeatPosition::fromBeats(startBeat), false, true);
        audioClipPtr->setLength(te::BeatDuration::fromBeats(lengthBeats), false);
    }

    // 5. UPDATE speed ratio and auto-tempo mode
    // Timeline placement is always stored in project beats, but TE should only
    // use source-beat audio processing when MAGDA beat/warp mode requires it.
    // Apply engine changes in both beat-based and time-based processing modes.
    // The stretcher is captured in the playback graph, so changing this property
    // must explicitly request a graph rebuild.
    auto desiredMode = static_cast<te::TimeStretcher::Mode>(audioEventRef(*clip).timeStretchMode);
    const bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
    if (!isAnalog && desiredMode == te::TimeStretcher::disabled &&
        (useSourceBeatProcessing || std::abs(audioEventRef(*clip).speedRatio - 1.0) > 0.001))
        desiredMode = te::TimeStretcher::defaultMode;
    if (isAnalog)
        desiredMode = te::TimeStretcher::disabled;

    if (audioClipPtr->getTimeStretchMode() != desiredMode) {
        audioClipPtr->setUsesProxy(false);
        audioClipPtr->setTimeStretchMode(desiredMode);
        needsGraphReallocation = true;
    }

    if (useSourceBeatProcessing && !audioEventRef(*clip).reversed) {
        // ========================================================================
        // AUTO-TEMPO MODE (Beat-based length, maintains musical time)
        // Warp also uses this path — TE only passes warpMap to WaveNodeRealTime
        // via the auto-tempo code path in EditNodeBuilder.
        // ========================================================================
        // In auto-tempo mode:
        // - TE's autoTempo is enabled (clips stretch/shrink with BPM)
        // - speedRatio must be 1.0 (TE requirement)
        // - Use beat-based loop range (setLoopRangeBeats)

        // Enable auto-tempo in TE if not already enabled
        if (!audioClipPtr->getAutoTempo()) {
            audioClipPtr->setAutoTempo(true);
        }

        // Force speedRatio to 1.0 (auto-tempo requirement)
        if (std::abs(audioClipPtr->getSpeedRatio() - 1.0) > 0.001) {
            audioClipPtr->setSpeedRatio(1.0);
        }

    } else if (!audioEventRef(*clip).reversed) {
        // ========================================================================
        // TIME-BASED MODE (Fixed absolute time, current default behavior)
        // ========================================================================

        // Always disable autoTempo in TE when our model says it's off
        if (audioClipPtr->getAutoTempo()) {
            audioClipPtr->setAutoTempo(false);
        }

        double teSpeedRatio = audioEventRef(*clip).speedRatio;
        double currentSpeedRatio = audioClipPtr->getSpeedRatio();

        if (std::abs(currentSpeedRatio - teSpeedRatio) > 0.001) {
            audioClipPtr->setUsesProxy(false);
            audioClipPtr->setSpeedRatio(teSpeedRatio);
        }

        // Sync warp state to engine (time-based warp — rare, but handle it)
        if (audioEventRef(*clip).warpEnabled != audioClipPtr->getWarpTime()) {
            audioClipPtr->setWarpTime(audioEventRef(*clip).warpEnabled);
        }
    }

    // 5b. WARP — sync warp state and restore markers (applies to both code paths)
    if (audioEventRef(*clip).warpEnabled) {
        if (!audioClipPtr->getWarpTime()) {
            audioClipPtr->setWarpTime(true);
        }

        // Restore saved warp markers if TE has no user markers yet
        if (!audioEventRef(*clip).warpMarkers.empty()) {
            auto& warpManager = audioClipPtr->getWarpTimeManager();
            // TE creates 2 default boundary markers; if only those exist, restore saved
            if (restoreWarpMarkersIfNeeded(warpManager, audioEventRef(*clip).warpMarkers)) {
                DBG("ClipSynchronizer: Restored " << audioEventRef(*clip).warpMarkers.size()
                                                  << " warp markers for clip " << clipId);
            }
        }
    }

    // 6. UPDATE loop properties (BEFORE offset — setLoopRangeBeats can reset offset)
    // Use beat-based loop range in auto-tempo/warp mode, time-based otherwise.
    // Tracktion has already mirrored these values into its reverse-proxy domain;
    // canonical source coordinates must not overwrite them on an unrelated sync.
    if (!audioEventRef(*clip).reversed && useSourceBeatProcessing) {
        // Auto-tempo mode: ALWAYS set beat-based loop range
        // The loop range defines the clip's musical extent (not just the loop region)

        if (clip->loopEnabled) {
            // Get tempo for beat calculations (curve-aware, at the clip's beat).
            double bpm = projectBpmAtClip(edit_, *clip);

            // Override TE's loopInfo to match our calibrated source interpretation.
            // setAutoTempo calibrates source interpretation BPM = projectBPM / speedRatio so that
            // enabling autoTempo doesn't change playback speed. TE uses loopInfo to map source
            // beats to source time, so BPM and source beat count must both agree.
            if (audioEventRef(*clip).interpBpm > 0.0 ||
                audioEventRef(*clip).interpTotalBeats > 0.0) {
                syncAudioSourceInterpretationToLoopInfo(*audioClipPtr, *clip);
            }

            auto [loopStartBeats, loopLengthBeats] =
                ClipOperations::getAutoTempoBeatRange(audioEventRef(*clip));

            auto loopRange = te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                                           te::BeatDuration::fromBeats(loopLengthBeats));
            audioClipPtr->setLoopRangeBeats(loopRange);
        } else if (audioClipPtr->isLooping()) {
            audioClipPtr->setLoopRangeBeats({});
        }
    } else if (!audioEventRef(*clip).reversed) {
        // Time-based mode: Use time-based loop range
        // Only use setLoopRange (time-based), NOT setLoopRangeBeats which forces
        // autoTempo=true and speedRatio=1.0, breaking time-stretch.
        double bpm = projectBpmAtClip(edit_, *clip);
        if (clip->loopEnabled &&
            audioEventRef(*clip).sourceLengthSeconds(clip->getTimelineLength(bpm)) > 0.0) {
            auto loopStartTime =
                te::TimePosition::fromSeconds(audioEventRef(*clip).engineLoopStartSeconds());
            auto loopEndTime = te::TimePosition::fromSeconds(
                audioEventRef(*clip).engineLoopEndSeconds(clip->getTimelineLength(bpm)));
            audioClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        } else if (audioClipPtr->isLooping()) {
            audioClipPtr->setLoopRange({});
        }
    }

    // 7. UPDATE audio offset (trim point in file)
    // Must come AFTER loop range — setLoopRangeBeats resets offset internally
    if (!audioEventRef(*clip).reversed) {
        double projectBpm = projectBpmAtClip(edit_, *clip);
        double teOffset = juce::jmax(
            0.0, audioEventRef(*clip).engineOffsetSeconds(clip->loopEnabled, projectBpm));
        auto currentOffset = audioClipPtr->getPosition().getOffset().inSeconds();
        if (std::abs(currentOffset - teOffset) > 0.001) {
            audioClipPtr->setOffset(te::TimeDuration::fromSeconds(teOffset));
        }
    }

    // 8. PITCH
    {
        bool isAnalog = audioEventRef(*clip).isAnalogPitchActive();
        if (audioEventRef(*clip).autoPitch != audioClipPtr->getAutoPitch())
            audioClipPtr->setAutoPitch(isAnalog ? false : audioEventRef(*clip).autoPitch);
        if (static_cast<int>(audioClipPtr->getAutoPitchMode()) !=
            audioEventRef(*clip).autoPitchMode)
            audioClipPtr->setAutoPitchMode(
                static_cast<te::AudioClipBase::AutoPitchMode>(audioEventRef(*clip).autoPitchMode));
        if (isAnalog) {
            if (std::abs(audioClipPtr->getPitchChange()) > 0.001f)
                audioClipPtr->setPitchChange(0.0f);
        } else {
            if (std::abs(audioClipPtr->getPitchChange() - audioEventRef(*clip).pitchChange) >
                0.001f)
                audioClipPtr->setPitchChange(audioEventRef(*clip).pitchChange);
        }
        if (audioClipPtr->getTransposeSemiTones(false) != audioEventRef(*clip).transpose)
            audioClipPtr->setTranspose(audioEventRef(*clip).transpose);
    }

    // 9. BEAT DETECTION
    if (audioEventRef(*clip).autoDetectBeats != audioClipPtr->getAutoDetectBeats())
        audioClipPtr->setAutoDetectBeats(audioEventRef(*clip).autoDetectBeats);
    if (std::abs(audioClipPtr->getBeatSensitivity() - audioEventRef(*clip).beatSensitivity) >
        0.001f)
        audioClipPtr->setBeatSensitivity(audioEventRef(*clip).beatSensitivity);

    // 10. PLAYBACK (isReversed handled at top of function)

    // 11. PER-CLIP MIX
    {
        float combinedGain = clip->volumeDB + clip->gainDB;
        if (std::abs(audioClipPtr->getGainDB() - combinedGain) > 0.001f)
            audioClipPtr->setGainDB(combinedGain);
    }
    if (std::abs(audioClipPtr->getPan() - clip->pan) > 0.001f)
        audioClipPtr->setPan(clip->pan);

    // 12. FADES
    {
        double teFadeIn = audioClipPtr->getFadeIn().inSeconds();
        if (std::abs(teFadeIn - audioEventRef(*clip).fadeInSeconds) > 0.001)
            audioClipPtr->setFadeIn(
                te::TimeDuration::fromSeconds(audioEventRef(*clip).fadeInSeconds));
    }
    {
        double teFadeOut = audioClipPtr->getFadeOut().inSeconds();
        if (std::abs(teFadeOut - audioEventRef(*clip).fadeOutSeconds) > 0.001)
            audioClipPtr->setFadeOut(
                te::TimeDuration::fromSeconds(audioEventRef(*clip).fadeOutSeconds));
    }
    if (static_cast<int>(audioClipPtr->getFadeInType()) != audioEventRef(*clip).fadeInType)
        audioClipPtr->setFadeInType(
            static_cast<te::AudioFadeCurve::Type>(audioEventRef(*clip).fadeInType));
    if (static_cast<int>(audioClipPtr->getFadeOutType()) != audioEventRef(*clip).fadeOutType)
        audioClipPtr->setFadeOutType(
            static_cast<te::AudioFadeCurve::Type>(audioEventRef(*clip).fadeOutType));
    if (static_cast<int>(audioClipPtr->getFadeInBehaviour()) !=
        audioEventRef(*clip).fadeInBehaviour)
        audioClipPtr->setFadeInBehaviour(
            static_cast<te::AudioClipBase::FadeBehaviour>(audioEventRef(*clip).fadeInBehaviour));
    if (static_cast<int>(audioClipPtr->getFadeOutBehaviour()) !=
        audioEventRef(*clip).fadeOutBehaviour)
        audioClipPtr->setFadeOutBehaviour(
            static_cast<te::AudioClipBase::FadeBehaviour>(audioEventRef(*clip).fadeOutBehaviour));
    if (clip->autoCrossfade != audioClipPtr->getAutoCrossfade())
        audioClipPtr->setAutoCrossfade(clip->autoCrossfade);

    if (audioClipPtr->getLaunchFadeSamples() != clip->launchFadeSamples)
        audioClipPtr->setLaunchFadeSamples(clip->launchFadeSamples);

    // 13. CHANNELS — removed (L/R controls removed from Inspector)
    return needsGraphReallocation;
}

}  // namespace magda
