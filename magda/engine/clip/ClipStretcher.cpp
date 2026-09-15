#include "clip/ClipStretcher.hpp"

#include <SoundTouch.h>
#include <signalsmith-stretch.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
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
 * @brief The generator Signalsmith draws bin phases from below half speed.
 *
 * Its default seeds from std::random_device, so two renders of one timeline
 * differ (#2700). The engine is a private member, so draws go through the
 * generator the calling stretcher installs for the duration of each call.
 */
struct InstalledRandom {
    using result_type = std::minstd_rand::result_type;

    explicit InstalledRandom(long) {}

    static constexpr result_type min() {
        return std::minstd_rand::min();
    }
    static constexpr result_type max() {
        return std::minstd_rand::max();
    }

    result_type operator()() {
        jassert(installed != nullptr);
        return installed != nullptr ? (*installed)() : min();
    }

    static inline thread_local std::minstd_rand* installed = nullptr;
};

/// Installs @p generator for Signalsmith calls made while this lives.
class RandomScope {
  public:
    explicit RandomScope(std::minstd_rand& generator)
        : previous_(std::exchange(InstalledRandom::installed, &generator)) {}
    ~RandomScope() {
        InstalledRandom::installed = previous_;
    }

    RandomScope(const RandomScope&) = delete;
    RandomScope& operator=(const RandomScope&) = delete;

  private:
    std::minstd_rand* previous_;
};

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
        : channels_(setup.numChannels), pointers_(setup.numChannels), stretch_(kRandomSeed) {
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
        random_.seed(kRandomSeed);
    }

    void prime(PrefetchStream& stream, std::int64_t until, int samples, double) override {
        const auto before = readPreRoll(stream, until, samples);
        const auto count = samplesOf(before);

        random_.seed(kRandomSeed);
        if (count <= 0) {
            stretch_.reset();
            return;
        }

        // outputSeek resets on its way in, and infers the rate from how much it
        // was given, which is why preRollSamples above is the length to give it.
        const RandomScope scope(random_);
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
        const RandomScope scope(random_);
        stretch_.process(sources, in, destinations, out);
    }

  private:
    static constexpr long kRandomSeed = 2700;

    int channels_ = 2;
    ChannelPointers pointers_;
    std::minstd_rand random_{kRandomSeed};
    signalsmith::stretch::SignalsmithStretch<float, InstalledRandom> stretch_;
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
 * the way the phase vocoder can. Read ahead of the audible position to cover
 * that latency. Priming feeds the beginning of the material to be heard and
 * keeps its output; discarding it would both lose the opening transient and
 * leave the pipe short again, however much input was used to prime it (#2683).
 */
class SoundTouchClipStretcher final : public ClipStretcher {
  public:
    explicit SoundTouchClipStretcher(const StretchSetup& setup)
        : channels_(setup.numChannels),
          maxBlockSamples_(stretchWorkSamples(setup.maxBlockSamples)) {
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

        // This SoundTouch binding uses the library's default cubic interpolator,
        // whose first output centres on input sample one, and a centred AA
        // filter. Supply that history too, or an impulse at file sample zero
        // is skipped before time stretching even begins. For downward pitch
        // shifts the AA filter follows interpolation, so its half-window is
        // converted back to input samples.
        const auto pitchRate = std::pow(2.0, static_cast<double>(setup.semitones) / 12.0);
        const auto filterHalf = touch_.getSetting(SETTING_USE_AA_FILTER) != 0
                                    ? touch_.getSetting(SETTING_AA_FILTER_LENGTH) / 2
                                    : 0;
        history_ = 1 + static_cast<int>(std::ceil(filterHalf * std::min(1.0, pitchRate)));

        // A fixed input lead, shared by the pool's cue and the voice's reads.
        // Cover the initial latency and a batch so output remains available
        // between SoundTouch's processing bursts. Use the cell, never the host
        // block size: priming must not change with callback partitioning.
        const auto batch =
            std::max(kStretchCellSamples, touch_.getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE));
        readAhead_ = touch_.getSetting(SETTING_INITIAL_LATENCY) +
                     static_cast<int>(std::ceil(
                         batch * std::clamp(setup.nominalRate, kMinStretchRate, kMaxStretchRate)));

        // Sized for the largest single push rather than for the largest block,
        // and that is what makes it the stride below rather than merely the
        // capacity. Everything that goes into the library goes through write(),
        // in pieces this buffer's length decides, so a length taken from the
        // block hands SoundTouch a different sequence of putSamples calls at
        // every block size. See stretchPushSamples: the sequence is what has to
        // be invariant, not just the room (#2078).
        //
        // Grown here, on the thread that made this, and never again.
        const auto frames = stretchPushSamples();
        interleaved_.resize(static_cast<std::size_t>(frames) * channels_);
        sources_.resize(static_cast<std::size_t>(channels_));
        deinterleaved_.resize(static_cast<std::size_t>(stretchWorkSamples(setup.maxBlockSamples)) *
                              channels_);

        allocatePreRoll(channels_, readAhead_ + history_);

        // Grow SoundTouch's FIFOs off the audio thread. The front buffer must
        // hold its longest sequence plus the largest input push. clear() keeps
        // the capacity, so subsequent starts and locates can reuse it.
        const auto worst = worstCase(setup);

        touch_.setTempo(worst.atTempo);
        pushSilence(worst.input);
        drainAll();

        // Reserve the downstream buffers across the supported rate range,
        // including the automatic sequence/seek-window breakpoints. Priming
        // retains the whole lookahead's output, so one batch alone is no
        // longer sufficient to reserve the output FIFO.
        for (const auto rate :
             {kMinStretchRate, kAutoSequenceLow, 1.0, kAutoSequenceTop, kMaxStretchRate,
              std::clamp(setup.nominalRate, kMinStretchRate, kMaxStretchRate)}) {
            touch_.setTempo(rate);
            // Priming retains its output now, so reserve room for the entire
            // lookahead at each rate as well as for one processing batch.
            pushSilence(std::max(readAhead_ + history_,
                                 touch_.getSetting(SETTING_NOMINAL_INPUT_SEQUENCE) + 1));
            drainAll();
        }

        touch_.setTempo(std::clamp(setup.nominalRate, kMinStretchRate, kMaxStretchRate));
        touch_.clear();
    }

    int readAheadSamples() const override {
        return readAhead_;
    }

    int preRollSamples(double) const override {
        return readAhead_ + history_;
    }

    void reset() override {
        touch_.clear();
    }

    void prime(PrefetchStream& stream, std::int64_t until, int samples, double rate) override {
        touch_.clear();
        touch_.setTempo(std::clamp(rate, kMinStretchRate, kMaxStretchRate));

        const auto before = readPreRoll(stream, until, samples);
        const auto count = samplesOf(before);
        if (count <= 0)
            return;

        // until includes readAhead_; samples also includes the filter history.
        // Retain the output from the audible start, and leave the stream where
        // the next cell will continue reading.
        writeAll(before, count);
    }

    void process(juce::dsp::AudioBlock<const float> input, double, double rate,
                 juce::dsp::AudioBlock<float> output) override {
        const auto in = samplesOf(input);
        const auto out = static_cast<int>(output.getNumSamples());
        if (out <= 0)
            return;

        if (in > 0) {
            // The input endpoints are rounded to whole samples. Their lengths
            // alternate even at a fixed tempo, so setting tempo from in/out
            // would modulate it every cell (and bias it at the rate clamp).
            touch_.setTempo(std::clamp(rate, kMinStretchRate, kMaxStretchRate));
            writeAll(input, in);
        }

        const auto ready = static_cast<int>(touch_.receiveSamples(
            deinterleaved_.data(), static_cast<unsigned int>(std::min(out, maxBlockSamples_))));

        for (std::size_t channel = 0; channel < output.getNumChannels(); ++channel) {
            auto* destination = output.getChannelPointer(channel);
            const auto source = std::min(static_cast<int>(channel), channels_ - 1);
            const float* interleaved = deinterleaved_.data() + source;

            for (auto sample = 0; sample < ready; ++sample)
                destination[sample] = interleaved[sample * channels_];

            // A pipe that has not caught up yet is silence rather than whatever
            // the scratch held. It happens on the blocks a locate is still being
            // absorbed over, and the voice hears it as not having sounded.
            juce::FloatVectorOperations::clear(destination + ready, out - ready);
        }
    }

  private:
    /// The most the front of the pipe can ever be holding, and the tempo where
    /// that is true.
    struct WorstCase {
        int input = 0;
        double atTempo = kMaxStretchRate;
    };

    /**
     * @brief What this has to have room for before a callback asks.
     *
     * The input side is what it keeps back between batches plus the largest
     * single push a callback can hand it. The sequence it keeps back moves with
     * the tempo, so the range is scanned for the longest one, and the largest
     * push is whichever is larger of a block's reading and a pre-roll: both are
     * bounded before the callback, the reading by the rate ceiling and the
     * pre-roll by the buffer it is read into.
     *
     * The scan is arithmetic rather than processing: setTempo recalculates the
     * lengths and getSetting reports them without a sample going through. What
     * comes back is the tempo to push at as well as the size, because pushing
     * where the sequence is longest is what settles it in one call.
     */
    WorstCase worstCase(const StretchSetup& setup) {
        auto sequence = 0;
        auto atTempo = kMaxStretchRate;

        for (auto step = 0; step <= kWarmUpSteps; ++step) {
            const auto through = static_cast<double>(step) / kWarmUpSteps;
            const auto rate = kMinStretchRate + (kMaxStretchRate - kMinStretchRate) * through;
            touch_.setTempo(rate);

            if (const auto asked = touch_.getSetting(SETTING_NOMINAL_INPUT_SEQUENCE);
                asked > sequence) {
                sequence = asked;
                atTempo = rate;
            }
        }

        touch_.setTempo(std::clamp(setup.nominalRate, kMinStretchRate, kMaxStretchRate));

        // The largest single push, which is a cell's reading or a pre-roll, and
        // neither of them is a block. Taking the block here is what left the
        // warm-up pushing eight times as much silence at 4096 as at 512, and
        // TDStretch::skipFract carried the difference past touch_.clear() into
        // the first splice of playback (#2078). Everything the warm-up settles
        // is settled by pushing what will actually be pushed later, so the
        // figure has to be the same one write() cuts its pieces to.
        const auto handed = std::max(
            stretchPushSamples(),
            static_cast<int>(std::ceil(preRollSamples(setup.nominalRate) * kPreRollHeadroom)));

        return WorstCase{sequence + handed, atTempo};
    }

    /// Take back everything ready, discarding it. Off the audio thread, in the
    /// warm-up, where what came out is silence anyway.
    void drainAll() {
        while (touch_.numSamples() > 0)
            touch_.receiveSamples(touch_.numSamples());
    }

    /// @p count samples of silence, in one call, so that the pipe grows to hold
    /// all of it rather than to whatever it had room for at the time.
    void pushSilence(int count) {
        std::vector<float> silence(static_cast<std::size_t>(count * channels_), 0.0f);
        touch_.putSamples(silence.data(), static_cast<unsigned int>(count));
    }

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
        // Where each pipe channel reads from, once rather than per sample. A
        // block narrower than the pipe repeats its last channel, which is what
        // the clamp inside the loop did.
        for (auto channel = 0; channel < channels_; ++channel) {
            const auto source =
                std::min(static_cast<std::size_t>(channel), block.getNumChannels() - 1);
            sources_[static_cast<std::size_t>(channel)] = block.getChannelPointer(source) + offset;
        }

        for (auto sample = 0; sample < count; ++sample)
            for (auto channel = 0; channel < channels_; ++channel)
                interleaved_[static_cast<std::size_t>(sample) * channels_ + channel] =
                    sources_[static_cast<std::size_t>(channel)][sample];

        touch_.putSamples(interleaved_.data(), static_cast<unsigned int>(count));
    }

    /// Tempos the range is scanned at for the longest sequence. Arithmetic
    /// rather than processing, so it can afford to be fine.
    static constexpr int kWarmUpSteps = 64;

    /// Where SoundTouch's automatic sequence and seek-window lengths stop moving
    /// with the tempo (AUTOSEQ_TEMPO_LOW and AUTOSEQ_TEMPO_TOP in TDStretch.cpp).
    /// Named here because the warm-up has to visit them: they are the corners of
    /// the piecewise-linear curve every buffer size behind the front of the pipe
    /// is derived from, and a corner is where a maximum can hide from a sieve
    /// that only looked at the ends.
    static constexpr double kAutoSequenceLow = 0.5;
    static constexpr double kAutoSequenceTop = 2.0;

    int channels_ = 2;
    int maxBlockSamples_ = 512;

    int readAhead_ = 0;
    int history_ = 0;

    std::vector<float> interleaved_;
    std::vector<float> deinterleaved_;
    std::vector<const float*> sources_;
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

            if (taken < kHistory) {
                auto* kept = history_.getWritePointer(channel);
                std::copy(kept + taken, kept + kHistory, kept);
            }

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
    stream.read(until - count, region, count, ReadPurpose::priming);

    return region;
}

