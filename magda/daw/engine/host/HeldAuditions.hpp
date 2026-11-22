// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include "core/TypeIds.hpp"

namespace magda::daw::engine_host {

/**
 * @brief Previews on a track that renders ahead, held until a plan plays it live (#1898).
 *
 * A track rendered ahead has no live MIDI input, so a preview reaching it is lost. The
 * preview promotes its track instead, and its notes wait, in order, for the plan that
 * renders it at the callback. Message thread.
 */
class HeldAuditions {
  public:
    /// Whether @p message plays now. Not while its track is not live or has notes waiting.
    bool admit(TrackId trackId, const juce::MidiMessage& message, bool trackIsLive);

    /// Tracks a preview promoted, which the next plan renders at the callback.
    const std::set<TrackId>& promoted() const {
        return promoted_;
    }

    /// Whether a promotion is waiting for a plan.
    bool waiting() const {
        return !held_.empty();
    }

    /// Every held note, in the order it arrived. After the plan that plays them is live.
    void release(const std::function<void(TrackId, const juce::MidiMessage&)>& play);

    /// The selection moved off the previewed tracks: they render ahead again.
    void forgetPromotions() {
        promoted_.clear();
    }

    void clear() {
        held_.clear();
        promoted_.clear();
    }

  private:
    std::vector<std::pair<TrackId, juce::MidiMessage>> held_;
    std::set<TrackId> promoted_;
};

}  // namespace magda::daw::engine_host
