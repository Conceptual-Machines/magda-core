#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "io/SourceReaders.hpp"

/**
 * Reverse, looping and rate conversion (#2036), which are one question asked
 * three times: which of a file's samples answer a position.
 *
 * Each of them is a reader wrapped around a reader, so each is tested the way a
 * reader is: ask for a run of samples and say which of the file's ought to come
 * back. Nothing here needs a stream, a clip or a callback, which is the point
 * of them being readers.
 */

using magda::engine::AudioFileReader;
using magda::engine::LoopingAudioFileReader;
using magda::engine::readThrough;
using magda::engine::ResamplingAudioFileReader;
using magda::engine::ReversedAudioFileReader;
using magda::engine::SourceRead;

namespace {

/// Sample n reads back as n, so where a sample came from is readable off the
/// value rather than inferred. Ends where it says it does, because half of what
/// is being tested here is what happens at the edges of a file.
class CountingReader final : public AudioFileReader {
  public:
    explicit CountingReader(std::int64_t length = 1000, double rate = 44100.0)
        : length_(length), rate_(rate) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return rate_;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        // Nothing above may ask for a position before the first sample: a
        // reader that is handed one has no way to answer it, and the layers
        // that need the material either side of where they land are the ones
        // that clamp (SourceReaders.cpp).
        if (startSample < 0)
            ++readsBeforeTheStart;

        const auto available = static_cast<int>(std::clamp<std::int64_t>(
            length_ - startSample, 0, static_cast<std::int64_t>(numSamples)));

        if (available < numSamples)
            destination.clear(destinationOffset + available, numSamples - available);
        if (available <= 0)
            return 0;

        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample));

        return available;
    }

    int readsBeforeTheStart = 0;

  private:
    std::int64_t length_ = 0;
    double rate_ = 44100.0;
};

/// Advertises material and answers none of it: a file truncated under us, a
/// device that went away. What a reader that has stopped answering looks like,
/// as against a position where the material is not.
class DeadReader final : public AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 1000;
    }
    double sampleRate() const override {
        return 44100.0;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        destination.clear(destinationOffset, numSamples);
        return 0;
    }
};

/// What a read of @p count samples from @p start came back with, channel zero.
std::vector<float> readOut(AudioFileReader& reader, std::int64_t start, int count) {
    juce::AudioBuffer<float> destination(2, count);
    destination.clear();
    reader.read(destination, 0, start, count);

    return std::vector<float>(destination.getReadPointer(0), destination.getReadPointer(0) + count);
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-3);
}

}  // namespace

TEST_CASE("A reversed reader is the file back to front", "[engine][clip][source]") {
    ReversedAudioFileReader reader(std::make_unique<CountingReader>(100), 100);

    CHECK(reader.lengthInSamples() == 100);

    SECTION("the first sample of it is the last sample of the file") {
        const auto out = readOut(reader, 0, 4);
        CHECK(out[0] == approx(99.0f));
        CHECK(out[1] == approx(98.0f));
        CHECK(out[3] == approx(96.0f));
    }

    SECTION("and the last is the first") {
        const auto out = readOut(reader, 96, 4);
        CHECK(out[0] == approx(3.0f));
        CHECK(out[3] == approx(0.0f));
    }

    SECTION("a run in the middle is the mirror of the run it names") {
        const auto out = readOut(reader, 40, 8);
        for (auto sample = 0; sample < 8; ++sample) {
            INFO("sample " << sample);
            CHECK(out[static_cast<std::size_t>(sample)] == approx(static_cast<float>(59 - sample)));
        }
    }

    SECTION("past the end of it is past the beginning of the file, and silent") {
        juce::AudioBuffer<float> destination(2, 8);
        destination.clear();

        CHECK(reader.read(destination, 0, 100, 8) == 0);
        CHECK(destination.getSample(0, 0) == approx(0.0f));
    }

    SECTION("a run that reaches past the end keeps the samples it does have") {
        const auto out = readOut(reader, 96, 8);
        CHECK(out[0] == approx(3.0f));
        CHECK(out[3] == approx(0.0f));
        CHECK(out[4] == approx(0.0f));
        CHECK(out[7] == approx(0.0f));
    }
}

TEST_CASE("The silence in front of a mirrored file is read, not reported as a failure",
          "[engine][clip][source]") {
    // A reversed clip trimmed longer than its source starts before the mirror's
    // first sample, which is past the file's last. That silence is content: a
    // stream told nothing came back stops reading altogether and reaches the
    // material with its read-ahead somewhere else (PrefetchStream::fill).
    ReversedAudioFileReader reader(std::make_unique<CountingReader>(100), 100);

    juce::AudioBuffer<float> destination(2, 8);
    destination.clear();

    CHECK(reader.read(destination, 0, -16, 8) == 8);
    CHECK(destination.getSample(0, 0) == approx(0.0f));
    CHECK(destination.getSample(0, 7) == approx(0.0f));

    SECTION("and a run crossing into it arrives on the sample it should") {
        const auto out = readOut(reader, -4, 8);
        CHECK(out[0] == approx(0.0f));
        CHECK(out[3] == approx(0.0f));
        CHECK(out[4] == approx(99.0f));
        CHECK(out[7] == approx(96.0f));
    }

    SECTION("while a reader that stops answering inside the file still says so") {
        ReversedAudioFileReader dead(std::make_unique<DeadReader>(), 1000);

        juce::AudioBuffer<float> buffer(2, 8);
        buffer.clear();
        CHECK(dead.read(buffer, 0, 0, 8) == 0);
    }
}

