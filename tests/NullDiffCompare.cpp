#include "NullDiffCompare.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <sstream>

namespace magda::nulldiff {

namespace {

constexpr double kNegativeInfinityDb = -std::numeric_limits<double>::infinity();

double toDb(double amplitude) {
    if (amplitude <= 0.0)
        return kNegativeInfinityDb;
    return 20.0 * std::log10(amplitude);
}

double fromDb(double db) {
    if (db == kNegativeInfinityDb)
        return 0.0;
    return std::pow(10.0, db / 20.0);
}

/// Channel-summed value, so a shift search reads one signal rather than
/// correlating each channel and hoping they agree.
double summed(const juce::AudioBuffer<float>& buffer, int sample) {
    double total = 0.0;
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        total += static_cast<double>(buffer.getSample(channel, sample));
    return total;
}

/// Where the material starts, which is where a priming offset is visible and
/// where a case that drifts has not drifted yet.
int onsetOf(const juce::AudioBuffer<float>& buffer) {
    auto peak = 0.0;
    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
        peak = std::max(peak, std::abs(summed(buffer, sample)));

    if (peak <= 0.0)
        return 0;

    const auto threshold = std::max(1.0e-4, peak * 0.01);
    for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
        if (std::abs(summed(buffer, sample)) >= threshold)
            return sample;

    return 0;
}

std::string sampleAddress(std::int64_t sample, double sampleRate) {
    char text[96];
    std::snprintf(text, sizeof(text), "sample %lld (%.4f s)", static_cast<long long>(sample),
                  sampleRate > 0.0 ? static_cast<double>(sample) / sampleRate : 0.0);
    return text;
}

// --- MIDI helpers ------------------------------------------------------------

constexpr int kNoteOff = 0x80;
constexpr int kNoteOn = 0x90;
constexpr int kControlChange = 0xB0;
constexpr int kPitchBend = 0xE0;

/// A controller stream is keyed by what a synth would keep separate. Pitch bend
/// has no controller number, so it takes one that no controller can.
struct ControllerKey {
    int channel = 1;
    int controller = 0;

