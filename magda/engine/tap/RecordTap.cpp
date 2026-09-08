#include "tap/RecordTap.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

void RecordTap::open(double startBeat) {
    // Outside the window below: the table is the writer's own, and filling it
    // there would put two thousand stores between a reader and its retry.
    held_.fill(kNotHeld);
    heldSlots_.clear();
    ++pass_;

    seq_.fetch_add(kOpening, std::memory_order_release);

    startBeat_.store(startBeat, std::memory_order_relaxed);
    lengthBeats_.store(0.0, std::memory_order_relaxed);
    numNotes_.store(0, std::memory_order_relaxed);
    notesLost_.store(0, std::memory_order_relaxed);
    peaksNeeded_.store(0, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_relaxed);

    seq_.fetch_add(kOpening, std::memory_order_release);
}

void RecordTap::extend(double lengthBeats) {
    lengthBeats_.store(lengthBeats, std::memory_order_relaxed);

    for (const auto slot : heldSlots_) {
        auto& note = notes_[slot];
        const auto start = note.startBeat.load(std::memory_order_relaxed);
        note.lengthBeats.store(std::max(0.0, lengthBeats - start), std::memory_order_relaxed);
    }
}

void RecordTap::writeSlot(std::uint32_t at, int note, int velocity, double beat) {
    auto& slot = notes_[at];

    // Odd while the slot changes hands, even once it stands. The count is what
    // a reader compares, because two strikes of one pitch at one velocity are
    // the same identity and comparing those would accept one strike's beat
    // beside the next one's length.
    slot.revision.fetch_add(1, std::memory_order_release);

    slot.identity.store(packIdentity(pass_, note, velocity), std::memory_order_relaxed);
    slot.startBeat.store(beat, std::memory_order_relaxed);
    slot.lengthBeats.store(0.0, std::memory_order_relaxed);

    slot.revision.fetch_add(1, std::memory_order_release);
}

void RecordTap::noteOn(int channel, int note, int velocity, double beat) {
    // A tap nobody draws holds no notes and has lost none: what was never asked
    // for is not a shortfall.
    if (notes_.empty())
        return;

    const auto index = heldIndex(channel, note);

    // A second note on for a pitch already down replaces the first, which is
    // what PassWalk::add does when it finishes the take. Appending instead
    // would leave a note nothing can close, growing to the end of the pass.
    if (const auto held = held_[index]; held != kNotHeld) {
        writeSlot(held, note, velocity, beat);
        return;
    }

    const auto at = numNotes_.load(std::memory_order_relaxed);
    if (at >= notes_.size()) {
        notesLost_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    writeSlot(static_cast<std::uint32_t>(at), note, velocity, beat);

    // The count is what publishes the slot, so it is stored last.
    numNotes_.store(at + 1, std::memory_order_release);

    held_[index] = static_cast<std::uint32_t>(at);
    heldSlots_.push_back(static_cast<std::uint32_t>(at));
}

void RecordTap::noteOff(int channel, int note, double beat) {
    const auto index = heldIndex(channel, note);
    const auto slot = held_[index];
    if (slot == kNotHeld)
        return;

    held_[index] = kNotHeld;
    std::erase(heldSlots_, slot);

    auto& held = notes_[slot];
    const auto start = held.startBeat.load(std::memory_order_relaxed);
    held.lengthBeats.store(std::max(0.0, beat - start), std::memory_order_relaxed);
}

void RecordTap::addPeaks(juce::dsp::AudioBlock<const float> audio, int numSamples,
                         std::int64_t passSample) {
    if (peaks_.empty() || numSamples <= 0 || audio.getNumChannels() == 0 ||
        settings_.samplesPerPeak <= 0)
        return;

    const auto perPeak = static_cast<std::int64_t>(settings_.samplesPerPeak);
    const auto capacity = static_cast<std::int64_t>(peaks_.size());

    for (auto offset = 0; offset < numSamples;) {
        const auto at = passSample + offset;
        const auto slot = at / perPeak;

        // Whatever is left of this tick, or of the block, whichever ends first.
        const auto chunk = static_cast<int>(
            std::min<std::int64_t>(numSamples - offset, ((slot + 1) * perPeak) - at));

        const auto reached = peaksNeeded_.load(std::memory_order_relaxed);

        if (slot < capacity)
            writePeak(static_cast<std::size_t>(slot), audio, offset, chunk, slot >= reached);

        if (slot >= reached)
            peaksNeeded_.store(slot + 1, std::memory_order_release);

        offset += chunk;
    }
}

void RecordTap::writePeak(std::size_t slot, juce::dsp::AudioBlock<const float> audio, int offset,
                          int numSamples, bool first) {
    const auto channels = std::min<std::size_t>(audio.getNumChannels(), 2);

    std::array<float, 2> peak{};
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto range = juce::FloatVectorOperations::findMinAndMax(
            audio.getChannelPointer(channel) + offset, numSamples);
        peak[channel] = std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
    }

    // A mono take reads as the same level on both, which is what it sounds
    // like, the rule LevelTap keeps for the same case.
    if (channels == 1)
        peak[1] = peak[0];

    if (!first) {
        const auto held = unpackPeak(peaks_[slot].load(std::memory_order_relaxed));
        peak[0] = std::max(held.left, peak[0]);
        peak[1] = std::max(held.right, peak[1]);
    }

    peaks_[slot].store(packPeak(peak[0], peak[1]), std::memory_order_relaxed);
}

