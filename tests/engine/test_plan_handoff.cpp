#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "exec/HandoffRing.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanHandoff.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/MidiTap.hpp"

// Where an op the callback runs reads one that may be rendered ahead (#1898).

using magda::engine::BlockInfo;
using magda::engine::EngineAudioSource;
using magda::engine::LivenessDomain;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanOp;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kBlock = 64;

PlanOp op(OpKind kind, OpRole role, int trackId, std::vector<PortRef> inputs,
          std::vector<magda::engine::PortDesc> outputs,
          LivenessDomain liveness = LivenessDomain::Deterministic) {
    PlanOp made;
    made.kind = kind;
    made.key.trackId = trackId;
    made.key.role = role;
    made.inputs = std::move(inputs);
    made.outputs = std::move(outputs);
    made.liveness = liveness;
    return made;
}

PlanOp output(PortRef in) {
    auto out = op(OpKind::Output, OpRole::HardwareOutput, 9, {in}, {});
    out.liveness = LivenessDomain::Deterministic;
    return out;
}

/// A clip track and a live input summed on the master, which is live because of the input.
RenderPlan mixedPlan() {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::AudioInput, OpRole::LiveAudioInput, 2, {}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MixAudio, OpRole::TrackAudioInput, 9,
                          {PortRef{0, 0}, PortRef{1, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    auto out = output(PortRef{2, 0});
    out.liveness = LivenessDomain::Live;
    plan.ops.push_back(out);
    plan.outputOps = {3};
    magda::engine::bakeScheduling(plan);
    return plan;
}

int countOf(const RenderPlan& plan, OpKind kind) {
    return static_cast<int>(
        std::ranges::count_if(plan.ops, [kind](const PlanOp& o) { return o.kind == kind; }));
}

class ConstantAudio final : public EngineAudioSource {
  public:
    explicit ConstantAudio(float v) : value(v) {}
    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.fill(value);
    }
    float value;
};

std::vector<float> render(const RenderPlan& plan, bool bySide = false) {
    ConstantAudio clip(0.25f);
    PlanBindings bindings;
    bindings.clipAudio[1] = &clip;
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);

    PlanExecutor executor;
    executor.prepare(plan, bindings, RenderContext{48000.0, kBlock, 2});
    juce::AudioBuffer<float> out(2, kBlock);
    out.clear();
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;
    block.continuous = true;
    if (bySide) {
        executor.processSide(0, 0, values, block, out);
        executor.processSide(1, 0, values, block, out);
    } else {
        executor.process(values, block, out);
    }
    return {out.getReadPointer(0), out.getReadPointer(0) + kBlock};
}

}  // namespace

TEST_CASE("A live op reads a deterministic one through a handoff", "[engine][plan][1898]") {
    const auto plan = mixedPlan();
    REQUIRE(magda::engine::validatePlan(plan).empty());
    CHECK(magda::engine::handoffProblems(plan).size() == 1);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    CHECK(magda::engine::handoffProblems(guarded).empty());
    REQUIRE(countOf(guarded, OpKind::Handoff) == 1);

    const auto& handoff = guarded.ops[2];
    CHECK(handoff.kind == OpKind::Handoff);
    CHECK(handoff.liveness == LivenessDomain::Deterministic);
    CHECK(handoff.key.trackId == 1);
    CHECK(handoff.key.role == OpRole::Handoff);
    CHECK(handoff.inputs.front() == PortRef{0, 0});
    CHECK(guarded.ops[3].inputs.front() == PortRef{2, 0});
    CHECK(guarded.outputOps == std::vector<magda::engine::OpId>{4});
    CHECK(magda::engine::carriesSchedule(guarded));

    CHECK(magda::engine::insertHandoffs(guarded).ops.size() == guarded.ops.size());
}