    bool operator<(const ControllerKey& other) const {
        return channel != other.channel ? channel < other.channel : controller < other.controller;
    }
};

constexpr int kPitchBendController = -1;

struct ControllerPoint {
    std::int64_t sample = 0;
    int value = 0;
};

using ControllerFunction = std::vector<ControllerPoint>;

std::map<ControllerKey, ControllerFunction> controllerFunctions(const MidiStream& stream,
                                                                std::int64_t shift) {
    std::map<ControllerKey, ControllerFunction> functions;

    for (const auto& event : stream) {
        ControllerKey key;
        key.channel = event.channel();

        int value = 0;
        if (event.type() == kControlChange) {
            key.controller = event.data1;
            value = event.data2;
        } else if (event.type() == kPitchBend) {
            key.controller = kPitchBendController;
            value = (static_cast<int>(event.data2) << 7) | static_cast<int>(event.data1);
        } else {
            continue;
        }

        functions[key].push_back({event.sample - shift, value});
    }

    for (auto& [key, points] : functions)
        std::stable_sort(points.begin(), points.end(),
                         [](const auto& a, const auto& b) { return a.sample < b.sample; });

    return functions;
}

/// The value the function holds at @p sample.
///
/// Before the first point it holds that point's value rather than nothing. A
/// controller nobody has set has no value at all, and the alternative
/// conventions both lie: zero invents a message neither engine sent, and
/// "undefined" would fail every case at instant zero over a difference in when
/// the first message goes out, which cannot be wrong when the value is the one
/// both agree on.
int valueAt(const ControllerFunction& points, std::int64_t sample) {
    if (points.empty())
        return 0;

    auto upper =
        std::upper_bound(points.begin(), points.end(), sample,
                         [](std::int64_t s, const ControllerPoint& p) { return s < p.sample; });
    if (upper == points.begin())
        return points.front().value;

    return std::prev(upper)->value;
}

/// Whether the function takes a value within @p tolerance of @p target at any
/// instant in [from, to]. Every value in the window is a candidate, not just
/// its extremes: a window holding 0 and 127 would otherwise accept anything
/// between them, which is most of the range a controller has.
bool holdsValueNear(const ControllerFunction& points, std::int64_t from, std::int64_t to,
                    int target, int tolerance) {
    if (points.empty())
        return false;

    if (std::abs(valueAt(points, from) - target) <= tolerance)
        return true;

    auto first =
        std::upper_bound(points.begin(), points.end(), from,
                         [](std::int64_t s, const ControllerPoint& p) { return s < p.sample; });
    for (auto point = first; point != points.end() && point->sample <= to; ++point)
        if (std::abs(point->value - target) <= tolerance)
            return true;

    return false;
}

/// Drop messages that repeat the value before them. The fork emits on its grid
/// whether the value moved or not, and a repeat says nothing about the curve:
/// counting it would make a stream that flattens early look longer than one
/// that stopped when it had nothing left to say.
ControllerFunction collapseRepeats(const ControllerFunction& points) {
    ControllerFunction collapsed;
    for (const auto& point : points)
        if (collapsed.empty() || collapsed.back().value != point.value)
            collapsed.push_back(point);
    return collapsed;
}

std::string describeKey(const ControllerKey& key) {
    char text[64];
    if (key.controller == kPitchBendController)
        std::snprintf(text, sizeof(text), "pitch bend ch%d", key.channel);
    else
        std::snprintf(text, sizeof(text), "cc%d ch%d", key.controller, key.channel);
    return text;
}

}  // namespace

// =============================================================================
// Audio
// =============================================================================

ShiftEstimate estimateShift(const juce::AudioBuffer<float>& native,
                            const juce::AudioBuffer<float>& incumbent, int maxShiftSamples) {
    ShiftEstimate estimate;

    if (native.getNumSamples() == 0 || incumbent.getNumSamples() == 0 || maxShiftSamples <= 0)
        return estimate;

    const auto onset = std::min(onsetOf(native), onsetOf(incumbent));

    // Wide enough that a wrong lag cannot correlate as well as the right one by
    // accident, which for periodic material means many periods. Narrowing this
    // is tempting and wrong: an eighth of a second of tone correlates with an
    // eighth of a second of noise at some lag, and the search would then name a
    // fiction rather than decline.
    const auto available = std::min(native.getNumSamples(), incumbent.getNumSamples()) - onset;
    const auto window = std::min(std::max(4 * maxShiftSamples, 4096), std::max(0, available));
    if (window <= 0)
        return estimate;

    // Searching every lag at full rate over a window that wide is tens of
    // millions of multiplies per case, which is the difference between a corpus
    // anybody runs and one nobody does. So the sweep happens on a decimated
    // pair, which costs the square of the factor, and the answer is then
    // refined at full rate around what it found. Decimation keeps the window's
    // whole length, so the strength that makes a false peak unlikely is kept
    // too; what it gives up is resolution, which is exactly what the second
    // pass puts back.
    constexpr int kDecimation = 8;

    // The coarse pass runs on a peak envelope rather than on the waveform, and
    // that is what makes the answer unique. Periodic material correlates just
    // as well a whole period away: a 220 Hz tone repeats every 200 samples, so
    // a waveform sweep cannot tell 1024 from 423 and whichever it picks is
    // arithmetic rather than an answer. An envelope has no period, so it says
    // which of those neighbourhoods the offset is in, and the waveform pass
    // then says where in that neighbourhood it is.
    const auto envelope = [&](const juce::AudioBuffer<float>& buffer, int from, int count) {
        std::vector<double> out(static_cast<std::size_t>(count / kDecimation), 0.0);
        for (std::size_t i = 0; i < out.size(); ++i) {
            auto peak = 0.0;
            for (auto k = 0; k < kDecimation; ++k) {
                const auto index = from + static_cast<int>(i) * kDecimation + k;
                if (index >= 0 && index < buffer.getNumSamples())
                    peak = std::max(peak, std::abs(summed(buffer, index)));
            }
            out[i] = peak;
        }
        return out;
    };

    /// Normalised correlation of the two at @p lag, in whatever domain the
    /// caller decimated to.
    const auto score = [](const std::vector<double>& a, const std::vector<double>& b, int lag,
                          int guard) {
        auto dot = 0.0;
        auto energyA = 0.0;
        auto energyB = 0.0;

        for (auto index = guard; index + guard < static_cast<int>(a.size()); ++index) {
            const auto other = index + lag;
            if (other < 0 || other >= static_cast<int>(b.size()))
                continue;

            const auto x = a[static_cast<std::size_t>(index)];
            const auto y = b[static_cast<std::size_t>(other)];
            dot += x * y;
            energyA += x * x;
            energyB += y * y;
        }

        if (energyA <= 0.0 || energyB <= 0.0)
            return -1.0;
        return dot / std::sqrt(energyA * energyB);
    };

    // The pair, from the onset, with room either side for the lag.
    const auto span = window + 2 * maxShiftSamples;
    const auto from = std::max(0, onset - maxShiftSamples);

    auto coarseA = envelope(native, from, span);
    auto coarseB = envelope(incumbent, from, span);
    const auto coarseGuard = maxShiftSamples / kDecimation;

    // With the mean left in, a level that does not change scores near one at
    // every lag and the peak lands wherever the arithmetic noise puts it. Taken
    // out, a flat envelope carries no energy and therefore no opinion, which is
    // the honest answer for material that is the same all the way through: the
    // waveform pass then works from zero rather than from a fiction.
    const auto centreOn = [](std::vector<double>& values) {
        auto total = 0.0;
        for (const auto value : values)
            total += value;
        const auto mean = values.empty() ? 0.0 : total / static_cast<double>(values.size());
        for (auto& value : values)
            value -= mean;
    };

    centreOn(coarseA);
    centreOn(coarseB);

    // Correlated across the whole envelope rather than inside a guard. The
    // guard is there to keep a lag from running off the end, and score already
    // skips what falls outside; what a guard would exclude here is the onset,
    // which for material that is level all the way through is the only feature
    // the envelope has. Excluding it leaves a flat window with no opinion, and
    // the peak then lands wherever the arithmetic noise puts it.
    auto bestCoarse = -1.0;
    auto bestCoarseLag = 0;
    for (auto lag = -coarseGuard; lag <= coarseGuard; ++lag) {
        const auto value = score(coarseA, coarseB, lag, 0);
        if (value > bestCoarse) {
            bestCoarse = value;
            bestCoarseLag = lag;
        }
    }

    // Full rate, around what the sweep found, wide enough to cover the samples
    // the decimation could not tell apart.
    std::vector<double> fineA(static_cast<std::size_t>(span), 0.0);
    std::vector<double> fineB(static_cast<std::size_t>(span), 0.0);
    for (auto i = 0; i < span; ++i) {
        const auto index = from + i;
        if (index < native.getNumSamples())
            fineA[static_cast<std::size_t>(i)] = summed(native, index);
        if (index < incumbent.getNumSamples())
            fineB[static_cast<std::size_t>(i)] = summed(incumbent, index);
    }

    estimate.envelopeSamples = static_cast<double>(bestCoarseLag * kDecimation);
    estimate.envelopeCorrelation = bestCoarse;

    const auto centre = bestCoarseLag * kDecimation;

    // Wide enough to cover what the envelope could not resolve, which is the
    // decimation either side plus the smoothing the envelope itself is.
    const auto reach = 4 * kDecimation;

    auto best = -1.0;
    auto bestLag = centre;
    std::vector<double> scores(static_cast<std::size_t>(2 * reach + 1), -1.0);

    for (auto offset = -reach; offset <= reach; ++offset) {
        const auto lag = centre + offset;
        const auto value = score(fineA, fineB, lag, maxShiftSamples);
        scores[static_cast<std::size_t>(offset + reach)] = value;

        if (value > best) {
            best = value;
            bestLag = lag;
        }
    }

    estimate.correlation = best;
    estimate.samples = bestLag;
    estimate.fractionalSamples = static_cast<double>(bestLag);

    // A parabola through the peak and its neighbours. A whole-sample answer is
    // not enough for band limited material: at 220 Hz one sample of
    // misalignment is a residual around -30 dB, so a case can be a fraction of
    // a sample out and look like a difference in the material rather than what
    // it is, which is a difference in the timing.
    const auto index = static_cast<std::size_t>(bestLag - centre + reach);
    if (index > 0 && index + 1 < scores.size()) {
        const auto left = scores[index - 1];
        const auto centreScore = scores[index];
        const auto right = scores[index + 1];
        const auto denominator = left - 2.0 * centreScore + right;
        if (left >= 0.0 && right >= 0.0 && std::abs(denominator) > 1.0e-12)
            estimate.fractionalSamples += 0.5 * (left - right) / denominator;
    }

    // A comparator that slides until something matches will always find
    // something. What it finds on a pair that differs in content is a fiction
    // that makes the case pass, so a weak best lag is no lag at all.
    estimate.found = best >= 0.98;
    if (!estimate.found) {
        estimate.samples = 0;
        estimate.fractionalSamples = 0.0;
    }

    return estimate;
}

juce::AudioBuffer<float> delayFractionally(const juce::AudioBuffer<float>& source,
                                           double delaySamples) {
    juce::AudioBuffer<float> result(source.getNumChannels(), source.getNumSamples());
    result.clear();

    const auto whole = static_cast<int>(std::floor(delaySamples));
    const auto fraction = delaySamples - static_cast<double>(whole);

    // Wide enough that the window's own error is far below the floor on
    // anything band limited.
    constexpr int kHalf = 32;

    std::array<double, 2 * kHalf + 1> taps{};
    auto sum = 0.0;
    for (auto k = -kHalf; k <= kHalf; ++k) {
        const auto x = static_cast<double>(k) - fraction;
        const auto sinc = std::abs(x) < 1.0e-9 ? 1.0
                                               : std::sin(juce::MathConstants<double>::pi * x) /
                                                     (juce::MathConstants<double>::pi * x);

        const auto phase = juce::MathConstants<double>::twoPi *
                           (static_cast<double>(k + kHalf) / static_cast<double>(2 * kHalf));
        const auto window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);

        taps[static_cast<std::size_t>(k + kHalf)] = sinc * window;
        sum += sinc * window;
    }

