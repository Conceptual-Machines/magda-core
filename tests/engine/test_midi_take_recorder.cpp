#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/MidiTakeRecorder.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TransportClock.hpp"
#include "transport/TransportState.hpp"

/**
 * @file test_midi_take_recorder.cpp
 * @brief A MIDI take, from armed to clip (#2462).
 *
 * A scheduled input and a driven transport, offline: an event is named by the
 * arrival it was played on.
 */

using magda::MidiCurveType;
using magda::engine::kAnyLiveMidiSource;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::LiveMidiInput;
using magda::engine::LiveMidiStream;
using magda::engine::LoopRange;
using magda::engine::MidiTakeRecorder;
using magda::engine::MidiTakeRecorderSettings;
using magda::engine::RecordedMidiTake;
using magda::engine::TempoMap;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 8000.0;
constexpr int kBlockSize = 64;

/// A beat at 120 bpm and this rate: every position here is a whole number of
/// samples.
constexpr int kBeatSamples = 4000;

TempoMap flat() {
    return TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
}

/// 120 bpm to beat 2, then 60 bpm. Two changes on one beat is a step, not a ramp.
TempoMap halvedAtBeatTwo() {
    return TempoMap({{0.0, 120.0, 0.0f}, {2.0, 120.0, 0.0f}, {2.0, 60.0, 0.0f}}, {{0.0, 4, 4}});
}

/// One event, named by the arrival it was played on.
struct Played {
    std::int64_t arrival = 0;
    juce::MidiMessage message;
};

Played noteOn(std::int64_t arrival, int note, int velocity = 100, int channel = 1) {
    return {arrival, juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity))};
}

Played noteOff(std::int64_t arrival, int note, int channel = 1) {
    return {arrival, juce::MidiMessage::noteOff(channel, note)};
}

Played controller(std::int64_t arrival, int number, int value, int channel = 1) {
    return {arrival, juce::MidiMessage::controllerEvent(channel, number, value)};
}

Played pitchBend(std::int64_t arrival, int value, int channel = 1) {
    return {arrival, juce::MidiMessage::pitchWheel(channel, value)};
}

/// What the track's instrument was handed.
struct Heard {
    std::int64_t sample = 0;
    int status = 0;
    int data1 = 0;
    int data2 = 0;

    bool operator==(const Heard&) const = default;
};

MidiTakeRecorderSettings takeOf(int latencySamples = 0) {
    MidiTakeRecorderSettings settings;
    settings.source = kAnyLiveMidiSource;
    settings.latencySamples = latencySamples;
    return settings;
}

/**
 * @brief A transport, a scheduled MIDI input and one take, a callback at a time.
 *
 * The monitor input renders every block whether or not a take is recording.
 */
class Rig {
  public:
    explicit Rig(MidiTakeRecorderSettings settings, TempoMap tempo = flat(), bool records = true)
        : monitor_(feed_, kAnyLiveMidiSource) {
        transport_.tempo = std::move(tempo);
        feed_.prepare(0, kBlockSize);

        if (records)
            recorder_ = std::make_unique<MidiTakeRecorder>(feed_, std::move(settings));
    }

    void schedule(std::vector<Played> events) {
        schedule_ = std::move(events);
    }

    void play(double fromBeat = 0.0, double countInBeats = 0.0) {
        ++transport_.request.generation;
        transport_.request.playing = true;
        transport_.request.locate = true;
        transport_.request.positionBeat = fromBeat;
        transport_.request.countInBeats = countInBeats;
    }

    void stop() {
        ++transport_.request.generation;
        transport_.request.playing = false;
        transport_.request.locate = false;
        transport_.request.countInBeats = 0.0;
    }

    void loop(double startBeat, double endBeat) {
        transport_.loop = LoopRange{true, startBeat, endBeat};
    }

