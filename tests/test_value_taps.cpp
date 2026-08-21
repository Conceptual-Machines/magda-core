#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "param/ParamTableCompiler.hpp"
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
using magda::engine::compileRenderPlan;
using magda::engine::ParamKey;
using magda::engine::ValueTap;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

constexpr TrackId kTrack = 1;
constexpr DeviceId kDevice = 7;

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    master.volume = 1.0f;
    return master;
}

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

/// What a case queues for the next block. Owned by the case rather than by the
/// session, because the point of one is to put a note in a named block.
struct ScriptedMidi {
    juce::MidiBuffer pending;
};

class ScriptedMidiSource final : public magda::engine::EngineMidiSource {
  public:
    explicit ScriptedMidiSource(ScriptedMidi* script) : script_(script) {}

    void render(const magda::engine::BlockInfo&, juce::MidiBuffer& out) override {
        if (script_ == nullptr)
            return;
        out.addEvents(script_->pending, 0, -1, 0);
        script_->pending.clear();
    }

  private:
    ScriptedMidi* script_ = nullptr;
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
        auto compiled =
            std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(trackList, master));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*compiled, trackList, master, values);

        const auto ids = magda::engine::collectRuntimeStateIds(trackList, master);
        const magda::engine::RenderContext context{kSampleRate, kBlock, 2};
        const auto result = session.publish(compiled, context, ids, std::move(values));
        plan = std::move(compiled);
        return result.published;
    }

    void render() {
        juce::AudioBuffer<float> output(2, kBlock);
        session.process(kBlock, output);
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