    if (sum != 0.0)
        for (auto& tap : taps)
            tap /= sum;

    for (auto channel = 0; channel < source.getNumChannels(); ++channel) {
        const auto* in = source.getReadPointer(channel);
        auto* out = result.getWritePointer(channel);

        for (auto sample = 0; sample < source.getNumSamples(); ++sample) {
            auto value = 0.0;
            for (auto k = -kHalf; k <= kHalf; ++k) {
                const auto index = sample - whole - k;
                if (index < 0 || index >= source.getNumSamples())
                    continue;
                value += taps[static_cast<std::size_t>(k + kHalf)] * static_cast<double>(in[index]);
            }
            out[sample] = static_cast<float>(value);
        }
    }

    return result;
}

EnvelopeAgreement compareEnvelopes(const juce::AudioBuffer<float>& native,
                                   const juce::AudioBuffer<float>& incumbent, int shiftSamples,
                                   double sampleRate, double followerHz) {
    EnvelopeAgreement result;

    const auto begin = std::max(0, -shiftSamples);
    const auto end = std::min(native.getNumSamples(), incumbent.getNumSamples() - shiftSamples);
    const auto length = end - begin;
    if (length <= 0)
        return result;

    // Rectified and smoothed. What a listener calls the timing of a sound, and
    // it survives the phase difference that makes the waveforms incomparable.
    const auto follow = [&](const juce::AudioBuffer<float>& buffer, int offset) {
        std::vector<double> envelope(static_cast<std::size_t>(length), 0.0);
        const auto coefficient =
            std::exp(-juce::MathConstants<double>::twoPi * followerHz / sampleRate);

        auto state = 0.0;
        for (auto index = 0; index < length; ++index) {
            auto peak = 0.0;
            for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                peak = std::max(
                    peak, std::abs(static_cast<double>(buffer.getSample(channel, offset + index))));

            state = peak + coefficient * (state - peak);
            envelope[static_cast<std::size_t>(index)] = state;
        }
        return envelope;
    };

    const auto a = follow(native, begin);
    const auto b = follow(incumbent, begin + shiftSamples);

    const auto mean = [](const std::vector<double>& values) {
        auto total = 0.0;
        for (const auto value : values)
            total += value;
        return values.empty() ? 0.0 : total / static_cast<double>(values.size());
    };

    const auto meanA = mean(a);
    const auto meanB = mean(b);

    // A lag window of a few milliseconds: this runs after the pinned shift, so
    // what is left should be nothing, and anything past this is not a lag.
    const auto maxLag = std::min(static_cast<int>(sampleRate * 0.01), length / 4);

    // Over a stretch of the envelope rather than all of it. An envelope is a
    // slow signal, so a couple of seconds says everything a whole take would,
    // and correlating every lag against every sample of a long render is how a
    // corpus stops being something anybody runs.
    const auto span = std::min(length - 2 * maxLag, static_cast<int>(sampleRate * 2.0));
    if (span <= 0)
        return result;

    auto best = -2.0;
    auto bestLag = 0;
    std::vector<double> scores(static_cast<std::size_t>(2 * maxLag + 1), 0.0);

    for (auto lag = -maxLag; lag <= maxLag; ++lag) {
        auto dot = 0.0;
        auto energyA = 0.0;
        auto energyB = 0.0;

        for (auto index = maxLag; index < maxLag + span; ++index) {
            const auto x = a[static_cast<std::size_t>(index)] - meanA;
            const auto y = b[static_cast<std::size_t>(index + lag)] - meanB;
            dot += x * y;
            energyA += x * x;
            energyB += y * y;
        }

        const auto value =
            (energyA > 0.0 && energyB > 0.0) ? dot / std::sqrt(energyA * energyB) : 0.0;
        scores[static_cast<std::size_t>(lag + maxLag)] = value;

        if (value > best) {
            best = value;
            bestLag = lag;
        }
    }

    result.correlation = best;
    result.lagSamples = static_cast<double>(bestLag);

    // Refined below a sample, the same way the shift is.
    const auto index = static_cast<std::size_t>(bestLag + maxLag);
    if (index > 0 && index + 1 < scores.size()) {
        const auto left = scores[index - 1];
        const auto centre = scores[index];
        const auto right = scores[index + 1];
        const auto denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-12)
            result.lagSamples += 0.5 * (left - right) / denominator;
    }

    return result;
}

