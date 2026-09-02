#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "NullDiffGain.hpp"
#include "core/DeviceState.hpp"
#include "core/ParameterUtils.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/PlanExecutor.hpp"
#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/FaustPlugin.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "param/ParamBlock.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_engine_device_layer.cpp
 * @brief The devices MAGDA ships, running under the native engine (#2174).
 *
 * The corpus knew one device until this slice, and it was written for the
 * corpus: a gain implemented twice, once in each leg. What that could prove was
 * that the two legs agree about a gain. What it could not is that either engine
 * runs a device somebody would find in a project, which is what a real project
 * is made of.
 *
 * These are the engine's half, on its own. They ask the two catalogs the app
 * asks, they build what those catalogs return, and they run it. What the two
 * hosts do with the same device is the corpus's question and is asked where
 * both legs are (magda_juce_tests).
 */

namespace {

namespace adapter = magda::daw::audio::engine_adapter;

/// A MIDI-driven device that needs nothing set to make a sound, which is what
/// lets a test assert on its output rather than on its parameters first.
constexpr const char* kInstrumentId = "magda_kick";

magda::engine::RenderContext contextFor(int blockSize = 512) {
    return {.sampleRate = 48000.0, .maxBlockSize = blockSize, .numChannels = 2};
}

/// One block of silence, and the block description that goes with it.
struct Block {
    explicit Block(const magda::engine::RenderContext& context)
        : buffer(context.numChannels, context.maxBlockSize) {
        buffer.clear();
        info.numSamples = context.maxBlockSize;
        info.playing = true;
        info.endSeconds = context.maxBlockSize / context.sampleRate;
        info.endBeat = info.endSeconds * 2.0;
    }

    magda::engine::DeviceBlock deviceBlock() {
        magda::engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(buffer);
        block.block = info;
        block.midiIn = &midi;
        return block;
    }

    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    magda::engine::BlockInfo info;
};

float peakOf(const juce::AudioBuffer<float>& buffer) {
    return buffer.getMagnitude(0, buffer.getNumSamples());
}

/// Counts what it was handed, and nothing else.
///
/// The adapter's MIDI path cannot be measured through a real device: what a
/// synth does with a clock byte is its own business, and a device that ignored
/// half its input would look exactly like an adapter that dropped half.
class CountingDevice final : public magda::daw::audio::MagdaDevice {
  public:
    magda::daw::audio::DeviceProperties properties() const override {
        magda::daw::audio::DeviceProperties properties;
        properties.pluginId = "counting";
        properties.name = "Counting";
        properties.takesMidiInput = true;
        return properties;
    }

    void process(magda::daw::audio::DeviceProcessContext& context) override {
        received = context.midi != nullptr ? context.midi->size() : 0;
    }

    int received = 0;
};

/// Emits whatever it was told to, however big.
///
/// A device is where the port's budget is spent, and SysEx is where spending it
/// stops looking like spending events: a dump is one event and hundreds of
/// bytes, so a device can sit far inside any event count while going far past
/// the byte budget the executor sized the port from.
class EmittingDevice final : public magda::daw::audio::MagdaDevice {
  public:
    EmittingDevice(int count, int dataBytes) : count_(count), payload_(dataBytes, 0x7f) {}

    magda::daw::audio::DeviceProperties properties() const override {
        magda::daw::audio::DeviceProperties properties;
        properties.pluginId = "emitting";
        properties.name = "Emitting";
        properties.takesMidiInput = true;
        return properties;
    }

    void process(magda::daw::audio::DeviceProcessContext& context) override {
        if (context.midi == nullptr)
            return;

        context.midi->clear();
        for (int event = 0; event < count_; ++event)
            context.midi->addEvent({juce::MidiMessage::createSysExMessage(
                                        payload_.data(), static_cast<int>(payload_.size())),
                                    0});
    }

  private:
    int count_;
    std::vector<std::uint8_t> payload_;
};

/// Records what the executor told it, and renders nothing.
class BoundProbe final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock&) override {}

    void setMidiInputBoundBytes(int bytes) override {
        bound = bytes;
    }

    int bound = -1;
};

/// Rewrites, reorders and drops what it was handed, on messages big enough to
/// be heap-allocated.
///
/// The three mutating paths of DeviceMidiBuffer in one device, on SysEx,
/// because that is where juce::MidiMessage owns heap and where getting the
/// assignment wrong leaks it rather than merely misbehaving.
class RewritingDevice final : public magda::daw::audio::MagdaDevice {
  public:
    magda::daw::audio::DeviceProperties properties() const override {
        magda::daw::audio::DeviceProperties properties;
        properties.pluginId = "rewriting";
        properties.name = "Rewriting";
        properties.takesMidiInput = true;
        return properties;
    }

