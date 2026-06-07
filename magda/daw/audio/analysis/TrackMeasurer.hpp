#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "AudioTapBuffer.hpp"

namespace magda::daw::audio {

/**
 * @brief Real-time loudness / level / stereo measurement for one signal point
 *        (a track output or the master), per issue #1388.
 *
 * Implements ITU-R BS.1770-4 K-weighted loudness (momentary / short-term /
 * gated-integrated LUFS), sample + optional oversampled true-peak, stereo
 * correlation and width, and the derived dynamics figures (PLR / PSR). It is
 * the pure DSP core: no Tracktion Engine, no plugin lifecycle, no enablement
 * policy - the always-on TrackMeasurementPlugin owns those and simply feeds
 * blocks here while a consumer (Levels meter or mixing agent) is listening.
 *
 * Threading: process() is audio-thread, allocation-free after prepare().
 * read() is message-thread and lock-free. Published scalars are plain atomics;
 * the integrated-loudness histogram is read with benign races (monotonic
 * counts, a torn read only yields a slightly stale integrated value - the same
 * "tearing acceptable for metering" stance as AudioTapBuffer).
 */

/// LUFS value reported when a window holds no above-floor signal.
inline constexpr float kSilenceLufs = -100.0f;
/// dB value reported for silence on the peak fields.
inline constexpr float kSilenceDb = -200.0f;

struct TrackMeasurementSnapshot {
    float momentaryLufs = kSilenceLufs;   ///< 400 ms K-weighted window
    float shortTermLufs = kSilenceLufs;   ///< 3 s K-weighted window
    float integratedLufs = kSilenceLufs;  ///< gated, since last reset()

    float samplePeakDb = kSilenceDb;  ///< max |sample|, dBFS (always computed)
    float truePeakDb = kSilenceDb;    ///< oversampled dBTP (only if true-peak enabled)

    float correlation = 1.0f;  ///< L/R correlation [-1, 1] (1 for mono)
    float width = 0.0f;        ///< side/(mid+side) energy ratio [0,1]: 0 mono, 1 fully out-of-phase

    float plr = 0.0f;  ///< peak-to-loudness ratio: peak - integrated (LU)
    float psr = 0.0f;  ///< peak-to-short-term ratio: peak - short-term (LU)

    bool truePeakValid = false;  ///< false when oversampled true-peak is disabled
    bool valid = false;          ///< true once any signal has been processed
};

class TrackMeasurer {
  public:
    /**
     * @param sampleRate    feed rate
     * @param maxBlockSize  largest block process() will see
     * @param enableTruePeak  run the 4x oversampler (master-only by policy; per
     *                        issue #1388 per-track uses sample peak to keep the
     *                        N-tracks cost down - true-peak is the heavy part)
     */
    void prepare(double sampleRate, int maxBlockSize, bool enableTruePeak) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        enableTruePeak_ = enableTruePeak;
        computeKWeightingCoeffs(sampleRate_);
        if (enableTruePeak_)
            buildOversampler();
        blockSamples_ = juce::jmax(1, static_cast<int>(std::lround(sampleRate_ * 0.1)));  // 100 ms
        scratchL_.assign(static_cast<size_t>(juce::jmax(1, maxBlockSize)), 0.0f);
        scratchR_.assign(static_cast<size_t>(juce::jmax(1, maxBlockSize)), 0.0f);
        reset();
    }

    /// Clears integrated gating history and all running windows. Audio or message
    /// thread, but not concurrently with process().
    void reset() {
        for (auto& f : filtL_)
            f = {};
        for (auto& f : filtR_)
            f = {};
        blockEnergy_.fill(0.0);
        blockCount_ = 0;
        curBlockAcc_ = 0.0;
        curBlockN_ = 0;
        for (auto& h : histogram_)
            h.store(0, std::memory_order_relaxed);
        corrSmoothed_ = 1.0f;
        widthSmoothed_ = 0.0f;
        for (auto& s : tpDelayL_)
            s = 0.0f;
        for (auto& s : tpDelayR_)
            s = 0.0f;
        momentary_.store(kSilenceLufs, std::memory_order_relaxed);
        shortTerm_.store(kSilenceLufs, std::memory_order_relaxed);
        samplePeak_.store(kSilenceDb, std::memory_order_relaxed);
        truePeak_.store(kSilenceDb, std::memory_order_relaxed);
        correlation_.store(1.0f, std::memory_order_relaxed);
        width_.store(0.0f, std::memory_order_relaxed);
        valid_.store(false, std::memory_order_relaxed);
    }

