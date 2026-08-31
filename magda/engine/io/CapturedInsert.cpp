#include "io/CapturedInsert.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// Encoded MIDI a capture may hold, in bytes.
///
/// Reserved at prepare because adding to the buffer happens on the audio
/// thread. Generous rather than derived: what a MIDI-returning insert answers
/// with over a render is a fact about the outboard, and the cost of the ceiling
/// is a fraction of what one second of the audio beside it costs.
constexpr int kCapturedMidiBytes = 1 << 20;

/// What a MidiBuffer spends on an event besides its bytes: the sample position
/// and the length. Counted into the ceiling so the check is about what will be
/// stored rather than about what was handed over.
constexpr int kMidiEventOverhead = 6;

}  // namespace

CapturedInsert::CapturedInsert(Mode mode, int numChannels, EngineInsert* live)
    : mode_(mode), numChannels_(std::max(1, numChannels)), live_(live) {}

std::int64_t CapturedInsert::captureSampleOf(const BlockInfo& block) const {
    // The capture's own rate, never the render's. Seconds are what both domains
    // share, so this lands on the moment the block is at whatever rate the
    // graph asking is running.
    return static_cast<std::int64_t>(std::llround(block.startSeconds * captureRate_));
}

void CapturedInsert::prepare(const RenderContext& context) {
    if (live_ != nullptr)
        live_->prepare(context);

    // The graph running now, whichever mode this is in.
    renderRate_ = context.sampleRate;

    // Only while capturing. A bounce is handed a capture that already exists
    // and must not clear it, which is the one way this class could quietly turn
    // a render into silence.
    if (mode_ != Mode::Capturing) {
        // And the one thing a replay has to check. Every stored position is in
        // capture-rate samples and the audio behind them is capture-rate audio;
        // positions convert through seconds and the round trip is held in them,
        // but the samples themselves would need a resampler. Refused rather
        // than approximated: played one for one a 44.1 kHz capture comes out of
        // a 48 kHz render 8.8 per cent slow and a tone and a half flat.
        rateMismatch_ = renderRate_ != captureRate_;
        return;
    }

    // The rate this recording is being made at, and the rate every position
    // stored below is in. Set here and nowhere else.
    captureRate_ = context.sampleRate;
    rateMismatch_ = false;
    midiOverflowed_ = false;

    // Remembered now rather than asked for later, and in seconds rather than
    // samples: by the time a bounce replays this the live insert may be gone,
    // and a count would mean a different duration in a render at another rate
    // (see the header).
    const auto latency = live_ != nullptr ? live_->latencySamples() : 0;
    roundTripSeconds_ = captureRate_ > 0.0 ? static_cast<double>(latency) / captureRate_ : 0.0;

    const auto length = std::max<std::int64_t>(0, window_.numSamples);
    samples_.assign(static_cast<std::size_t>(numChannels_),
                    std::vector<float>(static_cast<std::size_t>(length), 0.0f));

    written_.assign(static_cast<std::size_t>(length), 0);
    captured_.store(0, std::memory_order_relaxed);

    midi_.clear();
    midi_.ensureSize(static_cast<std::size_t>(kCapturedMidiBytes));
}

void CapturedInsert::reset() {
    if (live_ != nullptr)
        live_->reset();
}

int CapturedInsert::latencySamples() const {
    // The same instant in both modes, which is not the same number. A capture
    // holds what the hardware answered rather than what was sent to it, so a
    // replay declaring no latency would leave every parallel path undelayed
    // against a return that is still a round trip behind; and a replay
    // declaring the count it was captured as would delay them by that many of
    // the render's samples, which is a different duration whenever the rates
    // differ.
    if (mode_ == Mode::Capturing)
        return live_ != nullptr ? live_->latencySamples() : 0;

    return static_cast<int>(std::llround(roundTripSeconds_ * renderRate_));
}

void CapturedInsert::markCaptured(std::int64_t first, std::int64_t count) {
    const auto offset = first - window_.firstSample;

    std::int64_t added = 0;
    for (std::int64_t sample = 0; sample < count; ++sample) {
        const auto index = static_cast<std::size_t>(offset + sample);
        if (index >= written_.size())
            break;

        // Counted on the transition only, so a second pass over the same
        // stretch does not report more captured than the window holds.
        if (written_[index] == 0) {
            written_[index] = 1;
            ++added;
        }
    }

    if (added > 0)
        captured_.fetch_add(added, std::memory_order_relaxed);
}