TEST_CASE("The hardware output reads a deterministic mix through a handoff",
          "[engine][plan][1898]") {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(output(PortRef{0, 0}));
    plan.ops.push_back(output(PortRef{0, 0}));
    plan.ops.back().key.index = 1;
    plan.outputOps = {1, 2};
    magda::engine::bakeScheduling(plan);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    CHECK(countOf(guarded, OpKind::Handoff) == 1);
    CHECK(guarded.ops[2].inputs.front() == guarded.ops[3].inputs.front());
    CHECK(guarded.ops[1].kind == OpKind::Handoff);
}

TEST_CASE("MIDI crosses through its own handoff", "[engine][plan][1898]") {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, 1, {}, {SignalKind::Midi}));
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1,
                          {PortRef{0, 0}, PortRef{1, 0}}, {SignalKind::Midi},
                          LivenessDomain::Live));
    magda::engine::bakeScheduling(plan);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    REQUIRE(countOf(guarded, OpKind::Handoff) == 1);
    CHECK(guarded.ops[2].outputs.front().kind == SignalKind::Midi);
}

TEST_CASE("A handoff renders what its producer rendered", "[engine][plan][1898]") {
    const auto plan = mixedPlan();
    const auto plain = render(plan);
    const auto guarded = render(magda::engine::insertHandoffs(plan));
    REQUIRE(plain.size() == guarded.size());
    CHECK(std::memcmp(plain.data(), guarded.data(), plain.size() * sizeof(float)) == 0);
}

TEST_CASE("A handoff that claims liveness is refused", "[engine][plan][1898]") {
    auto guarded = magda::engine::insertHandoffs(mixedPlan());
    guarded.ops[2].liveness = LivenessDomain::Live;
    CHECK_FALSE(magda::engine::validatePlan(guarded).empty());
}

TEST_CASE("No buffer is shared across the boundary", "[engine][plan][1898]") {
    // The two sides may render in different blocks on different threads, so a slot is only
    // reused on its own side, a handoff's slot is its own, and nothing works in place across.
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::AudioInput, OpRole::LiveAudioInput, 2, {}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 3, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::MixAudio, OpRole::TrackAudioInput, 9,
                          {PortRef{0, 0}, PortRef{1, 0}, PortRef{2, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::Gain, OpRole::TrackMute, 9, {PortRef{3, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    auto out = output(PortRef{4, 0});
    out.liveness = LivenessDomain::Live;
    plan.ops.push_back(out);
    plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, 1, {}, {SignalKind::Midi}));
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1,
                          {PortRef{6, 0}, PortRef{7, 0}}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.outputOps = {5};
    magda::engine::bakeScheduling(plan);
    REQUIRE(magda::engine::validatePlan(plan).empty());

    const auto guarded = magda::engine::insertHandoffs(plan);
    REQUIRE(countOf(guarded, OpKind::Handoff) == 3);
    const auto prepared =
        magda::engine::resolveLayout(guarded, std::vector<int>(guarded.ops.size(), 0));
    const auto& buffers = prepared.buffers;

    // Arenas are numbered independently, so a slot is a kind and an index.
    std::map<std::pair<SignalKind, int>, std::set<int>> ownersBySlot;
    for (std::size_t i = 0; i < guarded.ops.size(); ++i) {
        const auto& planOp = guarded.ops[i];
        const auto owner = planOp.kind == OpKind::Handoff          ? 100 + static_cast<int>(i)
                           : magda::engine::runsAtCallback(planOp) ? 1
                                                                   : 0;
        for (std::size_t port = 0; port < planOp.outputs.size(); ++port) {
            const auto slot =
                buffers.portSlots[static_cast<std::size_t>(prepared.portOffsets[i]) + port];
            ownersBySlot[{planOp.outputs[port].kind, slot}].insert(owner);
        }
        if (planOp.kind == OpKind::Handoff || planOp.kind == OpKind::MixAudio)
            CHECK_FALSE(buffers.writesInPlace[i]);
        // Within the callback side, writing in place is still allowed.
        if (planOp.kind == OpKind::Gain)
            CHECK(buffers.writesInPlace[i]);
    }
    for (const auto& [slot, owners] : ownersBySlot) {
        INFO("slot " << slot.second);
        CHECK(owners.size() == 1);
    }
}

TEST_CASE("The two sides of a block render what the whole block does", "[engine][plan][1898]") {
    const auto guarded = magda::engine::insertHandoffs(mixedPlan());
    const auto whole = render(guarded);
    const auto split = render(guarded, true);
    REQUIRE(whole.size() == split.size());
    CHECK(std::memcmp(whole.data(), split.data(), whole.size() * sizeof(float)) == 0);
    CHECK(whole.front() == 0.25f);
}

namespace {

/// A different value every sample, so a block rendered out of turn cannot pass for its neighbour.
class CountingAudio final : public EngineAudioSource {
  public:
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        for (int i = 0; i < block.numSamples; ++i, ++count)
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), i, static_cast<float>(count));
    }
    int count = 0;
};