TEST_CASE("A reversed reader turns about the length it was given, not the file's",
          "[engine][clip][source]") {
    // The axis every mirrored position was worked out against. A file that has
    // gained a sample under the project must not move the material, because
    // where the clip was placed was worked out from what the model believes.
    ReversedAudioFileReader reader(std::make_unique<CountingReader>(200), 100);

    const auto out = readOut(reader, 0, 2);
    CHECK(out[0] == approx(99.0f));
    CHECK(out[1] == approx(98.0f));
}

TEST_CASE("A looping reader tiles the region it was given", "[engine][clip][source]") {
    LoopingAudioFileReader reader(std::make_unique<CountingReader>(1000), 100, 8);

    SECTION("a run through the wrap carries straight on into the next tile") {
        const auto out = readOut(reader, 100, 20);
        for (auto sample = 0; sample < 20; ++sample) {
            INFO("sample " << sample);
            CHECK(out[static_cast<std::size_t>(sample)] ==
                  approx(static_cast<float>(100 + sample % 8)));
        }
    }

    SECTION("a run that starts mid-tile keeps its phase") {
        const auto out = readOut(reader, 105, 6);
        CHECK(out[0] == approx(105.0f));
        CHECK(out[2] == approx(107.0f));
        CHECK(out[3] == approx(100.0f));
        CHECK(out[5] == approx(102.0f));
    }

    SECTION("a position far past the region is the phase it has got to") {
        const auto out = readOut(reader, 100 + 8 * 1000 + 3, 1);
        CHECK(out[0] == approx(103.0f));
    }

    SECTION("a position in front of the region is a phase within it too") {
        // An anchor away from the loop start is a phase rather than a place of
        // its own, which is how the incumbent reads a looped clip's offset.
        const auto out = readOut(reader, 97, 2);
        CHECK(out[0] == approx(105.0f));
        CHECK(out[1] == approx(106.0f));
    }

    SECTION("and it never ends, because a loop has no last sample") {
        CHECK(reader.lengthInSamples() > 1000000000);
    }
}

TEST_CASE("A loop over a region the file does not reach plays the silence in it",
          "[engine][clip][source]") {
    // The region is what the model asked for. Reporting the hole as the end of
    // the file would leave the stream above reading the same position over and
    // over instead of moving through the tile.
    LoopingAudioFileReader reader(std::make_unique<CountingReader>(100), 96, 8);

    const auto out = readOut(reader, 96, 8);
    CHECK(out[0] == approx(96.0f));
    CHECK(out[3] == approx(99.0f));
    CHECK(out[4] == approx(0.0f));
    CHECK(out[7] == approx(0.0f));

    SECTION("a read landing wholly inside that silence is still a read") {
        // The shape that would stop a reader for good: a region whose tail past
        // the end of its file is longer than a chunk, so a whole read lands in
        // it. Reported as nothing, the stream stops and the top of the loop
        // never comes round again.
        LoopingAudioFileReader reaching(std::make_unique<CountingReader>(100), 50, 400);

        juce::AudioBuffer<float> destination(2, 64);
        destination.clear();

        CHECK(reaching.read(destination, 0, 200, 64) == 64);
        CHECK(destination.getSample(0, 0) == approx(0.0f));

        // And the top of the region still plays, next time round.
        CHECK(readOut(reaching, 450, 2)[0] == approx(50.0f));
    }

    SECTION("while a reader that stops answering inside the file still says so") {
        LoopingAudioFileReader dead(std::make_unique<DeadReader>(), 0, 64);

        juce::AudioBuffer<float> destination(2, 8);
        destination.clear();
        CHECK(dead.read(destination, 0, 0, 8) == 0);
    }
}

