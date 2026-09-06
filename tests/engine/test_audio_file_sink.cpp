#include <juce_audio_formats/juce_audio_formats.h>

#if !JUCE_WINDOWS
    #include <sys/resource.h>

    #include <csignal>
#endif

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>

#include "exec/RenderContext.hpp"
#include "io/AudioFileSink.hpp"
#include "io/PcmQuantiser.hpp"

/**
 * @file test_audio_file_sink.cpp
 * @brief What a render is worth once it is a file (#2447).
 *
 * The claim under test is a round trip: what the sink was handed comes back off
 * the disk as itself, at every depth and in both containers. Read back rather
 * than asserted against numbers written here, because the failure this guards
 * against is a writer rounding the codes a second time, and a test that trusted
 * the writer would have nothing to say about it.
 */

using magda::engine::AudioFileFormat;
using magda::engine::AudioFileSink;
using magda::engine::AudioFileSpec;
using magda::engine::DitherMode;
using magda::engine::PcmQuantiser;
using magda::engine::RenderContext;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kSamples = 1024;

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_audio_file_sink_test");
    root.createDirectory();
    return root;
}

juce::File destination(const juce::String& name) {
    auto file = scratch().getChildFile(name);
    file.deleteFile();
    return file;
}

/// Something with samples all over the range rather than a shape: quiet parts
/// where a code is one or two, and peaks near full scale where a float's own
/// spacing is coarse.
juce::AudioBuffer<float> material(int numChannels, int numSamples = kSamples) {
    juce::AudioBuffer<float> buffer(numChannels, numSamples);

    for (auto channel = 0; channel < numChannels; ++channel)
        for (auto at = 0; at < numSamples; ++at) {
            const auto phase = 2.0 * std::numbers::pi * (channel + 1) * at / 128.0;
            const auto envelope = static_cast<double>(at) / numSamples;
            buffer.setSample(channel, at, static_cast<float>(0.94 * envelope * std::sin(phase)));
        }

    return buffer;
}

/// Everything in @p file, as the floats a reader makes of it.
juce::AudioBuffer<float> readBack(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

/// What the file should hold: the same material, quantised by the same unit the
/// sink uses. Unset @p mode is the sink's own default for the depth.
juce::AudioBuffer<float> quantised(const juce::AudioBuffer<float>& source, int bitDepth,
                                   std::optional<DitherMode> mode = {}) {
    juce::AudioBuffer<float> expected(source);
    PcmQuantiser quantiser(bitDepth, source.getNumChannels(),
                           mode.value_or(magda::engine::defaultDitherFor(bitDepth)));
    quantiser.process(expected, expected.getNumSamples());
    return expected;
}

void requireSame(const juce::AudioBuffer<float>& stored, const juce::AudioBuffer<float>& expected) {
    REQUIRE(stored.getNumChannels() == expected.getNumChannels());
    REQUIRE(stored.getNumSamples() == expected.getNumSamples());

    for (auto channel = 0; channel < expected.getNumChannels(); ++channel)
        for (auto at = 0; at < expected.getNumSamples(); ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(stored.getSample(channel, at) == expected.getSample(channel, at));
        }
}

/// A directory of its own, so a case can say what is left in it.
juce::File emptyDirectory(const juce::String& name) {
    auto directory = scratch().getChildFile(name);
    directory.deleteRecursively();
    directory.createDirectory();
    return directory;
}

/// Write @p source through a sink of @p spec, one block, and hand back the file.
juce::File writeInto(const juce::File& file, const AudioFileSpec& spec,
                     const juce::AudioBuffer<float>& source) {
    const RenderContext context{kSampleRate, source.getNumSamples(), source.getNumChannels()};

    auto sink = AudioFileSink::create(file, spec, context);
    REQUIRE(sink != nullptr);

    sink->write(source, source.getNumSamples());
    CHECK(sink->samplesWritten() == source.getNumSamples());
    REQUIRE(sink->close());

    return file;
}

juce::File writeThrough(const juce::String& name, const AudioFileSpec& spec,
                        const juce::AudioBuffer<float>& source) {
    return writeInto(destination(name), spec, source);
}

}  // namespace

TEST_CASE("A fixed-point file holds the codes the quantiser chose", "[engine][io][render][2447]") {
    const auto source = material(2);

    for (const auto format : {AudioFileFormat::wav, AudioFileFormat::flac}) {
        for (const auto bits : {16, 24}) {
            INFO("format " << static_cast<int>(format) << " bits " << bits);

            const auto file = writeThrough(
                format == AudioFileFormat::flac ? "codes.flac" : "codes.wav",
                {.format = format, .bitDepth = bits, .dither = DitherMode::none}, source);

            // Not "close to the input": exactly the grid the quantiser landed
            // on. A writer given float would round a second time and land some
            // of these one code low, which is the whole reason the sink hands
            // over codes.
            requireSame(readBack(file), quantised(source, bits, DitherMode::none));
        }
    }
}

