#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/MidiTakeRecorder.hpp"
#include "io/TakeNotes.hpp"
#include "io/TakeRecorder.hpp"
#include "tap/RecordTap.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TransportClock.hpp"
#include "transport/TransportState.hpp"

/**
 * @file test_record_tap.cpp
 * @brief The pass in flight, reported without a poll (#2463).
 *
 * Offline throughout: a driven transport, a scheduled input, and the tap read
 * from the same thread that fed it. What a case asserts is that the reading and
 * the take agree, which is the whole point of the tap being written by the
 * block that recorded the material.
 */

using magda::engine::AudioFileFormat;
using magda::engine::kAnyLiveMidiSource;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::LiveMidiStream;
using magda::engine::LoopRange;
using magda::engine::MidiTakeRecorder;
using magda::engine::MidiTakeRecorderSettings;
using magda::engine::PreviewNotes;
using magda::engine::RecordedMidiTake;
using magda::engine::RecordMaterial;
using magda::engine::RecordTap;
using magda::engine::RecordTapSettings;
using magda::engine::RecordTarget;
using magda::engine::RenderContext;
using magda::engine::TakeRecorder;
using magda::engine::TakeRecorderSettings;
using magda::engine::TempoMap;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 8000.0;

/// A beat at 120 bpm and this rate: every position here is a whole number of
/// samples.
constexpr int kBeatSamples = 4000;

Catch::Approx beats(double value) {
    return Catch::Approx(value).margin(1e-9);
}

TempoMap flat() {
    return TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
}

/// 120 bpm to beat 2, then 60 bpm. Two changes on one beat is a step.
TempoMap halvedAtBeatTwo() {
    return TempoMap({{0.0, 120.0, 0.0f}, {2.0, 120.0, 0.0f}, {2.0, 60.0, 0.0f}}, {{0.0, 4, 4}});
}

RecordTapSettings drawn(std::size_t maxNotes = 64, std::size_t maxPeaks = 0,
                        int samplesPerPeak = 500) {
    RecordTapSettings settings;
    settings.maxNotes = maxNotes;
    settings.maxPeaks = maxPeaks;
    settings.samplesPerPeak = samplesPerPeak;
    return settings;
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

/** @brief A transport, a scheduled MIDI input and one take, a callback at a time. */
class MidiRig {
  public:
    explicit MidiRig(RecordTapSettings tap, MidiTakeRecorderSettings settings = {},
                     TempoMap tempo = flat(), int blockSize = 64)
        : tap_(RecordMaterial::midi, tap), blockSize_(blockSize) {
        transport_.tempo = std::move(tempo);
        feed_.prepare(0, blockSize_);
        recorder_ = std::make_unique<MidiTakeRecorder>(feed_, tap_, std::move(settings));
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

    void loop(double startBeat, double endBeat) {
        transport_.loop = LoopRange{true, startBeat, endBeat};
    }

    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(blockSize_, left);
            deliver(callback);
            left -= callback;
        }
    }

    MidiTakeRecorder& recorder() {
        return *recorder_;
    }

    RecordTap::Reading reading() const {
        RecordTap::Reading into;
        REQUIRE(tap_.read(into));
        return into;
    }

    RecordedMidiTake finish() {
        return recorder_->finish(transport_.tempo);
    }

    /// What a host does once a take is a clip. The tap is not the take's.
    void releaseTake() {
        recorder_.reset();
    }

  private:
    void deliver(int numSamples) {
        juce::MidiBuffer arriving;
        for (const auto& event : schedule_)
            if (event.arrival >= arrival_ && event.arrival < arrival_ + numSamples)
                arriving.addEvent(event.message, static_cast<int>(event.arrival - arrival_));

        const std::array streams{LiveMidiStream{0, &arriving}};
        feed_.beginCallback(LiveInputBlock{{}, streams}, numSamples);

        for (const auto& segment : clock_.advance(transport_, kSampleRate, numSamples)) {
            feed_.beginSegment(segment.startSample, segment.block.numSamples);
            recorder_->capture(segment.block, segment.countingIn, transport_.loop);
        }

        feed_.endCallback();
        arrival_ += numSamples;
    }

    TransportSnapshot transport_;
    TransportClock clock_;
    LiveInputFeed feed_;
    RecordTap tap_;
    std::unique_ptr<MidiTakeRecorder> recorder_;

    std::vector<Played> schedule_;
    std::int64_t arrival_ = 0;
    int blockSize_ = 64;
};

