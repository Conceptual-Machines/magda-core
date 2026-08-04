#include "exec/EngineSession.hpp"

namespace magda::engine {

std::vector<std::string> EngineSession::publish(std::shared_ptr<const RenderPlan> plan,
                                                const RenderContext& context) {
    if (plan == nullptr)
        return {"no plan to publish"};

    auto prepared = std::make_shared<PreparedRender>();
    prepared->plan = std::move(plan);

    // Runtime state is realised before the swap and retired after it. Creating
    // early costs nothing if the plan turns out not to prepare; destroying
    // early would free something the audio thread is still rendering through.
    const auto bindings = store_.realise(*prepared->plan);
    auto messages = prepared->executor.prepare(*prepared->plan, bindings, context);
    if (!prepared->executor.isPrepared())
        return messages;

    // The swap. This blocks until the audio thread is out of the block it was
    // in, then hands the previous epoch back here, where its destructor runs.
    published_.nonRealtimeReplace(std::move(prepared));

    livePlan_ = published_.nonRealtimeAcquire()->plan;
    published_.nonRealtimeRelease();

    // Safe only now: before the swap, everything about to be dropped was still
    // reachable from the plan the audio thread was rendering.
    store_.retireUnused(*livePlan_);

    return messages;
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

    PublishedValues::ScopedAccess<farbot::ThreadType::realtime> values(values_);
    (*render)->executor.process(*values, block, output);
}

}  // namespace magda::engine
