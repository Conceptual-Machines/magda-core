#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "io/RecordStream.hpp"
#include "io/RecordThread.hpp"

/**
 * @file test_record_stream.cpp
 * @brief The write path, with nothing on either end of it (#2460).
 *
 * No take, no file, no input: a producer that pushes blocks and a consumer
 * that may or may not keep up. What the cases are about is the one thing that
 * separates this from a tap -- a sample the disk misses is a hole in a
 * performance, so it is refused and counted rather than overwritten.
 */

using magda::engine::RecordedMidiEvent;
using magda::engine::RecordSink;
using magda::engine::RecordStream;
using magda::engine::RecordStreamSettings;
using magda::engine::RecordThread;

namespace {

/// Keeps everything it is handed, in the order it was handed it.
class CollectingSink final : public RecordSink {
  public:
    bool writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples) override {
        for (auto sample = 0; sample < numSamples; ++sample)
            left.push_back(audio.getSample(0, sample));

        ++audioWrites;
        written.store(static_cast<int>(left.size()), std::memory_order_release);
        return !fails;
    }

    bool writeMidi(std::span<const RecordedMidiEvent> events) override {
        for (const auto& event : events)
            midi.push_back(event);

        return !fails;
    }

    void finish() override {
        ++finishes;
    }

    std::vector<float> left;
    std::vector<RecordedMidiEvent> midi;
    int audioWrites = 0;
    int finishes = 0;
    bool fails = false;

    /// Readable while the record thread is writing, which the ones below are
    /// not: a case waiting for a background round polls this.
    std::atomic<int> written{0};
};

/// A block whose samples say which sample of the take they are.
juce::AudioBuffer<float> countingBlock(int numChannels, int numSamples, int from) {
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (auto channel = 0; channel < numChannels; ++channel)
        for (auto sample = 0; sample < numSamples; ++sample)
            buffer.setSample(channel, sample, static_cast<float>(from + sample));
    return buffer;
}

bool writeBlock(RecordStream& stream, const juce::AudioBuffer<float>& block) {
    return stream.writeAudio(juce::dsp::AudioBlock<const float>(block), block.getNumSamples());
}

/// True once @p predicate holds, or false when it has not within a second.
/// Polled rather than slept through: a fixed wait is either flaky or slow.
template <typename Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::yield();
    }

    return predicate();
}

}  // namespace

TEST_CASE("A drained stream holds every sample, in order", "[engine][record]") {
    CollectingSink sink;
    RecordStream stream(sink, {2, 4096, 64, 256});

    constexpr int kBlockSize = 128;
    for (auto block = 0; block < 40; ++block) {
        REQUIRE(writeBlock(stream, countingBlock(2, kBlockSize, block * kBlockSize)));
        stream.drain();
    }

    stream.finish();

    REQUIRE(sink.left.size() == 40 * kBlockSize);
    for (std::size_t sample = 0; sample < sink.left.size(); ++sample)
        CHECK(sink.left[sample] == static_cast<float>(sample));

    CHECK(stream.samplesLost() == 0);
    CHECK_FALSE(stream.failed());
    CHECK(sink.finishes == 1);
}

TEST_CASE("A take crossing the end of the queue comes back unbroken", "[engine][record]") {
    CollectingSink sink;
    // Deliberately small, so 40 blocks lap it several times.
    RecordStream stream(sink, {1, 1024, 64, 300});
    REQUIRE(stream.capacitySamples() == 1024);

    constexpr int kBlockSize = 128;
    for (auto block = 0; block < 40; ++block) {
        REQUIRE(writeBlock(stream, countingBlock(1, kBlockSize, block * kBlockSize)));
        while (stream.drain()) {
        }
    }

    stream.finish();

    REQUIRE(sink.left.size() == 40 * kBlockSize);
    for (std::size_t sample = 0; sample < sink.left.size(); ++sample)
        CHECK(sink.left[sample] == static_cast<float>(sample));
}

TEST_CASE("A stalled writer costs samples, and says how many", "[engine][record]") {
    CollectingSink sink;
    RecordStream stream(sink, {1, 1024, 64, 256});

    constexpr int kBlockSize = 128;
    auto written = 0;
    auto refused = 0;

    // Nothing drains, so the queue fills and every block after it is refused.
    for (auto block = 0; block < 16; ++block) {
        if (writeBlock(stream, countingBlock(1, kBlockSize, block * kBlockSize)))
            written += kBlockSize;
        else
            refused += kBlockSize;
    }

    CHECK(written == stream.capacitySamples());
    CHECK(stream.samplesLost() == refused);

    // The room is what it was asked for: an overrun costs samples rather than
    // buying a bigger queue.
    CHECK(stream.capacitySamples() == 1024);

    // What it did keep is the earliest samples, not the latest: a queue that
    // overwrote would come back holding the end of the take.
    while (stream.drain()) {
    }
    stream.finish();

    REQUIRE(sink.left.size() == static_cast<std::size_t>(written));
    for (std::size_t sample = 0; sample < sink.left.size(); ++sample)
        CHECK(sink.left[sample] == static_cast<float>(sample));

    // And it recovers: a stall is a hole, not the end of the take.
    CHECK(writeBlock(stream, countingBlock(1, kBlockSize, 0)));
}

