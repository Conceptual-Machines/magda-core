#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "EngineSessionScaffold.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCompiler.hpp"
#include "tap/ValueTap.hpp"

/**
 * @file test_value_taps.cpp
 * @brief What the UI reads back (#2122).
 *
 * The engine publishes two numbers a window wants and cannot work out for
 * itself: what a modifier's output did this block, and what a parameter
 * resolved to once its base, its lane and its links had been settled against
 * each other. Both come out of the same tap, bound by the same key, published
 * at the one moment both exist.
 *
 * What is asserted here is the whole of that path, and one thing that is not on
 * it: nothing simulates a modifier. The incumbent engine keeps a MAGDA-side
 * copy of the random modulator turning so its editor animates between audio
 * callbacks, because there the value is polled at frame rate and a poll between
 * two callbacks has nothing to report. Here the block publishes what it
 * produced, so a tap that is not moving is an engine that is not rendering, and
 * that is the honest answer rather than a gap to paper over.
 */

using namespace magda;
using magda::engine::ParamKey;
using magda::engine::ValueTap;
using magda::test::makeMaster;
using magda::test::makeTrack;
using magda::test::ScriptedMidi;
using magda::test::ScriptedMidiSource;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

constexpr TrackId kTrack = 1;
constexpr DeviceId kDevice = 7;

/// A device with one parameter reading 0 to 1, so a normalised position and the
/// value the device is handed are the same number and a case says one thing.
DeviceInfo makeDevice(DeviceId id, float storedValue) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.mods = createDefaultMods(0);

    ParameterInfo info(0, "Level", "", 0.0f, 1.0f, 0.0f);
    info.currentValue = storedValue;
    device.parameters.push_back(info);

    return device;
}

/// A track whose one modifier is an envelope waiting for a note, linked to the
/// device's one parameter at @p amount.
///
/// An envelope rather than an LFO because what is being read back is a value
/// rather than a shape: it is shut until a note arrives and fully open
/// immediately after, so a tap either saw the block or did not.
TrackInfo trackWithEnvelope(float storedValue, float amount) {
    auto track = makeTrack(kTrack);
    track.volume = 1.0f;
    track.pan = 0.0f;
    track.recordArmed = true;
    track.inputMonitor = InputMonitorMode::In;
    track.midiInputDevice = "test";
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(kDevice, storedValue)));

    track.mods = createDefaultMods(1);
    track.mods[0].type = ModType::Envelope;
    track.mods[0].triggerMode = LFOTriggerMode::MIDI;
    track.mods[0].running = false;
    track.mods[0].envAttackMs = 0.0f;
    track.mods[0].envDecayMs = 0.0f;
    track.mods[0].envSustain = 1.0f;
    track.mods[0].envReleaseMs = 0.0f;
    track.mods[0].links.push_back(
        ModLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(kTrack, kDevice), 0),
                amount, /*bipolar=*/false, /*enabled=*/true});

    return track;
}

// --- keys, spelled out rather than built, so a case says the address it means

ParamKey deviceParam(int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = kTrack;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, kDevice};
    key.index = index;
    return key;
}

/// The modifier itself, taken as a source: the same address with no parameter
/// index, which is how a modifier is named everywhere in the engine.
ParamKey modifierKey(ModId id) {
    ParamKey key;
    key.kind = ParamKey::Kind::ModParam;
    key.scope = ParamKey::Scope::Track;
    key.trackId = kTrack;
    key.modId = id;
    key.index = -1;
    return key;
}

ParamKey trackVolume(TrackId track) {
    ParamKey key;
    key.kind = ParamKey::Kind::TrackVolume;
    key.scope = ParamKey::Scope::Track;
    key.trackId = track;
    return key;
}

/// A device that reads nothing and writes nothing. What the ops do with the
/// signal is not what these cases are about.
class NullDevice final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock&) override {}
};

/// A host that wants a fixed set of values read back, which is what having a
/// window open amounts to.
class Factory final : public magda::engine::RuntimeStateFactory {
  public:
    Factory(std::vector<ParamKey> wanted, ScriptedMidi* midi)
        : wanted_(std::move(wanted)), midi_(midi) {}

    std::unique_ptr<magda::engine::EngineDevice> createDevice(magda::engine::DeviceKey) override {
        return std::make_unique<NullDevice>();
    }

