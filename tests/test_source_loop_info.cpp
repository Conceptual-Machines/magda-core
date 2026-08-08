#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/SourceLoopInfo.hpp"

/**
 * What a file says about its own tempo (#2038).
 *
 * The model seeds an event's interpretation from this and then owns it, and the
 * seeding rule is that it never overwrites what the user set. That rule only
 * works if "the file said nothing" is distinguishable from "the file said
 * zero", which is why every field is optional and why most of what is below is
 * about absence rather than about values.
 *
 * A metadata map rather than a file, because that is all the parse ever sees.
 * No fixture, no format to register, and the acid chunks a loop library writes
 * are reproducible here exactly.
 */

using magda::engine::loopInfoFrom;

namespace {

constexpr double kSampleRate = 44100.0;

/// Two seconds, which at four beats is 120 bpm.
constexpr std::int64_t kTwoSeconds = 88200;

Catch::Approx approx(double value) {
    return Catch::Approx(value).margin(1e-6);
}

juce::StringPairArray acid(double beats, const char* tempo = nullptr) {
    juce::StringPairArray metadata;
    metadata.set(juce::WavAudioFormat::acidBeats, juce::String(beats));

    if (tempo != nullptr)
        metadata.set(juce::WavAudioFormat::acidTempo, tempo);

    return metadata;
}

}  // namespace

TEST_CASE("A file that says nothing is read as having said nothing", "[engine][io][loop-info]") {
    const auto info = loopInfoFrom({}, kSampleRate, kTwoSeconds);

    REQUIRE_FALSE(info.bpm.has_value());
    REQUIRE_FALSE(info.numBeats.has_value());
    REQUIRE_FALSE(info.numerator.has_value());
    REQUIRE_FALSE(info.denominator.has_value());
    REQUIRE_FALSE(info.rootNote.has_value());
    REQUIRE_FALSE(info.oneShot.has_value());
}

TEST_CASE("An acid chunk's beat count becomes a tempo", "[engine][io][loop-info]") {
    const auto info = loopInfoFrom(acid(4.0), kSampleRate, kTwoSeconds);

    REQUIRE(info.numBeats.has_value());
    REQUIRE(*info.numBeats == approx(4.0));
    REQUIRE(info.bpm.has_value());
    REQUIRE(*info.bpm == approx(120.0));
}

TEST_CASE("A tempo the file wrote is believed over one worked out from its length",
          "[engine][io][loop-info]") {
    // Four beats over two seconds is 120, but the file says 174, and a file
    // that wrote its tempo knows something a division does not.
    const auto info = loopInfoFrom(acid(4.0, "174"), kSampleRate, kTwoSeconds);

    REQUIRE(*info.bpm == approx(174.0));
    REQUIRE(*info.numBeats == approx(4.0));
}

TEST_CASE("A tempo with no beat count gives one", "[engine][io][loop-info]") {
    juce::StringPairArray metadata;
    metadata.set(juce::WavAudioFormat::acidTempo, "120");

    const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

    REQUIRE(*info.bpm == approx(120.0));
    REQUIRE(*info.numBeats == approx(4.0));
}

TEST_CASE("Nothing is inferred without a length to infer it from", "[engine][io][loop-info]") {
    SECTION("no rate") {
        const auto info = loopInfoFrom(acid(4.0), 0.0, kTwoSeconds);

        REQUIRE(*info.numBeats == approx(4.0));
        REQUIRE_FALSE(info.bpm.has_value());
    }

    SECTION("no samples") {
        const auto info = loopInfoFrom(acid(4.0), kSampleRate, 0);

        REQUIRE(*info.numBeats == approx(4.0));
        REQUIRE_FALSE(info.bpm.has_value());
    }
}

TEST_CASE("A root note counts only when the file marked it as set", "[engine][io][loop-info]") {
    juce::StringPairArray metadata = acid(4.0);
    metadata.set(juce::WavAudioFormat::acidRootNote, "60");

    SECTION("unset means there is no root, whatever the note field holds") {
        REQUIRE_FALSE(loopInfoFrom(metadata, kSampleRate, kTwoSeconds).rootNote.has_value());
    }

    SECTION("set means there is") {
        metadata.set(juce::WavAudioFormat::acidRootSet, "1");

        const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

        REQUIRE(info.rootNote.has_value());
        REQUIRE(*info.rootNote == 60);
    }

    SECTION("a file with no acid chunk falls back to its sampler chunk") {
        juce::StringPairArray sampler;
        sampler.set("MidiUnityNote", "48");

        const auto info = loopInfoFrom(sampler, kSampleRate, kTwoSeconds);

        REQUIRE(info.rootNote.has_value());
        REQUIRE(*info.rootNote == 48);
    }
}

TEST_CASE("A one shot says so rather than being guessed at", "[engine][io][loop-info]") {
    juce::StringPairArray metadata = acid(4.0);

    SECTION("absent") {
        REQUIRE_FALSE(loopInfoFrom(metadata, kSampleRate, kTwoSeconds).oneShot.has_value());
    }

    SECTION("set") {
        metadata.set(juce::WavAudioFormat::acidOneShot, "1");

        const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

        REQUIRE(info.oneShot.has_value());
        REQUIRE(*info.oneShot);
    }

    SECTION("written and false") {
        metadata.set(juce::WavAudioFormat::acidOneShot, "0");

        const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

        REQUIRE(info.oneShot.has_value());
        REQUIRE_FALSE(*info.oneShot);
    }
}

TEST_CASE("Time signatures come off either spelling", "[engine][io][loop-info]") {
    SECTION("the acid chunk's two fields") {
        juce::StringPairArray metadata = acid(4.0);
        metadata.set(juce::WavAudioFormat::acidNumerator, "3");
        metadata.set(juce::WavAudioFormat::acidDenominator, "4");

        const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

        REQUIRE(*info.numerator == 3);
        REQUIRE(*info.denominator == 4);
    }

    SECTION("the plainer one AIFF writes") {
        juce::StringPairArray metadata;
        metadata.set("time signature", "6/8");

        const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

        REQUIRE(*info.denominator == 6);
        REQUIRE(*info.numerator == 8);
    }
}

TEST_CASE("An empty value said no more than a missing key", "[engine][io][loop-info]") {
    juce::StringPairArray metadata;
    metadata.set(juce::WavAudioFormat::acidBeats, "");
    metadata.set(juce::WavAudioFormat::acidTempo, "");

    const auto info = loopInfoFrom(metadata, kSampleRate, kTwoSeconds);

    REQUIRE_FALSE(info.bpm.has_value());
    REQUIRE_FALSE(info.numBeats.has_value());
}