/// What arrival @p sample of @p channel carries, as the audio take's own cases
/// number it: rising, so a window's peak is its last sample.
float material(std::int64_t sample, int channel) {
    return static_cast<float>(sample + 1 + (static_cast<std::int64_t>(channel) * 50000)) * 1.0e-5f;
}

juce::File emptyDirectory(const juce::String& name) {
    auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("magda_record_tap_test")
                         .getChildFile(name);
    directory.deleteRecursively();
    directory.createDirectory();
    return directory;
}

/** @brief A transport, a synthetic audio input and one take. */
class AudioRig {
  public:
    AudioRig(const juce::File& directory, RecordTapSettings tap, int blockSize = 64,
             int latencySamples = 0)
        : input_(2, blockSize), tap_(RecordMaterial::audio, tap), blockSize_(blockSize) {
        transport_.tempo = flat();
        feed_.prepare(2, blockSize_);

        TakeRecorderSettings settings;
        settings.channels = {0, 1};
        settings.latencySamples = latencySamples;
        settings.directory = directory;
        settings.file.format = AudioFileFormat::wav;
        settings.file.bitDepth = 32;

        recorder_ = std::make_unique<TakeRecorder>(feed_, RenderContext{kSampleRate, blockSize_, 2},
                                                   tap_, std::move(settings));
    }

    void play(double fromBeat = 0.0) {
        ++transport_.request.generation;
        transport_.request.playing = true;
        transport_.request.locate = true;
        transport_.request.positionBeat = fromBeat;
    }

    void loop(double startBeat, double endBeat) {
        transport_.loop = LoopRange{true, startBeat, endBeat};
    }

    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(blockSize_, left);
            deliver(callback);
            left -= callback;
        }
    }

    TakeRecorder& recorder() {
        return *recorder_;
    }

    RecordTap::Reading reading() const {
        RecordTap::Reading into;
        REQUIRE(tap_.read(into));
        return into;
    }

  private:
    void deliver(int numSamples) {
        for (auto channel = 0; channel < input_.getNumChannels(); ++channel)
            for (auto at = 0; at < numSamples; ++at)
                input_.setSample(channel, at, material(arrival_ + at, channel));

        const LiveInputBlock block{juce::dsp::AudioBlock<const float>(input_).getSubBlock(
                                       0, static_cast<std::size_t>(numSamples)),
                                   {}};

        feed_.beginCallback(block, numSamples);

        for (const auto& segment : clock_.advance(transport_, kSampleRate, numSamples)) {
            feed_.beginSegment(segment.startSample, segment.block.numSamples);
            recorder_->capture(segment.block, segment.countingIn, transport_.loop);
        }

        feed_.endCallback();
        arrival_ += numSamples;
    }

    TransportSnapshot transport_;
    TransportClock clock_;
    LiveInputFeed feed_;
    juce::AudioBuffer<float> input_;
    RecordTap tap_;
    std::unique_ptr<TakeRecorder> recorder_;

    std::int64_t arrival_ = 0;
    int blockSize_ = 64;
};

}  // namespace

// --- the tap on its own ---

TEST_CASE("A tap nothing has opened reports no pass", "[engine][tap][record][2463]") {
    const RecordTap tap(RecordMaterial::midi, drawn());

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    CHECK_FALSE(reading.recording);
    CHECK(reading.pass == 0);
    CHECK(reading.lengthBeats == 0.0);
    CHECK(reading.notes.empty());
}

TEST_CASE("A held note reaches the end of the pass and stops where it came up",
          "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    played.open(8.0);
    played.strike(1, 60, 100, 0.5);
    played.reaches(1.0);

    SECTION("still down, it is as long as the pass") {
        played.reaches(2.5);

        RecordTap::Reading reading;
        REQUIRE(tap.read(reading));
        REQUIRE(reading.notes.size() == 1);
        CHECK(reading.notes[0].startBeat == beats(0.5));
        CHECK(reading.notes[0].lengthBeats == beats(2.0));
        CHECK(reading.lengthBeats == beats(2.5));
    }

    SECTION("once it comes up, the pass grows past it") {
        played.release(1, 60, 1.5);
        played.reaches(2.5);

        RecordTap::Reading reading;
        REQUIRE(tap.read(reading));
        REQUIRE(reading.notes.size() == 1);
        CHECK(reading.notes[0].lengthBeats == beats(1.0));
    }
}