SpectralAgreement compareSpectra(const juce::AudioBuffer<float>& native,
                                 const juce::AudioBuffer<float>& incumbent, int shiftSamples,
                                 double floorDb) {
    SpectralAgreement result;

    const auto begin = std::max(0, -shiftSamples);
    const auto end = std::min(native.getNumSamples(), incumbent.getNumSamples() - shiftSamples);
    if (end - begin < kSpectralWindow)
        return result;

    juce::dsp::FFT fft(static_cast<int>(std::log2(kSpectralWindow)));

    std::vector<float> window(kSpectralWindow);
    for (auto i = 0; i < kSpectralWindow; ++i)
        window[static_cast<std::size_t>(i)] = static_cast<float>(
            0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * i / (kSpectralWindow - 1)));

    std::vector<double> differences;

    const auto spectrumAt = [&](const juce::AudioBuffer<float>& buffer, int offset) {
        std::vector<float> data(static_cast<std::size_t>(2 * kSpectralWindow), 0.0f);
        for (auto i = 0; i < kSpectralWindow; ++i) {
            auto sum = 0.0f;
            for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                sum += buffer.getSample(channel, offset + i);
            data[static_cast<std::size_t>(i)] = sum * window[static_cast<std::size_t>(i)];
        }
        fft.performFrequencyOnlyForwardTransform(data.data());
        data.resize(static_cast<std::size_t>(kSpectralWindow / 2));
        return data;
    };

    for (auto frame = begin; frame + kSpectralWindow <= end; frame += kSpectralHop) {
        const auto a = spectrumAt(native, frame);
        const auto b = spectrumAt(incumbent, frame + shiftSamples);

        auto peak = 0.0f;
        for (std::size_t bin = 0; bin < a.size(); ++bin)
            peak = std::max({peak, a[bin], b[bin]});

        if (peak <= 0.0f)
            continue;

        ++result.frames;

        // Only bins that carry something. Comparing the noise floor of two
        // vocoders is comparing their dither.
        const auto binFloor = static_cast<double>(peak) * fromDb(floorDb);

        for (std::size_t bin = 0; bin < a.size(); ++bin) {
            const auto left = static_cast<double>(a[bin]);
            const auto right = static_cast<double>(b[bin]);
            if (left < binFloor && right < binFloor)
                continue;

            // Both clamped to the floor rather than to something far below it.
            // A bin that is silent in one render and merely quiet in the other
            // is a difference of whatever the clamp allows, and letting that be
            // unbounded means the 95th percentile measures the quietest corner
            // of the spectrum instead of the sound.
            differences.push_back(
                std::abs(toDb(std::max(left, binFloor)) - toDb(std::max(right, binFloor))));
        }
    }

    result.binsCompared = static_cast<int>(differences.size());
    if (differences.empty())
        return result;

    std::sort(differences.begin(), differences.end());
    result.medianDb = differences[differences.size() / 2];
    result.percentile95Db = differences[std::min(
        differences.size() - 1, static_cast<std::size_t>(differences.size() * 95 / 100))];

    return result;
}