TEST_CASE("A float file holds the render's own samples", "[engine][io][render][2447]") {
    const auto source = material(2);

    // Nothing quantises, so nothing is dithered either, even when a caller asks
    // for it: a 32-bit float file has no grid to hide.
    const auto file = writeThrough(
        "float.wav", {.format = AudioFileFormat::wav, .bitDepth = 32, .dither = DitherMode::tpdf},
        source);

    requireSame(readBack(file), source);
}

TEST_CASE("A fixed-point file is dithered unless the caller says otherwise",
          "[engine][io][render][2447]") {
    const auto source = material(1);

    const auto dithered =
        writeThrough("default.wav", {.format = AudioFileFormat::wav, .bitDepth = 16}, source);
    const auto plain = writeThrough(
        "plain.wav", {.format = AudioFileFormat::wav, .bitDepth = 16, .dither = DitherMode::none},
        source);

    // The choice reaches the file, which is the half of #2248 the unit could
    // not carry on its own: an unset spec at 16 bits is TPDF.
    requireSame(readBack(dithered), quantised(source, 16, DitherMode::tpdf));
    CHECK(magda::engine::defaultDitherFor(16) == DitherMode::tpdf);
    CHECK(magda::engine::defaultDitherFor(24) == DitherMode::tpdf);
    CHECK(magda::engine::defaultDitherFor(32) == DitherMode::none);

    const auto withNoise = readBack(dithered);
    const auto without = readBack(plain);

    auto differences = 0;
    for (auto at = 0; at < without.getNumSamples(); ++at)
        if (withNoise.getSample(0, at) != without.getSample(0, at))
            ++differences;

    INFO("samples that moved: " << differences);
    CHECK(differences > without.getNumSamples() / 10);
}

TEST_CASE("A shaped render is the shaped one, not a rounded one", "[engine][io][render][2447]") {
    const auto source = material(1);

    const auto file = writeThrough(
        "shaped.wav",
        {.format = AudioFileFormat::wav, .bitDepth = 16, .dither = DitherMode::shaped}, source);

    requireSame(readBack(file), quantised(source, 16, DitherMode::shaped));
}

TEST_CASE("How a render was cut into blocks is not in the file", "[engine][io][render][2447]") {
    const auto source = material(2);
    const auto whole =
        writeThrough("whole.wav", {.format = AudioFileFormat::wav, .bitDepth = 16}, source);

    const auto file = destination("split.wav");
    const RenderContext context{kSampleRate, kSamples, 2};

    auto sink =
        AudioFileSink::create(file, {.format = AudioFileFormat::wav, .bitDepth = 16}, context);
    REQUIRE(sink != nullptr);

    // Four blocks of the same material, which the dither's per-channel state has
    // to carry across for the file to come out the same. The render promises
    // block size is a batching choice; the file it lands in has to keep that
    // promise or a bounce depends on a buffer setting.
    constexpr int kBlock = kSamples / 4;
    for (auto at = 0; at < kSamples; at += kBlock) {
        juce::AudioBuffer<float> piece(2, kBlock);
        for (auto channel = 0; channel < 2; ++channel)
            piece.copyFrom(channel, 0, source, channel, at, kBlock);
        sink->write(piece, kBlock);
    }

    REQUIRE(sink->close());
    requireSame(readBack(file), readBack(whole));
}

TEST_CASE("The file is the width and rate the plan was prepared with",
          "[engine][io][render][2447]") {
    const auto source = material(1, 256);
    const auto file = destination("mono.wav");
    const RenderContext context{48000.0, 256, 1};

    auto sink =
        AudioFileSink::create(file, {.format = AudioFileFormat::wav, .bitDepth = 24}, context);
    REQUIRE(sink != nullptr);
    sink->write(source, 256);
    REQUIRE(sink->close());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);

    // Taken from the context rather than from the caller, so a header cannot
    // disagree with the render that filled it.
    CHECK(reader->numChannels == 1);
    CHECK(reader->sampleRate == 48000.0);
    CHECK(reader->bitsPerSample == 24);
    CHECK(reader->lengthInSamples == 256);
}

TEST_CASE("A spec no format holds is refused before anything renders",
          "[engine][io][render][2447]") {
    const RenderContext context{kSampleRate, 512, 2};

    // FLAC has no float, and a depth nothing writes is a depth nothing writes.
    CHECK(AudioFileSink::create(destination("refused.flac"),
                                {.format = AudioFileFormat::flac, .bitDepth = 32},
                                context) == nullptr);
    CHECK(AudioFileSink::create(destination("refused.wav"),
                                {.format = AudioFileFormat::wav, .bitDepth = 20},
                                context) == nullptr);

    // A directory nobody made. Refused here rather than at the first block, so
    // a render that cannot be stored is never run.
    const auto missing = scratch().getChildFile("no_such_directory").getChildFile("out.wav");
    CHECK(AudioFileSink::create(missing, {.format = AudioFileFormat::wav, .bitDepth = 24},
                                context) == nullptr);
}

