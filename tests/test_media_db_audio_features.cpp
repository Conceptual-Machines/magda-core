// Tests for the Phase C audio feature extractor (issue #768).
// Mirrors prototypes/media_db/tests/test_features.py — synthetic 440 Hz sine
// and silence — and adds end-to-end coverage of the source-tier precedence
// (filename > metadata > DSP) for BPM and key.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "../magda/daw/media_db/AudioFeatures.hpp"

namespace fs = std::filesystem;

namespace {

class TempDir {
  public:
    TempDir() {
        path_ = fs::temp_directory_path() /
                ("magda_audio_features_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const fs::path& path() const {
        return path_;
    }

  private:
    fs::path path_;
};

// Write `data` (interleaved if stereo; mono here) as a 16-bit PCM WAV.
void writeMonoWav(const fs::path& out, const std::vector<float>& samples, int sampleRate) {
    juce::File jf(juce::String(out.string()));
    jf.deleteFile();

    juce::WavAudioFormat wav;
    juce::StringPairArray metadata;
    std::unique_ptr<juce::FileOutputStream> stream(jf.createOutputStream());
    REQUIRE(stream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate, 1, 16, metadata, 0));
    REQUIRE(writer != nullptr);
    stream.release();  // writer owns the stream now

    juce::AudioBuffer<float> buf(1, static_cast<int>(samples.size()));
    std::memcpy(buf.getWritePointer(0), samples.data(), samples.size() * sizeof(float));
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()));
    writer.reset();  // flushes
}

std::vector<float> generateSine(double freq, double durationS, int sampleRate, float amplitude) {
    const int n = static_cast<int>(durationS * sampleRate);
    std::vector<float> out(n);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    for (int i = 0; i < n; ++i) {
        out[i] = static_cast<float>(amplitude * std::sin(kTwoPi * freq * i / sampleRate));
    }
    return out;
}

}  // namespace

TEST_CASE("AudioFeatures: 440 Hz sine reports expected duration / RMS / centroid",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    auto samples = generateSine(440.0, 2.0, kSr, 0.5F);
    auto wav = dir.path() / "sine_440.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());

    REQUIRE(feats->sampleRate == kSr);
    REQUIRE(feats->channels == 1);
    REQUIRE(feats->durationS == Catch::Approx(2.0).epsilon(0.01));

    // RMS of a 0.5-amplitude sine is 0.5/sqrt(2) ≈ 0.354
    REQUIRE(feats->rms == Catch::Approx(0.5F / std::sqrt(2.0F)).epsilon(0.02));

    // Centroid should land near 440 Hz (Hann window's main-lobe spread
    // plus mean-of-frames smoothing leaves us within a couple of hundred Hz)
    REQUIRE(feats->spectralCentroid > 350.0F);
    REQUIRE(feats->spectralCentroid < 600.0F);
}

TEST_CASE("AudioFeatures: silence has RMS=0 and no transients", "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 22050;
    std::vector<float> samples(kSr, 0.0F);  // 1 s of zeros
    auto wav = dir.path() / "silence.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());

    REQUIRE(feats->rms == 0.0F);
    REQUIRE(feats->transientDensity == 0.0F);
    // Key detection on silence must NOT crash; result is allowed to be either
    // nullopt (preferred) or a low-confidence guess.
    if (feats->keyConfidence.has_value()) {
        REQUIRE(feats->keyConfidence.value() <= 1.0F);
    }
}

TEST_CASE("AudioFeatures: filename BPM trumps DSP", "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    auto samples = generateSine(440.0, 1.5, kSr, 0.3F);
    auto wav = dir.path() / "synth_140bpm.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());
    REQUIRE(feats->bpm == 140.0);  // from filename, not DSP
}

TEST_CASE("AudioFeatures: filename key trumps chroma DSP", "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    // Audio content is a C-ish sine; filename says F#m — filename must win.
    auto samples = generateSine(523.25, 2.0, kSr, 0.3F);  // C5
    auto wav = dir.path() / "synth_F#m.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());
    REQUIRE(feats->keyRoot == "F#");
    REQUIRE(feats->keyScale == "minor");
    REQUIRE_FALSE(feats->keyConfidence.has_value());  // filename-derived
}

TEST_CASE("AudioFeatures: DSP key fallback returns a confidence value",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    // 261.63 Hz = C4. No key marker in filename → chroma DSP must run.
    auto samples = generateSine(261.63, 3.0, kSr, 0.4F);
    auto wav = dir.path() / "tonal_no_marker.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());
    REQUIRE(feats->keyRoot.has_value());
    REQUIRE(feats->keyConfidence.has_value());
    REQUIRE(feats->keyConfidence.value() > 0.0F);
}

TEST_CASE("AudioFeatures returns nullopt for missing file", "[media_db][audio_features]") {
    REQUIRE_FALSE(magda::media::extractFeatures("/no/such/file.wav").has_value());
}