void RecordTap::read(Reading& into) const {
    into.material = material_;
    into.target = settings_.target;
    into.scene = settings_.scene;

    std::uint32_t seq = 0;
    std::size_t numNotes = 0;
    std::int64_t needed = 0;

    for (auto attempt = 0; attempt < kReadAttempts; ++attempt) {
        seq = seq_.load(std::memory_order_acquire);

        into.recording = recording_.load(std::memory_order_relaxed);
        into.startBeat = startBeat_.load(std::memory_order_relaxed);
        into.lengthBeats = lengthBeats_.load(std::memory_order_relaxed);
        into.notesLost = notesLost_.load(std::memory_order_relaxed);

        numNotes = numNotes_.load(std::memory_order_acquire);
        needed = peaksNeeded_.load(std::memory_order_acquire);

        // An odd sequence is a reset in flight, and the reading above is half
        // of one pass and half of the next whatever it compares to.
        if ((seq & kOpening) == 0U && seq_.load(std::memory_order_acquire) == seq)
            break;
    }

    into.pass = seq / 2;

    // Only the notes this pass recorded. A wrap partway through leaves the
    // slots holding the next pass's, which are not this reading's to draw.
    into.notes.clear();
    for (std::size_t at = 0; at < numNotes; ++at) {
        const auto& slot = notes_[at];

        const auto revision = slot.revision.load(std::memory_order_acquire);

        // Mid-replacement. Skipped rather than ending the list, because a
        // retrigger rewrites a slot the pass has already moved past.
        if ((revision & 1U) != 0U)
            continue;

        const auto identity = slot.identity.load(std::memory_order_relaxed);

        // Past this pass's own notes: what follows was written by the next.
        if (static_cast<std::uint32_t>(identity >> 32U) != into.pass)
            break;

        const auto startBeat = slot.startBeat.load(std::memory_order_relaxed);
        const auto lengthBeats = slot.lengthBeats.load(std::memory_order_relaxed);

        // The slot could have changed hands under the three loads above. A
        // length that grew is the same note being held and turns nothing.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.revision.load(std::memory_order_relaxed) != revision)
            continue;

        into.notes.push_back({static_cast<int>(identity & 0x7fU),
                              static_cast<int>((identity >> 8U) & 0x7fU), startBeat, lengthBeats});
    }

    const auto numPeaks = static_cast<std::size_t>(std::min<std::int64_t>(needed, peaks_.size()));
    into.peaks.resize(numPeaks);
    for (std::size_t at = 0; at < numPeaks; ++at)
        into.peaks[at] = unpackPeak(peaks_[at].load(std::memory_order_relaxed));

    into.peaksLost = std::max<std::int64_t>(0, needed - static_cast<std::int64_t>(peaks_.size()));
}

}  // namespace magda::engine