AudioComparison compareAudio(const juce::AudioBuffer<float>& native,
                             const juce::AudioBuffer<float>& incumbent,
                             const AudioCompareOptions& options) {
    AudioComparison result;
    result.floorUsed = options.floorDb;
    result.lengthDifference = native.getNumSamples() - incumbent.getNumSamples();

    if (native.getNumChannels() != incumbent.getNumChannels()) {
        result.refusal = "channel counts differ: " + std::to_string(native.getNumChannels()) +
                         " against " + std::to_string(incumbent.getNumChannels());
        return result;
    }

    if (native.getNumSamples() == 0 || incumbent.getNumSamples() == 0) {
        result.refusal = "one render is empty";
        return result;
    }

    // Both buffers whole, and before the alignment is even estimated. A
    // comparator that silently certifies garbage is worse than no comparator,
    // and every comparison against a NaN is false: a NaN residual never beats
    // the running peak, so the peak stays at zero, the level reads as -inf and
    // the pair is reported as a perfect null.
    //
    // Whole rather than over the overlap, because a shift excludes a margin at
    // each end and the envelope and spectral comparisons trim the same margins.
    // Garbage that lands in one of those is read by nothing and would sail
    // through every check a stretched case makes. Whole rather than over the
    // residual as well, so that two infinities cancelling to a finite
    // difference are caught.
    const auto firstNonFinite = [](const juce::AudioBuffer<float>& buffer) {
        for (auto channel = 0; channel < buffer.getNumChannels(); ++channel) {
            const auto* samples = buffer.getReadPointer(channel);
            for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (!std::isfinite(samples[sample]))
                    return static_cast<std::int64_t>(sample);
        }
        return static_cast<std::int64_t>(-1);
    };

    for (const auto& [buffer, whose] :
         {std::pair{&native, "native render"}, std::pair{&incumbent, "incumbent"}}) {
        const auto at = firstNonFinite(*buffer);
        if (at < 0)
            continue;

        result.refusal = "a non-finite sample in the " + std::string(whose) + " at " +
                         sampleAddress(at, options.sampleRate);
        result.peakDb = 0.0;
        result.rmsDb = 0.0;
        result.firstDivergence = at;
        return result;
    }

    if (options.measureShift) {
        const auto estimate = estimateShift(native, incumbent, options.maxShiftSamples);
        result.shiftSamples = estimate.samples;
        result.shiftNotFound = !estimate.found;
    }

    const auto shift = result.shiftSamples;
    const auto begin = static_cast<std::int64_t>(std::max(0, -shift));
    const auto end = std::min(static_cast<std::int64_t>(native.getNumSamples()),
                              static_cast<std::int64_t>(incumbent.getNumSamples()) - shift);

    if (end <= begin) {
        result.refusal = "nothing overlaps once the shift is applied";
        return result;
    }

    const auto floorLinear = fromDb(options.floorDb);
    auto peak = 0.0;
    auto sumOfSquares = 0.0;

    for (auto channel = 0; channel < native.getNumChannels(); ++channel) {
        const auto* a = native.getReadPointer(channel);
        const auto* b = incumbent.getReadPointer(channel);

        for (auto index = begin; index < end; ++index) {
            const auto left = static_cast<double>(a[index]);
            const auto right = static_cast<double>(b[index + shift]);

            const auto residual = left - right;
            const auto magnitude = std::abs(residual);

            if (magnitude > peak)
                peak = magnitude;
            sumOfSquares += residual * residual;

            if (result.firstDivergence < 0 && magnitude > floorLinear)
                result.firstDivergence = index;
        }
    }

    result.comparedSamples = end - begin;
    result.peakDb = toDb(peak);

    const auto count =
        static_cast<double>(result.comparedSamples) * static_cast<double>(native.getNumChannels());
    result.rmsDb = toDb(count > 0.0 ? std::sqrt(sumOfSquares / count) : 0.0);

    return result;
}

// =============================================================================
// MIDI
// =============================================================================