    void process(magda::daw::audio::DeviceProcessContext& context) override {
        if (context.midi == nullptr || context.midi->size() < 3)
            return;

        // Reversed, so the sort has a permutation to apply rather than an
        // already-ordered list to leave alone.
        const auto count = context.midi->size();
        for (int at = 0; at < count; ++at) {
            auto message = context.midi->message(at);
            message.setTimeStamp(static_cast<double>(count - at));
            context.midi->setEvent(at, {message, 0});
        }

        context.midi->sortByTimestamp();
        context.midi->removeEvent(1);
    }
};

/// What @p buffer costs against the port's budget, by the engine's own model:
/// six bytes an event plus the event's own length.
int budgetCostOf(const juce::MidiBuffer& buffer) {
    int bytes = 0;
    for (const auto metadata : buffer)
        bytes += (magda::engine::kMidiShortMessageBytes - 3) + metadata.numBytes;
    return bytes;
}

/// The slot @p device calls @p name, or -1.
int slotNamed(const magda::daw::audio::MagdaDevice& device, const juce::String& name) {
    for (int slot = 0; slot < device.parameterCount(); ++slot)
        if (device.parameterInfo(slot).name == name)
            return slot;
    return -1;
}

/// Set a slot from its display value rather than its normalised position, so a
/// test reads in the units the device documents.
void setDisplayValue(magda::daw::audio::MagdaDevice& device, int slot, float displayValue) {
    const auto info = device.parameterInfo(slot);
    device.setParameterValue(slot, magda::ParameterUtils::realToNormalized(displayValue, info));
}

}  // namespace

TEST_CASE("the engine can run every device that has moved to the SDK", "[engine][devices][2174]") {
    // The rule rather than a list of names, so a device migrating to the SDK
    // does not have to remember to come back here: what the factory answers is
    // exactly what the catalog carries, in both directions. A device with no
    // createDevice is one the engine cannot run, and saying so is the point --
    // the alternative is a stand-in that passes signal while the incumbent runs
    // the real thing.
    int sdkDevices = 0;

    for (const auto* spec : magda::daw::audio::getAllInternalPluginSpecs()) {
        REQUIRE(spec != nullptr);
        REQUIRE(spec->pluginId != nullptr);

        const auto expected = spec->createDevice != nullptr;
        INFO("internal device " << spec->pluginId);
        CHECK(adapter::canCreateEngineDevice(spec->pluginId) == expected);

        if (expected) {
            ++sdkDevices;
            magda::DeviceInfo model;
            model.pluginId = spec->pluginId;
            CHECK(adapter::createEngineDevice(model) != nullptr);
        }
    }

    for (const auto* spec : magda::daw::audio::compiled::getAllCompiledPluginSpecs()) {
        REQUIRE(spec != nullptr);
        REQUIRE(spec->pluginId != nullptr);

        // A compiled device is not in the internal registry, only its parameter
        // aliases are, so the factory has to ask both catalogs. Asserted here
        // because a factory that asked one would answer no for every compiled
        // device, which is most of the fleet.
        const auto expected = spec->createDevice != nullptr;
        INFO("compiled device " << spec->pluginId);
        CHECK(adapter::canCreateEngineDevice(spec->pluginId) == expected);

        if (expected) {
            ++sdkDevices;
            magda::DeviceInfo model;
            model.pluginId = spec->pluginId;
            CHECK(adapter::createEngineDevice(model) != nullptr);
        }
    }

    // The rule above is satisfied by a build where nothing has moved to the SDK
    // at all, which is a green test over an empty set.
    CHECK(sdkDevices > 0);
}

TEST_CASE("a device the catalogs do not have is refused rather than stood in for",
          "[engine][devices][2174]") {
    CHECK_FALSE(adapter::canCreateEngineDevice("not_a_device"));

    magda::DeviceInfo model;
    model.pluginId = "not_a_device";
    CHECK(adapter::createEngineDevice(model) == nullptr);

    // An external plugin reaches the factory as a pluginId nothing registered,
    // and gets the same answer for the same reason: nothing hosts VST3 in the
    // engine yet (#1893).
    model.pluginId = "VST3-1234567890";
    CHECK(adapter::createEngineDevice(model) == nullptr);
}

