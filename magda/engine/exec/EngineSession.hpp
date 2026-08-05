#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <string>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/RuntimeStateStore.hpp"

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
    explicit EngineSession(RuntimeStateFactory& factory) : store_(factory) {}

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
     */
    Result publish(std::shared_ptr<const RenderPlan> plan, const RenderContext& context,
                   const RuntimeStateIds& modelIds, PlanValues values);

    /// On the publishing thread. Values change at mixer speed rather than
    /// structural speed, so they travel on their own.
    void publishValues(PlanValues values);

    /// On the audio thread. Renders silence until something is published.
    void process(const BlockInfo& block, juce::AudioBuffer<float>& output);

    /// Runtime objects the store owns right now. On the publishing thread.
    std::size_t runtimeObjectCount() const {
        return store_.size();
    }

    /// The plan currently published, or null. On the publishing thread.
    std::shared_ptr<const RenderPlan> livePlan() const {
        return livePlan_;
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
    struct PreparedRender {
        std::shared_ptr<const RenderPlan> plan;
        PlanExecutor executor;
        PlanValues values;
    };

    using PublishedRender =
        farbot::RealtimeObject<std::shared_ptr<PreparedRender>,
                               farbot::RealtimeObjectOptions::nonRealtimeMutatable>;
    using PublishedValues =
        farbot::RealtimeObject<PlanValues, farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

    RuntimeStateStore store_;
    PublishedRender published_;
    PublishedValues values_;

    /// This thread's own handle on the epoch that is rendering. Held because
    /// the next publish prepares against it: the differ needs the plan it was
    /// built for, and whatever survives is shared with it rather than copied
    /// out of it. Replaced after the swap, which is where the epoch it
    /// displaces is destroyed, on this thread.
    std::shared_ptr<PreparedRender> live_;
    std::shared_ptr<const RenderPlan> livePlan_;
};

}  // namespace magda::engine
