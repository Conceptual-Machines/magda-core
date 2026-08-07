#include "clip/ClipStretcher.hpp"

#include <SoundTouch.h>
#include <signalsmith-stretch.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/TimeStretchModes.hpp"
#include "io/SourceReaders.hpp"

namespace magda::engine {

namespace {

/// Channel pointers, gathered without allocating. Every implementation needs
/// them, both libraries want an array of them, and a block cannot be handed over
/// as one.
class ChannelPointers {
  public:
    explicit ChannelPointers(int numChannels)
        : reading_(static_cast<std::size_t>(numChannels)),
          writing_(static_cast<std::size_t>(numChannels)) {}

    const float* const* gather(juce::dsp::AudioBlock<const float> block, int offset = 0) {
        for (std::size_t channel = 0; channel < reading_.size(); ++channel)
            reading_[channel] = channel < block.getNumChannels()
                                    ? block.getChannelPointer(channel) + offset
                                    : nullptr;
        return reading_.data();
    }

    float* const* gather(juce::dsp::AudioBlock<float> block) {
        for (std::size_t channel = 0; channel < writing_.size(); ++channel)
            writing_[channel] =
                channel < block.getNumChannels() ? block.getChannelPointer(channel) : nullptr;
        return writing_.data();
    }

  private:
    std::vector<const float*> reading_;
    std::vector<float*> writing_;
};

int samplesOf(juce::dsp::AudioBlock<const float> block) {
    return static_cast<int>(block.getNumSamples());
}

/// A pre-roll buffer big enough for the rate an event usually runs at, with room
/// for it to wander. Auto tempo's rate moves with the tempo curve, so the rate at
/// the moment of a locate is not the rate this was sized for; a quarter again
/// covers an ordinary tempo ramp, and beyond that priming is short rather than
/// wrong.
constexpr double kPreRollHeadroom = 1.25;

/**
 * @brief Signalsmith Stretch, the default engine.
 *
 * A phase vocoder, so the ratio is whatever a call is handed: input samples in,
 * output samples out, and the two lengths are the rate. That is what makes a
 * tempo curve free here, where an engine with a set tempo has to be told.
 *
 * Configured the way the incumbent configures it, down to the formant base and
 * the pitch-compensated neutral formant shift, because a project that sounded a
 * particular way through the fork has to go on sounding that way.
 *
 * Priming is `outputSeek`, which is the library's own answer to starting in the
 * middle of a file: hand it the material leading up to the first sample wanted
 * and it pre-computes the output that would have led there, so the next call
 * begins aligned rather than fading in over a window. The incumbent primes with
 * the material *after* the start instead, and so begins every stretched clip
 * about a preset window late; that is the one place this deliberately differs.
 */
class SignalsmithClipStretcher final : public ClipStretcher {
  public:
    explicit SignalsmithClipStretcher(const StretchSetup& setup)
        : channels_(setup.numChannels), pointers_(setup.numChannels) {
        // splitComputation: the FFT work is spread across calls rather than
        // landing in whichever block crosses a window boundary. This runs on the
        // audio thread, so the flat cost is the one that matters.
        stretch_.presetDefault(channels_, static_cast<float>(setup.sampleRate), true);
        stretch_.setFormantBase(static_cast<float>(200.0 / setup.sampleRate));
        stretch_.setTransposeSemitones(setup.semitones);
        stretch_.setFormantFactor(1.0f, true);

        allocatePreRoll(channels_, static_cast<int>(std::ceil(preRollSamples(setup.nominalRate) *
                                                              kPreRollHeadroom)));
    }

    int preRollSamples(double rate) const override {
        return static_cast<int>(std::ceil(stretch_.outputSeekLength(static_cast<float>(rate))));
    }

    void reset() override {
        stretch_.reset();
    }

    void prime(PrefetchStream& stream, std::int64_t until, int samples, double) override {
        const auto before = readPreRoll(stream, until, samples);
        const auto count = samplesOf(before);

        if (count <= 0) {
            stretch_.reset();
            return;
        }

        // outputSeek resets on its way in, and infers the rate from how much it
        // was given, which is why preRollSamples above is the length to give it.
        stretch_.outputSeek(pointers_.gather(before), count);
    }

    void process(juce::dsp::AudioBlock<const float> input, double, double,
                 juce::dsp::AudioBlock<float> output) override {
        const auto in = samplesOf(input);
        const auto out = static_cast<int>(output.getNumSamples());
        if (out <= 0)
            return;

        if (in <= 0) {
            output.clear();
            return;
        }

        const auto* const* sources = pointers_.gather(input);
        auto* const* destinations = pointers_.gather(output);
        stretch_.process(sources, in, destinations, out);
    }

