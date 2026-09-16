#include "TempoEstimator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numeric>

namespace magda::media {

namespace {

// Tempo is read over at most this much of a file. A loop is shorter than this
// anyway, and a song's tempo is not constant enough over ten minutes for the
// extra material to be evidence rather than smear.
constexpr double kMaxAnalysisSeconds = 60.0;

// Local mean window for the onset strength. Long enough to sit above a kick's
// decay, short enough to follow a build.
constexpr double kLocalMeanSeconds = 0.25;

// Harmonics summed into a candidate's comb score, and how much each counts.
constexpr int kCombHarmonics = 4;

// Tempo prior, as a Gaussian in log2(bpm). Octave errors are what a comb over
// an autocorrelation gets wrong, and the prior is what the listener's own bias
// towards a moderate tempo does about it (Ellis 2007).
constexpr double kPriorCentreBpm = 120.0;
constexpr double kPriorWidthOctaves = 0.9;

// A file's length is taken as a whole number of beats when it is within this
// of one. 1.5% is two beats in a 128-beat file.
constexpr double kWholeBarTolerance = 0.015;

// Half a winning period is taken instead of it when it correlates at least this
// well. A uniform pulse correlates equally at both, and the faster reading is
// the one that explains every onset rather than every second one; an accented
// backbeat correlates better at the slower period and keeps it.
constexpr double kOctaveTieRatio = 0.9;

/// The envelope minus its local mean, half-wave rectified, then scaled to unit
/// variance. What survives is where the signal rose faster than it had been.
std::vector<double> onsetStrength(const std::vector<float>& envelope, double hopSeconds) {
    const auto frames = envelope.size();
    std::vector<double> strength(frames, 0.0);
    if (frames == 0 || hopSeconds <= 0.0) {
        return strength;
    }

    const auto half =
        std::max<std::size_t>(1, static_cast<std::size_t>(kLocalMeanSeconds / hopSeconds / 2.0));

    std::vector<double> prefix(frames + 1, 0.0);
    for (std::size_t i = 0; i < frames; ++i) {
        prefix[i + 1] = prefix[i] + envelope[i];
    }

    for (std::size_t i = 0; i < frames; ++i) {
        const auto from = i > half ? i - half : 0;
        const auto to = std::min(frames, i + half + 1);
        const double mean = (prefix[to] - prefix[from]) / static_cast<double>(to - from);
        strength[i] = std::max(0.0, static_cast<double>(envelope[i]) - mean);
    }

    const double mean =
        std::accumulate(strength.begin(), strength.end(), 0.0) / static_cast<double>(frames);
    double variance = 0.0;
    for (double value : strength) {
        variance += (value - mean) * (value - mean);
    }
    variance /= static_cast<double>(frames);
    const double deviation = std::sqrt(variance);
    if (deviation <= 0.0) {
        return std::vector<double>(frames, 0.0);
    }
    for (double& value : strength) {
        value = (value - mean) / deviation;
    }

    // A beat period is rarely a whole number of hops, so a one-frame-wide peak
    // correlates at one lag and not at the one either side of it -- and the
    // rounding accumulates over the harmonics, which is enough to hand a comb
    // to half the tempo. Widening each onset to a few frames, which is what a
    // flux peak looks like on real material anyway, removes the cliff.
    static constexpr std::array<double, 5> kSmoothing = {1.0, 2.0, 3.0, 2.0, 1.0};
    static constexpr double kSmoothingSum = 9.0;
    std::vector<double> smoothed(frames, 0.0);
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (std::size_t tap = 0; tap < kSmoothing.size(); ++tap) {
            const auto offset = static_cast<std::ptrdiff_t>(i + tap) - 2;
            if (offset >= 0 && static_cast<std::size_t>(offset) < frames) {
                sum += strength[static_cast<std::size_t>(offset)] * kSmoothing[tap];
            }
        }
        smoothed[i] = sum / kSmoothingSum;
    }
    return smoothed;
}

/// Autocorrelation at every lag in [minLag, maxLag], normalised so that a lag
/// explaining the envelope as well as zero lag would score 1.
std::vector<double> autocorrelation(const std::vector<double>& strength, std::size_t minLag,
                                    std::size_t maxLag) {
    std::vector<double> correlation(maxLag + 1, 0.0);
    const auto frames = strength.size();

    double energy = 0.0;
    for (double value : strength) {
        energy += value * value;
    }
    if (energy <= 0.0 || frames <= maxLag + 1) {
        return correlation;
    }
    energy /= static_cast<double>(frames);

    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < frames; ++i) {
            sum += strength[i] * strength[i + lag];
        }
        correlation[lag] = (sum / static_cast<double>(frames - lag)) / energy;
    }
    return correlation;
}