    std::unique_ptr<magda::engine::EngineMidiSource> createMidiInput(TrackId) override {
        return midi_ == nullptr ? nullptr : std::make_unique<ScriptedMidiSource>(midi_);
    }

    std::vector<ParamKey> valuesToTap() override {
        return wanted_;
    }

  private:
    std::vector<ParamKey> wanted_;
    ScriptedMidi* midi_ = nullptr;
};

/// A session over @p trackList, having asked for @p wanted to be read back.
struct Session {
    Session(std::vector<TrackInfo> trackList, std::vector<ParamKey> wanted, ScriptedMidi* midi)
        : tracks(std::move(trackList)), factory(std::move(wanted), midi), session(factory) {
        master = makeMaster();
        published = publish(tracks);
    }

    /// Publish @p trackList as the project, which is what an edit amounts to.
    bool publish(const std::vector<TrackInfo>& trackList) {
        return publishWithLanes(trackList, {});
    }

    /// The same, with the automation the project has. A lane is the only way a
    /// mixer value reaches the parameter table at all.
    bool publishWithLanes(const std::vector<TrackInfo>& trackList,
                          std::span<const AutomationLaneInfo> lanes) {
        const magda::engine::RenderContext context{kSampleRate, kBlock, 2};
        auto result = magda::test::publishProject(session, trackList, master, context, lanes);
        plan = std::move(result.plan);
        return result.published;
    }

    void render() {
        juce::AudioBuffer<float> output(2, kBlock);
        session.process(kBlock, output);
    }

    /// Values the live plan found somewhere to publish from, which is not the
    /// number of taps: a key the table does not carry has one and is never
    /// written.
    int boundValueTaps() const {
        return session.boundValueTapCount();
    }

    /// What a window would do: ask for the tap by the key it asked to have
    /// tapped, and read it.
    ValueTap* tapFor(const ParamKey& key) const {
        return session.valueTap(key);
    }

    float read(const ParamKey& key) const {
        auto* tap = tapFor(key);
        REQUIRE(tap != nullptr);
        return tap->value();
    }

    std::uint32_t writes(const ParamKey& key) const {
        auto* tap = tapFor(key);
        REQUIRE(tap != nullptr);
        return tap->read().writes;
    }

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::shared_ptr<const magda::engine::RenderPlan> plan;
    Factory factory;
    magda::engine::EngineSession session;
    bool published = false;
};

}  // namespace

// --- the tap itself

TEST_CASE("A value tap holds what the last block published", "[engine][tap][value]") {
    ValueTap tap;
    tap.write(0.25f);
    tap.write(0.75f);

    CHECK(tap.value() == approx(0.75f));
}

TEST_CASE("Reading a value tap does not consume it", "[engine][tap][value]") {
    ValueTap tap;
    tap.write(0.4f);

    // The difference between this and a meter, and the reason it is not one: a
    // position is where the value is, so two readers are owed the same answer
    // and a reader arriving late is owed the current one rather than a zero.
    CHECK(tap.read().value == approx(0.4f));
    CHECK(tap.read().value == approx(0.4f));
    CHECK(tap.value() == approx(0.4f));
}

TEST_CASE("A value tap counts the blocks that published it", "[engine][tap][value]") {
    ValueTap tap;
    REQUIRE(tap.read().writes == 0);

    tap.write(0.5f);
    const auto first = tap.read().writes;

    // The same value again is still a block: what the count answers is whether
    // anything is rendering, and a tap that fell silent on a value that had
    // stopped moving would be indistinguishable from one whose engine had gone
    // away.
    tap.write(0.5f);
    const auto second = tap.read().writes;

    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(tap.value() == approx(0.5f));
}

TEST_CASE("A value tap's count belongs to the value beside it", "[engine][tap][value]") {
    ValueTap tap;

    // The pair is what a reader gates a repaint on, so the two halves coming
    // from different blocks is not a stale frame: a count that has moved past
    // the value it arrived with means the reader draws the older one and then
    // decides, at the next poll, that nothing has changed since. The value it
    // never drew would stay undrawn until something else wrote.
    for (std::uint32_t written = 1; written <= 8; ++written) {
        tap.write(static_cast<float>(written) / 8.0f);

        const auto reading = tap.read();
        CHECK(reading.writes == written);
        CHECK(reading.value == approx(static_cast<float>(reading.writes) / 8.0f));
    }
}