TEST_CASE("a shipped instrument renders through the engine's device op",
          "[engine][devices][2174]") {
    magda::DeviceInfo model;
    model.pluginId = kInstrumentId;

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    const auto context = contextFor();
    device->prepare(context);

    Block silent(context);
    auto silentBlock = silent.deviceBlock();
    device->process(silentBlock);
    CHECK(peakOf(silent.buffer) == 0.0f);

    Block played(context);
    played.midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    auto playedBlock = played.deviceBlock();
    device->process(playedBlock);

    // What is asserted is that the note reached the DSP and the DSP wrote into
    // the executor's own buffer, which is the whole of what the adapter is for.
    // What it sounds like is the device's business and is pinned by its own
    // tests.
    CHECK(peakOf(played.buffer) > 0.0f);
}

TEST_CASE("the plan's resolved values reach the device's parameters", "[engine][devices][2174]") {
    magda::DeviceInfo model;
    model.pluginId = kInstrumentId;

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(device.get());
    REQUIRE(hosted != nullptr);
    REQUIRE(hosted->device().parameterCount() > 0);

    const auto info = hosted->device().parameterInfo(0);
    const auto context = contextFor();
    device->prepare(context);

    // The table the executor publishes, built by hand: one parameter, holding
    // one value for the block, in the parameter's own units and read through
    // its own scale. Which is what a device gets from the value layer, and the
    // adapter's job is to turn it back into the normalised position the SDK
    // takes without either end having an opinion about the scale.
    const auto target = info.minValue + 0.75f * (info.maxValue - info.minValue);

    magda::engine::ResolvedParams table;
    table.prepare(1);
    table.beginBlock(context.maxBlockSize);
    table.setDomain(0, magda::ParameterUtils::domainOf(info));

    const auto position = magda::ParameterUtils::realToNormalized(target, info);
    auto* slot = table.slotFor(0);
    REQUIRE(slot != nullptr);
    slot[0] = {.startSample = 0, .startValue = position, .endValue = position};
    table.setSegmentCount(0, 1);

    Block block(context);
    auto deviceBlock = block.deviceBlock();
    deviceBlock.params = table.device(0, 1);
    device->process(deviceBlock);

    CHECK(hosted->device().parameterValue(0) == Catch::Approx(position).margin(1.0e-5));
}

TEST_CASE("the MIDI scratch holds every stream the port's budget admits",
          "[engine][devices][2174]") {
    // The budget is bytes and the cheapest event is one byte of data, so the
    // most events a legal block can carry is the budget divided by that, not by
    // what a note costs. A scratch sized from the note is three quarters of the
    // way there, and the quarter it is short by is a stream the adapter would
    // have to grow the vector for -- an allocation on the audio thread, for
    // input that broke no rule.
    //
    // Realtime bytes are what makes this reachable rather than theoretical: a
    // clock at speed is exactly this shape.
    constexpr int kEventOverheadBytes = magda::engine::kMidiShortMessageBytes - 3;
    constexpr int kWorstCaseEvents =
        magda::engine::kMaxMidiBytesPerPort / (kEventOverheadBytes + 1);

    auto counting = std::make_unique<CountingDevice>();
    auto* device = counting.get();

    adapter::EngineMagdaDevice hosted(std::move(counting), /*offlineRender=*/false);
    const auto context = contextFor();
    hosted.prepare(context);

    Block block(context);
    for (int event = 0; event < kWorstCaseEvents; ++event)
        block.midi.addEvent(juce::MidiMessage::midiClock(), event % context.maxBlockSize);

    // What was really encoded, checked rather than assumed: the assertion below
    // is only about the adapter if the buffer it reads is as full as the budget
    // allows.
    REQUIRE(block.midi.data.size() <= magda::engine::kMaxMidiBytesPerPort);
    REQUIRE(block.midi.getNumEvents() == kWorstCaseEvents);

    auto deviceBlock = block.deviceBlock();
    hosted.process(deviceBlock);

    CHECK(device->received == kWorstCaseEvents);
}

