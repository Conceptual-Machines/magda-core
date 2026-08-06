#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <string>
#include <vector>

#include "exec/ParallelPlanExecutor.hpp"
#include "exec/RenderThreadPool.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "transport/ClickGenerator.hpp"
#include "transport/TransportClock.hpp"

/**
 * @file EngineSession.hpp
 * @brief The live engine: one plan on the audio thread, swapped from another.
 *
 * A published plan is immutable, so a structural change does not edit it: it
 * compiles a new one and swaps it in whole. This is where that swap happens,
 * and where the old one is reclaimed.
 *
 * The rule the swap exists to keep is that the audio thread never waits and
 * never destroys. Publishing blocks the thread doing the publishing until the
 * audio thread has finished the block it is in, and everything the retired
 * epoch was the last owner of is destroyed there, on that thread. Nothing that
 * runs on the callback allocates, locks, or frees.
 *
 * Threading contract: one audio thread and one publishing thread. Everything
 * that is not process() belongs to a single non-realtime thread and is not
 * synchronised against itself, because the swap primitive only promises to
 * keep the audio thread out of the publisher's way, not publishers out of each
 * other's. Two threads publishing at once is a race, not a slow path.
 */

namespace magda::engine {

class EngineSession {
  public:
    /**
     * @brief A session rendering on @p pool.
     *
     * Null is a session that renders every block on the audio thread alone,
     * which is the same executor with one thread rather than a different one.
     * A host with a pool passes it here, once: it outlives every plan, and it
     * has to outlive this, because the epochs retired here are what let go of
     * it.
     */
    explicit EngineSession(RuntimeStateFactory& factory, RenderThreadPool* pool = nullptr)
        : store_(factory), pool_(pool) {}

    /// What came of a publish. `published` false means the plan was refused
    /// and the previous one is still playing; true with messages means it is
    /// live and something in it could not be honoured. The caller should not
    /// have to infer which by watching livePlan().
    struct Result {
        bool published = false;
        std::vector<std::string> messages;
    };

    /**
     * @brief Prepare a plan and make it the one the audio thread renders.
     *
     * On the publishing thread. A plan that does not prepare is not published
     * at all and the previous one keeps playing, which is the only safe
     * reading of a plan the executor has refused.
     *
     * @p modelIds is what the model still holds, which is not the same
     * question as what the plan uses: runtime state is kept for everything the
     * model names and destroyed only for what it has lost. It only ever
     * extends retention, so a set that has drifted from the plan being
     * published costs a dormant object its retirement, never the audio thread
     * a pointer.
     *
     * @p values is what the new plan renders with until publishValues() sends
     * something newer, and it is not optional. A plan swap invalidates whatever
     * was in flight, because those values were resolved against the plan being
     * replaced; without a table of its own, the new plan would render at unity
     * for the block or two before the next one arrives, and a fader sitting at
     * -20 dB jumping to 0 dB is louder than any click the swap could have made.
     * So a plan and the values it was resolved with travel together, and
     * publishValues() is left as what it is: the mixer-speed update path.
     *
     * They have to be values for this plan. A table resolved against another
     * one is refused rather than published, because the executor would decline
     * to apply it and render at unity with nothing saying why.
     */
    Result publish(std::shared_ptr<const RenderPlan> plan, const RenderContext& context,
                   const RuntimeStateIds& modelIds, PlanValues values);

    /// On the publishing thread. Values change at mixer speed rather than
    /// structural speed, so they travel on their own.
    void publishValues(PlanValues values);

    /**
     * @brief Tempo, loop, metronome and where the transport has been asked to
     *        be, as one value.
     *
     * On the publishing thread. Travels on its own for the same reason values
     * do: a tempo edit, a loop drag and a locate are all things that happen
     * while the plan they apply to keeps playing.
     *
     * Nothing is published by default, so a session that has never seen one
     * renders stopped at the beginning at 120 bpm rather than refusing to run.
     */
    void publishTransport(TransportSnapshot transport);

    /**
     * @brief Render @p numSamples. On the audio thread.
     *
     * The transport decides what stretch of timeline that is: the caller says
     * how many samples the device wants and nothing about where they come from.
     * A callback that a loop wraps inside is rendered as two blocks, back to
     * back, and neither of them straddles the wrap.
     *
     * Renders silence until a plan is published, and the cursor stands still
     * while it does. Not a policy about what a transport may do without a
     * graph: the sample rate is settled when a plan is prepared, and a clock
     * with no idea how long a sample lasts would be inventing a position rather
     * than reporting one.
     */
    void process(int numSamples, juce::AudioBuffer<float>& output);

    /// Where the transport is, in beats. Readable from any thread; this is
    /// what a playhead is drawn from.
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

    /// The plan currently published, or null. On the publishing thread.
    std::shared_ptr<const RenderPlan> livePlan() const {
        return livePlan_;
    }

    /**
     * @brief Fades of the live plan that have not finished, by its OpIds.
     *
     * On the publishing thread, and what insertCrossfades needs to know before
     * the next plan is compiled: a fade whose edge has arrived is re-emitted
     * while it is still running and dropped once it is not. Empty when nothing
     * is published, which is what a caller with no epoch to ask should pass.
     */
    std::vector<char> unfinishedCrossfades() const {
        return live_ == nullptr ? std::vector<char>{} : live_->executor.unfinishedCrossfades();
    }

  private:
    /// One epoch: a plan, the executor prepared for it, and the values it was
    /// published with. The executor points into the plan, so the three are
    /// retired together and never separately.
    ///
    /// The values are the floor rather than the current reading: whatever
    /// publishValues() has sent since is used when it belongs to this plan, and
    /// these are what the epoch renders with until it does. An epoch never
    /// renders without values that were resolved for it.
    ///
    /// The device properties and the metronome ride along for the same reason:
    /// both are settled against a context, and the only safe moment to settle
    /// them is while making an epoch nothing is rendering yet. The metronome is
    /// shared with the epoch it replaces whenever the context has not changed,
    /// so a click that is sounding is not cut off by a structural edit.
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

    /// The threads every epoch renders on, or null for a session that renders
    /// on the audio thread alone. Not owned, and shared across epochs: threads
    /// are made once and a structural edit is not a reason to remake them.
    RenderThreadPool* pool_ = nullptr;

    PublishedRender published_;
    PublishedValues values_;
    PublishedTransport transport_;

    /// The cursor. Not published and not swapped: it is where the timeline is,
    /// which is a property of the session rather than of any plan, and a plan
    /// swap must not move it.
    TransportClock clock_;

    /// This thread's own handle on the epoch that is rendering. Held because
    /// the next publish prepares against it: the differ needs the plan it was
    /// built for, and whatever survives is shared with it rather than copied
    /// out of it. Replaced after the swap, which is where the epoch it
    /// displaces is destroyed, on this thread.
    std::shared_ptr<PreparedRender> live_;
    std::shared_ptr<const RenderPlan> livePlan_;
};

}  // namespace magda::engine