double bpmForLag(double lag, double hopSeconds) {
    return 60.0 / (lag * hopSeconds);
}

double tempoPrior(double bpm) {
    const double octaves = std::log2(bpm / kPriorCentreBpm) / kPriorWidthOctaves;
    return std::exp(-0.5 * octaves * octaves);
}

/// The best correlation within a frame either side, which absorbs the rounding
/// of a period that is not a whole number of hops.
double correlationNear(const std::vector<double>& correlation, std::size_t lag) {
    if (lag >= correlation.size()) {
        return 0.0;
    }
    double best = correlation[lag];
    if (lag > 0) {
        best = std::max(best, correlation[lag - 1]);
    }
    if (lag + 1 < correlation.size()) {
        best = std::max(best, correlation[lag + 1]);
    }
    return best;
}

/// A candidate period scores its own correlation plus its harmonics', because
/// a true beat period repeats at every multiple of itself while a half-tempo
/// candidate only has support at the even ones.
///
/// Averaged over the harmonics that fit inside what was correlated, not summed:
/// a slow candidate has fewer of them in range, and summing would score it
/// lower for that alone.
double combScore(const std::vector<double>& correlation, std::size_t lag, double hopSeconds) {
    double score = 0.0;
    double weight = 0.0;
    for (int harmonic = 1; harmonic <= kCombHarmonics; ++harmonic) {
        const auto harmonicLag = lag * static_cast<std::size_t>(harmonic);
        if (harmonicLag + 1 >= correlation.size()) {
            break;
        }
        score += correlationNear(correlation, harmonicLag) / harmonic;
        weight += 1.0 / harmonic;
    }
    if (weight <= 0.0) {
        return 0.0;
    }
    return (score / weight) * tempoPrior(bpmForLag(static_cast<double>(lag), hopSeconds));
}

/// Sub-hop precision: fit a parabola through the peak and its neighbours.
double refineLag(const std::vector<double>& correlation, std::size_t lag) {
    if (lag == 0 || lag + 1 >= correlation.size()) {
        return static_cast<double>(lag);
    }
    const double left = correlation[lag - 1];
    const double centre = correlation[lag];
    const double right = correlation[lag + 1];
    const double denominator = left - 2.0 * centre + right;
    if (std::abs(denominator) < 1e-12) {
        return static_cast<double>(lag);
    }
    return static_cast<double>(lag) + 0.5 * (left - right) / denominator;
}

/// A loop is nearly always a whole number of bars, so a tempo that makes the
/// file an exact beat count is worth more than the one the peak landed on.
/// Prefers a multiple of four beats, which is where the bar lines are.
bool snapToWholeBars(double& bpm, double durationSeconds) {
    if (durationSeconds <= 0.0) {
        return false;
    }
    const double beats = durationSeconds * bpm / 60.0;
    if (beats < 2.0) {
        return false;
    }

    const double nearestBar = std::round(beats / 4.0) * 4.0;
    const double nearestBeat = std::round(beats);
    for (double candidate : {nearestBar, nearestBeat}) {
        if (candidate < 2.0) {
            continue;
        }
        if (std::abs(beats - candidate) / candidate <= kWholeBarTolerance) {
            const double snapped = candidate * 60.0 / durationSeconds;
            if (snapped >= kMinTempoBpm && snapped <= kMaxTempoBpm) {
                bpm = snapped;
                return true;
            }
        }
    }
    return false;
}

// How close a claimed tempo has to be to a measured one to be taken as the same
// tempo. Wider than the rounding in a filename token, narrower than the gap
// between two tempi anyone would write down.
constexpr double kHintTolerance = 0.02;

bool inTempoRange(double bpm) {
    return bpm >= kMinTempoBpm && bpm <= kMaxTempoBpm;
}