TEST_CASE("A pass takes the last one's notes with it", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    played.open(0.0);
    played.strike(1, 60, 100, 0.5);
    played.release(1, 60, 1.0);
    played.reaches(4.0);

    played.open(4.0);
    played.strike(1, 67, 100, 0.25);
    played.reaches(1.0);

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    CHECK(reading.pass == 2);
    CHECK(reading.startBeat == beats(4.0));
    REQUIRE(reading.notes.size() == 1);
    CHECK(reading.notes[0].noteNumber == 67);
}

TEST_CASE("A note the pass has no room for is counted", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn(2));
    PreviewNotes played(tap);
    played.open(0.0);

    for (auto note = 0; note < 5; ++note)
        played.strike(1, 60 + note, 100, static_cast<double>(note));

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    CHECK(reading.notes.size() == 2);
    CHECK(reading.notesLost == 3);
}

TEST_CASE("A closed pass keeps what it reached", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::audio, drawn());
    PreviewNotes played(tap);
    played.open(2.0);
    played.reaches(1.5);
    tap.close();

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    CHECK_FALSE(reading.recording);
    CHECK(reading.startBeat == beats(2.0));
    CHECK(reading.lengthBeats == beats(1.5));
}

TEST_CASE("A reading is one pass's own, never two", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    std::atomic<bool> writing{true};

    // Every pass is named three times over: by where it starts, by the pitch it
    // holds and by the beat every one of its notes sits on. A reading that
    // paired one pass's identity with another's coordinates would disagree with
    // itself on at least one of them.
    constexpr double kPassSpan = 1000.0;

    std::thread writer([&] {
        for (auto pass = 0; pass < 200000; ++pass) {
            const auto origin = static_cast<double>(pass) * kPassSpan;

            played.open(origin);

            for (auto note = 0; note < 8; ++note) {
                const auto at = origin + static_cast<double>(note);
                played.strike(1, 60 + (pass % 12), 100, at);
                played.release(1, 60 + (pass % 12), at + 0.25);
            }

            played.reaches(origin + 8.0);
            std::this_thread::yield();
        }

        writing.store(false);
    });

    auto settled = 0;
    auto checked = 0;
    RecordTap::Reading reading;

    while (writing.load()) {
        if (!tap.read(reading))
            continue;

        ++settled;
        if (reading.pass == 0)
            continue;

        const auto pass = static_cast<int>(reading.pass) - 1;
        const auto origin = static_cast<double>(pass) * kPassSpan;
        REQUIRE(reading.startBeat == beats(origin));

        for (const auto& note : reading.notes) {
            REQUIRE(note.noteNumber == 60 + (pass % 12));
            REQUIRE(note.startBeat >= origin);
            REQUIRE(note.startBeat < origin + 8.0);
            REQUIRE(note.lengthBeats <= beats(0.25));
            ++checked;
        }
    }

    writer.join();
    CHECK(settled > 0);
    CHECK(checked > 0);
}

TEST_CASE("A note is read as one note, not two halves of a slot", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    played.open(0.0);

    // A slot rewritten in place, one strike at a time rather than batched, so
    // what is under test is the guard on the call itself. The velocity says
    // which strike a note is and its beat says the same thing, so the two
    // disagreeing is the tear.
    std::atomic<bool> writing{true};

    std::thread writer([&] {
        for (auto strike = 0; strike < 400000; ++strike) {
            const auto velocity = 1 + (strike % 126);

            played.strike(1, 60, velocity, static_cast<double>(velocity));
            std::this_thread::yield();
        }

        writing.store(false);
    });

    auto checked = 0;
    RecordTap::Reading reading;

    while (writing.load()) {
        if (!tap.read(reading))
            continue;

        for (const auto& note : reading.notes) {
            REQUIRE(note.startBeat == beats(static_cast<double>(note.velocity)));
            ++checked;
        }
    }

    writer.join();
    CHECK(checked > 0);
}