/// One note per block, at a sample and pitch that move every block.
class CountingMidi final : public magda::engine::EngineMidiSource {
  public:
    void render(const BlockInfo& block, juce::MidiBuffer& out) override {
        out.addEvent(juce::MidiMessage::noteOn(1, 36 + (count % 48), 1.0f),
                     count % block.numSamples);
        ++count;
    }
    int count = 0;
};

class CollectingTap final : public magda::engine::MidiTap {
  public:
    void write(const juce::MidiBuffer& midi, const BlockInfo&) override {
        auto& block = blocks.emplace_back();
        for (const auto event : midi)
            block.emplace_back(event.samplePosition, event.getMessage().getNoteNumber());
    }
    std::vector<std::vector<std::pair<int, int>>> blocks;
};

/// A clip and a MIDI clip, each read by live ops through a handoff, rendered for @p blocks.
struct Rendered {
    std::vector<float> audio;
    std::vector<std::vector<std::pair<int, int>>> midi;
    int misses = 0;
};

RenderPlan aheadAndLivePlan() {
    auto plan = mixedPlan();
    plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, 1, {}, {SignalKind::Midi}));
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1,
                          {PortRef{4, 0}, PortRef{5, 0}}, {SignalKind::Midi},
                          LivenessDomain::Live));
    magda::engine::bakeScheduling(plan);
    return magda::engine::insertHandoffs(plan);
}

/// Whole blocks, or the ahead side up to @p lag blocks before the callback through a ring of
/// @p depth; @p skip is a block the ahead side never renders, and @p whole one whose table has
/// no sides, on the side @p wholeOn names or on both (-1).
Rendered renderLagged(int blocks, int lag = -1, int depth = 1, int skip = -1, int whole = -1,
                      int wholeOn = -1) {
    const auto plan = aheadAndLivePlan();
    CountingAudio clip;
    CountingMidi notes;
    CollectingTap tap;
    PlanBindings bindings;
    bindings.clipAudio[1] = &clip;
    bindings.clipMidi[1] = &notes;
    for (const auto& planOp : plan.ops)
        if (planOp.kind == OpKind::MergeMidi)
            bindings.midiTaps[planOp.key] = &tap;
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);
    auto sideless = values;
    sideless.params = std::make_shared<magda::engine::ParamTable>();
    const auto valuesFor = [&](int index, int side) -> const PlanValues& {
        return index == whole && (wholeOn < 0 || wholeOn == side) ? sideless : values;
    };

    PlanExecutor executor;
    executor.setRenderAheadDepth(depth);
    for (const auto& message : executor.prepare(plan, bindings, RenderContext{48000.0, kBlock, 2}))
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(executor.isPrepared());

    juce::AudioBuffer<float> out(2, kBlock), ahead(2, kBlock);
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;
    block.continuous = true;

    Rendered rendered;
    int nextAhead = 0;
    for (int i = 0; i < blocks; ++i) {
        if (lag < 0) {
            executor.process(values, block, out);
        } else {
            while (nextAhead <= i + lag && nextAhead < blocks) {
                if (nextAhead != skip &&
                    !executor.processSide(0, static_cast<std::uint64_t>(nextAhead),
                                          valuesFor(nextAhead, 0), block, ahead))
                    break;
                ++nextAhead;
            }
            executor.processSide(1, static_cast<std::uint64_t>(i), valuesFor(i, 1), block, out);
        }
        rendered.audio.insert(rendered.audio.end(), out.getReadPointer(0),
                              out.getReadPointer(0) + kBlock);
    }
    rendered.midi = tap.blocks;
    rendered.misses = executor.handoffMisses();
    return rendered;
}

}  // namespace