TEST_CASE("A resampling reader presents the file at the device's rate", "[engine][clip][source]") {
    // Half the source rate, so one source sample is two of these, and a ramp
    // comes out as a ramp of half the slope. A ramp is exact through the curve
    // this interpolates along, so the value at a position is the position.
    ResamplingAudioFileReader reader(std::make_unique<CountingReader>(1000, 22050.0), 22050.0,
                                     44100.0);

    CHECK(reader.sampleRate() == 44100.0);
    CHECK(reader.lengthInSamples() == 2000);

    SECTION("a sample of it is where it lands in the file") {
        const auto out = readOut(reader, 400, 8);
        for (auto sample = 0; sample < 8; ++sample) {
            INFO("sample " << sample);
            CHECK(out[static_cast<std::size_t>(sample)] ==
                  approx(static_cast<float>(400 + sample) * 0.5f));
        }
    }

    SECTION("and the other way round for a file recorded above the device") {
        ResamplingAudioFileReader down(std::make_unique<CountingReader>(1000, 88200.0), 88200.0,
                                       44100.0);

        CHECK(down.lengthInSamples() == 500);

        const auto out = readOut(down, 100, 4);
        CHECK(out[0] == approx(200.0f));
        CHECK(out[1] == approx(202.0f));
    }

    SECTION("past the last sample there is nothing") {
        juce::AudioBuffer<float> destination(2, 8);
        destination.clear();

        CHECK(reader.read(destination, 0, 2000, 8) == 0);
        CHECK(destination.getSample(0, 0) == approx(0.0f));
    }

    SECTION("in front of the first, silence that is read like any other") {
        // A clip cued into the silence ahead of its own material reads it and
        // walks through it, converted or not. The layers have to agree with the
        // base reader about what structural silence is, or a converted clip
        // stalls where an unconverted one plays.
        juce::AudioBuffer<float> destination(2, 32);
        destination.clear();

        CHECK(reader.read(destination, 0, -64, 32) == 32);
        CHECK(destination.getSample(0, 0) == approx(0.0f));
    }

    SECTION("while a reader that stops answering inside the file still says so") {
        ResamplingAudioFileReader dead(std::make_unique<DeadReader>(), 22050.0, 44100.0);

        juce::AudioBuffer<float> destination(2, 8);
        destination.clear();
        CHECK(dead.read(destination, 0, 0, 8) == 0);
    }
}

TEST_CASE("A resampled sample is the same sample however it was reached",
          "[engine][clip][source]") {
    // No cursor and no carried phase: an output sample's source position is its
    // own position times the ratio and nothing else. It is what lets a bounce
    // that seeks straight to the middle of a clip render what the callback
    // played on the way there.
    auto sequential = std::make_unique<ResamplingAudioFileReader>(
        std::make_unique<CountingReader>(4000, 32000.0), 32000.0, 44100.0);
    auto seeking = std::make_unique<ResamplingAudioFileReader>(
        std::make_unique<CountingReader>(4000, 32000.0), 32000.0, 44100.0);

    std::vector<float> read;
    for (auto start = 0; start < 512; start += 64) {
        const auto block = readOut(*sequential, start, 64);
        read.insert(read.end(), block.begin(), block.end());
    }

    const auto direct = readOut(*seeking, 448, 64);

    for (auto sample = 0; sample < 64; ++sample) {
        INFO("sample " << sample);
        // Exactly, not nearly: the same arithmetic over the same samples.
        CHECK(read[static_cast<std::size_t>(448 + sample)] ==
              direct[static_cast<std::size_t>(sample)]);
    }
}

TEST_CASE("Nothing below is ever asked for a position before the first sample",
          "[engine][clip][source]") {
    // The resampler reaches for the sample before the one it lands on, so the
    // first sample of a file is a read of the one that is not there. Handing
    // that to a file reader is asking it for a position it has no way to
    // answer.
    auto file = std::make_unique<CountingReader>(1000, 22050.0);
    auto* counting = file.get();

    ResamplingAudioFileReader reader(std::move(file), 22050.0, 44100.0);
    readOut(reader, 0, 64);

    CHECK(counting->readsBeforeTheStart == 0);
}

TEST_CASE("A reading asked for nothing is the file itself", "[engine][clip][source]") {
    auto file = std::make_unique<CountingReader>(1000);
    const AudioFileReader* plain = file.get();

    SourceRead how;
    how.lengthInSamples = 1000;
    how.sourceSampleRate = 44100.0;
    how.deviceSampleRate = 44100.0;

    const auto through = readThrough(std::move(file), how);
    CHECK(through.get() == plain);
}

TEST_CASE("A reading composes mirror, tile and rate in that order", "[engine][clip][source]") {
    // The incumbent's order. Tiling over the mirror is what makes a reversed
    // loop the same region played backwards, and converting last leaves
    // everything below it in the source's own samples.
    SourceRead how;
    how.lengthInSamples = 100;
    how.sourceSampleRate = 22050.0;
    how.deviceSampleRate = 44100.0;
    how.reversed = true;

    // The forward region [40, 48) is [100 - 48, 100 - 40) once mirrored.
    how.loopStartSamples = 52;
    how.loopLengthSamples = 8;

    const auto reading = readThrough(std::make_unique<CountingReader>(100, 22050.0), how);
    REQUIRE(reading != nullptr);
    CHECK(reading->sampleRate() == 44100.0);

    // Device sample 104 is source sample 52, the top of the mirrored region,
    // which is the file's sample 47: the last of the forward region, as a
    // backwards read should start on.
    const auto out = readOut(*reading, 104, 8);
    CHECK(out[0] == approx(47.0f));
    CHECK(out[2] == approx(46.0f));
    CHECK(out[4] == approx(45.0f));

    // And a tile later it is back at the top of the region rather than carrying
    // on down the file.
    const auto wrapped = readOut(*reading, 104 + 16, 1);
    CHECK(wrapped[0] == approx(47.0f));
}
