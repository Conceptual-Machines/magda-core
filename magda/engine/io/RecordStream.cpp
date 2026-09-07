#include "io/RecordStream.hpp"

#include <algorithm>

namespace magda::engine {

namespace {

int roundedCapacity(int wanted, int floorSamples) {
    return juce::nextPowerOfTwo(std::max(floorSamples, wanted));
}

}  // namespace

RecordStream::RecordStream(RecordSink& sink, const RecordStreamSettings& settings)
    : sink_(sink),
      capacity_(roundedCapacity(settings.capacitySamples, 1024)),
      mask_(static_cast<std::uint64_t>(capacity_) - 1),
      chunkSamples_(std::max(1, settings.chunkSamples)) {
    if (settings.numChannels > 0)
        audio_.setSize(settings.numChannels, capacity_);

    midi_.resize(static_cast<std::size_t>(roundedCapacity(settings.midiCapacityEvents, 64)));
    midiMask_ = midi_.size() - 1;
}

bool RecordStream::writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples) {
    const auto channels = audio_.getNumChannels();
    if (numSamples <= 0 || channels == 0)
        return true;

    const auto write = audioWrite_.load(std::memory_order_relaxed);
    const auto read = audioRead_.load(std::memory_order_acquire);
    const auto free = static_cast<std::uint64_t>(capacity_) - (write - read);

    if (free < static_cast<std::uint64_t>(numSamples)) {
        samplesLost_.fetch_add(numSamples, std::memory_order_relaxed);
        return false;
    }

    const auto offset = static_cast<int>(write & mask_);
    const auto first = std::min(numSamples, capacity_ - offset);
    const auto rest = numSamples - first;
    const auto sourceChannels = static_cast<int>(audio.getNumChannels());

    for (auto channel = 0; channel < channels; ++channel) {
        // A stream given fewer channels than it holds writes silence into the
        // rest rather than leaving the last take's samples there.
        if (channel >= sourceChannels) {
            audio_.clear(channel, offset, first);
            if (rest > 0)
                audio_.clear(channel, 0, rest);
            continue;
        }

        const auto* source = audio.getChannelPointer(static_cast<std::size_t>(channel));
        audio_.copyFrom(channel, offset, source, first);
        if (rest > 0)
            audio_.copyFrom(channel, 0, source + first, rest);
    }

    audioWrite_.store(write + static_cast<std::uint64_t>(numSamples), std::memory_order_release);
    return true;
}

bool RecordStream::writeMidi(const juce::MidiBuffer& midi, std::int64_t blockStartSample) {
    auto kept = true;

    for (const auto metadata : midi) {
        if (metadata.numBytes > 3) {
            eventsLost_.fetch_add(1, std::memory_order_relaxed);
            kept = false;
            continue;
        }

        const auto write = midiWrite_.load(std::memory_order_relaxed);
        const auto read = midiRead_.load(std::memory_order_acquire);

        if (midi_.size() - (write - read) == 0) {
            eventsLost_.fetch_add(1, std::memory_order_relaxed);
            kept = false;
            continue;
        }

        auto& slot = midi_[static_cast<std::size_t>(write & midiMask_)];
        slot.sample = blockStartSample + metadata.samplePosition;
        slot.numBytes = static_cast<std::uint8_t>(metadata.numBytes);
        slot.status = metadata.data[0];
        slot.data1 = metadata.numBytes > 1 ? metadata.data[1] : 0;
        slot.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;

        midiWrite_.store(write + 1, std::memory_order_release);
    }

    return kept;
}

int RecordStream::contiguousAudioToDrain(std::uint64_t read, std::uint64_t available) const {
    const auto offset = static_cast<int>(read & mask_);
    const auto wanted =
        std::min<std::uint64_t>(available, static_cast<std::uint64_t>(chunkSamples_));
    return std::min(static_cast<int>(wanted), capacity_ - offset);
}

bool RecordStream::drain() {
    auto worked = false;

    if (audio_.getNumChannels() > 0) {
        const auto read = audioRead_.load(std::memory_order_relaxed);
        const auto write = audioWrite_.load(std::memory_order_acquire);

        if (const auto available = write - read; available > 0) {
            const auto take = contiguousAudioToDrain(read, available);
            const auto offset = static_cast<std::size_t>(read & mask_);
            const auto chunk = juce::dsp::AudioBlock<const float>(audio_).getSubBlock(
                offset, static_cast<std::size_t>(take));

            if (!failed_.load(std::memory_order_relaxed) && !sink_.writeAudio(chunk, take))
                failed_.store(true, std::memory_order_relaxed);

            audioRead_.store(read + static_cast<std::uint64_t>(take), std::memory_order_release);
            worked = true;
        }
    }

    const auto midiRead = midiRead_.load(std::memory_order_relaxed);
    const auto midiWrite = midiWrite_.load(std::memory_order_acquire);

    if (const auto available = midiWrite - midiRead; available > 0) {
        const auto offset = static_cast<std::size_t>(midiRead & midiMask_);
        const auto take = std::min<std::size_t>(available, midi_.size() - offset);

        if (!failed_.load(std::memory_order_relaxed) &&
            !sink_.writeMidi(std::span<const RecordedMidiEvent>(midi_.data() + offset, take)))
            failed_.store(true, std::memory_order_relaxed);

        midiRead_.store(midiRead + take, std::memory_order_release);
        worked = true;
    }

    return worked;
}

void RecordStream::finish() {
    while (drain()) {
    }

    sink_.finish();
}

}  // namespace magda::engine