TEST_CASE("The ahead side renders as far ahead as the ring is deep", "[engine][plan][1898]") {
    constexpr int kBlocks = 16;
    const auto whole = renderLagged(kBlocks);
    REQUIRE(whole.audio.size() == kBlocks * kBlock);
    REQUIRE(whole.midi.size() == kBlocks);
    CHECK(whole.audio.back() != whole.audio.front());

    for (const int lag : {0, 1, 3}) {
        INFO("lag " << lag);
        const auto lagged = renderLagged(kBlocks, lag, lag + 1);
        CHECK(lagged.misses == 0);
        REQUIRE(lagged.audio.size() == whole.audio.size());
        CHECK(std::memcmp(whole.audio.data(), lagged.audio.data(),
                          whole.audio.size() * sizeof(float)) == 0);
        CHECK(lagged.midi == whole.midi);
    }
}

TEST_CASE("The ahead side waits for the callback to release an entry", "[engine][plan][1898]") {
    const auto plan = aheadAndLivePlan();
    CountingAudio clip;
    PlanBindings bindings;
    bindings.clipAudio[1] = &clip;
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);

    PlanExecutor executor;
    executor.setRenderAheadDepth(2);
    for (const auto& message : executor.prepare(plan, bindings, RenderContext{48000.0, kBlock, 2}))
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(executor.isPrepared());
    juce::AudioBuffer<float> out(2, kBlock);
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;

    // Any block may come first: a plan prepared mid-stream starts where it is driven.
    CHECK(executor.processSide(0, 40, values, block, out));
    CHECK(executor.processSide(0, 41, values, block, out));
    CHECK_FALSE(executor.processSide(0, 42, values, block, out));
    CHECK(clip.count == 2 * kBlock);

    executor.processSide(1, 40, values, block, out);
    CHECK(out.getSample(0, kBlock - 1) == static_cast<float>(kBlock - 1));
    CHECK_FALSE(executor.processSide(0, 40, values, block, out));
    CHECK(executor.processSide(0, 42, values, block, out));
    CHECK(executor.handoffMisses() == 0);
}

TEST_CASE("A block the ahead side missed hears its handoffs silent", "[engine][plan][1898]") {
    constexpr int kBlocks = 6;
    constexpr int kSkip = 3;
    const auto whole = renderLagged(kBlocks);
    const auto lagged = renderLagged(kBlocks, 1, 2, kSkip);
    CHECK(lagged.misses == 1);
    REQUIRE(lagged.audio.size() == whole.audio.size());

    const auto at = [](const std::vector<float>& audio, int block) {
        return audio.data() + block * kBlock;
    };
    const std::vector<float> silent(kBlock, 0.0f);
    CHECK(std::memcmp(at(lagged.audio, kSkip), silent.data(), kBlock * sizeof(float)) == 0);
    CHECK(lagged.midi[kSkip].empty());
    CHECK(std::memcmp(at(lagged.audio, kSkip - 1), at(whole.audio, kSkip - 1),
                      kBlock * sizeof(float)) == 0);
    CHECK(lagged.midi[kSkip - 1] == whole.midi[kSkip - 1]);
}

