#include "param/ModBridge.hpp"

#include <algorithm>

namespace magda::engine {

using sdk::barBeatsOf;

double modBarsElapsed(const BlockInfo& block, const ModTiming& timing) {
    // A stopped block covers no beats and still has to turn a free-running
    // modifier, so it keeps its own time at the tempo and signature the cursor
    // sits on. One beat, one bpm, one bar length, and no conversion to be wrong
    // about.
    if (!(block.playing && !block.beats.empty())) {
        const auto seconds =
            static_cast<double>(std::max(block.numSamples, 0)) / std::max(timing.sampleRate, 1.0);
        return seconds * timing.bpm / 60.0 /
               std::max(barBeatsOf(timing.numerator, timing.denominator), 1.0e-6);
    }

    // A block assembled by hand carries no map, so its own beat length under
    // the one signature it was given is the only line there is.
    if (block.tempo == nullptr)
        return block.beats.length() /
               std::max(barBeatsOf(timing.numerator, timing.denominator), 1.0e-6);

    // Through the origin in both directions on a shifted block, which is what
    // keeps the map usable there (BlockInfo::beatAtTime says the same about a
    // moment).
    const auto from = block.beats.start + block.materialOrigin.beat;
    const auto to = block.beats.end + block.materialOrigin.beat;

    // A bar is a different number of beats on each side of a signature change,
    // so a block that spans one covers a fraction of each: the map knows where
    // the change is, so the block's bars are the sum over the spans it runs
    // through (#2340).
    double bars = 0.0;
    double at = from;
    for (int guard = 0; guard < 64 && at < to; ++guard) {
        const auto grid = block.tempo->barsAndBeatsAt(at);
        const auto end = std::min(to, block.tempo->signatureEndAfter(at));
        if (!(end > at))
            break;
        bars += (end - at) / std::max(barBeatsOf(grid.numerator, grid.denominator), 1.0e-6);
        at = end;
    }
    return bars;
}

double modBarPosition(const BlockInfo& block, const ModTiming& timing) {
    if (block.tempo != nullptr) {
        // The bar grid the block renders in, which is the one its first sample
        // sounds under rather than the one its first beat happens to sit in
        // (BlockInfo::openingBeat). A signature the epsilon swallowed would
        // otherwise run this whole block at the old bar fraction instead of
        // restarting on the new bar: four four to three four at the boundary is
        // a bar-rate LFO a quarter of a cycle out for the length of the block.
        const auto opening = block.openingBeat();
        const auto grid = block.tempo->barsAndBeatsAt(opening);
        const auto barBeats = 4.0 * grid.numerator / std::max(1, grid.denominator);

        // Back to the block's own first sample, under that grid. A hundredth of
        // a sample at most, since that is all the opening beat is nudged by,
        // but the phase is the value the block opens on and the grid is only
        // what says how long a bar is there.
        const auto atOpeningBeat = grid.bar + (grid.beat / std::max(grid.numerator, 1));
        return atOpeningBeat - ((opening - block.beats.start) / std::max(barBeats, 1.0e-6));
    }

    const auto barBeats = 4.0 * timing.numerator / timing.denominator;
    return block.beats.start / std::max(barBeats, 1.0e-6);
}

ModTiming modTimingFor(const BlockInfo& block, double sampleRate) {
    ModTiming timing;
    timing.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // The map where there is one. A block assembled by hand carries none, and
    // what it gets is what a session that has never seen a transport renders
    // at: 120 in four four, which is the same default TransportState holds.
    //
    // Asked at the beat the block's first sample sounds on rather than at its
    // first beat, which is not reliably in the same section
    // (BlockInfo::openingBeat).
    //
    // The bpm here is what a stopped block keeps time at. A rolling one reads
    // the bars it covers instead (modBarsElapsed), so a block spanning a tempo
    // or signature change is not this reading's problem.
    if (block.tempo != nullptr) {
        const auto opening = block.openingBeat();
        const auto signature = block.tempo->barsAndBeatsAt(opening);
        timing.bpm = block.tempo->bpmAt(opening);
        timing.numerator = signature.numerator;
        timing.denominator = signature.denominator;
    }

    if (!(timing.bpm > 0.0))
        timing.bpm = 120.0;
    timing.numerator = std::max(timing.numerator, 1);
    timing.denominator = std::max(timing.denominator, 1);

    return timing;
}

sdk::ModBlock modBlockFor(const BlockInfo& block, const ModTiming& timing) {
    sdk::ModBlock out;
    out.numSamples = block.numSamples;
    out.playing = block.playing;
    out.secondsStart = block.seconds.start;
    out.barPosition = modBarPosition(block, timing);
    out.barsElapsed = modBarsElapsed(block, timing);
    return out;
}

}  // namespace magda::engine
