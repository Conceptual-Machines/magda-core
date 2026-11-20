// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanHandoff.hpp"

/// @file An instrument moving between the ahead side and the callback as a selection plays it.

using namespace magda;
using namespace magda::engine;

namespace {

constexpr int kBlockSize = 64;
constexpr double kRate = 44100.0;
constexpr TrackId kSynth = 3;

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Media) {
    TrackInfo track;
    track.id = id;
    track.type = type;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID, TrackType::Master);
    master.audioOutputDevice = {};
    return master;
}

TrackInfo makeSynth() {
    auto synth = makeTrack(kSynth);
    DeviceInfo instrument;
    instrument.id = 30;
    instrument.name = "Synth";
    instrument.deviceType = DeviceType::Instrument;
    instrument.isInstrument = true;
    synth.chain.fxChainElements.push_back(makeDeviceElement(instrument));
    return synth;
}

/// What the instrument heard, on the monotonic sample it heard it.
struct Heard {
    std::vector<std::int64_t> noteOns;
    std::vector<std::int64_t> keyOffs;  ///< note-offs for the held key
    std::vector<std::int64_t> panics;
};

constexpr int kHeldKey = 60;

class NoteLog final : public EngineDevice {
  public:
    explicit NoteLog(Heard& heard) : heard_(heard) {}

    void process(DeviceBlock& block) override {
        const auto start = block.block.monotonicSamples.start.sample;
        if (block.midiInAllNotesOff)
            heard_.panics.push_back(start);
        if (block.midiIn == nullptr)
            return;
        for (const auto metadata : *block.midiIn) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn() && message.getNoteNumber() != kHeldKey)
                heard_.noteOns.push_back(start + metadata.samplePosition);
            else if (message.isNoteOff() && message.getNoteNumber() == kHeldKey)
                heard_.keyOffs.push_back(start + metadata.samplePosition);
        }
    }

  private:
    Heard& heard_;
};

/// A keyboard whose one key goes down on the first block it plays and stays down.
class HeldKey final : public EngineMidiSource {
  public:
    explicit HeldKey(bool press) : press_(press) {}

    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        if (press_ && !held_) {
            out.addEvent(juce::MidiMessage::noteOn(1, kHeldKey, 1.0f), 0);
            held_ = true;
        }
    }

    void releaseHeld(juce::MidiBuffer& out) override {
        if (std::exchange(held_, false))
            out.addEvent(juce::MidiMessage::noteOff(1, kHeldKey), 0);
    }

  private:
    bool press_;
    bool held_ = false;
};

class Factory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineDevice> createDevice(DeviceKey) override {
        return std::make_unique<NoteLog>(heard);
    }
    std::unique_ptr<EngineMidiSource> createClipMidiSource(TrackId trackId) override {
        return std::make_unique<ClipMidiSource>(trackId, *clips);
    }
    std::unique_ptr<EngineMidiSource> createMidiInput(TrackId) override {
        return std::make_unique<HeldKey>(pressKey);
    }
    Heard heard;
    ClipSnapshotFeed* clips = nullptr;
    bool pressKey = false;
};

/// A note on every beat for @p beats beats.
std::shared_ptr<const ClipSnapshot> everyBeat(int beats) {
    ClipInfo clip;
    clip.id = 1;
    clip.trackId = kSynth;
    clip.view = ClipView::Arrangement;
    clip.setMidiContent();
    clip.setPlacementBeats(0.0, beats);
    for (int beat = 0; beat < beats; ++beat)
        clip.midiNotes.push_back(MidiNote{36, 100, static_cast<double>(beat), 0.25, 0, {}});
    return std::make_shared<const ClipSnapshot>(
        compileClipSnapshot({ClipLane{kSynth, {clip}}}, {},
                            magda::engine::TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}})));
}

struct Rig {
    Factory factory;
    EngineSession session{factory};