TEST_CASE("A block rendered whole waits for the ahead side to stop", "[engine][plan][1898]") {
    // A sideless table renders its block whole at the callback, ahead-side ops included, so
    // nothing after it may have been rendered ahead by then.
    constexpr int kBlocks = 8;
    const auto whole = renderLagged(kBlocks);
    for (const int at : {0, 2, 7}) {
        INFO("whole at " << at);
        const auto lagged = renderLagged(kBlocks, 2, 3, -1, at);
        CHECK(lagged.misses == 0);
        REQUIRE(lagged.audio.size() == whole.audio.size());
        CHECK(std::memcmp(whole.audio.data(), lagged.audio.data(),
                          whole.audio.size() * sizeof(float)) == 0);
        CHECK(lagged.midi == whole.midi);
    }
}

TEST_CASE("A handoff ring hands back MIDI, fractions and panic as written",
          "[engine][plan][1898]") {
    const auto plan = aheadAndLivePlan();
    std::size_t midiHandoff = plan.ops.size();
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].kind == OpKind::Handoff &&
            plan.ops[i].outputs.front().kind == SignalKind::Midi)
            midiHandoff = i;
    REQUIRE(midiHandoff < plan.ops.size());

    magda::engine::HandoffRing ring;
    ring.prepare(plan, 2, 2, kBlock, std::vector<int>(plan.ops.size(), 4096));
    REQUIRE(ring.canWrite(7, true));
    ring.midi(midiHandoff, 7).addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 5);
    ring.fractions(midiHandoff, 7).add(5, 1, 60, 0.25f);
    ring.setPanic(midiHandoff, 7, true);
    ring.publish(7, kBlock);

    const auto& reader = ring;
    CHECK(reader.published(7, kBlock) == magda::engine::HandoffRing::Published::split);
    CHECK(reader.published(7, kBlock / 2) == magda::engine::HandoffRing::Published::missing);
    CHECK(reader.published(9, kBlock) == magda::engine::HandoffRing::Published::missing);
    CHECK(reader.midi(midiHandoff, 7).getNumEvents() == 1);
    CHECK(reader.fractions(midiHandoff, 7).at(5, 1, 60) == 0.25f);
    CHECK(reader.panic(midiHandoff, 7));

    CHECK(ring.canWrite(8, true));
    CHECK_FALSE(ring.canWrite(9, true));
    ring.release(7);
    CHECK(ring.canWrite(9, true));
}

TEST_CASE("A block the ahead side never reached renders whole", "[engine][plan][1898]") {
    constexpr int kBlocks = 6;
    const auto whole = renderLagged(kBlocks);
    const auto lagged = renderLagged(kBlocks, 0, 1, 3);
    CHECK(lagged.misses == 0);
    REQUIRE(lagged.audio.size() == whole.audio.size());
    CHECK(std::memcmp(whole.audio.data(), lagged.audio.data(),
                      whole.audio.size() * sizeof(float)) == 0);
    CHECK(lagged.midi == whole.midi);
}

TEST_CASE("A block that cannot render in order or split is dropped", "[engine][plan][1898]") {
    // Rendering it whole would run the ahead side's sources again after later blocks; split, its
    // live ops would read parameters the other side resolved.
    constexpr int kBlocks = 8;
    constexpr int kAt = 2;
    for (const auto [skip, wholeOn] : {std::pair{kAt, -1}, std::pair{-1, 1}}) {
        INFO("skip " << skip << ", sideless on " << wholeOn);
        const auto lagged = renderLagged(kBlocks, 2, 3, skip, kAt, wholeOn);
        CHECK(lagged.misses == 1);
        REQUIRE(lagged.audio.size() == kBlocks * kBlock);
        const std::vector<float> silent(kBlock, 0.0f);
        CHECK(std::memcmp(lagged.audio.data() + kAt * kBlock, silent.data(),
                          kBlock * sizeof(float)) == 0);
        CHECK(lagged.midi.size() == kBlocks - 1);

        // The clip advanced once per block it rendered, never twice.
        const auto rendered = skip == kAt ? kAt : kAt + 1;
        CHECK(lagged.audio[(kAt + 1) * kBlock] == static_cast<float>(rendered * kBlock));
    }
}

