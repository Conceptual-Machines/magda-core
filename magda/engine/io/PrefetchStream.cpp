#include "io/PrefetchStream.hpp"

#include <algorithm>

namespace magda::engine {

PrefetchStream::PrefetchStream(std::unique_ptr<AudioFileReader> reader,
                               const RenderContext& context, const PrefetchSettings& settings)
    : reader_(std::move(reader)),
      chunkSamples_(std::max(1, settings.chunkSamples)),
      // Room for every chunk at once, rounded up to what the fifo insists on:
      // one sized to the pool exactly could not hold all of it, and farbot
      // indexes by mask rather than by modulo.
      filled_(juce::nextPowerOfTwo(std::max(2, settings.chunkCount) + 1)),
      spent_(juce::nextPowerOfTwo(std::max(2, settings.chunkCount) + 1)) {
    if (reader_ != nullptr) {
        length_ = reader_->lengthInSamples();
        sourceSampleRate_ = reader_->sampleRate();
    }

    numChannels_ = std::max(1, context.numChannels);

    // A chunk has to be able to answer a whole callback, otherwise a block
    // longer than a chunk would underrun however far ahead the reader got.
    chunkSamples_ = std::max(chunkSamples_, context.maxBlockSize);

    pool_.reserve(static_cast<std::size_t>(std::max(2, settings.chunkCount)));
    for (auto i = 0; i < std::max(2, settings.chunkCount); ++i) {
        auto chunk = std::make_unique<Chunk>();
        chunk->audio.setSize(numChannels_, chunkSamples_);
        chunk->audio.clear();
        spent_.push(chunk.get());
        pool_.push_back(std::move(chunk));
    }
}

void PrefetchStream::seek(std::int64_t sourceStart) {
    requestSeek(sourceStart);
}

void PrefetchStream::requestSeek(std::int64_t sourceStart) {
    nextSample_ = sourceStart;
    ++generation_;

    // Whatever the callback was reading came from somewhere else now.
    releaseCurrent();

    farbot::RealtimeObject<SeekRequest, farbot::RealtimeObjectOptions::realtimeMutatable>::
        ScopedAccess<farbot::ThreadType::realtime>
            request(request_);
    request->sourceStart = sourceStart;
    request->generation = generation_;
}

void PrefetchStream::releaseCurrent() {
    if (current_ == nullptr)
        return;

    const auto pushed = spent_.push(std::move(current_));
    jassert(pushed);
    juce::ignoreUnused(pushed);

    current_ = nullptr;
    currentOffset_ = 0;
}

bool PrefetchStream::takeNextChunk() {
    Chunk* chunk = nullptr;

    while (filled_.pop(chunk)) {
        // Read for a position that has since been abandoned, or for one the
        // callback has already gone past. Either way it is not what is being
        // asked for, and the fifo behind it might be.
        if (chunk->generation != generation_ ||
            chunk->startSample + chunk->numSamples <= nextSample_) {
            spent_.push(std::move(chunk));
            continue;
        }

        // The reader is ahead of where the callback is asking. Nothing in the
        // fifo can fill that gap, so it stays where it is and the callback
        // takes the silence.
        if (chunk->startSample > nextSample_) {
            spent_.push(std::move(chunk));
            return false;
        }

        current_ = chunk;
        currentOffset_ = static_cast<int>(nextSample_ - chunk->startSample);
        return true;
    }

    return false;
}

int PrefetchStream::read(std::int64_t sourceStart, juce::dsp::AudioBlock<float> destination,
                         int numSamples) {
    if (numSamples <= 0)
        return 0;

    if (sourceStart != nextSample_)
        requestSeek(sourceStart);

    destination.clear();

    auto done = 0;

    while (done < numSamples) {
        if (current_ == nullptr && !takeNextChunk())
            break;

        const auto available = current_->numSamples - currentOffset_;
        const auto count = std::min(available, numSamples - done);

        for (std::size_t channel = 0; channel < destination.getNumChannels(); ++channel) {
            // A chunk carries the engine's channels rather than the file's, so
            // this is a straight copy; the modulo is for a destination wider
            // than the engine renders, which nothing asks for today and which
            // should repeat rather than fall silent if anything ever does.
            const auto source = static_cast<int>(channel) % current_->audio.getNumChannels();
            juce::FloatVectorOperations::copy(
                destination.getChannelPointer(channel) + done,
                current_->audio.getReadPointer(source, currentOffset_), count);
        }

        currentOffset_ += count;
        nextSample_ += count;
        done += count;

        if (currentOffset_ >= current_->numSamples)
            releaseCurrent();
    }

    if (done < numSamples) {
        // Past the end of the file nothing is late: there is no sample there to
        // be waiting for. Anywhere else, the reader did not keep up, and the
        // callback has just played silence that is not in the material.
        const auto wanted = std::min<std::int64_t>(sourceStart + numSamples, length_);
        if (sourceStart + done < wanted)
            underruns_.fetch_add(1, std::memory_order_relaxed);

        // The cursor moves whether or not the samples arrived. A stream that
        // resumed where it ran dry would play the missing audio late and stay
        // late for the rest of the take; the gap is the honest cost.
        nextSample_ = sourceStart + numSamples;
    }

    return done;
}

bool PrefetchStream::fill() {
    if (reader_ == nullptr)
        return false;

    {
        farbot::RealtimeObject<SeekRequest, farbot::RealtimeObjectOptions::realtimeMutatable>::
            ScopedAccess<farbot::ThreadType::nonRealtime>
                request(request_);

        if (request->generation != fillGeneration_) {
            fillGeneration_ = request->generation;
            fillPosition_ = request->sourceStart;
            stalled_ = false;
        }
    }

    if (stalled_)
        return false;

    auto worked = false;
    Chunk* chunk = nullptr;

    while (fillPosition_ < length_ && spent_.pop(chunk)) {
        const auto count =
            static_cast<int>(std::min<std::int64_t>(chunkSamples_, length_ - fillPosition_));

        chunk->startSample = fillPosition_;
        chunk->numSamples = reader_->read(chunk->audio, 0, fillPosition_, count);
        chunk->generation = fillGeneration_;

        fillPosition_ += chunk->numSamples;
        worked = true;

        // A reader that gives nothing at a position inside its own file has
        // stopped being able to answer: a file truncated under us, a device
        // that went away. Asking again would spin the thread at full tilt
        // against something that is not going to improve, so the stream stops
        // reading until a seek gives it somewhere else to try.
        stalled_ = chunk->numSamples <= 0;

        const auto pushed = filled_.push(std::move(chunk));
        jassert(pushed);
        juce::ignoreUnused(pushed);

        if (stalled_)
            break;
    }

    return worked;
}

}  // namespace magda::engine