    /// Feed @p numSamples of callbacks through the take.
    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(kBlockSize, left);
            deliver(callback);
            left -= callback;
        }
    }

    /// Empty the queue after every callback, the way the record thread would.
    void drainAsItGoes() {
        drains_ = true;
    }

    MidiTakeRecorder& recorder() {
        return *recorder_;
    }

    RecordedMidiTake finish() {
        return recorder_->finish(transport_.tempo);
    }

    const std::vector<Heard>& heard() const {
        return heard_;
    }

  private:
    void deliver(int numSamples) {
        juce::MidiBuffer arriving;
        for (const auto& event : schedule_)
            if (event.arrival >= arrival_ && event.arrival < arrival_ + numSamples)
                arriving.addEvent(event.message, static_cast<int>(event.arrival - arrival_));

        const std::array streams{LiveMidiStream{0, &arriving}};
        const LiveInputBlock block{{}, streams};

        feed_.beginCallback(block, numSamples);

        for (const auto& segment : clock_.advance(transport_, kSampleRate, numSamples)) {
            feed_.beginSegment(segment.startSample, segment.block.numSamples);

            if (recorder_ != nullptr)
                recorder_->capture(segment.block, segment.countingIn, transport_.loop);

            juce::MidiBuffer monitored;
            monitor_.render(segment.block, monitored);

            for (const auto metadata : monitored) {
                const auto message = metadata.getMessage();
                heard_.push_back(Heard{arrival_ + segment.startSample + metadata.samplePosition,
                                       message.getRawData()[0], message.getRawData()[1],
                                       message.getRawDataSize() > 2 ? message.getRawData()[2] : 0});
            }
        }

        feed_.endCallback();
        arrival_ += numSamples;

        if (drains_ && recorder_ != nullptr)
            while (recorder_->stream().drain()) {
            }
    }

    TransportSnapshot transport_;
    TransportClock clock_;
    LiveInputFeed feed_;
    LiveMidiInput monitor_;
    std::unique_ptr<MidiTakeRecorder> recorder_;

    std::vector<Played> schedule_;
    std::vector<Heard> heard_;

    /// Input samples delivered so far, which the schedule is numbered by.
    std::int64_t arrival_ = 0;

    bool drains_ = false;
};

}  // namespace

TEST_CASE("A note lands on the beat it was played on", "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({noteOn(kBeatSamples, 60), noteOff(kBeatSamples * 3 / 2, 60)});
    rig.play();
    rig.run(kBeatSamples * 2);

    const auto take = rig.finish();
    REQUIRE_FALSE(take.empty());
    CHECK(take.eventsLost == 0);
    CHECK(take.messagesDropped == 0);

    REQUIRE(take.active.notes.size() == 1);
    CHECK(take.active.notes[0].noteNumber == 60);
    CHECK(take.active.notes[0].velocity == 100);
    CHECK(take.active.notes[0].startBeat == Catch::Approx(1.0));
    CHECK(take.active.notes[0].lengthBeats == Catch::Approx(0.5));

    // A single pass is an ordinary clip, with no take list.
    CHECK(take.clip.takes.empty());
    CHECK(take.startBeat == 0.0);
    CHECK(take.lengthBeats == Catch::Approx(2.0));
}

TEST_CASE("A note recorded across a tempo change lands where it was played",
          "[engine][io][record][midi][2462]") {
    // Beat 3 is two beats at 120 and one at 60: one second, then one more.
    constexpr int kBeatThree = 16000;

    Rig rig(takeOf(), halvedAtBeatTwo());
    rig.schedule({noteOn(kBeatThree, 64), noteOff(kBeatThree + 8000, 64)});
    rig.play();
    rig.run(kBeatThree + 8000);

    const auto take = rig.finish();
    REQUIRE(take.active.notes.size() == 1);
    CHECK(take.active.notes[0].startBeat == Catch::Approx(3.0));

    // A beat is 8000 samples on this side of the change, so the note is one.
    CHECK(take.active.notes[0].lengthBeats == Catch::Approx(1.0));
}