    /// Audio thread. Process one (interleaved-by-pointer) block. Mono is handled
    /// by treating R == L. Allocation-free.
    void process(const float* const* channels, int numChannels, int numSamples) noexcept {
        if (numChannels <= 0 || numSamples <= 0)
            return;
        const float* l = channels[0];
        const float* r = numChannels > 1 ? channels[1] : channels[0];
        if (numSamples > static_cast<int>(scratchL_.size()))
            return;  // block exceeds prepared size; skip (signal still passes through upstream)

        double sumMidSq = 0.0, sumSideSq = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        float samplePeak = 0.0f;
        const bool capture = captureSpectrum_.load(std::memory_order_acquire);

        for (int i = 0; i < numSamples; ++i) {
            const float xl = l[i];
            const float xr = r[i];

            // Sample peak (cheap, always on).
            samplePeak = juce::jmax(samplePeak, std::abs(xl), std::abs(xr));

            // K-weighted energy for loudness.
            const double kl = applyKWeight(filtL_, xl);
            const double kr = applyKWeight(filtR_, xr);
            curBlockAcc_ += kl * kl + kr * kr;  // stereo channel weights = 1.0
            if (++curBlockN_ >= blockSamples_)
                closeGatingBlock();

            // Mid/side for correlation + width.
            const double mid = 0.5 * (xl + xr);
            const double side = 0.5 * (xl - xr);
            sumMidSq += mid * mid;
            sumSideSq += side * side;
            sumLR += static_cast<double>(xl) * xr;
            sumLL += static_cast<double>(xl) * xl;
            sumRR += static_cast<double>(xr) * xr;

            if (capture)
                scratchL_[static_cast<size_t>(i)] = static_cast<float>(mid);  // mono for band FFT
        }
        if (capture)
            spectrumRing_.write(scratchL_.data(), numSamples);

        // Sample peak -> dBFS.
        if (samplePeak > 0.0f)
            publishMax(samplePeak_, linearToDb(samplePeak));

        // True peak (oversampled) only when enabled.
        if (enableTruePeak_) {
            const float tp = juce::jmax(oversamplePeak(tpDelayL_, l, numSamples),
                                        oversamplePeak(tpDelayR_, r, numSamples));
            if (tp > 0.0f)
                publishMax(truePeak_, linearToDb(tp));
        }

        // Correlation: normalised cross-correlation over the block, one-pole smoothed.
        const double denom = std::sqrt(sumLL * sumRR);
        const float instCorr =
            denom > 1.0e-12 ? static_cast<float>(juce::jlimit(-1.0, 1.0, sumLR / denom)) : 1.0f;
        corrSmoothed_ += kCorrSmooth * (instCorr - corrSmoothed_);
        correlation_.store(corrSmoothed_, std::memory_order_relaxed);

        // Width: side energy as a fraction of total mid+side, one-pole smoothed.
        // Bounded [0,1] and well-defined at the anti-phase limit (mid -> 0 -> width 1).
        const double msTotal = sumMidSq + sumSideSq;
        const float instWidth = msTotal > 1.0e-12 ? static_cast<float>(sumSideSq / msTotal) : 0.0f;
        widthSmoothed_ += kCorrSmooth * (instWidth - widthSmoothed_);
        width_.store(widthSmoothed_, std::memory_order_relaxed);

        // Momentary (400 ms = last 4 gating blocks) and short-term (3 s = 30 blocks).
        momentary_.store(windowLufs(4), std::memory_order_relaxed);
        shortTerm_.store(windowLufs(30), std::memory_order_relaxed);
        valid_.store(true, std::memory_order_relaxed);
    }

