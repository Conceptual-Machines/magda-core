#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderThreadPool.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"

using magda::engine::BlockInfo;
using magda::engine::EngineAudioSource;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::ParallelPlanExecutor;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanOp;
using magda::engine::PlanValues;
using magda::engine::PortDesc;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RenderThreadPool;
using magda::engine::SignalKind;

namespace {

constexpr int kBlockSize = 16;

PlanOp audioOp(OpKind kind, std::vector<PortRef> inputs = {}) {
    PlanOp op;
    op.kind = kind;
    op.inputs = std::move(inputs);
    op.outputs = {PortDesc{SignalKind::Audio}};
    return op;
}

PlanOp midiOp(OpKind kind, std::vector<PortRef> inputs = {}) {
    PlanOp op;
    op.kind = kind;
    op.inputs = std::move(inputs);
    op.outputs = {PortDesc{SignalKind::Midi}};
    return op;
}

void giveUniqueKeys(RenderPlan& plan) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        plan.ops[i].key.trackId = 1;
        plan.ops[i].key.index = static_cast<int>(i);
    }
}

PlanValues valuesFor(const RenderPlan& plan) {
    const auto problems = magda::engine::validatePlan(plan);
    for (const auto& problem : problems)
        UNSCOPED_INFO(problem);
    REQUIRE(problems.empty());
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);
    return values;
}

std::vector<bool> requiredOf(const PlanValues& values) {
    std::vector<bool> required;
    for (const auto& value : values.ops)
        required.push_back(value.required);
    return required;
}

RenderPlan mutedBranchPlan() {
    RenderPlan plan;
    auto source = audioOp(OpKind::ClipAudio);
    source.key.role = OpRole::ClipAudio;
    plan.ops.push_back(source);                           // 0: observable source
    plan.ops.push_back(audioOp(OpKind::Gain, {{0, 0}}));  // 1: removable work
    plan.ops.push_back(audioOp(OpKind::Gain, {{1, 0}}));  // 2: mute gate
    auto output = audioOp(OpKind::Output, {{2, 0}});
    output.outputs.clear();
    output.key.role = OpRole::HardwareOutput;
    plan.ops.push_back(output);  // 3: root
    giveUniqueKeys(plan);
    plan.outputOps = {3};
    magda::engine::bakeScheduling(plan);
    return plan;
}

RenderPlan deltaPlan() {
    RenderPlan plan;
    auto source = audioOp(OpKind::ClipAudio);
    source.key.role = OpRole::ClipAudio;
    plan.ops.push_back(source);                                       // 0
    plan.ops.push_back(audioOp(OpKind::Gain, {{0, 0}}));              // 1: wet
    plan.ops.push_back(audioOp(OpKind::Gain, {{0, 0}}));              // 2: dry
    plan.ops.push_back(audioOp(OpKind::Subtract, {{1, 0}, {2, 0}}));  // 3
    auto output = audioOp(OpKind::Output, {{3, 0}});
    output.outputs.clear();
    output.key.role = OpRole::HardwareOutput;
    plan.ops.push_back(output);  // 4
    giveUniqueKeys(plan);
    plan.outputOps = {4};
    magda::engine::bakeScheduling(plan);
    return plan;
}

class ConstantSource final : public EngineAudioSource {
  public:
    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        ++renders;
        out.fill(1.0f);
    }

    int renders = 0;
};

BlockInfo nextBlock(int index) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.continuous = index != 0;
    block.beats.start = static_cast<double>(index);
    block.beats.end = static_cast<double>(index + 1);
    return block;
}

template <typename Executor>
std::vector<float> renderSequence(Executor& executor, const RenderPlan& plan,
                                  PlanBindings& bindings, std::vector<PlanValues> values) {
    for (const auto& message :
         executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2}))
        FAIL_CHECK(message);

    juce::AudioBuffer<float> output(2, kBlockSize);
    std::vector<float> stream;
    for (std::size_t i = 0; i < values.size(); ++i) {
        executor.process(values[i], nextBlock(static_cast<int>(i)), output);
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
                stream.push_back(output.getSample(channel, sample));
    }
    return stream;
}

std::vector<float> constantBlocks(std::initializer_list<float> levels) {
    std::vector<float> samples;
    for (const auto level : levels)
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < kBlockSize; ++sample)
                samples.push_back(level);
    return samples;
}

}  // namespace

