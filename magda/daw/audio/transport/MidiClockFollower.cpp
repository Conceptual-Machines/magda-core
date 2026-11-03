// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MidiClockFollower.hpp"

#include <cmath>

#include "../../core/TempoUtils.hpp"
#include "../../project/ProjectManager.hpp"

namespace magda {

namespace {

constexpr int kTempoIntervalMs = 100;

/// The project tempo follows in tenths of a BPM, once the clock is this far from it.
constexpr double kTempoStep = 0.1;
constexpr double kTempoTolerance = 0.07;

/// The clock in the millisecond counter's seconds, which JUCE stamps an input's messages in.
double nowSeconds() {
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

}  // namespace

MidiClockFollower::MidiClockFollower(Hooks hooks) : hooks_(std::move(hooks)) {
    Config::getInstance().addListener(this);
    configChanged();
}

MidiClockFollower::~MidiClockFollower() {
    Config::getInstance().removeListener(this);
    stopTimer();
    MidiBridge::getInstance().setMidiClockInput({}, nullptr);
    cancelPendingUpdate();
}

void MidiClockFollower::configChanged() {
    const auto& input = Config::getInstance().getMidiClockInput();
    if (input == input_)
        return;

    // The last input's callback is over before the tracker is cleared under it.
    auto& midi = MidiBridge::getInstance();
    midi.setMidiClockInput({}, nullptr);
    tracker_.reset();
    shownTempo_.reset();

    input_ = input;
    if (isEnabled()) {
        midi.setMidiClockInput(juce::String(input_), this);
        startTimer(kTempoIntervalMs);
    } else {
        stopTimer();
    }
    if (onStateChanged)
        onStateChanged();
}

void MidiClockFollower::onMidiClock(const juce::MidiMessage& message) {
    if (message.isMidiClock()) {
        const auto now = nowSeconds();
        const auto stamped = message.getTimeStamp();
        tracker_.clock(std::abs(stamped - now) < 1.0 ? stamped : now);
        return;
    }

    if (message.isMidiStart())
        tracker_.start();
    else if (message.isMidiContinue())
        tracker_.resume();
    else if (message.isMidiStop())
        tracker_.stop();
    else if (message.isSongPositionPointer())
        tracker_.songPosition(message.getSongPositionPointerMidiBeat());
    else
        return;
    triggerAsyncUpdate();
}

std::optional<double> MidiClockFollower::beatAfter(std::chrono::microseconds ahead,
                                                   double /*quantum*/) {
    return tracker_.beatAt(nowSeconds() + std::chrono::duration<double>(ahead).count());
}

void MidiClockFollower::handleAsyncUpdate() {
    using Kind = MidiClockTracker::Event::Kind;
    while (const auto event = tracker_.takeEvent()) {
        switch (event->kind) {
            case Kind::play:
                if (hooks_.playFrom)
                    hooks_.playFrom(event->beat);
                break;
            case Kind::stop:
                if (hooks_.stopPlaying)
                    hooks_.stopPlaying();
                break;
            case Kind::locate:
                if (hooks_.locate)
                    hooks_.locate(event->beat);
                break;
        }
    }
}

void MidiClockFollower::timerCallback() {
    std::optional<double> tempo;
    if (const auto seconds = tracker_.secondsPerClock(nowSeconds()))
        tempo = 60.0 / (*seconds * MidiClockTracker::kClocksPerBeat);

    if (tempo.has_value() != shownTempo_.has_value() ||
        (tempo && std::abs(*tempo - *shownTempo_) >= kTempoStep / 2.0)) {
        shownTempo_ = tempo;
        if (onStateChanged)
            onStateChanged();
    }

    if (!tempo || !hooks_.setProjectTempo)
        return;
    const auto project = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (std::abs(*tempo - project) > kTempoTolerance)
        hooks_.setProjectTempo(clampBpm(std::round(*tempo / kTempoStep) * kTempoStep));
}

}  // namespace magda
