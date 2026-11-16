// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file MutableResampler.hpp
 * @brief The rate crossing the three Mutable ports share (#2409).
 *
 * Each port runs its DSP at the fixed rate its tuning is written in (Clouds and
 * Elements 32 kHz, Rings 48 kHz) and crosses to the host rate here: a cubic
 * interpolator, band-limited by an 8th-order Butterworth on whichever side of
 * the crossing runs faster.
 */

#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace magda::daw::audio::mutable_resampling {

/** @brief 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between x1 and x2. */
inline float cubic(float x0, float x1, float x2, float x3, float t) {
    const float a = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;
    const float b = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c = -0.5f * x0 + 0.5f * x2;
    return ((a * t + b) * t + c) * t + x1;
}

/** @brief One biquad section, transposed direct form II. */
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void clear() {
        z1 = z2 = 0.0f;
    }

    void setLowpass(double fc, double fs, double q) {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<float>((1.0 - cw) * 0.5 / a0);
        b1 = static_cast<float>((1.0 - cw) / a0);
        b2 = b0;
        a1 = static_cast<float>(-2.0 * cw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    float process(float x) {
        const float y = (b0 * x) + z1;
        z1 = (b1 * x) - (a1 * y) + z2;
        z2 = (b2 * x) - (a2 * y);
        return y;
    }

    /** @brief Group delay at DC, in samples. */
    double delaySamples() const {
        const double num = static_cast<double>(b1) + 2.0 * static_cast<double>(b2);
        const double numSum =
            static_cast<double>(b0) + static_cast<double>(b1) + static_cast<double>(b2);
        const double den = static_cast<double>(a1) + 2.0 * static_cast<double>(a2);
        const double denSum = 1.0 + static_cast<double>(a1) + static_cast<double>(a2);
        return num / numSum - den / denSum;
    }
};

/// Pole Qs of an 8th-order Butterworth, 1/(2 cos(pi (2k+1)/16)).
inline constexpr double kButterworthQ[4] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};

/**
 * @brief The band limit either side of a crossing between @p internalRate and @p hostRate.
 *
 * Under the lower of the two Nyquists: 15 kHz for a 32 kHz DSP, and clear of the host's own
 * Nyquist at low rates. A 15 kHz corner at 22.05 kHz is an unstable biquad: OfflineMixAnalysis
 * renders there, and it took Clouds' output to 1e37 and then to NaN (#2365 review).
 */
inline double bandLimitCorner(double internalRate, double hostRate) {
    return std::min(0.46875 * internalRate, 0.45 * hostRate);
}

/** @brief A stereo 8th-order Butterworth lowpass. */
struct BandLimit {
    Biquad l[4], r[4];

    void prepare(double corner, double filterRate) {
        for (int i = 0; i < 4; ++i) {
            l[i].setLowpass(corner, filterRate, kButterworthQ[i]);
            r[i].setLowpass(corner, filterRate, kButterworthQ[i]);
        }
        clear();
    }

    void clear() {
        for (int i = 0; i < 4; ++i) {
            l[i].clear();
            r[i].clear();
        }
    }

    void process(float& L, float& R) {
        for (int i = 0; i < 4; ++i) {
            L = l[i].process(L);
            R = r[i].process(R);
        }
    }

    /** @brief Delay at low frequencies, in samples at the rate it was prepared at. */
    double delaySamples() const {
        double total = 0.0;
        for (const auto& section : l)
            total += section.delaySamples();
        return total;
    }
};

/** @brief Stereo ring buffer, allocation-free on the audio thread. */
struct StereoRing {
    static constexpr int kCap = 4096;
    static constexpr int kMask = kCap - 1;
    float l[kCap];
    float r[kCap];
    int head = 0;
    int count = 0;

    void clear() {
        head = 0;
        count = 0;
    }

    void push(float L, float R) {
        const int i = (head + count) & kMask;
        l[i] = L;
        r[i] = R;
        if (count < kCap)
            ++count;
        else
            head = (head + 1) & kMask;  // overrun: drop oldest
    }

    float L(int i) const {
        return l[(head + i) & kMask];
    }

    float R(int i) const {
        return r[(head + i) & kMask];
    }

    void pop(int n) {
        head = (head + n) & kMask;
        count -= n;
    }
};

/**
 * @brief Cubic pull resampler reading a StereoRing at rate Fin, one sample at Fout per pull.
 *
 * step = Fin/Fout. Phase stays in [1,2), so the 4-point window is always ring indices 0..3.
 */
struct RingResampler {
    double pos = 1.0;
    double step = 1.0;

    void reset() {
        pos = 1.0;
    }

    bool pull(StereoRing& ring, float& oL, float& oR) {
        // The 4-point window plus whatever this call pops: step can exceed 1,
        // and popping past count leaves it negative, which puts the next push
        // behind head (#2365 review).
        if (ring.count < 3 + static_cast<int>(std::ceil(step)))
            return false;
        const auto t = static_cast<float>(pos - 1.0);
        oL = cubic(ring.L(0), ring.L(1), ring.L(2), ring.L(3), t);
        oR = cubic(ring.R(0), ring.R(1), ring.R(2), ring.R(3), t);
        pos += step;
        while (pos >= 2.0) {
            ring.pop(1);
            pos -= 1.0;
        }
        return true;
    }
};

/**
 * @brief An instrument's internal-to-host leg, pulling DSP samples on demand.
 *
 * On demand rather than through a ring, so no callback partition can starve it. Rising to the
 * host rate, the band limit follows the interpolator to catch its images; falling, it runs at
 * the internal rate first, since what lies past the host's Nyquist has to go before it folds.
 */
class OutputLeg {
  public:
    void prepare(double internalRate, double hostRate) {
        ratio_ = internalRate / hostRate;
        filterInternal_ = hostRate < internalRate;
        const double filterRate = filterInternal_ ? internalRate : hostRate;
        limit_.prepare(bandLimitCorner(internalRate, hostRate), filterRate);
        latencySeconds_ = limit_.delaySamples() / filterRate;
        reset();
    }

    void reset() {
        primed_ = false;
        frac_ = 0.0;
        std::fill(std::begin(xL_), std::end(xL_), 0.0f);
        std::fill(std::begin(xR_), std::end(xR_), 0.0f);
        limit_.clear();
    }

    /** @brief The filter's delay; the interpolator adds none, since priming places it. */
    double latencySeconds() const {
        return latencySeconds_;
    }

    /** @brief Renders @p n host samples, calling @p pull(l, r) for each DSP sample needed. */
    template <typename Pull> void render(float* outL, float* outR, int n, Pull&& pull) {
        if (!primed_) {
            // A sample of the silence before the DSP started, so host sample 0
            // is the DSP's sample 0.
            xL_[0] = xR_[0] = 0.0f;
            for (int j = 1; j < 4; ++j)
                next(pull, xL_[j], xR_[j]);
            primed_ = true;
        }
        for (int i = 0; i < n; ++i) {
            const auto t = static_cast<float>(frac_);
            float L = cubic(xL_[0], xL_[1], xL_[2], xL_[3], t);
            float R = cubic(xR_[0], xR_[1], xR_[2], xR_[3], t);
            if (!filterInternal_)
                limit_.process(L, R);
            outL[i] = L;
            if (outR != nullptr)
                outR[i] = R;

            frac_ += ratio_;
            while (frac_ >= 1.0) {
                frac_ -= 1.0;
                xL_[0] = xL_[1];
                xL_[1] = xL_[2];
                xL_[2] = xL_[3];
                xR_[0] = xR_[1];
                xR_[1] = xR_[2];
                xR_[2] = xR_[3];
                next(pull, xL_[3], xR_[3]);
            }
        }
    }

  private:
    template <typename Pull> void next(Pull& pull, float& l, float& r) {
        pull(l, r);
        if (filterInternal_)
            limit_.process(l, r);
    }

    BandLimit limit_;
    double ratio_ = 1.0;
    double frac_ = 0.0;
    double latencySeconds_ = 0.0;
    float xL_[4]{};
    float xR_[4]{};
    bool filterInternal_ = false;
    bool primed_ = false;
};

}  // namespace magda::daw::audio::mutable_resampling
