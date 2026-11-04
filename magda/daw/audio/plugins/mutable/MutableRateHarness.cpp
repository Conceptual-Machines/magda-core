// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/mutable/MutableRateHarness.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace magda::daw::audio {

namespace {

constexpr int kZ = MutableResampler::kZeroCrossings;

/// Table points per zero crossing. Linear between them, the error is below -110 dB per tap.
constexpr int kResolution = 512;

/// With kZ = 24: flat to 0.456 of the lower rate within 0.1 dB, 89 dB down from 0.5625 of it.
constexpr double kKaiserBeta = 9.5;

constexpr int kTableSize = kZ * kResolution + 2;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; term > 1.0e-14 * sum; ++k) {
        const double half = x / (2.0 * k);
        term *= half * half;
        sum += term;
    }
    return sum;
}

/** @brief One side of the kernel, sampled in samples of the lower rate, with its slopes. */
struct KernelTable {
    float value[kTableSize]{};
    float slope[kTableSize]{};

    KernelTable() {
        const double norm = besselI0(kKaiserBeta);
        for (int j = 0; j < kZ * kResolution; ++j) {
            const double u = static_cast<double>(j) / kResolution;
            const double x = juce::MathConstants<double>::pi * u;
            const double sinc = j == 0 ? 1.0 : std::sin(x) / x;
            const double edge = u / kZ;
            value[j] = static_cast<float>(
                sinc * besselI0(kKaiserBeta * std::sqrt(1.0 - edge * edge)) / norm);
        }
        for (int j = 0; j + 1 < kTableSize; ++j)
            slope[j] = value[j + 1] - value[j];
    }

    /** @brief The kernel at @p u table points from its centre. */
    float at(double u) const {
        const int j = static_cast<int>(u);
        if (j >= kZ * kResolution)
            return 0.0f;
        return value[j] + static_cast<float>(u - j) * slope[j];
    }
};

const KernelTable& kernel() {
    static const KernelTable table;
    return table;
}

/// A rate as an integer. Hosts report whole hertz; anything else keeps a millihertz.
bool isWhole(double rate) {
    return std::abs(rate - std::round(rate)) < 1.0e-6;
}

}  // namespace

//==============================================================================
void MutableResampler::prepare(double inRate, double outRate) {
    const double unit = isWhole(inRate) && isWhole(outRate) ? 1.0 : 1000.0;
    auto in = std::max<std::int64_t>(1, std::llround(inRate * unit));
    auto out = std::max<std::int64_t>(1, std::llround(outRate * unit));
    const auto divisor = std::gcd(in, out);
    in /= divisor;
    out /= divisor;

    stepNumerator_ = in;
    denominator_ = out;
    passThrough_ = in == out;
    scale_ = std::min(1.0, static_cast<double>(out) / static_cast<double>(in));
    halfWidth_ = passThrough_ ? 0.0 : kZ / scale_;

    // Built here rather than on the first pull, which is on the audio thread.
    (void)kernel();
    reset(0);
}

void MutableResampler::reset(int delayOutputFrames) {
    const auto delay = std::max(0, delayOutputFrames);
    head_ = 0;
    fraction_ = 0;
    position_ = 0;

    int prime = delay;
    if (!passThrough_) {
        // The first output centred `delay` outputs ahead of input frame 0, with a full kernel of
        // silence behind it.
        const std::int64_t delayInput = static_cast<std::int64_t>(delay) * stepNumerator_;
        prime = static_cast<int>(
            std::ceil(static_cast<double>(delayInput) / static_cast<double>(denominator_) +
                      halfWidth_ - 1.0e-9));
        const std::int64_t centre = static_cast<std::int64_t>(prime) * denominator_ - delayInput;
        position_ = static_cast<int>(centre / denominator_);
        fraction_ = centre % denominator_;
    }

    jassert(prime < kCapacity / 2);
    count_ = std::min(prime, kCapacity / 2);
    std::fill_n(l_, count_, 0.0f);
    std::fill_n(r_, count_, 0.0f);
}