TEST_CASE("Required ops stop at a silent gate and return when it opens",
          "[engine][exec][required-ops]") {
    const auto plan = mutedBranchPlan();
    auto values = valuesFor(plan);

    values.ops[2].silent = true;
    magda::engine::resolveRequiredOps(plan, values);
    CHECK(requiredOf(values) == std::vector<bool>{true, false, true, true});

    values.ops[2].silent = false;
    magda::engine::resolveRequiredOps(plan, values);
    CHECK(requiredOf(values) == std::vector<bool>{true, true, true, true});
}

TEST_CASE("A live shared consumer keeps a producer needed by a silent branch",
          "[engine][exec][required-ops]") {
    RenderPlan plan;
    plan.ops.push_back(audioOp(OpKind::ClipAudio));       // 0: source root
    plan.ops.push_back(audioOp(OpKind::Gain, {{0, 0}}));  // 1: shared pure work
    plan.ops.push_back(audioOp(OpKind::Gain, {{1, 0}}));  // 2: silent branch
    plan.ops.push_back(audioOp(OpKind::Gain, {{1, 0}}));  // 3: live branch
    plan.ops.push_back(audioOp(OpKind::MixAudio, {{2, 0}, {3, 0}}));
    auto output = audioOp(OpKind::Output, {{4, 0}});
    output.outputs.clear();
    plan.ops.push_back(output);
    giveUniqueKeys(plan);
    plan.outputOps = {5};
    magda::engine::bakeScheduling(plan);

    auto values = valuesFor(plan);
    values.ops[2].silent = true;
    magda::engine::resolveRequiredOps(plan, values);

    CHECK(requiredOf(values) == std::vector<bool>{true, true, true, true, true, true});
}

TEST_CASE("Delta liveness reads the dry edge only while dry is subtracted",
          "[engine][exec][required-ops]") {
    const auto plan = deltaPlan();
    auto values = valuesFor(plan);

    magda::engine::resolveRequiredOps(plan, values);
    CHECK(requiredOf(values) == std::vector<bool>{true, true, false, true, true});

    values.ops[3].subtractsDry = true;
    magda::engine::resolveRequiredOps(plan, values);
    CHECK(requiredOf(values) == std::vector<bool>{true, true, true, true, true});
}

TEST_CASE("State effects sources and MIDI observers remain required without consumers",
          "[engine][exec][required-ops]") {
    RenderPlan plan;
    plan.ops.push_back(audioOp(OpKind::ClipAudio));   // 0
    plan.ops.push_back(midiOp(OpKind::ClipMidi));     // 1
    plan.ops.push_back(audioOp(OpKind::ClipAudio));   // 2
    plan.ops.push_back(midiOp(OpKind::SessionMidi));  // 3
    auto audioInput = audioOp(OpKind::AudioInput);    // 4
    audioInput.liveness = magda::engine::LivenessDomain::Live;
    plan.ops.push_back(audioInput);
    auto midiInput = midiOp(OpKind::MidiInput);  // 5
    midiInput.liveness = magda::engine::LivenessDomain::Live;
    plan.ops.push_back(midiInput);
    plan.ops.push_back(audioOp(OpKind::Device, {magda::engine::noInput(), magda::engine::noInput(),
                                                magda::engine::noInput()}));  // 6
    auto delay = audioOp(OpKind::Delay, {{0, 0}});                            // 7
    delay.key.role = OpRole::MixInputDelay;
    plan.ops.push_back(delay);
    auto crossfade = audioOp(OpKind::Crossfade, {{0, 0}, {2, 0}});  // 8
    crossfade.key.role = OpRole::EdgeCrossfade;
    plan.ops.push_back(crossfade);
    plan.ops.push_back(audioOp(OpKind::Meter, {magda::engine::noInput()}));  // 9
    auto mod =
        audioOp(OpKind::ModSource, {magda::engine::noInput(), magda::engine::noInput()});  // 10
    mod.outputs.clear();
    plan.ops.push_back(mod);
    auto insertSend =
        audioOp(OpKind::InsertSend, {magda::engine::noInput(), magda::engine::noInput()});  // 11
    insertSend.outputs.clear();
    plan.ops.push_back(insertSend);
    plan.ops.push_back(audioOp(OpKind::InsertReturn));                             // 12
    plan.ops.push_back(midiOp(OpKind::MergeMidi));                                 // 13
    plan.ops.push_back(midiOp(OpKind::MidiNoteGate, {magda::engine::noInput()}));  // 14
    plan.ops.push_back(audioOp(OpKind::Gain, {{7, 0}}));                           // 15
    plan.ops.push_back(audioOp(OpKind::Gain, {magda::engine::noInput()}));         // 16
    plan.ops.push_back(audioOp(OpKind::MixAudio));                                 // 17
    plan.ops.push_back(
        audioOp(OpKind::Fader, {magda::engine::noInput(), magda::engine::noInput()}));  // 18
    giveUniqueKeys(plan);

    auto values = valuesFor(plan);
    magda::engine::resolveRequiredOps(plan, values);

    for (std::size_t i = 0; i < 15; ++i) {
        INFO("root op " << i);
        CHECK(values.ops[i].required);
    }
    CHECK_FALSE(values.ops[15].required);
    CHECK_FALSE(values.ops[16].required);
    CHECK_FALSE(values.ops[17].required);
    CHECK_FALSE(values.ops[18].required);
}

