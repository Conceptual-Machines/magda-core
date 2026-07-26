#include "AudioFeatures.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "PathRules.hpp"

namespace magda::media {

namespace {

// ---- FFT params (~23 ms windows at 44.1 kHz) -----------------------------
constexpr int kFftOrder = 10;
constexpr int kFftSize = 1 << kFftOrder;  // 1024
constexpr int kFftBins = kFftSize / 2;    // 512 (drop Nyquist for stats)
constexpr int kHopSize = kFftSize / 4;    // 75% overlap

using SimdFloat = juce::dsp::SIMDRegister<float>;
constexpr auto kSimdWidth = SimdFloat::SIMDNumElements;
static_assert(kFftBins % kSimdWidth == 0);

alignas(SimdFloat::SIMDRegisterSize) constexpr auto kBinIndices = [] {
    std::array<float, kFftBins> indices{};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<float>(i);
    }
    return indices;
}();

// ---- Krumhansl-Schmuckler key profiles (Krumhansl 1990) ------------------
// Unnormalized; we L2-normalize at correlation time to make scoring a cosine.
constexpr std::array<float, 12> kMajorProfile = {6.35F, 2.23F, 3.48F, 2.33F, 4.38F, 4.09F,
                                                 2.52F, 5.19F, 2.39F, 3.66F, 2.29F, 2.88F};
constexpr std::array<float, 12> kMinorProfile = {6.33F, 2.68F, 3.52F, 5.38F, 2.60F, 3.53F,
                                                 2.54F, 4.75F, 3.98F, 2.69F, 3.34F, 3.17F};

constexpr std::array<const char*, 12> kPitchClasses = {"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};

void normalize12(std::array<float, 12>& v) {
    float sumSq = 0;
    for (float x : v) {
        sumSq += x * x;
    }
    if (sumSq <= 0) {
        return;
    }
    const float inv = 1.0F / std::sqrt(sumSq);
    for (float& x : v) {
        x *= inv;
    }
}

const std::array<float, 12>& normalizedMajorProfile() {
    static const auto profile = [] {
        auto normalized = kMajorProfile;
        normalize12(normalized);
        return normalized;
    }();
    return profile;
}

const std::array<float, 12>& normalizedMinorProfile() {
    static const auto profile = [] {
        auto normalized = kMinorProfile;
        normalize12(normalized);
        return normalized;
    }();
    return profile;
}

// ---- File decode ---------------------------------------------------------

struct DecodedAudio {
    juce::AudioBuffer<float> mono;
    juce::StringPairArray metadata;
    int sampleRate;
    int channels;
    juce::int64 lengthSamples;
};

std::optional<DecodedAudio> decodeFile(const std::filesystem::path& path) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    juce::File jpath(juce::String(path.string()));
    if (!jpath.existsAsFile()) {
        return std::nullopt;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(jpath));
    if (!reader) {
        return std::nullopt;
    }
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0 || reader->numChannels < 1) {
        return std::nullopt;
    }

    DecodedAudio out;
    out.sampleRate = static_cast<int>(reader->sampleRate);
    out.channels = static_cast<int>(reader->numChannels);
    out.lengthSamples = reader->lengthInSamples;
    out.metadata = reader->metadataValues;

    const int len = static_cast<int>(out.lengthSamples);
    juce::AudioBuffer<float> multi(out.channels, len);
    multi.clear();
    reader->read(&multi, 0, len, 0, true, true);

    out.mono.setSize(1, len);
    out.mono.clear();
    const float gain = 1.0F / static_cast<float>(out.channels);
    for (int ch = 0; ch < out.channels; ++ch) {
        out.mono.addFrom(0, 0, multi, ch, 0, len, gain);
    }
    return out;
}

// ---- BPM tiers ----------------------------------------------------------

std::optional<double> metadataBpm(const juce::StringPairArray& meta) {
    // Acidized WAV files store tempo in an "acid" chunk; JUCE's WavAudioFormat
    // exposes it via metadataValues. The exact key spelling varies across JUCE
    // versions; check a few well-known variants.
    static constexpr std::array<const char*, 4> kKeys = {"acid tempo", "ACID Tempo", "tempo",
                                                         "bpm"};
    for (const auto* k : kKeys) {
        if (meta.containsKey(k)) {
            const double v = meta[k].getDoubleValue();
            if (v >= 30.0 && v <= 300.0) {
                return v;
            }
        }
    }
    return std::nullopt;
}

std::optional<double> dspBpm(const std::filesystem::path& path) {
    (void)path;
    return std::nullopt;
}