TEST_CASE("A value tap's count wraps past its maximum without passing through zero",
          "[engine][tap][value]") {
    // Zero is the reading that says nothing in the engine publishes this value
    // and the model's own is the answer, so a count that reached it by counting
    // would hand a host a live parameter wearing that sign. Thirty-three days
    // at 96 kHz and 64 samples a block gets there, which is a rig left running
    // rather than a hypothetical, and is not a number a test can render its way
    // to: the rule is asserted where it is decided.
    STATIC_REQUIRE(ValueTap::nextWriteCount(0) == 1);
    STATIC_REQUIRE(ValueTap::nextWriteCount(41) == 42);
    STATIC_REQUIRE(ValueTap::nextWriteCount(0xFFFFFFFEU) == 0xFFFFFFFFU);
    STATIC_REQUIRE(ValueTap::nextWriteCount(0xFFFFFFFFU) == 1);
}

TEST_CASE("A value tap that has published nothing reads as nothing", "[engine][tap][value]") {
    const ValueTap tap;

    const auto reading = tap.read();
    CHECK(reading.writes == 0);
    CHECK(reading.value == approx(0.0f));
}

// --- what the engine publishes through them

TEST_CASE("A modifier tap publishes the modifier's own output", "[engine][tap][value]") {
    const auto track = trackWithEnvelope(/*storedValue=*/0.0f, /*amount=*/1.0f);
    const auto mod = modifierKey(track.mods[0].id);

    ScriptedMidi midi;
    Session session({track}, {mod}, &midi);
    REQUIRE(session.published);

    // Shut before the note: a MIDI-triggered envelope waits, and a modifier at
    // rest outputs nothing.
    session.render();
    CHECK(session.read(mod) == approx(0.0f));

    midi.pending.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    session.render();
    CHECK(session.read(mod) == approx(1.0f));
}

TEST_CASE("A parameter tap publishes what the block resolved, not what is stored",
          "[engine][tap][value]") {
    // Half a link's worth of envelope on top of a stored quarter: two numbers
    // that cannot be mistaken for each other, and neither of them is what the
    // model holds.
    const auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);
    const auto param = deviceParam(0);
    const auto mod = modifierKey(track.mods[0].id);

    ScriptedMidi midi;
    Session session({track}, {param, mod}, &midi);
    REQUIRE(session.published);

    // Both found a home: the parameter in the table, the modifier in the
    // runtime beside it.
    CHECK(session.boundValueTaps() == 2);

    session.render();
    CHECK(session.read(param) == approx(0.25f));
    CHECK(session.read(mod) == approx(0.0f));

    midi.pending.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    session.render();

    // The parameter carries the link's depth and the modifier does not: what a
    // knob draws is where the value went, and what a modifier editor draws is
    // the shape the modifier made, whatever any link did with it.
    CHECK(session.read(param) == approx(0.75f));
    CHECK(session.read(mod) == approx(1.0f));
}

TEST_CASE("A value nobody asked about publishes nothing", "[engine][tap][value]") {
    const auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);

    // The host asks for the modifier and not for the parameter, which is a
    // modifier editor open over a device panel that is shut.
    const auto mod = modifierKey(track.mods[0].id);

    ScriptedMidi midi;
    Session session({track}, {mod}, &midi);
    REQUIRE(session.published);

    session.render();

    CHECK(session.tapFor(mod) != nullptr);
    CHECK(session.tapFor(deviceParam(0)) == nullptr);
}

TEST_CASE("A value the table does not carry is bound to nothing rather than refused",
          "[engine][tap][value]") {
    const auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);

    // A parameter index the device does not have, which is a window that
    // outlived the thing it was opened on. The publish goes through and the tap
    // exists; what it never gets is a block.
    const auto missing = deviceParam(99);

    ScriptedMidi midi;
    Session session({track}, {missing}, &midi);
    REQUIRE(session.published);

    session.render();
    session.render();

    auto* tap = session.tapFor(missing);
    REQUIRE(tap != nullptr);
    CHECK(tap->read().writes == 0);
    CHECK(session.boundValueTaps() == 0);
}

