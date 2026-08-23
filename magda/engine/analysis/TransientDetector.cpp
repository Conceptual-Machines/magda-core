#include "analysis/TransientDetector.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace magda::engine {

namespace {

/// The incumbent reads in chunks of this, and where a trigger's rewind lands is
/// measured from the start of one. Matching it is what keeps a detected marker
/// in the same place rather than half a millisecond from it.
constexpr int kBlockSamples = 32768;

/// A one-pole follower with separate attack and release, where 1 is instant and
/// 0 never moves. The incumbent's, coefficient for coefficient.
class EnvelopeFollower {
  public:
    void setCoefficients(float attack, float release) noexcept {
        attack_ = attack;
        release_ = release;
    }

    void process(float* samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const auto in = std::abs(samples[i]);

            if (envelope_ < in)
                envelope_ += attack_ * (in - envelope_);
            else if (envelope_ > in)
                envelope_ -= release_ * (envelope_ - in);

            samples[i] = envelope_;
        }
    }

  private:
    float envelope_ = 0.0f;
    float attack_ = 1.0f;
    float release_ = 1.0f;
};

/// Sample-to-sample difference. What turns a smoothed envelope into something
/// that peaks where the envelope was climbing fastest, which is the onset.
class Differentiator {
  public:
    void process(float* samples, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            const auto current = samples[i];
            samples[i] = current - last_;
            last_ = current;
        }
    }

  private:
    float last_ = 0.0f;
};

/// The file's own peak, which the threshold is relative to. A quiet recording
/// and a loud one have the same transients, and a fixed threshold would find
/// them only in the second.
float peakOf(AudioFileReader& reader, juce::AudioBuffer<float>& block, std::int64_t totalSamples) {
    float peak = 0.0f;

    for (std::int64_t at = 0; at < totalSamples; at += kBlockSamples) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(kBlockSamples, totalSamples - at));

        if (reader.read(block, 0, at, count) <= 0)
            continue;

        const auto range =
            juce::FloatVectorOperations::findMinAndMax(block.getReadPointer(0), count);
        peak = std::max(peak, std::max(std::abs(range.getStart()), std::abs(range.getEnd())));
    }

    return peak;
}

/// Thin @p transients until none are closer together than @p spacing.
///
/// Backwards, keeping the later of any pair, and repeated because one pass can
/// leave a new pair adjacent where it removed what sat between them. The
/// incumbent gives up after ten passes and so does this: what it is thinning is
/// a detector's own retriggers, and a file that still has them after ten rounds
/// has something the spacing rule was never going to fix.
void thin(std::vector<double>& transients, double spacing) {
    if (transients.size() < 2)
        return;

    for (int pass = 0; pass < 10; ++pass) {
        const auto before = transients.size();
        auto last = transients.back();

        for (int i = static_cast<int>(transients.size()) - 2; i >= 0; --i) {
            const auto at = transients[static_cast<std::size_t>(i)];

            if (last - at < spacing)
                transients.erase(transients.begin() + i);
            else
                last = at;
        }

        if (transients.size() == before)
            return;
    }
}

}  // namespace

std::vector<double> detectTransients(AudioFileReader& reader,
                                     const TransientDetectionSettings& settings) {
    std::vector<double> transients;

    const auto sampleRate = reader.sampleRate();
    const auto totalSamples = reader.lengthInSamples();

    if (!(sampleRate > 0.0) || totalSamples <= 0)
        return transients;

    // One channel, which is what the reader hands the file's first through and
    // what the incumbent detects on. A transient is in every channel of a
    // recording that has one.
    juce::AudioBuffer<float> block(1, kBlockSamples);

    const auto peak = peakOf(reader, block, totalSamples);
    const auto scale = peak > 0.0f ? 1.0f / peak : 1.0f;

    EnvelopeFollower followers[3];
    for (auto& follower : followers)
        follower.setCoefficients(1.0f, 0.002f);

    Differentiator differentiator;

    const auto threshold = juce::Decibels::decibelsToGain(
        -10.0f - std::clamp(settings.sensitivity, 0.0f, 1.0f) * 30.0f);
    const auto lockout = static_cast<int>(sampleRate * settings.retriggerSeconds);

    // Half a millisecond, because what crosses the threshold is the differential
    // of a smoothed envelope and that peaks a little after the attack it came
    // from. Placing the marker where the sound starts rather than where the
    // detector noticed is the difference between a warped beat landing on the
    // grid and landing just behind it.
    //
    // Rounded up, which is not a preference. The incumbent truncates after the
    // subtraction (`int (i - sampleRate * 0.0005)`), and for a whole-numbered i
    // that is the same as subtracting the rounded-up rewind: at 44100 its
    // effective rewind is 23 samples, not the 22 that truncating the rewind on
    // its own would give. Reproducing this detector exactly is the whole
    // argument for having written it this way, and one sample is a marker that
    // moved.
    const auto rewind = static_cast<int>(std::ceil(sampleRate * 0.0005));

    int countdown = 0;

    for (std::int64_t at = 0; at < totalSamples; at += kBlockSamples) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(kBlockSamples, totalSamples - at));

        reader.read(block, 0, at, count);

        auto* samples = block.getWritePointer(0);
        juce::FloatVectorOperations::multiply(samples, scale, count);

        followers[0].process(samples, count);
        followers[1].process(samples, count);
        differentiator.process(samples, count);
        followers[2].process(samples, count);

        for (int i = 0; i < count; ++i) {
            if (countdown > 0)
                --countdown;

            if (samples[i] > threshold) {
                if (countdown == 0)
                    transients.push_back(static_cast<double>(at + std::max(0, i - rewind)) /
                                         sampleRate);

                countdown = lockout;
            }
        }
    }

    thin(transients, settings.minimumSpacingSeconds);

    return transients;
}

}  // namespace magda::engine