TEST_CASE("A retrigger that restores the same note is still a replacement",
          "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    played.open(0.0);

    // One pitch at one velocity, struck over and over without coming up. Every
    // strike packs the identity the last one did, so nothing about the note
    // itself tells two of them apart. The length says which strike a beat came
    // from.
    std::atomic<bool> writing{true};

    std::thread writer([&] {
        for (auto strike = 0; strike < 400000; ++strike) {
            const auto at = static_cast<double>(strike);

            played.strike(1, 60, 100, at);
            played.reaches(at + 1.0 + static_cast<double>(strike % 7));
            std::this_thread::yield();
        }

        writing.store(false);
    });

    auto checked = 0;
    RecordTap::Reading reading;

    while (writing.load()) {
        if (!tap.read(reading))
            continue;

        for (const auto& note : reading.notes) {
            // Zero is the strike itself, before the pass has extended it.
            if (note.lengthBeats == 0.0)
                continue;

            REQUIRE(note.lengthBeats ==
                    beats(1.0 + static_cast<double>(std::llround(note.startBeat) % 7)));
            ++checked;
        }
    }

    writer.join();
    CHECK(checked > 0);
}

TEST_CASE("A pass's length belongs to the pass that is reported", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    std::atomic<bool> writing{true};

    // Each pass says which it is twice: where it starts, and a length one beat
    // past that. A wrap taken between the two is a length off the next pass
    // against the last one's start, which on an overlay is one that collapses.
    std::thread writer([&] {
        for (auto pass = 1; pass <= 200000; ++pass) {
            const auto origin = static_cast<double>(pass) * 1000.0;

            {
                const RecordTap::Change change(tap);
                played.open(origin);
                played.reaches(origin + 1.0);
            }

            std::this_thread::yield();
        }

        writing.store(false);
    });

    auto checked = 0;
    RecordTap::Reading reading;

    while (writing.load()) {
        if (!tap.read(reading) || reading.pass == 0)
            continue;

        REQUIRE(reading.lengthBeats == beats(reading.startBeat + 1.0));
        ++checked;
    }

    writer.join();
    CHECK(checked > 0);
}

TEST_CASE("A read that cannot settle leaves the reader what it had",
          "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);

    played.open(4.0);
    played.strike(1, 60, 100, 0.5);
    played.reaches(1.0);

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    REQUIRE(reading.notes.size() == 1);

    // A block still being taken. Nothing settles while one is open, and what
    // the reader already had is a better answer than half of two passes.
    const RecordTap::Change change(tap);
    played.open(8.0);

    CHECK_FALSE(tap.read(reading));
    CHECK(reading.startBeat == beats(4.0));
    REQUIRE(reading.notes.size() == 1);
    CHECK(reading.notes[0].noteNumber == 60);
}

TEST_CASE("A block's events reach a reader together", "[engine][tap][record][2463]") {
    RecordTap tap(RecordMaterial::midi, drawn());
    PreviewNotes played(tap);
    played.open(0.0);

    RecordTap::Reading reading;
    REQUIRE(tap.read(reading));
    REQUIRE(reading.notes.empty());

    {
        const RecordTap::Change change(tap);
        played.strike(1, 60, 100, 0.0);

        // Halfway through the block, and not readable as such.
        CHECK_FALSE(tap.read(reading));
        CHECK(reading.notes.empty());

        played.strike(1, 64, 100, 0.25);
    }

    REQUIRE(tap.read(reading));
    CHECK(reading.notes.size() == 2);
}

// --- the pass a MIDI take is recording ---

TEST_CASE("A note is drawn at the beat the finished clip holds it at",
          "[engine][io][record][midi][2463]") {
    const auto tempo = GENERATE_COPY(flat(), halvedAtBeatTwo());

    MidiRig rig(drawn(), {}, tempo);
    rig.schedule({noteOn(kBeatSamples, 64), noteOff(3 * kBeatSamples, 64)});
    rig.play();
    rig.run(4 * kBeatSamples);

    const auto preview = rig.reading();
    const auto take = rig.finish();

    REQUIRE(preview.notes.size() == 1);
    REQUIRE(take.active.notes.size() == 1);
    CHECK(preview.notes[0].startBeat == beats(take.active.notes[0].startBeat));
    CHECK(preview.notes[0].lengthBeats == beats(take.active.notes[0].lengthBeats));
    CHECK(preview.notes[0].noteNumber == take.active.notes[0].noteNumber);
    CHECK(preview.startBeat == beats(take.startBeat));
}

