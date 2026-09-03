#include "param/ModLfo.hpp"

#include <algorithm>
#include <cmath>

#include "core/ModCurve.hpp"

namespace magda::engine {

namespace {

/// A rate this low is a period longer than any session, and it is what a
/// modulated rate arriving at the bottom of its range would otherwise divide
/// by. Well below MAGDA's own slider (0.05 Hz) and the fork's (0.01 Hz), so it
/// is a guard rather than a range.
constexpr double kMinHz = 1.0e-4;

/// A loop region narrower than this is a region the user has collapsed rather
/// than one they want repeated, and dividing the cycle by it would spin.
constexpr float kMinLoopLength = 1.0e-4f;

/// The fractional part, for a position that is only ever positive here.
double fractionOf(double cycles) {
    return cycles - std::floor(cycles);
}

/// Into [0, 1), which is where a phase has to be before a shape is read at it.
float wrapPhase(float phase) {
    phase -= std::floor(phase);
    return phase >= 1.0f ? 0.0f : phase;
}

bool loopsInRegion(const LfoSettings& settings) {
    // A drawn curve only. The loop region is MSEG's, and the built-in
    // waveforms have no intro to play once before settling into one.
    return settings.wave == magda::LFOWaveform::Custom && settings.useLoopRegion &&
           (settings.loopEnd - settings.loopStart) > kMinLoopLength;
}

}  // namespace

double barFractionOf(int rateType) {
    constexpr double dot = 1.5;
    constexpr double triplet = 2.0 / 3.0;

    switch (static_cast<magda::ModRateType>(rateType)) {
        case magda::ModRateType::SixteenBars:
            return 16.0;
        case magda::ModRateType::EightBars:
            return 8.0;
        case magda::ModRateType::FourBars:
            return 4.0;
        case magda::ModRateType::TwoBars:
            return 2.0;
        case magda::ModRateType::Bar:
            return 1.0;
        case magda::ModRateType::DottedHalf:
            return 1.0 / 2.0 * dot;
        case magda::ModRateType::Half:
            return 1.0 / 2.0;
        case magda::ModRateType::TripletHalf:
            return 1.0 / 2.0 * triplet;
        case magda::ModRateType::DottedQuarter:
            return 1.0 / 4.0 * dot;
        case magda::ModRateType::Quarter:
            return 1.0 / 4.0;
        case magda::ModRateType::TripletQuarter:
            return 1.0 / 4.0 * triplet;
        case magda::ModRateType::DottedEighth:
            return 1.0 / 8.0 * dot;
        case magda::ModRateType::Eighth:
            return 1.0 / 8.0;
        case magda::ModRateType::TripletEighth:
            return 1.0 / 8.0 * triplet;
        case magda::ModRateType::DottedSixteenth:
            return 1.0 / 16.0 * dot;
        case magda::ModRateType::Sixteenth:
            return 1.0 / 16.0;
        case magda::ModRateType::TripletSixteenth:
            return 1.0 / 16.0 * triplet;
        case magda::ModRateType::DottedThirtySecond:
            return 1.0 / 32.0 * dot;
        case magda::ModRateType::ThirtySecond:
            return 1.0 / 32.0;
        case magda::ModRateType::TripletThirtySecond:
            return 1.0 / 32.0 * triplet;
        case magda::ModRateType::DottedSixtyFourth:
            return 1.0 / 64.0 * dot;
        case magda::ModRateType::SixtyFourth:
            return 1.0 / 64.0;
        case magda::ModRateType::TripletSixtyFourth:
            return 1.0 / 64.0 * triplet;
        case magda::ModRateType::Hertz:
            break;
    }

    // Hertz is not a division, and neither is an ordinal from a project this
    // build does not know. A bar is what a modifier with no division falls
    // back to, which is the fork's answer as well.
    return 1.0;
}

double cycleBeats(int rateType, int numerator, int denominator) {
    // A bar in quarter notes, which is what a beat is here. The signature's
    // own note value is the denominator's, so six eight is three quarter notes
    // rather than six.
    const auto barBeats = 4.0 * std::max(numerator, 1) / std::max(denominator, 1);
    return barFractionOf(rateType) * barBeats;
}

int rateTypeFromLaneValue(float laneValue) {
    const auto shifted = static_cast<int>(std::lround(laneValue)) + 1;
    return std::clamp(shifted, static_cast<int>(magda::ModRateType::SixteenBars),
                      static_cast<int>(magda::ModRateType::TripletSixtyFourth));
}

float laneValueFromRateType(int rateType) {
    return static_cast<float>(
        std::max(rateType, static_cast<int>(magda::ModRateType::SixteenBars)) - 1);
}

double modBarPosition(const BlockInfo& block, const ModTiming& timing) {
    if (block.tempo != nullptr) {
        // The bar grid the block renders in, which is the section's rather than
        // the one its first beat happens to sit in (BlockInfo::sectionBeat).
        // A signature the epsilon swallowed would otherwise run this whole
        // block at the old bar fraction instead of restarting on the new bar:
        // four four to three four at the boundary is a bar-rate LFO a quarter
        // of a cycle out for the length of the block.
        const auto inside = block.sectionBeat();
        const auto grid = block.tempo->barsAndBeatsAt(inside);
        const auto barBeats = 4.0 * grid.numerator / std::max(1, grid.denominator);

        // Back to the block's own first sample, under that grid. The phase is
        // the value the block opens on, and the section is only what says how
        // long a bar is there.
        const auto atSectionBeat = grid.bar + (grid.beat / std::max(grid.numerator, 1));
        return atSectionBeat - ((inside - block.beats.start) / std::max(barBeats, 1.0e-6));
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
    // Asked about the section the block renders in rather than about its first
    // beat, which is not reliably in it (BlockInfo::sectionBeat).
    if (block.tempo != nullptr) {
        const auto inside = block.sectionBeat();
        const auto signature = block.tempo->barsAndBeatsAt(inside);
        timing.bpm = block.tempo->bpmAt(inside);
        timing.numerator = signature.numerator;
        timing.denominator = signature.denominator;
    }

    if (!(timing.bpm > 0.0))
        timing.bpm = 120.0;
    timing.numerator = std::max(timing.numerator, 1);
    timing.denominator = std::max(timing.denominator, 1);

    return timing;
}

float lfoShapeAt(const LfoSettings& settings, std::span<const magda::CurvePointData> curve,
                 float phase) {
    return magda::modcurve::shapeAt(settings.wave, settings.preset, curve, phase);
}

void restartLfo(LfoState& state, const LfoSettings& settings) {
    if (settings.sync != ModSync::Note)
        return;

    state.cycles = 0.0;
    state.completed = false;
    state.started = true;
}

float advanceLfo(LfoState& state, const LfoSettings& settings,
                 std::span<const magda::CurvePointData> curve, const BlockInfo& block,
                 const ModTiming& timing) {
    // A fresh state takes its gate from the settings, and so does one whose
    // trigger mode has changed underneath it: the gate belongs to the triggers
    // of the mode that opened it, and a mode change retires all of them. An
    // audio-triggered LFO sits shut between hits, and switching it to free
    // running would otherwise leave it shut for ever, because nothing in the
    // new mode ever opens a gate. The held-note count goes with it, for the
    // same reason.
    if (!state.started || state.trigger != settings.trigger) {
        state.started = true;
        state.trigger = settings.trigger;
        state.gated = settings.startGated;
        state.heldNotes = 0;
    }

    // A latch belongs to the setting that made it. Turning one-shot off clears
    // it, so turning it back on plays the cycle again rather than resuming a
    // hold from before, which is what the fork's snapshot does on the same
    // edit (CurveSnapshotHolder::update).
    if (!settings.oneShot)
        state.completed = false;

    const double hz = std::max(static_cast<double>(settings.rate.hz), kMinHz);
    const double beatsPerCycle =
        std::max(cycleBeats(settings.rate.rateType, timing.numerator, timing.denominator), 1.0e-6);

    // A timeline-locked LFO is a function of where the block is rather than of
    // how many blocks have gone by, which is what puts two of them at one rate
    // in phase with each other and with the bar however playback got there.
    if (settings.sync == ModSync::Transport) {
        state.cycles = settings.tempoSync
                           ? modBarPosition(block, timing) / barFractionOf(settings.rate.rateType)
                           : block.seconds.start * hz;
    }

    const double cycles = state.cycles;
    const bool looping = loopsInRegion(settings);
    const bool holding = settings.oneShot && !looping && (state.completed || cycles >= 1.0);

    float phase = 0.0f;
    float shape = 0.0f;

    if (holding) {
        // Through, and parked where the cycle ended. The phase is published at
        // the end rather than wherever the ramp would have got to, because
        // that is where the curve the user can see is being held.
        state.completed = true;
        phase = 1.0f;
        shape = magda::modcurve::endValue(settings.wave, settings.preset, curve);
    } else if (looping) {
        // The intro plays once and the region repeats from there. Read off the
        // cumulative position rather than the wrapped phase, because which of
        // the two it is depends on how many cycles have gone by.
        const double length = static_cast<double>(settings.loopEnd - settings.loopStart);
        const double start = static_cast<double>(settings.loopStart);
        const double effective =
            cycles < start ? cycles : start + std::fmod(cycles - start, length);

        phase = wrapPhase(static_cast<float>(effective));
        shape = lfoShapeAt(settings, curve, phase);
    } else {
        phase = wrapPhase(static_cast<float>(fractionOf(cycles)) + settings.phaseOffset);
        shape = lfoShapeAt(settings, curve, phase);
    }

    // A curve drawn as a level is applied as the amount it takes away, so that
    // an inactive modifier, which outputs 0, means "no attenuation" rather
    // than "silence" (see LfoSettings::invertOutput).
    float value = settings.invertOutput ? 1.0f - shape : shape;

    // The gate is the last word, and it is flat zero rather than a floor: the
    // whole modulation system reads 0 as a modifier doing nothing.
    if (state.gated)
        value = 0.0f;

    // A trigger asked for one block of nothing, and this is the first block a
    // device can see (LfoState::forceZero). The phase is published where the
    // restart put it, so the editor's dot is already back at the top, and it
    // is not advanced, so the shape resumes from its start on the next block
    // rather than a block into itself.
    const bool zeroed = state.forceZero;
    if (zeroed) {
        state.forceZero = false;
        value = 0.0f;
    }

    state.phase = phase;
    state.value = value;

    // Moved on for the next block, after this one's value has been settled,
    // which is the order the fork's timer uses: the value a block renders with
    // is the value at its first sample.
    if (settings.sync != ModSync::Transport && !holding && !zeroed) {
        const double blockSeconds =
            static_cast<double>(std::max(block.numSamples, 0)) / timing.sampleRate;
        const double periodSeconds =
            settings.tempoSync ? beatsPerCycle * 60.0 / timing.bpm : 1.0 / hz;

        state.cycles += blockSeconds / std::max(periodSeconds, 1.0e-9);
    }

    // Kept bounded, so an LFO left running for hours is as precise as one that
    // started a moment ago. A one-shot is the exception: how far past the end
    // it is is the whole question it answers.
    if (looping) {
        const double length = static_cast<double>(settings.loopEnd - settings.loopStart);
        const double start = static_cast<double>(settings.loopStart);
        if (state.cycles >= start + length)
            state.cycles = start + std::fmod(state.cycles - start, length);
    } else if (!settings.oneShot && state.cycles >= 1.0) {
        state.cycles = fractionOf(state.cycles);
    }

    return value;
}

}  // namespace magda::engine