MidiLifetime pairNotes(const MidiStream& stream) {
    MidiLifetime lifetime;

    auto sorted = stream;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    // Keyed by channel and pitch, because that is what a synth keeps separate,
    // and first in first out within a key so that two notes of one pitch pair
    // the way they sounded.
    std::map<int, std::deque<MidiNoteSpan>> pending;

    for (const auto& event : sorted) {
        const auto type = event.type();
        const auto isOff = type == kNoteOff || (type == kNoteOn && event.data2 == 0);
        const auto isOn = type == kNoteOn && event.data2 > 0;

        if (!isOn && !isOff)
            continue;

        const auto key = (event.channel() << 8) | static_cast<int>(event.data1);

        if (isOn) {
            MidiNoteSpan note;
            note.channel = event.channel();
            note.pitch = event.data1;
            note.velocity = event.data2;
            note.onSample = event.sample;
            note.offSample = event.sample;
            pending[key].push_back(note);
            continue;
        }

        auto entry = pending.find(key);
        if (entry == pending.end() || entry->second.empty()) {
            ++lifetime.unmatchedOffs;
            continue;
        }

        auto note = entry->second.front();
        entry->second.pop_front();
        note.offSample = event.sample;
        lifetime.notes.push_back(note);
    }

    for (const auto& [key, notes] : pending)
        lifetime.hanging += static_cast<int>(notes.size());

    std::stable_sort(lifetime.notes.begin(), lifetime.notes.end(),
                     [](const MidiNoteSpan& a, const MidiNoteSpan& b) {
                         if (a.onSample != b.onSample)
                             return a.onSample < b.onSample;
                         if (a.channel != b.channel)
                             return a.channel < b.channel;
                         return a.pitch < b.pitch;
                     });

    return lifetime;
}

