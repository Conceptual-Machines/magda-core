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
     * Values are published separately, and a plan swap invalidates the ones in
     * flight: they were resolved against the old plan, so the executor falls
     * back to unity until publishValues() catches up. Publish the plan first,
     * then its values.
     */
    Result publish(std::shared_ptr<const RenderPlan> plan, const RenderContext& context,
                   const RuntimeStateIds& modelIds);

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
    /// One epoch: a plan and the executor prepared for it. The executor points
    /// into the plan, so the two are retired together and never separately.
    struct PreparedRender {
        std::shared_ptr<const RenderPlan> plan;
        PlanExecutor executor;
    };

    using PublishedRender =
        farbot::RealtimeObject<std::shared_ptr<PreparedRender>,
                               farbot::RealtimeObjectOptions::nonRealtimeMutatable>;
    using PublishedValues =
        farbot::RealtimeObject<PlanValues, farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

    RuntimeStateStore store_;
    PublishedRender published_;
    PublishedValues values_;
    std::shared_ptr<const RenderPlan> livePlan_;
};

}  // namespace magda::engine