TEST_CASE("A note still held when the take ends lasts until it does",
          "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({noteOn(kBeatSamples, 62)});
    rig.play();
    rig.run(kBeatSamples * 2);

    const auto take = rig.finish();
    REQUIRE(take.active.notes.size() == 1);
    CHECK(take.active.notes[0].startBeat == Catch::Approx(1.0));
    CHECK(take.active.notes[0].lengthBeats == Catch::Approx(1.0));
}

TEST_CASE("A note held across a loop wrap belongs to one pass, once",
          "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    Rig rig(takeOf());
    rig.loop(0.0, 4.0);
    // Down three quarters of a beat before the wrap, up a quarter beat after it.
    rig.schedule({noteOn(kLoopSamples - 3000, 67), noteOff(kLoopSamples + 1000, 67)});
    rig.play();
    rig.run(kLoopSamples * 2);

    const auto take = rig.finish();
    REQUIRE(take.clip.takes.size() == 2);

    REQUIRE(take.clip.takes[0].notes.size() == 1);
    CHECK(take.clip.takes[0].notes[0].startBeat == Catch::Approx(3.25));

    // Cut off at the pass end rather than running past it.
    CHECK(take.clip.takes[0].notes[0].lengthBeats == Catch::Approx(0.75));

    // The note off has no note on in this pass, and starts nothing.
    CHECK(take.clip.takes[1].notes.empty());
}

TEST_CASE("Each loop pass is a take, and the clip is loop-aligned",
          "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    Rig rig(takeOf());
    rig.loop(0.0, 4.0);
    rig.schedule({
        noteOn(kBeatSamples, 60),
        noteOff(kBeatSamples + 500, 60),
        noteOn(kLoopSamples + kBeatSamples, 62),
        noteOff(kLoopSamples + kBeatSamples + 500, 62),
        noteOn((kLoopSamples * 2) + kBeatSamples, 64),
        noteOff((kLoopSamples * 2) + kBeatSamples + 500, 64),
    });
    rig.play();
    rig.run(kLoopSamples * 3);

    const auto take = rig.finish();
    REQUIRE(take.clip.takes.size() == 3);

    // Every pass positions its notes against its own start: take 0 at beat 0.
    for (const auto& pass : take.clip.takes) {
        REQUIRE(pass.notes.size() == 1);
        CHECK(pass.notes[0].startBeat == Catch::Approx(1.0));
    }

    CHECK(take.clip.takes[0].notes[0].noteNumber == 60);
    CHECK(take.clip.takes[1].notes[0].noteNumber == 62);
    CHECK(take.clip.takes[2].notes[0].noteNumber == 64);

    CHECK(take.startBeat == Catch::Approx(0.0));
    CHECK(take.lengthBeats == Catch::Approx(4.0));
    CHECK(take.passesLost == 0);
}

TEST_CASE("The active take is the last full pass", "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    SECTION("three full passes play the third") {
        Rig rig(takeOf());
        rig.loop(0.0, 4.0);
        rig.schedule({noteOn(100, 60), noteOff(200, 60)});
        rig.play();
        rig.run(kLoopSamples * 3);

        const auto take = rig.finish();
        REQUIRE(take.clip.takes.size() == 3);
        CHECK(take.clip.currentTakeIndex == 2);
    }

    SECTION("a final pass cut short falls back to the one before it") {
        Rig rig(takeOf());
        rig.loop(0.0, 4.0);
        rig.schedule({noteOn(100, 60), noteOff(200, 60)});
        rig.play();
        rig.run((kLoopSamples * 2) + (kLoopSamples / 4));

        const auto take = rig.finish();
        REQUIRE(take.clip.takes.size() == 3);
        CHECK(take.clip.currentTakeIndex == 1);

        // The short pass is still a take, just not the one that plays.
        CHECK(take.active.notes.size() == take.clip.takes[1].notes.size());
    }
}

