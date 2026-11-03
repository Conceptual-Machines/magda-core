// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <optional>
#include <string>

#include "../../core/Config.hpp"
#include "../../engine/PhaseSource.hpp"
#include "../MidiBridge.hpp"
#include "MidiClockTracker.hpp"

/**
 * @file MidiClockFollower.hpp
 * @brief MAGDA following an external MIDI clock on the input Config names (#3190).
 */

namespace magda {

/**
 * @brief Takes the project tempo from the incoming clock, plays, stops and locates as it says,
 *        and is the phase the engine follows meanwhile.
 *
 * Message thread, but for the clock's own messages. Turned on by naming an input, which turns
 * Ableton Link off: whichever is on is the engine's phase source, and the window asks which.
 * While on, tempo automation is ignored, as under Link.
 */
class MidiClockFollower : public PhaseSource,
                          private MidiClockListener,
                          private ConfigListener,
                          private juce::AsyncUpdater,
                          private juce::Timer {
  public:
    struct Hooks {
        /// Set the project tempo as an edit would.
        std::function<void(double)> setProjectTempo;
        /// Start the transport at a beat, restarting it if it plays.
        std::function<void(double)> playFrom;
        std::function<void()> stopPlaying;
        /// Move the stopped transport to a beat.
        std::function<void(double)> locate;
    };

    explicit MidiClockFollower(Hooks hooks);
    ~MidiClockFollower() override;

    MidiClockFollower(const MidiClockFollower&) = delete;
    MidiClockFollower& operator=(const MidiClockFollower&) = delete;

    bool isEnabled() const {
        return !input_.empty();
    }

    /** @brief The tempo the clock arrives at, as last measured; nothing while none arrives. */
    std::optional<double> receivedTempo() const {
        return shownTempo_;
    }

    std::optional<double> beatAfter(std::chrono::microseconds ahead, double quantum) override;
    bool joinsOnStart() const override {
        return true;
    }

    /// Called when following is switched on or off, or the clock starts or stops arriving.
    std::function<void()> onStateChanged;

  private:
    void onMidiClock(const juce::MidiMessage& message) override;
    void configChanged() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    Hooks hooks_;
    std::string input_;
    MidiClockTracker tracker_;
    std::optional<double> shownTempo_;
};

}  // namespace magda
