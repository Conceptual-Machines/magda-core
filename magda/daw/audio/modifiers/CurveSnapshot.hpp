#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <span>

#include "../../core/ModCurve.hpp"
#include "../../core/ModInfo.hpp"

namespace magda {

/**
 * @brief Fixed-size curve data safe for audio-thread reading.
 *
 * Mirrors the variable-length CurvePointData vector from ModInfo but in a
 * fixed-size std::array so there are no heap allocations on the audio thread.
 */
struct CurveSnapshot {
    static constexpr int kMaxPoints = 64;

    /// The model's own point, so the snapshot is a fixed-size copy of the
    /// drawn curve rather than a second description of one.
    using Point = CurvePointData;

    std::array<Point, kMaxPoints> points{};
    int count = 0;
    CurvePreset preset = CurvePreset::Triangle;
    bool hasCustomPoints = false;
    bool oneShot = false;
    // Level-envelope curve: the applied output is (1 - curve value). See
    // ModInfo::invertOutput. Applied at the evaluateCallback return, so the
    // stored points and the UI phase indicator stay on the drawn curve.
    bool invertOutput = false;

    // MSEG loop region. When useLoopRegion is set, the intro [0, loopStart)
    // plays once, then [loopStart, loopEnd] repeats indefinitely.
    bool useLoopRegion = false;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;

    /// The drawn points, as a span the shared reading takes.
    std::span<const Point> drawn() const {
        return {points.data(), static_cast<size_t>(count)};
    }

    /**
     * @brief Generate a preset curve value (no custom points).
     *
     * The model's own reading (core/ModCurve.hpp): pure math, no allocations,
     * safe for the audio thread.
     */
    static float evaluatePreset(CurvePreset p, float phase) {
        return magda::modcurve::preset(p, phase);
    }

    /**
     * @brief Return the value at the very end of the curve (for oneshot hold).
     *
     * Returns the last custom point's value, or the preset evaluated at phase 1.0.
     * Avoids the wrap-around interpolation that evaluate(0.999) would do
     * (which would interpolate from the last point back toward the first).
     */
    float endValue() const {
        return magda::modcurve::endValue(LFOWaveform::Custom, preset, drawn());
    }

    /**
     * @brief Map a curve value to the applied modulator output.
     *
     * Identity normally; (1 - value) for level-envelope curves so an inactive
     * modifier (TE forces output 0 when gated / untriggered) means "no
     * attenuation" while the drawn curve reads as the audible level.
     */
    float applyOutput(float value) const {
        return invertOutput ? 1.0f - value : value;
    }

    /**
     * @brief Evaluate the curve at a given phase.
     *
     * The model's own reading (core/ModCurve.hpp), so the editor's preview,
     * this snapshot and the native engine's LFO all draw one curve.
     */
    float evaluate(float phase) const {
        return magda::modcurve::shapeAt(LFOWaveform::Custom, preset, drawn(), phase);
    }
};

/**
 * @brief Double-buffered CurveSnapshot holder for lock-free audio-thread reads.
 *
 * Message thread writes to the inactive buffer then atomically swaps.
 * Audio thread reads the active buffer via the static callback.
 */
struct CurveSnapshotHolder {
    CurveSnapshot buffers[2];
    std::atomic<CurveSnapshot*> active{&buffers[0]};

    // One-shot state: cumulative phase tracks how far through the cycle we are.
    // evaluateCallback adds phase deltas each block on the destination audio thread.
    // When cumulative >= 1.0 the curve has played once → hold at endValue.
    //
    // pendingReset_ is the cross-thread signal: resetOneShot() (source audio thread)
    // sets it to true; evaluateCallback (destination audio thread) consumes it and
    // locally zeroes cumulativePhase_/previousPhase_. This avoids the race where a
    // concurrent evaluateCallback reads stale cumulative and overwrites the zero.
    std::atomic<bool> pendingReset_{false};
    std::atomic<float> cumulativePhase_{0.0f};
    std::atomic<float> previousPhase_{-1.0f};
    std::atomic<bool> oneShotCompleted_{false};
    std::atomic<int> evalLogCount_{0};  // throttle DBG spam in evaluateCallback
    // Last remapped (looped) phase, published for the UI phase indicator so the
    // dot follows the loop region instead of TE's raw 0..1 sweep.
    std::atomic<float> lastEffectivePhase_{0.0f};

