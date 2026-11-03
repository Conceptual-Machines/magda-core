// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

/**
 * @file MidiClockTracker.hpp
 * @brief What an incoming MIDI clock says: its tempo, its beat, and its starts and stops (#3190).
 */

namespace magda {

/**
 * @brief Follows one MIDI clock: a line fitted through its last beat of clocks gives the tempo and
 *        the beat, and Start, Continue, Stop and Song Position move its transport.
 *
 * One writer, the MIDI thread. @ref beatAt and @ref secondsPerClock read from any thread without
 * waiting; @ref takeEvent is the message thread's.
 */
class MidiClockTracker {
  public:
    static constexpr int kClocksPerBeat = 24;
    /// Clocks the line is fitted through.
    static constexpr int kWindow = 24;
    /// A longer silence is a clock that stopped: its tempo is measured afresh.
    static constexpr double kLostSeconds = 0.25;

    /** @brief What the transport should do, oldest first. */
    struct Event {
        enum class Kind { play, stop, locate };
        Kind kind = Kind::stop;
        /// Where to play from or locate to.
        double beat = 0.0;
    };

    /// A clock arrived at @p seconds. MIDI thread, like the four below.
    void clock(double seconds);
    void start();
    void resume();
    void stop();
    void songPosition(int sixteenths);

    /// The clock's beat at @p seconds, while it runs and its tempo is known.
    std::optional<double> beatAt(double seconds) const;

    /// Seconds between clocks, once a whole window of them has arrived and none is overdue.
    std::optional<double> secondsPerClock(double nowSeconds) const;

    std::optional<Event> takeEvent();

    /// Forget everything. Only while nothing calls the MIDI-thread side.
    void reset();

  private:
    /** @brief One reading of the clock, as the readers take it. */
    struct Reading {
        bool running = false;
        /// The clock whose arrival is @ref startBeat: at a start, the next one to arrive.
        double startClock = 0.0;
        double startBeat = 0.0;
        /// The fitted line: through clock @ref fitClock at @ref fitTime, at this slope. Zero
        /// slope until two clocks fit.
        double fitClock = 0.0;
        double fitTime = 0.0;
        double secondsPerClock = 0.0;
        double lastClock = -1.0;
        double lastTime = 0.0;
        /// Fitted through a whole window.
        bool settled = false;
    };

    void push(Event event);
    void publish();
    Reading read() const;
    void fit();

    // The writer's own.
    std::array<double, kWindow> times_{};
    std::int64_t clocks_ = 0;
    int fitted_ = 0;
    Reading state_;
    /// Where a stopped clock stands, which Continue plays from.
    double positionBeat_ = 0.0;

    /** @brief @ref state_ as published: a seqlock like TransportClock::syncPoint, odd while
     * written. */
    struct Shared {
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<bool> running{false};
        std::atomic<double> startClock{0.0};
        std::atomic<double> startBeat{0.0};
        std::atomic<double> fitClock{0.0};
        std::atomic<double> fitTime{0.0};
        std::atomic<double> secondsPerClock{0.0};
        std::atomic<double> lastClock{-1.0};
        std::atomic<double> lastTime{0.0};
        std::atomic<bool> settled{false};
    };
    Shared shared_;

    static constexpr int kEventCapacity = 64;
    juce::AbstractFifo events_{kEventCapacity};
    std::array<Event, kEventCapacity> eventSlots_{};
};

}  // namespace magda