TEST_CASE("a device writing past the port's byte budget is cut off at the bytes",
          "[engine][devices][2174]") {
    // Sixty-six raw bytes an event, so seventy-two against the budget: fifty-six
    // fit and the fifty-seventh does not. Nothing near the event cap the scratch
    // is sized by, which is the point -- a guard counting events would let every
    // one of these through and hand the port ten times what it reserved for.
    constexpr int kPayloadBytes = 64;
    constexpr int kEmitted = 200;

    auto emitting = std::make_unique<EmittingDevice>(kEmitted, kPayloadBytes);
    adapter::EngineMagdaDevice hosted(std::move(emitting), /*offlineRender=*/false);

    const auto context = contextFor();
    hosted.prepare(context);

    Block block(context);
    juce::MidiBuffer out;

    auto deviceBlock = block.deviceBlock();
    deviceBlock.midiOut = &out;
    hosted.process(deviceBlock);

    // Every event that landed is whole, and what landed fits the budget.
    CHECK(budgetCostOf(out) <= magda::engine::kMaxMidiBytesPerPort);

    // And it really was the bytes that stopped it, not the event cap: fewer
    // events arrived than were emitted, and far fewer than the cap allows.
    CHECK(out.getNumEvents() < kEmitted);
    CHECK(out.getNumEvents() > 0);
}

TEST_CASE("the adapter carries a merged input, not one producer's worth",
          "[engine][devices][2174]") {
    // kMaxMidiBytesPerPort is what one producer may write. A device's input
    // port is often a merge, and the executor sums the bound through the MIDI
    // graph for exactly that reason, so a scratch sized from the constant drops
    // the tail of anything arriving on a track with more than one source. The
    // adapter has to size from what it is told.
    constexpr int kEventOverheadBytes = magda::engine::kMidiShortMessageBytes - 3;
    constexpr int kBound = 3 * magda::engine::kMaxMidiBytesPerPort;
    constexpr int kEvents = kBound / (kEventOverheadBytes + 1);

    auto counting = std::make_unique<CountingDevice>();
    auto* device = counting.get();

    adapter::EngineMagdaDevice hosted(std::move(counting), /*offlineRender=*/false);
    const auto context = contextFor();
    hosted.prepare(context);
    hosted.setMidiInputBoundBytes(kBound);

    Block block(context);
    for (int event = 0; event < kEvents; ++event)
        block.midi.addEvent(juce::MidiMessage::midiClock(), event % context.maxBlockSize);

    REQUIRE(block.midi.getNumEvents() == kEvents);
    REQUIRE(kEvents > magda::engine::kMaxMidiBytesPerPort / (kEventOverheadBytes + 1));

    auto deviceBlock = block.deviceBlock();
    hosted.process(deviceBlock);

    CHECK(device->received == kEvents);
}

TEST_CASE("the executor tells a device what can reach its MIDI input", "[engine][devices][2174]") {
    // The bound is the engine's fact and no device can work it out for itself,
    // so it has to arrive. Asserted at the boundary rather than through a
    // render: what would go wrong silently is nobody saying anything.
    magda::TrackInfo instrument;
    instrument.id = 1;
    instrument.type = magda::TrackType::Media;
    instrument.name = "Instrument";
    instrument.chain.fxChainElements.emplace_back(magda::nulldiff::synthDevice(10));

    magda::TrackInfo effect;
    effect.id = 2;
    effect.type = magda::TrackType::Media;
    effect.name = "Effect";
    effect.chain.fxChainElements.emplace_back(magda::nulldiff::gainDevice(20));

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    master.type = magda::TrackType::Master;
    master.name = "Master";

    const auto plan = magda::engine::compileRenderPlan({instrument, effect}, master);

    BoundProbe synthProbe;
    BoundProbe gainProbe;

    magda::engine::PlanBindings bindings;
    for (const auto& op : plan.ops)
        if (op.kind == magda::engine::OpKind::Device)
            bindings.devices[op.key.deviceKey()] =
                op.key.deviceId == 10 ? static_cast<magda::engine::EngineDevice*>(&synthProbe)
                                      : static_cast<magda::engine::EngineDevice*>(&gainProbe);

    magda::engine::PlanExecutor executor;
    executor.prepare(plan, bindings, contextFor(), nullptr, nullptr);
    REQUIRE(executor.isPrepared());

    // The instrument is fed through the track's MIDI merge, so it is told what
    // that merge can carry. The gain has no MIDI input at all and is told so,
    // rather than being left at whatever it assumed.
    CHECK(synthProbe.bound >= magda::engine::kMaxMidiBytesPerPort);
    CHECK(gainProbe.bound == 0);
}

