// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "EngineSessionScaffold.hpp"
#include "exec/EngineSession.hpp"
#include "io/LiveOutput.hpp"
#include "transport/MidiClockOut.hpp"
#include "transport/TransportClock.hpp"

using magda::engine::LiveMidiOutput;
using magda::engine::MidiClockOut;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 44100.0;

/// 120 bpm, the default tempo: a beat is 22050 samples, a clock 918.75.
constexpr int kSamplesPerBeat = 22050;

constexpr std::uint8_t kClock = 0xf8;
constexpr std::uint8_t kStart = 0xfa;
constexpr std::uint8_t kContinue = 0xfb;
constexpr std::uint8_t kStop = 0xfc;
constexpr std::uint8_t kSongPosition = 0xf2;

struct Sent {
    std::int64_t at = 0;
    std::vector<std::uint8_t> bytes;

    bool operator==(const Sent&) const = default;
};

/** @brief A port that keeps what it is sent, timed from the start of the run. */
class RecordingPort final : public LiveMidiOutput {
  public:
    void send(int sample, const std::uint8_t* data, int size) override {
        sent.push_back({callbackStart + sample, {data, data + size}});
    }
    void sendAfterQueued(const juce::MidiMessage&) override {}

    std::int64_t callbackStart = 0;
    std::vector<Sent> sent;

    std::vector<Sent> clocks() const {
        std::vector<Sent> found;
        for (const auto& message : sent)
            if (message.bytes == std::vector<std::uint8_t>{kClock})
                found.push_back(message);
        return found;
    }
    std::vector<Sent> others() const {
        std::vector<Sent> found;
        for (const auto& message : sent)
            if (message.bytes != std::vector<std::uint8_t>{kClock})
                found.push_back(message);
        return found;
    }
};

/** @brief A clock and the clock out it feeds, rendered callback by callback. */
struct Rig {
    TransportClock clock;
    MidiClockOut out;
    RecordingPort port;
    TransportSnapshot snapshot;
    std::int64_t elapsed = 0;
    std::uint64_t generation = 0;

    Rig() {
        connect(true);
    }

    void connect(bool connected) {
        snapshot.midiClock.ports[0] = &port;
        snapshot.midiClock.count = connected ? 1 : 0;
    }

    void play(double fromBeat, double countInBeats = 0.0) {
        snapshot.request = {.generation = ++generation,
                            .playing = true,
                            .positionBeat = fromBeat,
                            .countInBeats = countInBeats};
    }

    void stop() {
        snapshot.request = {.generation = ++generation, .playing = false, .locate = false};
    }

    void run(int samples, int blockSize = 512) {
        for (auto done = 0; done < samples;) {
            const auto block = std::min(blockSize, samples - done);
            port.callbackStart = elapsed;
            for (const auto& segment : clock.advance(snapshot, kSampleRate, block))
                out.render(segment, snapshot.midiClock);
            elapsed += block;
            done += block;
        }
    }
};

Sent message(std::int64_t at, std::vector<std::uint8_t> bytes) {
    return {at, std::move(bytes)};
}

}  // namespace

TEST_CASE("A start from the top sends Start and 24 clocks a beat", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.play(0.0);
    rig.run(kSamplesPerBeat * 2);

    CHECK(rig.port.others() == std::vector<Sent>{message(0, {kStart})});
    REQUIRE(rig.port.sent.size() > 1);
    CHECK(rig.port.sent[1] == message(0, {kClock}));

    const auto clocks = rig.port.clocks();
    REQUIRE(clocks.size() == 48);
    CHECK(clocks[1].at == 918);
    CHECK(clocks[4].at == 3675);
    CHECK(clocks[24].at == kSamplesPerBeat);
}