void MutableResampler::push(float l, float r) {
    if (head_ + count_ == kCapacity) {
        if (count_ == kCapacity) {
            jassertfalse;  // nothing pulls the history down: a caller bug, not a rate
            dropBefore(1);
        }
        std::memmove(l_, l_ + head_, sizeof(float) * static_cast<size_t>(count_));
        std::memmove(r_, r_ + head_, sizeof(float) * static_cast<size_t>(count_));
        head_ = 0;
    }
    l_[head_ + count_] = l;
    r_[head_ + count_] = r;
    ++count_;
}

int MutableResampler::lastTap() const {
    const double centre =
        position_ + static_cast<double>(fraction_) / static_cast<double>(denominator_);
    return static_cast<int>(std::floor(centre + halfWidth_));
}

bool MutableResampler::canPull() const {
    return passThrough_ ? position_ < count_ : lastTap() < count_;
}

void MutableResampler::pull(float& l, float& r) {
    if (passThrough_) {
        l = l_[head_];
        r = r_[head_];
        dropBefore(1);
        return;
    }

    const auto& table = kernel();
    const double centre =
        position_ + static_cast<double>(fraction_) / static_cast<double>(denominator_);
    const int first = std::max(0, static_cast<int>(std::floor(centre - halfWidth_)) + 1);
    const int last = std::min(count_ - 1, static_cast<int>(std::floor(centre + halfWidth_)));
    const double toTable = scale_ * kResolution;
    const float* histL = l_ + head_;
    const float* histR = r_ + head_;

    float accL = 0.0f, accR = 0.0f;
    for (int i = first; i <= last; ++i) {
        const float w = table.at(std::abs(centre - i) * toTable);
        accL += w * histL[i];
        accR += w * histR[i];
    }
    l = accL * static_cast<float>(scale_);
    r = accR * static_cast<float>(scale_);

    position_ += static_cast<int>(stepNumerator_ / denominator_);
    fraction_ += stepNumerator_ % denominator_;
    if (fraction_ >= denominator_) {
        fraction_ -= denominator_;
        ++position_;
    }
    const double next =
        position_ + static_cast<double>(fraction_) / static_cast<double>(denominator_);
    dropBefore(static_cast<int>(std::floor(next - halfWidth_)) + 1);
}

void MutableResampler::dropBefore(int index) {
    const int n = std::clamp(index, 0, count_);
    head_ += n;
    count_ -= n;
    // Passing through, the next output is always the oldest frame.
    if (!passThrough_)
        position_ -= n;
}

//==============================================================================
MutableRateHarness::MutableRateHarness(double internalRate, int blockFrames, Role role)
    : internalRate_(internalRate),
      blockFrames_(std::clamp(blockFrames, 1, kMaxBlockFrames)),
      role_(role) {}

void MutableRateHarness::prepare(double hostRate) {
    out_.prepare(internalRate_, hostRate);
    constexpr double kWhole = 1.0e-9;

    if (role_ == Role::Instrument) {
        // A block is rendered when the window's leading edge reaches it, which is a half-width
        // ahead of where it is heard.
        latency_ = static_cast<int>(std::ceil(out_.halfWidth() / out_.step() - kWhole));
    } else {
        // The output may not wait on a block still filling: the input leg's half-width, then the
        // output leg's and all but one frame of a block, in host frames (#2365 review).
        in_.prepare(hostRate, internalRate_);
        latency_ = static_cast<int>(std::ceil(
            in_.halfWidth() + (out_.halfWidth() + blockFrames_ - 1) * in_.step() - kWhole));
    }
    reset();
}

void MutableRateHarness::reset() {
    out_.reset(latency_);
    if (role_ == Role::Effect)
        in_.reset(0);
    blockFill_ = role_ == Role::Instrument ? blockFrames_ : 0;
}

void MutableRateHarness::pullPlaced(float& l, float& r) {
    if (out_.canPull()) {
        out_.pull(l, r);
        return;
    }
    // The latency placed at reset rules this out; a starved pull would shift the output for good.
    jassertfalse;
    l = r = 0.0f;
}

}  // namespace magda::daw::audio
