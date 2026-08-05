#include "exec/EngineSession.hpp"

#include <algorithm>

namespace magda::engine {

EngineSession::Result EngineSession::publish(std::shared_ptr<const RenderPlan> plan,
                                             const RenderContext& context,
                                             const RuntimeStateIds& modelIds, PlanValues values) {
    if (plan == nullptr)
        return {false, {"no plan to publish"}};

    auto prepared = std::make_shared<PreparedRender>(pool_);
    prepared->plan = std::move(plan);
    prepared->values = std::move(values);
    prepared->context = context;

    // The metronome is prepared against the device, not the plan, so it is
    // shared with the epoch it replaces unless the device changed. Sharing is
    // what keeps a click that is sounding from being cut in half by an edit
    // somewhere else in the project; a new one is built only when the old one
    // was made for a sample rate that no longer applies.
    if (live_ != nullptr && live_->click != nullptr && live_->context == context) {
        prepared->click = live_->click;
    } else {
        prepared->click = std::make_shared<ClickGenerator>();
        prepared->click->prepare(context);
    }

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

    // Values the executor would not apply are refused rather than published,
    // because what it does with them instead is render at unity: the caller
    // would have asked for a fader and got 0 dB, with nothing anywhere saying
    // why. Asked of the executor rather than worked out here, so that the
    // answer cannot differ from the one the block itself will get.
    if (!prepared->executor.appliesValues(prepared->values)) {
        messages.push_back(
            "the values published with this plan are not values it can render: they were "
            "resolved against a different plan, or against this one before it was whole. "
            "It is not published, because it would have rendered at unity");
        return {false, std::move(messages)};
    }

    // The epoch's values become the ones in flight as well. Matching
    // fingerprints say two tables fit the same structure, which is not the same
    // as saying which of them is newer: republishing a plan whose structure did
    // not change (a latency re-prepare is exactly that) leaves a table from
    // before it still matching, and selecting on compatibility alone would keep
    // choosing it and ignore what this publish carries. Replacing it here is
    // what makes the bundled table the newest thing there is at the moment it
    // goes live.
    values_.nonRealtimeReplace(prepared->values);

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

void EngineSession::publishTransport(TransportSnapshot transport) {
    transport_.nonRealtimeReplace(std::move(transport));
}

void EngineSession::process(int numSamples, juce::AudioBuffer<float>& output) {
    output.clear();

    PublishedRender::ScopedAccess<farbot::ThreadType::realtime> render(published_);
    if (*render == nullptr || numSamples <= 0)
        return;

    // The executor asserts on a block longer than the plan was prepared for.
    // Here the same number also says how much buffer there is to write into, so
    // it is held to what the caller actually provided rather than trusted: the
    // pieces of a split callback are views onto this, and a view past the end
    // of it is not a wrong answer, it is someone else's memory.
    jassert(numSamples <= output.getNumSamples());
    numSamples = std::min(numSamples, output.getNumSamples());

    // Values and plans travel separately and are swapped one after the other,
    // so for the moment between the two the ones in flight can belong to the
    // plan this replaced. The epoch's own are the floor for exactly that
    // window, and the difference between a fader holding its position through a
    // structural edit and jumping to unity for a block.
    //
    // Compatibility is all this can ask. Which of two tables is newer is
    // settled by publish, which replaces the one in flight with the epoch's as
    // it goes live, so anything still matching afterwards arrived after it.
    PublishedValues::ScopedAccess<farbot::ThreadType::realtime> values(values_);
    const auto& table = (*render)->executor.appliesValues(*values) ? *values : (*render)->values;

    PublishedTransport::ScopedAccess<farbot::ThreadType::realtime> transport(transport_);

    // One callback is one or more stretches of timeline. It is more than one
    // exactly when a loop wraps inside it, and the pieces are rendered as
    // separate blocks so that nothing downstream has to know a wrap can happen
    // in the middle of a buffer. Everything below the plan is block-size
    // independent already, which is what makes cutting a callback free.
    for (const auto& segment :
         clock_.advance(*transport, (*render)->context.sampleRate, numSamples)) {
        // A view on the output, not a copy: same channels, same memory, offset
        // to where this piece belongs.
        juce::AudioBuffer<float> piece(output.getArrayOfWritePointers(), output.getNumChannels(),
                                       segment.startSample, segment.block.numSamples);

        (*render)->executor.process(table, segment.block, piece);

        // After the plan and outside it. The metronome is not in the graph: it
        // is never recorded, never routed, and not the master fader's to
        // attenuate.
        if ((*render)->click != nullptr)
            (*render)->click->render(transport->tempo, transport->click, segment.block,
                                     segment.countingIn, output, segment.startSample);
    }
}

}  // namespace magda::engine