TEST_CASE("A start mid-bar is placed on the next sixteenth", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.play(1.1);
    rig.run(kSamplesPerBeat);

    // Sixteenth 5, a quarter of a beat on: 0.15 beat after the start.
    const auto placed = static_cast<std::int64_t>(0.15 * kSamplesPerBeat);
    CHECK(rig.port.others() ==
          std::vector<Sent>{message(placed, {kSongPosition, 5, 0}), message(placed, {kContinue})});
    const auto clocks = rig.port.clocks();
    REQUIRE(!clocks.empty());
    CHECK(clocks.front().at == placed);
}

TEST_CASE("A Song Position past 128 sixteenths uses both bytes", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.play(100.0);
    rig.run(512);
    CHECK(rig.port.others() == std::vector<Sent>{message(0, {kSongPosition, 400 & 0x7f, 400 >> 7}),
                                                 message(0, {kContinue})});
}

TEST_CASE("Stopping sends Stop once, and no clocks after it", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.play(0.0);
    rig.run(kSamplesPerBeat);
    const auto clocksBefore = rig.port.clocks().size();

    rig.stop();
    rig.run(kSamplesPerBeat);
    CHECK(rig.port.clocks().size() == clocksBefore);
    CHECK(rig.port.others() ==
          std::vector<Sent>{message(0, {kStart}), message(kSamplesPerBeat, {kStop})});
}

TEST_CASE("A loop wrap tells the port where it is without stopping it",
          "[engine][midi_clock][3190]") {
    Rig rig;
    rig.snapshot.loop = {.enabled = true, .startBeat = 0.0, .endBeat = 1.0};
    rig.play(0.0);
    rig.run(kSamplesPerBeat * 2);

    CHECK(rig.port.others() ==
          std::vector<Sent>{message(0, {kStart}), message(kSamplesPerBeat, {kSongPosition, 0, 0})});
    CHECK(rig.port.clocks().size() == 48);
}

TEST_CASE("A count-in sends nothing until the timeline rolls", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.play(0.0, 4.0);
    rig.run(kSamplesPerBeat * 5);

    CHECK(rig.port.others() == std::vector<Sent>{message(4 * kSamplesPerBeat, {kStart})});
    CHECK(rig.port.clocks().size() == 24);
}

TEST_CASE("A port connected mid-play joins on the next sixteenth", "[engine][midi_clock][3190]") {
    Rig rig;
    rig.connect(false);
    rig.play(0.0);
    rig.run(1000);
    REQUIRE(rig.port.sent.empty());

    rig.connect(true);
    rig.run(kSamplesPerBeat);

    // The first sixteenth after sample 1000 is at 5512.5.
    CHECK(rig.port.others() ==
          std::vector<Sent>{message(5512, {kSongPosition, 1, 0}), message(5512, {kContinue})});
    CHECK(rig.port.clocks().front().at == 5512);
}

TEST_CASE("Where the clocks land does not depend on how the blocks were cut",
          "[engine][midi_clock][3190]") {
    Rig whole;
    Rig cut;
    for (auto* rig : {&whole, &cut})
        rig->play(0.3);
    whole.run(kSamplesPerBeat * 3, 4096);
    cut.run(kSamplesPerBeat * 3, 37);

    CHECK(whole.port.sent == cut.port.sent);
}

TEST_CASE("A session sends clock to the ports its transport names", "[engine][midi_clock][3190]") {
    magda::engine::RuntimeStateFactory factory;
    magda::engine::EngineSession session(factory);
    const auto master = magda::test::makeMaster();
    REQUIRE(magda::test::publishProject(session, {}, master, {kSampleRate, 512, 2}).published);

    RecordingPort port;
    TransportSnapshot snapshot;
    snapshot.request = {.generation = 1, .playing = true, .positionBeat = 0.0};
    snapshot.midiClock.ports[0] = &port;
    snapshot.midiClock.count = 1;
    session.publishTransport(snapshot);

    juce::AudioBuffer<float> output(2, 512);
    for (auto block = 0; block < 44; ++block) {
        port.callbackStart = block * 512;
        session.process(512, output);
    }

    CHECK(port.others() == std::vector<Sent>{message(0, {kStart})});
    CHECK(port.clocks().size() == 25);
}