TEST_CASE("a device may rewrite, reorder and drop long messages", "[engine][devices][2174]") {
    // juce::MidiMessage keeps anything past eight bytes on the heap, and its
    // move assignment overwrites the destination's pointer without freeing what
    // it held. So every path that puts a message somewhere has to copy or
    // move-construct, and the two obvious spellings -- vector::erase and
    // stable_sort over the events -- do neither. This runs all three mutating
    // paths over SysEx, which is where that goes wrong.
    auto rewriting = std::make_unique<RewritingDevice>();
    adapter::EngineMagdaDevice hosted(std::move(rewriting), /*offlineRender=*/false);

    const auto context = contextFor();
    hosted.prepare(context);

    // Distinguishable payloads, long enough to be heap-allocated.
    const std::vector<std::uint8_t> first(32, 0x11);
    const std::vector<std::uint8_t> second(32, 0x22);
    const std::vector<std::uint8_t> third(32, 0x33);

    Block block(context);
    block.midi.addEvent(juce::MidiMessage::createSysExMessage(first.data(), 32), 0);
    block.midi.addEvent(juce::MidiMessage::createSysExMessage(second.data(), 32), 1);
    block.midi.addEvent(juce::MidiMessage::createSysExMessage(third.data(), 32), 2);

    juce::MidiBuffer out;
    auto deviceBlock = block.deviceBlock();
    deviceBlock.midiOut = &out;
    hosted.process(deviceBlock);

    // Reversed by the rewrite, then the second of those dropped: third, first.
    std::vector<std::uint8_t> marks;
    for (const auto metadata : out) {
        const auto message = metadata.getMessage();
        REQUIRE(message.isSysEx());
        REQUIRE(message.getSysExDataSize() == 32);
        marks.push_back(message.getSysExData()[0]);
    }

    CHECK(marks == std::vector<std::uint8_t>{0x33, 0x11});
}

TEST_CASE("one note-off releases a pitch that was pressed many times", "[engine][devices][2192]") {
    // The mono and legato voice keeps a stack of what is held, and a note-on
    // for a pitch already on it moves that pitch to the top rather than adding
    // a second copy. Two things rest on that.
    //
    // The audible one is here: a pitch held twice would need two note-offs to
    // be let go, and an unbalanced stream sends one. Those streams are ordinary
    // -- a recorded clip playing back merged with the live input that fed it
    // sends every note twice -- and the poly path has releasePolyVoicesForPitch()
    // for exactly this. The mono path had nothing, so the note hung.
    //
    // The other is that the stack is then bounded by the MIDI range and can be
    // sized once, off the audio thread, instead of growing under process().
    // That is what the repeat count below is for: it is past any bound the
    // stack could be given, so a stack that appended would have grown.
    magda::DeviceInfo model;
    model.pluginId = "magda_fm";

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(device.get());
    REQUIRE(hosted != nullptr);
    auto& sdk = hosted->device();

    const int voiceModeSlot = slotNamed(sdk, "Voice Mode");
    const int sustainSlot = slotNamed(sdk, "Amp Sustain");
    const int releaseSlot = slotNamed(sdk, "Amp Release");
    const int attackSlot = slotNamed(sdk, "Amp Attack");
    REQUIRE(voiceModeSlot >= 0);
    REQUIRE(sustainSlot >= 0);
    REQUIRE(releaseSlot >= 0);
    REQUIRE(attackSlot >= 0);

    const auto context = contextFor();
    device->prepare(context);

    // Mono, held at full sustain so a note that never releases stays audible,
    // with the envelope's ends short enough to settle inside a few blocks.
    setDisplayValue(sdk, voiceModeSlot, 1.0f);  // Poly / Mono / Legato
    setDisplayValue(sdk, sustainSlot, 1.0f);
    setDisplayValue(sdk, attackSlot, 1.0f);
    setDisplayValue(sdk, releaseSlot, 1.0f);

    constexpr int kNote = 60;
    constexpr int kRepeats = 400;

    Block pressed(context);
    for (int repeat = 0; repeat < kRepeats; ++repeat)
        pressed.midi.addEvent(juce::MidiMessage::noteOn(1, kNote, 1.0f), 0);
    auto pressedBlock = pressed.deviceBlock();
    device->process(pressedBlock);

    Block held(context);
    auto heldBlock = held.deviceBlock();
    device->process(heldBlock);
    const float heldPeak = peakOf(held.buffer);

    // The note has to be sounding, or the release below proves nothing.
    REQUIRE(heldPeak > 0.0f);

    Block lifted(context);
    lifted.midi.addEvent(juce::MidiMessage::noteOff(1, kNote), 0);
    auto liftedBlock = lifted.deviceBlock();
    device->process(liftedBlock);

    float releasedPeak = 0.0f;
    for (int block = 0; block < 8; ++block) {
        Block quiet(context);
        auto quietBlock = quiet.deviceBlock();
        device->process(quietBlock);
        releasedPeak = peakOf(quiet.buffer);
    }

    // One note-off, four hundred note-ons: the voice is released. A stack that
    // appended would still be holding three hundred and ninety-nine of them and
    // sitting at full sustain here.
    CHECK(releasedPeak < heldPeak * 0.05f);
}

