#include "exec/EngineSession.hpp"

namespace magda::engine {

EngineSession::Result EngineSession::publish(std::shared_ptr<const RenderPlan> plan,
                                             const RenderContext& context,
                                             const RuntimeStateIds& modelIds, PlanValues values) {
    if (plan == nullptr)
        return {false, {"no plan to publish"}};

    auto prepared = std::make_shared<PreparedRender>();
    prepared->plan = std::move(plan);
    prepared->values = std::move(values);

    // Realising early costs nothing if the plan turns out not to prepare: what
    // it created stays in the store, where the next attempt reuses it and the
    // model, not the plan, decides when it goes.
    //
    // The epoch still rendering is handed over so the new one can take on what
    // the differ says survived, rather than starting every delay line empty.
    // Read only: it is live until the swap below, and it is this thread's own
    // handle on it that keeps it alive long enough to be read at all.
    const auto bindings = store_.realise(*prepared->plan, context);
    auto messages = prepared->executor.prepare(*prepared->plan, bindings, context,
                                               live_ == nullptr ? nullptr : &live_->executor);
    if (!prepared->executor.isPrepared())
        return {false, std::move(messages)};

    // The swap. This blocks until the audio thread is out of the block it was
    // in, then hands the previous epoch back here, where its destructor runs.
    published_.nonRealtimeReplace(prepared);

    // Only now: until the swap, the epoch this replaces was the one rendering,
    // and anything the new one took over from it is shared rather than copied.
    live_ = std::move(prepared);
    livePlan_ = live_->plan;

    // Safe only now: before the swap, everything about to be destroyed was
    // still reachable from the plan the audio thread was rendering. The plan
    // that is live goes in as well, so what it names survives however stale
    // the caller's model IDs turn out to be.
    store_.releaseDeleted(*livePlan_, modelIds);

    return {true, std::move(messages)};
}

void EngineSession::publishValues(PlanValues values) {
    values_.nonRealtimeReplace(std::move(values));
}

void EngineSession::process(const BlockInfo& block, juce::AudioBuffer<float>& output) {
    PublishedRender::ScopedAccess<farbot::ThreadType::realtime> render(published_);
    if (*render == nullptr) {
        output.clear();
        return;
    }

    // Values and plans travel separately, so the ones in flight can belong to
    // the plan this replaced. The epoch's own are what it renders with until
    // newer ones for this plan arrive, which is the difference between a fader
    // holding its position through a structural edit and jumping to unity for
    // a block.
    PublishedValues::ScopedAccess<farbot::ThreadType::realtime> values(values_);
    const auto& table = values->planFingerprint == (*render)->executor.planFingerprint()
                            ? *values
                            : (*render)->values;
    (*render)->executor.process(table, block, output);
}

}  // namespace magda::engine