TEST_CASE("A first pass that did not start on the loop is a lead-in, not a take",
          "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    Rig rig(takeOf());
    rig.loop(0.0, 4.0);
    rig.schedule({
        noteOn(500, 60),
        noteOff(1000, 60),
        noteOn(kBeatSamples * 3, 62),
        noteOff((kBeatSamples * 3) + 500, 62),
    });
    // Two beats in, so the first stretch cannot share the clip start.
    rig.play(2.0);
    rig.run(kLoopSamples * 2);

    const auto take = rig.finish();

    // Three stretches recorded, the lead-in dropped.
    REQUIRE(take.clip.takes.size() == 2);
    CHECK(take.startBeat == Catch::Approx(0.0));
    CHECK(take.lengthBeats == Catch::Approx(4.0));

    // The lead-in's own note is gone with it; what is left starts at the loop.
    for (const auto& pass : take.clip.takes)
        for (const auto& note : pass.notes)
            CHECK(note.noteNumber != 60);
}

TEST_CASE("A take starts where the count-in ends", "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({noteOn(kBeatSamples, 60), noteOff(kBeatSamples + 500, 60),
                  noteOn(kBeatSamples * 3, 64), noteOff((kBeatSamples * 3) + 500, 64)});
    rig.play(0.0, 2.0);
    rig.run(kBeatSamples * 4);

    const auto take = rig.finish();

    // A count-in is time before the play position, so it is not in the take.
    REQUIRE(take.active.notes.size() == 1);
    CHECK(take.active.notes[0].noteNumber == 64);
    CHECK(take.active.notes[0].startBeat == Catch::Approx(1.0));
    CHECK(take.startBeat == Catch::Approx(0.0));
}

TEST_CASE("An event is placed where it was played, not where it arrived",
          "[engine][io][record][midi][2462]") {
    SECTION("a positive latency moves it earlier and drops what precedes the take") {
        Rig rig(takeOf(800));
        rig.schedule({noteOn(400, 60), noteOff(600, 60), noteOn(kBeatSamples + 800, 64),
                      noteOff(kBeatSamples + 1300, 64)});
        rig.play();
        rig.run(kBeatSamples * 2);

        const auto take = rig.finish();
        REQUIRE(take.active.notes.size() == 1);
        CHECK(take.active.notes[0].noteNumber == 64);
        CHECK(take.active.notes[0].startBeat == Catch::Approx(1.0));
    }

    SECTION("a negative adjustment moves it later") {
        Rig rig(takeOf(-800));
        rig.schedule({noteOn(kBeatSamples, 64), noteOff(kBeatSamples + 500, 64)});
        rig.play();
        rig.run(kBeatSamples * 2);

        const auto take = rig.finish();
        REQUIRE(take.active.notes.size() == 1);
        CHECK(take.active.notes[0].startBeat == Catch::Approx(1.2));
    }
}

TEST_CASE("A negative adjustment moves an event across a pass boundary, not the boundary",
          "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    Rig rig(takeOf(-800));
    rig.loop(0.0, 4.0);
    // Arrives 400 before the wrap, which is 400 after it once corrected.
    rig.schedule({noteOn(kLoopSamples - 400, 67), noteOff(kLoopSamples - 200, 67)});
    rig.play();
    rig.run(kLoopSamples * 2);

    const auto take = rig.finish();
    REQUIRE(take.clip.takes.size() == 2);
    CHECK(take.clip.takes[0].notes.empty());
    REQUIRE(take.clip.takes[1].notes.size() == 1);
    CHECK(take.clip.takes[1].notes[0].startBeat == Catch::Approx(400.0 / kBeatSamples));

    // Both passes are the loop, rather than the first running long by it.
    CHECK(take.lengthBeats == Catch::Approx(4.0));
}