/// The relation a hint picks out of half, two thirds, same, three halves and
/// double -- name first, then metadata. A bare 16th grid measures at a
/// dotted-eighth period, two thirds of its tempo, so 3:2 is as real as an
/// octave. Nullopt when no hint lands on one: the audio then keeps what it
/// measured. The measured tempo is always a candidate, in range or not.
std::optional<double> hintedOctave(double measuredBpm, const TempoHints& hints) {
    if (measuredBpm <= 0.0) {
        return std::nullopt;
    }
    for (const auto& hint : {hints.fromName, hints.fromMetadata}) {
        if (!hint || *hint <= 0.0) {
            continue;
        }
        for (double candidate : {measuredBpm * 0.5, measuredBpm * 2.0 / 3.0, measuredBpm,
                                 measuredBpm * 1.5, measuredBpm * 2.0}) {
            if (candidate != measuredBpm && !inTempoRange(candidate)) {
                continue;
            }
            if (std::abs(*hint - candidate) / candidate <= kHintTolerance) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<TempoEstimate> estimateTempo(const std::vector<float>& onsetEnvelope,
                                           double hopSeconds, double durationSeconds) {
    if (hopSeconds <= 0.0 || onsetEnvelope.empty()) {
        return std::nullopt;
    }

    const auto minLag = static_cast<std::size_t>(std::floor(60.0 / (kMaxTempoBpm * hopSeconds)));
    const auto maxLag = static_cast<std::size_t>(std::ceil(60.0 / (kMinTempoBpm * hopSeconds)));
    if (minLag < 1 || maxLag <= minLag) {
        return std::nullopt;
    }

    // Two periods of the slowest tempo searched, or the envelope cannot show a
    // repeat at all and whatever peaks is an accident.
    const auto usable =
        std::min(onsetEnvelope.size(), static_cast<std::size_t>(kMaxAnalysisSeconds / hopSeconds));
    if (usable < maxLag * 2) {
        return std::nullopt;
    }

    const std::vector<float> window(onsetEnvelope.begin(),
                                    onsetEnvelope.begin() + static_cast<std::ptrdiff_t>(usable));
    const auto strength = onsetStrength(window, hopSeconds);

    // Out to the last harmonic a candidate can be scored on, or the comb is
    // inert for every period longer than a quarter of the range and the prior
    // decides on its own.
    const auto correlatedTo =
        std::min(maxLag * static_cast<std::size_t>(kCombHarmonics), usable - 1);
    const auto correlation = autocorrelation(strength, minLag, correlatedTo);

    std::size_t bestLag = 0;
    double bestScore = 0.0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        const double score = combScore(correlation, lag, hopSeconds);
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestLag == 0 || bestScore <= 0.0) {
        return std::nullopt;
    }

    // Octave resolution. A period and half of it score the same comb when the
    // onsets are uniform -- every harmonic of the slower one is a harmonic of
    // the faster -- and the prior then decides on nothing. So walk down while
    // the halved period explains the envelope as well, which is what tells a
    // 174 from the 87 that ignores every second onset.
    while (bestLag / 2 >= minLag && correlationNear(correlation, bestLag / 2) >=
                                        kOctaveTieRatio * correlationNear(correlation, bestLag)) {
        bestLag /= 2;
    }

    // The best candidate that is not the winner's neighbour or its octave. A
    // tempo worth reporting beats the rest of the range, not just its own
    // harmonics -- those it beats by construction.
    double runnerUp = 0.0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        const double ratio = static_cast<double>(lag) / static_cast<double>(bestLag);
        const bool related = std::abs(ratio - 1.0) < 0.15 || std::abs(ratio - 2.0) < 0.15 ||
                             std::abs(ratio - 0.5) < 0.075;
        if (!related) {
            runnerUp = std::max(runnerUp, combScore(correlation, lag, hopSeconds));
        }
    }

    TempoEstimate estimate;
    estimate.bpm = bpmForLag(refineLag(correlation, bestLag), hopSeconds);
    estimate.wholeBars = snapToWholeBars(estimate.bpm, durationSeconds);

    // Two things have to hold for an answer: the envelope repeats at this
    // period, and nothing unrelated explains it nearly as well. Either alone
    // is satisfied by material with no tempo in it.
    const double peak = std::clamp(correlationNear(correlation, bestLag) / 0.35, 0.0, 1.0);
    const double margin =
        bestScore > 0.0 ? std::clamp((bestScore - runnerUp) / bestScore / 0.30, 0.0, 1.0) : 0.0;
    estimate.confidence = peak * margin;

    return estimate;
}

double refineTempo(double measuredBpm, double durationSeconds, const TempoHints& hints) {
    double bpm = hintedOctave(measuredBpm, hints).value_or(measuredBpm);
    snapToWholeBars(bpm, durationSeconds);
    return bpm;
}

bool hintAgrees(const std::optional<TempoEstimate>& estimate, const TempoHints& hints) {
    return estimate.has_value() && hintedOctave(estimate->bpm, hints).has_value();
}

std::optional<double> resolveTempo(const std::optional<TempoEstimate>& estimate,
                                   double durationSeconds, const TempoHints& hints) {
    if (!estimate) {
        return std::nullopt;
    }
    if (hintAgrees(estimate, hints)) {
        return refineTempo(estimate->bpm, durationSeconds, hints);
    }
    if (estimate->confidence >= kMinTempoConfidence) {
        return refineTempo(estimate->bpm, durationSeconds, {});
    }
    return std::nullopt;
}

}  // namespace magda::media