    explicit Rig(bool played = false, bool pressKey = false) {
        factory.clips = &session.clipFeed();
        factory.pressKey = pressKey;
        session.setRenderAhead(3, false);
        session.publishClips(everyBeat(64));
        publish(played);
        TransportSnapshot transport;
        transport.request.generation = 1;
        transport.request.playing = true;
        session.publishTransport(transport);
    }

    void publish(bool played) {
        const std::vector<TrackInfo> model{makeSynth()};
        CompileOptions options;
        options.auditionMidi = true;
        options.playedInstruments = played ? std::set<TrackId>{kSynth} : std::set<TrackId>{};
        const auto plan = std::make_shared<const RenderPlan>(
            insertHandoffs(compileRenderPlan(model, makeMaster(), options)));
        PlanValues values;
        resolvePlanValues(*plan, model, makeMaster(), values);
        REQUIRE(session
                    .publish(plan, RenderContext{kRate, kBlockSize, 2},
                             collectRuntimeStateIds(model, makeMaster()), std::move(values))
                    .published);
    }

    void roll(int callbacks) {
        juce::AudioBuffer<float> out(2, kBlockSize);
        for (int i = 0; i < callbacks; ++i) {
            session.process(kBlockSize, out);
            session.renderAheadOnce();
        }
    }
};

}  // namespace

TEST_CASE("Selecting an instrument neither repeats nor drops its notes", "[engine][1898]") {
    // Moving off the track takes its live input away; a panic there cut the clip's hits.
    Rig rig;
    const int perBeat = static_cast<int>(kRate / 2.0) / kBlockSize;
    for (int move = 0; move < 12; ++move) {
        rig.roll(perBeat + (37 * move));
        rig.publish(move % 2 == 0);
    }
    rig.roll(perBeat);

    const auto& heard = rig.factory.heard;
    const std::set<std::int64_t> unique(heard.noteOns.begin(), heard.noteOns.end());
    CHECK(heard.noteOns.size() > 15);
    CHECK(unique.size() == heard.noteOns.size());
    for (const auto on : heard.noteOns)
        CHECK(on % static_cast<std::int64_t>(kRate / 2.0) == 0);
    CHECK(heard.panics.empty());
}

TEST_CASE("A tempo change neither panics nor repeats an instrument rendered ahead",
          "[engine][1898]") {
    // A tempo drag publishes a transport per step; the blocks rendered ahead at the old tempo
    // are heard rather than thrown away as a jump.
    Rig rig;
    const int perBeat = static_cast<int>(kRate / 2.0) / kBlockSize;
    rig.roll(perBeat / 2);
    for (int step = 1; step <= 10; ++step) {
        TransportSnapshot transport;
        transport.tempo = magda::engine::TempoMap({{0.0, 120.0 + 2.0 * step, 0.0f}}, {{0.0, 4, 4}});
        transport.request.generation = 1;
        transport.request.playing = true;
        rig.session.publishTransport(transport);
        // Faster than the look-ahead drains as well as slower, as a tempo drag goes.
        rig.roll(step % 3);
    }
    rig.roll(4 * perBeat);

    const auto& heard = rig.factory.heard;
    const std::set<std::int64_t> unique(heard.noteOns.begin(), heard.noteOns.end());
    CHECK(heard.noteOns.size() >= 4);
    CHECK(unique.size() == heard.noteOns.size());
    CHECK(heard.panics.empty());
    CHECK(rig.session.renderAheadDiscards() == 0);
}

TEST_CASE("A key held as the selection moves off its instrument is released, and only it",
          "[engine][1898]") {
    Rig rig(/*played=*/true, /*pressKey=*/true);
    rig.roll(100);
    REQUIRE(rig.factory.heard.keyOffs.empty());

    rig.publish(false);
    rig.roll(100);
    CHECK(rig.factory.heard.keyOffs.size() == 1);
    CHECK(rig.factory.heard.panics.empty());

    // A republish before the instrument has rendered still releases it once.
    rig.publish(true);
    rig.roll(100);
    rig.publish(false);
    rig.publish(false);
    rig.roll(100);
    CHECK(rig.factory.heard.keyOffs.size() == 2);
    CHECK(rig.factory.heard.panics.empty());
}