namespace {

/// A clip's notes, each at a fraction of its sample, raising a panic every third block.
class FractionalMidi final : public magda::engine::EngineMidiSource {
  public:
    explicit FractionalMidi(int notesPerBlock = 1, int firstNote = 36)
        : perBlock(notesPerBlock), base(firstNote) {}

    void render(const BlockInfo& block, juce::MidiBuffer& out) override {
        magda::engine::NoteFractions ignored;
        renderWithFractions(block, out, ignored);
    }
    void renderWithFractions(const BlockInfo& block, juce::MidiBuffer& out,
                             magda::engine::NoteFractions& fractions) override {
        for (int n = 0; n < perBlock; ++n) {
            const auto sample = (count + n) % block.numSamples;
            const auto note = base + ((count + n) % 48);
            out.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), sample);
            fractions.add(sample, 1, note, 0.125f * static_cast<float>(1 + (count + n) % 7));
        }
        panic = count % 3 == 0;
        ++count;
    }
    bool raisedAllNotesOff() const override {
        return panic;
    }

    int perBlock;
    int base;
    int count = 0;
    bool panic = false;
};

/// What a live device was handed, block by block: each note-on's sample, pitch and fraction,
/// and the panic.
class HeardProbe final : public magda::engine::EngineDevice {
  public:
    struct Note {
        int sample;
        int note;
        float fraction;
        bool operator==(const Note&) const = default;
    };
    struct Block {
        std::vector<Note> notes;
        bool panic;
        bool operator==(const Block&) const = default;
    };

    void setMidiInputBoundBytes(int bytes) override {
        boundBytes = bytes;
    }
    void process(magda::engine::DeviceBlock& block) override {
        block.audio.clear();
        auto& heard = blocks.emplace_back();
        heard.panic = block.midiInAllNotesOff;
        for (const auto event : *block.midiIn) {
            const auto message = event.getMessage();
            heard.notes.push_back(
                {event.samplePosition, message.getNoteNumber(),
                 block.midiInFractions->at(event.samplePosition, message.getChannel(),
                                           message.getNoteNumber())});
        }
    }

    std::vector<Block> blocks;
    int boundBytes = 0;
};

/// @p sources MIDI clips merged on track 1, read by a live device through a handoff.
RenderPlan clipsIntoLiveDevice(int sources) {
    RenderPlan plan;
    std::vector<PortRef> clips;
    for (int track = 1; track <= sources; ++track) {
        clips.push_back(PortRef{static_cast<magda::engine::OpId>(plan.ops.size()), 0});
        plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, track, {}, {SignalKind::Midi}));
    }
    auto feed = clips.front();
    if (sources > 1) {
        feed = PortRef{static_cast<magda::engine::OpId>(plan.ops.size()), 0};
        plan.ops.push_back(
            op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1, clips, {SignalKind::Midi}));
    }
    // Live because the track also takes a live MIDI input, which nothing plays here.
    const PortRef keys{static_cast<magda::engine::OpId>(plan.ops.size()), 0};
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    const PortRef heard{static_cast<magda::engine::OpId>(plan.ops.size()), 0};
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::ChainMidiMerge, 1, {feed, keys},
                          {SignalKind::Midi}, LivenessDomain::Live));
    auto device = op(OpKind::Device, OpRole::DeviceProcess, 1, {PortRef{}, heard, PortRef{}},
                     {SignalKind::Audio}, LivenessDomain::Live);
    device.key.deviceId = 9;
    plan.ops.push_back(device);
    plan.ops.push_back(output(PortRef{static_cast<magda::engine::OpId>(plan.ops.size() - 1), 0}));
    plan.ops.back().liveness = LivenessDomain::Live;
    plan.outputOps = {static_cast<magda::engine::OpId>(plan.ops.size() - 1)};
    magda::engine::bakeScheduling(plan);
    return magda::engine::insertHandoffs(plan);
}