TEST_CASE("A block with no table it fits leaves the taps holding", "[engine][tap][value]") {
    const auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);
    const auto param = deviceParam(0);

    ScriptedMidi midi;
    Session session({track}, {param}, &midi);
    REQUIRE(session.published);

    midi.pending.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    session.render();
    REQUIRE(session.read(param) == approx(0.75f));

    // Values with no table at all, which is what a publish resolved before
    // there was one looks like. The block resolves nothing, so it publishes
    // nothing: a watched knob slammed to zero for the block a mismatched table
    // takes to be replaced would be a visible fault reporting an invisible one.
    magda::engine::PlanValues empty;
    magda::engine::resolvePlanValues(*session.plan, session.tracks, session.master, empty);
    empty.params.reset();
    REQUIRE(session.session.publishValues(std::move(empty)).published);

    const auto before = session.writes(param);
    session.render();

    CHECK(session.read(param) == approx(0.75f));
    CHECK(session.writes(param) == before);
}

// --- what the store does with them

TEST_CASE("A value tap outlives the plans that reference it", "[engine][tap][value]") {
    auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);
    const auto param = deviceParam(0);

    ScriptedMidi midi;
    Session session({track}, {param}, &midi);
    REQUIRE(session.published);

    midi.pending.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    session.render();

    auto* before = session.tapFor(param);
    REQUIRE(before != nullptr);
    const auto reading = before->read();

    // An edit somewhere else in the project: a second track, which recompiles
    // the plan and republishes the table without touching this parameter.
    auto tracks = session.tracks;
    tracks.push_back(makeTrack(2));
    REQUIRE(session.publish(tracks));

    // The same tap, still holding what the last block put in it. A new one
    // would read as the value having jumped to zero on an edit that did not
    // touch it.
    CHECK(session.tapFor(param) == before);
    CHECK(before->read().value == approx(reading.value));
    CHECK(before->read().writes == reading.writes);

    // And still being written, which keeping the object does not prove: a
    // republish that left the map alone and stopped re-binding would freeze
    // every open window from the first edit onwards and pass everything above.
    session.render();
    CHECK(before->read().writes > reading.writes);
    CHECK(before->read().value == approx(0.75f));
}

TEST_CASE("A value the engine stops publishing goes back to publishing nothing",
          "[engine][tap][value]") {
    // The lane is what puts a fader in the parameter table at all: a mixer
    // value is carried only while something reaches it. So this is a fader that
    // is automated, watched, and then has its automation taken away.
    auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);
    const auto fader = trackVolume(kTrack);

    AutomationLaneInfo lane;
    lane.id = 1;
    lane.target = ControlTarget::trackVolume(kTrack);
    lane.type = AutomationLaneType::Absolute;
    lane.authorityState = AutomationAuthorityState::Reading;
    lane.absolutePoints = {AutomationPoint{1, 0.0, 0.4}, AutomationPoint{2, 4.0, 0.4}};

    const std::vector<AutomationLaneInfo> lanes{lane};

    ScriptedMidi midi;
    Session session({track}, {fader}, &midi);
    REQUIRE(session.published);
    REQUIRE(session.publishWithLanes(session.tracks, lanes));

    session.render();
    const auto automated = session.writes(fader);
    REQUIRE(automated > 0);

    // The lane goes. The fader leaves the table with it, so nothing publishes
    // the value any more and the tap has to say so: left counting from before,
    // it would hold the last automated position under a stalled count, which
    // this header tells a host to read as an engine that has stopped.
    REQUIRE(session.publish(session.tracks));
    session.render();

    CHECK(session.writes(fader) == 0);
    CHECK(session.read(fader) == approx(0.0f));
}

TEST_CASE("A value tap goes when what it names does", "[engine][tap][value]") {
    const auto track = trackWithEnvelope(/*storedValue=*/0.25f, /*amount=*/0.5f);
    const auto param = deviceParam(0);

    ScriptedMidi midi;
    Session session({track}, {param, trackVolume(kTrack)}, &midi);
    REQUIRE(session.published);
    session.render();

    REQUIRE(session.tapFor(param) != nullptr);
    REQUIRE(session.tapFor(trackVolume(kTrack)) != nullptr);

    // The device is deleted. The host is still asking for its parameter,
    // because the window it had open has not been told yet; what stops the tap
    // from being remade is that the table no longer carries the key and the
    // model no longer holds the device.
    auto stripped = session.tracks;
    stripped[0].chain.fxChainElements.clear();
    REQUIRE(session.publish(stripped));

    CHECK(session.tapFor(param) == nullptr);

    // The track is still there, so its own value is still tapped: eviction
    // follows what the model holds rather than what the plan happens to use.
    CHECK(session.tapFor(trackVolume(kTrack)) != nullptr);
}
