#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <string>
#include <vector>

#include "clip/ClipSnapshotFeed.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/RenderThreadPool.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/LiveInput.hpp"
#include "launch/SessionLauncher.hpp"
#include "transport/ClickGenerator.hpp"
#include "transport/TransportClock.hpp"

/**
 * @file EngineSession.hpp
 * @brief The live engine: one plan on the audio thread, swapped from another.
 *
 * A published plan is immutable, so a structural change compiles a new one
 * and swaps it in whole. This is where that swap happens, and where the old
 * plan is reclaimed.
 *
 * The rule the swap exists to keep: the audio thread never waits and never
 * destroys. Publishing blocks the publishing thread until the audio thread
 * finishes its current block, and everything the retired epoch owned is
 * destroyed there, on the publishing thread. Nothing on the callback
 * allocates, locks, or frees.
 *
 * Threading contract: one audio thread, one publishing thread. Everything
 * that is not process() runs on a single non-realtime thread and is not
 * synchronised against itself -- the swap primitive only keeps the audio
 * thread out of the publisher's way, not publishers out of each other's. Two
 * threads publishing at once is a race, not a slow path.
 */

namespace magda::engine {

/// Only ever held as a pointer here, and only ever used from the .cpp, so
/// the session does not hand its own includers a thread and a stream pool.
class ClipVoicePool;

class EngineSession {
  public:
    /**
     * @brief A session rendering on @p pool.
     *
     * Null means every block renders on the audio thread alone (the same
     * executor with one thread instead of many). A host passing a pool does
     * so once: the pool outlives every plan, and must outlive this session,
     * since retired epochs are what release it.
     *
     * @p voices provisions the readers a track's clips play through
     * (#2035); null for a session with no arrangement audio (an offline
     * render reading straight through its files, or a test that publishes
     * no clips). Passed here rather than fed separately because a snapshot
     * has to reach it and the audio thread together, and it needs to know
     * where the transport is.
     */
    explicit EngineSession(RuntimeStateFactory& factory, RenderThreadPool* pool = nullptr,
                           ClipVoicePool* voices = nullptr)
        : store_(factory), pool_(pool), voices_(voices) {}

    /// What came of a publish. `published` false means the plan was refused
    /// and the previous one is still playing; true with messages means it's
    /// live but something in it could not be honoured.
    struct Result {
        bool published = false;
        std::vector<std::string> messages;
    };

    /**
     * @brief Prepare a plan and make it the one the audio thread renders.
     *
     * On the publishing thread. A plan that fails to prepare is not
     * published; the previous one keeps playing.
     *
     * @p modelIds is what the model still holds -- not the same as what the
     * plan uses. Runtime state is kept for everything the model names and
     * destroyed only for what it has lost; retention only ever extends, so
     * a set that has drifted from the published plan costs a dormant object
     * its retirement, never the audio thread a pointer.
     *
     * @p values is what the new plan renders with until publishValues()
     * sends something newer, and is not optional: a plan swap invalidates
     * whatever was in flight, since those values were resolved against the
     * plan being replaced. Without a table of its own the new plan would
     * render at unity for a block or two, and a fader jumping from -20 dB to
     * 0 dB is louder than any click the swap could have made. Values for
     * another plan are refused rather than published, since the executor
     * would decline to apply them and render at unity with no explanation.
     */
    Result publish(std::shared_ptr<const RenderPlan> plan, const RenderContext& context,
                   const RuntimeStateIds& modelIds, PlanValues values);

    /**
     * @brief Newer values for the plan that is playing.
     *
     * On the publishing thread. Values change at mixer speed rather than
     * structural speed, so they travel on their own.
     *
     * The parameter table rides along (#2117) and can change shape without
     * the plan changing: linking a macro for the first time gives it a
     * parameter, and linking one more thing to an already-linked parameter
     * widens the room a block gathers contributions in. Neither shows up in
     * the fingerprint, and neither fits what the live epoch allocated.
     *
     * So a publish of that shape is escalated here into a structural one, on
     * the plan already playing -- a prepare and a swap, the cost of any
     * structural edit, which is what a link edit is: rare, human speed, not
     * something to make a block pay for. The alternative is a table nothing
     * can render, with a project's parameters quietly ceasing to follow the
     * model.
     *
     * Values belonging to another plan are refused rather than escalated,
     * since republishing the live plan with them would just refuse them a
     * second time, one layer further in.
     */
    Result publishValues(PlanValues values);

    /**
     * @brief Tempo, loop, metronome and where the transport has been asked
     *        to be, as one value.
     *
     * On the publishing thread. Travels on its own for the same reason
     * values do: a tempo edit, a loop drag and a locate all happen while the
     * plan they apply to keeps playing.
     *
     * Nothing is published by default, so a session that has never seen one
     * renders stopped at the beginning at 120 bpm rather than refusing to run.
     */
    void publishTransport(TransportSnapshot transport);