// ---- Spectral stats ----------------------------------------------------

struct SpectralStats {
    float centroid;
    float flatness;
    float transientDensity;
};

// ---- Chroma + Krumhansl-Schmuckler key ----------------------------------

struct ChromaKey {
    std::string root;
    std::string scale;
    float confidence;
};

std::optional<ChromaKey> computeKey(std::array<float, 12> chroma) {
    float chromaTotal = 0;
    for (float c : chroma) {
        chromaTotal += c;
    }
    if (chromaTotal <= 0) {
        return std::nullopt;
    }

    normalize12(chroma);
    const auto& normMajor = normalizedMajorProfile();
    const auto& normMinor = normalizedMinorProfile();

    float bestScore = -1.0F;
    int bestRoot = 0;
    std::string bestScale = "major";
    for (int root = 0; root < 12; ++root) {
        float scoreMajor = 0;
        float scoreMinor = 0;
        for (int i = 0; i < 12; ++i) {
            const int idx = (i + 12 - root) % 12;
            scoreMajor += chroma[i] * normMajor[idx];
            scoreMinor += chroma[i] * normMinor[idx];
        }
        if (scoreMajor > bestScore) {
            bestScore = scoreMajor;
            bestRoot = root;
            bestScale = "major";
        }
        if (scoreMinor > bestScore) {
            bestScore = scoreMinor;
            bestRoot = root;
            bestScale = "minor";
        }
    }
    return ChromaKey{std::string(kPitchClasses[bestRoot]), bestScale, std::max(0.0F, bestScore)};
}

struct FrameReductions {
    float magnitudeSum;
    float binWeightedSum;
    float positiveFlux;
};

FrameReductions reduceFrame(const float* magnitudes, float* previousMagnitudes) {
    auto magnitudeSum = SimdFloat::expand(0.0F);
    auto binWeightedSum = SimdFloat::expand(0.0F);
    auto positiveFlux = SimdFloat::expand(0.0F);
    const auto zero = SimdFloat::expand(0.0F);

    for (std::size_t offset = 0; offset < kFftBins; offset += kSimdWidth) {
        const auto magnitude = SimdFloat::fromRawArray(magnitudes + offset);
        const auto previous = SimdFloat::fromRawArray(previousMagnitudes + offset);
        const auto bins = SimdFloat::fromRawArray(kBinIndices.data() + offset);
        magnitudeSum += magnitude;
        binWeightedSum = SimdFloat::multiplyAdd(binWeightedSum, magnitude, bins);
        positiveFlux += SimdFloat::max(magnitude - previous, zero);
        magnitude.copyToRawArray(previousMagnitudes + offset);
    }

    return {magnitudeSum.sum(), binWeightedSum.sum(), positiveFlux.sum()};
}

std::array<signed char, kFftBins> makePitchClassLookup(int sampleRate) {
    std::array<signed char, kFftBins> lookup{};
    lookup.fill(-1);

    for (int bin = 1; bin < kFftBins; ++bin) {
        const float frequency = bin * static_cast<float>(sampleRate) / kFftSize;
        if (frequency < 27.5F || frequency > 4186.0F) {
            continue;
        }

        const float semis = 12.0F * std::log2(frequency / 440.0F);
        int pitchClass = static_cast<int>(std::lround(semis + 9.0F)) % 12;
        if (pitchClass < 0) {
            pitchClass += 12;
        }
        lookup[bin] = static_cast<signed char>(pitchClass);
    }
    return lookup;
}

struct SpectralAnalysis {
    SpectralStats stats{0.0F, 0.0F, 0.0F};
    std::optional<ChromaKey> key;
};