struct Heard {
    std::vector<HeardProbe::Block> blocks;
    int boundBytes = 0;
    int misses = 0;
};

/// The device's view over @p blocks, whole or with the ahead side @p lag blocks early.
Heard hearThroughHandoff(int sources, int notesPerBlock, int blocks, int lag = -1) {
    const auto plan = clipsIntoLiveDevice(sources);
    for (const auto& problem : magda::engine::validatePlan(plan))
        UNSCOPED_INFO(problem);
    REQUIRE(magda::engine::validatePlan(plan).empty());
    REQUIRE(countOf(plan, OpKind::Handoff) == 1);

    std::vector<std::unique_ptr<FractionalMidi>> clips;
    HeardProbe probe;
    PlanBindings bindings;
    for (int track = 1; track <= sources; ++track) {
        clips.push_back(std::make_unique<FractionalMidi>(notesPerBlock, 12 * track));
        bindings.clipMidi[track] = clips.back().get();
    }
    bindings.devices[magda::engine::DeviceKey{9}] = &probe;
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);

    PlanExecutor executor;
    executor.setRenderAheadDepth(std::max(lag, 0) + 1);
    for (const auto& message : executor.prepare(plan, bindings, RenderContext{48000.0, kBlock, 2}))
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(executor.isPrepared());

    juce::AudioBuffer<float> out(2, kBlock), ahead(2, kBlock);
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;
    block.continuous = true;
    int nextAhead = 0;
    for (int i = 0; i < blocks; ++i) {
        if (lag < 0) {
            executor.process(values, block, out);
            continue;
        }
        while (nextAhead <= i + lag && nextAhead < blocks &&
               executor.processSide(0, static_cast<std::uint64_t>(nextAhead), values, block, ahead))
            ++nextAhead;
        executor.processSide(1, static_cast<std::uint64_t>(i), values, block, out);
    }
    return {probe.blocks, probe.boundBytes, executor.handoffMisses()};
}

}  // namespace

TEST_CASE("Fractions and panics cross a handoff rendered ahead", "[engine][plan][1898]") {
    constexpr int kBlocks = 9;
    const auto whole = hearThroughHandoff(1, 1, kBlocks);
    REQUIRE(whole.blocks.size() == kBlocks);
    CHECK(std::ranges::count_if(whole.blocks, [](const auto& b) { return b.panic; }) == 3);
    CHECK(std::ranges::adjacent_find(whole.blocks, [](const auto& a, const auto& b) {
              return a.notes.front().fraction != b.notes.front().fraction;
          }) != whole.blocks.end());

    for (const int lag : {0, 2}) {
        INFO("lag " << lag);
        const auto lagged = hearThroughHandoff(1, 1, kBlocks, lag);
        CHECK(lagged.misses == 0);
        CHECK(lagged.blocks == whole.blocks);
    }
}

TEST_CASE("A handoff reserves the MIDI its merge can carry", "[engine][plan][1898]") {
    // Two clips at 300 note-ons a block, nine bytes each: 5400 bytes past the handoff, more
    // than one producer's budget.
    constexpr int kNotes = 300;
    constexpr int kBlocks = 4;
    const auto whole = hearThroughHandoff(2, kNotes, kBlocks);
    // Both clips through the handoff, and the live keys merged behind it.
    CHECK(whole.boundBytes == 3 * magda::engine::kMaxMidiBytesPerPort);
    REQUIRE(whole.blocks.size() == kBlocks);
    CHECK(whole.blocks.front().notes.size() == 2 * kNotes);

    const auto lagged = hearThroughHandoff(2, kNotes, kBlocks, 2);
    CHECK(lagged.misses == 0);
    CHECK(lagged.boundBytes == whole.boundBytes);
    CHECK(lagged.blocks == whole.blocks);
}