    /**
     * @brief What every track plays, resolved (#2034).
     *
     * On the publishing thread, on its own like values and the transport:
     * moving a clip is not a topology change and must not compile a plan.
     * The clip snapshot it replaces is destroyed here, on this thread.
     *
     * Its seconds were derived through a tempo map, so a tempo edit
     * publishes both -- a snapshot compiled against the previous map would
     * place every clip at the seconds that map gave it.
     */
    void publishClips(std::shared_ptr<const ClipSnapshot> clips);

    /// The feed clip sources read. Handed to a source when created, and
    /// owned here because it outlives every plan those sources are bound
    /// into: a structural recompile must not cost a track the clips it was
    /// playing.
    ClipSnapshotFeed& clipFeed() {
        return clips_;
    }

    /// The feed session sources read their handles from (#2301). Owned here
    /// for the same reason as the clip feed: a structural recompile must not
    /// stop a clip that is sounding. Published by publishClips().
    LaunchHandleFeed& launchHandleFeed() {
        return handles_;
    }

    /**
     * @brief Where a launch is asked for, from off the audio thread (#2305).
     *
     * Ask through a `LaunchRequestQueue::Gesture`: everything inside its scope
     * reaches the audio thread together, so a scene launches on one sample.
     * A request names a slot, so asking for one that has gone does nothing.
     */
    LaunchRequestQueue& launchRequests() {
        return requests_;
    }

    /**
     * @brief Where @p key's state is published for the UI, or null (#2303).
     *
     * Both the lookup and the pointer belong to the publishing thread, exactly
     * as valueTap() does: the next publishClips() that stops naming the slot
     * destroys the tap on that thread, with nothing deferring it, so a pointer
     * cached and read from a paint or animation thread is a use-after-free
     * waiting for a clip edit. Fetch it here, use it before returning to the
     * message loop, and ask again after every publish.
     */
    const LaunchTap* launchTap(const SlotKey& key) const {
        return store_.launchTap(key);
    }

    /**
     * @brief The handle for @p key, or null. Tests and diagnostics only.
     *
     * State belongs to @ref launchTap and changes to @ref launchRequests; a
     * handle is advanced on the audio thread, so reading one here races the
     * callback.
     */
    const LaunchHandle* launchHandle(const SlotKey& key) const {
        return store_.findHandle(key);
    }

    /**
     * @brief Render @p numSamples. On the audio thread.
     *
     * The transport decides what stretch of timeline that is -- the caller
     * only says how many samples the device wants. A callback that a loop
     * wraps inside renders as two blocks back to back, and neither straddles
     * the wrap.
     *
     * @p input is what the device just captured (#2459), read by the live
     * sources an armed or monitoring track compiles. Default for a host with
     * no inputs and for every offline render, where the ops are not compiled
     * at all.
     *
     * Renders silence until a plan is published, and the cursor stands
     * still while it does: the sample rate is only settled when a plan is
     * prepared, and a clock with no idea how long a sample lasts would be
     * inventing a position, not reporting one.
     */
    void process(int numSamples, juce::AudioBuffer<float>& output,
                 const LiveInputBlock& input = {});

    /**
     * @brief The feed a live source reads its block from.
     *
     * The host builds LiveAudioInput and LiveMidiInput against this and binds
     * them through RuntimeStateFactory::createAudioInput / createMidiInput;
     * process() narrows it to each block as it goes. One per session rather
     * than one per source, since every input op reads the same callback.
     *
     * Prepare it when the audio device opens, with the channels and block size
     * that device delivers: the feed copies each callback's input, and the room
     * for that copy cannot be taken while a callback is running. An unprepared
     * feed reads silence and counts what it could not hold.
     */
    LiveInputFeed& liveInputs() {
        return liveInputs_;
    }

    /// Where the transport is, in beats. Readable from any thread; what a
    /// playhead is drawn from.
    double positionBeats() const {
        return clock_.positionBeats();
    }

    /// Callbacks in which a loop was too short to render as separate blocks.
    int loopWrapOverflows() const {
        return clock_.loopWrapOverflows();
    }

    /// Runtime objects the store owns right now. On the publishing thread.
    std::size_t runtimeObjectCount() const {
        return store_.size();
    }

    /**
     * @brief Where @p key's value is published, or nullptr (#2122).
     *
     * What a host collects after asking for a value through
     * RuntimeStateFactory::valuesToTap(). What it hands back is where a knob
     * under modulation, a modifier editor, and a touch or latch gesture all
     * draw from.
     *
     * Both the lookup and the pointer belong to the publishing thread as a
     * lifetime rule, not a convention: the next publish can destroy the tap
     * outright, on that thread, with nothing deferring reclamation. A cached
     * pointer read from a paint or animation thread is a use-after-free
     * waiting for an edit -- fetch it here, use it before returning to the
     * message loop, and ask again after every publish.
     *
     * See RuntimeStateStore::valueTap() for what null means, which is not
     * the same as the tap being unbound.
     */
    ValueTap* valueTap(const ParamKey& key) const {
        return store_.valueTap(key);
    }