TEST_CASE("a device built for the engine gets the state the project saved",
          "[engine][devices][2192]") {
    // Parameters reach a device through the plan's value layer, so for most
    // devices there is nothing else to carry and the factory carried nothing.
    // The runtime Faust device breaks that: its dsp source is state, not a
    // parameter, and a device built without it runs the default passthrough.
    // That is a render of a project nobody saved, which is the one thing the
    // factory must not do quietly.
    constexpr const char* kSource = R"FAUST(
// Self-contained test DSP. The literal "stdfaust.lib" in this comment is
// load-bearing: the compile step only skips its automatic import when the
// source already mentions the library.
process = *(0.25), *(0.25);
)FAUST";

    magda::device_state::Doc doc;
    doc.deviceType = "faust";
    doc.root.props.set("dspSource", kSource);
    doc.root.props.set("dspName", "Saved patch");

    magda::DeviceInfo model;
    model.pluginId = "faust";
    model.pluginState = magda::device_state::encode(doc);
    REQUIRE(model.pluginState.isNotEmpty());

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(device.get());
    REQUIRE(hosted != nullptr);

    auto* faust = dynamic_cast<magda::daw::audio::FaustPlugin*>(&hosted->device());
    REQUIRE(faust != nullptr);

    CHECK(faust->getDspSource().contains("*(0.25)"));
    CHECK(faust->getDspName() == juce::String("Saved patch"));
}

TEST_CASE("an engine-built arpeggiator gets the settings the model saved",
          "[engine][devices][2299]") {
    // Ramp cycles / quantize / hard angle are device state, not parameters, so
    // the only way they reach a native render is through the model's saved
    // document. The faceplate edit path captures into DeviceInfo.pluginState;
    // this pins the other half: a device built from that state actually holds
    // the values.
    magda::device_state::Doc doc;
    doc.deviceType = "arpeggiator";
    doc.root.props.set("arpRampCycles", 5);
    doc.root.props.set("arpQuantize", 0.75f);
    doc.root.props.set("arpQuantizeSub", 32);
    doc.root.props.set("arpHardAngle", true);

    magda::DeviceInfo model;
    model.pluginId = "arpeggiator";
    model.pluginState = magda::device_state::encode(doc);
    REQUIRE(model.pluginState.isNotEmpty());

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(device.get());
    REQUIRE(hosted != nullptr);

    auto* arp = dynamic_cast<magda::daw::audio::ArpeggiatorPlugin*>(&hosted->device());
    REQUIRE(arp != nullptr);

    CHECK(arp->rampCycles.load() == 5);
    CHECK(arp->quantize.load() == Catch::Approx(0.75f));
    CHECK(arp->quantizeSub.load() == 32);
    CHECK(arp->hardAngle.load());
}

TEST_CASE("the Rings resonator renders through the engine's device op", "[engine][devices][2299]") {
    // The first hand-written device to cross for #2299. What earns it a named
    // case next to the generic sweep above is its shape: a MIDI-excited synth
    // with no note-off gate, whose voice keeps ringing after the strum -- the
    // sweep proves the factory builds it, not that MIDI reaches the exciter.
    magda::DeviceInfo model;
    model.pluginId = "magda_rings";

    auto device = adapter::createEngineDevice(model);
    REQUIRE(device != nullptr);

    const auto context = contextFor();
    device->prepare(context);

    Block silent(context);
    auto silentBlock = silent.deviceBlock();
    device->process(silentBlock);
    CHECK(peakOf(silent.buffer) == 0.0f);

    Block struck(context);
    struck.midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
    auto struckBlock = struck.deviceBlock();
    device->process(struckBlock);
    CHECK(peakOf(struck.buffer) > 0.0f);

    // Rings has no gate: the resonator decays per its Damping control, so the
    // block after the strum still carries the tail.
    Block ringing(context);
    auto ringingBlock = ringing.deviceBlock();
    device->process(ringingBlock);
    CHECK(peakOf(ringing.buffer) > 0.0f);
}
