// Tests for the Phase C audio feature extractor (issue #768).
// Mirrors prototypes/media_db/tests/test_features.py — synthetic 440 Hz sine
// and silence — and adds end-to-end coverage of where a key and a tempo come
// from: the name for a key, the audio for a tempo (#2674).

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

#include "../../magda/daw/media_db/AudioFeatures.hpp"

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

/// Impulses on a grid: `subdivision` of them per beat at `bpm`. What a drum
/// loop's onset envelope looks like with nothing else in it.
std::vector<float> generateClickTrain(double bpm, double durationS, int sampleRate,
                                      int subdivision = 1) {
    std::vector<float> out(static_cast<std::size_t>(durationS * sampleRate), 0.0F);
    const double period = 60.0 / bpm / subdivision;
    for (double t = 0.0; static_cast<std::size_t>(t * sampleRate) < out.size(); t += period) {
        out[static_cast<std::size_t>(t * sampleRate)] = 0.9F;
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
    REQUIRE(feats->spectralFlatness >= 0.0F);
    REQUIRE(feats->spectralFlatness < 0.1F);
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

// A name is a claim, and a claim only picks an octave of what the audio
// measured (#2674). A sine measured nothing, so the 140 has nothing to pick.
TEST_CASE("AudioFeatures: a filename BPM is not a tempo on its own", "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    auto samples = generateSine(440.0, 1.5, kSr, 0.3F);
    auto wav = dir.path() / "synth_140bpm.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());
    REQUIRE_FALSE(feats->bpm.has_value());
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
    REQUIRE(feats->keyConfidence.value() <= 1.0F);
}

TEST_CASE("AudioFeatures: click train reports finite spectral stats and transients",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    constexpr double kDurationS = 2.0;
    std::vector<float> samples(static_cast<std::size_t>(kDurationS * kSr), 0.0F);
    for (int click = kSr / 8; click < static_cast<int>(samples.size()); click += kSr / 4) {
        samples[static_cast<std::size_t>(click)] = 0.9F;
    }
    auto wav = dir.path() / "click_train.wav";
    writeMonoWav(wav, samples, kSr);

    auto feats = magda::media::extractFeatures(wav);
    REQUIRE(feats.has_value());
    REQUIRE(std::isfinite(feats->spectralCentroid));
    REQUIRE(std::isfinite(feats->spectralFlatness));
    REQUIRE(feats->spectralCentroid > 0.0F);
    REQUIRE(feats->spectralFlatness >= 0.0F);
    REQUIRE(feats->spectralFlatness <= 1.0F);
    REQUIRE(feats->transientDensity > 2.0F);
    REQUIRE(feats->transientDensity < 6.0F);
}

TEST_CASE("AudioFeatures returns nullopt for missing file", "[media_db][audio_features]") {
    REQUIRE_FALSE(magda::media::extractFeatures("/no/such/file.wav").has_value());
}

// What a clip asks for outside a scan: the audio, settled by the claims (#2674).
TEST_CASE("AudioFeatures: detectTempo answers nothing for material with no tempo in it",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;

    auto named = dir.path() / "riff_128bpm.wav";
    writeMonoWav(named, generateSine(440.0, 2.0, kSr, 0.3F), kSr);
    REQUIRE_FALSE(magda::media::detectTempo(named, nullptr).has_value());

    auto pad = dir.path() / "pad.wav";
    writeMonoWav(pad, generateSine(220.0, 3.0, kSr, 0.3F), kSr);
    REQUIRE_FALSE(magda::media::detectTempo(pad, nullptr).has_value());
}

TEST_CASE("AudioFeatures: the audio outvotes a name that disagrees with it",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    auto wav = dir.path() / "groove_120bpm.wav";
    writeMonoWav(wav, generateClickTrain(128.0, 8.0, kSr), kSr);

    const auto bpm = magda::media::detectTempo(wav, nullptr);
    REQUIRE(bpm.has_value());
    // 120 is no octave of 128, so it is dropped. 8 s is 17 beats at 128 and the
    // whole-beat snap takes the reading that makes that exact.
    REQUIRE(*bpm == Catch::Approx(127.5).margin(0.2));
}

TEST_CASE("AudioFeatures: a name picks the octave of what the audio measured",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    constexpr double kDurationS = 16.0 * 60.0 / 87.0;  // 16 beats at 87, 32 at 174
    auto wav = dir.path() / "loop_174bpm.wav";
    writeMonoWav(wav, generateClickTrain(87.0, kDurationS, kSr), kSr);

    const auto bpm = magda::media::detectTempo(wav, nullptr);
    REQUIRE(bpm.has_value());
    REQUIRE(*bpm == Catch::Approx(174.0).margin(0.1));
}

TEST_CASE("AudioFeatures: a name picks the 3:2 relation of a bare 16th grid",
          "[media_db][audio_features]") {
    TempDir dir;
    constexpr int kSr = 44100;
    constexpr double kDurationS = 16.0 * 60.0 / 174.0;  // 5.5172 s

    // 16ths at 174 with no accent are uniform, and the strongest period in them
    // is six of those: 116 bpm, two thirds of the tempo. The name picks the
    // 3:2 relation the way it would pick an octave; without a name the field
    // stays empty for MediaDbIndexer::measureMissingTempo (#2674).
    auto hats = dir.path() / "hats_174bpm.wav";
    writeMonoWav(hats, generateClickTrain(174.0, kDurationS, kSr, 4), kSr);
    const auto named = magda::media::detectTempo(hats, nullptr);
    REQUIRE(named.has_value());
    REQUIRE(*named == Catch::Approx(174.0).margin(0.1));
    auto bare = dir.path() / "hats.wav";
    writeMonoWav(bare, generateClickTrain(174.0, kDurationS, kSr, 4), kSr);
    REQUIRE_FALSE(magda::media::detectTempo(bare, nullptr).has_value());

    // Pulsed on the beat instead, the same length is the case that started
    // #2674: the audio names 174 with no help from anything.
    auto beat = dir.path() / "hats_beat.wav";
    writeMonoWav(beat, generateClickTrain(174.0, kDurationS, kSr), kSr);
    const auto bpm = magda::media::detectTempo(beat, nullptr);
    REQUIRE(bpm.has_value());
    REQUIRE(*bpm == Catch::Approx(174.0).margin(0.1));
}