TEST_CASE("A MIDI-carrying fader remains required as an observable gate",
          "[engine][exec][required-ops]") {
    RenderPlan plan;
    auto fader = audioOp(OpKind::Fader, {magda::engine::noInput(), magda::engine::noInput()});
    fader.outputs.push_back(PortDesc{SignalKind::Midi});
    plan.ops.push_back(std::move(fader));
    giveUniqueKeys(plan);

    auto values = valuesFor(plan);
    magda::engine::resolveRequiredOps(plan, values);
    CHECK(values.ops[0].required);
}

TEST_CASE("resolvePlanValues applies required-op liveness to a compiled model",
          "[engine][exec][required-ops]") {
    auto rack = std::make_unique<magda::RackInfo>();
    rack->id = 5;
    magda::ChainInfo chain;
    chain.id = 10;
    chain.muted = true;
    magda::DeviceInfo effect;
    effect.id = 7;
    effect.name = "Effect";
    effect.deviceType = magda::DeviceType::Effect;
    chain.elements.push_back(magda::makeDeviceElement(effect));
    rack->chains.push_back(std::move(chain));

    magda::TrackInfo track;
    track.id = 1;
    track.type = magda::TrackType::Media;
    track.audioOutputDevice = "master";
    track.chain.fxChainElements.push_back(magda::ChainElement{std::move(rack)});

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    master.type = magda::TrackType::Master;

    const auto plan = magda::engine::compileRenderPlan({track}, master);
    PlanValues values;
    const auto messages = magda::engine::resolvePlanValues(plan, {track}, master, values);
    for (const auto& message : messages)
        UNSCOPED_INFO(message);
    REQUIRE(messages.empty());
    REQUIRE(values.planFingerprint == magda::engine::planFingerprint(plan));

    bool foundSilentGate = false;
    bool foundPrunedWork = false;
    for (std::size_t i = 0; i < values.ops.size(); ++i) {
        foundSilentGate = foundSilentGate || values.ops[i].silent;
        foundPrunedWork = foundPrunedWork || !values.ops[i].required;
    }
    CHECK(foundSilentGate);
    CHECK(foundPrunedWork);
}

TEST_CASE("A static zero gain does not prune work automation may restore",
          "[engine][exec][required-ops]") {
    const auto plan = mutedBranchPlan();
    auto values = valuesFor(plan);
    values.ops[2].gainLeft = 0.0f;
    values.ops[2].gainRight = 0.0f;

    magda::engine::resolveRequiredOps(plan, values);
    CHECK(requiredOf(values) == std::vector<bool>{true, true, true, true});
}

TEST_CASE("Stale incomplete and malformed value tables conservatively run every op",
          "[engine][exec][required-ops]") {
    auto plan = mutedBranchPlan();

    SECTION("fingerprint mismatch") {
        auto values = valuesFor(plan);
        values.planFingerprint ^= 1U;
        for (auto& value : values.ops)
            value.required = false;
        magda::engine::resolveRequiredOps(plan, values);
        CHECK(requiredOf(values) == std::vector<bool>(plan.ops.size(), true));
    }

    SECTION("missing values") {
        auto values = valuesFor(plan);
        values.ops.pop_back();
        for (auto& value : values.ops)
            value.required = false;
        magda::engine::resolveRequiredOps(plan, values);
        CHECK(requiredOf(values) == std::vector<bool>(values.ops.size(), true));
    }

    SECTION("malformed input") {
        auto values = valuesFor(plan);
        plan.ops[1].inputs[0] = PortRef{99, 0};
        values.planFingerprint = magda::engine::planFingerprint(plan);
        for (auto& value : values.ops)
            value.required = false;
        magda::engine::resolveRequiredOps(plan, values);
        CHECK(requiredOf(values) == std::vector<bool>(plan.ops.size(), true));
    }
}