TEST_CASE("A pass reports the length it captured, whatever the block size",
          "[engine][io][record][midi][2463]") {
    const auto blockSize = GENERATE(16, 64, 100);

    MidiRig rig(drawn(), {}, flat(), blockSize);
    rig.play();
    rig.run(6 * kBeatSamples);

    const auto reading = rig.reading();
    CHECK(reading.recording);
    CHECK(reading.pass == 1);
    CHECK(reading.lengthBeats == beats(6.0));
}

TEST_CASE("An armed track with a stopped transport reports nothing",
          "[engine][io][record][midi][2463]") {
    SECTION("never played") {
        MidiRig rig(drawn());
        rig.run(2 * kBeatSamples);

        const auto reading = rig.reading();
        CHECK_FALSE(reading.recording);
        CHECK(reading.pass == 0);
        CHECK(reading.lengthBeats == 0.0);
    }

    SECTION("counting in") {
        MidiRig rig(drawn());
        rig.play(0.0, 2.0);
        rig.run(2 * kBeatSamples);

        const auto reading = rig.reading();
        CHECK_FALSE(reading.recording);
        CHECK(reading.pass == 0);
    }
}

TEST_CASE("A loop wrap draws the pass it opened, not the one it ended",
          "[engine][io][record][midi][2463]") {
    MidiRig rig(drawn());
    rig.loop(0.0, 2.0);
    rig.schedule({noteOn(kBeatSamples / 2, 60), noteOff(kBeatSamples, 60),
                  noteOn(2 * kBeatSamples + kBeatSamples / 2, 72), noteOff(3 * kBeatSamples, 72)});
    rig.play();
    rig.run(3 * kBeatSamples + (kBeatSamples / 2));

    const auto reading = rig.reading();
    CHECK(reading.pass == 2);
    CHECK(reading.startBeat == beats(0.0));
    REQUIRE(reading.notes.size() == 1);
    CHECK(reading.notes[0].noteNumber == 72);
    CHECK(reading.notes[0].startBeat == beats(0.5));
}

TEST_CASE("The pass stands once the take that wrote it is gone",
          "[engine][io][record][midi][2463]") {
    MidiRig rig(drawn());
    rig.schedule({noteOn(kBeatSamples, 60), noteOff(2 * kBeatSamples, 60)});
    rig.play();
    rig.run(3 * kBeatSamples);

    const auto take = rig.finish();
    rig.releaseTake();

    // What the overlay holds until the clip appears in its place, which is only
    // possible because the tap is not the take's to take with it.
    const auto reading = rig.reading();
    CHECK_FALSE(reading.recording);
    CHECK(reading.pass == 1);
    REQUIRE(reading.notes.size() == 1);
    REQUIRE(take.active.notes.size() == 1);
    CHECK(reading.notes[0].startBeat == beats(take.active.notes[0].startBeat));
    CHECK(reading.notes[0].lengthBeats == beats(take.active.notes[0].lengthBeats));
}

TEST_CASE("A take says which target it is recording into", "[engine][io][record][midi][2463]") {
    auto tap = drawn();
    tap.target = RecordTarget::slot;
    tap.scene = 3;

    MidiRig rig(tap);
    rig.play();
    rig.run(kBeatSamples);

    const auto reading = rig.reading();
    CHECK(reading.target == RecordTarget::slot);
    CHECK(reading.scene == 3);
    CHECK(reading.material == RecordMaterial::midi);
}

TEST_CASE("A take nobody draws publishes the pass and nothing else",
          "[engine][io][record][midi][2463]") {
    MidiRig rig(RecordTapSettings{});
    rig.schedule({noteOn(kBeatSamples, 60)});
    rig.play();
    rig.run(2 * kBeatSamples);

    const auto reading = rig.reading();
    CHECK(reading.recording);
    CHECK(reading.lengthBeats == beats(2.0));
    CHECK(reading.notes.empty());

    // Nothing was asked for, so nothing was lost.
    CHECK(reading.notesLost == 0);
    CHECK(reading.peaksLost == 0);
}

