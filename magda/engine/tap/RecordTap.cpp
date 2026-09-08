#include "tap/RecordTap.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace magda::engine {

void RecordTap::open(double startBeat) {
    const Change change(*this);

    pass_.fetch_add(1, std::memory_order_relaxed);
    startBeat_.store(startBeat, std::memory_order_relaxed);
    lengthBeats_.store(0.0, std::memory_order_relaxed);
    numNotes_.store(0, std::memory_order_relaxed);
    notesLost_.store(0, std::memory_order_relaxed);
    peaksNeeded_.store(0, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_relaxed);
}

void RecordTap::writeSlot(std::size_t slot, int noteNumber, int velocity, double beat) {
    auto& note = notes_[slot];
    note.identity.store(packIdentity(noteNumber, velocity), std::memory_order_relaxed);
    note.startBeat.store(beat, std::memory_order_relaxed);
    note.lengthBeats.store(0.0, std::memory_order_relaxed);
}

std::size_t RecordTap::addNote(int noteNumber, int velocity, double beat) {
    const auto at = numNotes_.load(std::memory_order_relaxed);

    // A tap nobody draws holds no notes and has lost none: what was never asked
    // for is not a shortfall.
    if (notes_.empty())
        return kNoSlot;

    if (at >= notes_.size()) {
        notesLost_.fetch_add(1, std::memory_order_relaxed);
        return kNoSlot;
    }

    const Change change(*this);
    writeSlot(at, noteNumber, velocity, beat);
    numNotes_.store(at + 1, std::memory_order_relaxed);
    return at;
}

void RecordTap::replaceNote(std::size_t slot, int noteNumber, int velocity, double beat) {
    if (slot >= notes_.size())
        return;

    const Change change(*this);
    writeSlot(slot, noteNumber, velocity, beat);
}

void RecordTap::noteReaches(std::size_t slot, double endBeat) {
    if (slot >= notes_.size())
        return;

    auto& note = notes_[slot];
    const auto start = note.startBeat.load(std::memory_order_relaxed);
    note.lengthBeats.store(std::max(0.0, endBeat - start), std::memory_order_relaxed);
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

bool RecordTap::read(Reading& into) const {
    for (auto attempt = 0; attempt < kReadAttempts; ++attempt) {
        const auto revision = revision_.load(std::memory_order_acquire);
        if ((revision & kChanging) != 0U)
            continue;

        const auto recording = recording_.load(std::memory_order_relaxed);
        const auto pass = pass_.load(std::memory_order_relaxed);
        const auto startBeat = startBeat_.load(std::memory_order_relaxed);
        const auto notesLost = notesLost_.load(std::memory_order_relaxed);
        const auto numNotes = std::min(numNotes_.load(std::memory_order_relaxed), notes_.size());

        // Inside the check, not after it. A length is only measured from a
        // start while the pass that has both of them stands: a wrap taken
        // between the two would put the next pass's length -- nothing, at the
        // moment it opens -- against the last one's start.
        const auto lengthBeats = lengthBeats_.load(std::memory_order_relaxed);

        into.staging.clear();
        for (std::size_t at = 0; at < numNotes; ++at) {
            const auto identity = notes_[at].identity.load(std::memory_order_relaxed);
            into.staging.push_back({static_cast<int>(identity & 0x7fU),
                                    static_cast<int>((identity >> 8U) & 0x7fU),
                                    notes_[at].startBeat.load(std::memory_order_relaxed),
                                    notes_[at].lengthBeats.load(std::memory_order_relaxed)});
        }

        std::atomic_thread_fence(std::memory_order_acquire);
        if (revision_.load(std::memory_order_relaxed) != revision)
            continue;

        into.recording = recording;
        into.material = material_;
        into.target = settings_.target;
        into.scene = settings_.scene;
        into.pass = pass;
        into.startBeat = startBeat;
        into.notesLost = notesLost;
        into.lengthBeats = lengthBeats;
        into.notes.swap(into.staging);

        const auto needed = peaksNeeded_.load(std::memory_order_acquire);
        const auto numPeaks =
            static_cast<std::size_t>(std::min<std::int64_t>(needed, peaks_.size()));

        into.peaks.resize(numPeaks);
        for (std::size_t at = 0; at < numPeaks; ++at)
            into.peaks[at] = unpackPeak(peaks_[at].load(std::memory_order_relaxed));

        into.peaksLost =
            std::max<std::int64_t>(0, needed - static_cast<std::int64_t>(peaks_.size()));
        return true;
    }

    return false;
}

}  // namespace magda::engine