MidiComparison compareMidi(const MidiStream& native, const MidiStream& incumbent,
                           const MidiCompareOptions& options) {
    MidiComparison result;

    const auto nativeLifetime = pairNotes(native);
    auto incumbentLifetime = pairNotes(incumbent);

    result.nativeHanging = nativeLifetime.hanging;
    result.incumbentHanging = incumbentLifetime.hanging;

    // Each stream is checked on its own before the two are compared. A note
    // left hanging by the fork is not a reason to accept one here.
    if (nativeLifetime.hanging > 0)
        result.problems.push_back("native left " + std::to_string(nativeLifetime.hanging) +
                                  " note(s) hanging");
    if (nativeLifetime.unmatchedOffs > 0)
        result.problems.push_back("native sent " + std::to_string(nativeLifetime.unmatchedOffs) +
                                  " note-off(s) for notes it never started");
    if (incumbentLifetime.hanging > 0)
        result.problems.push_back("incumbent left " + std::to_string(incumbentLifetime.hanging) +
                                  " note(s) hanging");
    if (incumbentLifetime.unmatchedOffs > 0)
        result.problems.push_back("incumbent sent " +
                                  std::to_string(incumbentLifetime.unmatchedOffs) +
                                  " note-off(s) for notes it never started");

    // The declared shift brings the incumbent into the native domain. Declared,
    // never fitted: the only thing it stands for is the fork dropping
    // midiOffset on an unlooped arranger clip.
    for (auto& note : incumbentLifetime.notes) {
        note.onSample -= options.noteShiftSamples;
        note.offSample -= options.noteShiftSamples;
    }

    const auto tolerance = static_cast<std::int64_t>(options.noteToleranceSamples);

    // Everything the note and controller comparisons do not reach, compared
    // rather than counted. Counting was not enough: equal counts of channel
    // pressure would pass whatever channel, value or instant they carried, and
    // an MPE note opens with one, so this is a stream the corpus really is
    // asserting on rather than a tally it prints.
    const auto others = [](const MidiStream& stream, std::int64_t shift) {
        std::vector<MidiEvent> out;
        for (const auto& event : stream) {
            const auto type = event.type();
            if (type == kNoteOn || type == kNoteOff || type == kControlChange || type == kPitchBend)
                continue;

            auto moved = event;
            moved.sample -= shift;
            out.push_back(moved);
        }

        std::stable_sort(out.begin(), out.end(), [](const MidiEvent& a, const MidiEvent& b) {
            if (a.sample != b.sample)
                return a.sample < b.sample;
            if (a.status != b.status)
                return a.status < b.status;
            return a.data1 < b.data1;
        });
        return out;
    };

    const auto nativeOthers = others(native, 0);
    const auto incumbentOthers = others(incumbent, options.noteShiftSamples);

    result.nativeUncompared = static_cast<int>(nativeOthers.size());
    result.incumbentUncompared = static_cast<int>(incumbentOthers.size());

    auto othersMatch = nativeOthers.size() == incumbentOthers.size();
    if (!othersMatch)
        result.problems.push_back("messages outside notes, controllers and pitch bend: " +
                                  std::to_string(result.nativeUncompared) +
                                  " in the native render against " +
                                  std::to_string(result.incumbentUncompared) + " in the incumbent");

    for (std::size_t i = 0; othersMatch && i < nativeOthers.size(); ++i) {
        const auto& a = nativeOthers[i];
        const auto& b = incumbentOthers[i];

        if (a.status == b.status && a.data1 == b.data1 && a.data2 == b.data2 &&
            std::abs(a.sample - b.sample) <= tolerance)
            continue;

        othersMatch = false;
        result.problems.push_back("a message outside notes and controllers differs at " +
                                  sampleAddress(a.sample, options.sampleRate) + ": status " +
                                  std::to_string(static_cast<int>(a.status)) + " value " +
                                  std::to_string(static_cast<int>(a.data1)) + " against status " +
                                  std::to_string(static_cast<int>(b.status)) + " value " +
                                  std::to_string(static_cast<int>(b.data1)) + " at " +
                                  sampleAddress(b.sample, options.sampleRate));
    }

    result.otherMessagesMatch = othersMatch;

    std::vector<char> matched(incumbentLifetime.notes.size(), 0);
    const auto endEarly = static_cast<std::int64_t>(options.incumbentNoteEndEarlySamples);

    for (const auto& note : nativeLifetime.notes) {
        std::size_t found = incumbentLifetime.notes.size();

        for (std::size_t index = 0; index < incumbentLifetime.notes.size(); ++index) {
            if (matched[index])
                continue;

            const auto& other = incumbentLifetime.notes[index];
            if (other.channel != note.channel || other.pitch != note.pitch)
                continue;
            if (std::abs(other.onSample - note.onSample) > tolerance)
                continue;

            found = index;
            break;
        }

        if (found == incumbentLifetime.notes.size()) {
            ++result.notesOnlyInNative;
            if (result.problems.size() < 24)
                result.problems.push_back("note " + std::to_string(note.pitch) + " ch" +
                                          std::to_string(note.channel) + " at " +
                                          sampleAddress(note.onSample, options.sampleRate) +
                                          " is not in the incumbent");
            continue;
        }

        matched[found] = 1;
        ++result.notesCompared;

        const auto& other = incumbentLifetime.notes[found];
        if (other.velocity != note.velocity ||
            std::abs(other.offSample + endEarly - note.offSample) > tolerance) {
            ++result.notesMismatched;
            if (result.problems.size() < 24)
                result.problems.push_back(
                    "note " + std::to_string(note.pitch) + " ch" + std::to_string(note.channel) +
                    " at " + sampleAddress(note.onSample, options.sampleRate) + ": velocity " +
                    std::to_string(note.velocity) + " against " + std::to_string(other.velocity) +
                    ", ends at " + std::to_string(note.offSample) + " against " +
                    std::to_string(other.offSample));
        }
    }

    for (std::size_t index = 0; index < incumbentLifetime.notes.size(); ++index) {
        if (matched[index])
            continue;

        ++result.notesOnlyInIncumbent;
        const auto& note = incumbentLifetime.notes[index];
        if (result.problems.size() < 24)
            result.problems.push_back("note " + std::to_string(note.pitch) + " ch" +
                                      std::to_string(note.channel) + " at " +
                                      sampleAddress(note.onSample, options.sampleRate) +
                                      " is not in the native render");
    }

    result.otherMessagesMatch = othersMatch;

    result.notesMatch = result.notesOnlyInNative == 0 && result.notesOnlyInIncumbent == 0 &&
                        result.notesMismatched == 0 && nativeLifetime.hanging == 0 &&
                        nativeLifetime.unmatchedOffs == 0 && incumbentLifetime.hanging == 0 &&
                        incumbentLifetime.unmatchedOffs == 0;

    // --- controllers, as functions of time ---------------------------------
    //
    // The engine's stream IS the curve: a message goes out on every change of
    // the quantised value, so its step function is the quantised curve exactly.
    // The fork's is a sampling of that curve on a 1/16-beat grid. So the test
    // is not that the two functions agree instant for instant, which is false
    // by construction and gets worse the faster the curve moves: a pitch-bend
    // dive over a hundred milliseconds gets three grid points there and about a
    // hundred here, and at most instants the fork simply has not sent the value
    // the curve is at. The test is that the fork's sampling is a faithful one.
    //
    // Three things say that, and between them they catch everything a wrong
    // controller stream can be:
    //
    // - every message the fork sends lands on the engine's curve, within one
    //   grid step either way. A wrong value, a curve evaluated with the wrong
    //   tension and a stream arriving late all break this.
    // - the two cover the same span, first change to last change.
    // - they reach the same extremes.
    //
    // Repeats collapse first. The fork emits on its grid whether the value
    // moved or not, so a curve that flattens for two bars keeps producing
    // identical messages there; those carry no information and would otherwise
    // make its span look longer than the engine's.

    const auto nativeFunctions = controllerFunctions(native, 0);
    const auto incumbentFunctions = controllerFunctions(incumbent, options.noteShiftSamples);

    const auto slack = static_cast<std::int64_t>(
        std::llround(options.staleBeats * 60.0 / std::max(1.0, options.bpm) * options.sampleRate));

    result.controllersMatch = true;

    std::vector<ControllerKey> keys;
    for (const auto& [key, points] : nativeFunctions)
        keys.push_back(key);
    for (const auto& [key, points] : incumbentFunctions)
        if (nativeFunctions.find(key) == nativeFunctions.end())
            keys.push_back(key);
    std::sort(keys.begin(), keys.end());

    const auto fail = [&result](const std::string& message) {
        result.controllersMatch = false;
        if (result.problems.size() < 24)
            result.problems.push_back(message);
    };

    for (const auto& key : keys) {
        const auto nativeEntry = nativeFunctions.find(key);
        const auto incumbentEntry = incumbentFunctions.find(key);

        if (nativeEntry == nativeFunctions.end() || incumbentEntry == incumbentFunctions.end()) {
            fail(describeKey(key) + " is only in the " +
                 (nativeEntry == nativeFunctions.end() ? "incumbent" : "native render"));
            continue;
        }

        const auto curve = collapseRepeats(nativeEntry->second);
        const auto sampled = collapseRepeats(incumbentEntry->second);
        if (curve.empty() || sampled.empty()) {
            fail(describeKey(key) + " has no messages on one side");
            continue;
        }

        auto landed = true;
        for (const auto& point : sampled) {
            if (holdsValueNear(curve, point.sample - slack, point.sample + slack, point.value,
                               options.valueTolerance))
                continue;

            fail(describeKey(key) + ": the incumbent sends " + std::to_string(point.value) +
                 " at " + sampleAddress(point.sample, options.sampleRate) +
                 ", which the native curve is not at within a grid step (it is at " +
                 std::to_string(valueAt(curve, point.sample)) + " there)");

            // One message per controller. Twenty thousand instants of the same
            // disagreement is not twenty thousand findings.
            landed = false;
            break;
        }

        if (!landed)
            continue;

        if (std::abs(curve.front().sample - sampled.front().sample) > slack ||
            std::abs(curve.back().sample - sampled.back().sample) > slack)
            fail(describeKey(key) + ": the streams cover different spans, native " +
                 sampleAddress(curve.front().sample, options.sampleRate) + " to " +
                 sampleAddress(curve.back().sample, options.sampleRate) + ", incumbent " +
                 sampleAddress(sampled.front().sample, options.sampleRate) + " to " +
                 sampleAddress(sampled.back().sample, options.sampleRate));

        // What is deliberately NOT asserted: that the two reach the same
        // extremes. A grid sampler misses the peak of anything moving faster
        // than its grid, and missing it is the divergence rather than a symptom
        // of one. A dive of a hundred milliseconds gets four grid points at 120
        // bpm, so the fork's stream turns round at whatever the curve happened
        // to be at 94 ms and never sends the value at the bottom. Requiring
        // equal extremes would fail every fast curve in the corpus for being
        // exactly what it was predicted to be.
        //
        // Nothing is lost by leaving it out. A stream that stops early in time
        // is caught by the span, and a value the curve never takes is caught by
        // the landing test above, which is the only way an extreme can be wrong
        // rather than merely absent.
    }

    return result;
}