TEST_CASE("A second render replaces the file rather than following it",
          "[engine][io][render][2447]") {
    const auto first = material(2);
    const auto file =
        writeThrough("replaced.wav", {.format = AudioFileFormat::wav, .bitDepth = 16}, first);
    REQUIRE(readBack(file).getNumSamples() == kSamples);

    const auto second = material(2, 256);
    const RenderContext context{kSampleRate, 256, 2};

    auto sink =
        AudioFileSink::create(file, {.format = AudioFileFormat::wav, .bitDepth = 16}, context);
    REQUIRE(sink != nullptr);
    sink->write(second, 256);
    REQUIRE(sink->close());

    // The short render, and only it. A stream opened over the old file would
    // have written after it and left a file the header no longer describes.
    CHECK(readBack(file).getNumSamples() == 256);
}

TEST_CASE("A block narrower than the file is refused rather than stored",
          "[engine][io][render][2447]") {
    const auto file = destination("narrow.wav");
    const RenderContext context{kSampleRate, 256, 2};

    auto sink =
        AudioFileSink::create(file, {.format = AudioFileFormat::wav, .bitDepth = 24}, context);
    REQUIRE(sink != nullptr);

    const auto mono = material(1, 256);
    sink->write(mono, 256);

    // Nothing was written and close says so: the alternative is a stereo file
    // whose second channel is whatever was in that memory.
    CHECK(sink->samplesWritten() == 0);
    CHECK_FALSE(sink->close());
}

TEST_CASE("A refused render leaves the file that was already there", "[engine][io][render][2447]") {
    const auto directory = emptyDirectory("refusals");
    const auto file = directory.getChildFile("export.wav");

    const auto source = material(2);
    writeInto(file, {.format = AudioFileFormat::wav, .bitDepth = 16}, source);
    const auto before = readBack(file);

    // A shape the format will not take rather than a depth it will not take:
    // FLAC holds eight channels, and the refusal comes from the encoder, past
    // every check that could be made of the spec on its own.
    const RenderContext nine{kSampleRate, 512, 9};
    CHECK(AudioFileSink::create(file, {.format = AudioFileFormat::flac, .bitDepth = 24}, nine) ==
          nullptr);

    const RenderContext stereo{kSampleRate, 512, 2};
    CHECK(AudioFileSink::create(file, {.format = AudioFileFormat::wav, .bitDepth = 20}, stereo) ==
          nullptr);

    // Yesterday's export, still there. A render that never ran has no business
    // costing anyone the last one that did.
    requireSame(readBack(file), before);

    // And nothing beside it: the file a refused render opened is its own to
    // clean up.
    CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 1);
}

TEST_CASE("A render the disk did not take is not a successful render",
          "[engine][io][render][2447]") {
#if JUCE_WINDOWS
    SUCCEED("The file size limit this needs is POSIX");
#else
    for (const auto format : {AudioFileFormat::wav, AudioFileFormat::flac}) {
        INFO("format " << static_cast<int>(format));

        const auto directory = emptyDirectory("full_disk");
        const auto file =
            directory.getChildFile(format == AudioFileFormat::flac ? "export.flac" : "export.wav");

        const auto source = material(2);
        writeInto(file, {.format = format, .bitDepth = 16}, source);
        const auto before = readBack(file);

        auto sink = AudioFileSink::create(file, {.format = format, .bitDepth = 16},
                                          RenderContext{kSampleRate, kSamples, 2});
        REQUIRE(sink != nullptr);

        // The whole render fits in the stream's own buffer, so nothing has
        // reached the disk yet and every write here succeeds. What runs out of
        // room is the finish -- the header written back over the front, the
        // encoder flushed -- which is where a writer has no way left to say so.
        rlimit previous{};
        REQUIRE(getrlimit(RLIMIT_FSIZE, &previous) == 0);
        auto* previousHandler = std::signal(SIGXFSZ, SIG_IGN);
        const rlimit limited{512, previous.rlim_max};
        REQUIRE(setrlimit(RLIMIT_FSIZE, &limited) == 0);

        sink->write(source, kSamples);
        const auto closed = sink->close();

        REQUIRE(setrlimit(RLIMIT_FSIZE, &previous) == 0);
        std::signal(SIGXFSZ, previousHandler);

        // Reported, rather than a full disk coming back as a finished export.
        CHECK_FALSE(closed);
        CHECK(sink->samplesWritten() == kSamples);

        sink.reset();

        // And the destination is what it was, because the render never reached
        // it: a file that could not be written does not cost anyone the file
        // that was already there.
        requireSame(readBack(file), before);
        CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 1);
    }
#endif
}