    /// Enable capturing a mono signal ring for masking band analysis. Off by
    /// default; the manager turns it on only during a masking pass. Cheap when
    /// off (a single branch); when on, just copies a mono downmix to the ring.
    void setSpectrumCaptureEnabled(bool shouldCapture) noexcept {
        captureSpectrum_.store(shouldCapture, std::memory_order_release);
    }
    bool spectrumCaptureEnabled() const noexcept {
        return captureSpectrum_.load(std::memory_order_acquire);
    }
    /// Message thread. Lock-free access to the captured signal for band analysis.
    const AudioTapBuffer& getSpectrumRing() const noexcept {
        return spectrumRing_;
    }
    double sampleRate() const noexcept {
        return sampleRate_;
    }

    /// Message thread. Lock-free snapshot of current measurements.
    TrackMeasurementSnapshot read() const noexcept {
        TrackMeasurementSnapshot s;
        s.valid = valid_.load(std::memory_order_relaxed);
        s.momentaryLufs = momentary_.load(std::memory_order_relaxed);
        s.shortTermLufs = shortTerm_.load(std::memory_order_relaxed);
        s.integratedLufs = computeIntegrated();
        s.samplePeakDb = samplePeak_.load(std::memory_order_relaxed);
        s.truePeakValid = enableTruePeak_;
        s.truePeakDb = enableTruePeak_ ? truePeak_.load(std::memory_order_relaxed) : kSilenceDb;
        s.correlation = correlation_.load(std::memory_order_relaxed);
        s.width = width_.load(std::memory_order_relaxed);

        // Dynamics: peak (true-peak if available, else sample peak) above loudness.
        const float peak =
            s.truePeakValid && s.truePeakDb > kSilenceDb ? s.truePeakDb : s.samplePeakDb;
        if (peak > kSilenceDb && s.integratedLufs > kSilenceLufs)
            s.plr = peak - s.integratedLufs;
        if (peak > kSilenceDb && s.shortTermLufs > kSilenceLufs)
            s.psr = peak - s.shortTermLufs;
        return s;
    }

    bool truePeakEnabled() const noexcept {
        return enableTruePeak_;
    }

  private:
    // ---- ITU-R BS.1770-4 K-weighting: two cascaded biquads (TDF-II) ----------
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct BiquadState {
        double z1 = 0, z2 = 0;
    };

    Biquad preFilter_;                    // stage 1: high-shelf
    Biquad highPass_;                     // stage 2: RLB high-pass
    std::array<BiquadState, 2> filtL_{};  // [0]=pre, [1]=highpass
    std::array<BiquadState, 2> filtR_{};

