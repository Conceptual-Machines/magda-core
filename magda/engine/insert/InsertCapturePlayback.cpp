#include "insert/InsertCapturePlayback.hpp"

#include <algorithm>
#include <cmath>

#include "io/SourceReaders.hpp"

namespace magda::engine {

namespace {

/// @p capture's samples at @p rate, through the curve a file at another rate is
/// read through. The same buffer back when the rates already agree.
juce::AudioBuffer<float> atRate(const juce::AudioBuffer<float>& source, double sourceRate,
                                double rate) {
    if (sourceRate <= 0.0 || rate <= 0.0 || std::abs(sourceRate - rate) < 1e-9) {
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf(source);
        return copy;
    }

    const auto ratio = sourceRate / rate;
    const auto length = static_cast<int>(std::llround(source.getNumSamples() / ratio));

    juce::AudioBuffer<float> resampled(source.getNumChannels(), std::max(0, length));
    resampled.clear();

    const auto last = source.getNumSamples() - 1;

    for (auto channel = 0; channel < source.getNumChannels(); ++channel) {
        const auto* from = source.getReadPointer(channel);
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

    // A render reads every channel of its slot, so a capture of fewer is a
    // channel of silence nobody asked for.
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
    // Filled completely or cleared completely, like an audio source's: what the
    // seam promises is a block with no holes in it.
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

        // Rebuilt at the length it was sent at, since that is what says whether
        // the second byte is data or the next message.
        const auto message =
            event->numBytes >= 3
                ? juce::MidiMessage(event->status, event->data1, event->data2)
                : (event->numBytes == 2 ? juce::MidiMessage(event->status, event->data1)
                                        : juce::MidiMessage(event->status));

        midi.addEvent(message, static_cast<int>(offset));
    }
}

}  // namespace magda::engine
