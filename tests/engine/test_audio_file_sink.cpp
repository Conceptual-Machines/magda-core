#include <juce_audio_formats/juce_audio_formats.h>

#if !JUCE_WINDOWS
    #include <sys/resource.h>

    #include <csignal>
#endif

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

#include "exec/RenderContext.hpp"
#include "io/AudioFileSink.hpp"
#include "io/Mp3AudioFormat.hpp"
#include "io/PcmQuantiser.hpp"
#include "io/SourceLoopInfo.hpp"

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

/// Signal-to-error ratio of @p stored against @p expected, in dB.
double signalToErrorDb(const juce::AudioBuffer<float>& stored,
                       const juce::AudioBuffer<float>& expected) {
    double signal = 0.0;
    double error = 0.0;
    for (auto channel = 0; channel < expected.getNumChannels(); ++channel)
        for (auto at = 0; at < expected.getNumSamples(); ++at) {
            const double want = expected.getSample(channel, at);
            const double diff = stored.getSample(channel, at) - want;
            signal += want * want;
            error += diff * diff;
        }
    return 10.0 * std::log10(signal / std::max(error, 1.0e-20));
}

TEST_CASE("An Opus file holds the render, within what the codec loses", "[engine][io][2955]") {
    constexpr double rate = 48000.0;
    constexpr int samples = 48000;
    const auto source = material(2, samples);
    const auto file = destination("render.opus");

    auto sink = AudioFileSink::create(file, {.format = AudioFileFormat::opus},
                                      RenderContext{rate, samples, 2});
    REQUIRE(sink != nullptr);
    sink->write(source, samples);
    REQUIRE(sink->close());

    // Same length as the render: the encoder's pre-skip is in the header, so
    // the reader drops it rather than the file starting late.
    const auto stored = readBack(file);
    REQUIRE(stored.getNumChannels() == 2);
    REQUIRE(stored.getNumSamples() == samples);
    CHECK(signalToErrorDb(stored, source) > 20.0);
}

TEST_CASE("An Opus render at 44.1 kHz is stored at 48 kHz", "[engine][io][2955]") {
    // Opus has one rate. The render keeps the project's and the encoder
    // resamples, so nothing upstream has to know.
    const auto file =
        writeThrough("resampled.opus", {.format = AudioFileFormat::opus}, material(2, 44100));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == 48000.0);
    CHECK(std::abs(reader->lengthInSamples - 48000) <= 1);
}

TEST_CASE("An Opus bitrate is one the encoder offers", "[engine][io][2955]") {
    const auto source = material(2, 44100);

    // A lossy spec has no depth, so one nothing writes is not a refusal.
    const auto low = writeThrough(
        "low.opus", {.format = AudioFileFormat::opus, .bitDepth = 20, .bitrateKbps = 64}, source);
    const auto high =
        writeThrough("high.opus", {.format = AudioFileFormat::opus, .bitrateKbps = 256}, source);
    CHECK(high.getSize() > low.getSize());

    CHECK(AudioFileSink::create(destination("odd.opus"),
                                {.format = AudioFileFormat::opus, .bitrateKbps = 100},
                                RenderContext{kSampleRate, 512, 2}) == nullptr);
}