  private:
    int channels_ = 2;
    ChannelPointers pointers_;
    signalsmith::stretch::SignalsmithStretch<float> stretch_;
};

/**
 * @brief SoundTouch, for the two pinned modes that name it.
 *
 * Carried because kSoundTouchNormal and kSoundTouchBetter are project-file
 * integers: sessions saved with either have to go on playing through the
 * algorithm they were made with, whatever else is available now.
 *
 * A pipe rather than a function, which is the awkward part. It takes input and
 * gives back whatever it has finished, so a block cannot demand an exact length
 * the way the phase vocoder can. Two things follow. Its tempo is set per block
 * from the two lengths, so a moving rate still works. And what it holds back is
 * absorbed by priming with more material than alignment alone needs: enough that
 * once the pre-roll's own output has been discarded there is a block's worth
 * already waiting, so the first block a listener hears is full rather than the
 * fourth.
 */
class SoundTouchClipStretcher final : public ClipStretcher {
  public:
    explicit SoundTouchClipStretcher(const StretchSetup& setup)
        : channels_(setup.numChannels), maxBlockSamples_(setup.maxBlockSamples) {
        touch_.setChannels(static_cast<unsigned int>(channels_));
        touch_.setSampleRate(static_cast<unsigned int>(setup.sampleRate));

        if (setup.mode == time_stretch_mode::kSoundTouchBetter) {
            touch_.setSetting(SETTING_USE_AA_FILTER, 1);
            touch_.setSetting(SETTING_AA_FILTER_LENGTH, 64);
            touch_.setSetting(SETTING_USE_QUICKSEEK, 0);
            touch_.setSetting(SETTING_SEQUENCE_MS, 60);
            touch_.setSetting(SETTING_SEEKWINDOW_MS, 25);
        }

        touch_.setPitchSemiTones(setup.semitones);
        touch_.setTempo(std::clamp(setup.nominalRate, kMinStretchRate, kMaxStretchRate));

        // Sized for the worst block this can be asked for: the largest output
        // block, times the fastest the reading may be consumed for it. Grown
        // here, on the thread that made this, and never again.
        const auto frames =
            static_cast<int>(std::ceil(setup.maxBlockSamples * kMaxStretchRate)) + 1;
        interleaved_.resize(static_cast<std::size_t>(frames * channels_));
        deinterleaved_.resize(static_cast<std::size_t>(setup.maxBlockSamples * channels_));

        allocatePreRoll(channels_, static_cast<int>(std::ceil(preRollSamples(setup.nominalRate) *
                                                              kPreRollHeadroom)));
    }

    int preRollSamples(double rate) const override {
        // What the pipe holds before it will answer at all, plus a block's worth
        // of surplus so that the block after priming is already there. Both are
        // input samples, so the surplus is counted at the rate it will be
        // consumed at.
        const auto latency = touch_.getSetting(SETTING_INITIAL_LATENCY);
        const auto batch = touch_.getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE);
        const auto cushion = std::max(maxBlockSamples_, std::max(batch, 0));

        return latency + static_cast<int>(std::ceil(cushion * rate));
    }

    void reset() override {
        touch_.clear();
        discard_ = 0;
    }

    void prime(PrefetchStream& stream, std::int64_t until, int samples, double rate) override {
        touch_.clear();
        discard_ = 0;
        touch_.setTempo(std::clamp(rate, kMinStretchRate, kMaxStretchRate));

        const auto before = readPreRoll(stream, until, samples);
        const auto count = samplesOf(before);
        if (count <= 0)
            return;

        writeAll(before, count);

        // Everything the pre-roll will eventually come back out as. Discarding
        // it is what puts the output at the first sample meant to be heard;
        // draining happens as it becomes available, over however many blocks
        // that takes, because the pipe cannot be made to answer sooner.
        discard_ = static_cast<int>(std::llround(count / std::max(rate, kMinStretchRate)));
    }

    void process(juce::dsp::AudioBlock<const float> input, double, double,
                 juce::dsp::AudioBlock<float> output) override {
        const auto in = samplesOf(input);
        const auto out = static_cast<int>(output.getNumSamples());
        if (out <= 0)
            return;

        if (in > 0) {
            touch_.setTempo(
                std::clamp(static_cast<double>(in) / out, kMinStretchRate, kMaxStretchRate));
            writeAll(input, in);
        }

        if (discard_ > 0)
            discard_ -=
                static_cast<int>(touch_.receiveSamples(static_cast<unsigned int>(discard_)));

        const auto ready = discard_ > 0
                               ? 0
                               : static_cast<int>(touch_.receiveSamples(
                                     deinterleaved_.data(),
                                     static_cast<unsigned int>(std::min(out, maxBlockSamples_))));

        for (std::size_t channel = 0; channel < output.getNumChannels(); ++channel) {
            auto* destination = output.getChannelPointer(channel);
            const auto source = std::min(static_cast<int>(channel), channels_ - 1);

            for (auto sample = 0; sample < ready; ++sample)
                destination[sample] =
                    deinterleaved_[static_cast<std::size_t>(sample * channels_ + source)];

            // A pipe that has not caught up yet is silence rather than whatever
            // the scratch held. It happens on the blocks a locate is still being
            // absorbed over, and the voice hears it as not having sounded.
            for (auto sample = ready; sample < out; ++sample)
                destination[sample] = 0.0f;
        }
    }

