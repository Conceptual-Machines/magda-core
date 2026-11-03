// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MidiClockTracker.hpp"

#include <algorithm>

namespace magda {

void MidiClockTracker::clock(double seconds) {
    if (fitted_ > 0) {
        const auto last = times_[static_cast<std::size_t>((clocks_ - 1) % kWindow)];
        if (seconds - last > kLostSeconds)
            fitted_ = 0;
        seconds = std::max(seconds, last);
    }

    times_[static_cast<std::size_t>(clocks_ % kWindow)] = seconds;
    ++clocks_;
    fitted_ = std::min(fitted_ + 1, kWindow);
    fit();
    state_.lastClock = static_cast<double>(clocks_ - 1);
    state_.lastTime = seconds;
    publish();
}

void MidiClockTracker::fit() {
    const auto first = clocks_ - fitted_;
    const auto time = [this](std::int64_t clock) {
        return times_[static_cast<std::size_t>(clock % kWindow)];
    };

    // Seconds from the first clock fitted, so the sums keep their precision.
    const auto origin = time(first);
    auto meanClock = 0.0;
    auto meanTime = 0.0;
    for (auto clock = first; clock < clocks_; ++clock) {
        meanClock += static_cast<double>(clock - first);
        meanTime += time(clock) - origin;
    }
    meanClock /= fitted_;
    meanTime /= fitted_;

    auto covariance = 0.0;
    auto variance = 0.0;
    for (auto clock = first; clock < clocks_; ++clock) {
        const auto dx = static_cast<double>(clock - first) - meanClock;
        covariance += dx * (time(clock) - origin - meanTime);
        variance += dx * dx;
    }

    state_.fitClock = static_cast<double>(first) + meanClock;
    state_.fitTime = origin + meanTime;
    state_.secondsPerClock = variance > 0.0 && covariance > 0.0 ? covariance / variance : 0.0;
    state_.settled = fitted_ == kWindow && state_.secondsPerClock > 0.0;
}

void MidiClockTracker::start() {
    positionBeat_ = 0.0;
    resume();
}

void MidiClockTracker::resume() {
    state_.running = true;
    state_.startClock = static_cast<double>(clocks_);
    state_.startBeat = positionBeat_;
    publish();
    push({.kind = Event::Kind::play, .beat = positionBeat_});
}

void MidiClockTracker::stop() {
    if (state_.running)
        positionBeat_ = state_.startBeat +
                        ((static_cast<double>(clocks_) - state_.startClock) / kClocksPerBeat);
    state_.running = false;
    publish();
    push({.kind = Event::Kind::stop});
}

void MidiClockTracker::songPosition(int sixteenths) {
    positionBeat_ = sixteenths / 4.0;

    // A running clock counts on from here; the transport is located only while it stands.
    if (state_.running) {
        state_.startClock = static_cast<double>(clocks_);
        state_.startBeat = positionBeat_;
        publish();
        return;
    }
    push({.kind = Event::Kind::locate, .beat = positionBeat_});
}

std::optional<double> MidiClockTracker::beatAt(double seconds) const {
    const auto reading = read();
    if (!reading.running || !(reading.secondsPerClock > 0.0) ||
        reading.lastClock < reading.startClock || seconds - reading.lastTime > kLostSeconds)
        return std::nullopt;

    const auto clock = reading.fitClock + ((seconds - reading.fitTime) / reading.secondsPerClock);
    return reading.startBeat + ((clock - reading.startClock) / kClocksPerBeat);
}

std::optional<double> MidiClockTracker::secondsPerClock(double nowSeconds) const {
    const auto reading = read();
    if (!reading.settled || nowSeconds - reading.lastTime > kLostSeconds)
        return std::nullopt;
    return reading.secondsPerClock;
}

std::optional<MidiClockTracker::Event> MidiClockTracker::takeEvent() {
    const auto scope = events_.read(1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return std::nullopt;
    return eventSlots_[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1
                                                                     : scope.startIndex2)];
}

void MidiClockTracker::reset() {
    times_ = {};
    clocks_ = 0;
    fitted_ = 0;
    state_ = {};
    positionBeat_ = 0.0;
    publish();
    events_.reset();
}

void MidiClockTracker::push(Event event) {
    // Full only if the message thread has stalled for dozens of starts and stops: the newest wait.
    const auto scope = events_.write(1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return;
    eventSlots_[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1
                                                              : scope.startIndex2)] = event;
}

void MidiClockTracker::publish() {
    const auto writing = shared_.sequence.load(std::memory_order_relaxed) + 1;
    shared_.sequence.store(writing, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    shared_.running.store(state_.running, std::memory_order_relaxed);
    shared_.startClock.store(state_.startClock, std::memory_order_relaxed);
    shared_.startBeat.store(state_.startBeat, std::memory_order_relaxed);
    shared_.fitClock.store(state_.fitClock, std::memory_order_relaxed);
    shared_.fitTime.store(state_.fitTime, std::memory_order_relaxed);
    shared_.secondsPerClock.store(state_.secondsPerClock, std::memory_order_relaxed);
    shared_.lastClock.store(state_.lastClock, std::memory_order_relaxed);
    shared_.lastTime.store(state_.lastTime, std::memory_order_relaxed);
    shared_.settled.store(state_.settled, std::memory_order_relaxed);

    shared_.sequence.store(writing + 1, std::memory_order_release);
}

MidiClockTracker::Reading MidiClockTracker::read() const {
    for (;;) {
        const auto before = shared_.sequence.load(std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;

        const Reading reading{.running = shared_.running.load(std::memory_order_relaxed),
                              .startClock = shared_.startClock.load(std::memory_order_relaxed),
                              .startBeat = shared_.startBeat.load(std::memory_order_relaxed),
                              .fitClock = shared_.fitClock.load(std::memory_order_relaxed),
                              .fitTime = shared_.fitTime.load(std::memory_order_relaxed),
                              .secondsPerClock =
                                  shared_.secondsPerClock.load(std::memory_order_relaxed),
                              .lastClock = shared_.lastClock.load(std::memory_order_relaxed),
                              .lastTime = shared_.lastTime.load(std::memory_order_relaxed),
                              .settled = shared_.settled.load(std::memory_order_relaxed)};

        std::atomic_thread_fence(std::memory_order_acquire);
        if (shared_.sequence.load(std::memory_order_relaxed) == before)
            return reading;
    }
}

}  // namespace magda