/// What a reader makes of @p file's header: rate, channels, length.
std::unique_ptr<juce::AudioFormatReader> readerFor(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

constexpr int kMp3Frame = 1152;

/// What an MP3's LAME tag says about its length: the frames in the file, and
/// the encoder's delay at the front and padding at the end, in samples.
struct Mp3Tag {
    std::int64_t frames = 0;
    int delay = 0;
    int padding = 0;

    /// The render's own length, which is what a gapless player plays.
    std::int64_t samples() const {
        return (frames * kMp3Frame) - delay - padding;
    }
};

/// The tag at the front of @p file. Read here rather than through a decoder:
/// Core Audio trims by it, JUCE's own reader (Linux) does not, so a decoder's
/// length says which platform ran the test, not what the file holds.
/// The bytes an ID3v2 tag at the front of @p data takes, header included, or 0.
std::size_t id3v2Size(const unsigned char* data, std::size_t size) {
    if (size < 10 || std::memcmp(data, "ID3", 3) != 0)
        return 0;
    // Synchsafe: seven bits a byte.
    return 10 + ((std::size_t{data[6]} << 21) | (std::size_t{data[7]} << 14) |
                 (std::size_t{data[8]} << 7) | data[9]);
}

Mp3Tag mp3Tag(const juce::File& file) {
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    const auto* data = static_cast<const unsigned char*>(bytes.getData());
    const auto size = bytes.getSize();

    // In the first frame, which follows any ID3v2 tag: "Xing" (VBR) or "Info"
    // (CBR), flags, frame count; LAME's own extension is 120 bytes on, with
    // delay and padding as two 12-bit fields at +21.
    const auto first = id3v2Size(data, size);
    for (auto at = first; at + 4 <= std::min<std::size_t>(size, first + 64); ++at) {
        if (std::memcmp(data + at, "Xing", 4) != 0 && std::memcmp(data + at, "Info", 4) != 0)
            continue;
        REQUIRE(at + 144 <= size);
        REQUIRE((data[at + 7] & 0x01) != 0);
        REQUIRE(std::memcmp(data + at + 120, "LAME", 4) == 0);

        const auto* lame = data + at + 120;
        return {.frames = (std::int64_t{data[at + 8]} << 24) | (data[at + 9] << 16) |
                          (data[at + 10] << 8) | data[at + 11],
                .delay = (lame[21] << 4) | (lame[22] >> 4),
                .padding = ((lame[22] & 0x0F) << 8) | lame[23]};
    }
    FAIL("no LAME tag");
    return {};
}

TEST_CASE("An MP3 file holds the render, within what the codec loses", "[engine][io][3033]") {
    constexpr int samples = 44100;
    const auto source = material(2, samples);
    const auto file =
        writeThrough("render.mp3", {.format = AudioFileFormat::mp3, .bitrateKbps = 320}, source);

    CHECK(mp3Tag(file).samples() == samples);

    // A decoder that ignores the tag starts late by the encoder's and its own
    // delay, so the comparison is lined up first.
    const auto stored = readBack(file);
    REQUIRE(stored.getNumChannels() == 2);
    REQUIRE(stored.getNumSamples() >= samples);

    auto best = -1000.0;
    for (auto delay = 0; delay <= 2 * kMp3Frame; ++delay) {
        juce::AudioBuffer<float> aligned(2, samples);
        aligned.clear();
        for (auto channel = 0; channel < 2; ++channel)
            aligned.copyFrom(channel, 0, stored, channel, delay,
                             std::min(samples, stored.getNumSamples() - delay));
        best = std::max(best, signalToErrorDb(aligned, source));
    }
    CHECK(best > 20.0);
}

TEST_CASE("An MP3 render above 48 kHz is stored at its family's base rate", "[engine][io][3033]") {
    CHECK(magda::engine::mp3FileRateFor(44100.0) == 44100);
    CHECK(magda::engine::mp3FileRateFor(48000.0) == 48000);
    CHECK(magda::engine::mp3FileRateFor(88200.0) == 44100);
    CHECK(magda::engine::mp3FileRateFor(96000.0) == 48000);
    CHECK(magda::engine::mp3FileRateFor(192000.0) == 48000);

    for (const auto [renderRate, fileRate] :
         {std::pair{96000.0, 48000.0}, std::pair{88200.0, 44100.0}}) {
        INFO("render rate " << renderRate);
        const auto samples = static_cast<int>(renderRate);
        const auto file = destination("resampled.mp3");

        auto sink = AudioFileSink::create(file, {.format = AudioFileFormat::mp3},
                                          RenderContext{renderRate, samples, 2});
        REQUIRE(sink != nullptr);
        sink->write(material(2, samples), samples);
        REQUIRE(sink->close());

        const auto reader = readerFor(file);
        REQUIRE(reader != nullptr);
        CHECK(reader->sampleRate == fileRate);
        CHECK(std::abs(mp3Tag(file).samples() - static_cast<std::int64_t>(fileRate)) <= 1);
    }
}

TEST_CASE("A VBR MP3 says how long it is", "[engine][io][3033]") {
    // Frames vary in size, so only the tag at the front can say how many there
    // are; without it a player guesses from the file size.
    constexpr int samples = 44100;
    const auto file = writeThrough("vbr.mp3", {.format = AudioFileFormat::mp3, .vbrQuality = 0},
                                   material(2, samples));

    const auto tag = mp3Tag(file);
    CHECK(tag.frames > 0);
    CHECK(tag.samples() == samples);
}

TEST_CASE("An MP3 rate or level is one the encoder offers", "[engine][io][3033]") {
    const auto source = material(2, 44100);

    const auto low =
        writeThrough("low.mp3", {.format = AudioFileFormat::mp3, .bitrateKbps = 64}, source);
    const auto high =
        writeThrough("high.mp3", {.format = AudioFileFormat::mp3, .bitrateKbps = 320}, source);
    CHECK(high.getSize() > low.getSize() * 3);

    const auto best =
        writeThrough("v0.mp3", {.format = AudioFileFormat::mp3, .vbrQuality = 0}, source);
    const auto worst =
        writeThrough("v9.mp3", {.format = AudioFileFormat::mp3, .vbrQuality = 9}, source);
    CHECK(best.getSize() > worst.getSize());

    const RenderContext stereo{kSampleRate, 512, 2};
    const auto refused = [&](const AudioFileSpec& spec) {
        return AudioFileSink::create(destination("refused.mp3"), spec, stereo) == nullptr;
    };
    CHECK(refused({.format = AudioFileFormat::mp3, .bitrateKbps = 100}));
    CHECK(refused({.format = AudioFileFormat::mp3, .vbrQuality = 10}));
    CHECK(refused({.format = AudioFileFormat::mp3, .bitrateKbps = 128, .vbrQuality = 2}));

    // A VBR level is LAME's alone.
    CHECK(refused({.format = AudioFileFormat::opus, .vbrQuality = 2}));
    CHECK(refused({.format = AudioFileFormat::wav, .vbrQuality = 2}));
}

/// The text of each ID3v2.3 text frame and the comment in @p file, by frame id.
std::map<juce::String, juce::String> id3v2Frames(const juce::File& file) {
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    const auto* data = static_cast<const unsigned char*>(bytes.getData());
    const auto end = id3v2Size(data, bytes.getSize());
    REQUIRE(end > 0);

    // Text is Latin-1 (encoding 0) or UTF-16 behind a byte-order mark (1).
    const auto decode = [](const unsigned char* text, std::size_t length, unsigned char encoding) {
        if (encoding == 0)
            return juce::String(juce::CharPointer_UTF8(reinterpret_cast<const char*>(text)), length)
                .trimCharactersAtEnd(juce::String::charToString(0));
        juce::String result;
        const auto bigEndian = length >= 2 && text[0] == 0xFE;
        for (std::size_t at = 2; at + 1 < length; at += 2) {
            const auto unit =
                bigEndian ? (text[at] << 8) | text[at + 1] : (text[at + 1] << 8) | text[at];
            if (unit == 0)
                break;
            result += juce::String::charToString(static_cast<juce::juce_wchar>(unit));
        }
        return result;
    };

    std::map<juce::String, juce::String> frames;
    for (std::size_t at = 10; at + 10 <= end && data[at] != 0;) {
        const juce::String id(reinterpret_cast<const char*>(data + at), 4);
        const auto length = (std::size_t{data[at + 4]} << 24) | (std::size_t{data[at + 5]} << 16) |
                            (std::size_t{data[at + 6]} << 8) | data[at + 7];
        const auto* body = data + at + 10;
        if (id == "COMM") {
            // Encoding, language, a descriptor ending in a null, then the text.
            const auto unitBytes = body[0] == 1 ? 2U : 1U;
            std::size_t text = 4;
            while (text + unitBytes <= length &&
                   (body[text] != 0 || (unitBytes == 2 && body[text + 1] != 0)))
                text += unitBytes;
            text += unitBytes;
            frames[id] = decode(body + text, length - text, body[0]);
        } else if (id.startsWithChar('T')) {
            frames[id] = decode(body + 1, length - 1, body[0]);
        }
        at += 10 + length;
    }
    return frames;
}

/// Credits with a name outside Latin-1, which is what UTF-16 tags are for.
magda::engine::AudioFileMetadata credits() {
    magda::engine::AudioFileMetadata metadata;
    metadata.title = juce::String::fromUTF8("Nuit d\xc3\xa9toil\xc3\xa9"
                                            "e \xe2\x80\x94 \xe6\x98\x9f");
    metadata.artist = "Nomad Monad";
    metadata.album = "Slices";
    metadata.date = "2026";
    metadata.genre = "Ambient";
    metadata.comment = "Rendered for #3035";
    return metadata;
}

/// Whether @p file holds the Vorbis comment @p key=@p value, length prefix and all.
bool hasVorbisComment(const juce::File& file, const juce::String& key, const juce::String& value) {
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    const auto comment = (key + "=" + value).toStdString();
    const std::string_view data(static_cast<const char*>(bytes.getData()), bytes.getSize());
    const auto at = data.find(comment);
    if (at == std::string_view::npos || at < 4)
        return false;
    const auto* length = reinterpret_cast<const unsigned char*>(data.data() + at - 4);
    return (std::size_t{length[0]} | (std::size_t{length[1]} << 8) |
            (std::size_t{length[2]} << 16) | (std::size_t{length[3]} << 24)) == comment.size();
}

TEST_CASE("An Opus file carries the project's credits", "[engine][io][3035]") {
    const auto expected = credits();
    const auto file =
        writeThrough("credits.opus", {.format = AudioFileFormat::opus, .metadata = expected},
                     material(2, 48000));

    // Read from the bytes rather than through JUCE's reader, which builds its
    // strings as Latin-1 and so cannot give back a UTF-8 title it was handed.
    CHECK(hasVorbisComment(file, "TITLE", expected.title));
    CHECK(hasVorbisComment(file, "ARTIST", expected.artist));
    CHECK(hasVorbisComment(file, "ALBUM", expected.album));
    CHECK(hasVorbisComment(file, "DATE", expected.date));
    CHECK(hasVorbisComment(file, "GENRE", expected.genre));
    CHECK(hasVorbisComment(file, "COMMENT", expected.comment));
}

TEST_CASE("An MP3 file carries the project's credits as ID3v2", "[engine][io][3035]") {
    constexpr int samples = 44100;
    const auto expected = credits();
    for (const auto& spec :
         {AudioFileSpec{.format = AudioFileFormat::mp3, .metadata = expected},
          AudioFileSpec{
              .format = AudioFileFormat::mp3, .bitrateKbps = 192, .metadata = expected}}) {
        INFO("bitrate " << spec.bitrateKbps);
        const auto file = writeThrough("credits.mp3", spec, material(2, samples));

        const auto frames = id3v2Frames(file);
        CHECK(frames.at("TIT2") == expected.title);
        CHECK(frames.at("TPE1") == expected.artist);
        CHECK(frames.at("TALB") == expected.album);
        CHECK(frames.at("TYER") == expected.date);
        CHECK(frames.at("TCON") == expected.genre);
        CHECK(frames.at("COMM") == expected.comment);

        // The LAME tag is rewritten in the first frame, after the ID3 tag
        // rather than over it.
        CHECK(mp3Tag(file).samples() == samples);
        const auto reader = readerFor(file);
        REQUIRE(reader != nullptr);
        CHECK(reader->sampleRate == 44100.0);
    }
}

TEST_CASE("An MP3 without credits has no ID3 tag", "[engine][io][3035]") {
    const auto file =
        writeThrough("bare.mp3", {.format = AudioFileFormat::mp3}, material(2, 44100));
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    CHECK(static_cast<const unsigned char*>(bytes.getData())[0] == 0xFF);
}

TEST_CASE("A rendered WAV reports its musical facts on re-import", "[engine][io][2771]") {
    AudioFileSpec spec;
    spec.bitDepth = 32;
    spec.metadata.tempo = 120.0;
    spec.metadata.beats = 4.0;
    spec.metadata.numerator = 6;
    spec.metadata.denominator = 8;
    spec.metadata.keyRoot = 9;
    spec.metadata.keyQuality = 1;
    spec.metadata.oneShot = true;
    spec.metadata.description = "MAGDA test render";
    spec.metadata.originator = "MAGDA test";

    auto file = writeThrough("musical.wav", spec, material(2, 88200));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    const auto& values = reader->metadataValues;
    CHECK(values[juce::WavAudioFormat::bwavDescription] == "MAGDA test render");
    CHECK(values[juce::WavAudioFormat::bwavOriginator] == "MAGDA test");
    CHECK(values[juce::WavAudioFormat::bwavOriginationDate].isNotEmpty());
    CHECK(values[juce::WavAudioFormat::bwavOriginationTime].isNotEmpty());
    CHECK(values[juce::WavAudioFormat::riffInfoKeywords] == "A minor");
    CHECK(values[juce::WavAudioFormat::acidizerFlag] == "1");

    const auto info =
        magda::engine::loopInfoFrom(values, reader->sampleRate, reader->lengthInSamples);
    REQUIRE(info.bpm);
    CHECK(*info.bpm == 120.0);
    REQUIRE(info.numBeats);
    CHECK(*info.numBeats == 4.0);
    CHECK(info.numerator == 6);
    CHECK(info.denominator == 8);
    CHECK(info.rootNote == 69);
    CHECK(info.oneShot == true);

    spec.metadata.tempo = 75.0;
    spec.metadata.beats = 2.5;
    file = writeThrough("partial-beats.wav", spec, material(2, 88200));
    reader.reset(wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    const auto partial = magda::engine::loopInfoFrom(reader->metadataValues, reader->sampleRate,
                                                     reader->lengthInSamples);
    REQUIRE(partial.numBeats);
    CHECK(*partial.numBeats == 2.5);
}

TEST_CASE("A WAV without render metadata has no BWF chunk", "[engine][io][2771]") {
    const auto file = writeThrough("plain-take.wav", {}, material(1, 128));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK_FALSE(reader->metadataValues.containsKey(juce::WavAudioFormat::bwavDescription));
}

TEST_CASE("BWF text is cut to what the chunk can hold", "[engine][io][2771]") {
    // The originator is "MAGDA " plus a git-describe version, which grows with
    // the commit count: over 32 bytes JUCE cuts it, and a caller comparing what
    // it asked for against what came back would call a good render a failure.
    magda::engine::AudioFileMetadata facts;
    facts.originator = "MAGDA 1.234.567-8901-gabcdef012-dirty";
    facts.description = juce::String::repeatedString("x", 300);

    const auto values = magda::engine::wavMetadataFor(facts);
    CHECK(values.at(juce::WavAudioFormat::bwavOriginator).getNumBytesAsUTF8() == 32);
    CHECK(values.at(juce::WavAudioFormat::bwavDescription).getNumBytesAsUTF8() == 256);

    AudioFileSpec spec;
    spec.bitDepth = 32;
    spec.metadata = facts;
    const auto file = writeThrough("long-bwf-text.wav", spec, material(1, 128));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->metadataValues[juce::WavAudioFormat::bwavOriginator] ==
          values.at(juce::WavAudioFormat::bwavOriginator));
    CHECK(reader->metadataValues[juce::WavAudioFormat::bwavDescription] ==
          values.at(juce::WavAudioFormat::bwavDescription));
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

    // Opus maps at most eight channels, and MP3 two.
    CHECK(AudioFileSink::create(file, {.format = AudioFileFormat::opus}, nine) == nullptr);
    CHECK(AudioFileSink::create(file, {.format = AudioFileFormat::mp3},
                                RenderContext{kSampleRate, 512, 3}) == nullptr);

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
    for (const auto format : {AudioFileFormat::wav, AudioFileFormat::flac, AudioFileFormat::opus,
                              AudioFileFormat::mp3}) {
        INFO("format " << static_cast<int>(format));

        const auto directory = emptyDirectory("full_disk");
        const auto file = directory.getChildFile("export" + magda::engine::extensionFor(format));

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