TEST_CASE("Serial and parallel executors agree across mute value publishes",
          "[engine][exec][required-ops]") {
    const auto plan = mutedBranchPlan();
    auto audible = valuesFor(plan);
    auto muted = audible;
    muted.ops[2].silent = true;
    magda::engine::resolveRequiredOps(plan, audible);
    magda::engine::resolveRequiredOps(plan, muted);
    REQUIRE_FALSE(muted.ops[1].required);

    ConstantSource serialSource;
    PlanBindings serialBindings;
    serialBindings.clipAudio[1] = &serialSource;
    PlanExecutor serial;
    auto unprunedAudible = audible;
    auto unprunedMuted = muted;
    for (auto* table : {&unprunedAudible, &unprunedMuted})
        for (auto& value : table->ops)
            value.required = true;
    const auto reference =
        renderSequence(serial, plan, serialBindings,
                       {unprunedAudible, unprunedMuted, unprunedAudible, unprunedMuted});

    ConstantSource prunedSerialSource;
    PlanBindings prunedSerialBindings;
    prunedSerialBindings.clipAudio[1] = &prunedSerialSource;
    PlanExecutor prunedSerial;
    const auto serialPruned =
        renderSequence(prunedSerial, plan, prunedSerialBindings, {audible, muted, audible, muted});

    ConstantSource parallelSource;
    PlanBindings parallelBindings;
    parallelBindings.clipAudio[1] = &parallelSource;
    RenderThreadPool pool(2, false);
    ParallelPlanExecutor parallel(&pool);
    const auto actual =
        renderSequence(parallel, plan, parallelBindings, {audible, muted, audible, muted});

    CHECK(reference == constantBlocks({1.0f, 0.0f, 1.0f, 0.0f}));
    CHECK(serialPruned == reference);
    CHECK(actual == reference);
    CHECK(serialSource.renders == 4);
    CHECK(parallelSource.renders == 4);
    CHECK(prunedSerialSource.renders == 4);
}

TEST_CASE("Serial and parallel executors agree when delta toggles its dry edge",
          "[engine][exec][required-ops]") {
    const auto plan = deltaPlan();
    auto wet = valuesFor(plan);
    wet.ops[1].gainLeft = wet.ops[1].gainRight = 2.0f;
    wet.ops[2].gainLeft = wet.ops[2].gainRight = 0.5f;
    auto delta = wet;
    delta.ops[3].subtractsDry = true;
    magda::engine::resolveRequiredOps(plan, wet);
    magda::engine::resolveRequiredOps(plan, delta);
    REQUIRE_FALSE(wet.ops[2].required);
    REQUIRE(delta.ops[2].required);

    ConstantSource serialSource;
    PlanBindings serialBindings;
    serialBindings.clipAudio[1] = &serialSource;
    PlanExecutor serial;
    auto unprunedWet = wet;
    auto unprunedDelta = delta;
    for (auto* table : {&unprunedWet, &unprunedDelta})
        for (auto& value : table->ops)
            value.required = true;
    const auto reference = renderSequence(serial, plan, serialBindings,
                                          {unprunedWet, unprunedDelta, unprunedWet, unprunedDelta});

    ConstantSource prunedSerialSource;
    PlanBindings prunedSerialBindings;
    prunedSerialBindings.clipAudio[1] = &prunedSerialSource;
    PlanExecutor prunedSerial;
    const auto serialPruned =
        renderSequence(prunedSerial, plan, prunedSerialBindings, {wet, delta, wet, delta});

    ConstantSource parallelSource;
    PlanBindings parallelBindings;
    parallelBindings.clipAudio[1] = &parallelSource;
    RenderThreadPool pool(2, false);
    ParallelPlanExecutor parallel(&pool);
    const auto actual = renderSequence(parallel, plan, parallelBindings, {wet, delta, wet, delta});

    CHECK(reference == constantBlocks({2.0f, 1.5f, 2.0f, 1.5f}));
    CHECK(serialPruned == reference);
    CHECK(actual == reference);
}
