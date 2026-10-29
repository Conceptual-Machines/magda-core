// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScopeAnalysis.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kTriggerFraction =
    0.1f;                             // of the search's peak; lower finds more triggers in a mix
constexpr float kFitFraction = 0.3f;  // the line fits the rise out to here, averaging more noise
constexpr float kSilence = 1.0e-5f;

float sampleAt(const float* samples, int numSamples, long long i) {
    return samples[std::clamp<long long>(i, 0, numSamples - 1)];
}

float linearAt(const float* samples, int numSamples, double t) {
    t = std::clamp(t, 0.0, static_cast<double>(numSamples - 1));
    const auto i = static_cast<long long>(t);
    const float a = samples[i];
    const float b = sampleAt(samples, numSamples, i + 1);
    return a + (b - a) * static_cast<float>(t - static_cast<double>(i));
}

/// Where the line fitted through samples [first, last] crosses zero.
double fittedZero(const float* samples, int first, int last) {
    if (last <= first)
        return first;
    const int n = last - first + 1;
    const double meanT = 0.5 * (first + last);
    double meanY = 0.0;
    for (int k = first; k <= last; ++k)
        meanY += samples[k];
    meanY /= n;

    double sty = 0.0;
    double stt = 0.0;
    for (int k = first; k <= last; ++k) {
        const double dt = k - meanT;
        sty += dt * (samples[k] - meanY);
        stt += dt * dt;
    }
    const double slope = sty / stt;
    if (slope <= 0.0)
        return meanT;
    return std::clamp(meanT - meanY / slope, static_cast<double>(first), static_cast<double>(last));
}

}  // namespace

ScopeSpan scopeSpan(double sampleRate, double timebaseMs, int maxWindow) {
    const int minSearch = static_cast<int>(std::ceil(sampleRate / kScopeLowestTriggerHz)) + 1;
    const int maxDisplay = maxWindow - minSearch - kScopeSincHalfWidth;

    ScopeSpan span;
    span.displaySamples =
        std::clamp(static_cast<int>(timebaseMs * 0.001 * sampleRate), 64, maxDisplay);
    span.searchSamples = minSearch;
    span.readCount = span.searchSamples + span.displaySamples + kScopeSincHalfWidth;
    return span;
}

double findRisingTrigger(const float* samples, int numSamples, int searchEnd) {
    float peak = 0.0f;
    for (int i = 0; i <= searchEnd && i < numSamples; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    if (peak < kSilence)
        return -1.0;

    // Nearest the middle: a tone locks on any of its cycles, while the first or
    // latest trigger holds a drum or a mix still and then jumps.
    const float h = kTriggerFraction * peak;
    const double target = 0.5 * searchEnd;
    double trigger = -1.0;
    int lastBelow = -1;
    for (int i = 0; i < numSamples; ++i) {
        if (samples[i] < -h) {
            if (i > searchEnd)
                break;
            lastBelow = i;
        } else if (lastBelow >= 0 && samples[i] >= h) {
            // Widen the fit along the rise, at most four times the armed span each side.
            const float fitH = kFitFraction * peak;
            const int reach = 4 * (i - lastBelow);
            int first = lastBelow;
            while (first > 0 && lastBelow - first < reach && samples[first - 1] >= -fitH)
                --first;
            int last = i;
            while (last + 1 < numSamples && last - i < reach && samples[last + 1] <= fitH)
                ++last;
            const double zero = fittedZero(samples, first, last);
            if (zero > searchEnd)
                break;
            if (trigger < 0.0 || std::abs(zero - target) < std::abs(trigger - target))
                trigger = zero;
            lastBelow = -1;
        }
    }
    return trigger;
}

float sincSample(const float* samples, int numSamples, double t) {
    const double base = std::floor(t);
    const double frac = t - base;
    const auto i0 = static_cast<long long>(base);
    if (frac < 1.0e-9)
        return sampleAt(samples, numSamples, i0);

    double sum = 0.0;
    double weights = 0.0;
    for (long long k = i0 - kScopeSincHalfWidth + 1; k <= i0 + kScopeSincHalfWidth; ++k) {
        const double x = t - static_cast<double>(k);
        const double w = x / kScopeSincHalfWidth;
        const double blackman = 0.42 + 0.5 * std::cos(kPi * w) + 0.08 * std::cos(2.0 * kPi * w);
        const double weight = blackman * std::sin(kPi * x) / (kPi * x);
        sum += weight * sampleAt(samples, numSamples, k);
        weights += weight;
    }
    return static_cast<float>(sum / weights);
}

void envelopeColumns(const float* samples, int numSamples, double start, double samplesPerColumn,
                     int cols, std::vector<float>& mins, std::vector<float>& maxs) {
    mins.assign(static_cast<size_t>(cols), 0.0f);
    maxs.assign(static_cast<size_t>(cols), 0.0f);
    if (numSamples < 1)
        return;

    for (int c = 0; c < cols; ++c) {
        const double a = start + c * samplesPerColumn;
        const double b = a + samplesPerColumn;
        float lo = linearAt(samples, numSamples, a);
        float hi = lo;
        const float atB = linearAt(samples, numSamples, b);
        lo = std::min(lo, atB);
        hi = std::max(hi, atB);
        const auto last = std::min(static_cast<double>(numSamples), b);
        for (auto k = static_cast<long long>(std::floor(a)) + 1; static_cast<double>(k) < last;
             ++k) {
            if (k < 0)
                continue;
            lo = std::min(lo, samples[k]);
            hi = std::max(hi, samples[k]);
        }
        mins[static_cast<size_t>(c)] = lo;
        maxs[static_cast<size_t>(c)] = hi;
    }
}

}  // namespace magda::daw::audio