    /**
     * @brief Message thread: copy curve data from ModInfo into the inactive
     *        buffer, then swap active pointer.
     */
    void update(const ModInfo& modInfo) {
        // Determine which buffer is inactive
        CurveSnapshot* current = active.load(std::memory_order_acquire);
        CurveSnapshot* back = (current == &buffers[0]) ? &buffers[1] : &buffers[0];

        // Fill the back buffer
        back->preset = modInfo.curvePreset;
        back->hasCustomPoints = !modInfo.curvePoints.empty();
        back->oneShot = modInfo.oneShot;
        back->invertOutput = modInfo.invertOutput;
        back->useLoopRegion = modInfo.useLoopRegion;
        back->loopStart = modInfo.loopStart;
        back->loopEnd = modInfo.loopEnd;
        back->count =
            std::min(static_cast<int>(modInfo.curvePoints.size()), CurveSnapshot::kMaxPoints);

        for (int i = 0; i < back->count; ++i)
            back->points[static_cast<size_t>(i)] = modInfo.curvePoints[static_cast<size_t>(i)];

        // Swap: audio thread will now read from the newly written buffer
        active.store(back, std::memory_order_release);

        // If oneShot was turned off, reset completed state
        if (!modInfo.oneShot)
            oneShotCompleted_.store(false, std::memory_order_release);
    }

    /**
     * @brief Reset one-shot state so the LFO plays through one more cycle.
     *
     * Call this alongside LFOModifier::triggerNoteOn() when retriggering.
     * Safe to call from any thread. Sets a pending flag that evaluateCallback
     * consumes on the destination audio thread, avoiding cross-thread races
     * on cumulativePhase_/previousPhase_.
     */
    void resetOneShot() {
        oneShotCompleted_.store(false, std::memory_order_release);
        pendingReset_.store(true, std::memory_order_release);
    }

    /**
     * @brief Static callback wired to LFOModifier::customWaveFunction.
     *
     * Called on the audio thread once per block. userData points to this holder.
     * Loads the active snapshot and evaluates the curve at the given phase.
     * In one-shot mode, holds the end value after the first complete cycle.
     */
    static float evaluateCallback(float phase, void* userData) {
        auto* holder = static_cast<CurveSnapshotHolder*>(userData);
        if (!holder)
            return 0.0f;
        const CurveSnapshot* snap = holder->active.load(std::memory_order_acquire);

        const float loopLen = snap->loopEnd - snap->loopStart;
        const bool looping = snap->useLoopRegion && loopLen > 1.0e-4f;

        // Both one-shot completion tracking and MSEG looping integrate the
        // incoming phase into a cumulative position. Looping additionally
        // remaps that position so the [loopStart, loopEnd] region repeats once
        // the intro (0 -> loopStart) has played through.
        if (snap->oneShot || looping) {
            // Consume pending reset from resetOneShot() on this thread,
            // so cumulativePhase_/previousPhase_ are only written by one thread.
            bool wasReset = holder->pendingReset_.exchange(false, std::memory_order_acquire);
            if (wasReset) {
                holder->cumulativePhase_.store(0.0f, std::memory_order_relaxed);
                holder->previousPhase_.store(-1.0f, std::memory_order_relaxed);
                holder->oneShotCompleted_.store(false, std::memory_order_relaxed);
                holder->evalLogCount_.store(0, std::memory_order_relaxed);
            }

            // A one-shot without a sustain loop holds its end value once it has
            // played through. With a loop, the region sustains instead.
            if (snap->oneShot && !looping &&
                holder->oneShotCompleted_.load(std::memory_order_relaxed))
                return snap->applyOutput(snap->endValue());

            // Accumulate phase delta to advance the cumulative position.
            float prev = holder->previousPhase_.load(std::memory_order_relaxed);
            holder->previousPhase_.store(phase, std::memory_order_relaxed);

            float cum = holder->cumulativePhase_.load(std::memory_order_relaxed);
            if (prev >= 0.0f) {
                float delta = phase - prev;
                // Normal forward movement: delta is small positive.
                // Phase wrap (0.99 → 0.01): delta is ~-1.0, correct to ~+0.01.
                if (delta < -0.5f)
                    delta += 1.0f;
                if (delta > 0.0f) {
                    cum += delta;
                    // Keep the cumulative position bounded inside the loop so a
                    // long-running LFO never loses float precision.
                    if (looping && cum >= snap->loopEnd)
                        cum = snap->loopStart + std::fmod(cum - snap->loopStart, loopLen);
                    holder->cumulativePhase_.store(cum, std::memory_order_relaxed);
                    if (snap->oneShot && !looping && cum >= 1.0f) {
                        holder->oneShotCompleted_.store(true, std::memory_order_relaxed);
                        return snap->applyOutput(snap->endValue());
                    }
                }
            }

            if (looping) {
                const float eff = (cum < snap->loopStart)
                                      ? cum  // intro segment, plays once
                                      : snap->loopStart + std::fmod(cum - snap->loopStart, loopLen);
                holder->lastEffectivePhase_.store(eff, std::memory_order_relaxed);
                return snap->applyOutput(snap->evaluate(eff));
            }
        }

        return snap->applyOutput(snap->evaluate(phase));
    }
};

}  // namespace magda
