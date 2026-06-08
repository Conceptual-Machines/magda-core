#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <string>
#include <vector>

#include "../../../agents/mixing_agent.hpp"
#include "../../core/TypeIds.hpp"

namespace magda {

class TracktionEngineWrapper;

namespace daw::audio {

/**
 * @brief Offline whole-mix analysis driver (#886).
 *
 * Renders the mix offline (no playback), measures it with the production DSP via
 * MixAnalysisInput, and hands the result to MixAnalysisAgent -- the chosen path
 * for "analyse the mix" (render, do NOT make the user play through).
 *
 * The whole mix is the maximally relational case: every track judged against
 * every other (masking is already pairwise across the set), so there is no
 * "whole-mix vs relational" mode -- just a track set (default: all) and a depth.
 *
 *  - Shallow: ONE render pass of the summed mix -> fingerprint -> agent. Fast.
 *  - Deep:    master pass + each track isolated (N+1 passes) -> full per-track
 *             input (masking + tonal balance + timeline) -> agent. Scales with
 *             track count.
 *
 * start() is called on the message thread: it does the edit-mutating setup
 * (stop transport, free playback context, enable plugins, prepare for offline
 * rendering), then runs the render passes + LLM call on a background thread,
 * reporting progress and the final result back on the message thread. The driver
 * owns itself and cleans up when done.
 *
 * Memory note: Deep currently holds every rendered stem buffer resident at once
 * (MixAnalysisInput::build measures them together). Fine for stem sessions; a
 * long/many-track project is heavy. Bounding this (measure-and-free per stem, or
 * the single-pass tap render) is the documented follow-up.
 */
class OfflineMixAnalysis {
  public:
    enum class Depth {
        Shallow,  ///< measure the summed mix only (1 render pass)
        Deep      ///< measure each track + the master (N+1 render passes)
    };

    enum class RangeMode {
        WholeEdit,  ///< render the entire edit
        LoopRange   ///< render only the transport loop range (else falls back to whole edit)
    };

    struct Request {
        Depth depth = Depth::Deep;
        RangeMode range = RangeMode::WholeEdit;
        std::vector<magda::TrackId>
            trackSet;          ///< tracks to isolate (Deep); empty => all audio tracks
        float bpm = 0.0f;      ///< project tempo (0 = omit)
        std::string genre;     ///< project genre (empty = omit)
        std::string question;  ///< optional user question (empty = general assessment)
        int numSegments = 16;  ///< master timeline slices (Deep)
    };

    /// Human-readable progress line, delivered on the message thread.
    using ProgressFn = std::function<void(const juce::String&)>;
    /// Final agent result, delivered on the message thread.
    using CompletionFn = std::function<void(MixAnalysisAgent::Result)>;

    /**
     * Kick off an offline analysis. Call on the MESSAGE thread. Returns
     * immediately; onProgress / onComplete fire on the message thread. If the
     * engine has no edit, onComplete is called synchronously with an error.
     */
    static void start(TracktionEngineWrapper& engine, Request request, ProgressFn onProgress,
                      CompletionFn onComplete);
};

}  // namespace daw::audio
}  // namespace magda
