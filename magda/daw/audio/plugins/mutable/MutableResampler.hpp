// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file MutableResampler.hpp
 * @brief The rate crossing the three Mutable ports share (#2409).
 *
 * Each port runs its DSP at the fixed rate its tuning is written in (Clouds and
 * Elements 32 kHz, Rings 48 kHz) and crosses to the host rate here: a polyphase
 * windowed-sinc resampler (#3328), with the 8th-order Butterworth of #2409 kept on
 * whichever side of the crossing runs faster. The sinc alone measured worse than the
 * cubic and Butterworth it replaced on two of the image and folding cases.
 */

#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace magda::daw::audio::mutable_resampling {

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
 * @brief A Kaiser-windowed sinc lowpass for one rate crossing, tabulated by phase.
 *
 * The cutoff sits at kCutoff of the lower rate's Nyquist, so the one kernel removes an
 * interpolation's images and what a decimation would fold.
 */
class SincKernel {
  public:
    static constexpr int kZeroCrossings = 16;
    static constexpr double kBeta = 9.0;
    static constexpr double kCutoff = 0.9;
    static constexpr int kPhases = 256;

    void prepare(double inRate, double outRate) {
        const double scale = kCutoff * std::min(1.0, outRate / inRate);
        lead_ = static_cast<int>(std::ceil(kZeroCrossings / scale));
        taps_ = 2 * lead_;
        table_.assign(static_cast<size_t>((kPhases + 1) * taps_), 0.0f);

        for (int p = 0; p <= kPhases; ++p) {
            const double frac = static_cast<double>(p) / kPhases;
            float* row = &table_[static_cast<size_t>(p * taps_)];
            double sum = 0.0;
            for (int k = 0; k < taps_; ++k) {
                const double c = scale * windowedSinc((lead_ - 1 + frac - k) * scale);
                row[k] = static_cast<float>(c);
                sum += c;
            }
            // Unity at DC for every phase, or the phase steps would modulate a held level.
            for (int k = 0; k < taps_; ++k)
                row[k] = static_cast<float>(row[k] / sum);
        }
    }

    /// Input samples the window holds.
    int taps() const {
        return taps_;
    }

    /// Input samples the window reaches past the output instant.
    int lead() const {
        return lead_;
    }

    /**
     * @brief The output at @p frac past window index lead() - 1.
     * @param l,r taps() input samples, oldest first.
     */
    void apply(const float* l, const float* r, double frac, float& outL, float& outR) const {
        const double position = frac * kPhases;
        const int p = std::min(static_cast<int>(position), kPhases - 1);
        const auto t = static_cast<float>(position - p);
        const float* c0 = &table_[static_cast<size_t>(p * taps_)];
        const float* c1 = c0 + taps_;

        float l0 = 0.0f, l1 = 0.0f, r0 = 0.0f, r1 = 0.0f;
        for (int k = 0; k < taps_; ++k) {
            l0 += c0[k] * l[k];
            l1 += c1[k] * l[k];
            r0 += c0[k] * r[k];
            r1 += c1[k] * r[k];
        }
        outL = l0 + t * (l1 - l0);
        outR = r0 + t * (r1 - r0);
    }

  private:
    static double besselI0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; term > 1.0e-12 * sum; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    /// sinc(x) under a Kaiser window reaching kZeroCrossings either side.
    static double windowedSinc(double x) {
        const double r = x / kZeroCrossings;
        if (std::abs(r) >= 1.0)
            return 0.0;
        const double window = besselI0(kBeta * std::sqrt(1.0 - r * r)) / besselI0(kBeta);
        if (x == 0.0)
            return window;
        const double px = juce::MathConstants<double>::pi * x;
        return window * std::sin(px) / px;
    }

    std::vector<float> table_;
    int taps_ = 2;
    int lead_ = 1;
};

/**
 * @brief Reads samples at one rate out at another through a SincKernel.
 *
 * Push input until needed() is zero, then read() one output; the history is mirrored so the
 * kernel always sees one contiguous window.
 */
class PolyphaseResampler {
  public:
    /// Ready at 1:1, so a device that processes before it is prepared reads silence, not past
    /// an empty buffer.
    PolyphaseResampler() {
        prepare(1.0, 1.0);
    }

    void prepare(double inRate, double outRate) {
        kernel_.prepare(inRate, outRate);
        step_ = inRate / outRate;
        const auto size = static_cast<size_t>(2 * kernel_.taps());
        l_.assign(size, 0.0f);
        r_.assign(size, 0.0f);
        reset();
    }

    /// Silence behind the window, and the output instant on the first sample pushed.
    void reset() {
        std::fill(l_.begin(), l_.end(), 0.0f);
        std::fill(r_.begin(), r_.end(), 0.0f);
        write_ = 0;
        frac_ = 0.0;
        needed_ = kernel_.lead() + 1;
    }

    int needed() const {
        return needed_;
    }

    int lead() const {
        return kernel_.lead();
    }

    void push(float l, float r) {
        const int taps = kernel_.taps();
        l_[static_cast<size_t>(write_)] = l_[static_cast<size_t>(write_ + taps)] = l;
        r_[static_cast<size_t>(write_)] = r_[static_cast<size_t>(write_ + taps)] = r;
        write_ = write_ + 1 == taps ? 0 : write_ + 1;
        --needed_;
    }

    void read(float& l, float& r) {
        kernel_.apply(&l_[static_cast<size_t>(write_)], &r_[static_cast<size_t>(write_)], frac_, l,
                      r);
        frac_ += step_;
        const double whole = std::floor(frac_);
        frac_ -= whole;
        needed_ += static_cast<int>(whole);
    }

  private:
    SincKernel kernel_;
    std::vector<float> l_, r_;
    double step_ = 1.0;
    double frac_ = 0.0;
    int write_ = 0;
    int needed_ = 1;
};

/** @brief A PolyphaseResampler fed from a StereoRing, one output per pull. */
struct RingResampler {
    void prepare(double inRate, double outRate) {
        resampler_.prepare(inRate, outRate);
    }

    void reset() {
        resampler_.reset();
    }

    /// Input samples the first output waits for.
    int priming() const {
        return resampler_.lead() + 1;
    }

    /// False, consuming what there was, when the ring runs out before the window fills.
    bool pull(StereoRing& ring, float& oL, float& oR) {
        while (resampler_.needed() > 0) {
            if (ring.count == 0)
                return false;
            resampler_.push(ring.L(0), ring.R(0));
            ring.pop(1);
        }
        resampler_.read(oL, oR);
        return true;
    }

  private:
    PolyphaseResampler resampler_;
};

/**
 * @brief An instrument's internal-to-host leg, pulling DSP samples on demand.
 *
 * On demand rather than through a ring, so no callback partition can starve it. The window
 * reaches lead() DSP samples past the sample being output, so the DSP runs that far ahead and
 * a note lands that much late; that is the leg's latency. At equal rates it passes through.
 */
class OutputLeg {
  public:
    void prepare(double internalRate, double hostRate) {
        bypass_ = internalRate == hostRate;
        resampler_.prepare(internalRate, hostRate);
        filterInternal_ = hostRate < internalRate;
        const double filterRate = filterInternal_ ? internalRate : hostRate;
        limit_.prepare(bandLimitCorner(internalRate, hostRate), filterRate);
        // A note lands on average half a DSP sample past the window's lead, less the host sample
        // it arrives on.
        latencySeconds_ = bypass_ ? 0.0
                                  : (resampler_.lead() + 0.5) / internalRate - 1.0 / hostRate +
                                        limit_.delaySamples() / filterRate;
        reset();
    }

    void reset() {
        resampler_.reset();
        limit_.clear();
    }

    double latencySeconds() const {
        return latencySeconds_;
    }

    /** @brief Renders @p n host samples, calling @p pull(l, r) for each DSP sample needed. */
    template <typename Pull> void render(float* outL, float* outR, int n, Pull&& pull) {
        for (int i = 0; i < n; ++i) {
            float L = 0.0f, R = 0.0f;
            if (bypass_) {
                pull(L, R);
            } else {
                while (resampler_.needed() > 0) {
                    float l = 0.0f, r = 0.0f;
                    pull(l, r);
                    if (filterInternal_)
                        limit_.process(l, r);
                    resampler_.push(l, r);
                }
                resampler_.read(L, R);
                if (!filterInternal_)
                    limit_.process(L, R);
            }
            outL[i] = L;
            if (outR != nullptr)
                outR[i] = R;
        }
    }

  private:
    PolyphaseResampler resampler_;
    BandLimit limit_;
    double latencySeconds_ = 0.0;
    bool bypass_ = false;
    bool filterInternal_ = false;
};

}  // namespace magda::daw::audio::mutable_resampling