SpectralAnalysis computeSpectralAnalysis(const juce::AudioBuffer<float>& mono, int sampleRate,
                                         bool analyseKey) {
    SpectralAnalysis out;
    const int numSamples = mono.getNumSamples();
    if (numSamples < kFftSize) {
        return out;
    }

    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann);

    alignas(SimdFloat::SIMDRegisterSize) std::array<float, kFftSize * 2> fftData{};
    alignas(SimdFloat::SIMDRegisterSize) std::array<float, kFftBins> previousMagnitudes{};
    std::array<float, 12> chroma{};
    const auto pitchClasses =
        analyseKey ? makePitchClassLookup(sampleRate) : std::array<signed char, kFftBins>{};
    std::vector<float> flux;
    flux.reserve(static_cast<size_t>(numSamples / kHopSize));

    const float* src = mono.getReadPointer(0);
    const float binFrequency = static_cast<float>(sampleRate) / kFftSize;
    double sumCentroid = 0;
    double sumFlatness = 0;
    int frames = 0;

    for (int start = 0; start + kFftSize <= numSamples; start += kHopSize) {
        std::memcpy(fftData.data(), src + start, static_cast<size_t>(kFftSize) * sizeof(float));
        std::memset(fftData.data() + kFftSize, 0, static_cast<size_t>(kFftSize) * sizeof(float));
        window.multiplyWithWindowingTable(fftData.data(), kFftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

        const auto reductions = reduceFrame(fftData.data(), previousMagnitudes.data());
        if (reductions.magnitudeSum > 0) {
            sumCentroid += reductions.binWeightedSum * binFrequency / reductions.magnitudeSum;
        }

        double logSum = 0;
        for (int bin = 0; bin < kFftBins; ++bin) {
            logSum += std::log(fftData[bin] + 1e-10F);
        }
        const double geomMean = std::exp(logSum / kFftBins);
        const double arithMean = reductions.magnitudeSum / kFftBins;
        if (arithMean > 0) {
            sumFlatness += geomMean / arithMean;
        }

        flux.push_back(reductions.positiveFlux);

        if (analyseKey) {
            for (int bin = 1; bin < kFftBins; ++bin) {
                const int pitchClass = pitchClasses[bin];
                if (pitchClass >= 0) {
                    chroma[static_cast<std::size_t>(pitchClass)] += fftData[bin];
                }
            }
        }
        ++frames;
    }

    if (frames > 0) {
        out.stats.centroid = static_cast<float>(sumCentroid / frames);
        out.stats.flatness = static_cast<float>(sumFlatness / frames);
    }

    // Transient density: peak picking on the flux envelope above 1.5×mean.
    // Simple but matches the prototype's order of magnitude on Splice samples.
    if (flux.size() >= 3) {
        double meanFlux = 0;
        for (float value : flux) {
            meanFlux += value;
        }
        meanFlux /= flux.size();
        const auto threshold = static_cast<float>(meanFlux * 1.5);
        int peaks = 0;
        for (std::size_t i = 1; i + 1 < flux.size(); ++i) {
            if (flux[i] > flux[i - 1] && flux[i] > flux[i + 1] && flux[i] > threshold) {
                ++peaks;
            }
        }
        const double durationS = static_cast<double>(numSamples) / sampleRate;
        if (durationS > 0) {
            out.stats.transientDensity = static_cast<float>(peaks / durationS);
        }
    }

    if (analyseKey) {
        out.key = computeKey(chroma);
    }
    return out;
}

}  // namespace

std::optional<AudioFeatures> extractFeatures(const std::filesystem::path& path) {
    auto decoded = decodeFile(path);
    if (!decoded) {
        return std::nullopt;
    }

    AudioFeatures f;
    f.sampleRate = decoded->sampleRate;
    f.channels = decoded->channels;
    f.durationS = static_cast<double>(decoded->lengthSamples) / decoded->sampleRate;

    // --- BPM: filename > metadata ---
    if (auto p = parseBpmFromPath(path)) {
        f.bpm = *p;
    } else if (auto m = metadataBpm(decoded->metadata)) {
        f.bpm = *m;
    } else if (auto d = dspBpm(path)) {
        f.bpm = *d;
    }

    // --- Key: filename > DSP ---
    // Metadata-encoded keys (ACID root note) exist but are rare and decode
    // formats vary; punt to a future pass when we have a representative corpus.
    const auto parsedKey = parseKeyFromPath(path);
    if (parsedKey) {
        const auto& pk = *parsedKey;
        f.keyRoot = pk.root;
        f.keyScale = pk.scale;
        // No DSP confidence — filename evidence is treated as ground truth.
    }

    // Spectral statistics and optional chroma share a single windowed FFT pass.
    const auto analysis =
        computeSpectralAnalysis(decoded->mono, decoded->sampleRate, !parsedKey.has_value());
    if (!parsedKey && analysis.key) {
        const auto& ck = *analysis.key;
        f.keyRoot = ck.root;
        f.keyScale = ck.scale;
        f.keyConfidence = ck.confidence;
    }

    // --- Always-DSP spectral stats ---
    f.rms = decoded->mono.getRMSLevel(0, 0, decoded->mono.getNumSamples());
    f.spectralCentroid = analysis.stats.centroid;
    f.spectralFlatness = analysis.stats.flatness;
    f.transientDensity = analysis.stats.transientDensity;

    return f;
}

}  // namespace magda::media