// --- the pass an audio take is recording ---

TEST_CASE("A pass's peaks are the samples the take wrote", "[engine][io][record][2463]") {
    AudioRig rig(emptyDirectory("peaks"), drawn(0, 32, 500));
    rig.play();
    rig.run(2000);

    const auto reading = rig.reading();
    CHECK(reading.material == RecordMaterial::audio);
    REQUIRE(reading.peaks.size() == 4);

    // Rising material, so a window's peak is its last sample.
    for (std::size_t at = 0; at < reading.peaks.size(); ++at) {
        const auto last = static_cast<std::int64_t>((at + 1) * 500) - 1;
        CHECK(reading.peaks[at].left == Catch::Approx(material(last, 0)));
        CHECK(reading.peaks[at].right == Catch::Approx(material(last, 1)));
    }
}

TEST_CASE("An audio pass reports the length it wrote, whatever the block size",
          "[engine][io][record][2463]") {
    const auto blockSize = GENERATE(16, 64, 100);

    AudioRig rig(emptyDirectory("length"), drawn(0, 64, 500), blockSize);
    rig.play();
    rig.run(5 * kBeatSamples);

    const auto reading = rig.reading();
    const auto written = static_cast<double>(rig.recorder().capturedSamples());
    CHECK(reading.lengthBeats == beats(written / kBeatSamples));
    CHECK(reading.lengthBeats == beats(5.0));
}

TEST_CASE("Peaks a pass has no room for are counted", "[engine][io][record][2463]") {
    AudioRig rig(emptyDirectory("peaks_lost"), drawn(0, 2, 500));
    rig.play();
    rig.run(2000);

    const auto reading = rig.reading();
    CHECK(reading.peaks.size() == 2);
    CHECK(reading.peaksLost == 2);
}

TEST_CASE("A wrap starts the audio pass's peaks again", "[engine][io][record][2463]") {
    AudioRig rig(emptyDirectory("wrap"), drawn(0, 64, 500));
    rig.loop(0.0, 1.0);
    rig.play();
    rig.run(kBeatSamples + 1000);

    const auto reading = rig.reading();
    CHECK(reading.pass == 2);
    CHECK(reading.startBeat == beats(0.0));
    CHECK(reading.lengthBeats == beats(0.25));
    REQUIRE(reading.peaks.size() == 2);

    // The second pass's first peak is the material that arrived after the wrap,
    // not the first pass's.
    CHECK(reading.peaks[0].left == Catch::Approx(material(kBeatSamples + 499, 0)));
}

TEST_CASE("A pitch struck twice replaces the note that was already down",
          "[engine][io][record][midi][2463]") {
    MidiRig rig(drawn());
    rig.schedule({noteOn(kBeatSamples, 60, 90), noteOn(2 * kBeatSamples, 60, 110),
                  noteOff(3 * kBeatSamples, 60)});
    rig.play();
    rig.run(5 * kBeatSamples);

    const auto preview = rig.reading();
    const auto take = rig.finish();

    // PassWalk drops the first strike, so the preview holds one note too, and
    // the pass growing past it does not stretch it.
    REQUIRE(take.active.notes.size() == 1);
    REQUIRE(preview.notes.size() == 1);
    CHECK(preview.notes[0].velocity == 110);
    CHECK(preview.notes[0].startBeat == beats(take.active.notes[0].startBeat));
    CHECK(preview.notes[0].lengthBeats == beats(take.active.notes[0].lengthBeats));
    CHECK(preview.notes[0].lengthBeats == beats(1.0));
}