    /// Values the live plan found somewhere to publish from -- not the
    /// number of taps the store holds, since a key the parameter table does
    /// not carry has a tap that is never written through. On the publishing
    /// thread.
    int boundValueTapCount() const {
        return live_ == nullptr ? 0 : live_->executor.boundValueTapCount();
    }

    /// The plan currently published, or null. On the publishing thread.
    std::shared_ptr<const RenderPlan> livePlan() const {
        return livePlan_;
    }

    /**
     * @brief Fades of the live plan that have not finished, by its OpIds.
     *
     * On the publishing thread; what insertCrossfades needs before the next
     * plan is compiled: a fade whose edge has arrived is re-emitted while
     * still running and dropped once it is not. Empty when nothing is
     * published.
     */
    std::vector<char> unfinishedCrossfades() const {
        return live_ == nullptr ? std::vector<char>{} : live_->executor.unfinishedCrossfades();
    }

  private:
    /**
     * @brief One epoch: a plan, the executor prepared for it, and the
     * values it was published with.
     *
     * The executor points into the plan, so the three are retired together,
     * never separately. `values` is a floor rather than the current
     * reading: whatever publishValues() has sent since is used when it
     * belongs to this plan, and this is what the epoch renders with until
     * then -- an epoch never renders without values resolved for it.
     *
     * The device properties and metronome ride along for the same reason:
     * both are settled against a context, and the only safe moment is while
     * building an epoch nothing is rendering yet. The metronome is shared
     * with the epoch it replaces whenever the context hasn't changed, so a
     * click that is sounding isn't cut off by a structural edit.
     */
    struct PreparedRender {
        explicit PreparedRender(RenderThreadPool* pool) : executor(pool) {}

        std::shared_ptr<const RenderPlan> plan;
        ParallelPlanExecutor executor;
        PlanValues values;
        RenderContext context;
        std::shared_ptr<ClickGenerator> click;
    };

    using PublishedRender =
        farbot::RealtimeObject<std::shared_ptr<PreparedRender>,
                               farbot::RealtimeObjectOptions::nonRealtimeMutatable>;
    using PublishedValues =
        farbot::RealtimeObject<PlanValues, farbot::RealtimeObjectOptions::nonRealtimeMutatable>;
    using PublishedTransport =
        farbot::RealtimeObject<TransportSnapshot,
                               farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

    RuntimeStateStore store_;

    /// The threads every epoch renders on, or null for a session rendering
    /// on the audio thread alone. Not owned, shared across epochs: threads
    /// are made once and a structural edit is not a reason to remake them.
    RenderThreadPool* pool_ = nullptr;

    /// Where the readers behind a track's clips come from, or null. Not
    /// owned or swapped: a property of the session, like the clip feed, and
    /// a structural edit must not cost a track the readers it was playing
    /// through.
    ClipVoicePool* voices_ = nullptr;

    PublishedRender published_;
    PublishedValues values_;
    PublishedTransport transport_;

    /// Not swapped with a plan and not keyed to one: what a track plays is a
    /// property of the model, and a structural edit is not a reason to lose it.
    ClipSnapshotFeed clips_;

    /// Where the audio thread finds a slot's handle. Travels with the clips.
    LaunchHandleFeed handles_;

    /// What has been asked of those handles and not applied yet. Outside every
    /// epoch, like the feed it is read beside: a request made while a plan was
    /// being compiled is still a request when the new one is live.
    LaunchRequestQueue requests_;

    /// The cursor. Not published and not swapped: it's where the timeline
    /// is, a property of the session rather than of any plan, and a plan
    /// swap must not move it.
    TransportClock clock_;

    /// The callback's live input, narrowed per block. Outside every epoch for
    /// the same reason the clock is: what the device captured is a property of
    /// the callback, not of the plan rendering it.
    LiveInputFeed liveInputs_;

    /// What the model held at the last publish. Kept so a values publish
    /// escalated into a structural one has a set to publish with; retention
    /// only ever extends, so the worst a stale set can cost is a dormant
    /// object its retirement.
    RuntimeStateIds lastModelIds_;

    /// This thread's own handle on the rendering epoch. Held because the
    /// next publish prepares against it: the differ needs the plan it was
    /// built for, and whatever survives is shared with it rather than
    /// copied out. Replaced after the swap, which is where the epoch it
    /// displaces is destroyed, on this thread.
    std::shared_ptr<PreparedRender> live_;
    std::shared_ptr<const RenderPlan> livePlan_;
};

}  // namespace magda::engine