bool CapturedInsert::covers(std::int64_t first, std::int64_t count) const {
    // A capture that cannot be replayed covers nothing, whatever is in it. This
    // is the preflight a bounce asks, and it is the place a rate mismatch or a
    // MIDI overflow has to stop one: both produce a render that is wrong in a
    // way nothing downstream can see.
    if (!usable())
        return false;

    if (count <= 0)
        return true;
    if (!window_.contains(first, count))
        return false;

    const auto offset = static_cast<std::size_t>(first - window_.firstSample);
    for (std::size_t sample = 0; sample < static_cast<std::size_t>(count); ++sample) {
        const auto index = offset + sample;
        if (index >= written_.size() || written_[index] == 0)
            return false;
    }

    return true;
}

void CapturedInsert::send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
                          const juce::MidiBuffer& midi) {
    // A bounce sends nothing. There is nothing on the other end that could
    // answer in time, and a send that went out anyway would put the render's
    // audio through somebody's monitors at whatever speed the render is going.
    if (mode_ == Mode::Playing || live_ == nullptr)
        return;

    live_->send(block, audio, midi);
}

void CapturedInsert::receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                             juce::MidiBuffer& midi) {
    // The block's own length rather than the audio's. A MIDI-returning insert
    // is handed an empty audio block, and a span taken from that would be zero:
    // nothing would be recorded as captured, and the bounce would replay a
    // capture it believed was empty.
    // The block's own length rather than the audio's, and in capture-rate
    // samples: a block is a duration, and the two rates count it differently.
    const auto seconds =
        static_cast<double>(block.numSamples) / (renderRate_ > 0.0 ? renderRate_ : 1.0);
    const auto numSamples = static_cast<std::int64_t>(std::llround(seconds * captureRate_));
    const auto channels = static_cast<int>(audio.getNumChannels());
    const auto first = captureSampleOf(block);

    if (mode_ == Mode::Capturing) {
        if (live_ != nullptr) {
            live_->receive(block, audio, midi);
        } else {
            audio.clear();
            midi.clear();
        }

        // Clipped to the window rather than refused outside it: a live pass
        // runs over the whole arrangement and the capture is one stretch of it.
        const auto from = std::max(first, window_.firstSample);
        const auto to = std::min(first + numSamples, window_.firstSample + window_.numSamples);
        if (to <= from)
            return;

        const auto within = to - from;
        const auto intoStore = from - window_.firstSample;
        const auto fromBlock = from - first;

        for (int channel = 0; channel < channels && channel < numChannels_; ++channel) {
            auto& written = samples_[static_cast<std::size_t>(channel)];
            for (std::int64_t sample = 0; sample < within; ++sample)
                written[static_cast<std::size_t>(intoStore + sample)] =
                    audio.getSample(channel, static_cast<int>(fromBlock + sample));
        }

        // The MIDI too, at absolute positions, for the same reason the audio is
        // written at its own: a MIDI-returning insert feeding an instrument
        // downstream is silent in the bounce if this is dropped.
        for (const auto metadata : midi) {
            const auto at = first + metadata.samplePosition;
            if (at < from || at >= to)
                continue;

            // Bounded rather than merely reserved. ensureSize is a reservation
            // and addEvent grows past it, which on this thread is an
            // allocation; and a capture that allocated its way out of the
            // problem would still be one with events missing from it, reporting
            // success. So the ceiling is checked and the capture is marked
            // unusable instead.
            if (static_cast<int>(midi_.data.size()) + metadata.numBytes + kMidiEventOverhead >
                kCapturedMidiBytes) {
                midiOverflowed_ = true;
                break;
            }

            midi_.addEvent(metadata.data, metadata.numBytes, static_cast<int>(at));
        }

        markCaptured(from, within);
        return;
    }

    // Playing. Silence and no events outside what was captured: a bounce of a
    // range nobody played through the hardware has no answer, and the nearest
    // thing captured is not it.
    audio.clear();
    midi.clear();

    const auto from = std::max(first, window_.firstSample);
    const auto to = std::min(first + numSamples, window_.firstSample + window_.numSamples);
    if (to <= from)
        return;

    const auto within = to - from;
    const auto fromStore = from - window_.firstSample;
    const auto intoBlock = from - first;

    for (int channel = 0; channel < channels; ++channel) {
        if (channel >= static_cast<int>(samples_.size()))
            break;

        const auto& stored = samples_[static_cast<std::size_t>(channel)];
        for (std::int64_t sample = 0; sample < within; ++sample)
            audio.setSample(channel, static_cast<int>(intoBlock + sample),
                            stored[static_cast<std::size_t>(fromStore + sample)]);
    }

    // Rebased onto the block, which is where a consumer expects an event to be.
    for (const auto metadata : midi_) {
        const auto at = static_cast<std::int64_t>(metadata.samplePosition);
        if (at >= from && at < to)
            midi.addEvent(metadata.data, metadata.numBytes, static_cast<int>(at - first));
    }
}

}  // namespace magda::engine
