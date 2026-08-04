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
 */

namespace magda::engine {

class EngineSession {
  public:
    explicit EngineSession(RuntimeStateFactory& factory) : store_(factory) {}

    /**
     * @brief Prepare a plan and make it the one the audio thread renders.
     *
     * Off the audio thread. Returns whatever the executor could not honour, as
     * PlanExecutor::prepare does. A plan that does not prepare is not
     * published at all and the previous one keeps playing, which is the only
     * safe reading of a plan the executor has refused.
     *
     * Values are published separately, and a plan swap invalidates the ones in
     * flight: they were resolved against the old plan, so the executor falls
     * back to unity until publishValues() catches up. Publish the plan first,
     * then its values.
     */
    std::vector<std::string> publish(std::shared_ptr<const RenderPlan> plan,
                                     const RenderContext& context);

    /// Off the audio thread. Values change at mixer speed rather than
    /// structural speed, so they travel on their own.
    void publishValues(PlanValues values);

    /// On the audio thread. Renders silence until something is published.
    void process(const BlockInfo& block, juce::AudioBuffer<float>& output);

    /// Runtime objects the store owns right now. Off the audio thread.
    std::size_t runtimeObjectCount() const {
        return store_.size();
    }

    /// The plan currently published, or null. Off the audio thread.
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