  private:
    /// All of @p count, in pieces the interleaving buffer can hold. The buffer
    /// is sized for one block's worth of reading and a pre-roll is many times
    /// that, and putSamples does not care how many calls the material arrives
    /// in; nothing else here may assume a length it did not size for.
    void writeAll(juce::dsp::AudioBlock<const float> block, int count) {
        const auto stride =
            static_cast<int>(interleaved_.size() / static_cast<std::size_t>(channels_));

        for (auto done = 0; done < count;) {
            const auto run = std::min(stride, count - done);
            write(block, done, run);
            done += run;
        }
    }

    void write(juce::dsp::AudioBlock<const float> block, int offset, int count) {
        for (auto sample = 0; sample < count; ++sample)
            for (auto channel = 0; channel < channels_; ++channel) {
                const auto source =
                    std::min(static_cast<std::size_t>(channel), block.getNumChannels() - 1);
                interleaved_[static_cast<std::size_t>(sample * channels_ + channel)] =
                    block.getChannelPointer(source)[offset + sample];
            }

        touch_.putSamples(interleaved_.data(), static_cast<unsigned int>(count));
    }

    int channels_ = 2;
    int maxBlockSamples_ = 512;

    /// Output samples still owed to the pre-roll. Non-zero only while a start or
    /// a locate is being absorbed.
    int discard_ = 0;

    std::vector<float> interleaved_;
    std::vector<float> deinterleaved_;
    soundtouch::SoundTouch touch_;
};

/**
 * @brief The reading played faster or slower, pitch and all.
 *
 * Not a stretcher: this is what a tape machine does, and it is what analog pitch
 * asks for (AudioEvent::isAnalogPitchActive). The model has already folded the
 * pitch factor into the speed ratio when that mode is on, so there is nothing to
 * do here but land between samples.
 *
 * The same cubic curve the rate converter below the stream uses
 * (io/SourceReaders.hpp), so a file at another rate and a clip playing fast are
 * not two different sounds.
 *
 * The curve reaches a sample either side of where it lands, and the stream above
 * cannot be read twice: a read that does not continue the last one is a seek. So
 * the read runs @ref readAheadSamples ahead of the position wanted and the few
 * samples behind it are kept here. A constant offset and a fixed history, not a
 * cursor: where a block reads is still derived from the timeline, and nothing
 * accumulates.
 */
class ResamplingClipStretcher final : public ClipStretcher {
  public:
    explicit ResamplingClipStretcher(const StretchSetup& setup) : channels_(setup.numChannels) {
        history_.setSize(channels_, kHistory);
        history_.clear();
        allocatePreRoll(channels_, kHistory);
    }

    int readAheadSamples() const override {
        return kReadAhead;
    }

    int preRollSamples(double) const override {
        return kHistory;
    }

    void reset() override {
        history_.clear();
    }

    void prime(PrefetchStream& stream, std::int64_t until, int samples, double) override {
        history_.clear();

        const auto before = readPreRoll(stream, until, std::min(samples, kHistory));
        const auto count = samplesOf(before);
        const auto taken = std::min(count, kHistory);
        if (taken <= 0)
            return;

        // The last of it, and pushed to the end: what a curve reaches back for
        // is the material immediately before where it lands, so a short pre-roll
        // leaves the silence at the far end rather than under the first sample.
        for (auto channel = 0; channel < channels_; ++channel) {
            const auto source =
                std::min(static_cast<std::size_t>(channel), before.getNumChannels() - 1);
            history_.copyFrom(channel, kHistory - taken,
                              before.getChannelPointer(source) + count - taken, taken);
        }
    }