    // Coefficient derivation ported from the well-known libebur128 filter design
    // so LUFS is correct at any sample rate, not just 48 kHz.
    void computeKWeightingCoeffs(double fs) {
        {
            const double f0 = 1681.974450955533;
            const double G = 3.999843853973347;
            const double Q = 0.7071752369554196;
            const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            preFilter_.b0 = (Vh + Vb * K / Q + K * K) / a0;
            preFilter_.b1 = 2.0 * (K * K - Vh) / a0;
            preFilter_.b2 = (Vh - Vb * K / Q + K * K) / a0;
            preFilter_.a1 = 2.0 * (K * K - 1.0) / a0;
            preFilter_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444;
            const double Q = 0.5003270373238773;
            const double K = std::tan(juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highPass_.b0 = 1.0 / a0;
            highPass_.b1 = -2.0 / a0;
            highPass_.b2 = 1.0 / a0;
            highPass_.a1 = 2.0 * (K * K - 1.0) / a0;
            highPass_.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    static double biquad(const Biquad& c, BiquadState& s, double x) noexcept {
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    double applyKWeight(std::array<BiquadState, 2>& st, double x) const noexcept {
        return biquad(highPass_, st[1], biquad(preFilter_, st[0], x));
    }

    // ---- Gating blocks (100 ms) feeding momentary/short-term/integrated -------
    static constexpr int kRing = 30;           // 30 * 100 ms = 3 s short-term window
    std::array<double, kRing> blockEnergy_{};  // mean-square per 100 ms block
    int blockCount_ = 0;                       // total blocks closed since reset
    double curBlockAcc_ = 0.0;
    int curBlockN_ = 0;
    int blockSamples_ = 4800;

    // Integrated histogram: block-loudness in 0.1 LU bins from -70..+5 LUFS.
    static constexpr int kHistBins = 751;
    static constexpr double kHistMin = -70.0;
    static constexpr double kHistStep = 0.1;
    std::array<std::atomic<std::uint32_t>, kHistBins> histogram_{};

    void closeGatingBlock() noexcept {
        const double meanSq = curBlockN_ > 0 ? curBlockAcc_ / curBlockN_ : 0.0;
        blockEnergy_[static_cast<size_t>(blockCount_ % kRing)] = meanSq;
        ++blockCount_;
        curBlockAcc_ = 0.0;
        curBlockN_ = 0;

        // Integrated: 400 ms gating block = last 4 100 ms blocks; absolute -70 LUFS gate.
        if (blockCount_ >= 4) {
            double e = 0.0;
            for (int k = 0; k < 4; ++k)
                e += blockEnergy_[static_cast<size_t>((blockCount_ - 1 - k) % kRing)];
            const double meanSq4 = e / 4.0;
            if (meanSq4 > 0.0) {
                const double loud = -0.691 + 10.0 * std::log10(meanSq4);
                if (loud >= -70.0) {
                    int bin = static_cast<int>(std::lround((loud - kHistMin) / kHistStep));
                    bin = juce::jlimit(0, kHistBins - 1, bin);
                    histogram_[static_cast<size_t>(bin)].fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    /// Loudness over the last `nBlocks` 100 ms blocks (momentary/short-term).
    float windowLufs(int nBlocks) const noexcept {
        const int n = juce::jmin(nBlocks, blockCount_, kRing);
        if (n <= 0)
            return kSilenceLufs;
        double e = 0.0;
        for (int k = 0; k < n; ++k)
            e += blockEnergy_[static_cast<size_t>((blockCount_ - 1 - k) % kRing)];
        const double meanSq = e / n;
        if (meanSq <= 0.0)
            return kSilenceLufs;
        return static_cast<float>(-0.691 + 10.0 * std::log10(meanSq));
    }

    /// Gated integrated loudness from the histogram (BS.1770 relative -10 LU gate).
    float computeIntegrated() const noexcept {
        // First pass: ungated mean energy of all blocks above the absolute gate.
        double sumE = 0.0;
        std::uint64_t count = 0;
        for (int b = 0; b < kHistBins; ++b) {
            const std::uint32_t c =
                histogram_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
            if (c == 0)
                continue;
            const double loud = kHistMin + b * kHistStep;
            const double energy = std::pow(10.0, (loud + 0.691) / 10.0);
            sumE += energy * c;
            count += c;
        }
        if (count == 0)
            return kSilenceLufs;
        const double ungated = -0.691 + 10.0 * std::log10(sumE / static_cast<double>(count));
        const double relGate = ungated - 10.0;

        // Second pass: mean energy of blocks above the relative gate.
        double sumE2 = 0.0;
        std::uint64_t count2 = 0;
        for (int b = 0; b < kHistBins; ++b) {
            const double loud = kHistMin + b * kHistStep;
            if (loud < relGate)
                continue;
            const std::uint32_t c =
                histogram_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
            if (c == 0)
                continue;
            const double energy = std::pow(10.0, (loud + 0.691) / 10.0);
            sumE2 += energy * c;
            count2 += c;
        }
        if (count2 == 0)
            return kSilenceLufs;
        return static_cast<float>(-0.691 + 10.0 * std::log10(sumE2 / static_cast<double>(count2)));
    }

    // ---- True-peak 4x polyphase oversampler ----------------------------------
    static constexpr int kOsFactor = 4;
    static constexpr int kTapsPerPhase = 12;
    std::array<std::array<float, kTapsPerPhase>, kOsFactor> osCoeffs_{};
    std::array<float, kTapsPerPhase> tpDelayL_{};
    std::array<float, kTapsPerPhase> tpDelayR_{};

    void buildOversampler() {
        // Windowed-sinc low-pass (cutoff at original Nyquist), split into 4 polyphase
        // sub-filters. Hann window over the full kernel for a clean stopband.
        const int total = kOsFactor * kTapsPerPhase;
        const double fc = 0.5 / kOsFactor;  // normalised to oversampled rate
        std::vector<double> kernel(static_cast<size_t>(total));
        const double mid = (total - 1) / 2.0;
        for (int n = 0; n < total; ++n) {
            const double x = n - mid;
            const double sinc = std::abs(x) < 1.0e-9
                                    ? 2.0 * fc
                                    : std::sin(2.0 * juce::MathConstants<double>::pi * fc * x) /
                                          (juce::MathConstants<double>::pi * x);
            const double w =
                0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * n / (total - 1));
            kernel[static_cast<size_t>(n)] = sinc * w;
        }
        // Unity passband gain after upsampling (compensate the kOsFactor zero-stuffing loss).
        double sum = 0.0;
        for (double v : kernel)
            sum += v;
        const double norm = sum > 1.0e-12 ? (kOsFactor / sum) : 1.0;
        for (int phase = 0; phase < kOsFactor; ++phase)
            for (int t = 0; t < kTapsPerPhase; ++t) {
                const int idx = t * kOsFactor + phase;
                osCoeffs_[static_cast<size_t>(phase)][static_cast<size_t>(t)] =
                    idx < total ? static_cast<float>(kernel[static_cast<size_t>(idx)] * norm)
                                : 0.0f;
            }
    }

    float oversamplePeak(std::array<float, kTapsPerPhase>& delay, const float* x, int n) noexcept {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            // Shift newest sample into the delay line (newest at [0]).
            for (int t = kTapsPerPhase - 1; t > 0; --t)
                delay[static_cast<size_t>(t)] = delay[static_cast<size_t>(t - 1)];
            delay[0] = x[i];
            for (int phase = 0; phase < kOsFactor; ++phase) {
                float acc = 0.0f;
                const auto& c = osCoeffs_[static_cast<size_t>(phase)];
                for (int t = 0; t < kTapsPerPhase; ++t)
                    acc += c[static_cast<size_t>(t)] * delay[static_cast<size_t>(t)];
                peak = juce::jmax(peak, std::abs(acc));
            }
        }
        return peak;
    }

    static float linearToDb(float lin) noexcept {
        return lin > 1.0e-9f ? 20.0f * std::log10(lin) : kSilenceDb;
    }

    static void publishMax(std::atomic<float>& slot, float candidate) noexcept {
        float cur = slot.load(std::memory_order_relaxed);
        while (candidate > cur &&
               !slot.compare_exchange_weak(cur, candidate, std::memory_order_relaxed)) {
        }
    }

    static constexpr float kCorrSmooth = 0.2f;  // one-pole smoothing per block

    double sampleRate_ = 48000.0;
    bool enableTruePeak_ = false;
    std::vector<float> scratchL_, scratchR_;
    float corrSmoothed_ = 1.0f, widthSmoothed_ = 0.0f;

    std::atomic<float> momentary_{kSilenceLufs};
    std::atomic<float> shortTerm_{kSilenceLufs};
    std::atomic<float> samplePeak_{kSilenceDb};
    std::atomic<float> truePeak_{kSilenceDb};
    std::atomic<float> correlation_{1.0f};
    std::atomic<float> width_{0.0f};
    std::atomic<bool> valid_{false};

    // Masking band analysis: a mono signal ring filled only while capture is on.
    // The FFT/band grouping runs on the message thread (see band_spectrum).
    std::atomic<bool> captureSpectrum_{false};
    AudioTapBuffer spectrumRing_{4096};  // >= one 2048-pt FFT frame
};

}  // namespace magda::daw::audio