// =============================================================================
// The report
// =============================================================================

const char* toString(Verdict verdict) {
    switch (verdict) {
        case Verdict::Null:
            return "null";
        case Verdict::Shift:
            return "shift";
        case Verdict::Midi:
            return "midi";
        case Verdict::ReportOnly:
            return "report";
    }
    return "?";
}

std::string formatDb(double db) {
    if (!std::isfinite(db))
        return "-inf";

    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", db);
    return text;
}

std::string formatReport(const std::vector<CaseReport>& cases, double sampleRate, int blockSize) {
    std::ostringstream out;

    out << "magda-null-diff v1\n";
    out << "cases=" << cases.size() << " rate=" << static_cast<long long>(sampleRate)
        << " blockSize=" << blockSize << "\n";

    auto index = 0;
    for (const auto& report : cases) {
        char line[256];
        std::snprintf(line, sizeof(line), "[%2d] %-28s %-8s ", ++index, report.name.c_str(),
                      toString(report.verdict));
        out << line;

        if (!report.unmeasurable.empty()) {
            out << "unmeasurable: " << report.unmeasurable;
        } else if (report.hasMidi) {
            const auto& midi = report.midi;
            std::snprintf(line, sizeof(line), "notes=%d%s cc=%s", midi.notesCompared,
                          midi.notesMatch ? "" : "!", midi.controllersMatch ? "ok" : "differs");
            out << line;
        } else if (report.hasSpectral) {
            std::snprintf(line, sizeof(line),
                          "shift=%-8.2f envelope=%-6.2f median=%-5.2f p95=%.2f dB",
                          report.measuredShift, report.envelope.lagSamples, report.spectra.medianDb,
                          report.spectra.percentile95Db);
            out << line;
        } else if (report.hasAudio) {
            const auto& audio = report.audio;
            std::snprintf(line, sizeof(line), "peak=%-9s rms=%-9s shift=%.3f",
                          formatDb(audio.peakDb).c_str(), formatDb(audio.rmsDb).c_str(),
                          report.hasMeasuredShift ? report.measuredShift
                                                  : static_cast<double>(audio.shiftSamples));
            out << line;
            if (!audio.refusal.empty())
                out << " refused: " << audio.refusal;
        }

        out << (report.passed ? "  ok" : "  FAIL") << "\n";
    }

    return out.str();
}

}  // namespace magda::nulldiff
