// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpeechAnalysis.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace magda::audio {
namespace {

constexpr double kHopSeconds = 0.01;
constexpr double kFeatureRate = 16000.0;
constexpr int kFeatureHop = 160;
constexpr int kWindow = 640;   // 40 ms: three periods of a 75 Hz voice
constexpr int kFftOrder = 11;  // 2048 >= 2 * kWindow, so the autocorrelation does not wrap
constexpr int kFftSize = 1 << kFftOrder;
constexpr int kMinLag = 32;                // 500 Hz
constexpr int kMaxLag = 213;               // 75 Hz
constexpr int kFlatLowBin = 13;            // 100 Hz
constexpr int kFlatHighBin = 896;          // 7 kHz
constexpr float kVoicedThreshold = 0.45f;  // Praat's voicing threshold
constexpr double kBridgeSeconds = 0.05;    // a dip shorter than this stays inside a sound
constexpr double kMinSoundSeconds = 0.04;  // a shorter sound is a click
constexpr double kRoomMarginDb = 10.0;     // periodicity this close to the floor is hum
// Measured on a real take: room and body sounds with pitch sat 17-26 dB under the voice.
constexpr double kVoiceBelowDb = 15.0;
constexpr double kConsonantBelowDb = 20.0;
constexpr double kMinVoiceSeconds = 0.15;
constexpr double kBreathMaxVoiced = 0.3;
constexpr double kBreathMinFlatness = 0.1;
constexpr double kBreathBelowVoiceDb = 8.0;

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    float process(float x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2) {
    return {.b0 = b0 / a0, .b1 = b1 / a0, .b2 = b2 / a0, .a1 = a1 / a0, .a2 = a2 / a0};
}

struct Design {
    double cosine;
    double alpha;
};

Design design(double rate, double fc, double q) {
    const double w0 = 2.0 * std::numbers::pi * fc / rate;
    return {std::cos(w0), std::sin(w0) / (2.0 * q)};
}

// BS.1770 K-weighting at any rate, as libebur128 designs it; at 48 kHz it gives the spec's
// coefficients exactly.
Biquad kShelf(double rate) {
    constexpr double q = 0.7071752369554196;
    const double k = std::tan(std::numbers::pi * 1681.974450955533 / rate);
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    return normalised(vh + vb * k / q + k * k, 2.0 * (k * k - vh), vh - vb * k / q + k * k,
                      1.0 + k / q + k * k, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
}

Biquad kHighPass(double rate) {
    constexpr double q = 0.5003270373238773;
    const double k = std::tan(std::numbers::pi * 38.13547087602444 / rate);
    const double a0 = 1.0 + k / q + k * k;
    return {.b0 = 1.0,
            .b1 = -2.0,
            .b2 = 1.0,
            .a1 = 2.0 * (k * k - 1.0) / a0,
            .a2 = (1.0 - k / q + k * k) / a0};
}

Biquad lowPass(double rate, double fc, double q) {
    const auto [c, alpha] = design(rate, fc, q);
    return normalised((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

double toDb(double power) {
    return power > 1.0e-20 ? 10.0 * std::log10(power) : -200.0;
}

double percentile(std::vector<double> values, double fraction) {
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(index));
    return values[index];
}

struct Frame {
    double levelDb = -200.0;
    double kPower = 0.0;
    float voicing = 0.0f;
    float flatness = 0.0f;
};

enum class SoundKind { Voice, Breath, Consonant, Room };

/// A run of frames above the threshold, [first, last].
struct Sound {
    std::size_t first = 0;
    std::size_t last = 0;
    SoundKind kind = SoundKind::Room;
};

}  // namespace

struct SpeechAnalyser::Impl {
    Impl(double sampleRate, int channels, SpeechAnalysisOptions opts)
        : rate(sampleRate),
          numChannels(std::max(1, channels)),
          options(opts),
          hopSamples(std::max(1, static_cast<int>(std::lround(sampleRate * kHopSeconds)))),
          step(sampleRate / kFeatureRate),
          kFilters(static_cast<std::size_t>(numChannels)) {
        for (auto& filters : kFilters)
            filters = {kShelf(rate), kHighPass(rate)};
        // Fourth-order Butterworth below the 8 kHz Nyquist of the feature rate.
        if (rate > kFeatureRate)
            antiAlias = {lowPass(rate, 7000.0, 0.5411961), lowPass(rate, 7000.0, 1.3065630)};

        hann.resize(kWindow);
        for (int i = 0; i < kWindow; ++i)
            hann[static_cast<std::size_t>(i)] = static_cast<float>(
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (i + 0.5) / kWindow));
        // The window's own autocorrelation, which a voiced frame's is divided by (Boersma 1993).
        windowCorrelation.resize(kMaxLag + 1);
        double zero = 0.0;
        for (int i = 0; i < kWindow; ++i)
            zero += hann[static_cast<std::size_t>(i)] * hann[static_cast<std::size_t>(i)];
        for (int lag = 0; lag <= kMaxLag; ++lag) {
            double sum = 0.0;
            for (int i = 0; i + lag < kWindow; ++i)
                sum += hann[static_cast<std::size_t>(i)] * hann[static_cast<std::size_t>(i + lag)];
            windowCorrelation[static_cast<std::size_t>(lag)] = static_cast<float>(sum / zero);
        }
        scratch.resize(2 * kFftSize);
    }

    void process(const float* const* channels, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            double mono = 0.0;
            for (int ch = 0; ch < numChannels; ++ch) {
                const float x = channels[ch][i];
                mono += x;
                auto& [shelf, high] = kFilters[static_cast<std::size_t>(ch)];
                const float k = high.process(shelf.process(x));
                hopK += static_cast<double>(k) * k;
            }
            mono /= numChannels;
            hopLevel += mono * mono;
            if (++hopCount == hopSamples)
                closeHop();
            resample(static_cast<float>(mono));
        }
        totalSamples += numSamples;
        computeFeatures(false);
    }

    void closeHop() {
        frames.push_back({.levelDb = toDb(hopLevel / hopCount), .kPower = hopK / hopCount});
        hopK = hopLevel = 0.0;
        hopCount = 0;
    }

    /// Linear interpolation is enough after the anti-alias filter: the features are band energies.
    void resample(float x) {
        if (rate > kFeatureRate)
            x = antiAlias[1].process(antiAlias[0].process(x));
        while (nextPosition <= static_cast<double>(inputIndex)) {
            const auto fraction =
                static_cast<float>(nextPosition - static_cast<double>(inputIndex - 1));
            low.push_back(previous + fraction * (x - previous));
            nextPosition += step;
        }
        previous = x;
        ++inputIndex;
    }

    void computeFeatures(bool flush) {
        for (; featuresDone < frames.size(); ++featuresDone) {
            const auto centre =
                static_cast<std::int64_t>(featuresDone) * kFeatureHop + kFeatureHop / 2;
            const auto start = centre - kWindow / 2;
            if (!flush && lowBase + static_cast<std::int64_t>(low.size()) < start + kWindow)
                break;
            measure(featuresDone, start);
        }
        const auto keepFrom =
            static_cast<std::int64_t>(featuresDone) * kFeatureHop + kFeatureHop / 2 - kWindow / 2;
        if (const auto drop =
                std::min<std::int64_t>(keepFrom - lowBase, static_cast<std::int64_t>(low.size()));
            drop > 0) {
            low.erase(low.begin(), low.begin() + drop);
            lowBase += drop;
        }
    }

    void measure(std::size_t index, std::int64_t start) {
        std::ranges::fill(scratch, 0.0f);
        double energy = 0.0;
        for (int i = 0; i < kWindow; ++i) {
            const auto at = start + i - lowBase;
            const float x = at >= 0 && at < static_cast<std::int64_t>(low.size())
                                ? low[static_cast<std::size_t>(at)]
                                : 0.0f;
            const float windowed = x * hann[static_cast<std::size_t>(i)];
            scratch[static_cast<std::size_t>(i)] = windowed;
            energy += static_cast<double>(windowed) * windowed;
        }
        if (energy < 1.0e-12)
            return;

        fft.performRealOnlyForwardTransform(scratch.data());
        double logSum = 0.0;
        double sum = 0.0;
        for (int bin = 0; bin < kFftSize; ++bin) {
            const auto re = scratch[static_cast<std::size_t>(2 * bin)];
            const auto im = scratch[static_cast<std::size_t>(2 * bin + 1)];
            const double power = static_cast<double>(re) * re + static_cast<double>(im) * im;
            scratch[static_cast<std::size_t>(2 * bin)] = static_cast<float>(power);
            scratch[static_cast<std::size_t>(2 * bin + 1)] = 0.0f;
            if (bin >= kFlatLowBin && bin <= kFlatHighBin) {
                logSum += std::log(power + 1.0e-20);
                sum += power;
            }
        }
        constexpr double bins = kFlatHighBin - kFlatLowBin + 1;
        auto& frame = frames[index];
        frame.flatness =
            sum > 0.0 ? static_cast<float>(std::exp(logSum / bins) / (sum / bins)) : 0.0f;

        fft.performRealOnlyInverseTransform(scratch.data());
        const float zero = scratch[0];
        if (zero <= 0.0f)
            return;
        float best = 0.0f;
        for (int lag = kMinLag; lag <= kMaxLag; ++lag)
            best = std::max(best, scratch[static_cast<std::size_t>(lag)] / zero /
                                      windowCorrelation[static_cast<std::size_t>(lag)]);
        frame.voicing = std::min(best, 1.0f);
    }

    SpeechAnalysisResult finish() {
        if (hopCount > 0)
            closeHop();
        computeFeatures(true);
        return segment();
    }

    double timeOf(std::size_t frame) const {
        return std::min(static_cast<double>(frame) * kHopSeconds, duration());
    }
    double duration() const {
        return static_cast<double>(totalSamples) / rate;
    }
    bool voiced(std::size_t frame) const {
        return frames[frame].voicing > kVoicedThreshold;
    }

    struct Levels {
        double floor = 0.0;
        double threshold = 0.0;
        double voice = 0.0;
    };

    /// The room, the level that counts as sound, and the voice's median level above it.
    Levels levelsOf(const std::vector<double>& levels) const {
        Levels out;
        out.floor = percentile(levels, 0.10);
        const double speech = percentile(levels, 0.95);
        // Between the room and the voice, low enough to keep breaths, never inside the floor.
        out.threshold = options.thresholdDb.value_or(
            std::max(std::min(out.floor + std::max(6.0, 0.3 * (speech - out.floor)), speech - 35.0),
                     out.floor + 3.0));
        std::vector<double> voiced;
        for (std::size_t i = 0; i < frames.size(); ++i)
            if (frames[i].levelDb > std::max(out.threshold, out.floor + kRoomMarginDb) &&
                voicedAbove(i, out.floor))
                voiced.push_back(frames[i].levelDb);
        out.voice = voiced.empty() ? speech : percentile(voiced, 0.5);
        return out;
    }

    /// Voiced, and far enough above the room that the periodicity is not the room's hum.
    bool voicedAbove(std::size_t frame, double floor) const {
        return voiced(frame) && frames[frame].levelDb > floor + kRoomMarginDb;
    }

    std::vector<Sound> soundsAbove(double threshold) const {
        const auto bridge = static_cast<std::size_t>(std::lround(kBridgeSeconds / kHopSeconds));
        const auto shortest = static_cast<std::size_t>(std::lround(kMinSoundSeconds / kHopSeconds));
        std::vector<Sound> sounds;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].levelDb <= threshold)
                continue;
            if (!sounds.empty() && i - sounds.back().last <= bridge)
                sounds.back().last = i;
            else
                sounds.push_back({.first = i, .last = i});
        }
        std::erase_if(sounds, [&](const Sound& s) { return s.last - s.first + 1 < shortest; });
        return sounds;
    }

    SoundKind kindOf(const Sound& sound, const Levels& levels) const {
        std::size_t voicedFrames = 0;
        double flatness = 0.0;
        double power = 0.0;
        for (auto i = sound.first; i <= sound.last; ++i) {
            voicedFrames += voicedAbove(i, levels.floor) ? 1 : 0;
            flatness += frames[i].flatness;
            power += std::pow(10.0, frames[i].levelDb / 10.0);
        }
        const auto count = static_cast<double>(sound.last - sound.first + 1);
        const double level = toDb(power / count);
        const double length = timeOf(sound.last + 1) - timeOf(sound.first);
        if (length >= options.breathMinSeconds && length <= options.breathMaxSeconds &&
            static_cast<double>(voicedFrames) / count < kBreathMaxVoiced &&
            flatness / count > kBreathMinFlatness && level < levels.voice - kBreathBelowVoiceDb)
            return SoundKind::Breath;
        if (level > levels.voice - kVoiceBelowDb &&
            static_cast<double>(voicedFrames) * kHopSeconds >= kMinVoiceSeconds)
            return SoundKind::Voice;
        // A consonant is loud enough to belong to the word beside it; anything quieter is room.
        return level > levels.voice - kConsonantBelowDb ? SoundKind::Consonant : SoundKind::Room;
    }

    /// K-weighted loudness over the voiced frames, or every frame when none is voiced.
    double loudnessOf(std::size_t first, std::size_t last, double floor) const {
        double voicedPower = 0.0;
        double allPower = 0.0;
        std::size_t voicedFrames = 0;
        for (auto i = first; i <= last; ++i) {
            allPower += frames[i].kPower;
            if (voicedAbove(i, floor)) {
                voicedPower += frames[i].kPower;
                ++voicedFrames;
            }
        }
        const double mean = voicedFrames > 0 ? voicedPower / static_cast<double>(voicedFrames)
                                             : allPower / static_cast<double>(last - first + 1);
        return -0.691 + toDb(mean);
    }

    SpeechAnalysisResult segment() const {
        SpeechAnalysisResult result;
        result.durationSeconds = duration();
        // Digital silence counts as -120 dB, so an edited take with no room still has a floor.
        std::vector<double> levels;
        bool heard = false;
        for (const auto& frame : frames) {
            levels.push_back(std::max(frame.levelDb, -120.0));
            heard = heard || frame.levelDb > -120.0;
        }
        if (!heard) {
            if (result.durationSeconds > 0.0)
                result.silences.push_back({0.0, result.durationSeconds});
            return result;
        }
        const auto room = levelsOf(levels);
        result.thresholdDb = room.threshold;

        auto sounds = soundsAbove(room.threshold);
        for (auto& sound : sounds)
            sound.kind = kindOf(sound, room);

        // Voice and consonants closer than the shortest silence join into one phrase, unless a
        // breath parts them; a phrase needs voice in it.
        struct Range {
            std::size_t first;
            std::size_t last;
            bool voice;
        };
        std::vector<Range> phrases;
        bool breathSince = true;
        for (const auto& sound : sounds) {
            if (sound.kind == SoundKind::Room)
                continue;
            if (sound.kind == SoundKind::Breath) {
                breathSince = true;
                continue;
            }
            const bool voice = sound.kind == SoundKind::Voice;
            const bool joins =
                !breathSince && !phrases.empty() &&
                timeOf(sound.first) - timeOf(phrases.back().last + 1) < options.minSilenceSeconds;
            if (joins) {
                phrases.back().last = sound.last;
                phrases.back().voice = phrases.back().voice || voice;
            } else {
                phrases.push_back({sound.first, sound.last, voice});
            }
            breathSince = false;
        }
        std::erase_if(phrases, [](const Range& range) { return !range.voice; });

        struct Region {
            double start;
            double end;
            int phrase;  // index into result.phrases, or -1 for a breath
        };
        std::vector<Region> regions;
        for (const auto& range : phrases) {
            regions.push_back({timeOf(range.first), timeOf(range.last + 1),
                               static_cast<int>(result.phrases.size())});
            result.phrases.push_back({.start = regions.back().start,
                                      .end = regions.back().end,
                                      .loudness = loudnessOf(range.first, range.last, room.floor)});
        }
        for (const auto& sound : sounds)
            if (sound.kind == SoundKind::Breath)
                regions.push_back({timeOf(sound.first), timeOf(sound.last + 1), -1});
        std::ranges::sort(regions, {}, &Region::start);

        for (std::size_t i = 0; i < regions.size(); ++i) {
            auto& region = regions[i];
            if (region.phrase < 0)
                continue;
            const double before = i > 0 ? regions[i - 1].end : 0.0;
            const double after = i + 1 < regions.size() ? regions[i + 1].start : duration();
            region.start = std::max(region.start - options.paddingSeconds, before);
            region.end = std::min(region.end + options.paddingSeconds, after);
            auto& phrase = result.phrases[static_cast<std::size_t>(region.phrase)];
            phrase.start = region.start;
            phrase.end = region.end;
        }

        double cursor = 0.0;
        for (const auto& region : regions) {
            if (region.phrase < 0)
                result.breaths.push_back({region.start, region.end});
            if (region.start > cursor)
                result.silences.push_back({cursor, region.start});
            cursor = std::max(cursor, region.end);
        }
        if (duration() > cursor)
            result.silences.push_back({cursor, duration()});
        return result;
    }

    const double rate;
    const int numChannels;
    const SpeechAnalysisOptions options;
    const int hopSamples;
    const double step;  // input samples per feature-rate sample

    std::vector<std::array<Biquad, 2>> kFilters;
    std::array<Biquad, 2> antiAlias;
    double hopK = 0.0;
    double hopLevel = 0.0;
    int hopCount = 0;
    std::int64_t totalSamples = 0;

    double nextPosition = 0.0;  // the next feature-rate sample, in input samples
    float previous = 0.0f;
    std::int64_t inputIndex = 0;
    std::vector<float> low;  // the feature-rate signal from lowBase on
    std::int64_t lowBase = 0;

    std::vector<Frame> frames;
    std::size_t featuresDone = 0;
    juce::dsp::FFT fft{kFftOrder};
    std::vector<float> hann;
    std::vector<float> windowCorrelation;
    std::vector<float> scratch;
};

SpeechAnalyser::SpeechAnalyser(double sampleRate, int numChannels, SpeechAnalysisOptions options)
    : impl_(std::make_unique<Impl>(sampleRate, numChannels, options)) {}

SpeechAnalyser::~SpeechAnalyser() = default;

void SpeechAnalyser::process(const float* const* channels, int numSamples) {
    impl_->process(channels, numSamples);
}

SpeechAnalysisResult SpeechAnalyser::finish() {
    return impl_->finish();
}

}  // namespace magda::audio