TEST_CASE("CC and pitch bend survive with their positions", "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({controller(kBeatSamples, 74, 96), pitchBend(kBeatSamples * 3 / 2, 10000)});
    rig.play();
    rig.run(kBeatSamples * 2);

    const auto take = rig.finish();

    REQUIRE(take.active.cc.size() == 1);
    CHECK(take.active.cc[0].controller == 74);
    CHECK(take.active.cc[0].value == 96);
    CHECK(take.active.cc[0].beatPosition == Catch::Approx(1.0));
    CHECK(take.active.cc[0].curveType == MidiCurveType::Step);

    REQUIRE(take.active.pitchBend.size() == 1);
    CHECK(take.active.pitchBend[0].value == 10000);
    CHECK(take.active.pitchBend[0].beatPosition == Catch::Approx(1.5));
}

TEST_CASE("A message the model cannot hold is dropped where it can be said",
          "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({
        {kBeatSamples, juce::MidiMessage::programChange(1, 7)},
        {kBeatSamples + 100, juce::MidiMessage::channelPressureChange(1, 64)},
        {kBeatSamples + 200, juce::MidiMessage::aftertouchChange(1, 60, 64)},
        noteOn(kBeatSamples + 300, 60),
        noteOff(kBeatSamples + 400, 60),
    });
    rig.play();
    rig.run(kBeatSamples * 2);

    const auto take = rig.finish();
    CHECK(take.messagesDropped == 3);

    // What the model does hold is untouched by what it does not.
    CHECK(take.active.notes.size() == 1);
    CHECK(take.eventsLost == 0);
}

TEST_CASE("A note off at zero velocity ends the note", "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({noteOn(kBeatSamples, 60), noteOn(kBeatSamples + 2000, 60, 0)});
    rig.play();
    rig.run(kBeatSamples * 2);

    const auto take = rig.finish();
    REQUIRE(take.active.notes.size() == 1);
    CHECK(take.active.notes[0].lengthBeats == Catch::Approx(0.5));
}

TEST_CASE("What a take holds does not depend on when the queue was drained",
          "[engine][io][record][midi][2462]") {
    constexpr int kLoopSamples = kBeatSamples * 4;

    const auto record = [](bool drains) {
        Rig rig(takeOf());
        rig.loop(0.0, 4.0);
        if (drains)
            rig.drainAsItGoes();

        rig.schedule({noteOn(kBeatSamples, 60), noteOff(kBeatSamples + 500, 60),
                      noteOn(kLoopSamples + (kBeatSamples * 2), 62),
                      noteOff(kLoopSamples + (kBeatSamples * 2) + 500, 62)});
        rig.play();
        rig.run(kLoopSamples * 2);
        return rig.finish();
    };

    const auto queued = record(false);
    const auto drained = record(true);

    REQUIRE(queued.clip.takes.size() == drained.clip.takes.size());
    for (std::size_t pass = 0; pass < queued.clip.takes.size(); ++pass)
        CHECK(queued.clip.takes[pass] == drained.clip.takes[pass]);
}

TEST_CASE("Recording does not change what the track hears", "[engine][io][record][midi][2462]") {
    const auto listen = [](bool records) {
        Rig rig(takeOf(), flat(), records);
        rig.schedule({noteOn(kBeatSamples, 60), noteOff(kBeatSamples + 500, 60),
                      controller(kBeatSamples + 700, 74, 96)});
        rig.play();
        rig.run(kBeatSamples * 2);

        if (records)
            (void)rig.finish();

        return rig.heard();
    };

    // A take reads the feed rather than consuming it.
    CHECK(listen(true) == listen(false));
    CHECK_FALSE(listen(false).empty());
}

TEST_CASE("A take that never rolled is empty", "[engine][io][record][midi][2462]") {
    Rig rig(takeOf());
    rig.schedule({noteOn(100, 60), noteOff(200, 60)});
    rig.run(kBeatSamples);

    const auto take = rig.finish();
    CHECK(take.empty());
    CHECK(take.active.notes.empty());
    CHECK(take.clip.takes.empty());
    CHECK(take.lengthBeats == 0.0);
}
