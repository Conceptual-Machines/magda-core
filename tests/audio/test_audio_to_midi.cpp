// Audio-to-MIDI transcription tests (issue #1168).
//
// Two layers:
//   1. BasicPitchPostprocess.decodeNotes on a hand-built posteriorgram — pure,
//      no model, validates the note-decoding port deterministically.
//   2. BasicPitchTranscriber end-to-end on a synthesized tone, using the
//      checked-in 230 KB model (resources/models/basic_pitch.onnx).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

#include "../magda/daw/transcription/BasicPitchPostprocess.hpp"
#include "../magda/daw/transcription/BasicPitchTranscriber.hpp"

namespace bp = magda::transcription::basicpitch;
namespace fs = std::filesystem;

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

std::string modelPath() {
    return std::string(MAGDA_REPO_ROOT) + "/resources/models/basic_pitch.onnx";
}

}  // namespace

TEST_CASE("decodeNotes recovers a single sustained note", "[transcription][postprocess]") {
    constexpr int kFrames = 100;
    constexpr int kFreqBin = 48;  // MIDI 69 (A4) == bin 48 + MIDI_OFFSET 21
    constexpr int kOnsetFrame = 10;
    constexpr int kOffFrame = 60;

    bp::ModelOutput out;
    out.nFrames = kFrames;
    out.note.assign(static_cast<size_t>(kFrames) * bp::kNoteFreqBins, 0.0F);
    out.onset.assign(static_cast<size_t>(kFrames) * bp::kNoteFreqBins, 0.0F);
    out.contour.assign(static_cast<size_t>(kFrames) * bp::kContourFreqBins, 0.0F);

    // Sustained frame energy on the note bin for [onset, off).
    for (int t = kOnsetFrame; t < kOffFrame; ++t)
        out.note[static_cast<size_t>(t) * bp::kNoteFreqBins + kFreqBin] = 0.9F;
    // A single onset peak at the start.
    out.onset[static_cast<size_t>(kOnsetFrame) * bp::kNoteFreqBins + kFreqBin] = 1.0F;
    // Contour energy at the matching contour bin (3 bins/semitone).
    const int contourBin = static_cast<int>(std::lround(
        12.0 * bp::kContoursBinsPerSemitone * std::log2(440.0 / bp::kAnnotationsBaseFrequency)));
    for (int t = kOnsetFrame; t < kOffFrame; ++t)
        out.contour[static_cast<size_t>(t) * bp::kContourFreqBins + contourBin] = 1.0F;

    bp::PostprocessParams params;
    const auto notes = bp::decodeNotes(out, params);

    REQUIRE(notes.size() == 1);
    CHECK(notes[0].pitch == 69);
    CHECK(notes[0].startFrame == kOnsetFrame);
    CHECK(notes[0].endFrame == kOffFrame);
    CHECK(notes[0].amplitude == Catch::Approx(0.9F).epsilon(0.01));
}

TEST_CASE("frameToTimeSeconds is monotonic and starts near zero", "[transcription][postprocess]") {
    CHECK(bp::frameToTimeSeconds(0) == Catch::Approx(0.0).margin(0.01));
    double prev = -1.0;
    for (int f = 0; f < 200; ++f) {
        const double t = bp::frameToTimeSeconds(f);
        CHECK(t > prev);
        prev = t;
    }
}

TEST_CASE("BasicPitchTranscriber transcribes an A4 tone", "[transcription][needs-model]") {
    if (!fs::exists(modelPath()))
        SKIP("bundled model missing: " + modelPath());

    magda::transcription::BasicPitchTranscriber transcriber(modelPath());
    if (!transcriber.isLoaded())
        SKIP("ONNX backend unavailable on this build");

    // ~1.5s of a harmonic-rich A4 (440 Hz) at 22050 Hz so the model sees a
    // note-like spectrum rather than a bare sinusoid.
    constexpr int kSr = 22050;
    constexpr double kFreq = 440.0;
    std::vector<float> tone(static_cast<size_t>(kSr * 3 / 2), 0.0F);
    for (int i = 0; i < static_cast<int>(tone.size()); ++i) {
        const double t = static_cast<double>(i) / kSr;
        double s = 0.6 * std::sin(kTwoPi * kFreq * t);
        s += 0.25 * std::sin(kTwoPi * 2.0 * kFreq * t);
        s += 0.12 * std::sin(kTwoPi * 3.0 * kFreq * t);
        tone[static_cast<size_t>(i)] = static_cast<float>(s * 0.5);
    }

    const auto notes = transcriber.transcribe(tone.data(), static_cast<int>(tone.size()), kSr);

    REQUIRE_FALSE(notes.empty());
    // The dominant detected pitch should be A4 (69), within a semitone.
    bool foundA4 = false;
    for (const auto& n : notes) {
        if (std::abs(n.pitch - 69) <= 1)
            foundA4 = true;
    }
    CHECK(foundA4);
}