TEST_CASE("A write the sink refuses breaks the take rather than the queue", "[engine][record]") {
    CollectingSink sink;
    sink.fails = true;
    RecordStream stream(sink, {1, 1024, 64, 256});

    constexpr int kBlockSize = 128;
    for (auto block = 0; block < 8; ++block) {
        REQUIRE(writeBlock(stream, countingBlock(1, kBlockSize, block * kBlockSize)));
        while (stream.drain()) {
        }
    }

    CHECK(stream.failed());

    // The failure is reported once and the queue keeps draining, so the audio
    // thread is never blocked by a disk that has already given up.
    CHECK(stream.samplesLost() == 0);
    CHECK(sink.audioWrites == 1);
    CHECK(writeBlock(stream, countingBlock(1, kBlockSize, 0)));
}

TEST_CASE("MIDI keeps the sample it was played on", "[engine][record]") {
    CollectingSink sink;
    RecordStream stream(sink, {0, 1024, 8, 256});

    juce::MidiBuffer first;
    first.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 5);
    first.addEvent(juce::MidiMessage::controllerEvent(1, 74, 100), 63);

    juce::MidiBuffer second;
    second.addEvent(juce::MidiMessage::noteOff(1, 60), 10);

    REQUIRE(stream.writeMidi(first, 0));
    REQUIRE(stream.writeMidi(second, 512));
    while (stream.drain()) {
    }
    stream.finish();

    REQUIRE(sink.midi.size() == 3);
    CHECK(sink.midi[0].sample == 5);
    CHECK(sink.midi[0].numBytes == 3);
    CHECK(sink.midi[1].sample == 63);
    CHECK(sink.midi[2].sample == 522);
    CHECK(stream.eventsLost() == 0);
}

TEST_CASE("MIDI a stream cannot hold is dropped and counted", "[engine][record]") {
    CollectingSink sink;
    RecordStream stream(sink, {0, 1024, 64, 256});

    SECTION("a burst past the room it has") {
        juce::MidiBuffer flood;
        for (auto event = 0; event < 200; ++event)
            flood.addEvent(juce::MidiMessage::controllerEvent(1, 74, event % 128), event);

        CHECK_FALSE(stream.writeMidi(flood, 0));
        CHECK(stream.eventsLost() == 200 - 64);

        while (stream.drain()) {
        }
        CHECK(sink.midi.size() == 64);
    }

    SECTION("and a message longer than a take can hold") {
        const std::vector<std::uint8_t> payload{0x7D, 0x01, 0x02, 0x03};
        juce::MidiBuffer sysex;
        sysex.addEvent(
            juce::MidiMessage::createSysExMessage(payload.data(), static_cast<int>(payload.size())),
            0);

        CHECK_FALSE(stream.writeMidi(sysex, 0));
        CHECK(stream.eventsLost() == 1);

        while (stream.drain()) {
        }
        CHECK(sink.midi.empty());
    }
}

TEST_CASE("The record thread drains what it is given and nothing else", "[engine][record]") {
    CollectingSink sink;
    RecordStream stream(sink, {1, 4096, 64, 256});

    SECTION("driven by hand, with no thread behind it") {
        RecordThread thread(false);
        REQUIRE(thread.streamCount() == 0);
        CHECK_FALSE(thread.drainOnce());

        thread.add(stream);
        REQUIRE(thread.streamCount() == 1);

        REQUIRE(writeBlock(stream, countingBlock(1, 128, 0)));
        CHECK(thread.drainOnce());
        CHECK(sink.left.size() == 128);

        thread.remove(stream);
        REQUIRE(thread.streamCount() == 0);

        // What is written after it was removed stays queued: closing a take is
        // finish(), not unregistering.
        REQUIRE(writeBlock(stream, countingBlock(1, 128, 128)));
        CHECK_FALSE(thread.drainOnce());
        CHECK(sink.left.size() == 128);
    }

    SECTION("and in the background, without being asked") {
        RecordThread thread;
        thread.add(stream);

        for (auto block = 0; block < 8; ++block)
            REQUIRE(writeBlock(stream, countingBlock(1, 128, block * 128)));

        CHECK(waitFor([&sink] { return sink.written.load(std::memory_order_acquire) == 1024; }));

        thread.remove(stream);
        stream.finish();

        REQUIRE(sink.left.size() == 1024);
        for (std::size_t sample = 0; sample < sink.left.size(); ++sample)
            CHECK(sink.left[sample] == static_cast<float>(sample));
    }
}
