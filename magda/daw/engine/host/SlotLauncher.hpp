#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../core/ClipTypes.hpp"
#include "../../core/TypeIds.hpp"

namespace magda {
struct ClipInfo;
}

namespace magda::engine {
class EngineSession;
class LaunchTap;
class TempoMap;
}  // namespace magda::engine

/**
 * @file SlotLauncher.hpp
 * @brief What the session view asks of the launcher, and what it reads back (#2552).
 *
 * The launcher's engine half was finished by #1894 and had never had an app
 * driving it: EngineSession publishes a handle per slot, applies what the
 * request queue carries and writes each block's result to a tap. This is the
 * other end -- a click becomes a request at a resolved beat, and a tap becomes
 * the state a slot button draws.
 *
 * Quantization is here rather than in the engine, which is where it already
 * lived: a launch reaches the queue with the monotonic beat it is due on, and
 * the engine is told when rather than asked to work it out.
 *
 * Message thread throughout. The taps are read, never held: the publish that
 * stops naming a slot destroys its tap on this thread (EngineSession::launchTap).
 */

namespace magda::daw::engine_host {

/// What a launch needs of the host that owns the session. An interface rather
/// than the host itself, because the session is rebuilt whenever the device
/// changes and a launcher holding one would outlive it.
class LaunchHost {
  public:
    virtual ~LaunchHost() = default;

    /// The live session, or null before a device has built one.
    virtual engine::EngineSession* launchSession() = 0;

    /// The map the boundaries are counted on, which is the one being rendered.
    virtual const engine::TempoMap& launchTempo() const = 0;

    /// The signature's bar, in beats.
    virtual double launchBeatsPerBar() const = 0;

    virtual bool launchTransportPlaying() const = 0;

    /// Start the transport, since a launch made while stopped starts one.
    virtual void startLaunchTransport() = 0;
};

class SlotLauncher {
  public:
    explicit SlotLauncher(LaunchHost& host) : host_(host) {}

    // ===== What the model asks for =====

    /** @brief Launch @p clipId, at its own quantization. */
    void launch(ClipId clipId);

    /** @brief Stop @p clipId now, and give its track back to the arrangement. */
    void stop(ClipId clipId);

    /** @brief Every slot of @p sceneIndex across @p trackIds, on one beat. */
    void launchScene(const std::vector<TrackId>& trackIds, int sceneIndex);

    /** @brief Stop what @p trackId is playing, at the stopping clip's quantization. */
    void stopTrack(TrackId trackId);

    /** @brief Stop every slot and return every track to its arrangement. */
    void stopEverything();

    // ===== What the UI reads =====

    /** @brief What @p clipId's slot is doing, as the last block left it. */
    SessionClipPlayState playState(ClipId clipId) const;

    /** @brief Whether a quantized stop is in flight on @p trackId, which is
     *         what the empty-slot stop affordance blinks on. */
    bool stopPending(TrackId trackId) const;

    /** @brief Where @p clipId's run has got to inside its own material, in
     *         seconds, or -1.0 when it is not sounding. */
    double playheadSeconds(ClipId clipId) const;

    /** @brief The clip the session playhead follows, or INVALID_CLIP_ID. */
    ClipId playheadClip() const {
        return playheadClip_;
    }

    /** @brief Every sounding slot's playhead, in seconds. */
    std::unordered_map<ClipId, double> playheads() const;

    /**
     * @brief Turn what the taps say into the notifications the view repaints
     *        on, once a frame.
     *
     * Also where a run that ended on its own is noticed: the engine stops a
     * slot at a follow action or at the end of a one-shot without telling
     * anyone, so the model's idea of what a track is playing is settled here.
     */
    void processStateEvents();

    /** @brief Forget every slot, for a project closing under the launcher. */
    void forget();

  private:
    /// The beat @p clip's launch is due on, or nothing for as soon as possible.
    std::optional<double> dueBeat(const ClipInfo& clip) const;

    /// The tap for @p clip's slot, or null. Never stored: see the file comment.
    const engine::LaunchTap* tapFor(const ClipInfo& clip) const;

    /// Whether @p clip's slot has been published with a handle to launch, and
    /// says so once when it has not.
    bool hasHandle(const ClipInfo& clip) const;

    /// What one pass through @p clip is worth, and whether it repeats, for
    /// wrapping the elapsed beats a tap reports.
    struct Material {
        double passBeats = 0.0;
        bool looping = false;
    };
    static Material materialOf(const ClipInfo& clip);

    /// Stop everything sounding, for a transport that stopped: a slot resumed
    /// mid-phrase is not what the fork does, and not what a launcher means.
    void stopForTransport();

    /// Re-launch what the model still says each track is playing.
    void relaunchActive();

    /// Session where a track has an active clip, Arrangement everywhere else.
    static void syncPlaybackModes();

    LaunchHost& host_;

    /// What each slot was last seen doing, so a change can be notified once.
    std::unordered_map<ClipId, SessionClipPlayState> lastState_;

    /// The tap as it read when a slot was asked to play. Until it moves, the
    /// audio thread has not answered the click, and the slot is queued: the
    /// click's own notification reads the state, and a slot button blinks on
    /// it or never does (#2674). Cleared by any stop the launcher issues.
    struct Asked {
        bool playing = false;
        int queued = 0;
        bool holdsSection = false;
        double elapsedBeats = 0.0;
    };
    mutable std::unordered_map<ClipId, Asked> asked_;
    void noteAsked(const ClipInfo& clip);

    /// Tracks with a quantized stop in flight (@ref stopPending).
    std::unordered_set<TrackId> stopping_;

    /// The most recently launched clip, which is the one a clip editor draws.
    ClipId playheadClip_ = INVALID_CLIP_ID;

    /// Whether the transport was rolling last time round, for the edges.
    bool wasPlaying_ = false;
};

}  // namespace magda::daw::engine_host