TEST_CASE("An audio pass turns over where the file does, not where the wrap is",
          "[engine][io][record][2463]") {
    // The boundary the sink took is an arrival, and with a positive latency the
    // written audio is that far behind it: the samples in between are still the
    // last pass's file.
    constexpr int kLatency = 128;
    constexpr int kPerPeak = 500;

    AudioRig rig(emptyDirectory("latency_wrap"), drawn(0, 64, kPerPeak), 64, kLatency);
    rig.loop(0.0, 1.0);
    rig.play();

    SECTION("the pass stands until the audio reaches the boundary") {
        rig.run(kBeatSamples + 64);
        CHECK(rig.reading().pass == 1);
    }

    SECTION("and its first peak is the audio the new file opens with") {
        rig.run(kBeatSamples + kLatency + 1000);

        const auto reading = rig.reading();
        CHECK(reading.pass == 2);
        REQUIRE(reading.peaks.size() == 2);

        // Written sample 4000 is the arrival a latency later, and the peak
        // covering it runs to the end of its tick.
        constexpr auto kFirstArrival = kBeatSamples + kLatency;
        CHECK(reading.peaks[0].left == Catch::Approx(material(kFirstArrival + kPerPeak - 1, 0)));
    }
}

TEST_CASE("A later pass is drawn where the take will hold it too",
          "[engine][io][record][midi][2463]") {
    // The pass a wrap opened, where the preview's origin and the clip's are
    // arrived at differently: the tap counts from the boundary as it goes, the
    // take splits on it in finish().
    MidiRig rig(drawn());
    rig.loop(0.0, 2.0);
    rig.schedule({noteOn(kBeatSamples / 2, 60), noteOff(kBeatSamples, 60),
                  noteOn(2 * kBeatSamples + (kBeatSamples / 2), 72),
                  noteOff(3 * kBeatSamples, 72)});
    rig.play();
    rig.run((2 * kBeatSamples) + (3 * kBeatSamples) / 2);

    const auto preview = rig.reading();
    const auto take = rig.finish();

    REQUIRE(take.clip.takes.size() == 2);
    REQUIRE(take.clip.takes[1].notes.size() == 1);
    REQUIRE(preview.notes.size() == 1);

    CHECK(preview.pass == 2);
    CHECK(preview.startBeat == beats(take.startBeat));
    CHECK(preview.notes[0].noteNumber == take.clip.takes[1].notes[0].noteNumber);
    CHECK(preview.notes[0].startBeat == beats(take.clip.takes[1].notes[0].startBeat));
    CHECK(preview.notes[0].lengthBeats == beats(take.clip.takes[1].notes[0].lengthBeats));
}

TEST_CASE("A note held across a wrap is not drawn in the pass it did not start in",
          "[engine][io][record][midi][2463]") {
    MidiRig rig(drawn());
    rig.loop(0.0, 2.0);
    rig.schedule(
        {noteOn(3 * kBeatSamples / 2, 60), noteOff((2 * kBeatSamples) + kBeatSamples, 60)});
    rig.play();
    rig.run(3 * kBeatSamples);

    const auto preview = rig.reading();
    const auto take = rig.finish();

    // The take gives it to the pass it started in, cut off at that pass's end,
    // and the pass it ran into holds nothing.
    REQUIRE(take.clip.takes.size() == 2);
    CHECK(take.clip.takes[0].notes.size() == 1);
    CHECK(take.clip.takes[1].notes.empty());
    CHECK(preview.pass == 2);
    CHECK(preview.notes.empty());
}

TEST_CASE("A loop shorter than the latency still turns the preview over",
          "[engine][io][record][2463]") {
    // Every wrap names a boundary the written audio has not reached yet, so a
    // preview holding only the newest would sit on its first pass while the
    // sink cut eight files.
    constexpr int kLoopSamples = 64;
    constexpr int kLatency = 128;

    AudioRig rig(emptyDirectory("short_loop"), drawn(0, 64, 16), 64, kLatency);
    rig.loop(0.0, static_cast<double>(kLoopSamples) / kBeatSamples);
    rig.play();
    rig.run(640);

    const auto reading = rig.reading();

    // Seven boundaries the written audio reached, after the pass the take
    // opened with. The length is one loop rather than the eight the preview
    // would have run together.
    CHECK(reading.pass == 8);
    CHECK(reading.lengthBeats == beats(static_cast<double>(kLoopSamples) / kBeatSamples));
    CHECK(reading.peaks.size() == kLoopSamples / 16);

    CHECK(rig.recorder().finish().clip.takes.size() > 1);
}
