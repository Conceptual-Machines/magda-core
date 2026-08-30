#include "io/PrefetchStream.hpp"

#include <algorithm>
#include <limits>
#include <utility>

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

void PrefetchStream::startAt(std::int64_t sourceStart) {
    // Both cursors, and neither generation. Nothing has been read for the
    // position this leaves behind, so there is nothing to invalidate; leaving
    // the two sides on the generation they were born with is exactly what keeps
    // the first fill from being read for one position and dropped for another.
    nextSample_ = sourceStart;
    fillPosition_ = sourceStart;
}

void PrefetchStream::seek(std::int64_t sourceStart) {
    // Publish and return. Everything the callback owns is left alone, which is
    // what makes this safe to call while the stream is playing rather than
    // safe only if nobody forgets it is not.
    cue_.nonRealtimeReplace(SeekRequest{sourceStart, ++cueGeneration_});
}

void PrefetchStream::applyPendingCue() {
    // Whether anything played out of this stream since the last time it was
    // asked. Read here rather than in read(), so that what it answers is a
    // question about the block that just went by.
    const auto wasSounding = std::exchange(readSinceCueCheck_, false);

    farbot::RealtimeObject<SeekRequest, farbot::RealtimeObjectOptions::nonRealtimeMutatable>::
        ScopedAccess<farbot::ThreadType::realtime>
            cue(cue_);

    if (cue->generation == appliedCue_)
        return;

    // A stream that is sounding keeps sounding. Taking the cue up would abandon
    // the generation the material still playing was read for and hand its pool
    // back, so the cue would cost a gap and then be overridden by the very next
    // read anyway, which asks for where playback actually is. It waits instead,
    // and is taken up when the stream falls silent.
    if (wasSounding)
        return;

    appliedCue_ = cue->generation;
    requestSeek(cue->sourceStart);
}

void PrefetchStream::requestSeek(std::int64_t sourceStart) {
    nextSample_ = sourceStart;
    ++generation_;

    // Whatever the callback was reading came from somewhere else now.
    releaseCurrent();

    // And so is everything waiting behind it. Handing the pool back here rather
    // than as the callback works through it is what lets the reader start
    // refilling immediately: a stream that was cued but is not being read yet
    // is the whole point of cueing, and it has no callback coming to recycle
    // chunks for it. Every chunk in the fifo was read for the generation this
    // call just left behind, so all of them go.
    Chunk* stale = nullptr;
    while (filled_.pop(stale)) {
        const auto returned = spent_.push(std::move(stale));
        jassert(returned);
        juce::ignoreUnused(returned);
    }

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

    destination.clear();

    // Nothing in this request can play: it is past the last sample of the
    // reading. Not an underrun, and not a seek either. A stream whose clip is
    // still placed but whose material has run out has not gone anywhere, and
    // moving the cursor for it would throw away a cue meant for wherever it
    // goes next, which is exactly when cueing it is useful.
    //
    // Before the first sample is the opposite case and is read rather than
    // skipped. That silence belongs to a stream on its way into its own
    // material: a reversed clip trimmed longer than its source begins with it
    // (io/SourceReaders.hpp), and a stream that stood still through it would
    // arrive at the material with its read-ahead pointed somewhere else, seek,
    // and cost a block of silence at the one moment the clip has to be exact.
    if (sourceStart >= length_)
        return 0;

    // Reading is what sounding means, and this is a read that could play
    // something: one that comes back short because the reader is behind still
    // counts, because that stream is playing and merely starved. Taking a cue
    // up is not done here: it happens once at the top of a block, through
    // applyPendingCue, and doing it again inside the read would see a stream
    // that has not reached its read *yet this block* and mistake it for one
    // that is not playing at all.
    readSinceCueCheck_ = true;

    if (sourceStart != nextSample_)
        requestSeek(sourceStart);

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
        // How much of the stream is left, saturating rather than wrapping.
        //
        // The subtraction on its own overflows, and the way it overflows is
        // silent and unsafe. A looping reader has no last sample, so its
        // length is int64's maximum (SourceReaders.cpp), and a position can be
        // negative: an event anchored before its own loop start is anchored at
        // a phase within it, and a reversed clip trimmed longer than its source
        // begins ahead of the first sample. Subtracting a negative position
        // from that maximum wraps to a large negative, whose low thirty-two
        // bits truncate back to a positive count larger than a chunk -- 8414
        // samples into a chunk of 4096, which is a read past the end of the
        // buffer rather than a wrong number. A real project found it: a loop
        // whose region starts after the clip does.
        //
        // Branched rather than written as one expression, because the guard has
        // to come before the arithmetic it is guarding and not beside it. The
        // saturating form needs int64's maximum plus the position, and that sum
        // is only representable while the position is negative: computing it
        // first and testing the sign afterwards is the same overflow again,
        // moved.
        const auto remaining = [this]() -> std::int64_t {
            constexpr auto unbounded = std::numeric_limits<std::int64_t>::max();

            if (fillPosition_ >= 0)
                return length_ - fillPosition_;  // both non-negative, so no wrap

            // Safe here and only here: the position is negative, so the sum is
            // below the maximum rather than past it.
            const auto headroom = unbounded + fillPosition_;
            return length_ > headroom ? unbounded : length_ - fillPosition_;
        }();

        const auto count = static_cast<int>(std::min<std::int64_t>(chunkSamples_, remaining));

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
