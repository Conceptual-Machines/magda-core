#include "insert/InsertCapturePlayback.hpp"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "io/SourceReaders.hpp"

namespace magda::engine {

namespace {

/**
 * @brief @p source with everything above @p cutoff taken out.
 *
 * What a rate reduction needs and interpolation does not do: dropping samples
 * folds whatever sat above the new Nyquist back down into the audible band, and
 * an outboard return carries plenty up there. Linear phase, so the delay it
 * adds is exactly half its length and comes straight back off.
 *
 * 102 taps at the design below, run once over the capture rather than per
 * block, and only when the export asks for a lower rate than the pass had.
 */
juce::AudioBuffer<float> bandLimited(const juce::AudioBuffer<float>& source, double sourceRate,
                                     double cutoff) {
    const auto coefficients = juce::dsp::FilterDesign<float>::designFIRLowpassKaiserMethod(
        static_cast<float>(cutoff), sourceRate, 0.05f, -80.0f);

    const auto* taps = coefficients->getRawCoefficients();
    const auto length = static_cast<int>(coefficients->getFilterOrder() + 1);
    const auto delay = length / 2;
    const auto last = source.getNumSamples() - 1;

    juce::AudioBuffer<float> filtered(source.getNumChannels(), source.getNumSamples());

    for (auto channel = 0; channel < source.getNumChannels(); ++channel) {
        const auto* from = source.getReadPointer(channel);
        auto* to = filtered.getWritePointer(channel);

        for (auto at = 0; at < source.getNumSamples(); ++at) {
            auto sum = 0.0;

            for (auto tap = 0; tap < length; ++tap)
                sum +=
                    static_cast<double>(taps[tap]) *
                    static_cast<double>(from[std::clamp(at + delay - tap, 0, std::max(0, last))]);

            to[at] = static_cast<float>(sum);
        }
    }

    return filtered;
}

/**
 * @brief @p source at @p rate, through the curve io/SourceReaders.hpp reads a
 *        file at another rate through.
 *
 * A copy when the rates already agree. Band-limited first when the rate is
 * going down, since the curve interpolates and does not filter.
 */
juce::AudioBuffer<float> atRate(const juce::AudioBuffer<float>& source, double sourceRate,
                                double rate) {
    if (sourceRate <= 0.0 || rate <= 0.0 || std::abs(sourceRate - rate) < 1e-9) {
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf(source);
        return copy;
    }

    const auto ratio = sourceRate / rate;
    const auto length = static_cast<int>(std::llround(source.getNumSamples() / ratio));

    // Below the target's Nyquist with room for the filter to come down in.
    const auto limited = rate < sourceRate ? bandLimited(source, sourceRate, 0.45 * rate)
                                           : juce::AudioBuffer<float>();
    const auto& material = rate < sourceRate ? limited : source;

    juce::AudioBuffer<float> resampled(material.getNumChannels(), std::max(0, length));
    resampled.clear();

    const auto last = material.getNumSamples() - 1;

    for (auto channel = 0; channel < material.getNumChannels(); ++channel) {
        const auto* from = material.getReadPointer(channel);
        auto* to = resampled.getWritePointer(channel);

        for (auto at = 0; at < resampled.getNumSamples(); ++at) {
            const auto position = at * ratio;
            const auto index = static_cast<int>(std::floor(position));
            const auto fraction = position - index;

            const auto sampleAt = [&](int offset) {
                return from[std::clamp(index + offset, 0, std::max(0, last))];
            };

            to[at] = cubicLagrange(sampleAt(-1), sampleAt(0), sampleAt(1), sampleAt(2), fraction);
        }
    }

    return resampled;
}

}  // namespace

std::unique_ptr<InsertCapturePlayback> InsertCapturePlayback::create(const InsertCapture& capture,
                                                                     const CaptureWindow& window,
                                                                     const RenderContext& context) {
    if (!capture.complete() || capture.lengthInSamples() <= 0 || capture.sampleRate() <= 0.0)
        return nullptr;

    if (!capture.window().covers(window))
        return nullptr;

    // A render reads every channel of its slot.
    if (capture.numChannels() < context.numChannels)
        return nullptr;

    auto audio = atRate(capture.audio(), capture.sampleRate(), context.sampleRate);

    std::vector<CapturedMidiEvent> midi;
    midi.reserve(capture.midi().size());
    for (const auto& event : capture.midi()) {
        auto moved = event;
        moved.sample = std::llround(static_cast<double>(event.sample) / capture.sampleRate() *
                                    context.sampleRate);
        midi.push_back(moved);
    }

    const auto latency =
        static_cast<int>(std::llround(capture.roundTripSeconds() * context.sampleRate));

    return std::unique_ptr<InsertCapturePlayback>(new InsertCapturePlayback(
        std::move(audio), std::move(midi), capture.window(), context.sampleRate, latency));
}

InsertCapturePlayback::InsertCapturePlayback(juce::AudioBuffer<float> audio,
                                             std::vector<CapturedMidiEvent> midi,
                                             CaptureWindow window, double sampleRate,
                                             int latencySamples)
    : audio_(std::move(audio)),
      midi_(std::move(midi)),
      window_(window),
      sampleRate_(sampleRate),
      latencySamples_(latencySamples) {}

void InsertCapturePlayback::receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                                    juce::MidiBuffer& midi) {
    // Filled or cleared completely, as the seam promises.
    audio.clear();

    if (!block.playing)
        return;

    const auto blockStart =
        std::llround((block.seconds.start - window_.startSeconds) * sampleRate_);
    const auto length = static_cast<std::int64_t>(audio_.getNumSamples());

    const auto from = std::max<std::int64_t>(blockStart, 0);
    const auto to = std::min<std::int64_t>(blockStart + block.numSamples, length);

    if (to > from) {
        const auto channels =
            std::min(static_cast<int>(audio.getNumChannels()), audio_.getNumChannels());
        const auto count = static_cast<std::size_t>(to - from);
        const auto destinationOffset = static_cast<std::size_t>(from - blockStart);

        for (auto channel = 0; channel < channels; ++channel) {
            const auto* source = audio_.getReadPointer(channel, static_cast<int>(from));
            auto* destination =
                audio.getChannelPointer(static_cast<std::size_t>(channel)) + destinationOffset;
            std::copy_n(source, count, destination);
        }
    }

    const auto first = std::lower_bound(
        midi_.begin(), midi_.end(), blockStart,
        [](const CapturedMidiEvent& event, std::int64_t sample) { return event.sample < sample; });

    for (auto event = first; event != midi_.end(); ++event) {
        const auto offset = event->sample - blockStart;
        if (offset >= block.numSamples)
            break;

        // At the length it was sent at: that is what says whether the second
        // byte is data or the next message.
        const auto message =
            event->numBytes >= 3
                ? juce::MidiMessage(event->status, event->data1, event->data2)
                : (event->numBytes == 2 ? juce::MidiMessage(event->status, event->data1)
                                        : juce::MidiMessage(event->status));

        midi.addEvent(message, static_cast<int>(offset));
    }
}

}  // namespace magda::engine