    void process(juce::dsp::AudioBlock<const float> input, double offset, double step,
                 juce::dsp::AudioBlock<float> output) override {
        const auto in = samplesOf(input);
        const auto out = static_cast<int>(output.getNumSamples());
        if (out <= 0)
            return;

        if (in <= 0) {
            output.clear();
            return;
        }

        for (std::size_t channel = 0; channel < output.getNumChannels(); ++channel) {
            const auto source = std::min(channel, input.getNumChannels() - 1);
            const auto* kept =
                history_.getReadPointer(std::min(static_cast<int>(channel), channels_ - 1));
            const auto* fresh = input.getChannelPointer(source);
            auto* destination = output.getChannelPointer(channel);

            // History first, then this block's input: one array, so a position
            // that falls across the join needs no special case.
            const auto at = [kept, fresh, in](int index) {
                if (index < kHistory)
                    return kept[std::max(index, 0)];
                return fresh[std::min(index - kHistory, in - 1)];
            };

            for (auto sample = 0; sample < out; ++sample) {
                const auto position = kHistory + offset + sample * step;
                const auto index = static_cast<int>(std::floor(position));

                destination[sample] = cubicLagrange(at(index - 1), at(index), at(index + 1),
                                                    at(index + 2), position - index);
            }
        }

        // What the next block will reach back into.
        for (auto channel = 0; channel < channels_; ++channel) {
            const auto source =
                std::min(static_cast<std::size_t>(channel), input.getNumChannels() - 1);
            const auto taken = std::min(in, kHistory);

            if (taken < kHistory)
                for (auto sample = 0; sample < kHistory - taken; ++sample)
                    history_.setSample(channel, sample,
                                       history_.getSample(channel, sample + taken));

            history_.copyFrom(channel, kHistory - taken,
                              input.getChannelPointer(source) + in - taken, taken);
        }
    }

  private:
    /// Samples kept behind the block. Enough that the slowest rate the engine
    /// allows still lands inside them, with a sample to spare either end.
    static constexpr int kHistory = 6;

    /// How far ahead of the wanted position the reading is consumed, so the
    /// curve's forward reach is always inside material already read.
    static constexpr int kReadAhead = 3;

    int channels_ = 2;
    juce::AudioBuffer<float> history_;
};

}  // namespace

void ClipStretcher::allocatePreRoll(int numChannels, int numSamples) {
    preRoll_.setSize(std::max(1, numChannels), std::max(0, numSamples));
    preRoll_.clear();
}

juce::dsp::AudioBlock<const float> ClipStretcher::readPreRoll(PrefetchStream& stream,
                                                              std::int64_t until, int wanted) {
    // What fits, and taken from the end: the samples that matter are the ones
    // immediately before the first one to be heard, so a pre-roll that has to be
    // cut short is cut at the far end rather than at the near one.
    const auto count = std::min(wanted, preRoll_.getNumSamples());
    if (count <= 0)
        return {};

    juce::dsp::AudioBlock<float> block(preRoll_);
    auto region = block.getSubBlock(0, static_cast<std::size_t>(count));
    region.clear();

    // Whatever the stream had. Short is the ordinary answer while a locate is
    // still being caught up with, and the silence in front of it primes as
    // silence, which is what it will sound like.
    stream.read(until - count, region, count);

    return region;
}

std::unique_ptr<ClipStretcher> makeStretcher(const StretchSetup& setup) {
    if (setup.numChannels <= 0 || setup.maxBlockSamples <= 0 || !(setup.sampleRate > 0.0))
        return nullptr;

    switch (setup.mode) {
        case time_stretch_mode::kSignalsmith:
            return std::make_unique<SignalsmithClipStretcher>(setup);

        case time_stretch_mode::kSoundTouchNormal:
        case time_stretch_mode::kSoundTouchBetter:
            return std::make_unique<SoundTouchClipStretcher>(setup);

        case time_stretch_mode::kDisabled:
            // Nothing to preserve, so a rate change is a tape speed change and a
            // rate of one is nothing at all. This is the analog pitch path: the
            // model has already put the pitch factor into the speed ratio.
            //
            // A speed ramp is the exception to the rate of one. Its edges are
            // not at one however the clip is set, and a ramp is a tape effect in
            // any case, so this is what plays it.
            if (!setup.speedRamp && std::abs(setup.nominalRate - 1.0) < 1.0e-9)
                return nullptr;

            return std::make_unique<ResamplingClipStretcher>(setup);

        default:
            // A mode this build has no engine for, which is every value
            // Tracktion's enum holds that MAGDA never wrote. The incumbent
            // answers this with its default engine (TimeStretcher::
            // checkModeIsAvailable) and so does this: the clip is asking to be
            // stretched, and the position map has already decided how much
            // material a block will consume. Returning nothing here would leave
            // a block reading that material at unity and the next one starting
            // where the ratio says, which is a skip at every block boundary
            // rather than a fallback.
            return std::make_unique<SignalsmithClipStretcher>(setup);
    }
}

}  // namespace magda::engine