std::unique_ptr<ClipStretcher> makeStretcher(const StretchSetup& setup) {
    if (setup.numChannels <= 0 || setup.maxBlockSamples <= 0 || !(setup.sampleRate > 0.0))
        return nullptr;

    // A clip that asks for nothing gets nothing, whatever its mode field says,
    // and that is a parity requirement rather than an economy. The mode is a
    // preference for how to stretch and not an instruction to stretch: the
    // incumbent engages an engine on auto tempo, auto pitch, a pitch change or a
    // ratio off unity, and never on the mode alone
    // (AudioClipBase::usesTimeStretchedProxy). A clip whose dropdown was touched
    // once and asks for nothing plays clean through the fork, and a phase
    // vocoder run at one-to-one is not clean: it re-synthesises what it was
    // given. Every project that ever opened that dropdown would otherwise be a
    // difference in the null-diff corpus (#2040).
    //
    // Auto tempo is the carve-out, and keeps its engine at a unity average. The
    // average is not what it plays at: its ratio is the project's tempo over the
    // file's own and moves with the tempo curve, so a clip averaging unity is
    // still stretching in both directions around it. The incumbent engages on
    // auto tempo alone for the same reason.
    if (!setup.followsTempo && !setup.speedRamp && std::abs(setup.semitones) < 0.001f &&
        std::abs(setup.nominalRate - 1.0) < 1.0e-9)
        return nullptr;

    switch (setup.mode) {
        case time_stretch_mode::kSignalsmith:
            return std::make_unique<SignalsmithClipStretcher>(setup);

        case time_stretch_mode::kSoundTouchNormal:
        case time_stretch_mode::kSoundTouchBetter:
            return std::make_unique<SoundTouchClipStretcher>(setup);

        case time_stretch_mode::kDisabled:
            // Nothing to preserve, so a rate change is a tape speed change. This
            // is the analog pitch path: the model has already put the pitch
            // factor into the speed ratio, and a ramp is a tape effect too.
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
